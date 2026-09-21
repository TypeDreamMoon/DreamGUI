// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIFontEmojiDataCustomization.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "IDetailsView.h"
#include "Core/DreamUIFontEmojiData.h"
#include "PropertyType/DreamUIFontEmojiKeyCustomization.h"

#define LOCTEXT_NAMESPACE "DreamUIFontEmojiDataCustomization"
FDreamUIFontEmojiDataCustomization::FDreamUIFontEmojiDataCustomization()
{
}

FDreamUIFontEmojiDataCustomization::~FDreamUIFontEmojiDataCustomization()
{
}

TSharedRef<IDetailCustomization> FDreamUIFontEmojiDataCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIFontEmojiDataCustomization);
}
void FDreamUIFontEmojiDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	// An empty list is a real state -- the panel rebuilds while a selection is being cleared.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamUIFontEmojiData>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// Null on a host that is not an SDetailsView (a details panel embedded in another tool).
	if (const TSharedPtr<IDetailsView> DetailsView = DetailBuilder.GetDetailsViewSharedPtr())
	{
		DetailsView->RegisterInstancedCustomPropertyTypeLayout(FDreamUIFontEmojiKey::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamUIFontEmojiKeyCustomization::MakeInstance));
	}
}
#undef LOCTEXT_NAMESPACE