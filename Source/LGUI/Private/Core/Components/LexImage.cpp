// Copyright 2025-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexImage.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIImageBrush.h"
#include "Core/LexUIDrawCall.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexUISpriteData.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "Utils/LexUIUtils.h"

#if WITH_EDITOR
void ULexImage::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == FName("ResourceObject"))
	{
		if (bHasAddToSprite)
		{
			auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}
void ULexImage::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	static const FName BrushName = GET_MEMBER_NAME_CHECKED(ULexImage, Brush);

	if (MemberName == BrushName)
	{
		
	}
}
#endif

ULexUISpriteData_BaseObject* ULexImage::SpriteRenderGetSprite_Implementation() const
{
	if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		return LexSprite;
	}
	return nullptr;
}

void ULexImage::ApplyAtlasTextureScaleUp_Implementation()
{
	check(bHasAddToSprite);
	auto& vertices = UIGeometry->Vertices;
	if (vertices.Num() != 0)
	{
		for (int i = 0; i < vertices.Num(); i++)
		{
			auto& uv = vertices[i];
			uv.TextureCoordinate[0].X *= 0.5f;
			uv.TextureCoordinate[0].Y *= 0.5f;
		}
	}
	auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
	UIGeometry->Texture = LexSprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void ULexImage::ApplyAtlasTextureChange_Implementation()
{
	check(bHasAddToSprite);
	auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
	UIGeometry->Texture = LexSprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

ULexImage::ULexImage(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Brush.SetResourceObject(ULexUISpriteData::GetDefaultWhiteSolid());
	Brush.DrawAs = ELexUIImageBrushDrawType::Image;
}

UTexture* ULexImage::GetTextureToCreateGeometry()
{
	if (auto Texture = Cast<UTexture>(Brush.GetResourceObject()))
	{
		return Texture;
	}
	if (auto SlateTextureAtlas = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject()))
	{
		return SlateTextureAtlas->GetSlateAtlasData().AtlasTexture;
	}
	if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
	{
		if (!bHasAddToSprite)
		{
			LexSprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
		return LexSprite->GetAtlasTexture();
	}
	return FLexUIUtils::GetDefaultWhiteTexture();
}
UMaterialInterface* ULexImage::GetMaterialToCreateGeometry()
{
	if (auto Material = Cast<UMaterialInterface>(Brush.GetResourceObject()))
	{
		return Material;
	}
	return nullptr;
}

void ULexImage::OnUpdateGeometry(FLexUIGeometry& InMesh, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	auto Widget = this->GetWidget();
	auto RenderSize = Widget->GetSize();
	auto Pivot = Widget->GetPivot();
	auto RenderCanvas = Widget->GetRenderCanvas();
	auto FinalColor = FLexUIUtils::MultiplyColor(Brush.TintColor, this->GetFinalColor());

	switch (Brush.DrawAs)
	{
	case ELexUIImageBrushDrawType::None:
		return;
	case ELexUIImageBrushDrawType::Image:
		{
			DRAW_AS_IMAGE:
			FLexUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = LexSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.MinUV.X = Brush.UVRegion.X;
				SpriteInfo.MaxUV.Y = Brush.UVRegion.Y;
				SpriteInfo.MaxUV.X = Brush.UVRegion.Z;
				SpriteInfo.MinUV.Y = Brush.UVRegion.W;
			}
			FLexUIGeometry::UpdateUIRectSimpleVertex(&InMesh, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
			, SpriteInfo, RenderCanvas, this, FinalColor
			, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	case ELexUIImageBrushDrawType::Border:
	case ELexUIImageBrushDrawType::Box:
		{
			if (bHasAddToSprite)
			{
				auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
				if (!LexSprite->GetSpriteInfo().HasBorder())
					goto DRAW_AS_IMAGE;
			}
			else
			{
				if (Brush.Margin.Left == 0 && Brush.Margin.Right == 0 && Brush.Margin.Top == 0 && Brush.Margin.Bottom == 0)
					goto DRAW_AS_IMAGE;
			}
			bool bFillCenter = Brush.DrawAs == ELexUIImageBrushDrawType::Box;
			FLexUISpriteInfo SpriteInfo;
			if (bHasAddToSprite)
			{
				auto LexSprite = (ULexUISpriteData_BaseObject*)Brush.GetResourceObject();
				SpriteInfo = LexSprite->GetSpriteInfo();
			}
			else
			{
				SpriteInfo.Width = Brush.ImageSize.X;
				SpriteInfo.Height = Brush.ImageSize.Y;
				SpriteInfo.MinUV.X = Brush.UVRegion.X;
				SpriteInfo.MaxUV.Y = Brush.UVRegion.Y;
				SpriteInfo.MaxUV.X = Brush.UVRegion.Z;
				SpriteInfo.MinUV.Y = Brush.UVRegion.W;
				SpriteInfo.Border.Left = Brush.Margin.Left * Brush.ImageSize.X;
				SpriteInfo.Border.Right = Brush.Margin.Right * Brush.ImageSize.X;
				SpriteInfo.Border.Top = Brush.Margin.Top * Brush.ImageSize.Y;
				SpriteInfo.Border.Bottom = Brush.Margin.Bottom * Brush.ImageSize.Y;
				float uvWidth = SpriteInfo.MaxUV.X - SpriteInfo.MinUV.X, uvHeight = SpriteInfo.MaxUV.Y - SpriteInfo.MinUV.Y;
				SpriteInfo.BorderMinUV.X = SpriteInfo.MinUV.X + Brush.Margin.Left * uvWidth;
				SpriteInfo.BorderMaxUV.X = SpriteInfo.MaxUV.X - Brush.Margin.Right * uvWidth;
				SpriteInfo.BorderMaxUV.Y = SpriteInfo.MaxUV.Y - Brush.Margin.Bottom * uvHeight;
				SpriteInfo.BorderMinUV.Y = SpriteInfo.MinUV.Y + Brush.Margin.Top * uvHeight;
			}
			FLexUIGeometry::UpdateUIRectBorderVertex(&InMesh, bFillCenter, RenderSize.X, RenderSize.Y, FVector2f(Pivot)
				, SpriteInfo, RenderCanvas, this, FinalColor
				, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
		}
		break;
	}
}

void ULexImage::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexImage::BeginDestroy()
{
	Super::BeginDestroy();
	
	if (bHasAddToSprite)
	{
		if (auto LexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject()))
		{
			LexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

void ULexImage::SetBrush(const FLexUIImageBrush& Value)
{
	auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
	if (OldLexSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	
	MarkVerticesDirty(true, true, true, Brush.TintColor != Value.TintColor);
	MarkTextureDirty();
	MarkMaterialDirty();
	Brush = Value;
}

void ULexImage::SetBrush_LexUISprite(ULexUISpriteData_BaseObject* Value)
{
	auto OldLexSprite = Cast<ULexUISpriteData_BaseObject>(Brush.GetResourceObject());
	auto NewLexSprite = Value;
	//handle Sprite
	if (OldLexSprite != nullptr && NewLexSprite != nullptr)
	{
		if(OldLexSprite->GetAtlasTexture() != NewLexSprite->GetAtlasTexture())
		{
			//remove from old
			if (bHasAddToSprite)
			{
				OldLexSprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			{
				NewLexSprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		MarkUVDirty();
		Brush.SetResourceObject(Value);
		return;
	}
	if (OldLexSprite != nullptr)
	{
		//remove from old
		if (bHasAddToSprite)
		{
			OldLexSprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
	if (NewLexSprite != nullptr)
	{
		NewLexSprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
	MarkVerticesDirty(false, false, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value);
}

void ULexImage::SetBrush_SlateSprite(TScriptInterface<ISlateTextureAtlasInterface> Value)
{
	auto OldSlateSprite = Cast<ISlateTextureAtlasInterface>(Brush.GetResourceObject());
	auto NewSlateSprite = Value;
	if (OldSlateSprite != nullptr && NewSlateSprite != nullptr)
	{
		if (OldSlateSprite->GetSlateAtlasData().AtlasTexture != NewSlateSprite->GetSlateAtlasData().AtlasTexture)
		{
			MarkTextureDirty();
		}
		MarkUVDirty();
		Brush.SetResourceObject(Value.GetObject());
		return;
	}

	MarkVerticesDirty(true, true, true, false);
	MarkTextureDirty();
	if (Cast<UMaterialInterface>(Brush.GetResourceObject()) != nullptr)//if old brush is material then mark material dirty
	{
		MarkMaterialDirty();
	}
	Brush.SetResourceObject(Value.GetObject());
}

void ULexImage::SetBrushTintColor(FColor Value)
{
	if (Brush.TintColor != Value)
	{
		Brush.TintColor = Value;
		MarkColorDirty();
	}
}

float ULexImage::GetPreferredWidth() const
{
	return Brush.ImageSize.X;
}

float ULexImage::GetPreferredHeight() const
{
	return Brush.ImageSize.Y;
}
