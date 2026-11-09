// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraMeshBuilder.h"
#include "DreamNiagaraVertexLayout.h"
#include "Async/ParallelFor.h"
#include "Algo/StableSort.h"

namespace DreamNiagaraMeshBuilderLocal
{
	/** Below this many particles the work stays on the calling thread: a task costs more than it saves. */
	constexpr int32 ParallelMinimum = 256;
	constexpr int32 ParallelBatch = 128;

	/** Makes room for InVertices and InIndices, by tiers; says whether either array grew. */
	bool Reserve(FDreamNiagaraMeshTarget& InOutTarget, int32 InVertices, int32 InIndices)
	{
		bool bGrew = false;
		if (InOutTarget.Vertices->Num() < InVertices)
		{
			InOutTarget.Vertices->SetNumUninitialized(FDreamNiagaraMeshBuilder::GrowCapacity(InVertices));
			bGrew = true;
		}
		if (InOutTarget.Indices->Num() < InIndices)
		{
			InOutTarget.Indices->SetNumUninitialized(FDreamNiagaraMeshBuilder::GrowCapacity(InIndices));
			bGrew = true;
		}
		InOutTarget.bGrew |= bGrew;
		return bGrew;
	}

	FVector3f ToPlane(const FDreamNiagaraBuildSpace& InSpace, const FVector3f& InSimPosition)
	{
		FVector3f Plane = InSpace.SimToPlane.TransformPosition(InSimPosition);
		if (InSpace.bFlattenDepth)
		{
			Plane.X = 0.0f;
		}
		return Plane;
	}

	/** A vector of the simulation on the plane, as (right, up); zero when it points out of the plane. */
	FVector2f DirectionOnPlane(const FDreamNiagaraBuildSpace& InSpace, const FVector3f& InSimVector)
	{
		const FVector3f Plane = InSpace.SimToPlane.TransformVector(InSimVector);
		FVector2f OnPlane(Plane.Y, Plane.Z);
		return OnPlane.Normalize(UE_SMALL_NUMBER) ? OnPlane : FVector2f::ZeroVector;
	}

	FDreamUIMeshVertex MakeVertex(const FDreamNiagaraBuildSpace& InSpace, const FVector3f& InPlane, const FColor& InColor,
		const FVector2f& InUV0, const FVector2f& InUV2, float InPacked, const FVector4f& InDynamic, float InRecord)
	{
		FDreamUIMeshVertex Vertex(InSpace.PlaneToCanvas.TransformPosition(InPlane), InColor);
		Vertex.TextureCoordinate[0] = InUV0;
		Vertex.TextureCoordinate[1] = FVector2f(InRecord, InDynamic.X);
		Vertex.TextureCoordinate[2] = InUV2;
		Vertex.TextureCoordinate[3] = FVector2f(InPacked, InDynamic.Y);
		Vertex.UV4 = FVector2f(InDynamic.Z, InDynamic.W);
		return Vertex;
	}

	FLinearColor Tinted(const FLinearColor& InColor, const FLinearColor& InTint)
	{
		return FLinearColor(InColor.R * InTint.R, InColor.G * InTint.G, InColor.B * InTint.B, InColor.A * InTint.A);
	}
}

int32 FDreamNiagaraMeshBuilder::GrowCapacity(int32 InNeeded)
{
	if (InNeeded <= 64)
	{
		return 64;
	}
	return static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(InNeeded)));
}

int32 FDreamNiagaraMeshBuilder::MaxSprites()
{
	return (LEXUI_MAX_VERTEX_COUNT - 1) / 4;
}

