// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "HitProxies.h"

//this file mostly reference from "UnrealEd/public/LevelViewportClickHandlers.h"

class AActor;
class ABrush;
class FLexUIPrefabEditorViewportClient;
class UModel;
struct FTypedElementHandle;
struct FViewportClick;
struct HActor;

namespace LexUIPrefabViewportClickHandlers
{
	bool ClickViewport(FLexUIPrefabEditorViewportClient* ViewportClient, const FViewportClick& Click);

	bool ClickElement(FLexUIPrefabEditorViewportClient* ViewportClient, const FTypedElementHandle& HitElement, const FViewportClick& Click);

	bool ClickActor(FLexUIPrefabEditorViewportClient* ViewportClient,AActor* Actor,const FViewportClick& Click,bool bAllowSelectionChange);

	bool ClickComponent(FLexUIPrefabEditorViewportClient* ViewportClient, HActor* ActorHitProxy, const FViewportClick& Click);

	void ClickBrushVertex(FLexUIPrefabEditorViewportClient* ViewportClient,ABrush* InBrush,FVector* InVertex,const FViewportClick& Click);

	void ClickStaticMeshVertex(FLexUIPrefabEditorViewportClient* ViewportClient,AActor* InActor,FVector& InVertex,const FViewportClick& Click);
	
	void ClickSurface(FLexUIPrefabEditorViewportClient* ViewportClient, UModel* Model, int32 iSurf, const FViewportClick& Click);

	void ClickBackdrop(FLexUIPrefabEditorViewportClient* ViewportClient,const FViewportClick& Click);

	void ClickLevelSocket(FLexUIPrefabEditorViewportClient* ViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);
};


