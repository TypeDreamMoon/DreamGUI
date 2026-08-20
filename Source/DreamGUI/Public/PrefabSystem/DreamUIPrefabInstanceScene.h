// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIPrefabScene.h"
#include "Core/Components/DreamWidget.h"

#if WITH_EDITOR

class UStaticMeshComponent;
class UDreamUIPrefab;
class AActor;

//Encapsulates a simple scene setup for Prefab Editor.
class DREAMGUI_API FDreamUIPrefabInstanceScene : public FDreamUIPrefabScene
{
public:
	FDreamUIPrefabInstanceScene(ConstructionValues CVS);
	~FDreamUIPrefabInstanceScene();
	
	static const FString RootAgentActorName;
	UDreamWidget* GetParentForLoadPrefab(UDreamUIPrefab* InPrefab);
	void SetSkyCubeVisibility(bool bVisible);
	UDreamWidget* GetRootAgent()const { return RootAgentWidget.Get(); }
private:
	TStrongObjectPtr<UDreamWidget> RootAgentWidget = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
