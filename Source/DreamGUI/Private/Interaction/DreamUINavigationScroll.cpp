// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUINavigationScroll.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/UIScrollView.h"

namespace DreamUINavigationScrollLocal
{
	/** One scrolling container on the way up. Exactly one of the two pointers is set. */
	struct FScrollAncestor
	{
		UDreamLayoutContainerScrollBox* Box = nullptr;
		UUIScrollView* View = nullptr;
	};

	/**
	 * Every scrolling container between InWidget and the root, innermost first.
	 *
	 * The walk starts at the parent, not at InWidget: a widget's own scroll box scrolls its children,
	 * so asking it to reveal itself is meaningless. A single ancestor can carry both kinds at once --
	 * the layout does the arranging while a legacy component handles the gesture -- so both are
	 * checked at every level rather than stopping at the first hit.
	 */
	static void CollectScrollAncestors(const UDreamWidget* InWidget, TArray<FScrollAncestor>& OutAncestors)
	{
		if (!IsValid(InWidget))return;
		UDreamWidget* Ancestor = InWidget->GetParent();
		while (IsValid(Ancestor))
		{
			if (auto Box = Cast<UDreamLayoutContainerScrollBox>(Ancestor->GetLayoutContainer()))
			{
				FScrollAncestor& Entry = OutAncestors.AddDefaulted_GetRef();
				Entry.Box = Box;
			}
			if (auto View = Ancestor->GetComponent<UUIScrollView>())
			{
				FScrollAncestor& Entry = OutAncestors.AddDefaulted_GetRef();
				Entry.View = View;
			}
			Ancestor = Ancestor->GetParent();
		}
	}
}

bool FDreamUINavigationScroll::IsReachableByScrolling(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))return false;

	TArray<DreamUINavigationScrollLocal::FScrollAncestor> Ancestors;
	DreamUINavigationScrollLocal::CollectScrollAncestors(InWidget, Ancestors);

	// One container able to move towards its target is enough. The target chains outwards the same way
	// RevealWidget scrolls, so a row its own list cannot reach still counts as reachable when the page
	// around that list can bring the list into view.
	UDreamWidget* Target = const_cast<UDreamWidget*>(InWidget);
	for (const DreamUINavigationScrollLocal::FScrollAncestor& Ancestor : Ancestors)
	{
		if (Ancestor.Box != nullptr)
		{
			if (Ancestor.Box->CanScrollWidgetIntoView(Target))
			{
				return true;
			}
			Target = Ancestor.Box->GetWidget() != nullptr ? Ancestor.Box->GetWidget() : Target;
		}
		if (Ancestor.View != nullptr)
		{
			if (Ancestor.View->CanScrollWidgetIntoView(Target))
			{
				return true;
			}
			Target = Ancestor.View->GetWidget() != nullptr ? Ancestor.View->GetWidget() : Target;
		}
	}
	return false;
}

bool FDreamUINavigationScroll::RevealWidget(UDreamWidget* InWidget, bool bAnimate)
{
	if (!IsValid(InWidget))return false;

	TArray<DreamUINavigationScrollLocal::FScrollAncestor> Ancestors;
	DreamUINavigationScrollLocal::CollectScrollAncestors(InWidget, Ancestors);

	// Each container reveals the one nested inside it, and only the innermost reveals the widget
	// itself. Asking every level to reveal the widget would be wrong twice over: an outer container
	// would aim at where the widget is *now*, before the inner scroll has been arranged, and a page
	// that cannot fit a whole list would fight the list over which row ends up framed.
	bool bMovedAnything = false;
	UDreamWidget* Target = InWidget;
	for (const DreamUINavigationScrollLocal::FScrollAncestor& Ancestor : Ancestors)
	{
		if (Ancestor.Box != nullptr)
		{
			bMovedAnything |= Ancestor.Box->ScrollWidgetIntoView(Target, bAnimate);
			Target = Ancestor.Box->GetWidget() != nullptr ? Ancestor.Box->GetWidget() : Target;
		}
		if (Ancestor.View != nullptr)
		{
			bMovedAnything |= Ancestor.View->ScrollWidgetIntoView(Target, bAnimate);
			Target = Ancestor.View->GetWidget() != nullptr ? Ancestor.View->GetWidget() : Target;
		}
	}
	return bMovedAnything;
}

namespace DreamUINavigationScrollLocal
{
	/**
	 * The scrolling container closest to InWidget, or an empty entry when it has none.
	 *
	 * Paging acts on the INNERMOST one, unlike RevealWidget which walks the whole chain: a page key
	 * pressed inside a list means "this list", and scrolling the page around it as well would move
	 * the thing the player is reading out from under them.
	 */
	static FScrollAncestor FindInnermostScrollAncestor(const UDreamWidget* InWidget)
	{
		TArray<FScrollAncestor> Ancestors;
		CollectScrollAncestors(InWidget, Ancestors);
		return Ancestors.Num() > 0 ? Ancestors[0] : FScrollAncestor();
	}

