// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierPositionAsUV.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"


UDreamMeshModifierPositionAsUV::UDreamMeshModifierPositionAsUV()
{
}

void UDreamMeshModifierPositionAsUV::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto DreamVisual = GetVisualBatchMesh();
	if (!DreamVisual)return;
	auto RenderCanvas = DreamVisual->GetWidget()->GetRenderCanvas();
	auto& originVertices = InGeometry.OriginVertices;
	switch (UVChannel)
	{
	case 0:
	{
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[0] = FVector2f(vert.Y, vert.Z) * Scale;
		}
	}
	break;
	case 1:
	{
		if (!RenderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[1] = FVector2f(vert.Y, vert.Z) * Scale;
		}
	}
	break;
	case 2:
	{
		if (!RenderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[2] = FVector2f(vert.Y, vert.Z) * Scale;
		}
	}
	break;
	case 3:
	{
		if (!RenderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[3] = FVector2f(vert.Y, vert.Z) * Scale;
		}
	}
	break;
	}
}