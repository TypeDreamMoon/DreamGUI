// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamPointerPolicy.h"

#include "Core/Components/DreamWidget.h"

namespace DreamPointerPolicy
{
	bool ShouldIgnoreHitWhileDragging(const UDreamWidget* InHitWidget, const UDreamWidget* InDragWidget, bool bInIsDragging)
	{
		if (!bInIsDragging || InHitWidget == nullptr || InDragWidget == nullptr)
		{
			return false;
		}
		return InHitWidget == InDragWidget || InHitWidget->IsChildOf(InDragWidget);
	}

	UDreamWidget* ResolveEnabledHitTarget(UDreamWidget* InTopHit)
	{
		// Up from the hit to the first widget that takes input. The interactable answer cascades down from a disabled
		// widget (UDreamWidget::CalculateInteractable_Recursive), so this is the parent of the outermost disabled one --
		// Slate's cut -- unless a widget in between was pinned Interactable, which this framework honours.
		for (UDreamWidget* Walker = InTopHit; Walker != nullptr; Walker = Walker->GetParent())
		{
			if (IsValid(Walker) && Walker->GetInteractableInHierarchy())
			{
				return Walker;
			}
		}
		return nullptr;
	}

	bool ResolveCursor(TArrayView<UDreamWidget* const> InHoverStackInnermostFirst, EMouseCursor::Type& OutCursor)
	{
		for (UDreamWidget* Widget : InHoverStackInnermostFirst)
		{
			if (!IsValid(Widget))
			{
				continue;
			}
			const EMouseCursor::Type Claimed = Widget->GetCursor();
			// Default means "no opinion", not "force the arrow". Otherwise a plain container between
			// a hand-cursor button and the root would cancel the button's claim, which is the shape
			// nearly every hierarchy has.
			if (Claimed != EMouseCursor::Default)
			{
				OutCursor = Claimed;
				return true;
			}
		}
		return false;
	}
}
