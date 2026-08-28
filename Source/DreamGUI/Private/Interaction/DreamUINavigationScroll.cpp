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