void FDreamNiagaraMeshBuilder::SortSprites(TConstArrayView<FDreamNiagaraSpriteParticle> InParticles, EDreamNiagaraSortMode InMode, TArray<int32>& OutOrder)
{
	OutOrder.SetNumUninitialized(InParticles.Num());
	for (int32 Index = 0; Index < InParticles.Num(); ++Index)
	{
		OutOrder[Index] = Index;
	}
	if (InMode == EDreamNiagaraSortMode::None)
	{
		return;
	}
	// Stable: particles of one key keep the simulation's order, so that a tie does not flicker from frame to frame.
	const bool bAscending = InMode == EDreamNiagaraSortMode::Ascending;
	Algo::StableSort(OutOrder, [InParticles, bAscending](int32 A, int32 B)
	{
		return bAscending ? InParticles[A].SortKey < InParticles[B].SortKey : InParticles[A].SortKey > InParticles[B].SortKey;
	});
}

FVector2f FDreamNiagaraMeshBuilder::SubImageUV(float InFrame, const FIntPoint& InSubImageSize, const FVector2f& InUV)
{
	// As Niagara's sprite vertex factory does it: half a frame of bias against the precision of the division, the column
	// wrapped, the row not (a frame past the sheet's last reads past its bottom, as it does in Niagara).
	const float Columns = static_cast<float>(FMath::Max(1, InSubImageSize.X));
	const float Rows = static_cast<float>(FMath::Max(1, InSubImageSize.Y));
	const float Biased = InFrame + 0.5f;
	const float Column = FMath::Floor(FMath::Fmod(Biased, Columns));
	const float Row = FMath::Floor(Biased / Columns);
	return FVector2f((Column + InUV.X) / Columns, (Row + InUV.Y) / Rows);
}

void FDreamNiagaraMeshBuilder::BuildEmpty(const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget)
{
	using namespace DreamNiagaraMeshBuilderLocal;
	Reserve(InOutTarget, 3, 3);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FDreamUIMeshVertex Vertex(FVector3f::ZeroVector, FColor(0, 0, 0, 0));
		Vertex.TextureCoordinate[1] = FVector2f(InCommon.WidgetRecord, 0.0f);
		(*InOutTarget.Vertices)[Index] = Vertex;
		(*InOutTarget.Indices)[Index] = static_cast<FDreamUIMeshIndex>(Index);
	}
	InOutTarget.NumVertices = 3;
	InOutTarget.NumIndices = 3;
	InOutTarget.Bounds = FBox(ForceInit);
}