	/** One screenful for this container: its own visible extent along the axis it scrolls. */
	static float GetPageExtent(const FScrollAncestor& InAncestor)
	{
		if (InAncestor.Box != nullptr)
		{
			const UDreamWidget* Widget = InAncestor.Box->GetWidget();
			if (!IsValid(Widget))
			{
				return 0.0f;
			}
			return InAncestor.Box->Orientation == EDreamPanelOrientation::Vertical
				? Widget->GetHeight() : Widget->GetWidth();
		}
		if (InAncestor.View != nullptr)
		{
			const UDreamWidget* Widget = InAncestor.View->GetWidget();
			if (!IsValid(Widget))
			{
				return 0.0f;
			}
			// A view can scroll both ways; the vertical extent is the page, because a page key means
			// vertical everywhere it exists and a horizontal-only view is reached by the stick instead.
			return InAncestor.View->CanScrollOnAxis(/*bInHorizontalAxis*/false)
				? Widget->GetHeight() : Widget->GetWidth();
		}
		return 0.0f;
	}
}

bool FDreamUINavigationScroll::HasScrollableAncestor(const UDreamWidget* InWidget)
{
	const DreamUINavigationScrollLocal::FScrollAncestor Ancestor =
		DreamUINavigationScrollLocal::FindInnermostScrollAncestor(InWidget);
	if (Ancestor.Box != nullptr)
	{
		return Ancestor.Box->GetMaxScrollOffset() > 0.0f;
	}
	if (Ancestor.View != nullptr)
	{
		return Ancestor.View->CanScrollOnAxis(true) || Ancestor.View->CanScrollOnAxis(false);
	}
	return false;
}

bool FDreamUINavigationScroll::ScrollByPages(UDreamWidget* InWidget, float InPages, bool bAnimate)
{
	if (!IsValid(InWidget) || FMath::IsNearlyZero(InPages))
	{
		return false;
	}
	const DreamUINavigationScrollLocal::FScrollAncestor Ancestor =
		DreamUINavigationScrollLocal::FindInnermostScrollAncestor(InWidget);
	const float PageExtent = DreamUINavigationScrollLocal::GetPageExtent(Ancestor);
	if (PageExtent <= 0.0f)
	{
		return false;
	}
	const float Delta = PageExtent * InPages;
	if (Ancestor.Box != nullptr)
	{
		if (bAnimate)
		{
			const float Target = FMath::Clamp(Ancestor.Box->GetScrollOffset() + Delta, 0.0f, Ancestor.Box->GetMaxScrollOffset());
			if (FMath::IsNearlyEqual(Target, Ancestor.Box->GetScrollOffset()))
			{
				return false;//already at that limit; saying so lets a caller fall through to something else
			}
			Ancestor.Box->SetScrollOffsetAnimated(Target);
			return true;
		}
		return Ancestor.Box->ScrollBy(Delta);
	}
	if (Ancestor.View != nullptr)
	{
		// The axis the view actually scrolls. A vertical view takes the page on Y; a horizontal-only
		// one takes it on X, so a page key is not simply inert on a horizontal carousel.
		const FVector2D Before = Ancestor.View->GetScrollOffset();
		const bool bVertical = Ancestor.View->CanScrollOnAxis(/*bInHorizontalAxis*/false);
		Ancestor.View->ScrollBy(bVertical ? FVector2D(0.0f, Delta) : FVector2D(Delta, 0.0f));
		return !Ancestor.View->GetScrollOffset().Equals(Before, 0.01f);
	}
	return false;
}

bool FDreamUINavigationScroll::ScrollToExtent(UDreamWidget* InWidget, bool bToStart)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	const DreamUINavigationScrollLocal::FScrollAncestor Ancestor =
		DreamUINavigationScrollLocal::FindInnermostScrollAncestor(InWidget);
	if (Ancestor.Box != nullptr)
	{
		const float Before = Ancestor.Box->GetScrollOffset();
		if (bToStart)
		{
			Ancestor.Box->ScrollToStart();
		}
		else
		{
			Ancestor.Box->ScrollToEnd();
		}
		return !FMath::IsNearlyEqual(Before, Ancestor.Box->GetScrollOffset());
	}
	if (Ancestor.View != nullptr)
	{
		const FVector2D Before = Ancestor.View->GetScrollOffset();
		if (bToStart)
		{
			Ancestor.View->ScrollToStart();
		}
		else
		{
			Ancestor.View->ScrollToEnd();
		}
		return !Ancestor.View->GetScrollOffset().Equals(Before, 0.01f);
	}
	return false;
}

bool FDreamUINavigationScroll::ScrollByDelta(UDreamWidget* InWidget, const FVector2D& InDelta)
{
	if (!IsValid(InWidget) || InDelta.IsNearlyZero())
	{
		return false;
	}
	const DreamUINavigationScrollLocal::FScrollAncestor Ancestor =
		DreamUINavigationScrollLocal::FindInnermostScrollAncestor(InWidget);
	if (Ancestor.Box != nullptr)
	{
		// A box scrolls on one axis, and which component of the stick to take is decided by that axis
		// rather than by the caller -- the same stick should scroll a vertical list and a horizontal
		// one without the caller having to know which it is looking at.
		const float Amount = Ancestor.Box->Orientation == EDreamPanelOrientation::Vertical ? InDelta.Y : InDelta.X;
		return Ancestor.Box->ScrollBy(Amount);
	}
	if (Ancestor.View != nullptr)
	{
		const FVector2D Before = Ancestor.View->GetScrollOffset();
		Ancestor.View->ScrollBy(InDelta);
		return !Ancestor.View->GetScrollOffset().Equals(Before, 0.01f);
	}
	return false;
}
