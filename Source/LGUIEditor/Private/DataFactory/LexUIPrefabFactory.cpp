// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIPrefabFactory.h"

#include "Core/Components/LexWidget.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabFactory"


ULexUIPrefabFactory::ULexUIPrefabFactory()
{
	SupportedClass = ULexUIPrefab::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* ULexUIPrefabFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (SourcePrefab != nullptr)//prefab variant
	{
		ULexUIPrefab* NewAsset = NewObject<ULexUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = true;
		ULexUIPrefabHelperObject* HelperObject = NewObject<ULexUIPrefabHelperObject>(GetTransientPackage());
		HelperObject->PrefabAsset = NewAsset;
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObject;
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
		auto World = SourcePrefab->GetPrefabInstanceScene()->GetWorld();
		HelperObject->LoadedRootWidget = SourcePrefab->LoadPrefabWithExistingObjects(World, World, nullptr, MapGuidToObject, SubPrefabMap);
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
		
		HelperObject->LoadedRootWidget->DestroyWidget();
		HelperObject->ConditionalBeginDestroy();
		return NewAsset;
	}
	else
	{
		ULexUIPrefab* NewAsset = NewObject<ULexUIPrefab>(InParent, Class, Name, Flags | RF_Transactional);
		NewAsset->bIsPrefabVariant = false;
		ULexUIPrefabHelperObject* HelperObject = NewObject<ULexUIPrefabHelperObject>(GetTransientPackage());
		HelperObject->PrefabAsset = NewAsset;
		HelperObject->LoadedRootWidget = NewObject<ULexWidget>();
		HelperObject->LoadedRootWidget->SetDisplayName(NewAsset->GetName());
		HelperObject->SavePrefab();
		
		HelperObject->LoadedRootWidget->DestroyWidget();
		HelperObject->ConditionalBeginDestroy();
		return NewAsset;
	}
}

#undef LOCTEXT_NAMESPACE
