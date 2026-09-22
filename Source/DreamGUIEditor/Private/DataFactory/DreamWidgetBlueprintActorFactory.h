// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "ActorFactories/ActorFactory.h"
#include "DreamWidgetBlueprintActorFactory.generated.h"

/**
 * Dragging a DreamUI widget Blueprint into a level: spawn ADreamWorldWidgetActor and hand the
 * Blueprint's generated class to its world widget component.
 *
 * The actor is C++ now. It used to be whichever Blueprint the project settings named as the
 * world-space root, which meant the drop gesture depended on a content asset that had to ship, be
 * found by soft path, and carry the right components in its construction script -- and the render
 * mode was baked into the choice of asset, so switching renderers meant respawning. One actor with
 * one component settles all of that: the renderer is a property on the component.
 */
UCLASS()
class UDreamWidgetBlueprintActorFactory : public UActorFactory
{
	GENERATED_BODY()
public:
	UDreamWidgetBlueprintActorFactory();
	//~ Begin UActorFactory
	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
	virtual UClass* GetDefaultActorClass(const FAssetData& AssetData) override;
	//~ End UActorFactory
};
