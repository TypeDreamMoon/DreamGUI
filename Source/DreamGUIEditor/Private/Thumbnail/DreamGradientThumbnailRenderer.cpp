// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Thumbnail/DreamGradientThumbnailRenderer.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Core/DreamGradientAsset.h"
#include "Engine/Texture2D.h"
#include "GlobalRenderResources.h"
#include "Styling/AppStyle.h"
#include "TextureResource.h"

namespace DreamGradientThumbnailLocal
{
	/** Cells a side: the gradient is exact at every vertex and blended between them, so a hard stop is one cell wide. */
	constexpr int32 CellsPerSide = 48;
}

bool UDreamGradientThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	return Cast<UDreamGradientAsset>(Object) != nullptr;
}

void UDreamGradientThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	using namespace DreamGradientThumbnailLocal;
	const UDreamGradientAsset* Asset = Cast<UDreamGradientAsset>(Object);
	if (Asset == nullptr || Canvas == nullptr || Width == 0 || Height == 0)
	{
		return;
	}
	const FDreamGradient& Gradient = Asset->GetGradient();

	// What a transparent stop lets through. The style's Checkerboard brush is an image file with no texture behind it, so
	// the engine's grid texture stands in, as the sprite thumbnails do; rooted, because nothing else holds it. Decoration:
	// without either the gradient draws alone.
	static UTexture2D* CheckerTexture = nullptr;
	if (CheckerTexture == nullptr)
	{
		const FSlateBrush* CheckerBrush = FAppStyle::GetBrush("Checkerboard");
		CheckerTexture = CheckerBrush != nullptr ? Cast<UTexture2D>(CheckerBrush->GetResourceObject()) : nullptr;
		if (CheckerTexture == nullptr)
		{
			CheckerTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"), nullptr, LOAD_None, nullptr);
			if (CheckerTexture != nullptr)
			{
				CheckerTexture->AddToRoot();
			}
		}
	}
	if (CheckerTexture != nullptr && CheckerTexture->GetResource() != nullptr)
	{
		Canvas->DrawTile((double)X, (double)Y, (double)Width, (double)Height, 0.0f, 0.0f, 4.0f, 4.0f,
			FLinearColor(0.15f, 0.15f, 0.15f), CheckerTexture->GetResource(), false);
	}

	// The tile as the box: square, so angles read as they would on a square text block.
	const float Aspect = (float)Width / (float)Height;
	constexpr int32 VerticesPerSide = CellsPerSide + 1;
	TArray<FLinearColor> Colors;
	Colors.SetNumUninitialized(VerticesPerSide * VerticesPerSide);
	for (int32 Row = 0; Row < VerticesPerSide; ++Row)
	{
		for (int32 Column = 0; Column < VerticesPerSide; ++Column)
		{
			const FVector2f BoxUV((float)Column / CellsPerSide, (float)Row / CellsPerSide);
			Colors[Row * VerticesPerSide + Column] = Gradient.Evaluate(BoxUV, Aspect);
		}
	}
	const auto VertexPosition = [X, Y, Width, Height](int32 InColumn, int32 InRow)
	{
		return FVector2D(X + (double)Width * InColumn / CellsPerSide, Y + (double)Height * InRow / CellsPerSide);
	};
	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(CellsPerSide * CellsPerSide * 2);
	for (int32 Row = 0; Row < CellsPerSide; ++Row)
	{
		for (int32 Column = 0; Column < CellsPerSide; ++Column)
		{
			const int32 TopLeft = Row * VerticesPerSide + Column;
			const int32 BottomLeft = TopLeft + VerticesPerSide;
			FCanvasUVTri& Upper = Triangles.AddDefaulted_GetRef();
			Upper.V0_Pos = VertexPosition(Column, Row);			Upper.V0_UV = FVector2D::ZeroVector;	Upper.V0_Color = Colors[TopLeft];
			Upper.V1_Pos = VertexPosition(Column + 1, Row);		Upper.V1_UV = FVector2D::ZeroVector;	Upper.V1_Color = Colors[TopLeft + 1];
			Upper.V2_Pos = VertexPosition(Column, Row + 1);		Upper.V2_UV = FVector2D::ZeroVector;	Upper.V2_Color = Colors[BottomLeft];
			FCanvasUVTri& Lower = Triangles.AddDefaulted_GetRef();
			Lower.V0_Pos = VertexPosition(Column + 1, Row);		Lower.V0_UV = FVector2D::ZeroVector;	Lower.V0_Color = Colors[TopLeft + 1];
			Lower.V1_Pos = VertexPosition(Column + 1, Row + 1);	Lower.V1_UV = FVector2D::ZeroVector;	Lower.V1_Color = Colors[BottomLeft + 1];
			Lower.V2_Pos = VertexPosition(Column, Row + 1);		Lower.V2_UV = FVector2D::ZeroVector;	Lower.V2_Color = Colors[BottomLeft];
		}
	}
	FCanvasTriangleItem TriangleItem(Triangles, GWhiteTexture);
	TriangleItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TriangleItem);
}
