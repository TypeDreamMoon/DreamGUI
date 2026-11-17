// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIEdgeAntiAliasing.h"

#include "Core/DreamUIGeometry.h"

namespace DreamUIEdgeAntiAliasingLocal
{
	/** A local position on the plane the element is drawn in: local Y across, local Z up. */
	FORCEINLINE FVector2f Flat(const FVector3f& InPosition)
	{
		return FVector2f(InPosition.Y, InPosition.Z);
	}

	FORCEINLINE float Cross(const FVector2f& A, const FVector2f& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** InPoint's weights in the triangle (A, B, C); false when the triangle has no area. */
	bool Barycentric(const FVector2f& InPoint, const FVector2f& A, const FVector2f& B, const FVector2f& C, FVector3f& OutWeights)
	{
		const float Area = Cross(B - A, C - A);
		if (FMath::Abs(Area) <= UE_SMALL_NUMBER)
		{
			return false;
		}
		const float WeightB = Cross(InPoint - A, C - A) / Area;
		const float WeightC = Cross(B - A, InPoint - A) / Area;
		OutWeights = FVector3f(1.0f - WeightB - WeightC, WeightB, WeightC);
		return true;
	}

	/** How often an edge is used, and by the first triangle that used it: its two ends in that triangle's winding, and the third corner. */
	struct FEdgeUse
	{
		int32 Count = 0;
		int32 A = 0;
		int32 B = 0;
		int32 Third = 0;
	};

	/** An edge of the outline: its two vertices in the winding of the triangle that owns it, and the way out of the mesh. */
	struct FOutlineEdge
	{
		int32 A = 0;
		int32 B = 0;
		int32 WeldA = 0;
		int32 WeldB = 0;
		float Length = 0.0f;
		FVector2f Normal = FVector2f::ZeroVector;
	};

	/** A vertex of the outline, as the outline edges that meet at it say. */
	struct FCorner
	{
		FVector2f First = FVector2f::ZeroVector;
		FVector2f Sum = FVector2f::ZeroVector;
		int32 Edges = 0;
		float ShortestEdge = MAX_flt;
	};
}

DreamUIEdgeAntiAliasing::FResult DreamUIEdgeAntiAliasing::Apply(FDreamUIGeometry& InOutGeometry, const float InPixel)
{
	using namespace DreamUIEdgeAntiAliasingLocal;

	FResult Result;
	TArray<FDreamUIOriginVertexData>& Origins = InOutGeometry.OriginVertices;
	TArray<FDreamUIMeshVertex>& Vertices = InOutGeometry.Vertices;
	TArray<FDreamUIMeshIndex>& Triangles = InOutGeometry.Triangles;
	const int32 VertexCount = Origins.Num();
	const int32 IndexCount = Triangles.Num();
	if (!FMath::IsFinite(InPixel) || InPixel <= 0.0f || VertexCount < 3 || Vertices.Num() != VertexCount
		|| IndexCount < 3 || IndexCount % 3 != 0)
	{
		return Result;
	}
	// The strip is laid in the plane the element is drawn in; a mesh that leaves it (a 3D deform) is not touched.
	const float FlatTolerance = InPixel * 1.0e-3f;
	for (const FDreamUIOriginVertexData& Origin : Origins)
	{
		if (Origin.Position.ContainsNaN() || FMath::Abs(Origin.Position.X) > FlatTolerance)
		{
			return Result;
		}
	}

	// Vertices at the same place are one vertex of the outline, whatever else differs between them: a mesh that repeats a
	// vertex where its texture or colour changes has no outline along that seam.
	const double Quantum = (double)InPixel / 64.0;
	TArray<int32> Weld;
	Weld.SetNumUninitialized(VertexCount);
	TArray<FVector2f> WeldPosition;
	WeldPosition.Reserve(VertexCount);
	{
		TMap<FInt64Point, int32> WeldOfKey;
		WeldOfKey.Reserve(VertexCount);
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const FVector2f Position = Flat(Origins[Index].Position);
			const FInt64Point Key((int64)FMath::RoundToDouble(Position.X / Quantum), (int64)FMath::RoundToDouble(Position.Y / Quantum));
			if (const int32* Found = WeldOfKey.Find(Key))
			{
				Weld[Index] = *Found;
			}
			else
			{
				Weld[Index] = WeldPosition.Add(Position);
				WeldOfKey.Add(Key, Weld[Index]);
			}
		}
	}

