// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Engine/World.h"

/*
 * What the files UDreamWidget is defined across share with each other and with nothing else: the
 * helpers that were file-local while the class was defined in one file. They stay file-local -- an
 * anonymous namespace, inline so that a file which calls none of them is not warned about them --
 * because nothing outside those files has any use for them.
 */
namespace
{
	/**
	 * Recursion / ancestor-walk guard for the geometry hot path. MarkLayoutForRebuild runs from every
	 * SetWidth, SetHeight, SetSizeDelta and SetAnchoredPosition -- once per tweened frame per widget --
	 * and a default TSet heap-allocates on its first insertion. Inline storage covers a normal
	 * hierarchy depth without touching the allocator; deeper trees spill to the heap as before.
	 */
	using FDreamVisitedWidgetSet = TSet<const UDreamWidget*, DefaultKeyFuncs<const UDreamWidget*>, TInlineSetAllocator<16>>;

	inline void RemovePanelSlotFromChild(UDreamWidget* ChildWidget)
	{
		if (!IsValid(ChildWidget) || !IsValid(ChildWidget->GetPanelSlot()))
		{
			return;
		}
#if WITH_EDITOR
		if (const UWorld* World = ChildWidget->GetWorld(); !World || !World->IsGameWorld())
		{
			DreamUI::ModifyIfKeptByUndo(*ChildWidget);
		}
#endif
		ChildWidget->RemovePanelSlot();
	}

	inline bool EnsurePanelSlotForChild(UDreamWidget* ParentWidget, UDreamWidget* ChildWidget, bool bRecaptureDesiredSize = false)
	{
		if (!IsValid(ParentWidget) || !IsValid(ChildWidget)
			|| !IsValid(Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			return false;
		}
		if (UDreamPanelSlot* ExistingSlot = ChildWidget->GetPanelSlot(); IsValid(ExistingSlot))
		{
			if (bRecaptureDesiredSize)
			{
				ExistingSlot->CaptureAuthoredGeometry(true);
			}
			else
			{
				ExistingSlot->CaptureAuthoredGeometry();
			}
			return false;
		}
#if WITH_EDITOR
		if (const UWorld* World = ChildWidget->GetWorld(); !World || !World->IsGameWorld())
		{
			DreamUI::ModifyIfKeptByUndo(*ChildWidget);
		}
#endif
		UDreamPanelSlot* NewSlot = ChildWidget->CreateNewPanelSlot<UDreamPanelSlot>();
		if (IsValid(NewSlot))
		{
			if (ParentWidget->GetLayoutContainer()->IsA<UDreamLayoutContainerScaleBox>())
			{
				NewSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
				NewSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
			}
			NewSlot->CaptureAuthoredGeometry(bRecaptureDesiredSize);
		}
		return IsValid(NewSlot);
	}

	inline void SynchronizePanelSlotForParent(UDreamWidget* ParentWidget, UDreamWidget* ChildWidget,
		bool bRecaptureDesiredSize = false)
	{
		if (IsValid(ParentWidget)
			&& IsValid(Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			EnsurePanelSlotForChild(ParentWidget, ChildWidget, bRecaptureDesiredSize);
		}
		else
		{
			RemovePanelSlotFromChild(ChildWidget);
		}
	}
}
