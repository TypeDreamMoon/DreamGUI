// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamLayoutSelfSpacer.h"
#include "Core/Components/DreamWidget.h"

void UDreamLayoutSelfSpacer::CalculateSize()
{
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->SetWidth(static_cast<float>(Size.X));
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
