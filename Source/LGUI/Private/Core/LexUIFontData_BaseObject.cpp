// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIFontData_BaseObject.h"
#include "LGUI.h"
#include "Core/LexUIFontEmojiData.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LGUIFontData_BaseObject"

ULexUIFontData_BaseObject* ULexUIFontData_BaseObject::GetDefaultFont()
{
	static auto defaultFont = LoadObject<ULexUIFontData_BaseObject>(NULL, TEXT("/LGUI/DefaultFont_DistanceField"));
	if (defaultFont == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default font error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		return nullptr;
	}
	return defaultFont;
}

void ULexUIFontData_BaseObject::PostInitProperties()
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

void ULexUIFontData_BaseObject::BeginDestroy()
{
	UObject::BeginDestroy();
	if (IsValid(EmojiData))
	{
		EmojiData->OnDataChange.RemoveAll(this);
	}
}

#if WITH_EDITOR
void ULexUIFontData_BaseObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_BaseObject, EmojiData))
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
void ULexUIFontData_BaseObject::PreEditChange(FProperty* PropertyAboutToChange)
{
	UObject::PreEditChange(PropertyAboutToChange);
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexUIFontData_BaseObject, EmojiData))
	{
		if (IsValid(EmojiData))
		{
			EmojiData->OnDataChange.RemoveAll(this);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
