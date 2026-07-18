// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabBehaviourUtils.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace LexUIPrefabBehaviourUtils
{

FString GetCompanionBlueprintName(ULexUIPrefab* InPrefab)
{
	return InPrefab != nullptr ? FString::Printf(TEXT("BP_%s"), *InPrefab->GetName()) : FString();
}

ULexUIBehaviour* FindBehaviourComponent(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab)
{
	if (InRootWidget == nullptr || InPrefab == nullptr)return nullptr;
	// the companion is the blueprint ULexUIBehaviour on the root widget whose blueprint asset
	// name matches the convention -- name-matching (not "first blueprint behaviour") so a
	// reusable behaviour attached to the root isn't mistaken for the companion
	const FString CompanionName = GetCompanionBlueprintName(InPrefab);
	for (ULexUIBehaviour* Comp : InRootWidget->GetAllComponents())
	{
		if (Comp == nullptr)continue;
		if (auto BP = Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy))
		{
			if (BP->GetName() == CompanionName)
			{
				return Comp;
			}
		}
	}
	return nullptr;
}

UBlueprint* FindBehaviourBlueprint(ULexWidget* InRootWidget, ULexUIPrefab* InPrefab)
{
	if (auto Comp = FindBehaviourComponent(InRootWidget, InPrefab))
	{
		return Cast<UBlueprint>(Comp->GetClass()->ClassGeneratedBy);
	}
	return nullptr;
}

UBlueprint* CreateBehaviourBlueprint(ULexUIPrefab* InPrefab, ULexWidget* InRootWidget)
{
	if (InPrefab == nullptr || InRootWidget == nullptr)return nullptr;

	const FString PackagePath = FPackageName::GetLongPackagePath(InPrefab->GetOutermost()->GetName());
	const FString BaseName = GetCompanionBlueprintName(InPrefab);

	// re-attach an orphaned companion asset (created before, component lost without saving)
	// instead of minting BP_<PrefabName>1
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(PackagePath / BaseName + TEXT(".") + BaseName));
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr
		|| !Blueprint->GeneratedClass->IsChildOf(ULexUIBehaviour::StaticClass()))
	{
		FString PackageName, AssetName;
		FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(PackagePath / BaseName, TEXT(""), PackageName, AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)return nullptr;
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ULexUIBehaviour::StaticClass(), Package, *AssetName
			, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint == nullptr)return nullptr;
		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();
	}

	// attach the logic host to the root widget; LexUI's AddComponent registers it in the
	// widget's Components array, which the prefab serializes
	InRootWidget->Modify();
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	InRootWidget->AddComponent(GeneratedClass);
	return Blueprint;
}

}//namespace LexUIPrefabBehaviourUtils
