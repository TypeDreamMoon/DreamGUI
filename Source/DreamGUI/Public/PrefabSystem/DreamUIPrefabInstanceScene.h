// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIPrefabScene.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

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
	/**
	 * Create (once) the design canvas the authored hierarchy hangs under: a root widget carrying a
	 * UDreamCanvas at InCanvasSize. Idempotent -- a second call returns the agent that already exists,
	 * which is what makes it safe to ask for on every preview rebuild.
	 *
	 * Takes plain values rather than an asset so it serves the widget-blueprint designer as well as
	 * the prefab editor; GetParentForLoadPrefab is the prefab-flavoured caller that works out what
	 * those values are for a prefab.
	 */
	UDreamWidget* EnsureRootAgent(FIntPoint InCanvasSize, EDreamRenderMode InRenderMode, FIntPoint InSizeInEditMode);
	void SetSkyCubeVisibility(bool bVisible);
	UDreamWidget* GetRootAgent()const { return RootAgentWidget.Get(); }
private:
	TStrongObjectPtr<UDreamWidget> RootAgentWidget = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
