// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierGradientColor.h"
#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/Components/DreamText.h"

UDreamMeshModifierGradientColor::UDreamMeshModifierGradientColor()
{
}
void UDreamMeshModifierGradientColor::ApplyColorAndAlpha(FColor& InOutColor, FColor InTintColor)
{
	if (bMultiplySourceAlpha)
	{
		InOutColor.A = (uint8)(FDreamUIUtils::ByteToFloat01(InOutColor.A) * InTintColor.A);
		InOutColor.R = InTintColor.R;
		InOutColor.G = InTintColor.G;
		InOutColor.B = InTintColor.B;
	}
	else
	{
		InOutColor = InTintColor;
	}
}
void UDreamMeshModifierGradientColor::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto& triangles = InGeometry.Triangles;
	auto& vertices = InGeometry.Vertices;

	auto vertexCount = vertices.Num();
	int32 triangleCount = triangles.Num();
	if (triangleCount == 0 || vertexCount == 0)return;

	// Every direction below paints a whole quad per pass and steps the cursor four times inside the
	// loop body, so the bound has to clear all four -- testing only the first corner walks off the
	// end of a geometry whose vertex count is not a multiple of four. That is not a hypothetical
	// shape: a modifier earlier in the list is free to append vertices in any number, and the
	// gradient is the one modifier here that reads a count it did not produce itself.
	switch (DirectionType)
	{
	case EDreamMeshModifierGradientColorDirection::BottomToTop:
	{
		for (int i = 0; i + 3 < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
		}
	}
	break;
	case EDreamMeshModifierGradientColorDirection::TopToBottom:
	{
		for (int i = 0; i + 3 < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
		}
	}
	break;
	case EDreamMeshModifierGradientColorDirection::LeftToRight:
	{
		for (int i = 0; i + 3 < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
		}
	}
	break;
	case EDreamMeshModifierGradientColorDirection::RightToLeft:
	{
		for (int i = 0; i + 3 < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
		}
	}
	break;
	case EDreamMeshModifierGradientColorDirection::FourCorner:
	{
		for (int i = 0; i + 3 < vertexCount;)
		{
			ApplyColorAndAlpha(vertices[i++].Color, Color1);
			ApplyColorAndAlpha(vertices[i++].Color, Color2);
			ApplyColorAndAlpha(vertices[i++].Color, Color3);
			ApplyColorAndAlpha(vertices[i++].Color, Color4);
		}
	}
	break;
	}
}

void UDreamMeshModifierGradientColor::SetDirectionType(EDreamMeshModifierGradientColorDirection Value)
{
	if (DirectionType != Value)
	{
		DirectionType = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
void UDreamMeshModifierGradientColor::SetMultiplySourceAlpha(bool Value)
{
	if (bMultiplySourceAlpha != Value)
	{
		bMultiplySourceAlpha = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
void UDreamMeshModifierGradientColor::SetColor1(FColor Value)
{
	if (Color1 != Value)
	{
		Color1 = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
void UDreamMeshModifierGradientColor::SetColor2(FColor Value)
{
	if (Color2 != Value)
	{
		Color2 = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
void UDreamMeshModifierGradientColor::SetColor3(FColor Value)
{
	if (Color3 != Value)
	{
		Color3 = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
void UDreamMeshModifierGradientColor::SetColor4(FColor Value)
{
	if (Color4 != Value)
	{
		Color4 = Value;
		if (GetVisualBatchMesh())GetVisualBatchMesh()->MarkColorDirty();
	}
}
