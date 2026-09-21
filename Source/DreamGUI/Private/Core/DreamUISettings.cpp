// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUISettings.h"
#include "DreamGUI.h"
#include "RHIGlobals.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUIDynamicSpriteAtlasData.h"

#if WITH_EDITOR
float UDreamUISettings::CacheAutoBatchThreshold = -1;
void UDreamUISettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISettings, DefaultAtlasSetting)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISettings, AtlasSettingForSpecificPackingTag)
			)
		{
			UDreamUISpriteData::MarkAllSpritesNeedToReinitialize();
			UDreamUIDynamicSpriteAtlasManager::InitCheck();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUISettings, AutoBatchThreshold))
		{
			CacheAutoBatchThreshold = AutoBatchThreshold;
		}
	}
}
#endif
const FDreamUIAtlasSettings& UDreamUISettings::GetAtlasSettings(const FName& InPackingTag)
{
	auto Settings = GetDefault<UDreamUISettings>();
	if (auto AtlasSettings = Settings->AtlasSettingForSpecificPackingTag.Find(InPackingTag))
	{
		return *AtlasSettings;
	}
	else
	{
		return Settings->DefaultAtlasSetting;
	}
}
int32 UDreamUISettings::GetAtlasTextureMaxSize(const FName& InPackingTag)
{
	return ConvertAtlasTextureSizeTypeToSize(GetAtlasSettings(InPackingTag).AtlasTextureMaxSize);
}
bool UDreamUISettings::GetAtlasTextureSRGB(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).AtlasTextureUseSRGB;
}
int32 UDreamUISettings::GetAtlasTexturePadding(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).SpaceBetweenSprites;
}
TextureFilter UDreamUISettings::GetAtlasTextureFilter(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).AtlasTextureFilter;
}
const TMap<FName, FDreamUIAtlasSettings>& UDreamUISettings::GetAllAtlasSettings()
{
	return GetDefault<UDreamUISettings>()->AtlasSettingForSpecificPackingTag;
}
float UDreamUISettings::GetAutoBatchThreshold()
{
#if WITH_EDITOR
	if (CacheAutoBatchThreshold <= -0.5f)
	{
		CacheAutoBatchThreshold = GetDefault<UDreamUISettings>()->AutoBatchThreshold;
	}
	return CacheAutoBatchThreshold;
#else
	return GetDefault<UDreamUISettings>()->AutoBatchThreshold;
#endif
}
float UDreamUISettings::GetParkedWidgetLifetimeSeconds()
{
	return GetDefault<UDreamUISettings>()->ParkedWidgetLifetimeSeconds;
}
int32 UDreamUISettings::ConvertAtlasTextureSizeTypeToSize(const EDreamUIAtlasTextureSizeType& InType)
{
	return 1 << ((int32)InType + 8);
}
int32 UDreamUISettings::GetPriorityInSceneViewExtension()
{
	return GetDefault<UDreamUISettings>()->PriorityInSceneViewExtension;
}

bool UDreamUISettings::GetUseBuiltInUIShader()
{
	return GetDefault<UDreamUISettings>()->bUseBuiltInUIShader;
}

bool UDreamUISettings::GetAsyncGlyphRasterization()
{
	return GetDefault<UDreamUISettings>()->bAsyncGlyphRasterization;
}

int32 UDreamUISettings::GetAsyncGlyphSyncBudgetPerFrame()
{
	return GetDefault<UDreamUISettings>()->AsyncGlyphSyncBudgetPerFrame;
}

int32 UDreamUISettings::GetMaxFontAtlasSlices()
{
	// Past GMaxTextureArrayLayers the create produces a texture the sampler cannot address, so the RHI
	// has the final word whatever the project asked for.
	const int32 RHILimit = FMath::Max((int32)GMaxTextureArrayLayers, 1);
	return FMath::Clamp(GetDefault<UDreamUISettings>()->MaxFontAtlasSlices, 1, RHILimit);
}