	// Every edge with the number of triangles that use it; an edge used once is the outline.
	const int32 TriangleCount = IndexCount / 3;
	TMap<uint64, FEdgeUse> Edges;
	Edges.Reserve(IndexCount);
	for (int32 Triangle = 0; Triangle < TriangleCount; ++Triangle)
	{
		const int32 Corner[3] = { (int32)Triangles[Triangle * 3], (int32)Triangles[Triangle * 3 + 1], (int32)Triangles[Triangle * 3 + 2] };
		if (Corner[0] >= VertexCount || Corner[1] >= VertexCount || Corner[2] >= VertexCount)
		{
			return Result;
		}
		const int32 Welded[3] = { Weld[Corner[0]], Weld[Corner[1]], Weld[Corner[2]] };
		if (Welded[0] == Welded[1] || Welded[1] == Welded[2] || Welded[0] == Welded[2])
		{
			continue;
		}
		const FVector2f P0 = WeldPosition[Welded[0]];
		if (FMath::Abs(Cross(WeldPosition[Welded[1]] - P0, WeldPosition[Welded[2]] - P0)) <= (float)(Quantum * Quantum))
		{
			continue;
		}
		for (int32 Side = 0; Side < 3; ++Side)
		{
			const int32 Next = (Side + 1) % 3;
			const uint32 Low = (uint32)FMath::Min(Welded[Side], Welded[Next]);
			const uint32 High = (uint32)FMath::Max(Welded[Side], Welded[Next]);
			FEdgeUse& Use = Edges.FindOrAdd(((uint64)Low << 32) | (uint64)High);
			if (Use.Count++ == 0)
			{
				Use.A = Corner[Side];
				Use.B = Corner[Next];
				Use.Third = Corner[(Side + 2) % 3];
			}
		}
	}

	TArray<FOutlineEdge> Outline;
	for (const TPair<uint64, FEdgeUse>& Pair : Edges)
	{
		const FEdgeUse& Use = Pair.Value;
		if (Use.Count != 1)
		{
			continue;
		}
		FOutlineEdge Edge;
		Edge.A = Use.A;
		Edge.B = Use.B;
		Edge.WeldA = Weld[Use.A];
		Edge.WeldB = Weld[Use.B];
		const FVector2f From = WeldPosition[Edge.WeldA];
		const FVector2f Along = WeldPosition[Edge.WeldB] - From;
		Edge.Length = Along.Size();
		if (Edge.Length <= (float)Quantum)
		{
			continue;
		}
		// Out of the mesh is away from the third corner of the triangle that owns the edge.
		Edge.Normal = FVector2f(Along.Y, -Along.X) / Edge.Length;
		if (FVector2f::DotProduct(Edge.Normal, WeldPosition[Weld[Use.Third]] - From) > 0.0f)
		{
			Edge.Normal = -Edge.Normal;
		}
		Outline.Add(Edge);
	}
	if (Outline.Num() == 0 || (int64)VertexCount + (int64)Outline.Num() * 2 >= (int64)LEXUI_MAX_VERTEX_COUNT)
	{
		return Result;
	}

	// Each outline vertex moves along the bisector of the edges that meet at it, by as much as keeps both of them half a
	// pixel from where they were (a mitre, held to four times that at a sharp corner). Inward it moves no more than a
	// quarter of its shortest edge, so a shape smaller than a pixel thins rather than turns inside out.
	const float Half = InPixel * 0.5f;
	TArray<FCorner> Corners;
	Corners.SetNum(WeldPosition.Num());
	for (const FOutlineEdge& Edge : Outline)
	{
		for (const int32 Welded : { Edge.WeldA, Edge.WeldB })
		{
			FCorner& Corner = Corners[Welded];
			if (Corner.Edges == 0)
			{
				Corner.First = Edge.Normal;
			}
			Corner.Sum += Edge.Normal;
			++Corner.Edges;
			Corner.ShortestEdge = FMath::Min(Corner.ShortestEdge, Edge.Length);
		}
	}
	TArray<FVector2f> OutOffset;
	TArray<FVector2f> InOffset;
	OutOffset.SetNumZeroed(WeldPosition.Num());
	InOffset.SetNumZeroed(WeldPosition.Num());
	for (int32 Welded = 0; Welded < Corners.Num(); ++Welded)
	{
		const FCorner& Corner = Corners[Welded];
		if (Corner.Edges == 0)
		{
			continue;
		}
		FVector2f Direction = Corner.Sum.GetSafeNormal();
		float Mitre = 1.0f;
		if (Direction.IsNearlyZero())
		{
			// Two edges turning straight back on each other: a spike with no inside; out along the first.
			Direction = Corner.First;
		}
		else if (Corner.Edges == 2)
		{
			Mitre = 1.0f / FMath::Max(FVector2f::DotProduct(Direction, Corner.First), 0.25f);
		}
		OutOffset[Welded] = Direction * (Half * Mitre);
		InOffset[Welded] = Direction * FMath::Min(Half * Mitre, Corner.ShortestEdge * 0.25f);
	}

