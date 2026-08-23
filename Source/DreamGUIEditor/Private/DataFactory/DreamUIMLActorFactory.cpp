// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DataFactory/DreamUIMLActorFactory.h"
#include "Core/DreamGUISettings.h"
#include "AssetRegistry/AssetData.h"
#include "Core/DreamUIManager.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "XMLSupport/DreamUIMLBehaviour.h"
#include "XMLSupport/DreamUIMLPresenterComponent.h"


#define LOCTEXT_NAMESPACE "DreamUIMLActorFactory"


UDreamUIMLActorFactory::UDreamUIMLActorFactory()
{
	DisplayName = LOCTEXT("DreamUIMLDisplayName", "DreamUIML");
	bShowInEditorQuickMenu = false;
	bUseSurfaceOrientation = false;
}

bool UDreamUIMLActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!AssetData.IsValid())return false;
	auto Asset = AssetData.GetAsset();
	auto Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UDreamUIMLBehaviour::StaticClass()))
	{
		return true;
	}

	return false;
}

bool UDreamUIMLActorFactory::PreSpawnActor(UObject* Asset, FTransform& InOutLocation)
{
	UDreamUIMLPresenterComponent::MarkNeedCheckNecessaryObjects();
	auto Blueprint = Cast<UBlueprint>(Asset);
	if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UDreamUIMLBehaviour::StaticClass()))
	{
		return true;
	}
	return false;
}

AActor* UDreamUIMLActorFactory::SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform,
	const FActorSpawnParameters& InSpawnParams)
{
	auto Actor = Super::SpawnActor(InAsset, InLevel, InTransform, InSpawnParams);
	auto UIMLPresenterComponent = Actor->FindComponentByClass<UDreamUIMLPresenterComponent>();
	if (!UIMLPresenterComponent)
	{
		UIMLPresenterComponent = NewObject<UDreamUIMLPresenterComponent>(Actor, UDreamUIMLPresenterComponent::StaticClass());
		Actor->SetRootComponent(UIMLPresenterComponent);
		UIMLPresenterComponent->RegisterComponent();
		Actor->AddInstanceComponent(UIMLPresenterComponent);
	}
	UIMLPresenterComponent->bIsSpawnFromFactory = true;
	return Actor;
}

void UDreamUIMLActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	auto Blueprint = Cast<UBlueprint>(Asset);
	auto DreamUIMLClass = Cast<UClass>(Blueprint->GeneratedClass);
	
	auto WidgetPresenterComponent = InNewActor->FindComponentByClass<UDreamUIMLPresenterComponent>();
	WidgetPresenterComponent->SetScriptClass(DreamUIMLClass);
	
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

void UDreamUIMLActorFactory::PostPlaceAsset(TArrayView<const FTypedElementHandle> InHandle,
	const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions)
{
	Super::PostPlaceAsset(InHandle, InPlacementInfo, InPlacementOptions);
}

UObject* UDreamUIMLActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	auto WidgetPresenterComponent = ActorInstance->FindComponentByClass<UDreamUIMLPresenterComponent>();
	check(WidgetPresenterComponent);
	return WidgetPresenterComponent->GetScriptClass();
}

UClass* UDreamUIMLActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	auto Asset = AssetData.GetAsset();
	if (!Asset)return nullptr;
	auto Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)return nullptr;
	auto DreamUIMLClass = Cast<UClass>(Blueprint->GeneratedClass);
	if (!DreamUIMLClass)return nullptr;
	if (!DreamUIMLClass->IsChildOf(UDreamUIMLBehaviour::StaticClass()))return nullptr;
	auto DreamUIML = GetDefault<UDreamUIMLBehaviour>(DreamUIMLClass);
	if (DreamUIML)
	{
		const auto RenderMode = DreamUIML->DefaultRenderMode;
		NewActorClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->GetRootClassForRenderMode(RenderMode, true),
			TEXT("markup root actor class for this render mode"));
		if (!NewActorClass)
		{
			NewActorClass = AActor::StaticClass();
		}
		return NewActorClass;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
