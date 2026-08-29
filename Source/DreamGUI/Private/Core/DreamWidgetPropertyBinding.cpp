// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetPropertyBinding.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"

UObject* ResolveDreamWidgetBindingTarget(const UDreamWidget* InWidget, EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	switch (InTarget)
	{
	case EDreamWidgetBindingTarget::Widget:
		return const_cast<UDreamWidget*>(InWidget);
	case EDreamWidgetBindingTarget::Visual:
		return InWidget->GetVisual();
	case EDreamWidgetBindingTarget::Behaviour:
	{
		const TArray<UDreamUIBehaviour*>& Components = InWidget->GetAllComponents();
		return Components.IsValidIndex(InBehaviourIndex) ? Components[InBehaviourIndex] : nullptr;
	}
	}
	return nullptr;
}
