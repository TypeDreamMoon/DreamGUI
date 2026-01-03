// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexSpriteBase.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUISpriteData.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/LexUIDrawCall.h"

ULexSpriteBase::ULexSpriteBase(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Sprite = ULexUISpriteData::GetDefaultWhiteSolid();
}

void ULexSpriteBase::BeginPlay()
{
	Super::BeginPlay();
	if (!bHasAddToSprite)
	{
		if (IsValid(Sprite))
		{
			Sprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}

void ULexSpriteBase::EndPlay()
{
	Super::EndPlay();
	if (bHasAddToSprite)
	{
		if (IsValid(Sprite))
		{
			Sprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

void ULexSpriteBase::ApplyAtlasTextureChange_Implementation()
{
	UIGeometry->Texture = Sprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, true, false);
}
void ULexSpriteBase::ApplyAtlasTextureScaleUp_Implementation()
{
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
	UIGeometry->Texture = Sprite->GetAtlasTexture();
	if (DrawCall.IsValid())
	{
		DrawCall->Texture = UIGeometry->Texture;
		DrawCall->bTextureChanged = true;
		DrawCall->bNeedToUpdateVertex = true;
	}
	MarkVerticesDirty(false, true, true, false);
	GetWidget()->MarkCanvasUpdate(true, true, false);
}

void ULexSpriteBase::SetSprite(ULexUISpriteData_BaseObject* Value, bool bSetSize)
{
	if (!IsValid(Value))
	{
		Value = ULexUISpriteData::GetDefaultWhiteSolid();
	}
	if (Sprite != Value)
	{
		if((!IsValid(Sprite) || !IsValid(Value))
			|| (Sprite->GetAtlasTexture() != Value->GetAtlasTexture()))
		{
			//remove from old
			if (IsValid(Sprite))
			{
				Sprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
			//add to new
			if (IsValid(Value))
			{
				Value->AddUISprite(this);
				bHasAddToSprite = true;
			}
			MarkTextureDirty();
		}
		Sprite = Value;
		MarkVertexUVDirty();
		if (bSetSize) SetSizeFromSpriteData();
	}
}
void ULexSpriteBase::SetSizeFromSpriteData()
{
	if (IsValid(Sprite))
	{
		auto Widget = GetWidget();
		Widget->SetWidth(Sprite->GetSpriteInfo().GetSourceWidth());
		Widget->SetHeight(Sprite->GetSpriteInfo().GetSourceHeight());
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Sprite is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}

void ULexSpriteBase::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
	{
		if (!bHasAddToSprite)
		{
			if (IsValid(Sprite))
			{
				Sprite->AddUISprite(this);
				bHasAddToSprite = true;
			}
		}
	}
#endif
}
void ULexSpriteBase::OnUnregister()
{
	Super::OnUnregister();
#if WITH_EDITOR
	if (this->GetWorld() && this->GetWorld()->WorldType == EWorldType::Editor)
	{
		if (bHasAddToSprite)
		{
			if (IsValid(Sprite))
			{
				Sprite->RemoveUISprite(this);
				bHasAddToSprite = false;
			}
		}
	}
#endif
}
#if WITH_EDITOR
void ULexSpriteBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
void ULexSpriteBase::OnPreChangeSpriteProperty()
{
	if (IsValid(Sprite))
	{
		Sprite->RemoveUISprite(this);
		bHasAddToSprite = false;
	}
}
void ULexSpriteBase::OnPostChangeSpriteProperty()
{
	if (IsValid(Sprite))
	{
		Sprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
}
#endif

void ULexSpriteBase::CheckSpriteData()
{
	if (!IsValid(Sprite))
	{
		Sprite = ULexUISpriteData::GetDefaultWhiteSolid();
		Sprite->AddUISprite(this);
	}
}
void ULexSpriteBase::OnBeforeCreateOrUpdateGeometry()
{
	if (!bHasAddToSprite)
	{
		CheckSpriteData();
		if (IsValid(Sprite))
		{
			Sprite->AddUISprite(this);
			bHasAddToSprite = true;
		}
	}
}

UTexture* ULexSpriteBase::GetTextureToCreateGeometry()
{
	if (!IsValid(Sprite))
	{
		Sprite = ULexUISpriteData::GetDefaultWhiteSolid();
	}
	if (IsValid(Sprite) && IsValid(Sprite->GetAtlasTexture()))
	{
		return Sprite->GetAtlasTexture();
	}
	return nullptr;
}

bool ULexSpriteBase::ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const
{
	if (IsValid(Sprite))
	{
		return Sprite->ReadPixel(InUV, OutPixel);
	}
	return false;
}
