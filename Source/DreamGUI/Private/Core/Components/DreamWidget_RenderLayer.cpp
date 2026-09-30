// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"

namespace DreamWidgetRenderLayerLocal
{
	/**
	 * Moves on whenever a render layer comes or goes, or a widget changes its place in a tree: every answer GetRenderLayer
	 * kept is stale from then on. Game thread only, as GetRenderLayer is. Sixty-four bits, so that it never comes round
	 * again to a generation some widget still holds.
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
