// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MeshModifier/DreamMeshDeform.h"
#include "Core/DreamUIGeometry.h"

namespace DreamMeshDeformLocal
{
	/**
	 * The weighted sum of InCount values, in the order given; or the value itself when every one of them is the same,
	 * bit for bit. The sum is written so that a weight of exactly 0 or 1 adds exactly nothing or the whole value: two
	 * neighbours cutting their shared edge then make the same vertices there, to the bit.
	 */
	float Blend(const float* InValues, const float* InWeights, int32 InCount)
	{
		bool bSame = true;
		for (int32 Index = 1; Index < InCount && bSame; ++Index)
		{
			bSame = InValues[Index] == InValues[0];
		}
		if (bSame)
		{
			return InValues[0];
		}
		float Sum = 0.0f;
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Sum += InValues[Index] * InWeights[Index];
		}
		return Sum;
	}

	uint8 BlendByte(const uint8* InValues, const float* InWeights, int32 InCount)
	{
		bool bSame = true;
		for (int32 Index = 1; Index < InCount && bSame; ++Index)
		{
			bSame = InValues[Index] == InValues[0];
		}
		if (bSame)
		{
			return InValues[0];
		}
		float Sum = 0.0f;
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Sum += static_cast<float>(InValues[Index]) * InWeights[Index];
		}
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Sum), 0, 255));
	}

	/** A new vertex made from InCount corners of InGeometry, InWeights of each: see FDreamMeshDeform::Subdivide. */
	void MakeVertex(const FDreamUIGeometry& InGeometry, const int32* InCorners, const float* InWeights, int32 InCount,
		FDreamUIOriginVertexData& OutOrigin, FDreamUIMeshVertex& OutVertex)
	{
		float Values[4];
		auto BlendOf = [&](TFunctionRef<float(int32)> InRead)
		{
			for (int32 Index = 0; Index < InCount; ++Index)
			{
				Values[Index] = InRead(InCorners[Index]);
			}
			return Blend(Values, InWeights, InCount);
		};
		const TArray<FDreamUIOriginVertexData>& Origins = InGeometry.OriginVertices;
		const TArray<FDreamUIMeshVertex>& Vertices = InGeometry.Vertices;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			OutOrigin.Position[Axis] = BlendOf([&](int32 V) { return Origins[V].Position[Axis]; });
			OutOrigin.Normal[Axis] = BlendOf([&](int32 V) { return Origins[V].Normal[Axis]; });
			OutOrigin.Tangent[Axis] = BlendOf([&](int32 V) { return Origins[V].Tangent[Axis]; });
			OutVertex.Position[Axis] = BlendOf([&](int32 V) { return Vertices[V].Position[Axis]; });
		}
		for (int32 Channel = 0; Channel < LEXUI_VERTEX_TEXCOORDINATE_COUNT; ++Channel)
		{
			OutVertex.TextureCoordinate[Channel].X = BlendOf([&](int32 V) { return Vertices[V].TextureCoordinate[Channel].X; });
			OutVertex.TextureCoordinate[Channel].Y = BlendOf([&](int32 V) { return Vertices[V].TextureCoordinate[Channel].Y; });
		}
		OutVertex.UV4.X = BlendOf([&](int32 V) { return Vertices[V].UV4.X; });
		OutVertex.UV4.Y = BlendOf([&](int32 V) { return Vertices[V].UV4.Y; });
		uint8 Bytes[4];
		auto BlendChannel = [&](TFunctionRef<uint8(const FColor&)> InRead)
		{
			for (int32 Index = 0; Index < InCount; ++Index)
			{
				Bytes[Index] = InRead(Vertices[InCorners[Index]].Color);
			}
			return BlendByte(Bytes, InWeights, InCount);
		};
		OutVertex.Color.R = BlendChannel([](const FColor& C) { return C.R; });
		OutVertex.Color.G = BlendChannel([](const FColor& C) { return C.G; });
		OutVertex.Color.B = BlendChannel([](const FColor& C) { return C.B; });
		OutVertex.Color.A = BlendChannel([](const FColor& C) { return C.A; });
		OutVertex.TangentX = Vertices[InCorners[0]].TangentX;
		OutVertex.TangentZ = Vertices[InCorners[0]].TangentZ;
	}

	FVector2f Plane(const FDreamUIOriginVertexData& InVertex)
	{
		return FVector2f(InVertex.Position.Y, InVertex.Position.Z);
	}

	/** A quad of two triangles that is an axis-aligned rectangle, its corners by where they are. */
	struct FRectQuad
	{
		int32 Corner[4] = { 0, 0, 0, 0 }; // left bottom, right bottom, left top, right top
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
		/** The first triangle's winding was counter-clockwise (x right, y up). */
		bool bCounterClockwise = true;
	};

	bool MatchRectQuad(const FDreamUIGeometry& InGeometry, int32 InFirstIndex, FRectQuad& OutQuad)
	{
		const TArray<FDreamUIMeshIndex>& Triangles = InGeometry.Triangles;
		int32 Unique[4];
		int32 UniqueCount = 0;
		for (int32 Index = InFirstIndex; Index < InFirstIndex + 6; ++Index)
		{
			const int32 Vertex = Triangles[Index];
			bool bSeen = false;
			for (int32 Seen = 0; Seen < UniqueCount; ++Seen)
			{
				bSeen |= Unique[Seen] == Vertex;
			}
			if (!bSeen)
			{
				if (UniqueCount == 4)
				{
					return false;
				}
				Unique[UniqueCount++] = Vertex;
			}
		}
		if (UniqueCount != 4)
		{
			return false;
		}
		const TArray<FDreamUIOriginVertexData>& Origins = InGeometry.OriginVertices;
		FVector2f Min(MAX_flt, MAX_flt), Max(-MAX_flt, -MAX_flt);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2f P = Plane(Origins[Unique[Index]]);
			Min = FVector2f(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
			Max = FVector2f(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y));
		}
		if (!(Max.X > Min.X) || !(Max.Y > Min.Y))
		{
			return false;
		}
		bool bFilled[4] = { false, false, false, false };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2f P = Plane(Origins[Unique[Index]]);
			const bool bLeft = P.X == Min.X, bRight = P.X == Max.X, bBottom = P.Y == Min.Y, bTop = P.Y == Max.Y;
			if (!(bLeft || bRight) || !(bBottom || bTop))
			{
				return false;
			}
			const int32 Slot = (bRight ? 1 : 0) + (bTop ? 2 : 0);
			if (bFilled[Slot])
			{
				return false;
			}
			bFilled[Slot] = true;
			OutQuad.Corner[Slot] = Unique[Index];
		}
		OutQuad.Min = Min;
		OutQuad.Max = Max;
		const FVector2f A = Plane(Origins[Triangles[InFirstIndex]]);
		const FVector2f B = Plane(Origins[Triangles[InFirstIndex + 1]]);
		const FVector2f C = Plane(Origins[Triangles[InFirstIndex + 2]]);
		OutQuad.bCounterClockwise = FVector2f::CrossProduct(B - A, C - A) >= 0.0f;
		return true;
	}

	/** The grid lines InCell apart from InOrigin strictly inside InMin..InMax, clear of either end by a thousandth of a cell. */
	void Cuts(float InMin, float InMax, float InOrigin, float InCell, TArray<float, TInlineAllocator<64>>& OutLines)
	{
		OutLines.Reset();
		OutLines.Add(InMin);
		const float Clear = InCell * 0.001f;
		const int64 First = FMath::FloorToInt64((InMin - InOrigin) / InCell) + 1;
		for (int64 Line = First; ; ++Line)
		{
			const float At = InOrigin + static_cast<float>(Line) * InCell;
			if (At >= InMax - Clear)
			{
				break;
			}
			if (At > InMin + Clear)
			{
				OutLines.Add(At);
			}
			if (OutLines.Num() > 4096)
			{
				break;
			}
		}
		OutLines.Add(InMax);
	}

	int64 CountCuts(float InMin, float InMax, float InOrigin, float InCell)
	{
		const float Clear = InCell * 0.001f;
		const int64 First = FMath::FloorToInt64((InMin - InOrigin) / InCell) + 1;
		const int64 Last = FMath::CeilToInt64((InMax - Clear - InOrigin) / InCell) - 1;
		int64 Count = FMath::Max<int64>(0, Last - First + 1);
		// The ends lose a line that falls within the clearance of either one; the count is an estimate for the budget only.
		return Count + 1;
	}

	void EmitTriangle(TArray<FDreamUIMeshIndex>& OutTriangles, int32 InA, int32 InB, int32 InC, bool bInCounterClockwise)
	{
		OutTriangles.Add(static_cast<FDreamUIMeshIndex>(InA));
		if (bInCounterClockwise)
		{
			OutTriangles.Add(static_cast<FDreamUIMeshIndex>(InB));
			OutTriangles.Add(static_cast<FDreamUIMeshIndex>(InC));
		}
		else
		{
			OutTriangles.Add(static_cast<FDreamUIMeshIndex>(InC));
			OutTriangles.Add(static_cast<FDreamUIMeshIndex>(InB));
		}
	}
}