	// What every vertex was, before the outline moves: the strips' outer copies take it, and the moved vertices read
	// their new texture coordinate and colour off it.
	TArray<FVector2f> OriginalPosition;
	TArray<FVector2f> OriginalUV;
	TArray<FColor> OriginalColor;
	OriginalPosition.SetNumUninitialized(VertexCount);
	OriginalUV.SetNumUninitialized(VertexCount);
	OriginalColor.SetNumUninitialized(VertexCount);
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		OriginalPosition[Index] = Flat(Origins[Index].Position);
		OriginalUV[Index] = Vertices[Index].TextureCoordinate[0];
		OriginalColor[Index] = Vertices[Index].Color;
	}

	// The triangles each vertex is a corner of: the moved vertex is looked for in its own, so it reads a texture
	// coordinate from its own side of any seam.
	TArray<int32> FirstTriangle;
	TArray<int32> TrianglesOfVertex;
	FirstTriangle.SetNumZeroed(VertexCount + 1);
	for (int32 Index = 0; Index < IndexCount; ++Index)
	{
		++FirstTriangle[(int32)Triangles[Index] + 1];
	}
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		FirstTriangle[Index + 1] += FirstTriangle[Index];
	}
	TrianglesOfVertex.SetNumUninitialized(IndexCount);
	{
		TArray<int32> Filled = FirstTriangle;
		for (int32 Index = 0; Index < IndexCount; ++Index)
		{
			TrianglesOfVertex[Filled[(int32)Triangles[Index]]++] = Index / 3;
		}
	}

	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		const int32 Welded = Weld[Index];
		if (Corners[Welded].Edges == 0)
		{
			continue;
		}
		const FVector2f Target = OriginalPosition[Index] - InOffset[Welded];
		for (int32 Slot = FirstTriangle[Index]; Slot < FirstTriangle[Index + 1]; ++Slot)
		{
			const int32 Triangle = TrianglesOfVertex[Slot];
			const int32 I0 = (int32)Triangles[Triangle * 3];
			const int32 I1 = (int32)Triangles[Triangle * 3 + 1];
			const int32 I2 = (int32)Triangles[Triangle * 3 + 2];
			FVector3f Weights;
			if (!Barycentric(Target, OriginalPosition[I0], OriginalPosition[I1], OriginalPosition[I2], Weights) || Weights.GetMin() < -1.0e-3f)
			{
				continue;
			}
			Vertices[Index].TextureCoordinate[0] = OriginalUV[I0] * Weights.X + OriginalUV[I1] * Weights.Y + OriginalUV[I2] * Weights.Z;
			const auto Channel = [&Weights](uint8 A, uint8 B, uint8 C)
			{
				return (uint8)FMath::Clamp(FMath::RoundToInt(A * Weights.X + B * Weights.Y + C * Weights.Z), 0, 255);
			};
			const FColor& C0 = OriginalColor[I0];
			const FColor& C1 = OriginalColor[I1];
			const FColor& C2 = OriginalColor[I2];
			Vertices[Index].Color = FColor(Channel(C0.R, C1.R, C2.R), Channel(C0.G, C1.G, C2.G), Channel(C0.B, C1.B, C2.B), Channel(C0.A, C1.A, C2.A));
			break;
		}
		Origins[Index].Position = FVector3f(Origins[Index].Position.X, Target.X, Target.Y);
	}

	// The outer half of each strip: a transparent copy of the edge's two vertices half a pixel out, with the texture
	// coordinate and colour they had on the edge itself. Two triangles per edge, wound as the triangle that owns it.
	Origins.Reserve(VertexCount + Outline.Num() * 2);
	Vertices.Reserve(VertexCount + Outline.Num() * 2);
	Triangles.Reserve(IndexCount + Outline.Num() * 6);
	const auto AddOuter = [&](const int32 InFrom, const int32 InWelded)
	{
		FDreamUIOriginVertexData Origin = Origins[InFrom];
		const FVector2f Position = OriginalPosition[InFrom] + OutOffset[InWelded];
		Origin.Position = FVector3f(Origin.Position.X, Position.X, Position.Y);
		FDreamUIMeshVertex Vertex = Vertices[InFrom];
		Vertex.TextureCoordinate[0] = OriginalUV[InFrom];
		Vertex.Color = OriginalColor[InFrom];
		Vertex.Color.A = 0;
		Origins.Add(Origin);
		return Vertices.Add(Vertex);
	};
	for (const FOutlineEdge& Edge : Outline)
	{
		const int32 OuterA = AddOuter(Edge.A, Edge.WeldA);
		const int32 OuterB = AddOuter(Edge.B, Edge.WeldB);
		// The owning triangle runs A -> B with its third corner on the inside; the strip lies outside, so B -> A first.
		Triangles.Add((FDreamUIMeshIndex)Edge.B);
		Triangles.Add((FDreamUIMeshIndex)Edge.A);
		Triangles.Add((FDreamUIMeshIndex)OuterA);
		Triangles.Add((FDreamUIMeshIndex)Edge.B);
		Triangles.Add((FDreamUIMeshIndex)OuterA);
		Triangles.Add((FDreamUIMeshIndex)OuterB);
	}
	// No longer the quad a clip underlay reads corners off.
	InOutGeometry.DrawnQuad = FDreamUIGeometry::EDrawnQuad::None;

	Result.OutlineEdges = Outline.Num();
	Result.bApplied = true;
	return Result;
}
