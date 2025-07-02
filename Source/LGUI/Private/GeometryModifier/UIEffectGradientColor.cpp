// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "GeometryModifier/UIEffectGradientColor.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "LGUI/Public/Core/Components/LexText.h"

UUIEffectGradientColor::UUIEffectGradientColor()
{
}
void UUIEffectGradientColor::ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor)
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
void UUIEffectGradientColor::ModifyUIGeometry(
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
	case EUIEffectGradientColorDirection::BottomToTop:
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
	case EUIEffectGradientColorDirection::TopToBottom:
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
	case EUIEffectGradientColorDirection::LeftToRight:
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
	case EUIEffectGradientColorDirection::RightToLeft:
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
	case EUIEffectGradientColorDirection::FourCornor:
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
