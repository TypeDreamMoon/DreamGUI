// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexUIFontEmojiDataCustomization.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/LexUIFontEmojiData.h"
#include "PropertyType/LexUIFontEmojiKeyCustomization.h"

#define LOCTEXT_NAMESPACE "LexUIFontEmojiDataCustomization"
FLexUIFontEmojiDataCustomization::FLexUIFontEmojiDataCustomization()
{
}

FLexUIFontEmojiDataCustomization::~FLexUIFontEmojiDataCustomization()
{
}

TSharedRef<IDetailCustomization> FLexUIFontEmojiDataCustomization::MakeInstance()
{
	return MakeShareable(new FLexUIFontEmojiDataCustomization);
}
void FLexUIFontEmojiDataCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexUIFontEmojiData>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexUIFontEmojiKey::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexUIFontEmojiKeyCustomization::MakeInstance));
}
#undef LOCTEXT_NAMESPACE