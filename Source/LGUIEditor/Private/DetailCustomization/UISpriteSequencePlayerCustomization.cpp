// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISpriteSequencePlayerCustomization.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "LexUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Core/Components/LexSpriteBase.h"

#define LOCTEXT_NAMESPACE "UISpriteSequencePlayerCustomization"

TSharedRef<IDetailCustomization> FUISpriteSequencePlayerCustomization::MakeInstance()
{
	return MakeShareable(new FUISpriteSequencePlayerCustomization);
}
void FUISpriteSequencePlayerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUISpriteSequencePlayer>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	FLexUIEditorUtils::ShowError_RequireComponent(&DetailBuilder, TargetScriptPtr.Get(), ULexSpriteBase::StaticClass());
}
#undef LOCTEXT_NAMESPACE