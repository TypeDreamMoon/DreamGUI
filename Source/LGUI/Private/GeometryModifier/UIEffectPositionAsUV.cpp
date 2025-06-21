// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "GeometryModifier/UIEffectPositionAsUV.h"
#include "LGUI/Public/Core/Components/LGUICanvas.h"
#include "LGUI.h"


UUIEffectPositionAsUV::UUIEffectPositionAsUV()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUIEffectPositionAsUV::ModifyUIGeometry(
	FLexUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto uiRenderable = GetUIRenderable();
	if (!uiRenderable)return;
	auto renderCanvas = uiRenderable->GetRenderCanvas();
	auto& originVertices = InGeometry.OriginVertices;
	switch (uvChannel)
	{
	case 0:
	{
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[0] = FVector2f(vert.Y, vert.Z);
		}
	}
	break;
	case 1:
	{
		if (!renderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[1] = FVector2f(vert.Y, vert.Z);
		}
	}
	break;
	case 2:
	{
		if (!renderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[2] = FVector2f(vert.Y, vert.Z);
		}
	}
	break;
	case 3:
	{
		if (!renderCanvas)return;
		auto& vertices = InGeometry.Vertices;
		auto vertexCount = vertices.Num();
		for (int i = 0; i < vertexCount; i++)
		{
			auto& vert = originVertices[i].Position;
			vertices[i].TextureCoordinate[3] = FVector2f(vert.Y, vert.Z);
		}
	}
	break;
	}
}