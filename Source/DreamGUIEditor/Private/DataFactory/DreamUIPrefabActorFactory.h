// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "ActorFactories/ActorFactory.h"
#include "DreamUIPrefabActorFactory.generated.h"

/** Create a agent actor and use it to spawn the actual prefab. */
UCLASS()
class UDreamUIPrefabActorFactory : public UActorFactory
{
	GENERATED_BODY()
public:
	UDreamUIPrefabActorFactory();
	//~ Begin UActorFactory
	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual bool PreSpawnActor(UObject* Asset, FTransform& InOutLocation) override;
	virtual AActor* SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform, const FActorSpawnParameters& InSpawnParams) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual void PostPlaceAsset(TArrayView<const FTypedElementHandle> InHandle, const FAssetPlacementInfo& InPlacementInfo, const FPlacementOptions& InPlacementOptions) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
	virtual UClass* GetDefaultActorClass(const FAssetData& AssetData) override;
	//virtual FQuat AlignObjectToSurfaceNormal(const FVector& InSurfaceNormal, const FQuat& ActorRotation) const override;
	//~ End UActorFactory

};

