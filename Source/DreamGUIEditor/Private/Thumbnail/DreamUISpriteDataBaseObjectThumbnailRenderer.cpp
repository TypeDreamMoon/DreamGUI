// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Thumbnail/DreamUISpriteDataBaseObjectThumbnailRenderer.h"
#include "EngineModule.h"
#include "RendererInterface.h"
#include "SceneView.h"
#include "Engine/EngineTypes.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "CanvasItem.h"
#include "EditorStyleSet.h"
#include "CanvasTypes.h"
#include "DreamUIEditorUtils.h"
#include "Interfaces/IPluginManager.h"

UDreamUISpriteDataBaseObjectThumbnailRenderer::UDreamUISpriteDataBaseObjectThumbnailRenderer()
{

}
void UDreamUISpriteDataBaseObjectThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (auto sprite = Cast<UDreamUISpriteData_BaseObject>(Object))
	{
		DrawFrame(sprite, X, Y, Width, Height, RenderTarget, Canvas, nullptr);
	}
	
}
void UDreamUISpriteDataBaseObjectThumbnailRenderer::DrawFrame(class UDreamUISpriteData_BaseObject* Sprite, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget*, FCanvas* Canvas, FBoxSphereBounds* OverrideRenderBounds)
{
	const UTexture2D* SourceTexture = SourceTexture = Sprite->GetAtlasTexture();

	if (SourceTexture != nullptr)
	{
		const bool bUseTranslucentBlend = SourceTexture->HasAlphaChannel();
		DrawGrid(X, Y, Width, Height, Canvas);

		auto& SpriteInfo = Sprite->GetSpriteInfo();
		// Draw triangles
		if (SourceTexture->GetResource() != nullptr)
		{
			float triangleWidth = Width, triangleHeight = Height;
			float spriteWidth = SpriteInfo.Width;
			float spriteHeight = SpriteInfo.Height;
			float xOffset = 0, yOffset = 0;
			if (spriteWidth > spriteHeight)
			{
				triangleHeight = Height * spriteHeight / spriteWidth;
				yOffset = (Height - triangleHeight) * 0.5f;
			}
			else if (spriteWidth < spriteHeight)
			{
				triangleWidth = Width * spriteWidth / spriteHeight;
				xOffset = (Width - triangleWidth) * 0.5f;
			}
			TArray<FCanvasUVTri> Triangles;
			const FLinearColor SpriteColor(FLinearColor::White);

			FCanvasUVTri* Triangle1 = new (Triangles) FCanvasUVTri();
			Triangle1->V0_Pos = FVector2D(X + triangleWidth + xOffset, Y + yOffset); Triangle1->V0_UV = FVector2D(SpriteInfo.GetUV3()); Triangle1->V0_Color = SpriteColor;
			Triangle1->V1_Pos = FVector2D(X + xOffset, Y + yOffset); Triangle1->V1_UV = FVector2D(SpriteInfo.GetUV2()); Triangle1->V1_Color = SpriteColor;
			Triangle1->V2_Pos = FVector2D(X + xOffset, Y + triangleHeight + yOffset); Triangle1->V2_UV = FVector2D(SpriteInfo.GetUV0()); Triangle1->V2_Color = SpriteColor;

			FCanvasUVTri* Triangle2 = new (Triangles) FCanvasUVTri();
			Triangle2->V0_Pos = FVector2D(X + triangleWidth + xOffset, Y + yOffset); Triangle2->V0_UV = FVector2D(SpriteInfo.GetUV3()); Triangle2->V0_Color = SpriteColor;
			Triangle2->V1_Pos = FVector2D(X + xOffset, Y + triangleHeight + yOffset); Triangle2->V1_UV = FVector2D(SpriteInfo.GetUV0()); Triangle2->V1_Color = SpriteColor;
			Triangle2->V2_Pos = FVector2D(X + triangleWidth + xOffset, Y + triangleHeight + yOffset); Triangle2->V2_UV = FVector2D(SpriteInfo.GetUV1()); Triangle2->V2_Color = SpriteColor;

			FCanvasTriangleItem CanvasTriangle(Triangles, SourceTexture->GetResource());
			CanvasTriangle.BlendMode = bUseTranslucentBlend ? ESimpleElementBlendMode::SE_BLEND_Translucent : ESimpleElementBlendMode::SE_BLEND_Opaque;
			Canvas->DrawItem(CanvasTriangle);
		}
	}
	else
	{
		// Fallback for a bogus sprite
		DrawGrid(X, Y, Width, Height, Canvas);
	}
	//draw sprite icon
	static FString DreamGUIBasePath = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"))->GetBaseDir();
	FDreamUIEditorUtils::DrawThumbnailIcon(DreamGUIBasePath + TEXT("/Resources/Icons/UISprite_40x.png"), X, Y, Width, Height, Canvas);
}
void UDreamUISpriteDataBaseObjectThumbnailRenderer::DrawGrid(int32 X, int32 Y, uint32 Width, uint32 Height, FCanvas* Canvas)
{
	static UTexture2D* GridTexture = Cast<UTexture2D>(FAppStyle::GetBrush("Checkerboard")->GetResourceObject());
	if (GridTexture == nullptr)
	{
		GridTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"), nullptr, LOAD_None, nullptr);
		if (GridTexture != nullptr)
		{
			// Nothing else holds this one -- the style set owns the Checkerboard brush, but the
			// fallback is loaded here and referenced only by this static, so the next GC collects it
			// and the thumbnail after that draws through a freed object.
			GridTexture->AddToRoot();
		}
	}

	// Both sources can come back null -- an editor style without the Checkerboard brush, and an
	// engine content path that failed to load. Drawing the grid is decoration, so skip it rather
	// than dereference: the sprite itself still draws over the top.
	if (GridTexture == nullptr || GridTexture->GetResource() == nullptr)
	{
		return;
	}

	const bool bAlphaBlend = false;

	Canvas->DrawTile(
		(float)X,
		(float)Y,
		(float)Width,
		(float)Height,
		0.0f,
		0.0f,
		4.0f,
		4.0f,
		FLinearColor(0.15f, 0.15f, 0.15f),
		GridTexture->GetResource(),
		bAlphaBlend);
}
void UDreamUISpriteDataBaseObjectThumbnailRenderer::BeginDestroy()
{
	Super::BeginDestroy();
}