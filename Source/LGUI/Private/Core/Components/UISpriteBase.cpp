// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/UISpriteBase.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "LGUI/Public/Core/Components/LexCanvas.h"
#include "Core/LexUISpriteData.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/LexUIDrawCall.h"

UUISpriteBase::UUISpriteBase(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UUISpriteBase::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasAddToSprite)
	{
		if (IsValid(sprite))
		{
			sprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}

void UUISpriteBase::EndPlay()
{
	Super::EndPlay();
	if (bHasAddToSprite)
	{
		if (IsValid(sprite))
		{
			sprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

void UUISpriteBase::ApplyAtlasTextureChange_Implementation()
{
	geometry->Texture = sprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = geometry->Texture;
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, true, false);
}
void UUISpriteBase::ApplyAtlasTextureScaleUp_Implementation()
{
	auto& vertices = geometry->Vertices;
	if (vertices.Num() != 0)
	{
		for (int i = 0; i < vertices.Num(); i++)
		{
			auto& uv = vertices[i];
			uv.TextureCoordinate[0].X *= 0.5f;
			uv.TextureCoordinate[0].Y *= 0.5f;
		}
	}
	geometry->Texture = sprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = geometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void UUISpriteBase::SetSprite(ULexUISpriteData_BaseObject* newSprite, bool setSize)
{
	if (!IsValid(newSprite))
	{
		newSprite = ULexUISpriteData::GetDefaultWhiteSolid();
	}
	if (sprite != newSprite)
	{
		if((!IsValid(sprite) || !IsValid(newSprite))
			|| (sprite->GetAtlasTexture() != newSprite->GetAtlasTexture()))
		{
			//remove from old
			if (IsValid(sprite))
			{
				sprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			if (IsValid(newSprite))
			{
				newSprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		sprite = newSprite;
		MarkUVDirty();
		if (setSize) SetSizeFromSpriteData();
	}
}
void UUISpriteBase::SetSizeFromSpriteData()
{
	if (IsValid(sprite))
	{
		auto Widget = GetWidget();
		Widget->SetSize(FLexWidgetSize2::MakeFixed(FVector2f(sprite->GetSpriteInfo().GetSourceWidth(), sprite->GetSpriteInfo().GetSourceHeight())));
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Sprite is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}

void UUISpriteBase::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
	{
		if (!bHasAddToSprite)
		{
			if (IsValid(sprite))
			{
				sprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
		}
	}
#endif
}
void UUISpriteBase::OnUnregister()
{
	Super::OnUnregister();
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
	{
		if (bHasAddToSprite)
		{
			if (IsValid(sprite))
			{
				sprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
		}
	}
#endif
}
#if WITH_EDITOR
void UUISpriteBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
void UUISpriteBase::OnPreChangeSpriteProperty()
{
	if (IsValid(sprite))
	{
		sprite->RemoveUISprite(this);
		bHasAddToSprite = false;
	}
}
void UUISpriteBase::OnPostChangeSpriteProperty()
{
	if (IsValid(sprite))
	{
		sprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
}
#endif

void UUISpriteBase::CheckSpriteData()
{
	if (!IsValid(sprite))
	{
		sprite = ULexUISpriteData::GetDefaultWhiteSolid();
		sprite->AddUISprite(this);
	}
}
void UUISpriteBase::OnBeforeCreateOrUpdateGeometry()
{
	if (!bHasAddToSprite)
	{
		CheckSpriteData();
		if (IsValid(sprite))
		{
			sprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}

UTexture* UUISpriteBase::GetTextureToCreateGeometry()
{
	if (!IsValid(sprite))
	{
		sprite = ULexUISpriteData::GetDefaultWhiteSolid();
	}
	if (IsValid(sprite) && IsValid(sprite->GetAtlasTexture()))
	{
		return sprite->GetAtlasTexture();
	}
	return nullptr;
}

bool UUISpriteBase::ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const
{
	if (IsValid(sprite))
	{
		return sprite->ReadPixel(InUV, OutPixel);
	}
	return false;
}
