// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetParts.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"

#define LOCTEXT_NAMESPACE "DreamWidgetParts"

EDreamWidgetPartKind FDreamWidgetParts::KindOf(const UClass* InClass)
{
	if (InClass == nullptr)
	{
		return EDreamWidgetPartKind::None;
	}
	if (InClass->IsChildOf(UDreamVisual::StaticClass()))
	{
		return EDreamWidgetPartKind::Visual;
	}
	if (InClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
	{
		return EDreamWidgetPartKind::LayoutContainer;
	}
	if (InClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
	{
		return EDreamWidgetPartKind::LayoutSelf;
	}
	if (InClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return EDreamWidgetPartKind::Behaviour;
	}
	return EDreamWidgetPartKind::None;
}

UObject* FDreamWidgetParts::Find(const UDreamWidget* InWidget, const UClass* InClass)
{
	if (InWidget == nullptr || InClass == nullptr)
	{
		return nullptr;
	}
	auto IfA = [InClass](UObject* InPart) -> UObject*
	{
		return InPart != nullptr && InPart->IsA(InClass) ? InPart : nullptr;
	};
	switch (KindOf(InClass))
	{
	case EDreamWidgetPartKind::Visual:
		return IfA(InWidget->GetVisual());
	case EDreamWidgetPartKind::LayoutContainer:
		return IfA(InWidget->GetLayoutContainer());
	case EDreamWidgetPartKind::LayoutSelf:
		return IfA(InWidget->GetLayoutSelf());
	case EDreamWidgetPartKind::Behaviour:
		for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			if (UObject* Found = IfA(Behaviour))
			{
				return Found;
			}
		}
		return nullptr;
	case EDreamWidgetPartKind::None:
	default:
		// A base shared by more than one kind -- UDreamLayout, UDreamWidgetSubObjectBehaviour. The sub-objects are asked in
		// a fixed order so the same widget always gives the same answer.
		if (UObject* Found = IfA(InWidget->GetVisual()))
		{
			return Found;
		}
		if (UObject* Found = IfA(InWidget->GetLayoutContainer()))
		{
			return Found;
		}
		return IfA(InWidget->GetLayoutSelf());
	}
}

void FDreamWidgetParts::GetAll(const UDreamWidget* InWidget, TArray<UObject*>& OutParts)
{
	OutParts.Reset();
	if (InWidget == nullptr)
	{
		return;
	}
	if (UObject* Visual = InWidget->GetVisual())
	{
		OutParts.Add(Visual);
	}
	if (UObject* Container = InWidget->GetLayoutContainer())
	{
		OutParts.Add(Container);
	}
	if (UObject* Self = InWidget->GetLayoutSelf())
	{
		OutParts.Add(Self);
	}
	for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
	{
		if (Behaviour != nullptr)
		{
			OutParts.Add(Behaviour);
		}
	}
}

FText FDreamWidgetParts::DescribeKind(EDreamWidgetPartKind InKind)
{
	switch (InKind)
	{
	case EDreamWidgetPartKind::Visual:
		return LOCTEXT("KindVisual", "Visual");
	case EDreamWidgetPartKind::LayoutContainer:
		return LOCTEXT("KindLayoutContainer", "Layout");
	case EDreamWidgetPartKind::LayoutSelf:
		return LOCTEXT("KindLayoutSelf", "Layout Self");
	case EDreamWidgetPartKind::Behaviour:
		return LOCTEXT("KindBehaviour", "Behaviour");
	case EDreamWidgetPartKind::None:
	default:
		return LOCTEXT("KindPart", "Part");
	}
}

#undef LOCTEXT_NAMESPACE
