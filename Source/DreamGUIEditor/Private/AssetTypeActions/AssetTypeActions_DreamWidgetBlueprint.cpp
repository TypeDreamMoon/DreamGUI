// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "AssetTypeActions_DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprint.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_DreamWidgetBlueprint"

FAssetTypeActions_DreamWidgetBlueprint::FAssetTypeActions_DreamWidgetBlueprint(EAssetTypeCategories::Type InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FAssetTypeActions_DreamWidgetBlueprint::GetName() const
{
	return LOCTEXT("Name", "DreamUI Widget Blueprint");
}

UClass* FAssetTypeActions_DreamWidgetBlueprint::GetSupportedClass() const
{
	return UDreamWidgetBlueprint::StaticClass();
}

void FAssetTypeActions_DreamWidgetBlueprint::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object);
		if (Blueprint == nullptr)
		{
			continue;
		}
		// A Blueprint whose parent class went missing cannot be compiled or previewed, and the
		// designer would open onto a hierarchy nothing can instance. The stock Blueprint editor is
		// the right place to repair that, so hand it over rather than opening a broken designer.
		if (Blueprint->ParentClass == nullptr)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				LOCTEXT("MissingParentClass", "'{0}' has no parent class and cannot be designed until that is repaired."),
				FText::FromString(Blueprint->GetName())));
			continue;
		}
		TSharedRef<FDreamWidgetBlueprintEditor> Editor(new FDreamWidgetBlueprintEditor());
		Editor->InitDesigner(Mode, EditWithinLevelEditor, Blueprint);
	}
}

#undef LOCTEXT_NAMESPACE
