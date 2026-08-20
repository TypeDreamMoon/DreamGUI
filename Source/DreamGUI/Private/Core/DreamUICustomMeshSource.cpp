// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUICustomMeshSource.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamWidget.h"

DECLARE_CYCLE_STAT(TEXT("DreamGUICustomMesh Blueprint.OnFillMesh"), STAT_DreamGUICustomMesh_OnFillMesh, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamGUICustomMesh Blueprint.GetHitUV"), STAT_DreamGUICustomMesh_GetHitUV, STATGROUP_DreamGUI);
void UDreamUICustomMeshSource::OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamGUICustomMesh_OnFillMesh);
		ReceiveOnFillMesh(InDreamMesh, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
}
bool UDreamUICustomMeshSource::GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		SCOPE_CYCLE_COUNTER(STAT_DreamGUICustomMesh_GetHitUV);
		return ReceiveGetHitUV(InDreamMesh, InHitFaceIndex, InHitPoint, InLineStart, InLineEnd, OutHitUV);
	}
	return false;
}
bool UDreamUICustomMeshSource::SupportDrawcallBatching()const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveSupportDrawcallBatching();
	}
	return false;
}
bool UDreamUICustomMeshSource::GetHitUVbyFaceIndex(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, FVector2D& OutHitUV)const
{
	auto& Vertices = UIGeo->Vertices;
	auto& OriginVertices = UIGeo->OriginVertices;
	auto& Triangles = UIGeo->Triangles;

	if (InHitFaceIndex >= 0)
	{
		auto InverseTf = InDreamMesh->GetWidget()->GetWorldTransform().Inverse();
		auto LocalHitPoint = InverseTf.TransformPosition(InHitPoint);
		if (Triangles.IsValidIndex(InHitFaceIndex * 3 + 2))
		{
			int32 Index0 = Triangles[InHitFaceIndex * 3 + 0];
			int32 Index1 = Triangles[InHitFaceIndex * 3 + 1];
			int32 Index2 = Triangles[InHitFaceIndex * 3 + 2];

			auto Pos0 = (FVector)OriginVertices[Index0].Position;
			auto Pos1 = (FVector)OriginVertices[Index1].Position;
			auto Pos2 = (FVector)OriginVertices[Index2].Position;

			auto UV0 = (FVector2D)Vertices[Index0].TextureCoordinate[0];
			auto UV1 = (FVector2D)Vertices[Index1].TextureCoordinate[0];
			auto UV2 = (FVector2D)Vertices[Index2].TextureCoordinate[0];

			// Transform hit location from world to local space.
			// Find barycentric coords
			auto BaryCoords = FMath::ComputeBaryCentric2D(LocalHitPoint, Pos0, Pos1, Pos2);
			// Use to blend UVs
			OutHitUV = (BaryCoords.X * UV0) + (BaryCoords.Y * UV1) + (BaryCoords.Z * UV2);
			return true;
		}
	}
	return false;
}

#define MIN_SEG 4
#define MAX_SEG 32
void UDreamCustomMeshSource_Cylinder::OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = InDreamMesh->GetWidget();
	const auto Pivot = Widget->GetPivot();
	const auto SizeX = Widget->GetWidth();
	const auto SizeY = Widget->GetHeight();
	const auto Color = InDreamMesh->GetFinalColor();
	auto& Vertices = UIGeo->Vertices;
	auto& OriginVertices = UIGeo->OriginVertices;
	auto& Triangles = UIGeo->Triangles;

	auto ArcAngle = FMath::Max(FMath::DegreesToRadians(FMath::Abs(CylinderArcAngle)), 0.01f);
	auto ArcAngleSign = FMath::Sign(CylinderArcAngle);

	const int32 NumSegments = FMath::Lerp(MIN_SEG, MAX_SEG, ArcAngle / PI);

	const float Radius = SizeX / ArcAngle;
	const float Apothem = Radius * FMath::Cos(0.5f * ArcAngle);
	const float ChordLength = 2.0f * Radius * FMath::Sin(0.5f * ArcAngle);
	const float HalfChordLength = ChordLength * 0.5f;

	const float PivotOffsetX = ChordLength * (0.5 - Pivot.X);
	const float V = -SizeY * Pivot.Y;
	const float VL = SizeY * (1.0f - Pivot.Y);

	OriginVertices.Reserve(2 + NumSegments * 2);
	Vertices.Reserve(OriginVertices.Num());
	Triangles.Reserve(6 * NumSegments);
	const float RadiansPerStep = ArcAngle / NumSegments;
	float Angle = -ArcAngle * 0.5f;
	const FVector3f CenterPoint = FVector3f(0, HalfChordLength + PivotOffsetX, V);
	auto OriginVert = FDreamUIOriginVertexData();
	auto Vert = FDreamUIMeshVertex();
	Vert.Color = Color;
	OriginVert.Position = FVector3f(0, Radius * FMath::Sin(Angle) + PivotOffsetX, V);
	auto TangentX2D = FVector2f(CenterPoint) - FVector2f(OriginVert.Position);
	TangentX2D.Normalize();
	auto TangentZ = FVector3f(TangentX2D, 0);
	auto TangentY = FVector3f(0, 0, 1);
	auto TangentX = FVector3f::CrossProduct(TangentY, TangentZ);
	OriginVert.Normal = TangentZ;
	OriginVert.Tangent = TangentX;
	Vert.TextureCoordinate[0] = FVector2f(0, 1);
	OriginVertices.Add(OriginVert);
	Vertices.Add(Vert);
	OriginVert.Position.Z = VL;
	Vert.TextureCoordinate[0] = FVector2f(0, 0);
	OriginVertices.Add(OriginVert);
	Vertices.Add(Vert);

	float UVInterval = 1.0f / NumSegments;
	float UVX = 0;
	int32 TriangleIndex = 0;
	for (int32 Segment = 0; Segment < NumSegments; Segment++)
	{
		Angle += RadiansPerStep;
		UVX += UVInterval;

		OriginVert.Position = FVector3f(ArcAngleSign * (Radius * FMath::Cos(Angle) - Apothem), Radius * FMath::Sin(Angle) + PivotOffsetX, V);
		TangentX2D = FVector2f(OriginVert.Position) - FVector2f(CenterPoint);
		TangentX2D.Normalize();
		TangentZ = FVector3f(TangentX2D, 0);
		TangentX = FVector3f::CrossProduct(TangentY, TangentZ);
		OriginVert.Normal = TangentZ;
		OriginVert.Tangent = TangentX;
		Vert.TextureCoordinate[0] = FVector2f(UVX, 1);
		OriginVertices.Add(OriginVert);
		Vertices.Add(Vert);
		OriginVert.Position.Z = VL;
		Vert.TextureCoordinate[0] = FVector2f(UVX, 0);
		OriginVertices.Add(OriginVert);
		Vertices.Add(Vert);

		Triangles.Add(TriangleIndex);
		Triangles.Add(TriangleIndex + 3);
		Triangles.Add(TriangleIndex + 1);
		Triangles.Add(TriangleIndex);
		Triangles.Add(TriangleIndex + 2);
		Triangles.Add(TriangleIndex + 3);
		TriangleIndex += 2;
	}
}
bool UDreamCustomMeshSource_Cylinder::GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)const
{
	return GetHitUVbyFaceIndex(InDreamMesh, InHitFaceIndex, InHitPoint, OutHitUV);
}


