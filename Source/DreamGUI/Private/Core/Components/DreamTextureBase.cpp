// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamTextureBase.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Materials/MaterialInterface.h"
#include "Utils/DreamUIUtils.h"
#include "TextureResource.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "UITextureBase"

UDreamTextureBase::UDreamTextureBase(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void UDreamTextureBase::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void UDreamTextureBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UDreamTextureBase, Texture))
		{
			MarkTextureDirty();
		}
	}
}
void UDreamTextureBase::CheckTexture()
{
	if (!IsValid(Texture))
	{
		auto defaultWhiteSolid = FDreamUIUtils::GetDefaultWhiteTexture();
		if (IsValid(defaultWhiteSolid))
		{
			Texture = defaultWhiteSolid;
		}
	}
}
#endif

UTexture* UDreamTextureBase::GetTextureToCreateGeometry()
{
	if (!IsValid(Texture))
	{
		Texture = FDreamUIUtils::GetDefaultWhiteTexture();
	}
	return Texture;
}

UMaterialInterface* UDreamTextureBase::GetMaterialToCreateGeometry()
{
	return OverrideMaterial;
}

bool UDreamTextureBase::ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const
{
	if (IsValid(Texture))
	{
		if (auto texture2D = Cast<UTexture2D>(Texture))
		{
			auto PlatformData = texture2D->GetPlatformData();
			if (PlatformData && PlatformData->Mips.Num() > 0)
			{
				if (auto Pixels = (FColor*)(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_ONLY)))
				{
					auto uvInFullSize = FIntPoint(InUV.X * texture2D->GetSizeX(), InUV.Y * texture2D->GetSizeY());
					auto PixelIndex = uvInFullSize.Y * texture2D->GetSizeX() + uvInFullSize.X;
					OutPixel = Pixels[PixelIndex];
				}
				PlatformData->Mips[0].BulkData.Unlock();
				return true;
			}
		}
	}
	return false;
}

void UDreamTextureBase::SetTexture(UTexture* Value)
{
	if (Texture != Value)
	{
		Texture = Value;
		if (Texture == nullptr)
		{
			Texture = FDreamUIUtils::GetDefaultWhiteTexture();
		}
		MarkTextureDirty();
	}
}
void UDreamTextureBase::SetSizeFromTexture()
{
	if (IsValid(Texture))
	{
		auto Widget = GetWidget();
		Widget->SetWidth(Texture->GetSurfaceWidth());
		Widget->SetHeight(Texture->GetSurfaceHeight());
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Texture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UDreamTextureBase::SetOverrideMaterial(UMaterialInterface* Value)
{
	if (OverrideMaterial != Value)
	{
		OverrideMaterial = Value;
		MarkMaterialDirty();
	}
}

/*
 * A texture's natural size is its own dimensions -- the same pair SetSizeFromTexture writes into
 * the widget, so a texture asked to measure itself and a texture told to size itself to its content
 * agree by construction.
 *
 * GetSurfaceWidth is a read of already-resident platform data, so this is as cheap as the protocol
 * requires and touches nothing. What it is NOT is always available: a texture still compiling, or a
 * render target not yet allocated, answers zero. Zero is a claim in this protocol -- "give me no
 * room" -- and a UI element that vanished because its texture had not finished building is a bug
 * that would only reproduce on a cold DDC. Anything that is not a positive number abstains instead.
 */
float UDreamTextureBase::GetPreferredWidth() const
{
	if (!IsValid(Texture))
	{
		return -1;
	}
	const float Width = Texture->GetSurfaceWidth();
	return Width > 0.0f ? Width : -1.0f;
}

float UDreamTextureBase::GetPreferredHeight() const
{
	if (!IsValid(Texture))
	{
		return -1;
	}
	const float Height = Texture->GetSurfaceHeight();
	return Height > 0.0f ? Height : -1.0f;
}

#undef LOCTEXT_NAMESPACE
