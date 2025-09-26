// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexTextureBase.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Core/Components/LexCanvas.h"
#include "Materials/MaterialInterface.h"
#include "Utils/LexUIUtils.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "UITextureBase"

ULexTextureBase::ULexTextureBase(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
}

void ULexTextureBase::BeginPlay()
{
	Super::BeginPlay();
}
#if WITH_EDITOR
void ULexTextureBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULexTextureBase, Texture))
		{
			MarkTextureDirty();
		}
	}
}
void ULexTextureBase::CheckTexture()
{
	if (!IsValid(Texture))
	{
		auto defaultWhiteSolid = FLexUIUtils::GetDefaultWhiteTexture();
		if (IsValid(defaultWhiteSolid))
		{
			Texture = defaultWhiteSolid;
		}
	}
}
#endif

UTexture* ULexTextureBase::GetTextureToCreateGeometry()
{
	if (!IsValid(Texture))
	{
		Texture = FLexUIUtils::GetDefaultWhiteTexture();
	}
	return Texture;
}

bool ULexTextureBase::ReadPixelFromMainTexture(const FVector2D& InUV, FColor& OutPixel)const
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

void ULexTextureBase::SetTexture(UTexture* Value)
{
	if (Texture != Value)
	{
		Texture = Value;
		if (Texture == nullptr)
		{
			Texture = FLexUIUtils::GetDefaultWhiteTexture();
		}
		MarkTextureDirty();
	}
}
void ULexTextureBase::SetSizeFromTexture()
{
	if (IsValid(Texture))
	{
		auto Widget = GetWidget();
		Widget->SetWidth(Texture->GetSurfaceWidth());
		Widget->SetHeight(Texture->GetSurfaceHeight());
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Texture is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}

#undef LOCTEXT_NAMESPACE
