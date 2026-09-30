// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamWidget.h"

bool UDreamWidget::IsRenderLayer()const
{
	return false;
}

UDreamWidget* UDreamWidget::GetRenderLayer()const
{
	return nullptr;
}
