// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamMenuAnchor.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweener.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIButton.h"

const FName UDreamMenuAnchor::MenuSlotName(TEXT("Menu"));

void UDreamMenuAnchor::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Popup"), PopupNode);
	OutParts.Emplace(UDreamMenuAnchor::MenuSlotName, MenuNode);
}

void UDreamMenuAnchor::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The dropdown's list, generalised. POINT anchors and absolute sizes, deliberately: the popup is
	// inactive between opens, and an inactive widget's stretched axis never re-arranges -- its cached
	// span is stale (zero, after an elevation round trip), and Elevate pins whatever it finds.
	//
	// IgnoreLayout because a popup is not part of the layout it hangs off: an Auto-sized row holding
	// this anchor would otherwise grow by the menu's height the moment it opened and shove the rest
	// of the screen down.
	Realize(this,
		Node<UDreamRectBlock>("Popup")
			.Anchors(FVector2D(0.0, 0.0), FVector2D(0.0, 0.0))
			.Self([](UDreamWidget& InPopup)
			{
				InPopup.SetIgnoreLayout(true);
				InPopup.SetClipping(EDreamWidgetClipping::ClipToBounds);
			})
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				Widget("Menu")
					.With<UDreamLayoutContainerOverlay>()
					.With<UDreamNamedSlot>([](UDreamNamedSlot& InSlot)
					{
						// A menu is a list of things, so several is the useful answer -- the same call
						// UDreamScrollBox's content slot and UDreamBorder's make.
						InSlot.bAcceptsSeveral = true;
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
}

void UDreamMenuAnchor::WireParts()
{
	// Asleep until Open(); Open wakes it, positions it and lifts it.
	if (PopupNode != nullptr)
	{
		PopupNode->SetWidgetActive(false);
	}
}

void UDreamMenuAnchor::ApplyStyle()
{
	const FDreamMenuAnchorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle);

	ShapeFace(PopupNode, Active.CornerRadius);
	SkinFace(PopupNode, Active.BackgroundBrush);
	if (UDreamVisual* PopupVisual = PopupNode != nullptr ? PopupNode->GetVisual() : nullptr)
	{
		PopupVisual->SetColor(Active.Background);
	}
	if (!bPopupElevated)
	{
		// Never while the popup is lifted: Elevate re-anchored it to the screen root, and writing the
		// resting scheme over that would move an open menu to wherever the anchor happens to be. The
		// dropdown's ApplyListRestingGeometry makes the same check for the same reason.
		PlacePopup(Active);
	}
}

void UDreamMenuAnchor::PlacePopup(const FDreamMenuAnchorStyle& InStyle)
{
	if (PopupNode == nullptr)
	{
		return;
	}
	// A zero on an axis is "leave it to the content": the node keeps whatever size it has, which is
	// what an authored menu that states its own size wants.
	const FVector2D AnchorSize(GetWidth(), GetHeight());
	FVector2D Size(
		MenuSize.X > 0.0 ? MenuSize.X : PopupNode->GetWidth(),
		MenuSize.Y > 0.0 ? MenuSize.Y : PopupNode->GetHeight());
	if (UDreamLayoutContainerMenuAnchor::PlacementMatchesAnchorWidth(InStyle.Placement))
	{
		// UMG's ComboBox placements force the menu to the anchor's width, and that rule is the layout
		// panel's to state -- asking it rather than repeating the list of placements it applies to is
		// what keeps the two anchors agreeing when a placement is added.
		Size.X = AnchorSize.X;
	}

	// The POSITION is UDreamLayoutContainerMenuAnchor's arithmetic and not a second copy of it: the
	// layout panel and this control are two ways to open a menu, and a menu that landed somewhere
	// else depending on which one opened it would be two behaviours wearing one name. That function
	// answers in TOP-LEFT space with y downwards, relative to the anchor's own top-left corner.
	FVector2D TopLeft = UDreamLayoutContainerMenuAnchor::CalculateMenuPosition(
		InStyle.Placement, FVector2D::ZeroVector, AnchorSize, Size);
	// The gap is this control's own, because the layout panel has none: a placed menu touches its
	// anchor there. Pushed along the direction the placement chose, so the gap always widens the
	// distance between the two rather than moving the menu sideways.
	TopLeft += ResolveOffsetDirection(InStyle.Placement) * InStyle.Offset;

	// Into the widget frame: anchored to the control's TOP-LEFT corner with the popup's own top-left
	// as its pivot, so the number above IS the anchored position -- once y is negated, because this
	// framework's local space is y-UP and that function's is y-down.
	PopupNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
	PopupNode->SetPivot(FVector2D(0.0, 1.0));
	PopupNode->SetAnchoredPositionAndSizeDelta(FVector2D(TopLeft.X, -TopLeft.Y), Size);
}

FVector2D UDreamMenuAnchor::ResolveOffsetDirection(EDreamMenuPlacement InPlacement)
{
	// In the placement function's own space: y downwards. Which WAY a gap widens is the only thing
	// about a placement this control has to know, and it follows from the placement's family rather
	// than from its exact alignment -- every Below goes down, every Above goes up, and a menu centred
	// ON its anchor has no gap to widen because there is no edge between the two.
	switch (InPlacement)
	{
	case EDreamMenuPlacement::AboveAnchor:
	case EDreamMenuPlacement::CenteredAboveAnchor:
	case EDreamMenuPlacement::AboveRightAnchor:
		return FVector2D(0.0, -1.0);
	case EDreamMenuPlacement::MenuRight:
		return FVector2D(1.0, 0.0);
	case EDreamMenuPlacement::MenuLeft:
		return FVector2D(-1.0, 0.0);
	case EDreamMenuPlacement::Center:
		return FVector2D::ZeroVector;
	default:
		// Every Below placement, the combo boxes included.
		return FVector2D(0.0, 1.0);
	}
}

void UDreamMenuAnchor::EnsureMenuInstance()
{
	if (MenuInstance != nullptr || MenuClass == nullptr || MenuNode == nullptr)
	{
		return;
	}
	if (IsSlotFilled(UDreamMenuAnchor::MenuSlotName))
	{
		// Content somebody put in the hole is content they meant to see. The class is the fallback,
		// not a second menu drawn over the first.
		return;
	}
	if (GetWorld() == nullptr)
	{
		// Instancing a user widget needs a world, and with none this stays an empty menu rather than
		// half of one -- the same answer UDreamListViewBase gives for a row template class.
		return;
	}
	MenuInstance = CreateDreamWidget(GetWorld(), MenuClass, MenuNode);
	if (MenuInstance != nullptr)
	{
		MenuInstance->SetDisplayName(TEXT("MenuContent"));
		if (UDreamPanelSlot* MenuSlot = MenuInstance->GetPanelSlot())
		{
			MenuSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			MenuSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
		}
	}
}

void UDreamMenuAnchor::Open()
{
	if (bIsOpen || !IsValid(PopupNode))
	{
		return;
	}
	bIsOpen = true;
	EnsureMenuInstance();

	const FDreamMenuAnchorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle);
	// The placement first, while the popup is still a child of the anchor: Elevate keeps the world
	// position it finds, so positioning against the anchor and THEN lifting is what puts a menu in
	// the right place without the layer knowing anything about anchors.
	PlacePopup(Active);
	PopupNode->SetWidgetActive(true);

	if (bCloseOnClickOutside)
	{
		CreateBlocker();
	}
	if (UDreamUIPopupLayer* Popup = UDreamUIPopupLayer::Get(this))
	{
		bPopupElevated = Popup->Elevate(PopupNode);
	}

	if (Active.TransitionDuration > 0.0f)
	{
		PopupNode->SetRenderOpacity(0.0f);
		if (PopupNode->RenderOpacityTo(1.0f, Active.TransitionDuration, 0.0f, EDreamTweenEase::OutCubic) == nullptr)
		{
			// The tween manager is a world subsystem and hands back null without one, which is every
			// headless test and every designer preview. Snapping to the END state is the only correct
			// fallback: leaving the start value written would be a menu that is open and invisible.
			PopupNode->SetRenderOpacity(1.0f);
		}
	}
	else
	{
		PopupNode->SetRenderOpacity(1.0f);
	}
	OnMenuOpenChanged.Broadcast(true);
}