void FDreamNiagaraMeshBuilder::BuildSprites(TConstArrayView<FDreamNiagaraSpriteParticle> InParticles, const FDreamNiagaraSpriteSettings& InSettings,
	const FDreamNiagaraBuildSpace& InSpace, const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget)
{
	using namespace DreamNiagaraMeshBuilderLocal;
	TArray<int32> Order;
	SortSprites(InParticles, InSettings.SortMode, Order);
	const int32 NumSprites = FMath::Min(Order.Num(), MaxSprites());
	InOutTarget.NumDropped = Order.Num() - NumSprites;
	if (NumSprites == 0)
	{
		BuildEmpty(InCommon, InOutTarget);
		return;
	}
	Reserve(InOutTarget, NumSprites * 4, NumSprites * 6);
	InOutTarget.NumVertices = NumSprites * 4;
	InOutTarget.NumIndices = NumSprites * 6;

	// The quad's corners in DreamGUI's order -- bottom left, bottom right, top left, top right -- with the texture coordinate
	// each has across the sprite (from its top left, as Niagara's are), and its two triangles, anticlockwise on the plane.
	static const FVector2f CornerUV[4] = { FVector2f(0.0f, 1.0f), FVector2f(1.0f, 1.0f), FVector2f(0.0f, 0.0f), FVector2f(1.0f, 0.0f) };
	static const int32 QuadIndices[6] = { 0, 3, 2, 0, 1, 3 };

	const int32 NumChunks = NumSprites >= ParallelMinimum ? FMath::DivideAndRoundUp(NumSprites, ParallelBatch) : 1;
	TArray<FBox> ChunkBounds;
	ChunkBounds.Init(FBox(ForceInit), NumChunks);
	FDreamUIMeshVertex* const Vertices = InOutTarget.Vertices->GetData();
	FDreamUIMeshIndex* const Indices = InOutTarget.Indices->GetData();
	const float Frames = static_cast<float>(FMath::Max(1, InSettings.SubImageSize.X) * FMath::Max(1, InSettings.SubImageSize.Y));
	(void)Frames;

	ParallelFor(TEXT("DreamNiagara_BuildSprites"), NumChunks, 1, [&](int32 Chunk)
	{
		const int32 First = NumChunks == 1 ? 0 : Chunk * ParallelBatch;
		const int32 Last = NumChunks == 1 ? NumSprites : FMath::Min(NumSprites, First + ParallelBatch);
		FBox Bounds(ForceInit);
		for (int32 Sprite = First; Sprite < Last; ++Sprite)
		{
			const FDreamNiagaraSpriteParticle& Particle = InParticles[Order[Sprite]];
			const FVector3f Centre = ToPlane(InSpace, Particle.Position);
			const FVector2f Size(FMath::Abs(Particle.Size.X) * InSpace.SizeScale, FMath::Abs(Particle.Size.Y) * InSpace.SizeScale);

			// Right and down (the texture's +u and +v) on the plane, as (right, up) pairs.
			FVector2f Right(1.0f, 0.0f);
			FVector2f Down(0.0f, -1.0f);
			bool bRotate = true;
			if (InSettings.Alignment != EDreamNiagaraSpriteAlignment::Unaligned)
			{
				const FVector2f Top = DirectionOnPlane(InSpace, InSettings.Alignment == EDreamNiagaraSpriteAlignment::Velocity ? Particle.Velocity : Particle.Alignment);
				if (!Top.IsZero())
				{
					Down = -Top;
					Right = FVector2f(Top.Y, -Top.X);
				}
				// Niagara reads no rotation for a sprite aligned to its velocity.
				bRotate = InSettings.Alignment == EDreamNiagaraSpriteAlignment::Custom;
			}
			if (bRotate && Particle.Rotation != 0.0f)
			{
				float Sin, Cos;
				FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Particle.Rotation));
				const FVector2f TurnedRight = Down * Sin + Right * Cos;
				const FVector2f TurnedDown = Down * Cos - Right * Sin;
				Right = TurnedRight;
				Down = TurnedDown;
			}

			const float SubImage = Particle.SubImageIndex;
			const float Blend = InSettings.bSubImageBlend ? FMath::Frac(SubImage) : 0.0f;
			const float Frame = SubImage - FMath::Frac(SubImage);
			const FLinearColor Linear = Tinted(Particle.Color, InCommon.Tint);
			const float Scale = DreamNiagaraVertexLayout::ScaleFor(Linear);
			const FColor Colour = DreamNiagaraVertexLayout::EncodeColor(Linear, Scale);
			const float Packed = DreamNiagaraVertexLayout::Pack(Scale, Blend);

			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector2f& UV = CornerUV[Corner];
				const FVector2f Offset = Right * (Size.X * (UV.X - Particle.PivotOffset.X)) + Down * (Size.Y * (UV.Y - Particle.PivotOffset.Y));
				const FVector3f Plane(Centre.X, Centre.Y + Offset.X, Centre.Z + Offset.Y);
				const FVector2f Texturing = FVector2f(0.5f, 0.5f) + (UV - FVector2f(0.5f, 0.5f)) * Particle.UVScale;
				FDreamUIMeshVertex Vertex = MakeVertex(InSpace, Plane, Colour,
					SubImageUV(Frame, InSettings.SubImageSize, Texturing), SubImageUV(Frame + 1.0f, InSettings.SubImageSize, Texturing),
					Packed, Particle.DynamicParameter, InCommon.WidgetRecord);
				Bounds += FVector(Vertex.Position);
				Vertices[Sprite * 4 + Corner] = Vertex;
			}
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Indices[Sprite * 6 + Index] = static_cast<FDreamUIMeshIndex>(Sprite * 4 + QuadIndices[Index]);
			}
		}
		ChunkBounds[Chunk] = Bounds;
	}, NumChunks == 1 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	InOutTarget.Bounds = FBox(ForceInit);
	for (const FBox& Bounds : ChunkBounds)
	{
		InOutTarget.Bounds += Bounds;
	}
}

