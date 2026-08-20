// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamSpriteBase.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamWidget.h"

UDreamSpriteBase::UDreamSpriteBase(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
	Sprite = UDreamUISpriteData::GetDefaultWhiteSolid();
}

void UDreamSpriteBase::BeginPlay()
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

void UDreamSpriteBase::EndPlay()
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

void UDreamSpriteBase::ApplyAtlasTextureChange_Implementation()
{
	UIGeometry->Texture = Sprite->GetAtlasTexture();
	GetWidget()->MarkCanvasUpdate(true);
}

void UDreamSpriteBase::SetSprite(UDreamUISpriteData_BaseObject* Value, bool bSetSize)
{
	if (!IsValid(Value))
	{
		Value = UDreamUISpriteData::GetDefaultWhiteSolid();
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
void UDreamSpriteBase::SetSizeFromSpriteData()
{
	if (IsValid(Sprite))
	{
		auto Widget = GetWidget();
		Widget->SetWidth(Sprite->GetSpriteInfo().GetSourceWidth());
		Widget->SetHeight(Sprite->GetSpriteInfo().GetSourceHeight());
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Sprite is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}

void UDreamSpriteBase::SetOverrideMaterial(UMaterialInterface* Value)
{
	if (OverrideMaterial != Value)
	{
		OverrideMaterial = Value;
		MarkMaterialDirty();
	}
}

void UDreamSpriteBase::OnRegister()
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
void UDreamSpriteBase::OnUnregister()
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

void UDreamSpriteBase::BeginDestroy()
{
	Super::BeginDestroy();
	if (bHasAddToSprite)
	{
		if (IsValid(Sprite))
		{
			Sprite->RemoveUISprite(this);
			bHasAddToSprite = false;
		}
	}
}

#if WITH_EDITOR
void UDreamSpriteBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
void UDreamSpriteBase::OnPreChangeSpriteProperty()
{
	if (IsValid(Sprite))
	{
		Sprite->RemoveUISprite(this);
		bHasAddToSprite = false;
	}
}
void UDreamSpriteBase::OnPostChangeSpriteProperty()
{
	if (IsValid(Sprite))
	{
		Sprite->AddUISprite(this);
		bHasAddToSprite = true;
	}
}
#endif

void UDreamSpriteBase::CheckSpriteData()
{
	if (!IsValid(Sprite))
	{
		Sprite = UDreamUISpriteData::GetDefaultWhiteSolid();
		Sprite->AddUISprite(this);
	}
}
void UDreamSpriteBase::OnBeforeCreateOrUpdateGeometry()
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

UTexture* UDreamSpriteBase::GetTextureToCreateGeometry()
{
	if (!IsValid(Sprite))
	{
		Sprite = UDreamUISpriteData::GetDefaultWhiteSolid();
	}
	if (IsValid(Sprite) && IsValid(Sprite->GetAtlasTexture()))
	{
		return Sprite->GetAtlasTexture();
	}
	return nullptr;
}

UMaterialInterface* UDreamSpriteBase::GetMaterialToCreateGeometry()
{
	return OverrideMaterial;
}

bool UDreamSpriteBase::ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const
{
	if (IsValid(Sprite))
	{
		return Sprite->ReadPixel(InUV, OutPixel);
	}
	return false;
}
