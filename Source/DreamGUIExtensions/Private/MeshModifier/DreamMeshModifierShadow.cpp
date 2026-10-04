// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierShadow.h"
#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"


UDreamMeshModifierShadow::UDreamMeshModifierShadow()
{
	// A modifier works when the geometry is built, never each frame or when its widget moves.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}
void UDreamMeshModifierShadow::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto& triangles = InGeometry.Triangles;
	auto& originVertices = InGeometry.OriginVertices;
	auto& vertices = InGeometry.Vertices;

	auto vertexCount = originVertices.Num();
	int32 triangleCount = triangles.Num();
	if (triangleCount == 0 || vertexCount == 0)return;
	
	const int32 singleChannelTriangleIndicesCount = triangleCount;
	const int32 singleChannelVerticesCount = vertexCount;
	// The copy doubles the vertex count, and a triangle index cannot address past LEXUI_MAX_VERTEX_COUNT: a mesh whose copy
	// would not fit was dropped whole, its shadow and itself. It is drawn without its shadow instead, as Outline and
	// LongShadow leave out what does not fit.
	if (LEXUI_MAX_VERTEX_COUNT / singleChannelVerticesCount < 2)
	{
		if (!bLoggedVertexLimitWarning)
		{
			bLoggedVertexLimitWarning = true;
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d mesh is too large to cast a shadow (%d vertices, limit %d for the mesh and its copy); no shadow drawn.")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, singleChannelVerticesCount, LEXUI_MAX_VERTEX_COUNT);
		}
		return;
	}
	//create additional triangle pass
	triangles.AddUninitialized(singleChannelTriangleIndicesCount);
	//put origin triangles on last pass, this will make the origin triangle render at top
	for (int i = singleChannelTriangleIndicesCount, j = 0; j < singleChannelTriangleIndicesCount; i++, j++)
	{
		auto index = triangles[j];
		triangles[i] = index;
		triangles[j] = index + singleChannelVerticesCount;
	}
	
	vertexCount = singleChannelVerticesCount + singleChannelVerticesCount;
	originVertices.AddDefaulted(singleChannelVerticesCount);
	vertices.AddDefaulted(singleChannelVerticesCount);

	for (int channelIndex1 = singleChannelVerticesCount, channelIndexOrigin = 0; channelIndex1 < vertexCount; channelIndex1++, channelIndexOrigin++)
	{
		auto originVertPos = originVertices[channelIndexOrigin].Position;
		originVertPos += ShadowOffset;
		originVertices[channelIndex1].Position = originVertPos;

		if (bMultiplySourceAlpha)
		{
			auto& vertColor = vertices[channelIndex1].Color;
			vertColor.A = (uint8)(FDreamUIUtils::ByteToFloat01(vertices[channelIndexOrigin].Color.A) * ShadowColor.A);
			vertColor.R = ShadowColor.R;
			vertColor.G = ShadowColor.G;
			vertColor.B = ShadowColor.B;
		}
		else
		{
			vertices[channelIndex1].Color = ShadowColor;
		}

		// The bound is the vertex's own channel count, not the engine's MAX_STATIC_TEXCOORDS. A
		// DreamGUI vertex carries four texture coordinates where a static mesh vertex carries eight,
		// so counting to the engine's number writes four FVector2f past the end of the array member
		// and straight through the vertex's tangents into the vertex behind it.
		for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
		{
			vertices[channelIndex1].TextureCoordinate[i] = vertices[channelIndexOrigin].TextureCoordinate[i];
		}
		// Copied by name now that the loop above stops where it should. While it ran to the engine's
		// channel count these two fields were the first thing it wrote past the end of the array, so
		// the copy inherited the source's tangents by accident -- and the vertices behind the copies
		// arrive from AddDefaulted with nothing written into them at all, so a canvas that asks for
		// normals and tangents would light the shadow off whatever was in that memory.
		vertices[channelIndex1].TangentX = vertices[channelIndexOrigin].TangentX;
		vertices[channelIndex1].TangentZ = vertices[channelIndexOrigin].TangentZ;
	}
	// The copy paints nothing, and a colour glyph's copy is its silhouette.
	PrepareMeshCopies(InGeometry, singleChannelVerticesCount);
}

void UDreamMeshModifierShadow::SetShadowColor(FColor Value)
{
	if (ShadowColor != Value)
	{
		ShadowColor = Value;
		if (auto Visual = GetVisualBatchMesh())Visual->MarkColorDirty();
	}
}
void UDreamMeshModifierShadow::SetShadowOffset(FVector3f Value)
{
	if (ShadowOffset != Value)
	{
		ShadowOffset = Value;
		if (auto Visual = GetVisualBatchMesh())Visual->MarkVertexPositionDirty();
	}
}
void UDreamMeshModifierShadow::SetMultiplySourceAlpha(bool Value)
{
	if (bMultiplySourceAlpha != Value)
	{
		bMultiplySourceAlpha = Value;
		if (auto Visual = GetVisualBatchMesh())Visual->MarkColorDirty();
	}
}