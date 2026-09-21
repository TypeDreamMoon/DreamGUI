// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "AssetTypeActions_DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprint.h"
#include "DreamGUIEditorModule.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ToolMenuSection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

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

UDreamWidgetBlueprint* FAssetTypeActions_DreamWidgetBlueprint::CreateFromTemplate(UDreamWidgetBlueprint* InSource)
{
	if (!IsValid(InSource) || InSource->ParentClass == nullptr)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Cannot start from '%s': it has no parent class."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(InSource));
		return nullptr;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	const FString SourcePath = FPackageName::GetLongPackagePath(InSource->GetOutermost()->GetName());
	FString PackageName;
	FString AssetName;
	AssetTools.CreateUniqueAssetName(SourcePath / InSource->GetName(), TEXT("_Copy"), PackageName, AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Could not create the package '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *PackageName);
		return nullptr;
	}
	// The SOURCE's parent class, not UDreamUserWidget: "another one like this" means one that starts
	// from the same base as well as the same hierarchy.
	UDreamWidgetBlueprint* NewBlueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		InSource->ParentClass, Package, FName(*AssetName), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	if (NewBlueprint == nullptr)
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d Creating '%s' from '%s' produced nothing."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *AssetName, *InSource->GetName());
		return nullptr;
	}
	// The hierarchy, copied. Without a root the template has nothing to give and the new asset keeps
	// the empty one CreateBlueprint made, which is still a usable asset -- so this is not a failure.
	UDreamWidgetTree* SourceTree = InSource->WidgetTree;
	if (IsValid(SourceTree) && IsValid(SourceTree->RootWidget))
	{
		UDreamWidgetTree* NewTree = NewBlueprint->GetOrCreateWidgetTree(/*bEnsureRootWidget*/false);
		if (IsValid(NewTree))
		{
			// Whatever CreateBlueprint left behind goes first: the copy IS the hierarchy now, and a
			// tree with two roots is not a state anything downstream is written for.
			if (UDreamWidget* PreviousRoot = NewTree->RootWidget)
			{
				NewTree->RootWidget = nullptr;
				PreviousRoot->DestroyWidget();
			}
			// DuplicateSubtree, not DuplicateObject: widgets are outered flat to their tree, so a
			// widget's children are not its subobjects and duplication would have left the copy's
			// Children array pointing into the SOURCE asset.
			NewTree->RootWidget = UDreamWidget::DuplicateSubtree(NewTree, SourceTree->RootWidget);
			NewTree->RebuildParentLinks();
		}
	}
	// Built from the tree it now has, for the reason the factory compiles too: an asset that ships
	// its first compile without a hierarchy hands every early instance an empty one.
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	NewBlueprint->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	return NewBlueprint;
}

void FAssetTypeActions_DreamWidgetBlueprint::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UDreamWidgetBlueprint>> Blueprints;
	for (UObject* Object : InObjects)
	{
		if (UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(Object))
		{
			Blueprints.Add(Blueprint);
		}
	}
	if (Blueprints.Num() == 0)
	{
		return;
	}
	Section.AddMenuEntry(
		"DreamUI_NewFromTemplate",
		LOCTEXT("NewFromTemplate", "New Widget Blueprint From This"),
		LOCTEXT("NewFromTemplateTooltip", "Create a new DreamUI Widget Blueprint beside this one, with the same parent class and a copy of its hierarchy. The copy is independent from then on; to keep following this asset, derive from it instead."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.WidgetBlueprint"),
		FUIAction(FExecuteAction::CreateLambda([Blueprints]()
		{
			TArray<UObject*> Created;
			for (const TWeakObjectPtr<UDreamWidgetBlueprint>& Weak : Blueprints)
			{
				if (UDreamWidgetBlueprint* Copy = CreateFromTemplate(Weak.Get()))
				{
					Created.Add(Copy);
				}
			}
			// Opened, because the point of starting from a template is to start editing it.
			if (Created.Num() > 0 && GEditor != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(Created);
			}
		})));

	Section.AddMenuEntry(
		"DreamUI_NewChildBlueprint",
		LOCTEXT("NewChildBlueprint", "Create Child Widget Blueprint"),
		LOCTEXT("NewChildBlueprintTooltip", "Create a DreamUI Widget Blueprint whose parent class is this one. It inherits this hierarchy and keeps following it -- changes made here reach every child."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.BlueprintCore"),
		FUIAction(
			FExecuteAction::CreateLambda([Blueprints]()
			{
				// Only one: a child has exactly one parent, and "children of these three" is three
				// separate decisions the picker cannot express in one dialog.
				UDreamWidgetBlueprint* Source = Blueprints.Num() == 1 ? Blueprints[0].Get() : nullptr;
				if (!IsValid(Source) || Source->GeneratedClass == nullptr)
				{
					return;
				}
				FKismetEditorUtilities::CreateBlueprintFromClass(
					LOCTEXT("CreateChildTitle", "Create Child DreamUI Widget Blueprint"),
					Source->GeneratedClass, Source->GetName() + TEXT("_Child"));
			}),
			FCanExecuteAction::CreateLambda([Blueprints]()
			{
				return Blueprints.Num() == 1 && Blueprints[0].IsValid()
					&& Blueprints[0]->GeneratedClass != nullptr;
			})));
}

#undef LOCTEXT_NAMESPACE