UDreamCustomMeshSource_CurvyPlane::UDreamCustomMeshSource_CurvyPlane()
{
	ShapeCurve.EditorCurveData.SetKeys({
		FRichCurveKey(0, 0, 0, 0, ERichCurveInterpMode::RCIM_Cubic),
		FRichCurveKey(0.5, 1, 0, 0, ERichCurveInterpMode::RCIM_Cubic),
		FRichCurveKey(1, 0, 0, 0, ERichCurveInterpMode::RCIM_Cubic)
		});
}
void UDreamCustomMeshSource_CurvyPlane::OnFillMesh(UDreamVisualBatchMesh* InDreamMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = InDreamMesh->GetWidget();
	Segment = FMath::Clamp(Segment, 1, 200);
	const auto Pivot = Widget->GetPivot();
	const auto SizeX = Widget->GetWidth();
	const auto SizeY = Widget->GetHeight();
	const auto Color = InDreamMesh->GetFinalColor();

	const auto PivotOffsetX = SizeX * (0.5f - Pivot.X);
	const auto PivotOffsetY = SizeY * (0.5f - Pivot.Y);
	const float HalfSizeX = SizeX * 0.5f;
	const float HalfSizeY = SizeY * 0.5f;

	auto& Triangles = UIGeo->Triangles;
	auto& Vertices = UIGeo->Vertices;
	auto& OriginVertices = UIGeo->OriginVertices;

	int triangleIndicesCount = Segment * 6;
	Triangles.SetNumUninitialized(triangleIndicesCount);
	int vertIndex = 0, triangleIndex = 0;
	for (int SegIndex = 0; SegIndex < Segment; SegIndex++)
	{
		vertIndex = SegIndex * 2;
		triangleIndex = SegIndex * 6;
		Triangles[triangleIndex] = vertIndex;
		Triangles[triangleIndex + 1] = vertIndex + 2;
		Triangles[triangleIndex + 2] = vertIndex + 3;

		Triangles[triangleIndex + 3] = vertIndex;
		Triangles[triangleIndex + 4] = vertIndex + 3;
		Triangles[triangleIndex + 5] = vertIndex + 1;
	}

	int verticesCount = 2 + Segment * 2;
	Vertices.SetNumUninitialized(verticesCount);
	OriginVertices.SetNumUninitialized(verticesCount);
	float PosX = -HalfSizeX;
	float PosXInterval = SizeX / Segment;
	float UVX = 0;
	float UVXInterval = 1.0f / Segment;
	int VertIndex = 0;
	for (int WidthIndex = 0; WidthIndex <= Segment; WidthIndex++)
	{
		auto OffsetByCurve = ShapeCurve.GetRichCurve()->Eval(UVX) * CurveScale;
		auto OriginVert = FDreamUIOriginVertexData();
		OriginVert.Position = FVector3f(0, PivotOffsetX + PosX, PivotOffsetY - HalfSizeY + OffsetByCurve);
		auto Vert = FDreamUIMeshVertex();
		Vert.Color = Color;
		Vert.TextureCoordinate[0] = FVector2f(UVX, 1);
		OriginVertices[VertIndex] = OriginVert;
		Vertices[VertIndex++] = Vert;
		OriginVert.Position.Z += SizeY;
		Vert.TextureCoordinate[0] = FVector2f(UVX, 0);
		OriginVertices[VertIndex] = OriginVert;
		Vertices[VertIndex++] = Vert;
		PosX += PosXInterval;
		UVX += UVXInterval;
	}
}
bool UDreamCustomMeshSource_CurvyPlane::GetHitUV(const UDreamVisualBatchMesh* InDreamMesh, const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV) const
{
	return GetHitUVbyFaceIndex(InDreamMesh, InHitFaceIndex, InHitPoint, OutHitUV);
}
