// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

namespace DreamWidgetRenderLayerLocal
{
	/**
	 * Moves on whenever a render layer comes or goes, a widget changes its place in a tree, or a transform event is asked
	 * for: every answer GetRenderLayer and IsRenderLayerQuiet kept is stale from then on. Game thread only, as GetRenderLayer
	 * is. Sixty-four bits, so that it never comes round again to a generation some widget still holds.
	 */
	uint64 CacheGeneration = 1;
}

bool UDreamWidget::IsRenderLayer()const
{
	return bIsRenderLayer;
}

UDreamWidget* UDreamWidget::GetRenderLayer()const
{
	// Asked for every transform change of every widget, and for every vertex transform: worked out by walking up only when
	// a layer or the tree changed since the last answer.
	if (CachedRenderLayerGeneration != DreamWidgetRenderLayerLocal::CacheGeneration)
	{
		CachedRenderLayerGeneration = DreamWidgetRenderLayerLocal::CacheGeneration;
		CachedRenderLayer = nullptr;
		// Up through the widgets its canvas draws, and no further: a widget hosting a canvas belongs to that canvas, and
		// a layer never takes in another canvas's elements.
		if (const UDreamCanvas* Canvas = RenderCanvas.Get())
		{
			for (const UDreamWidget* Widget = this; Widget != nullptr && Widget->RenderCanvas.Get() == Canvas; Widget = Widget->Parent.Get())
			{
				if (Widget->bIsRenderLayer)
				{
					CachedRenderLayer = const_cast<UDreamWidget*>(Widget);
					break;
				}
			}
		}
	}
	return CachedRenderLayer.Get();
}

bool UDreamWidget::IsRenderLayerQuiet()const
{
	if (CachedLayerQuietGeneration != DreamWidgetRenderLayerLocal::CacheGeneration)
	{
		CachedLayerQuietGeneration = DreamWidgetRenderLayerLocal::CacheGeneration;
		// Down through everything under the layer. A widget hosting a canvas is enough to say no: that canvas's widgets
		// are its own, placed by where its widget stands, and so is anything under a layer inside this one.
		bool bQuiet = true;
		TArray<const UDreamWidget*, TInlineAllocator<32>> ToVisit;
		for (const UDreamWidget* Child : GetChildren())
		{
			ToVisit.Add(Child);
		}
		while (bQuiet && ToVisit.Num() > 0)
		{
			const UDreamWidget* Widget = ToVisit.Pop(EAllowShrinking::No);
			if (!IsValid(Widget))
			{
				continue;
			}
			bQuiet = !Widget->bIsCanvasWidget && !Widget->bIsRenderLayer && !Widget->OnTransformChangedEvent.IsBound();
			for (const UDreamWidget* Child : Widget->GetChildren())
			{
				ToVisit.Add(Child);
			}
		}
		bCachedLayerQuiet = bQuiet;
	}
	return bCachedLayerQuiet;
}

UDreamWidget::FTransformChangedEvent& UDreamWidget::GetTransformChangedEvent()
{
	// Whoever asks may bind, and every event is bound through here: a layer this widget is in is looked at again.
	InvalidateRenderLayerCaches();
	return OnTransformChangedEvent;
}

void UDreamWidget::InvalidateRenderLayerCaches()
{
	++DreamWidgetRenderLayerLocal::CacheGeneration;
}

void UDreamWidget::SetRenderLayerMode(EDreamWidgetRenderLayer Value)
{
	if (RenderLayer != Value)
	{
		RenderLayer = Value;
		if (UDreamCanvas* Canvas = RenderCanvas.Get())
		{
			Canvas->NoteRenderLayerModeChanged(this);
		}
	}
}
