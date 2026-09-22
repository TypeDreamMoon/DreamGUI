// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DataFactory/DreamWidgetBlueprintActorFactory.h"

#include "AssetRegistry/AssetData.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamWidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "DreamWidgetBlueprintActorFactory"

UDreamWidgetBlueprintActorFactory::UDreamWidgetBlueprintActorFactory()
{
	DisplayName = LOCTEXT("DisplayName", "DreamUI Widget");
	// In the Place Actors panel as well as on the drop gesture: the actor is a plain C++ class with
	// an empty WidgetClass, which is a usable starting point -- pick the widget in the Details panel.
	// While the root was a settings-named Blueprint there was nothing sensible to offer here.
	bShowInEditorQuickMenu = true;
	bUseSurfaceOrientation = false;
	NewActorClass = ADreamWorldWidgetActor::StaticClass();
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

void UDreamWidgetBlueprintActorFactory::PostSpawnActor(UObject* Asset, AActor* InNewActor)
{
	Super::PostSpawnActor(Asset, InNewActor);

	UDreamWidgetBlueprint* Blueprint = CastChecked<UDreamWidgetBlueprint>(Asset);
	ADreamWorldWidgetActor* WidgetActor = Cast<ADreamWorldWidgetActor>(InNewActor);
	if (WidgetActor == nullptr || WidgetActor->GetWidgetComponent() == nullptr)
	{
		return;
	}
	WidgetActor->GetWidgetComponent()->SetWidgetClass(Blueprint->GeneratedClass.Get());
}

UObject* UDreamWidgetBlueprintActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	const UDreamWorldWidgetComponent* WidgetComponent = ActorInstance->FindComponentByClass<UDreamWorldWidgetComponent>();
	if (WidgetComponent == nullptr)
	{
		return nullptr;
	}
	UClass* WidgetClass = WidgetComponent->GetWidgetClass();
	return WidgetClass != nullptr ? WidgetClass->ClassGeneratedBy : nullptr;
}

UClass* UDreamWidgetBlueprintActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
	// A drop into a 3D level means world space; the screen-space path is AddWidgetOfClassToViewport.
	// One class for both renderers -- the component's Backend property is what picks between them.
	return ADreamWorldWidgetActor::StaticClass();
}

#undef LOCTEXT_NAMESPACE
