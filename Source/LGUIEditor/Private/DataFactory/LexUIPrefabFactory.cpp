// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIPrefabFactory.h"

#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Modules/ModuleManager.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabFactory"

namespace LexUIPrefabFactoryLocal
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

ULexUIPrefabFactory::ULexUIPrefabFactory()
{
	SupportedClass = ULexUIPrefab::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool ULexUIPrefabFactory::ConfigureProperties()
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
	Options.ExtraPickerCommonClasses.Add(ULexLayoutContainerCanvasPanel::StaticClass());
	Options.ExtraPickerCommonClasses.Add(ULexLayoutContainerOverlay::StaticClass());
	Options.ExtraPickerCommonClasses.Add(ULexLayoutContainerHorizontalBox::StaticClass());
	Options.ExtraPickerCommonClasses.Add(ULexLayoutContainerVerticalBox::StaticClass());

	TSharedRef<LexUIPrefabFactoryLocal::FRootLayoutClassFilter> Filter =
		MakeShared<LexUIPrefabFactoryLocal::FRootLayoutClassFilter>();
	Filter->AllowedChildrenOfClasses.Add(ULexLayoutContainer::StaticClass());
	Filter->DisallowedClassFlags = CLASS_Abstract
		| CLASS_Deprecated
		| CLASS_NewerVersionExists
		| CLASS_Hidden
		| CLASS_HideDropDown
		| CLASS_Transient;
	Options.ClassFilters.Add(Filter);

	const FText TitleText = LOCTEXT("PickRootLayout", "Pick Root Layout for New LexUI Prefab");
	return SClassPickerDialog::PickClass(
		TitleText,
		Options,
		static_cast<UClass*&>(RootLayoutClass),
		ULexLayoutContainer::StaticClass());
}

UObject* ULexUIPrefabFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (SourcePrefab != nullptr)//prefab variant
	{
		ULexUIPrefab* NewAsset = NewObject<ULexUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = true;
		auto HelperObject = NewAsset->GetPrefabHelperObject();
		HelperObject->PrefabAsset = NewAsset;
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
		auto PrefabScene = NewAsset->GetPrefabInstanceScene();
		auto World = PrefabScene->GetWorld();
		HelperObject->LoadedRootWidget = SourcePrefab->LoadPrefabWithExistingObjects(World, World, PrefabScene->GetParentForLoadPrefab(NewAsset), MapGuidToObject, SubPrefabMap);
		FLexUISubPrefabData SubPrefabData;
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
		ULexUIPrefab* NewAsset = NewObject<ULexUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = false;
		NewAsset->PrefabVersion = LEXUI_CURRENT_PREFAB_VERSION;
		auto HelperObject = NewAsset->GetPrefabHelperObject();
		HelperObject->PrefabAsset = NewAsset;
		auto PrefabScene = NewAsset->GetPrefabInstanceScene();
		auto World = PrefabScene->GetWorld();
		HelperObject->LoadedRootWidget = NewObject<ULexWidget>(World);
		HelperObject->LoadedRootWidget->SetParent(PrefabScene->GetParentForLoadPrefab(NewAsset));
		HelperObject->LoadedRootWidget->SetDisplayName(NewAsset->GetName());
		if (RootLayoutClass != nullptr)
		{
			check(RootLayoutClass->IsChildOf(ULexLayoutContainer::StaticClass()));
			check(!RootLayoutClass->HasAnyClassFlags(CLASS_Abstract));
			HelperObject->LoadedRootWidget->CreateNewLayoutContainer(RootLayoutClass.Get());
		}
		HelperObject->SavePrefab();
		return NewAsset;
	}
}

#undef LOCTEXT_NAMESPACE
