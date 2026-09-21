// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DataFactory/DreamWidgetBlueprintFactory.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"

#include "AssetToolsModule.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "DreamWidgetBlueprintFactory"

namespace DreamWidgetBlueprintFactoryLocal
{
	class FDerivedClassFilter : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(
			const FClassViewerInitializationOptions& InInitOptions,
			const UClass* InClass,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return !InClass->HasAnyClassFlags(DisallowedClassFlags)
				&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
		}

		virtual bool IsUnloadedClassAllowed(
			const FClassViewerInitializationOptions& InInitOptions,
			const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData,
			TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags)
				&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
		}

		TSet<const UClass*> AllowedChildrenOfClasses;
		EClassFlags DisallowedClassFlags = CLASS_None;
	};
}

UDreamWidgetBlueprintFactory::UDreamWidgetBlueprintFactory()
{
	SupportedClass = UDreamWidgetBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	ParentClass = UDreamUserWidget::StaticClass();
}

uint32 UDreamWidgetBlueprintFactory::GetMenuCategories() const
{
	// The base resolves this through the asset type actions registered for SupportedClass, and there
	// are none for this type yet -- which would file it under Blueprint rather than under DreamUI.
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	return AssetTools.FindAdvancedAssetCategory(FName(TEXT("DreamUI")));
}

bool UDreamWidgetBlueprintFactory::ConfigureProperties()
{
	ParentClass = UDreamUserWidget::StaticClass();
	RootLayoutClass = nullptr;

	FModuleManager::LoadModuleChecked<FClassViewerModule>(TEXT("ClassViewer"));

	{
		FClassViewerInitializationOptions Options;
		Options.DisplayMode = EClassViewerDisplayMode::TreeView;
		Options.Mode = EClassViewerMode::ClassPicker;
		Options.bShowNoneOption = false;
		Options.bExpandAllNodes = true;
		Options.bIsBlueprintBaseOnly = true;
		Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
		Options.ExtraPickerCommonClasses.Add(UDreamUserWidget::StaticClass());

		TSharedRef<DreamWidgetBlueprintFactoryLocal::FDerivedClassFilter> Filter =
			MakeShared<DreamWidgetBlueprintFactoryLocal::FDerivedClassFilter>();
		Filter->AllowedChildrenOfClasses.Add(UDreamUserWidget::StaticClass());
		Filter->DisallowedClassFlags = CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden | CLASS_HideDropDown;
		Options.ClassFilters.Add(Filter);

		UClass* Chosen = nullptr;
		const bool bPicked = SClassPickerDialog::PickClass(
			LOCTEXT("PickParentClass", "Pick Parent Class for New DreamUI Widget Blueprint"),
			Options, Chosen, UDreamUserWidget::StaticClass());
		if (!bPicked)
		{
			return false;
		}
		ParentClass = Chosen != nullptr ? Chosen : UDreamUserWidget::StaticClass();
	}

	{
		FClassViewerInitializationOptions Options;
		Options.DisplayMode = EClassViewerDisplayMode::TreeView;
		Options.Mode = EClassViewerMode::ClassPicker;
		// None is a real answer here: a hierarchy whose root carries no panel at all is the one shape
		// the layout-container registry has no descriptor for, and it is what a free-form root wants.
		Options.bShowNoneOption = true;
		Options.bExpandAllNodes = true;
		Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
		Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerCanvasPanel::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerOverlay::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerHorizontalBox::StaticClass());
		Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerVerticalBox::StaticClass());

		TSharedRef<DreamWidgetBlueprintFactoryLocal::FDerivedClassFilter> Filter =
			MakeShared<DreamWidgetBlueprintFactoryLocal::FDerivedClassFilter>();
		Filter->AllowedChildrenOfClasses.Add(UDreamLayoutContainer::StaticClass());
		Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists
			| CLASS_Hidden | CLASS_HideDropDown | CLASS_Transient;
		Options.ClassFilters.Add(Filter);

		return SClassPickerDialog::PickClass(
			LOCTEXT("PickRootLayout", "Pick Root Layout for New DreamUI Widget Blueprint"),
			Options, static_cast<UClass*&>(RootLayoutClass), UDreamLayoutContainer::StaticClass());
	}
}

UObject* UDreamWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* Context, FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UDreamWidgetBlueprint::StaticClass()));

	UClass* CurrentParentClass = ParentClass != nullptr ? ParentClass.Get() : UDreamUserWidget::StaticClass();
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(CurrentParentClass) || !CurrentParentClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("CannotCreate", "Cannot create a DreamUI Widget Blueprint based on the class '{0}'."),
			FText::FromString(GetNameSafe(CurrentParentClass))));
		return nullptr;
	}

	UDreamWidgetBlueprint* NewBlueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		CurrentParentClass, InParent, Name, BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	if (NewBlueprint == nullptr)
	{
		return nullptr;
	}

	UDreamWidgetTree* Tree = NewBlueprint->GetOrCreateWidgetTree();
	if (UDreamWidget* Root = Tree->RootWidget)
	{
		Root->SetDisplayName(Name.ToString());
		// Stretch to the design canvas, the way a new UMG widget fills its designer surface and the
		// way UDreamScreenUISubsystem::ConfigurePage stretches a page at runtime. Left at the widget
		// default the root is a small fixed rect in the corner, which every full-screen hierarchy then
		// has to fix by hand, and which silently stays wrong for one used as a page.
		//
		// Written as anchor DATA rather than through SetHorizontalAndVerticalAnchorMinMax: that
		// setter's whole body sits inside `if (Parent.IsValid())` and a TREE'S ROOT HAS NO PARENT,
		// so the call was a no-op (with a warning nobody reads) while the SizeDelta line below
		// applied regardless. The result was the opposite of what the paragraph above asks for: not
		// a small rect in the corner but a 0x0 one, and a 0x0 root arranges every descendant into a
		// 0x0 rect -- so a brand-new widget blueprint came up with its whole hierarchy collapsed and
		// nothing anywhere saying why. Every control in it reads as "size is always zero".
		FDreamUIAnchorData Anchors = Root->GetAnchorData();
		Anchors.AnchorMin = FVector2D::ZeroVector;
		Anchors.AnchorMax = FVector2D(1.0, 1.0);
		Anchors.AnchoredPosition = FVector2D::ZeroVector;
		Anchors.SizeDelta = FVector2D::ZeroVector;
		Root->SetAnchorData(Anchors);
		if (RootLayoutClass != nullptr)
		{
			// CreateNewLayoutContainer adds whatever behaviours the container declares it needs, so
			// nothing is added by hand here.
			Root->CreateNewLayoutContainer(RootLayoutClass.Get());
		}
	}

	// The tree exists now, so the class has to be built from it -- an asset that ships its first
	// compile without a hierarchy hands every early instance an empty one.
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	return NewBlueprint;
}

#undef LOCTEXT_NAMESPACE
