// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWorldWidgetActor.h"

#include "Core/DreamWorldWidgetComponent.h"

ADreamWorldWidgetActor::ADreamWorldWidgetActor()
{
	// Nothing here ticks: the hierarchy is driven by the UI manager, and the tree follows this actor
	// through the canvas's binding on the component's TransformUpdated.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	WidgetComponent = CreateDefaultSubobject<UDreamWorldWidgetComponent>(TEXT("WidgetComponent"));
	RootComponent = WidgetComponent;
}
