// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamRetainerBox.h"

#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "DreamRetainerBox"

bool UDreamRetainerBox::ShouldRenderThisFrame(bool bInRetainRendering, int32 InPhase, int32 InPhaseCount, bool bInRenderRequested, uint64 InFrameNumber)
{
	if (!bInRetainRendering)
	{
		//not retaining: the subtree draws live, so every frame is a render frame
		return true;
	}
	if (bInRenderRequested)
	{
		//an explicit request outranks the phase; that is the whole point of RequestRender
		return true;
	}
	//a phase count of one (or nonsense) means every frame, which is also the safe answer for bad input
	const int32 SafePhaseCount = FMath::Max(InPhaseCount, 1);
	if (SafePhaseCount <= 1)
	{
		return true;
	}
	//wrap rather than clamp, so a phase set past the end still lands on a real frame of the cycle
	//instead of piling every such retainer onto the last one
	const int32 SafePhase = ((InPhase % SafePhaseCount) + SafePhaseCount) % SafePhaseCount;
	return (int32)(InFrameNumber % (uint64)SafePhaseCount) == SafePhase;
}

UDreamCanvas* UDreamRetainerBox::GetRetainedCanvas()const
{
	if (auto Widget = GetWidget())
	{
		return Widget->GetComponent<UDreamCanvas>();
	}
	return nullptr;
}

void UDreamRetainerBox::OnRegister()
{
	Super::OnRegister();
	ApplyCanvasConfiguration();
	ApplyGroupCompositing();
}

void UDreamRetainerBox::OnUnregister()
{
	RestoreCanvasConfiguration();
	Super::OnUnregister();
}

void UDreamRetainerBox::ApplyCanvasConfiguration()
{
	auto Canvas = GetRetainedCanvas();
	if (Canvas == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d DreamRetainerBox on '%s' has no DreamCanvas on the same widget, so there is nothing to retain. Add a DreamCanvas component to this widget.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__
			, GetWidget() != nullptr ? *GetWidget()->GetDisplayName() : TEXT("none"));
		return;
	}
	if (!bHasSavedCanvasState)
	{
		//remembered so OnUnregister can hand the canvas back as it was found; a retainer removed at
		//runtime must not leave the canvas rendering to a target nobody asks to update
		bHasSavedCanvasState = true;
		bSavedForceRenderToTarget = Canvas->GetForceRenderToTarget();
		bSavedRenderTargetUpdateMode = (uint8)Canvas->GetRenderTargetUpdateMode();
	}
	Canvas->SetForceRenderToTarget(true);
	//WhenRequest is what makes retaining retaining: the canvas redraws its target only when asked,
	//and Tick below is what decides when to ask
	Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::WhenRequest);
	//the first frame must draw something, whatever the phase says
	bRenderRequested = true;
}

void UDreamRetainerBox::RestoreCanvasConfiguration()
{
	if (!bHasSavedCanvasState)
	{
		return;
	}
	bHasSavedCanvasState = false;
	if (auto Canvas = GetRetainedCanvas())
	{
		Canvas->SetForceRenderToTarget(bSavedForceRenderToTarget);
		Canvas->SetRenderTargetUpdateMode((EDreamCanvasRenderTargetUpdateMode)bSavedRenderTargetUpdateMode);
		//it stopped being asked to update, so give it one so it is not left showing a stale texture
		Canvas->RequestUpdateForRenderTarget();
	}
}

void UDreamRetainerBox::ApplyGroupCompositing()
{
	auto Display = DisplayVisual.Get();
	if (Display == nullptr)
	{
		return;
	}
	if (auto Canvas = GetRetainedCanvas())
	{
		Display->SetPreviewCanvas(Canvas);
	}
	Display->SetPreviewMaterial(EffectMaterial);
	/**
	 * The group alpha, applied once to the composited texture. This is the whole difference from
	 * per-widget RenderOpacity: the subtree is already flattened into the texture by the time this
	 * multiplies it, so overlapping children fade together instead of through each other.
	 */
	Display->SetAlpha(GroupRenderOpacity);
}

