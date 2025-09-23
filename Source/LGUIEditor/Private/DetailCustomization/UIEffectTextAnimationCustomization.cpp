// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UIEffectTextAnimationCustomization.h"
#include "GeometryModifier/LexMeshModifierTextAnimation.h"
#include "LGUIEditorUtils.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UIEffectTextAnimationCustomization"

TSharedRef<IDetailCustomization> FUIEffectTextAnimationCustomization::MakeInstance()
{
	return MakeShareable(new FUIEffectTextAnimationCustomization);
}
void FUIEffectTextAnimationCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<ULexMeshModifierTextAnimation>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
}
#undef LOCTEXT_NAMESPACE