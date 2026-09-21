// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "ActorFactories/ActorFactory.h"
#include "DreamWidgetBlueprintActorFactory.generated.h"

/**
 * Dragging a DreamUI widget Blueprint into a level: spawn the world-space root actor and hand the
 * Blueprint's generated class to its presenter component.
 *
 * The prefab-era factory did exactly this and was deleted with the prefab machinery (P4-a), which
 * silently deleted the drop-into-level gesture with it -- the presenter component it drove was
 * kept. This is that factory re-aimed at the class model: same actor shape, same presenter, the
 * asset is now the widget Blueprint itself.
 */
UCLASS()
class UDreamWidgetBlueprintActorFactory : public UActorFactory
{
	GENERATED_BODY()
public:
	UDreamWidgetBlueprintActorFactory();
	//~ Begin UActorFactory
	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual AActor* SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform, const FActorSpawnParameters& InSpawnParams) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
	virtual UClass* GetDefaultActorClass(const FAssetData& AssetData) override;
	//~ End UActorFactory
};
