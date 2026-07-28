// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/LexPointerPolicy.h"

#include "Core/Components/LexWidget.h"

namespace LexPointerPolicy
{
	bool ShouldIgnoreHitWhileDragging(const ULexWidget* InHitWidget, const ULexWidget* InDragWidget, bool bInIsDragging)
	{
		if (!bInIsDragging || InHitWidget == nullptr || InDragWidget == nullptr)
		{
			return false;
		}
		return InHitWidget == InDragWidget || InHitWidget->IsChildOf(InDragWidget);
	}

	bool ResolveCursor(TArrayView<ULexWidget* const> InHoverStackInnermostFirst, EMouseCursor::Type& OutCursor)
	{
		for (ULexWidget* Widget : InHoverStackInnermostFirst)
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