bool FDreamMeshDeform::FStyle::IsIdentity() const
{
	const bool bStyleMoves = Style != EDreamWarpStyle::None && Bend != 0.0f;
	return !bStyleMoves && HorizontalDistortion == 0.0f && VerticalDistortion == 0.0f;
}

FVector2f FDreamMeshDeform::WarpPoint(const FStyle& InStyle, FVector2f InPoint, FVector2f InCentre, FVector2f InHalfSize)
{
	if (InStyle.IsIdentity())
	{
		return InPoint;
	}
	// Into the warp's frame: x along the length, y across it, from the box's centre. A vertical warp turns the box a
	// quarter turn clockwise first (its top becomes the right end) and back after.
	FVector2f Q = InPoint - InCentre;
	float A = FMath::Max(InHalfSize.X, KINDA_SMALL_NUMBER);
	float H = FMath::Max(InHalfSize.Y, KINDA_SMALL_NUMBER);
	if (InStyle.bVertical)
	{
		Q = FVector2f(Q.Y, -Q.X);
		Swap(A, H);
	}
	float S = Q.X / A;
	float T = Q.Y / H;

	// The distortions are the perspective of the box turned about its middle: the projective maps of [-1, 1] onto
	// itself that make one end the larger, the size across following the spacing along.
	const float E = FMath::Clamp(InStyle.HorizontalDistortion, -1.0f, 1.0f) * 0.5f;
	if (E != 0.0f)
	{
		const float Depth = 1.0f - E * S;
		const float Across = FMath::Sqrt(1.0f - E * E) / Depth;
		S = (S - E) / Depth;
		T *= Across;
	}
	const float F = FMath::Clamp(InStyle.VerticalDistortion, -1.0f, 1.0f) * 0.5f;
	if (F != 0.0f)
	{
		const float Depth = 1.0f - F * T;
		const float Across = FMath::Sqrt(1.0f - F * F) / Depth;
		T = (T - F) / Depth;
		S *= Across;
	}

	const float B = FMath::Clamp(InStyle.Bend, -1.0f, 1.0f);
	float X = S * A;
	float Y = T * H;
	// How far the arcs and the rise move an edge (half of the half length at full bend), and how far a wave swings.
	const float Sagitta = 0.5f * B * A;
	const float Amplitude = 0.25f * B * A;
	const float Phase = 2.0f * PI * InStyle.Phase;
	const float Across = 1.0f - S * S;
	switch (B == 0.0f ? EDreamWarpStyle::None : InStyle.Style)
	{
	default:
	case EDreamWarpStyle::None:
		break;
	case EDreamWarpStyle::Arc:
	{
		// Along a circle whose centre line keeps its length: the middle line of the box becomes an arc of B * 180 degrees.
		const float K = B * PI / (2.0f * A);
		const float Angle = K * X;
		const float SinAngle = FMath::Sin(Angle);
		const float HalfSin = FMath::Sin(0.5f * Angle);
		const float OutX = SinAngle / K + Y * SinAngle;
		const float OutY = Y * FMath::Cos(Angle) - 2.0f * HalfSin * HalfSin / K;
		X = OutX;
		Y = OutY;
		break;
	}
	case EDreamWarpStyle::ArcLower:
		Y -= 0.5f * (1.0f - T) * Sagitta * Across;
		break;
	case EDreamWarpStyle::ArcUpper:
		Y += 0.5f * (1.0f + T) * Sagitta * Across;
		break;
	case EDreamWarpStyle::Arch:
		Y += Sagitta * Across;
		break;
	case EDreamWarpStyle::Bulge:
		Y *= FMath::Max(0.0f, 1.0f + B * Across);
		break;
	case EDreamWarpStyle::ShellLower:
	{
		const float Weight = 0.5f * (1.0f - T);
		Y += Weight * Sagitta * Across;
		X *= 1.0f - 0.5f * B * Weight;
		break;
	}
	case EDreamWarpStyle::ShellUpper:
	{
		const float Weight = 0.5f * (1.0f + T);
		Y -= Weight * Sagitta * Across;
		X *= 1.0f - 0.5f * B * Weight;
		break;
	}
	case EDreamWarpStyle::Flag:
		Y += Amplitude * FMath::Sin(PI * S + Phase);
		break;
	case EDreamWarpStyle::Wave:
		Y += Amplitude * FMath::Sin(PI * S + 0.5f * PI * T + Phase);
		break;
	case EDreamWarpStyle::Fish:
		Y *= FMath::Max(0.0f, 1.0f - 0.8f * B * FMath::Sin(PI * S));
		break;
	case EDreamWarpStyle::Rise:
		Y += Sagitta * FMath::Sin(0.5f * PI * S);
		break;
	case EDreamWarpStyle::Fisheye:
	{
		const float Radius = FMath::Sqrt(S * S + T * T);
		if (Radius > 0.0f && Radius < 1.0f)
		{
			const float Strength = FMath::Clamp(B, -0.95f, 1.0f);
			const float Scale = (1.0f + Strength) / (1.0f + Strength * Radius);
			X *= Scale;
			Y *= Scale;
		}
		break;
	}
	case EDreamWarpStyle::Inflate:
		X *= 1.0f + 0.5f * B * (1.0f - T * T);
		Y *= 1.0f + 0.5f * B * Across;
		break;
	case EDreamWarpStyle::Squeeze:
		X *= 1.0f - 0.5f * B * (1.0f - T * T);
		Y *= 1.0f + 0.5f * B * Across;
		break;
	case EDreamWarpStyle::Twist:
	{
		// A swirl about the middle, turned in real units so that a long box is turned and not sheared: most at the
		// centre, not at all from the circle through its ends outward, so the ends hold while the middle turns and
		// the length runs through in an S.
		const float Reach = FMath::Max(A, H);
		const float Radius = FMath::Sqrt(X * X + Y * Y) / Reach;
		if (Radius < 1.0f)
		{
			const float Falloff = 1.0f - Radius;
			const float Turn = 0.5f * PI * B * Falloff * Falloff;
			const float SinTurn = FMath::Sin(Turn);
			const float CosTurn = FMath::Cos(Turn);
			const float TurnedX = X * CosTurn - Y * SinTurn;
			Y = X * SinTurn + Y * CosTurn;
			X = TurnedX;
		}
		break;
	}
	}
	FVector2f Out(X, Y);
	if (InStyle.bVertical)
	{
		Out = FVector2f(-Out.Y, Out.X);
	}
	return InCentre + Out;
}

