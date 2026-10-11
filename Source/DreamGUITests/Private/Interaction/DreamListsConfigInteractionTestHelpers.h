// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamListsInteractionTestTypes.h"

/**
 * What the list, tile and tree tests of the disabled and scaled configurations share, on top of DreamControlsConfigTest and
 * DreamListsInteraction. Plain inline functions, as theirs are.
 */
namespace DreamListsConfigTest
{
	/** Where item InItemIndex's row is drawn, or unset when it has no row. */
	inline TOptional<FBox2D> RowRect(const UDreamListViewBase& InList, int32 InItemIndex)
	{
		return DreamControlsConfigTest::DrawnRect(InList.GetRowWidget(InItemIndex));
	}

	/** Where the list's window -- its viewport -- is drawn, or unset. */
	inline TOptional<FBox2D> WindowRect(const UDreamListViewBase& InList)
	{
		return DreamControlsConfigTest::DrawnRect(InList.ViewportNode.Get());
	}

	/** Pumped until the list has come to rest -- a finger lifted off it leaves it coasting -- for as long as its own glide takes. */
	inline bool WaitUntilStill(FDreamDriverRig& InRig, const UDreamListViewBase& InList)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(5.0);
		const UDreamListViewBase* List = &InList;
		return InRig.Driver()->Wait(FDreamUntil::Condition([List]()
		{
			return List->ScrollBehaviour == nullptr || !List->ScrollBehaviour->IsScrolling();
		}, Timeout), Timeout, TEXT("the list comes to rest"));
	}

	/** Whether player 0's focus is on item InItemIndex's row, and that row is drawn inside InWindow, to within a pixel and a half. */
	inline bool IsFocusOnRowInside(const FDreamDriverRig& InRig, const UDreamListViewBase& InList, int32 InItemIndex, const FBox2D& InWindow)
	{
		const UDreamWidget* Row = InList.GetRowWidget(InItemIndex);
		const TOptional<FBox2D> Rect = DreamControlsConfigTest::DrawnRect(Row);
		return Row != nullptr && DreamControlsConfigTest::FocusIsIn(InRig, Row) && Row->GetRenderVisibleInHierarchy() && Rect.IsSet()
			&& Rect->Min.Y >= InWindow.Min.Y - 1.5 && Rect->Max.Y <= InWindow.Max.Y + 1.5;
	}
}
