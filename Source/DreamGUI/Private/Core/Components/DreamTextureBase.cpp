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

#undef LOCTEXT_NAMESPACE