void FDreamNiagaraMeshBuilder::GroupRibbons(TConstArrayView<FDreamNiagaraRibbonParticle> InParticles, int32 InMaxNumRibbons, TArray<TArray<int32>>& OutRibbons)
{
	OutRibbons.Reset();
	TMap<int64, int32> GroupOfKey;
	TArray<int64> Keys;
	for (int32 Index = 0; Index < InParticles.Num(); ++Index)
	{
		const int64 Key = InParticles[Index].RibbonKey;
		int32* Group = GroupOfKey.Find(Key);
		if (Group == nullptr)
		{
			Group = &GroupOfKey.Add(Key, OutRibbons.Num());
			OutRibbons.AddDefaulted();
			Keys.Add(Key);
		}
		OutRibbons[*Group].Add(Index);
	}
	// Ribbons by key, so that which ribbons a limit keeps does not depend on the order the simulation holds their points in.
	TArray<int32> GroupOrder;
	GroupOrder.SetNumUninitialized(Keys.Num());
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		GroupOrder[Index] = Index;
	}
	GroupOrder.Sort([&Keys](int32 A, int32 B) { return Keys[A] < Keys[B]; });
	TArray<TArray<int32>> Sorted;
	Sorted.Reserve(GroupOrder.Num());
	for (int32 Group : GroupOrder)
	{
		if (InMaxNumRibbons > 0 && Sorted.Num() >= InMaxNumRibbons)
		{
			break;
		}
		TArray<int32>& Points = Sorted.Add_GetRef(MoveTemp(OutRibbons[Group]));
		Algo::StableSort(Points, [InParticles](int32 A, int32 B) { return InParticles[A].LinkOrder < InParticles[B].LinkOrder; });
	}
	OutRibbons = MoveTemp(Sorted);
}

int32 FDreamNiagaraMeshBuilder::TessellationOf(TConstArrayView<FVector2f> InPoints, int32 InIndex, const FDreamNiagaraRibbonSettings& InSettings)
{
	const int32 MaxPieces = FMath::Clamp(InSettings.MaxTessellation, 1, 16);
	if (MaxPieces <= 1 || InIndex < 0 || InIndex + 1 >= InPoints.Num())
	{
		return 1;
	}
	if (InSettings.bConstantTessellation)
	{
		return MaxPieces;
	}
	// The sharper of the two bends at the segment's ends: how far the curve through it turns.
	auto Bend = [InPoints](int32 Middle) -> float
	{
		if (Middle <= 0 || Middle + 1 >= InPoints.Num())
		{
			return 0.0f;
		}
		const FVector2f In = (InPoints[Middle] - InPoints[Middle - 1]).GetSafeNormal();
		const FVector2f Out = (InPoints[Middle + 1] - InPoints[Middle]).GetSafeNormal();
		if (In.IsZero() || Out.IsZero())
		{
			return 0.0f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2f::DotProduct(In, Out), -1.0f, 1.0f)));
	};
	const float Angle = FMath::Max(Bend(InIndex), Bend(InIndex + 1));
	const float PerPiece = FMath::Max(InSettings.TessellationAngle, 1.0f);
	// A hair under, so that a bend of exactly so many pieces is not a piece more for its rounding.
	return FMath::Clamp(FMath::CeilToInt(Angle / PerPiece - UE_KINDA_SMALL_NUMBER), 1, MaxPieces);
}

