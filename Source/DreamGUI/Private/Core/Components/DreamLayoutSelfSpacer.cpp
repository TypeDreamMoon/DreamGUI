// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamLayoutSelfSpacer.h"
#include "Core/Components/DreamWidget.h"

void UDreamLayoutSelfSpacer::CalculateSize()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	// An axis the parent's layout sizes is the parent's to write, the way UDreamLayoutSelfAspectRatio::Solve
	// yields it. The spacer's size still reaches that layout -- it is what GetLayoutPreferredSize answers --
	// but writing it back over the panel's result started a fight neither side could win: a vertical box
	// handed a Fill slot its full width, this wrote the authored zero back with no write scope open, which
	// re-dirtied the box and every ancestor, and the box wrote the width again on the next pass, until the
	// manager gave up at its pass cap with "Layout did not converge" -- every frame.
	FDreamLayoutControlAnchorData ParentControl;
	if (const UDreamWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
	{
		if (const UDreamLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer(); IsValid(ParentLayout))
		{
			ParentControl = ParentLayout->GetLayoutControlAnchor(Widget);
		}
	}
	if (!ParentControl.bCanControlHorizontalSize)
	{
		Widget->SetWidth(static_cast<float>(Size.X));
	}
	if (!ParentControl.bCanControlVerticalSize)
	{
		Widget->SetHeight(static_cast<float>(Size.Y));
	}
}

FDreamLayoutControlAnchorData UDreamLayoutSelfSpacer::GetLayoutControlAnchor(const UDreamWidget* Widget) const
{
	FDreamLayoutControlAnchorData Result;
	if (Widget == GetWidget())
	{
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}
