// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DataFactory/DreamWidgetBlueprintActorFactory.h"

#include "AssetRegistry/AssetData.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPresenterComponent.h"
#include "DreamWidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "DreamWidgetBlueprintActorFactory"

UDreamWidgetBlueprintActorFactory::UDreamWidgetBlueprintActorFactory()
{
	DisplayName = LOCTEXT("DisplayName", "DreamUI Widget");
	bShowInEditorQuickMenu = false;
	bUseSurfaceOrientation = false;
}

bool UDreamWidgetBlueprintActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid() && AssetData.GetClass() != nullptr
		&& AssetData.GetClass()->IsChildOf(UDreamWidgetBlueprint::StaticClass()))
	{
		return true;
	}
	return false;
}

AActor* UDreamWidgetBlueprintActorFactory::SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform,
	const FActorSpawnParameters& InSpawnParams)
{
	UDreamWidgetPresenterComponentBase::MarkNeedCheckNecessaryObjects();
	AActor* Actor = Super::SpawnActor(InAsset, InLevel, InTransform, InSpawnParams);
	if (Actor == nullptr)
	{
		return nullptr;
	}
	// The settings' root actor usually carries a presenter already; a bare AActor fallback gets one
	// grafted so the drop still produces a working host.
	UDreamWidgetPresenterComponent* Presenter = Actor->FindComponentByClass<UDreamWidgetPresenterComponent>();
	if (Presenter == nullptr)
	{
		Presenter = NewObject<UDreamWidgetPresenterComponent>(Actor, UDreamWidgetPresenterComponent::StaticClass());
		Actor->SetRootComponent(Presenter);
		Presenter->RegisterComponent();
		Actor->AddInstanceComponent(Presenter);
	}
	Presenter->bIsSpawnFromFactory = true;
	return Actor;
}

void UDreamWidgetBlueprintActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	UDreamWidgetBlueprint* Blueprint = CastChecked<UDreamWidgetBlueprint>(Asset);
	UDreamWidgetPresenterComponent* Presenter = InNewActor->FindComponentByClass<UDreamWidgetPresenterComponent>();
	if (Presenter == nullptr)
	{
		return;
	}
	Presenter->SetWidgetClass(Blueprint->GeneratedClass.Get());

	UWorld* World = InNewActor->GetWorld();
	if (World != nullptr && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())
	{
		// Deferred a frame, exactly as the prefab factory did: the drop runs mid-placement, and the
		// event-system / raycaster check wants a settled world to look at.
		UDreamUIManagerObject::AddOneShotTickFunction([WeakPresenter = MakeWeakObjectPtr(Presenter)]()
		{
			if (WeakPresenter.IsValid())
			{
				WeakPresenter->CheckNecessaryObjects();
			}
		}, 1);
	}
}

UObject* UDreamWidgetBlueprintActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	UDreamWidgetPresenterComponent* Presenter = ActorInstance->FindComponentByClass<UDreamWidgetPresenterComponent>();
	if (Presenter == nullptr)
	{
		return nullptr;
	}
	UClass* WidgetClass = Presenter->GetWidgetClass();
	return WidgetClass != nullptr ? WidgetClass->ClassGeneratedBy : nullptr;
}

UClass* UDreamWidgetBlueprintActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	// A drop into a 3D level means world space; the screen-space path is AddWidgetOfClassToViewport.
	NewActorClass = UDreamGUISettings::LoadSettingClass(
		UDreamGUISettings::Get()->GetRootClassForRenderMode(EDreamRenderMode::WorldSpace_DreamUI),
		TEXT("world-space root actor class"));
	if (NewActorClass == nullptr)
	{
		NewActorClass = AActor::StaticClass();
	}
	return NewActorClass;
}

#undef LOCTEXT_NAMESPACE
