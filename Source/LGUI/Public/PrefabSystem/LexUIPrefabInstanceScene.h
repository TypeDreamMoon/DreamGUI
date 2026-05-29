// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexUIPrefabScene.h"
#include "Core/Components/LexWidget.h"

#if WITH_EDITOR

class UStaticMeshComponent;
class ULexUIPrefab;
class AActor;

//Encapsulates a simple scene setup for Prefab Editor.
class LGUI_API FLexUIPrefabInstanceScene : public FLexUIPrefabScene
{
public:
	FLexUIPrefabInstanceScene(ConstructionValues CVS);
	~FLexUIPrefabInstanceScene();
	
	static const FString RootAgentActorName;
	ULexWidget* GetParentForLoadPrefab(ULexUIPrefab* InPrefab);
	void SetSkyCubeVisibility(bool bVisible);
	ULexWidget* GetRootAgent()const { return RootAgentWidget.Get(); }
private:
	TStrongObjectPtr<ULexWidget> RootAgentWidget = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
