// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/LexUIPrefabActorFactory.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "LexUIEditorTools.h"
#include "AssetRegistry/AssetData.h"
#include "Core/LexUIManager.h"
#include "Core/Actor/LexWidgetPresenterComponent.h"
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
	ULexWidgetPresenterComponent::MarkNeedCheckNecessaryObjects();
	auto Prefab = CastChecked<ULexUIPrefab>(Asset);

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
	auto WidgetPresenterComponent = NewObject<ULexWidgetPresenterComponent>(Actor, ULexWidgetPresenterComponent::StaticClass());
	WidgetPresenterComponent->RegisterComponent();
	Actor->AddInstanceComponent(WidgetPresenterComponent);
	WidgetPresenterComponent->bIsSpawnFromPrefabFactory = true;
	return Actor;
}

void ULexUIPrefabActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	auto Prefab = CastChecked<ULexUIPrefab>(Asset);

	auto WidgetPresenterComponent = InNewActor->FindComponentByClass<ULexWidgetPresenterComponent>();
	WidgetPresenterComponent->GetRootWidget()->SetSizeDelta(Prefab->PrefabDataForPrefabEditor.CanvasSize);
	WidgetPresenterComponent->SetPrefab(Prefab);

	auto World = InNewActor->GetWorld();
	if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
	{
		ULexUIManagerObject::AddOneShotTickFunction([WeakObject = MakeWeakObjectPtr(WidgetPresenterComponent)]()
		{
			if (WeakObject.IsValid())
			{
				WeakObject->CheckNecessaryObjects();
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
	auto WidgetPresenterComponent = ActorInstance->FindComponentByClass<ULexWidgetPresenterComponent>();
	check(WidgetPresenterComponent);
	return WidgetPresenterComponent->GetPrefab();
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
		
		NewActorClass = LoadClass<AActor>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), *ClassName, *ClassName));
		return NewActorClass;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
