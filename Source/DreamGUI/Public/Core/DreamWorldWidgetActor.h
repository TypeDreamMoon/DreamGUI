// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamWorldWidgetActor.generated.h"

/**
 * An actor that is nothing but a world-space DreamUI hierarchy: what dragging a hierarchy class into
 * a level produces.
 *
 * C++ rather than a Blueprint in the plugin's content, which is what this replaces. A root asset
 * could not be changed without re-saving content every project had already placed, there was one per
 * renderer -- so switching renderer meant swapping the actor's class -- and the component it carried
 * was reachable only through that asset.
 */
UCLASS(ClassGroup = (DreamGUI), DisplayName = "DreamUI World Widget Actor",
	HideCategories = (Rendering, Replication, Collision, HLOD, Physics, Networking, Input, Actor, Navigation, LevelInstance, Cooking))
class DREAMGUI_API ADreamWorldWidgetActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamWorldWidgetActor();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	UDreamWorldWidgetComponent* GetWidgetComponent() const { return WidgetComponent; }

protected:
	/** The root component: everything this actor is. */
	UPROPERTY(Category = "DreamGUI", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamWorldWidgetComponent> WidgetComponent;
};
