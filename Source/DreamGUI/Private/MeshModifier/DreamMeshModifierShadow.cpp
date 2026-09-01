// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierShadow.h"
#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"


UDreamMeshModifierShadow::UDreamMeshModifierShadow()
{
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