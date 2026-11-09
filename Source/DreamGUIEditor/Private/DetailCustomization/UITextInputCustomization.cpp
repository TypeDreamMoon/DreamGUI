// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UITextInputCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "Interaction/UITextInput.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"

#define LOCTEXT_NAMESPACE "UITextComponentDetails"
FUITextInputCustomization::FUITextInputCustomization()
{
}

FUITextInputCustomization::~FUITextInputCustomization()
{
}

TSharedRef<IDetailCustomization> FUITextInputCustomization::MakeInstance()
{
	return MakeShareable(new FUITextInputCustomization);
}
void FUITextInputCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	// An empty list is a real state -- the panel rebuilds while a selection is being cleared -- and
	// indexing [0] there reads off the end of an empty array. The null branch below already handles
	// "no target", so this only has to reach it.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UUITextInput>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// The custom validation, the password character and the multi-line submit keys show by their EditConditions -- for a
	// custom input type, a password display, a multi-line input -- so the panel needs no rebuilding when those change.
	DetailBuilder.EditCategory("DreamGUI-Input");
}
void FUITextInputCustomization::ForceRefresh(TWeakPtr<IPropertyUtilities> InPropertyUtilities)
{
	if (TargetScriptPtr.IsValid())
	{
		if (const TSharedPtr<IPropertyUtilities> PropertyUtilities = InPropertyUtilities.Pin())
		{
			// Requested, not forced: this runs inside the property's own value-changed broadcast, and
			// the immediate variant tears the layout down while that broadcast is still on the stack.
			PropertyUtilities->RequestForceRefresh();
		}
	}
}
#undef LOCTEXT_NAMESPACE