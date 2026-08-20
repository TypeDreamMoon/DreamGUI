// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIPrefabActorFactory.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "AssetRegistry/AssetData.h"
#include "Core/DreamUIManager.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "PrefabSystem/DreamUIPrefabPresenterComponent.h"


#define LOCTEXT_NAMESPACE "DreamUIPrefabActorFactory"


UDreamUIPrefabActorFactory::UDreamUIPrefabActorFactory()
{
	DisplayName = LOCTEXT("PrefabDisplayName", "Prefab");
	bShowInEditorQuickMenu = false;
	bUseSurfaceOrientation = false;
}

bool UDreamUIPrefabActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid() && AssetData.GetClass()->IsChildOf(UDreamUIPrefab::StaticClass()))
	{
		return true;
	}

	return false;
}

bool UDreamUIPrefabActorFactory::PreSpawnActor(UObject* Asset, FTransform& InOutLocation)
{
	UDreamUIPrefabPresenterComponent::MarkNeedCheckNecessaryObjects();
	auto Prefab = CastChecked<UDreamUIPrefab>(Asset);

	if (Prefab == NULL)
	{
		return false;
	}
	return true;
}

AActor* UDreamUIPrefabActorFactory::SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform,
	const FActorSpawnParameters& InSpawnParams)
{
	auto Actor = Super::SpawnActor(InAsset, InLevel, InTransform, InSpawnParams);
	auto WidgetPresenterComponent = Actor->FindComponentByClass<UDreamUIPrefabPresenterComponent>();
	if (!WidgetPresenterComponent)
	{
		WidgetPresenterComponent = NewObject<UDreamUIPrefabPresenterComponent>(Actor, UDreamUIPrefabPresenterComponent::StaticClass());
		Actor->SetRootComponent(WidgetPresenterComponent);
		WidgetPresenterComponent->RegisterComponent();
		Actor->AddInstanceComponent(WidgetPresenterComponent);
	}
	WidgetPresenterComponent->bIsSpawnFromFactory = true;
	return Actor;
}

void UDreamUIPrefabActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	auto Prefab = CastChecked<UDreamUIPrefab>(Asset);

	auto WidgetPresenterComponent = InNewActor->FindComponentByClass<UDreamUIPrefabPresenterComponent>();
	// WidgetPresenterComponent->GetLoadedWidget()->SetSizeDelta(Prefab->PrefabDataForPrefabEditor.CanvasSize);
	WidgetPresenterComponent->SetPrefab(Prefab);

	auto World = InNewActor->GetWorld();
	if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
	{
		UDreamUIManagerObject::AddOneShotTickFunction([WeakObject = MakeWeakObjectPtr(WidgetPresenterComponent)]()
		{
			if (WeakObject.IsValid())
			{
				WeakObject->CheckNecessaryObjects();
			}
		}, 1);
	}
}

void UDreamUIPrefabActorFactory::PostPlaceAsset(TArrayView<const FTypedElementHandle> InHandle,
	const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions)
{
	Super::PostPlaceAsset(InHandle, InPlacementInfo, InPlacementOptions);
}

UObject* UDreamUIPrefabActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	auto WidgetPresenterComponent = ActorInstance->FindComponentByClass<UDreamUIPrefabPresenterComponent>();
	check(WidgetPresenterComponent);
	return WidgetPresenterComponent->GetPrefab();
}

UClass* UDreamUIPrefabActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	if (auto Prefab = Cast<UDreamUIPrefab>(AssetData.GetAsset()))
	{
		FString ClassName;
		auto RenderMode = (EDreamRenderMode)Prefab->PrefabDataForPrefabEditor.CanvasRenderMode;
		switch (RenderMode)
		{
		case EDreamRenderMode::WorldSpace:
			ClassName = TEXT("WorldSpaceRoot_UERenderer");
			break;
		case EDreamRenderMode::WorldSpace_DreamUI:
			ClassName = TEXT("WorldSpaceRoot_DreamRenderer");
			break;
		case EDreamRenderMode::ScreenSpaceOverlay:
			ClassName = TEXT("ScreenSpaceRoot");
		}
		
		NewActorClass = LoadClass<AActor>(NULL, *FString::Printf(TEXT("/DreamGUI/Blueprints/%s.%s_C"), *ClassName, *ClassName));
		if (!NewActorClass)
		{
			NewActorClass = AActor::StaticClass();
		}
		return NewActorClass;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
