// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIImageBrush.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUIWidgetRegistry.h"

#if WITH_EDITOR
void UDreamImage::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == FDreamUIImageBrush::GetPropertyName_ResourceObject())
	{
		UnregisterFromSprite();
	}
}
void UDreamImage::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	static const FName BrushName = GET_MEMBER_NAME_CHECKED(UDreamImage, Brush);

	if (MemberName == BrushName || PropertyName == FDreamUIImageBrush::GetPropertyName_ResourceObject())
	{
		if (auto Widget = GetWidget())
		{
			UDreamWidget::MarkLayoutForRebuild(Widget);
		}
	}
}
#endif

void UDreamImage::UnregisterFromSprite()
{
	if (bHasAddToSprite)
	{
		if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
		{
			DreamSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

UDreamUISpriteData_BaseObject* UDreamImage::SpriteRenderGetSprite_Implementation() const
{
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		return DreamSprite;
	}
	return nullptr;
}

void UDreamImage::ApplyAtlasTextureChange_Implementation()
{
	check(bHasAddToSprite);
	auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
	UIGeometry->Texture = DreamSprite->GetAtlasTexture();
	GetWidget()->MarkCanvasUpdate(true);
}

UDreamImage::UDreamImage(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Brush.SetResourceObject(UDreamUISpriteData::GetDefaultWhiteSolid());
	Brush.DrawAs = EDreamUIImageBrushDrawType::Image;
}

UTexture* UDreamImage::GetTextureToCreateGeometry()
{
	if (auto Texture = Cast<UTexture>(Brush.GetResourceObject()))
	{
		return Texture;
	}
	if (auto SlateTextureAtlas = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject()))
	{
		return SlateTextureAtlas->GetSlateAtlasData().AtlasTexture;
	}
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			DreamSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
		return DreamSprite->GetAtlasTexture();
	}
	return FDreamUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* UDreamImage::GetMaterialToCreateGeometry()
{
	if (auto Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
	{
		return Material;
	}
	return nullptr;
}

void UDreamImage::OnUpdateGeometry(FDreamUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = this->GetWidget();
	auto RenderSize = Widget->GetSize();
	auto Pivot = Widget->GetPivot();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto FinalColor = FDreamUIUtils::MultiplyColor(Brush.TintColor, this->GetFinalColor());

	switch (Brush.DrawAs)
	{
	case EDreamUIImageBrushDrawType::None:
		return;
	case EDreamUIImageBrushDrawType::Image:
		{
			DRAW_AS_IMAGE:
			FDreamUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = DreamSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, Brush.UVRegion);
			}
			FDreamUIGeometry::UpdateUIRectSimpleVertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
			, SpriteInfo, RenderCanvas, this, FinalColor
			, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	case EDreamUIImageBrushDrawType::Border:
	case EDreamUIImageBrushDrawType::Box:
		{
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				if (!DreamSprite->GetSpriteInfo().HasBorder())
					goto DRAW_AS_IMAGE;
			}
			else
			{
				if (Brush.Margin.Left == 0 && Brush.Margin.Right == 0 && Brush.Margin.Top == 0 && Brush.Margin.Bottom == 0)
					goto DRAW_AS_IMAGE;
			}
			bool bFillCenter = Brush.DrawAs == EDreamUIImageBrushDrawType::Box;
			FDreamUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto DreamSprite = (UDreamUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = DreamSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.Border.Left = Brush.Margin.Left * Brush.ImageSize.X;
				SpriteInfo.Border.Right = Brush.Margin.Right * Brush.ImageSize.X;
				SpriteInfo.Border.Top = Brush.Margin.Top * Brush.ImageSize.Y;
				SpriteInfo.Border.Bottom = Brush.Margin.Bottom * Brush.ImageSize.Y;
				SpriteInfo.ApplyUV(0, 0, SpriteInfo.Width, SpriteInfo.Height, 1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height, Brush.UVRegion);
				SpriteInfo.ApplyBorderUV(1.0f / SpriteInfo.Width, 1.0f / SpriteInfo.Height);
			}
			FDreamUIGeometry::UpdateUIRectBorderVertex(&InMesh, bFillCenter, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
				, SpriteInfo, RenderCanvas, this, FinalColor
				, Brush.PixelsPerUnitMultiplier
				, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	}
}

void UDreamImage::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamImage::BeginDestroy()
{
	Super::BeginDestroy();
	//unregister from sprite
	UnregisterFromSprite();
}

void UDreamImage::OnRegister()
{
	Super::OnRegister();
	if (auto DreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			DreamSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}

void UDreamImage::OnUnregister()
{
	Super::OnUnregister();
	UnregisterFromSprite();
}

void UDreamImage::SetBrush(const FDreamUIImageBrush& Value)
{
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, Brush.TintColor != Value.TintColor);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush = Value;
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_DreamUISprite(UDreamUISpriteData_BaseObject* Value)
{
	auto OldDreamSprite = Cast<UDreamUISpriteData_BaseObject>(Brush.GetResourceObject());
	auto NewDreamSprite = Value;
	//handle Sprite
	if (OldDreamSprite != nullptr && NewDreamSprite != nullptr)
	{
		if(OldDreamSprite->GetAtlasTexture() != NewDreamSprite->GetAtlasTexture())
		{
			//remove from old
			if (bHasAddToSprite)
			{
				OldDreamSprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			{
				NewDreamSprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		MarkVertexUVDirty();
		Brush.SetResourceObject(Value);
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
		return;
	}
	if (OldDreamSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldDreamSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	if (NewDreamSprite != nullptr)
	{
		NewDreamSprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
	MarkVerticesDirty(false, false, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	auto OldSlateSprite = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject());
	auto NewSlateSprite = Value;
	if (OldSlateSprite != nullptr && NewSlateSprite != nullptr)
	{
		if (OldSlateSprite->GetSlateAtlasData().AtlasTexture != NewSlateSprite->GetSlateAtlasData().AtlasTexture)
		{
			MarkTextureDirty();
		}
		MarkVertexUVDirty();
		Brush.SetResourceObject(Value.GetObject());
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
		return;
	}

	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value.GetObject());
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrush_Texture(UTexture* Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}
void UDreamImage::SetBrush_Material(UTexture* Value)
{
	//remove from old sprite
	UnregisterFromSprite();
	
	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush.SetResourceObject(Value);
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

void UDreamImage::SetBrushTintColor(FColor Value)
{
	if (Brush.TintColor != Value)
	{
		Brush.TintColor = Value;
		MarkColorDirty();
	}
}

float UDreamImage::GetPreferredWidth() const
{
	return Brush.ImageSize.X;
}

float UDreamImage::GetPreferredHeight() const
{
	return Brush.ImageSize.Y;
}

DECLARE_DREAM_GUI_VISUAL("Image", UDreamImage)
