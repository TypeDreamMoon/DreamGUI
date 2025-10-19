// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUISettings.h"
#include "LGUI.h"
#include "Core/LexUISpriteData.h"
#include "Core/LexUIDynamicSpriteAtlasData.h"

#if WITH_EDITOR
float ULexUISettings::CacheAutoBatchThreshold = -1;
void ULexUISettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISettings, DefaultAtlasSetting)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISettings, AtlasSettingForSpecificPackingTag)
			)
		{
			ULexUISpriteData::MarkAllSpritesNeedToReinitialize();
			ULexUIDynamicSpriteAtlasManager::InitCheck();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUISettings, AutoBatchThreshold))
		{
			CacheAutoBatchThreshold = AutoBatchThreshold;
		}
	}
}
#endif
const FLexUIAtlasSettings& ULexUISettings::GetAtlasSettings(const FName& InPackingTag)
{
	auto Settings = GetDefault<ULexUISettings>();
	if (auto AtlasSettings = Settings->AtlasSettingForSpecificPackingTag.Find(InPackingTag))
	{
		return *AtlasSettings;
	}
	else
	{
		return Settings->DefaultAtlasSetting;
	}
}
int32 ULexUISettings::GetAtlasTextureInitialSize(const FName& InPackingTag)
{
	return ConvertAtlasTextureSizeTypeToSize(GetAtlasSettings(InPackingTag).AtlasTextureInitialSize);
}
bool ULexUISettings::GetAtlasTextureSRGB(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).AtlasTextureUseSRGB;
}
int32 ULexUISettings::GetAtlasTexturePadding(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).SpaceBetweenSprites;
}
TextureFilter ULexUISettings::GetAtlasTextureFilter(const FName& InPackingTag)
{
	return GetAtlasSettings(InPackingTag).AtlasTextureFilter;
}
const TMap<FName, FLexUIAtlasSettings>& ULexUISettings::GetAllAtlasSettings()
{
	return GetDefault<ULexUISettings>()->AtlasSettingForSpecificPackingTag;
}
float ULexUISettings::GetAutoBatchThreshold()
{
#if WITH_EDITOR
	if (CacheAutoBatchThreshold <= -0.5f)
	{
		CacheAutoBatchThreshold = GetDefault<ULexUISettings>()->AutoBatchThreshold;
	}
	return CacheAutoBatchThreshold;
#else
	return GetDefault<ULGUISettings>()->AutoBatchThreshold;
#endif
}
int32 ULexUISettings::ConvertAtlasTextureSizeTypeToSize(const ELexUIAtlasTextureSizeType& InType)
{
	return ((int32)FMath::Pow(2.0, (double)InType)) * 256;
}
int32 ULexUISettings::GetPriorityInSceneViewExtension()
{
	return GetDefault<ULexUISettings>()->PriorityInSceneViewExtension;
}


#if WITH_EDITOR
void ULexUIEditorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	auto MemberProperty = PropertyChangedEvent.MemberProperty;
	auto Property = PropertyChangedEvent.Property;
	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexUIEditorSettings, ExtraPrefabFolders)
			|| (
				Property->GetFName() == GET_MEMBER_NAME_CHECKED(FDirectoryPath, Path)
				&& MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexUIEditorSettings, ExtraPrefabFolders)
				)
			)
		{
			for (FDirectoryPath& PathToFix : ExtraPrefabFolders)
			{
				if (!PathToFix.Path.IsEmpty() && !PathToFix.Path.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
				{
					PathToFix.Path = FString::Printf(TEXT("/Game/%s"), *PathToFix.Path);
				}
			}
			if (IsValid(GEditor))
			{
				GEditor->BroadcastLevelActorListChanged();//refresh Outliner menu
			}
		}
	}
}
void ULexUIEditorSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif
