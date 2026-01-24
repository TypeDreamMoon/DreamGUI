// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIPrefabActorFactory.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "LexUIEditorTools.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Core/LexUIManager.h"
#include "Core/Actor/LexWidgetRootActor.h"
#include "Core/Components/LexWidget.h"
#include "Event/LexScreenSpaceRaycaster.h"


#define LOCTEXT_NAMESPACE "LexUIPrefabActorFactory"


ULexUIPrefabActorFactory::ULexUIPrefabActorFactory()
{
	DisplayName = LOCTEXT("PrefabDisplayName", "Prefab");
	bShowInEditorQuickMenu = false;
	bUseSurfaceOrientation = false;
}

bool ULexUIPrefabActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid() && AssetData.GetClass()->IsChildOf(ULexUIPrefab::StaticClass()))
	{
		return true;
	}

	return false;
}

bool ULexUIPrefabActorFactory::PreSpawnActor(UObject* Asset, FTransform& InOutLocation)
{
	ALexWidgetRootActor::MarkNeedCheckNecessaryObjects();
	ULexUIPrefab* Prefab = CastChecked<ULexUIPrefab>(Asset);

	if (Prefab == NULL)
	{
		return false;
	}
	return true;
}

AActor* ULexUIPrefabActorFactory::SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform,
	const FActorSpawnParameters& InSpawnParams)
{
	auto Actor = Super::SpawnActor(InAsset, InLevel, InTransform, InSpawnParams);
	if (auto RootActor = CastChecked<ALexWidgetRootActor>(Actor))
	{
		RootActor->bIsSpawnFromPrefabFactory = true;
	}
	return Actor;
}

void ULexUIPrefabActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	auto Prefab = CastChecked<ULexUIPrefab>(Asset);

	auto PrefabActor = CastChecked<ALexWidgetRootActor>(InNewActor);
	PrefabActor->GetLexWidget()->SetSizeDelta(Prefab->PrefabDataForPrefabEditor.CanvasSize);
	PrefabActor->SetPrefab(Prefab);
	auto SelectedActor = FLexUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor != nullptr && PrefabActor->GetWorld() == SelectedActor->GetWorld())
	{
		FLexUIEditorTools::MakeCurrentLevel(SelectedActor);
		auto ParentComp = SelectedActor->GetRootComponent();
		PrefabActor->GetRootComponent()->AttachToComponent(ParentComp, FAttachmentTransformRules::KeepRelativeTransform);
	}

	auto World = InNewActor->GetWorld();
	if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
	{
		ULexUIManagerObject::AddOneShotTickFunction([WeakPrefabActor = MakeWeakObjectPtr(PrefabActor)]()
		{
			if (WeakPrefabActor.IsValid())
			{
				WeakPrefabActor->CheckNecessaryObjects();
			}
		}, 1);
	}
}

void ULexUIPrefabActorFactory::PostPlaceAsset(TArrayView<const FTypedElementHandle> InHandle,
	const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions)
{
	Super::PostPlaceAsset(InHandle, InPlacementInfo, InPlacementOptions);
}

UObject* ULexUIPrefabActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	check(ActorInstance->IsA(NewActorClass));
	auto PrefabActor = CastChecked<ALexWidgetRootActor>(ActorInstance);
	return PrefabActor->GetPrefab();
}

UClass* ULexUIPrefabActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	if (auto Prefab = Cast<ULexUIPrefab>(AssetData.GetAsset()))
	{
		FString ClassName;
		auto RenderMode = (ELexRenderMode)Prefab->PrefabDataForPrefabEditor.CanvasRenderMode;
		switch (RenderMode)
		{
		case ELexRenderMode::WorldSpace:
			ClassName = TEXT("WorldSpaceRoot_UERenderer");
			break;
		case ELexRenderMode::WorldSpace_LexUI:
			ClassName = TEXT("WorldSpaceRoot_LexRenderer");
			break;
		case ELexRenderMode::ScreenSpaceOverlay:
			ClassName = TEXT("ScreenSpaceRoot");
		}
		
		NewActorClass = LoadClass<ALexWidgetRootActor>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), *ClassName, *ClassName));
		return NewActorClass;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
