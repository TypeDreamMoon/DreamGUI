// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISpriteSheetTexturePlayerCustomization.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "LexUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Core/Components/LexTexture.h"

#define LOCTEXT_NAMESPACE "UISpriteSheetTexturePlayerCustomization"

TSharedRef<IDetailCustomization> FUISpriteSheetTexturePlayerCustomization::MakeInstance()
{
	return MakeShareable(new FUISpriteSheetTexturePlayerCustomization);
}
void FUISpriteSheetTexturePlayerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UUISpriteSheetTexturePlayer>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	FLexUIEditorUtils::ShowError_RequireComponent(&DetailBuilder, TargetScriptPtr.Get(), ULexTexture::StaticClass());
}
#undef LOCTEXT_NAMESPACE