float FDreamMeshDeform::Bernstein(int32 InDegree, int32 InIndex, float InT)
{
	static constexpr float Binomial[5][5] = {
		{ 1, 0, 0, 0, 0 },
		{ 1, 1, 0, 0, 0 },
		{ 1, 2, 1, 0, 0 },
		{ 1, 3, 3, 1, 0 },
		{ 1, 4, 6, 4, 1 },
	};
	if (InDegree < 0 || InDegree > 4 || InIndex < 0 || InIndex > InDegree)
	{
		return 0.0f;
	}
	const float U = 1.0f - InT;
	float Weight = Binomial[InDegree][InIndex];
	for (int32 Power = 0; Power < InIndex; ++Power)
	{
		Weight *= InT;
	}
	for (int32 Power = 0; Power < InDegree - InIndex; ++Power)
	{
		Weight *= U;
	}
	return Weight;
}

FVector2f FDreamMeshDeform::EvaluateLattice(TConstArrayView<FVector2D> InPoints, int32 InColumns, int32 InRows, FVector2f InPoint, FVector2f InMin, FVector2f InMax)
{
	if (InColumns < MinLatticeSize || InRows < MinLatticeSize || InColumns > MaxLatticeSize || InRows > MaxLatticeSize
		|| InPoints.Num() != InColumns * InRows)
	{
		return InPoint;
	}
	const FVector2f Size(FMath::Max(InMax.X - InMin.X, KINDA_SMALL_NUMBER), FMath::Max(InMax.Y - InMin.Y, KINDA_SMALL_NUMBER));
	const float U = (InPoint.X - InMin.X) / Size.X;
	const float V = (InMax.Y - InPoint.Y) / Size.Y;
	float Across[MaxLatticeSize];
	float Down[MaxLatticeSize];
	for (int32 Column = 0; Column < InColumns; ++Column)
	{
		Across[Column] = Bernstein(InColumns - 1, Column, U);
	}
	for (int32 Row = 0; Row < InRows; ++Row)
	{
		Down[Row] = Bernstein(InRows - 1, Row, V);
	}
	FVector2f Sum = FVector2f::ZeroVector;
	for (int32 Row = 0; Row < InRows; ++Row)
	{
		for (int32 Column = 0; Column < InColumns; ++Column)
		{
			Sum += FVector2f(InPoints[Row * InColumns + Column]) * (Across[Column] * Down[Row]);
		}
	}
	return FVector2f(InMin.X + Sum.X * Size.X, InMax.Y - Sum.Y * Size.Y);
}

