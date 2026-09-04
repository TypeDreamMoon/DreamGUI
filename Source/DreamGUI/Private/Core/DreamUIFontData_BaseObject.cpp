// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamGUISettings.h"
#include "DreamGUI.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Utils/DreamUIUtils.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DreamGUIFontData_BaseObject"

UDreamUIFontData_BaseObject* UDreamUIFontData_BaseObject::GetDefaultFont()
{
	// Strong, and re-checked every call: a bare static UObject* was neither. Nothing else references
	// the default font once the last text widget of a level is gone, so GC collected it and the next
	// widget got the dangling pointer back; and a first load that failed was cached as null forever.
	static TStrongObjectPtr<UDreamUIFontData_BaseObject> defaultFontCache;
	if (!defaultFontCache.IsValid())
	{
		defaultFontCache.Reset(UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultFont, TEXT("DefaultFont")));
	}
	auto defaultFont = defaultFontCache.Get();
	if (defaultFont == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default font error! Missing some content of DreamUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(DreamGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		return nullptr;
	}
	return defaultFont;
}

void UDreamUIFontData_BaseObject::PostInitProperties()
{
	UObject::PostInitProperties();
	if (IsValid(EmojiData))
	{
		EmojiData->OnDataChange.AddWeakLambda(this, [this]()
		{
			OnEmojiDataChanged.Broadcast();
		});
	}
}

void UDreamUIFontData_BaseObject::BeginDestroy()
{
	if (IsValid(EmojiData))
	{
		EmojiData->OnDataChange.RemoveAll(this);
	}
	UObject::BeginDestroy();
}

#if WITH_EDITOR
void UDreamUIFontData_BaseObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_BaseObject, EmojiData))
	{
		if (IsValid(EmojiData))
		{
			EmojiData->OnDataChange.AddWeakLambda(this, [this]()
			{
				OnEmojiDataChanged.Broadcast();
			});
		}
		OnEmojiDataChanged.Broadcast();
	}
}
void UDreamUIFontData_BaseObject::PreEditChange(FProperty* PropertyAboutToChange)
{
	UObject::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_BaseObject, EmojiData))
	{
		if (IsValid(EmojiData))
		{
			EmojiData->OnDataChange.RemoveAll(this);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