void UDreamMenuAnchor::Close()
{
	if (!bIsOpen)
	{
		return;
	}
	bIsOpen = false;
	DestroyBlocker();
	if (IsValid(PopupNode))
	{
		// Home first, then asleep: Restore reparents under the anchor again, and a popup put to sleep
		// while still lifted would be an inactive widget hanging off the screen root that nothing
		// would ever come back for.
		if (bPopupElevated)
		{
			if (UDreamUIPopupLayer* Popup = UDreamUIPopupLayer::Get(this))
			{
				Popup->Restore(PopupNode);
			}
			bPopupElevated = false;
		}
		PopupNode->SetWidgetActive(false);
		// The whole resting scheme, not just the numbers: Elevate re-anchored the popup to a POINT on
		// the screen root and Restore reparents plainly, so without this the next open works against
		// the wrong anchors. UDreamDropdown measured that one as a zero-width list on the second open.
		PlacePopup(ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle));
	}
	OnMenuOpenChanged.Broadcast(false);
}

void UDreamMenuAnchor::ToggleOpen()
{
	if (bIsOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

void UDreamMenuAnchor::NativeOnDestruct()
{
	// An anchor torn down while its menu is open would otherwise leave a lifted popup and a
	// full-screen blocker on the screen root with nothing left that could close them -- which is the
	// invisible-sheet-over-everything failure UUIDropdown::Show was fixed for.
	Close();
	Super::NativeOnDestruct();
}

void UDreamMenuAnchor::CreateBlocker()
{
	if (IsValid(BlockerNode))
	{
		return;
	}
	UDreamCanvas* RootCanvas = GetRootCanvas();
	if (RootCanvas == nullptr || !IsValid(RootCanvas->GetWidget()))
	{
		// No screen to cover. The menu still opens -- it is simply the caller's job to close it,
		// which is exactly what bCloseOnClickOutside=false means.
		return;
	}
	BlockerNode = NewObject<UDreamWidget>(GetOuter());
	BlockerNode->SetDisplayName(TEXT("MenuAnchor_Blocker"));
	BlockerNode->SetParent(RootCanvas->GetWidget(), false);
	BlockerNode->SetAnchorMin(FVector2D(0.0, 0.0));
	BlockerNode->SetAnchorMax(FVector2D(1.0, 1.0));
	BlockerNode->SetSizeDelta(FVector2D::ZeroVector);
	// A visual is what makes a widget raycastable at all, and an empty one draws nothing while still
	// answering the pointer -- hit testing is the rect range, not the pixels.
	BlockerNode->CreateNewVisual<UDreamVisualEmpty>();
	if (UDreamCanvas* BlockerCanvas = BlockerNode->AddComponent<UDreamCanvas>())
	{
		BlockerCanvas->SetOverrideSorting(true);
		BlockerCanvas->SetSortOrderToHighestOfHierarchy();
		BlockerCanvas->SetTraceChannel(RootCanvas->GetTraceChannel());
	}
	if (UUIButton* BlockerButton = BlockerNode->AddComponent<UUIButton>())
	{
		BlockerButton->GetOnClickEvent().AddWeakLambda(this, [this]()
		{
			Close();
		});
	}
}

void UDreamMenuAnchor::DestroyBlocker()
{
	if (IsValid(BlockerNode))
	{
		BlockerNode->DestroyWidget();
	}
	BlockerNode = nullptr;
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "MenuAnchor", UDreamMenuAnchor)
