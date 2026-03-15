// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexUIPrefabScene.h"

#if WITH_EDITOR

class UStaticMeshComponent;
class ULexUIPrefab;
class AActor;

//Encapsulates a simple scene setup for Prefab Editor.
class LGUI_API FLexUIPrefabInstanceScene : public FLexUIPrefabScene
{
public:
	FLexUIPrefabInstanceScene(ConstructionValues CVS);
	
	static const FString RootAgentActorName;
	USceneComponent* GetParentComponentForPrefab(ULexUIPrefab* InPrefab);
	AActor* GetRootAgentActor()const { return RootAgentActor; }
	void SetSkyCubeVisibility(bool bVisible);
private:

	AActor* RootAgentActor = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
