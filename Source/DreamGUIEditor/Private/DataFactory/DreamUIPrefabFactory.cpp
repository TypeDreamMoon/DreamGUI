// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DataFactory/DreamUIPrefabFactory.h"

#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "Interaction/DreamContentWidget.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Modules/ModuleManager.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabFactory"

namespace DreamUIPrefabFactoryLocal
{
	class FRootLayoutClassFilter : public IClassViewerFilter
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

UDreamUIPrefabFactory::UDreamUIPrefabFactory()
{
	SupportedClass = UDreamUIPrefab::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UDreamUIPrefabFactory::ConfigureProperties()
{
	RootLayoutClass = nullptr;
	if (SourcePrefab != nullptr)
	{
		return true;
	}

	FModuleManager::LoadModuleChecked<FClassViewerModule>(TEXT("ClassViewer"));

	FClassViewerInitializationOptions Options;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bShowNoneOption = true;
	Options.bExpandAllNodes = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
	Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerCanvasPanel::StaticClass());
	Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerOverlay::StaticClass());
	Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerHorizontalBox::StaticClass());
	Options.ExtraPickerCommonClasses.Add(UDreamLayoutContainerVerticalBox::StaticClass());

	TSharedRef<DreamUIPrefabFactoryLocal::FRootLayoutClassFilter> Filter =
		MakeShared<DreamUIPrefabFactoryLocal::FRootLayoutClassFilter>();
	Filter->AllowedChildrenOfClasses.Add(UDreamLayoutContainer::StaticClass());
	Filter->DisallowedClassFlags = CLASS_Abstract
		| CLASS_Deprecated
		| CLASS_NewerVersionExists
		| CLASS_Hidden
		| CLASS_HideDropDown
		| CLASS_Transient;
	Options.ClassFilters.Add(Filter);

	const FText TitleText = LOCTEXT("PickRootLayout", "Pick Root Layout for New DreamUI Prefab");
	return SClassPickerDialog::PickClass(
		TitleText,
		Options,
		static_cast<UClass*&>(RootLayoutClass),
		UDreamLayoutContainer::StaticClass());
}

UObject* UDreamUIPrefabFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (SourcePrefab != nullptr)//prefab variant
	{
		UDreamUIPrefab* NewAsset = NewObject<UDreamUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = true;
		NewAsset->PrefabSchemaVersion = LEXUI_CURRENT_PREFAB_SCHEMA_VERSION;
		auto HelperObject = NewAsset->GetPrefabHelperObject();
		HelperObject->PrefabAsset = NewAsset;
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<TObjectPtr<UDreamWidget>, FDreamUISubPrefabData> SubPrefabMap;
		auto PrefabScene = NewAsset->GetPrefabInstanceScene();
		auto World = PrefabScene->GetWorld();
		HelperObject->LoadedRootWidget = SourcePrefab->LoadPrefabWithExistingObjects(World, World, PrefabScene->GetParentForLoadPrefab(NewAsset), MapGuidToObject, SubPrefabMap);
		FDreamUISubPrefabData SubPrefabData;
		SubPrefabData.PrefabAsset = SourcePrefab;
		SubPrefabData.MapGuidToObject = MapGuidToObject;
		for (auto& KeyValue : MapGuidToObject)
		{
			auto GuidInParent = FGuid::NewGuid();
			SubPrefabData.MapObjectGuidFromParentPrefabToSubPrefab.Add(GuidInParent, KeyValue.Key);
			HelperObject->MapGuidToObject.Add(GuidInParent, KeyValue.Value);
		}
		HelperObject->SubPrefabMap.Add(HelperObject->LoadedRootWidget, SubPrefabData);
		HelperObject->SavePrefab();
		return NewAsset;
	}
	else
	{
		UDreamUIPrefab* NewAsset = NewObject<UDreamUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = false;
		NewAsset->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;
		NewAsset->PrefabSchemaVersion = LEXUI_CURRENT_PREFAB_SCHEMA_VERSION;
		auto HelperObject = NewAsset->GetPrefabHelperObject();
		HelperObject->PrefabAsset = NewAsset;
		auto PrefabScene = NewAsset->GetPrefabInstanceScene();
		auto World = PrefabScene->GetWorld();
		HelperObject->LoadedRootWidget = NewObject<UDreamWidget>(World);
		HelperObject->LoadedRootWidget->SetParent(PrefabScene->GetParentForLoadPrefab(NewAsset));
		HelperObject->LoadedRootWidget->SetDisplayName(NewAsset->GetName());
		// Stretch to the design canvas, the way a new UMG widget fills its designer surface and the
		// way UDreamScreenUISubsystem::ConfigurePage stretches a page at runtime. Left at the widget
		// default the root is a small fixed rect in the corner, which every full-screen prefab then
		// has to fix by hand, and which silently stays wrong for one used as a page.
		HelperObject->LoadedRootWidget->SetHorizontalAndVerticalAnchorMinMax(
			FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
		HelperObject->LoadedRootWidget->SetAnchoredPosition(FVector2D::ZeroVector);
		HelperObject->LoadedRootWidget->SetSizeDelta(FVector2D::ZeroVector);
		if (RootLayoutClass != nullptr)
		{
			check(RootLayoutClass->IsChildOf(UDreamLayoutContainer::StaticClass()));
			check(!RootLayoutClass->HasAnyClassFlags(CLASS_Abstract));
			if (HelperObject->LoadedRootWidget->CreateNewLayoutContainer(RootLayoutClass.Get())
				&& (RootLayoutClass->IsChildOf(UDreamLayoutContainerSizeBox::StaticClass())
					|| RootLayoutClass->IsChildOf(UDreamLayoutContainerScaleBox::StaticClass())
					|| RootLayoutClass->IsChildOf(UDreamLayoutContainerSafeZone::StaticClass())))
			{
				HelperObject->LoadedRootWidget->AddComponent<UDreamContentWidget>();
			}
		}
		HelperObject->SavePrefab();
		return NewAsset;
	}
}

#undef LOCTEXT_NAMESPACE
