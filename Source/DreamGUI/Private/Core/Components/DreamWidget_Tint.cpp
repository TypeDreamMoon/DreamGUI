// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"

/*
 * The content tint: a colour a widget lays over everything drawn below it and never over its own visual, as an
 * SCompoundWidget blends its ColorAndOpacity into the style its children paint with. A border's
 * ContentColorAndOpacity is the reason it exists.
 *
 * Asked for, not cached, the way GetFinalRenderOpacity is: a visual reads it when its colour is rebuilt, and the
 * setter marks every visual below dirty so that happens. A cached product would need invalidating from every attach
 * and detach as well, for a value that is white almost everywhere.
 */

void UDreamWidget::SetContentTint(const FLinearColor& Value)
{
	if (ContentTint == Value)
	{
		return;
	}
	ContentTint = Value;
	struct FLocal
	{
		static void MarkDirty(const UDreamWidget* Widget)
		{
			// Children can hold nulls between a teardown and the next tidy-up, as SetRenderOpacity's walk knows.
			if (!IsValid(Widget))
			{
				return;
			}
			if (Widget->Visual)
			{
				Widget->Visual->MarkColorDirty();
			}
			for (auto& Child : Widget->Children)
			{
				MarkDirty(Child);
			}
		}
	};
	// Below this widget only: its own visual is tinted by its ancestors' tints, not by its own.
	for (auto& Child : Children)
	{
		FLocal::MarkDirty(Child);
	}
}

FLinearColor UDreamWidget::GetInheritedContentTint()const
{
	FLinearColor Result = FLinearColor::White;
	for (const UDreamWidget* Ancestor = Parent.Get(); Ancestor != nullptr; Ancestor = Ancestor->Parent.Get())
	{
		Result *= Ancestor->ContentTint;
	}
	return Result;
}
