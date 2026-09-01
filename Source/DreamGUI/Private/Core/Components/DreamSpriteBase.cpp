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
	// A repack can move the sprite and change what its info says, and this is the notification that
	// it happened. The sprite data is being initialised on the line above regardless, so the copy is
	// free here and stale everywhere else if it is not taken.
	CacheSpriteSourceSize();
	GetWidget()->MarkCanvasUpdate(true);
}

void UDreamSpriteBase::CacheSpriteSourceSize()
{
	if (IsValid(Sprite))
	{
		const FDreamUISpriteInfo& Info = Sprite->GetSpriteInfo();
		const float Width = Info.GetSourceWidth();
		const float Height = Info.GetSourceHeight();
		// A sprite that reports nothing has not been packed yet, or has no texture behind it. Zero
		// is a claim in this protocol and this is not the component making one.
		CachedSpriteSourceSize = FVector2f(Width > 0.0f ? Width : -1.0f, Height > 0.0f ? Height : -1.0f);
	}
	else
	{
		CachedSpriteSourceSize = FVector2f(-1.0f, -1.0f);
	}
}

/*
 * A sprite has a real natural size: the pixel size it was authored at, which is the same number
 * SetSizeFromSpriteData writes into the widget. Unlike the procedural shapes in Extensions/, a
 * sprite in an Auto slot has something to say.
 *
 * The counter-intuitive part is where the number comes from. UDreamUISpriteData::GetSpriteInfo
 * reads like a getter and is an initialiser: it calls InitSpriteData, which on a first touch waits
 * for texture compilation, packs a runtime atlas, and on failure clears the packing tag,
 * MarkPackageDirty()s the asset and raises an editor notification. A measure pass runs this once
 * per element and may run twice; none of that belongs inside one. So the size is copied at the
 * moments this component is holding the sprite data anyway -- assignment, atlas change, the
 * geometry build -- and read back here for the cost of a field access.
 *
 * Before any of those have happened the answer is negative rather than zero, and the layout falls
 * back to the authored rect. That is the right trade: a wrong number that looks confident is worse
 * than an abstention the caller already knows how to handle.
 */
float UDreamSpriteBase::GetPreferredWidth() const
{
	return CachedSpriteSourceSize.X;
}

float UDreamSpriteBase::GetPreferredHeight() const
{
	return CachedSpriteSourceSize.Y;
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
		CacheSpriteSourceSize();
		MarkVertexUVDirty();
		if (bSetSize) SetSizeFromSpriteData();
	}
}
void UDreamSpriteBase::SetSizeFromSpriteData()
{
	if (IsValid(Sprite))
	{
		auto Widget = GetWidget();
		CacheSpriteSourceSize();
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
	// So that a sprite picked in the details panel measures right on the next layout rather than on
	// the one after the next geometry build.
	CacheSpriteSourceSize();
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
	// The geometry build is about to read the sprite info anyway, so this is the one recurring site
	// where the size can be refreshed for nothing -- and the one that catches a sprite swapped in by
	// something other than SetSprite, such as the details panel writing the property directly.
	CacheSpriteSourceSize();
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