FVector2f FDreamNiagaraMeshBuilder::CurvePoint(TConstArrayView<FVector2f> InPoints, int32 InIndex, float InT, float InTension)
{
	const int32 Last = InPoints.Num() - 1;
	const int32 A = FMath::Clamp(InIndex, 0, Last);
	const int32 B = FMath::Clamp(InIndex + 1, 0, Last);
	const float Scale = 1.0f - FMath::Clamp(InTension, 0.0f, 1.0f);
	// A cardinal spline's tangents: half the chord across each point, one-sided at the ribbon's two ends.
	auto Tangent = [InPoints, Last, Scale](int32 Point) -> FVector2f
	{
		if (Last < 1)
		{
			return FVector2f::ZeroVector;
		}
		if (Point == 0)
		{
			return (InPoints[1] - InPoints[0]) * Scale;
		}
		if (Point == Last)
		{
			return (InPoints[Last] - InPoints[Last - 1]) * Scale;
		}
		return (InPoints[Point + 1] - InPoints[Point - 1]) * (0.5f * Scale);
	};
	const float T = FMath::Clamp(InT, 0.0f, 1.0f);
	const float T2 = T * T;
	const float T3 = T2 * T;
	const float H00 = 2.0f * T3 - 3.0f * T2 + 1.0f;
	const float H10 = T3 - 2.0f * T2 + T;
	const float H01 = -2.0f * T3 + 3.0f * T2;
	const float H11 = T3 - T2;
	return InPoints[A] * H00 + Tangent(A) * H10 + InPoints[B] * H01 + Tangent(B) * H11;
}

