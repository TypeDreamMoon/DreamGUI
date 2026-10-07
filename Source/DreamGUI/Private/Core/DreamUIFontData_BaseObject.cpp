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

void UDreamUIFontData_BaseObject::BindEmojiData()
{
	if (IsValid(EmojiData))
	{
		// Unbound first: a font that is bound here and again on load (or again by an edit) would
		// otherwise refresh its texts once per binding.
		EmojiData->OnDataChange.RemoveAll(this);
		EmojiData->OnDataChange.AddWeakLambda(this, [this]()
		{
			OnEmojiDataChanged.Broadcast();
		});
	}
}

void UDreamUIFontData_BaseObject::PostInitProperties()
{
	UObject::PostInitProperties();
	BindEmojiData();
}

void UDreamUIFontData_BaseObject::PostLoad()
{
	UObject::PostLoad();
	// PostInitProperties runs before a loaded asset's properties are read, so for every font that came
	// from disk EmojiData was still empty there and nothing was bound: editing the emoji asset never
	// refreshed a text. Here the property holds what was saved.
	BindEmojiData();
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
		BindEmojiData();
		OnEmojiDataChanged.Broadcast();
	}
}
void UDreamUIFontData_BaseObject::PreEditChange(FProperty* PropertyAboutToChange)
{
	UObject::PreEditChange(PropertyAboutToChange);
	// Null means "an undo is about to restore everything", which no per-property branch below can
	// answer. See the note on UDreamWidget::PreEditChange.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}
	auto PropertyName = PropertyAboutToChange->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIFontData_BaseObject, EmojiData))
	{
		if (IsValid(EmojiData))
		{
			EmojiData->OnDataChange.RemoveAll(this);
		}
	}
}

void UDreamUIFontData_BaseObject::PreEditUndo()
{
	// The transaction restores EmojiData without naming that property. Let go while it still names
	// the asset whose event this font subscribed to, before serialization replaces the pointer.
	if (IsValid(EmojiData))
	{
		EmojiData->OnDataChange.RemoveAll(this);
	}
	Super::PreEditUndo();
}

void UDreamUIFontData_BaseObject::PostEditUndo()
{
	Super::PostEditUndo();
	if (IsValid(this))
	{
		BindEmojiData();
		OnEmojiDataChanged.Broadcast();
	}
}
#endif

#undef LOCTEXT_NAMESPACE
