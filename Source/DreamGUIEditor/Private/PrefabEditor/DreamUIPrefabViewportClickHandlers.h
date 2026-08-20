// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "HitProxies.h"

//this file mostly reference from "UnrealEd/public/LevelViewportClickHandlers.h"

class AActor;
class ABrush;
class FDreamUIPrefabEditorViewportClient;
class UModel;
struct FTypedElementHandle;
struct FViewportClick;
struct HActor;

namespace DreamUIPrefabViewportClickHandlers
{
	bool ClickViewport(FDreamUIPrefabEditorViewportClient* ViewportClient, const FViewportClick& Click);

	bool ClickElement(FDreamUIPrefabEditorViewportClient* ViewportClient, const FTypedElementHandle& HitElement, const FViewportClick& Click);

	bool ClickActor(FDreamUIPrefabEditorViewportClient* ViewportClient,AActor* Actor,const FViewportClick& Click,bool bAllowSelectionChange);

	bool ClickComponent(FDreamUIPrefabEditorViewportClient* ViewportClient, HActor* ActorHitProxy, const FViewportClick& Click);

	void ClickBrushVertex(FDreamUIPrefabEditorViewportClient* ViewportClient,ABrush* InBrush,FVector* InVertex,const FViewportClick& Click);

	void ClickStaticMeshVertex(FDreamUIPrefabEditorViewportClient* ViewportClient,AActor* InActor,FVector& InVertex,const FViewportClick& Click);
	
	void ClickSurface(FDreamUIPrefabEditorViewportClient* ViewportClient, UModel* Model, int32 iSurf, const FViewportClick& Click);

	void ClickBackdrop(FDreamUIPrefabEditorViewportClient* ViewportClient,const FViewportClick& Click);

	void ClickLevelSocket(FDreamUIPrefabEditorViewportClient* ViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);
};