void UDreamRetainerBox::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto Canvas = GetRetainedCanvas();
	if (Canvas == nullptr)
	{
		return;
	}
	if (ShouldRenderThisFrame(bRetainRendering, Phase, PhaseCount, bRenderRequested, GFrameCounter))
	{
		//consumed here, not in RequestUpdateForRenderTarget: a request is for one redraw, and the
		//canvas clears its own flag when it takes it
		bRenderRequested = false;
		Canvas->RequestUpdateForRenderTarget();
	}
}

void UDreamRetainerBox::RequestRender()
{
	bRenderRequested = true;
}

void UDreamRetainerBox::SetRetainRendering(bool Value)
{
	if (bRetainRendering != Value)
	{
		bRetainRendering = Value;
		//turning retaining back on has to redraw once: the texture is however old the last retained
		//frame left it
		bRenderRequested = true;
	}
}

void UDreamRetainerBox::SetRenderingPhase(int32 InPhase, int32 InPhaseCount)
{
	PhaseCount = FMath::Max(InPhaseCount, 1);
	Phase = FMath::Clamp(InPhase, 0, PhaseCount - 1);
}

void UDreamRetainerBox::SetGroupRenderOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (GroupRenderOpacity != Value)
	{
		GroupRenderOpacity = Value;
		//only the composite changes, not what is inside the texture, so this does NOT request a redraw
		ApplyGroupCompositing();
	}
}

void UDreamRetainerBox::SetEffectMaterial(UMaterialInterface* Value)
{
	if (EffectMaterial != Value)
	{
		EffectMaterial = Value;
		ApplyGroupCompositing();
	}
}

bool UDreamInvalidationBox::ShouldRebuildDrawCall(bool bInCanCache, bool bInCacheInvalidated)
{
	//not caching is a pass-through: the canvas rebuilds whenever it is dirty, as it did before
	if (!bInCanCache)
	{
		return true;
	}
	return bInCacheInvalidated;
}

UDreamCanvas* UDreamInvalidationBox::GetCachedCanvas()const
{
	if (auto Widget = GetWidget())
	{
		return Widget->GetComponent<UDreamCanvas>();
	}
	return nullptr;
}

void UDreamInvalidationBox::OnRegister()
{
	Super::OnRegister();
	//the subtree has never been built under this box, so let the first rebuild through
	bCacheInvalidated = true;
	ApplySuspendState();
}

void UDreamInvalidationBox::OnUnregister()
{
	//never leave the canvas suspended by a box that is no longer there
	if (auto Canvas = GetCachedCanvas())
	{
		Canvas->SetDrawCallRebuildSuspended(false);
	}
	Super::OnUnregister();
}

void UDreamInvalidationBox::ApplySuspendState()
{
	auto Canvas = GetCachedCanvas();
	if (Canvas == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d DreamInvalidationBox on '%s' has no DreamCanvas on the same widget, so there is no draw-call list to cache. Add a DreamCanvas component to this widget.")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__
			, GetWidget() != nullptr ? *GetWidget()->GetDisplayName() : TEXT("none"));
		return;
	}
	Canvas->SetDrawCallRebuildSuspended(!ShouldRebuildDrawCall(bCanCache, bCacheInvalidated));
}

void UDreamInvalidationBox::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	auto Canvas = GetCachedCanvas();
	if (Canvas == nullptr)
	{
		return;
	}
	if (ShouldRebuildDrawCall(bCanCache, bCacheInvalidated))
	{
		//let this frame's rebuild through, then close again. The canvas keeps its own dirty flag, so
		//"let through" costs nothing when there was nothing to rebuild.
		Canvas->SetDrawCallRebuildSuspended(false);
		bCacheInvalidated = false;
	}
	else
	{
		Canvas->SetDrawCallRebuildSuspended(true);
	}
}

void UDreamInvalidationBox::InvalidateCache()
{
	bCacheInvalidated = true;
	if (auto Canvas = GetCachedCanvas())
	{
		//released immediately rather than at the next tick, so an invalidate followed by a manual
		//update pass in the same frame behaves the way the caller plainly meant
		Canvas->SetDrawCallRebuildSuspended(false);
	}
}

void UDreamInvalidationBox::SetCanCache(bool Value)
{
	if (bCanCache != Value)
	{
		bCanCache = Value;
		//coming out of caching must not leave a deferred rebuild stranded, and going into it must not
		//freeze a subtree that never got its first build
		bCacheInvalidated = true;
		ApplySuspendState();
	}
}

#undef LOCTEXT_NAMESPACE