void FDreamMeshDeform::MakeRestLattice(int32 InColumns, int32 InRows, TArray<FVector2D>& OutPoints)
{
	InColumns = FMath::Clamp(InColumns, MinLatticeSize, MaxLatticeSize);
	InRows = FMath::Clamp(InRows, MinLatticeSize, MaxLatticeSize);
	OutPoints.Reset(InColumns * InRows);
	for (int32 Row = 0; Row < InRows; ++Row)
	{
		for (int32 Column = 0; Column < InColumns; ++Column)
		{
			OutPoints.Add(FVector2D(static_cast<double>(Column) / (InColumns - 1), static_cast<double>(Row) / (InRows - 1)));
		}
	}
}

FBox2f FDreamMeshDeform::MeasureBounds(const FDreamUIGeometry& InGeometry)
{
	FBox2f Box(ForceInit);
	for (const FDreamUIOriginVertexData& Vertex : InGeometry.OriginVertices)
	{
		Box += DreamMeshDeformLocal::Plane(Vertex);
	}
	return Box;
}

int32 FDreamMeshDeform::Subdivide(FDreamUIGeometry& InOutGeometry, const FSubdivision& InOptions, TArray<int32>* OutSourceVertex)
{
	using namespace DreamMeshDeformLocal;
	TArray<FDreamUIOriginVertexData>& Origins = InOutGeometry.OriginVertices;
	TArray<FDreamUIMeshVertex>& Vertices = InOutGeometry.Vertices;
	TArray<FDreamUIMeshIndex>& Triangles = InOutGeometry.Triangles;
	const int32 OriginalCount = Origins.Num();
	if (OutSourceVertex != nullptr)
	{
		OutSourceVertex->Reset(OriginalCount);
		for (int32 Index = 0; Index < OriginalCount; ++Index)
		{
			OutSourceVertex->Add(Index);
		}
	}
	if (OriginalCount == 0 || Vertices.Num() != OriginalCount || Triangles.Num() < 3)
	{
		return 0;
	}
	const FBox2f Bounds = MeasureBounds(InOutGeometry);
	const FVector2f GridOrigin = Bounds.Min;

	// The rectangles, and the triangles that are not part of one.
	TArray<FRectQuad> Quads;
	TArray<int32> Loose;
	const int32 TriangleCount = Triangles.Num() / 3;
	for (int32 Triangle = 0; Triangle < TriangleCount; )
	{
		FRectQuad Quad;
		if (Triangle + 1 < TriangleCount && MatchRectQuad(InOutGeometry, Triangle * 3, Quad))
		{
			Quads.Add(Quad);
			Triangle += 2;
		}
		else
		{
			Loose.Add(Triangle);
			Triangle += 1;
		}
	}
	float LongestLoose = 0.0f;
	for (const int32 Triangle : Loose)
	{
		const FVector2f A = Plane(Origins[Triangles[Triangle * 3]]);
		const FVector2f B = Plane(Origins[Triangles[Triangle * 3 + 1]]);
		const FVector2f C = Plane(Origins[Triangles[Triangle * 3 + 2]]);
		LongestLoose = FMath::Max(LongestLoose, FMath::Max(FVector2f::Distance(A, B), FMath::Max(FVector2f::Distance(B, C), FVector2f::Distance(C, A))));
	}

	// The cell grows until the cut mesh fits the budget.
	float Cell = FMath::Max(InOptions.CellLength, 0.25f);
	int32 Pieces = 1;
	bool bFits = false;
	for (int32 Attempt = 0; Attempt < 48 && !bFits; ++Attempt, Cell *= 1.25f)
	{
		int64 Added = 0;
		for (const FRectQuad& Quad : Quads)
		{
			const int64 Columns = CountCuts(Quad.Min.X, Quad.Max.X, GridOrigin.X, Cell);
			const int64 Rows = InOptions.bAcrossOnly ? 1 : CountCuts(Quad.Min.Y, Quad.Max.Y, GridOrigin.Y, Cell);
			Added += (Columns + 1) * (Rows + 1) - 4;
		}
		Pieces = InOptions.bAcrossOnly ? 1 : FMath::Clamp(FMath::CeilToInt(LongestLoose / Cell), 1, 64);
		Added += static_cast<int64>(Loose.Num()) * ((Pieces + 1) * (Pieces + 2) / 2 - 3);
		bFits = OriginalCount + Added <= FMath::Min(InOptions.MaxVertices, LEXUI_MAX_VERTEX_COUNT - 1);
		if (bFits)
		{
			break;
		}
	}
	if (!bFits)
	{
		return 0;
	}

	TArray<FDreamUIMeshIndex> NewTriangles;
	NewTriangles.Reserve(Triangles.Num() * 4);
	auto AddVertex = [&](const int32* InCorners, const float* InWeights, int32 InCount) -> int32
	{
		FDreamUIOriginVertexData Origin;
		FDreamUIMeshVertex Vertex;
		MakeVertex(InOutGeometry, InCorners, InWeights, InCount, Origin, Vertex);
		const int32 Index = Origins.Num();
		Origins.Add(Origin);
		Vertices.Add(Vertex);
		if (OutSourceVertex != nullptr)
		{
			OutSourceVertex->Add(InCorners[0]);
		}
		return Index;
	};

	TArray<float, TInlineAllocator<64>> Xs, Ys;
	TArray<int32> GridVertex;
	for (const FRectQuad& Quad : Quads)
	{
		Cuts(Quad.Min.X, Quad.Max.X, GridOrigin.X, Cell, Xs);
		if (InOptions.bAcrossOnly)
		{
			Ys.Reset();
			Ys.Add(Quad.Min.Y);
			Ys.Add(Quad.Max.Y);
		}
		else
		{
			Cuts(Quad.Min.Y, Quad.Max.Y, GridOrigin.Y, Cell, Ys);
		}
		const int32 Columns = Xs.Num();
		const int32 Rows = Ys.Num();
		GridVertex.SetNumUninitialized(Columns * Rows);
		const float Width = Quad.Max.X - Quad.Min.X;
		const float Height = Quad.Max.Y - Quad.Min.Y;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const bool bLeft = Column == 0, bRight = Column == Columns - 1, bBottom = Row == 0, bTop = Row == Rows - 1;
				if ((bLeft || bRight) && (bBottom || bTop))
				{
					GridVertex[Row * Columns + Column] = Quad.Corner[(bRight ? 1 : 0) + (bTop ? 2 : 0)];
					continue;
				}
				const float Fx = bLeft ? 0.0f : (bRight ? 1.0f : (Xs[Column] - Quad.Min.X) / Width);
				const float Fy = bBottom ? 0.0f : (bTop ? 1.0f : (Ys[Row] - Quad.Min.Y) / Height);
				const float Weights[4] = { (1.0f - Fx) * (1.0f - Fy), Fx * (1.0f - Fy), (1.0f - Fx) * Fy, Fx * Fy };
				GridVertex[Row * Columns + Column] = AddVertex(Quad.Corner, Weights, 4);
			}
		}
		for (int32 Row = 0; Row + 1 < Rows; ++Row)
		{
			for (int32 Column = 0; Column + 1 < Columns; ++Column)
			{
				const int32 LeftBottom = GridVertex[Row * Columns + Column];
				const int32 RightBottom = GridVertex[Row * Columns + Column + 1];
				const int32 LeftTop = GridVertex[(Row + 1) * Columns + Column];
				const int32 RightTop = GridVertex[(Row + 1) * Columns + Column + 1];
				EmitTriangle(NewTriangles, LeftBottom, RightTop, LeftTop, Quad.bCounterClockwise);
				EmitTriangle(NewTriangles, LeftBottom, RightBottom, RightTop, Quad.bCounterClockwise);
			}
		}
	}

	TArray<int32> TriangleVertex;
	for (const int32 Triangle : Loose)
	{
		const int32 Corners[3] = { static_cast<int32>(Triangles[Triangle * 3]), static_cast<int32>(Triangles[Triangle * 3 + 1]), static_cast<int32>(Triangles[Triangle * 3 + 2]) };
		if (Pieces <= 1)
		{
			NewTriangles.Add(static_cast<FDreamUIMeshIndex>(Corners[0]));
			NewTriangles.Add(static_cast<FDreamUIMeshIndex>(Corners[1]));
			NewTriangles.Add(static_cast<FDreamUIMeshIndex>(Corners[2]));
			continue;
		}
		// Grid point (I, J), I + J <= Pieces: I steps towards the second corner, J towards the third.
		auto Slot = [Pieces](int32 I, int32 J) { return J * (Pieces + 1) - J * (J - 1) / 2 + I; };
		TriangleVertex.SetNumUninitialized((Pieces + 1) * (Pieces + 2) / 2);
		for (int32 J = 0; J <= Pieces; ++J)
		{
			for (int32 I = 0; I + J <= Pieces; ++I)
			{
				int32 Vertex = INDEX_NONE;
				if (I == 0 && J == 0)
				{
					Vertex = Corners[0];
				}
				else if (I == Pieces)
				{
					Vertex = Corners[1];
				}
				else if (J == Pieces)
				{
					Vertex = Corners[2];
				}
				else
				{
					const float Weights[3] = { static_cast<float>(Pieces - I - J) / Pieces, static_cast<float>(I) / Pieces, static_cast<float>(J) / Pieces };
					Vertex = AddVertex(Corners, Weights, 3);
				}
				TriangleVertex[Slot(I, J)] = Vertex;
			}
		}
		for (int32 J = 0; J < Pieces; ++J)
		{
			for (int32 I = 0; I + J < Pieces; ++I)
			{
				NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I, J)]));
				NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I + 1, J)]));
				NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I, J + 1)]));
				if (I + J + 1 < Pieces)
				{
					NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I + 1, J)]));
					NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I + 1, J + 1)]));
					NewTriangles.Add(static_cast<FDreamUIMeshIndex>(TriangleVertex[Slot(I, J + 1)]));
				}
			}
		}
	}
	Triangles = MoveTemp(NewTriangles);
	return Origins.Num() - OriginalCount;
}

void FDreamMeshDeform::Deform(FDreamUIGeometry& InOutGeometry, TFunctionRef<FVector2f(FVector2f)> InWarp)
{
	const FBox2f Bounds = MeasureBounds(InOutGeometry);
	const float Step = FMath::Max(Bounds.bIsValid ? Bounds.GetSize().GetMax() * 0.001f : 0.0f, 0.001f);
	for (FDreamUIOriginVertexData& Vertex : InOutGeometry.OriginVertices)
	{
		const FVector2f Point(Vertex.Position.Y, Vertex.Position.Z);
		const FVector2f Moved = InWarp(Point);
		const FVector2f Along(Vertex.Tangent.Y, Vertex.Tangent.Z);
		const float AlongLength = Along.Size();
		if (AlongLength > KINDA_SMALL_NUMBER)
		{
			const FVector2f Turned = (InWarp(Point + Along * (Step / AlongLength)) - Moved).GetSafeNormal();
			if (!Turned.IsNearlyZero())
			{
				Vertex.Tangent = FVector3f(Vertex.Tangent.X, Turned.X * AlongLength, Turned.Y * AlongLength);
			}
		}
		Vertex.Position.Y = Moved.X;
		Vertex.Position.Z = Moved.Y;
	}
}
