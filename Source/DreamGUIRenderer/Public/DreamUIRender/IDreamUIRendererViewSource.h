// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What the renderer asks a screen-space root for when it sets up a view: where the UI is looked at from, and
 * how. The root canvas answers; the renderer only knows it through this, so it never names the canvas class,
 * which is the core's.
 *
 * Asked on the game thread, from FDreamUIRenderer::SetupView, of the first registered root that is still
 * alive -- the renderer keeps a weak reference to the object beside this pointer and never calls through it
 * once that reference has gone.
 */
class IDreamUIRendererViewSource
{
public:
	virtual ~IDreamUIRendererViewSource() = default;

	virtual FVector GetRendererViewLocation() const = 0;
	virtual FRotator GetRendererViewRotator() const = 0;
	virtual FMatrix GetRendererProjectionMatrix() const = 0;
	virtual bool GetRendererEnableDepthTest() const = 0;
	/** Fraction of the viewport the screen-space UI is drawn at; 1 is full resolution. */
	virtual float GetRendererScreenSpaceRenderScale() const = 0;
	/**
	 * The one view this root is drawn in, as FSceneView::PlayerIndex names a view (ULocalPlayer::CalcSceneView sets it
	 * to the local player's controller id), when the root fills that player's part of a split screen; INDEX_NONE when
	 * it fills the whole viewport, which every view draws through the shared view -- the only answer there was before.
	 */
	virtual int32 GetRendererViewPlayerIndex() const { return INDEX_NONE; }
};
