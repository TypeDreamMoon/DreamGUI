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
	auto lguiSettings = GetDefault<ULexUISettings>();
	if (auto atlasSettings = lguiSettings->AtlasSettingForSpecificPackingTag.Find(InPackingTag))
	{
		return *atlasSettings;
	}
	else
	{
		return lguiSettings->DefaultAtlasSetting;
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
FSimpleMulticastDelegate ULexUIEditorSettings::LexUIPreviewSetting_EditorPreviewViewportIndexChange;
FSimpleMulticastDelegate ULexUIEditorSettings::LexUIEditorSetting_PreserveHierarchyStateChange;
void ULexUIEditorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	auto MemberProperty = PropertyChangedEvent.MemberProperty;
	auto Property = PropertyChangedEvent.Property;
	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexUIEditorSettings, LexUIPreview_EditorViewIndex))
		{
			if (LexUIPreviewSetting_EditorPreviewViewportIndexChange.IsBound())
			{
				LexUIPreviewSetting_EditorPreviewViewportIndexChange.Broadcast();
			}
		}
		else if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexUIEditorSettings, ExtraPrefabFolders)
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
		else if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULexUIEditorSettings, bPreserveHierarchyState))
		{
			LexUIEditorSetting_PreserveHierarchyStateChange.Broadcast();
		}
	}
}
void ULexUIEditorSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
int32 ULexUIEditorSettings::GetLexUIPreview_EditorViewIndex()
{
	return GetDefault<ULexUIEditorSettings>()->LexUIPreview_EditorViewIndex;
}
void ULexUIEditorSettings::SetLexUIPreview_EditorViewIndex(int32 value)
{
	GetMutableDefault<ULexUIEditorSettings>()->LexUIPreview_EditorViewIndex = value;
	if (LexUIPreviewSetting_EditorPreviewViewportIndexChange.IsBound())
	{
		LexUIPreviewSetting_EditorPreviewViewportIndexChange.Broadcast();
	}
}
bool ULexUIEditorSettings::GetPreserveHierarchyState()
{
	return GetDefault<ULexUIEditorSettings>()->bPreserveHierarchyState;
}
float ULexUIEditorSettings::GetDelayRestoreHierarchyTime()
{
	return GetDefault<ULexUIEditorSettings>()->DelayRestoreHierarchyTime;
}
#endif
