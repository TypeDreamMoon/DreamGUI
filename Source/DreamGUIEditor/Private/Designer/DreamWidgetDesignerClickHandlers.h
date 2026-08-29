// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "HitProxies.h"

//this file mostly reference from "UnrealEd/public/LevelViewportClickHandlers.h"

class AActor;
class ABrush;
class FDreamWidgetDesignerViewportClient;
class UModel;
struct FTypedElementHandle;
struct FViewportClick;
struct HActor;

namespace DreamWidgetDesignerClickHandlers
{
	bool ClickViewport(FDreamWidgetDesignerViewportClient* ViewportClient, const FViewportClick& Click);

	bool ClickElement(FDreamWidgetDesignerViewportClient* ViewportClient, const FTypedElementHandle& HitElement, const FViewportClick& Click);

	bool ClickActor(FDreamWidgetDesignerViewportClient* ViewportClient,AActor* Actor,const FViewportClick& Click,bool bAllowSelectionChange);

	bool ClickComponent(FDreamWidgetDesignerViewportClient* ViewportClient, HActor* ActorHitProxy, const FViewportClick& Click);

	void ClickBrushVertex(FDreamWidgetDesignerViewportClient* ViewportClient,ABrush* InBrush,FVector* InVertex,const FViewportClick& Click);

	void ClickStaticMeshVertex(FDreamWidgetDesignerViewportClient* ViewportClient,AActor* InActor,FVector& InVertex,const FViewportClick& Click);
	
	void ClickSurface(FDreamWidgetDesignerViewportClient* ViewportClient, UModel* Model, int32 iSurf, const FViewportClick& Click);

	void ClickBackdrop(FDreamWidgetDesignerViewportClient* ViewportClient,const FViewportClick& Click);

	void ClickLevelSocket(FDreamWidgetDesignerViewportClient* ViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);
};