void FDreamNiagaraMeshBuilder::BuildRibbons(TConstArrayView<FDreamNiagaraRibbonParticle> InParticles, const FDreamNiagaraRibbonSettings& InSettings,
	const FDreamNiagaraBuildSpace& InSpace, const FDreamNiagaraVertexCommon& InCommon, FDreamNiagaraMeshTarget& InOutTarget)
{
	using namespace DreamNiagaraMeshBuilderLocal;
	TArray<TArray<int32>> Ribbons;
	GroupRibbons(InParticles, InSettings.MaxNumRibbons, Ribbons);

	/** A point of a ribbon after its segments are cut: where it is and what it carries. */
	struct FSample
	{
		FVector2f Point;
		float Depth = 0.0f;
		float Width = 0.0f;
		FLinearColor Color;
		float U0 = 0.0f;
		float U1 = 0.0f;
		FVector4f Dynamic;
	};
	struct FRibbonPlan
	{
		TArray<FSample> Samples;
		float Scale = 1.0f;
		int32 FirstVertex = 0;
		int32 FirstIndex = 0;
	};
	TArray<FRibbonPlan> Plans;
	Plans.Reserve(Ribbons.Num());
	int32 NumVertices = 0;
	int32 NumIndices = 0;
	InOutTarget.NumDropped = 0;
	const float SimPerCanvas = InSpace.SizeScale > UE_SMALL_NUMBER ? 1.0f / InSpace.SizeScale : 1.0f;

	for (const TArray<int32>& Ribbon : Ribbons)
	{
		if (Ribbon.Num() < 2)
		{
			continue;
		}
		const int32 Count = Ribbon.Num();
		TArray<FVector2f> Points;
		TArray<float> Depths;
		Points.SetNumUninitialized(Count);
		Depths.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector3f Plane = ToPlane(InSpace, InParticles[Ribbon[Index]].Position);
			Points[Index] = FVector2f(Plane.Y, Plane.Z);
			Depths[Index] = Plane.X;
		}
		// Each point's distance along the ribbon, in simulation units, for the UV sets that go by length.
		TArray<float> Along;
		Along.SetNumUninitialized(Count);
		Along[0] = 0.0f;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			Along[Index] = Along[Index - 1] + FVector2f::Distance(Points[Index - 1], Points[Index]) * SimPerCanvas;
		}
		const float Total = Along[Count - 1];
		auto UOf = [&](const FDreamNiagaraRibbonUVSettings& InUV, int32 InPoint, bool bFirstSet) -> float
		{
			if (InUV.bUseUOverride)
			{
				const FDreamNiagaraRibbonParticle& Particle = InParticles[Ribbon[InPoint]];
				return bFirstSet ? Particle.U0Override : Particle.U1Override;
			}
			switch (InUV.Distribution)
			{
			case EDreamNiagaraRibbonUVDistribution::ScaledByLength:
				return Total > UE_SMALL_NUMBER ? Along[InPoint] / Total : static_cast<float>(InPoint) / static_cast<float>(Count - 1);
			case EDreamNiagaraRibbonUVDistribution::TiledByLength:
				return Along[InPoint] / FMath::Max(InUV.TilingLength, UE_SMALL_NUMBER);
			default:
				return static_cast<float>(InPoint) / static_cast<float>(Count - 1);
			}
		};

		FRibbonPlan Plan;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FLinearColor Linear = Tinted(InParticles[Ribbon[Index]].Color, InCommon.Tint);
			Plan.Scale = FMath::Max(Plan.Scale, DreamNiagaraVertexLayout::ScaleFor(Linear));
		}
		for (int32 Segment = 0; Segment < Count - 1; ++Segment)
		{
			const FDreamNiagaraRibbonParticle& From = InParticles[Ribbon[Segment]];
			const FDreamNiagaraRibbonParticle& To = InParticles[Ribbon[Segment + 1]];
			const float FromU0 = UOf(InSettings.UV0, Segment, true);
			const float ToU0 = UOf(InSettings.UV0, Segment + 1, true);
			const float FromU1 = UOf(InSettings.UV1, Segment, false);
			const float ToU1 = UOf(InSettings.UV1, Segment + 1, false);
			const int32 Pieces = TessellationOf(Points, Segment, InSettings);
			// The segment's end is the next one's start: the last point of the ribbon is added after the loop.
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				const float T = static_cast<float>(Piece) / static_cast<float>(Pieces);
				FSample& Sample = Plan.Samples.AddDefaulted_GetRef();
				Sample.Point = Pieces > 1 ? CurvePoint(Points, Segment, T, InSettings.CurveTension) : Points[Segment];
				Sample.Depth = FMath::Lerp(Depths[Segment], Depths[Segment + 1], T);
				Sample.Width = FMath::Lerp(From.Width, To.Width, T);
				Sample.Color = FMath::Lerp(Tinted(From.Color, InCommon.Tint), Tinted(To.Color, InCommon.Tint), T);
				Sample.U0 = FMath::Lerp(FromU0, ToU0, T);
				Sample.U1 = FMath::Lerp(FromU1, ToU1, T);
				Sample.Dynamic = FMath::Lerp(From.DynamicParameter, To.DynamicParameter, T);
			}
		}
		{
			const FDreamNiagaraRibbonParticle& Last = InParticles[Ribbon[Count - 1]];
			FSample& Sample = Plan.Samples.AddDefaulted_GetRef();
			Sample.Point = Points[Count - 1];
			Sample.Depth = Depths[Count - 1];
			Sample.Width = Last.Width;
			Sample.Color = Tinted(Last.Color, InCommon.Tint);
			Sample.U0 = UOf(InSettings.UV0, Count - 1, true);
			Sample.U1 = UOf(InSettings.UV1, Count - 1, false);
			Sample.Dynamic = Last.DynamicParameter;
		}
		const int32 RibbonVertices = Plan.Samples.Num() * 2;
		if (NumVertices + RibbonVertices >= LEXUI_MAX_VERTEX_COUNT)
		{
			InOutTarget.NumDropped += Count;
			continue;
		}
		Plan.FirstVertex = NumVertices;
		Plan.FirstIndex = NumIndices;
		NumVertices += RibbonVertices;
		NumIndices += (Plan.Samples.Num() - 1) * 6;
		Plans.Add(MoveTemp(Plan));
	}

	if (NumVertices == 0)
	{
		BuildEmpty(InCommon, InOutTarget);
		return;
	}
	Reserve(InOutTarget, NumVertices, NumIndices);
	InOutTarget.NumVertices = NumVertices;
	InOutTarget.NumIndices = NumIndices;
	FDreamUIMeshVertex* const Vertices = InOutTarget.Vertices->GetData();
	FDreamUIMeshIndex* const Indices = InOutTarget.Indices->GetData();
	TArray<FBox> RibbonBounds;
	RibbonBounds.Init(FBox(ForceInit), Plans.Num());

	ParallelFor(TEXT("DreamNiagara_BuildRibbons"), Plans.Num(), 1, [&](int32 RibbonIndex)
	{
		const FRibbonPlan& Plan = Plans[RibbonIndex];
		const TArray<FSample>& Samples = Plan.Samples;
		const int32 Num = Samples.Num();
		const float Packed = DreamNiagaraVertexLayout::Pack(Plan.Scale, 0.0f);
		FBox Bounds(ForceInit);
		FVector2f LastTangent(1.0f, 0.0f);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			// Along the curve where the point is: from its neighbours, the last good direction where they meet.
			const FVector2f Before = Samples[FMath::Max(Index - 1, 0)].Point;
			const FVector2f After = Samples[FMath::Min(Index + 1, Num - 1)].Point;
			FVector2f Tangent = After - Before;
			if (Tangent.Normalize(UE_SMALL_NUMBER))
			{
				LastTangent = Tangent;
			}
			else
			{
				Tangent = LastTangent;
			}
			// Left of the way the ribbon goes: the edge whose V is 0.
			const FVector2f Normal(-Tangent.Y, Tangent.X);
			const FSample& Sample = Samples[Index];
			const float HalfWidth = 0.5f * Sample.Width * InSpace.SizeScale;
			const FColor Colour = DreamNiagaraVertexLayout::EncodeColor(Sample.Color, Plan.Scale);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const float V = static_cast<float>(Side);
				const FVector2f Edge = Sample.Point + Normal * (Side == 0 ? HalfWidth : -HalfWidth);
				const FVector2f UV0(Sample.U0 * InSettings.UV0.Scale.X + InSettings.UV0.Offset.X, V * InSettings.UV0.Scale.Y + InSettings.UV0.Offset.Y);
				const FVector2f UV1(Sample.U1 * InSettings.UV1.Scale.X + InSettings.UV1.Offset.X, V * InSettings.UV1.Scale.Y + InSettings.UV1.Offset.Y);
				FDreamUIMeshVertex Vertex = MakeVertex(InSpace, FVector3f(Sample.Depth, Edge.X, Edge.Y), Colour, UV0, UV1, Packed, Sample.Dynamic, InCommon.WidgetRecord);
				Bounds += FVector(Vertex.Position);
				Vertices[Plan.FirstVertex + Index * 2 + Side] = Vertex;
			}
		}
		// Each piece's quad: bottom left, bottom right, top left, top right are the right edge here and next and the left
		// edge here and next, anticlockwise as every DreamGUI quad is. Drawn last first, the pieces go in reverse.
		for (int32 Piece = 0; Piece < Num - 1; ++Piece)
		{
			const int32 Here = InSettings.bDrawLastFirst ? Num - 2 - Piece : Piece;
			const int32 TopHere = Plan.FirstVertex + Here * 2;
			const int32 BottomHere = TopHere + 1;
			const int32 TopNext = TopHere + 2;
			const int32 BottomNext = TopHere + 3;
			FDreamUIMeshIndex* const Quad = Indices + Plan.FirstIndex + Piece * 6;
			Quad[0] = static_cast<FDreamUIMeshIndex>(BottomHere);
			Quad[1] = static_cast<FDreamUIMeshIndex>(TopNext);
			Quad[2] = static_cast<FDreamUIMeshIndex>(TopHere);
			Quad[3] = static_cast<FDreamUIMeshIndex>(BottomHere);
			Quad[4] = static_cast<FDreamUIMeshIndex>(BottomNext);
			Quad[5] = static_cast<FDreamUIMeshIndex>(TopNext);
		}
		RibbonBounds[RibbonIndex] = Bounds;
	}, Plans.Num() < 4 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	InOutTarget.Bounds = FBox(ForceInit);
	for (const FBox& Bounds : RibbonBounds)
	{
		InOutTarget.Bounds += Bounds;
	}
}
