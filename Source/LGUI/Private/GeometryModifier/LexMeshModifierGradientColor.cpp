// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "GeometryModifier/LexMeshModifierGradientColor.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Core/Components/LexText.h"

ULexMeshModifierGradientColor::ULexMeshModifierGradientColor()
{
}
void ULexMeshModifierGradientColor::ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor)
{
	if (multiplySourceAlpha)
	{
		InOutColor.A = (uint8)(FLexUIUtils::Color255To1_Table[InOutColor.A] * InTintColor.A);
		InOutColor.R = InTintColor.R;
		InOutColor.G = InTintColor.G;
		InOutColor.B = InTintColor.B;
	}
	else
	{
		InOutColor = InTintColor;
	}
}
void ULexMeshModifierGradientColor::ModifyUIGeometry(
	FLexUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto& triangles = InGeometry.Triangles;
	auto& vertices = InGeometry.Vertices;

	auto vertexCount = vertices.Num();
	int32 triangleCount = triangles.Num();
	if (triangleCount == 0 || vertexCount == 0)return;

	switch (directionType)
	{
	case ELexMeshModifierGradientColorDirection::BottomToTop:
	{
		for (int i = 0; i < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
		}
	}
	break;
	case ELexMeshModifierGradientColorDirection::TopToBottom:
	{
		for (int i = 0; i < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
		}
	}
	break;
	case ELexMeshModifierGradientColorDirection::LeftToRight:
	{
		for (int i = 0; i < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
		}
	}
	break;
	case ELexMeshModifierGradientColorDirection::RightToLeft:
	{
		for (int i = 0; i < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, color2);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
			ApplyColorAndAlpha(vertices[i++].Color, color1);
		}
	}
	break;
	case ELexMeshModifierGradientColorDirection::FourCorner:
	{
		for (int i = 0; i < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, color1);
			ApplyColorAndAlpha(vertices[i++].Color, color2);
			ApplyColorAndAlpha(vertices[i++].Color, color3);
			ApplyColorAndAlpha(vertices[i++].Color, color4);
		}
	}
	break;
	}
}
