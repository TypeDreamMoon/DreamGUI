// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamRetainerBox.generated.h"

class UDreamCanvas;
class UDreamCanvasRenderTargetPreviewer;
class UMaterialInterface;

/**
 * Renders the widget's subtree into a texture and re-uses that texture on the frames in between.
 *
 * The same idea as UMG's URetainerBox, and deliberately the same vocabulary: Phase / PhaseCount to
 * spread re-renders across frames, RequestRender() to force one, SetRetainRendering(false) to fall
 * back to drawing live, an effect material applied to the retained texture.
 *
 * What it is built on, rather than beside: a DreamGUI canvas can already render to its own target
 * (bForceRenderToTarget) and already knows how to be asked for exactly one update
 * (RenderTargetUpdateMode::WhenRequest + RequestUpdateForRenderTarget). This behaviour is the policy
 * on top -- when to ask -- plus the group compositing below. It requires a UDreamCanvas on the same
 * widget and configures it; it does not invent a second retained-rendering path.
 *
 * Two consequences worth knowing:
 *
 * - The retained subtree is its own draw-call boundary. PrepareDrawCallBatchingData skips a child
 *   canvas with bForceRenderToTarget, so nothing inside a retainer batches with anything outside it.
 *   That is the point (its vertices are not re-assembled at all on a retained frame), not a cost.
 *
 * - GroupRenderOpacity is what per-widget RenderOpacity cannot be. Fading a subtree with
 *   RenderOpacity multiplies every element's alpha individually, so overlapping children show
 *   through each other on the way out. Retained, the subtree is composited first and the group alpha
 *   is applied once to the result -- exactly why UMG tells you to reach for a RetainerBox to fade a
 *   group.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Retainer Box")
class DREAMGUI_API UDreamRetainerBox : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	/**
	 * Whether this frame is one the retained texture is redrawn on. The whole policy, as arithmetic.
	 *
	 * Not retaining means every frame is a render frame -- the subtree draws live. Otherwise an
	 * explicit request always wins, and beyond that a frame qualifies when it is this retainer's turn
	 * in the phase cycle: PhaseCount frames, this one taking the Phase'th. Spreading several retainers
	 * across different phases is how a screen full of them avoids redrawing them all on one frame.
	 *
	 * Static and parameterised so the policy can be checked without a world, a canvas or an RHI --
	 * the rest of this class is wiring.
	 */
	static bool ShouldRenderThisFrame(bool bInRetainRendering, int32 InPhase, int32 InPhaseCount, bool bInRenderRequested, uint64 InFrameNumber);

	/** Redraw the retained texture on the next update, whatever the phase says. Mirrors URetainerBox::RequestRender. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void RequestRender();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetRetainRendering()const { return bRetainRendering; }
	/** false draws the subtree live every frame, as if this behaviour were not here. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRetainRendering(bool Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int32 GetPhase()const { return Phase; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int32 GetPhaseCount()const { return PhaseCount; }
	/** Which frame of the cycle this retainer redraws on. Clamped into [0, PhaseCount). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetRenderingPhase(int32 InPhase, int32 InPhaseCount);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetGroupRenderOpacity()const { return GroupRenderOpacity; }
	/** Opacity applied once to the composited subtree, not to each element in it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetGroupRenderOpacity(float Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UMaterialInterface* GetEffectMaterial()const { return EffectMaterial; }
	/** Material the retained texture is drawn through; the texture arrives in TextureParameterName. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEffectMaterial(UMaterialInterface* Value);

	/** The canvas this retainer drives, if the widget has one. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamCanvas* GetRetainedCanvas()const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bRetainRendering = true;
	/** Which frame of the cycle this retainer redraws on. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0"))
	int32 Phase = 0;
	/** Length of the cycle, in frames. 1 redraws every frame. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "1"))
	int32 PhaseCount = 1;
	/** Applied once to the composited subtree. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroupRenderOpacity = 1.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TObjectPtr<UMaterialInterface> EffectMaterial = nullptr;
	/** Texture parameter on EffectMaterial the retained texture is bound to. UMG calls this the same thing. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FName TextureParameterName = TEXT("Texture");
	/**
	 * The element that draws the retained texture.
	 *
	 * It has to live OUTSIDE the retained subtree -- usually on a sibling widget -- because everything
	 * inside the subtree is what is being drawn INTO the texture. That is a property rather than
	 * something conjured here: a retainer that built its own display element would have to invent a
	 * widget and a place in the hierarchy for it, and where that element sits is a layout decision.
	 * Leave it empty and the retaining still happens; nothing shows it, and a warning says so once.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TWeakObjectPtr<UDreamCanvasRenderTargetPreviewer> DisplayVisual = nullptr;

private:
	/** Set by RequestRender, consumed by the next update. */
	bool bRenderRequested = true;
	/** What the canvas was set to before this behaviour took it over, so OnUnregister can put it back. */
	bool bHasSavedCanvasState = false;
	bool bSavedForceRenderToTarget = false;
	uint8 bSavedRenderTargetUpdateMode = 0;

	void ApplyCanvasConfiguration();
	void RestoreCanvasConfiguration();
	/** Pushes GroupRenderOpacity and EffectMaterial onto whatever displays the retained texture. */
	void ApplyGroupCompositing();
};

/**
 * Holds a subtree's draw-call list until something says otherwise.
 *
 * UMG's UInvalidationBox caches Slate draw elements for a subtree and rebuilds them on
 * InvalidateCache(). DreamGUI's equivalent of "the cached draw elements" is the canvas's draw-call
 * list -- which geometry was batched with which, in what order, into which sections -- so that is
 * what this holds.
 *
 * It does NOT freeze the picture: the cheap per-frame vertex refresh still runs, so widgets that only
 * move, recolour or animate keep updating. What is deferred is the re-batching, which is the
 * expensive half and the half a structurally static subtree does not need. A rebuild asked for while
 * cached is remembered, not dropped, and happens as soon as the cache is invalidated.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Invalidation Box")
class DREAMGUI_API UDreamInvalidationBox : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	/**
	 * Whether the canvas may rebuild its draw-call list this frame.
	 *
	 * Caching off means always. Caching on means only when the cache has been invalidated since the
	 * last rebuild. Static for the same reason as UDreamRetainerBox::ShouldRenderThisFrame.
	 */
	static bool ShouldRebuildDrawCall(bool bInCanCache, bool bInCacheInvalidated);

	/** Rebuild the subtree's draw-calls at the next update. Mirrors UInvalidationBox::InvalidateCache. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void InvalidateCache();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetCanCache()const { return bCanCache; }
	/** false is a plain pass-through: the canvas rebuilds whenever it goes dirty, as it always did. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCanCache(bool Value);

	/** The canvas this box caches, if the widget has one. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamCanvas* GetCachedCanvas()const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bCanCache = true;

private:
	/** Starts true so the first frame always builds; cleared once the rebuild has been let through. */
	bool bCacheInvalidated = true;
	void ApplySuspendState();
};
