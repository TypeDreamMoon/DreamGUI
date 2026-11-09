// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamVisualBatchMeshCustomization.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "MaterialDomain.h"

#define LOCTEXT_NAMESPACE "DreamVisualBatchMeshCustomization"
FDreamVisualBatchMeshCustomization::FDreamVisualBatchMeshCustomization()
{
}

FDreamVisualBatchMeshCustomization::~FDreamVisualBatchMeshCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamVisualBatchMeshCustomization::MakeInstance()
{
	return MakeShareable(new FDreamVisualBatchMeshCustomization);
}
void FDreamVisualBatchMeshCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UDreamVisualBatchMesh>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{

	}
	else
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}

	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	// The blend mode is a row of the widget's Layer section, beside its opacity and mask: its DreamGUI_Section tag says so,
	// and FDreamDetailsLayout leaves it out of the visual's own rows.
}
#undef LOCTEXT_NAMESPACE