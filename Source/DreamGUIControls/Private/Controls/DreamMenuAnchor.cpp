// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamMenuAnchor.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UISelectable.h"

const FName UDreamMenuAnchor::MenuSlotName(TEXT("Menu"));

namespace DreamMenuAnchorLocal
{
	/**
	 * Player InUserIndex's pointer whose press is going down right now -- the press being dispatched, which the popup layer
	 * hears before anything else does (UDreamUIPopupLayer::NotifyPointerDown) -- or null.
	 */
	UDreamPointerEventData* FindPressGoingDown(const UObject* InContext, int32 InUserIndex)
	{
		const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InContext);
		const UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
		if (User == nullptr)
		{
			return nullptr;
		}
		for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Pointer : User->GetPointerEventDataMap())
		{
			UDreamPointerEventData* EventData = Pointer.Value;
			if (IsValid(EventData) && EventData->bNowIsTriggerPressed && !EventData->bPrevIsTriggerPressed)
			{
				return EventData;
			}
		}
		return nullptr;
	}
}

bool UDreamMenuAnchor::ShouldOpenDueToClick() const
{
	if (bIsOpen)
	{
		return false;
	}
	// The press that closed the menu from outside went on to what it landed on (EDreamPopupOutsideClick::PassThrough), and
	// a trigger it landed on clicks as it is let go: its click is the close's, not a new open. It is that press for as long
	// as the pointer still holds it -- the same press time, and a press widget the release has not yet let go of.
	const UDreamPointerEventData* Press = DismissingPress.Get();
	const bool bClickOfTheClosingPress = Press != nullptr && Press->PressTime == DismissingPressTime && IsValid(Press->PressWidget);
	return !bClickOfTheClosingPress;
}

EDreamPopupOutsideClick UDreamMenuAnchor::ResolveOutsideClick() const
{
	if (!bCloseOnClickOutside)
	{
		return EDreamPopupOutsideClick::Ignore;
	}
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	return Settings != nullptr && Settings->bMenusConsumeOutsideClick ? EDreamPopupOutsideClick::Consume : EDreamPopupOutsideClick::PassThrough;
}

void UDreamMenuAnchor::StopOpenFade()
{
	// Without its completion: nothing hangs on it, and the opacity it leaves is the next writer's to set.
	if (UDreamTweener* Fade = OpenFadeTweener.Get())
	{
		UDreamTweenManager::KillIfIsTweening(this, Fade, false);
	}
	OpenFadeTweener = nullptr;
}

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

void UDreamMenuAnchor::ComputePlacement(const FDreamMenuAnchorStyle& InStyle, FVector2D& OutTopLeft, FVector2D& OutSize) const
{
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

	if (bFitInWindow)
	{
		// Against the root widget, the nearest thing here to UMG's window: it is the rect the whole
		// hierarchy is laid out inside. The arithmetic is the panel spelling's, CALLED rather than
		// copied -- a menu that landed somewhere else depending on which anchor opened it would be
		// two behaviours wearing one name, which is the same argument as CalculateMenuPosition above.
		// Where this control sits in the root, and at what scale, is the panel's answer too: the root's
		// pivot can be anywhere and a scale box can sit between the two.
		FVector2D WindowSize = FVector2D::ZeroVector;
		FVector2D AnchorOffset = FVector2D::ZeroVector;
		FVector2D AnchorScale = FVector2D::UnitVector;
		if (UDreamLayoutContainerMenuAnchor::GetPlacementInWindow(this, WindowSize, AnchorOffset, AnchorScale))
		{
			TopLeft = UDreamLayoutContainerMenuAnchor::FitMenuInWindowFromPanelSpace(
				TopLeft, Size, AnchorOffset, AnchorSize.X, WindowSize, false, AnchorScale);
		}
	}
	OutTopLeft = TopLeft;
	OutSize = Size;
}

void UDreamMenuAnchor::PlacePopup(const FDreamMenuAnchorStyle& InStyle)
{
	if (PopupNode == nullptr)
	{
		return;
	}
	FVector2D TopLeft = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	ComputePlacement(InStyle, TopLeft, Size);
	// Into the widget frame: anchored to the control's TOP-LEFT corner with the popup's own top-left
	// as its pivot, so the number above IS the anchored position -- once y is negated, because this
	// framework's local space is y-UP and that function's is y-down.
	PopupNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
	PopupNode->SetPivot(FVector2D(0.0, 1.0));
	PopupNode->SetAnchoredPositionAndSizeDelta(FVector2D(TopLeft.X, -TopLeft.Y), Size);
}

void UDreamMenuAnchor::PlaceLiftedPopup(UDreamWidget* InPopup)
{
	if (InPopup == nullptr || InPopup != PopupNode)
	{
		return;
	}
	const UDreamWidget* Plane = InPopup->GetParent();
	if (!IsValid(Plane))
	{
		return;
	}
	bPopupElevated = true;
	FVector2D TopLeft = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
	ComputePlacement(ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle), TopLeft, Size);
	// The lift pinned the popup to a point of the screen root with its top-left pivot kept, so its size is its width and
	// height, and its anchored position is where its top-left corner sits in the root's plane: this control's own
	// top-left corner moved by TopLeft -- y negated, this local space being y-up -- carried into that plane, Y across
	// and Z up, as the lift itself maps.
	InPopup->SetWidth(static_cast<float>(Size.X));
	InPopup->SetHeight(static_cast<float>(Size.Y));
	const FVector Corner(0.0, GetLocalSpaceLeft() + TopLeft.X, GetLocalSpaceTop() - TopLeft.Y);
	const FVector InPlane = Plane->GetLayoutWorldTransform().InverseTransformPosition(
		GetLayoutWorldTransform().TransformPosition(Corner));
	const FVector2D Anchored(InPlane.Y, InPlane.Z);
	// Only for a real move: the transforms round a little differently from frame to frame, and an exact compare
	// would lay the menu out again on every one of them.
	if (!InPopup->GetAnchoredPosition().Equals(Anchored, 0.01))
	{
		InPopup->SetAnchoredPosition(Anchored);
	}
}

void UDreamMenuAnchor::RefreshOpenPlacement()
{
	if (bPopupElevated)
	{
		PlaceLiftedPopup(PopupNode);
	}
	else
	{
		PlacePopup(ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle));
	}
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

bool UDreamMenuAnchor::IsCurrentTransition(const TWeakObjectPtr<UDreamMenuAnchor>& InAnchor, uint64 InSerial, bool bInOpen)
{
	const UDreamMenuAnchor* Anchor = InAnchor.Get();
	return Anchor != nullptr && Anchor->MenuTransitionSerial == InSerial && Anchor->bIsOpen == bInOpen
		&& (!bInOpen || IsValid(Anchor->PopupNode));
}

void UDreamMenuAnchor::EnsureMenuInstance(uint64 InOpenSerial)
{
	const TWeakObjectPtr<UDreamMenuAnchor> WeakThis(this);
	if (!IsCurrentTransition(WeakThis, InOpenSerial, true) || !IsValid(MenuNode)
		|| IsValid(MenuInstance) || IsValid(ProvidedMenuContent))
	{
		return;
	}
	if (IsSlotFilled(UDreamMenuAnchor::MenuSlotName))
	{
		// Content somebody put in the hole is content they meant to see. The class is the fallback,
		// not a second menu drawn over the first.
		return;
	}
	if (OnGetUserMenuContentEvent.IsBound())
	{
		// A provider can close, reopen or destroy the anchor. Its answer belongs only to the open
		// that asked: a nested open may already have supplied different content to this same slot.
		UDreamWidget* Provided = OnGetUserMenuContentEvent.Execute();
		if (!IsCurrentTransition(WeakThis, InOpenSerial, true) || !IsValid(MenuNode))
		{
			return;
		}
		if (IsValid(Provided))
		{
			const TWeakObjectPtr<UDreamWidget> WeakProvided(Provided);
			ProvidedMenuContent = Provided;
			// Publish before adoption, whose activation handlers may open this anchor again.
			Provided->TrySetParent(MenuNode, /*InKeepWorldPosition*/false);
			if (!IsCurrentTransition(WeakThis, InOpenSerial, true) || !WeakProvided.IsValid())
			{
				return;
			}
			if (UDreamPanelSlot* ProvidedSlot = Provided->GetPanelSlot())
			{
				ProvidedSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
				ProvidedSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
			}
			return;
		}
		// A handler with nothing to show yet leaves the class below its turn.
	}
	if (MenuClass == nullptr || GetWorld() == nullptr)
	{
		return;
	}
	TWeakObjectPtr<UDreamUserWidget> CreatedInstance;
	CreateDreamWidget(GetWorld(), MenuClass, MenuNode,
		[WeakThis, InOpenSerial, &CreatedInstance](UDreamUserWidget* InInstance)
		{
			CreatedInstance = InInstance;
			// Construct/enable handlers may reopen the menu. Publish before they run so that
			// the nested open can reuse the instance instead of building another over it.
			if (IsCurrentTransition(WeakThis, InOpenSerial, true))
			{
				WeakThis->MenuInstance = InInstance;
			}
		});
	UDreamUserWidget* Created = CreatedInstance.Get();
	if (!IsCurrentTransition(WeakThis, InOpenSerial, true))
	{
		// A superseded creation that nobody adopted is still ours to dispose of. An instance
		// already published may now be the nested open's content, and must be left alone.
		if (IsValid(Created) && (!WeakThis.IsValid() || WeakThis->MenuInstance != Created))
		{
			Created->DestroyWidget();
		}
		return;
	}
	if (IsValid(Created))
	{
		MenuInstance = Created;
		Created->SetDisplayName(TEXT("MenuContent"));
		if (UDreamPanelSlot* MenuSlot = Created->GetPanelSlot())
		{
			MenuSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			MenuSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
		}
	}
}

void UDreamMenuAnchor::Open(bool bFocusMenu)
{
	// Disable closes the menu, whose closed callback may immediately try to reopen it. Refuse
	// that request while inactive so the popup cannot outlive the screen that opened it.
	if (!IsValid(this) || !GetWidgetActiveInHierarchy() || bIsOpen || !IsValid(PopupNode))
	{
		return;
	}
	const TWeakObjectPtr<UDreamMenuAnchor> WeakThis(this);
	const uint64 OpenSerial = ++MenuTransitionSerial;
	bIsOpen = true;
	// A new open: the press that closed the last one, if one did, is nothing to this one.
	DismissingPress = nullptr;
	EnsureMenuInstance(OpenSerial);
	if (!IsCurrentTransition(WeakThis, OpenSerial, true))
	{
		return;
	}

	const FDreamMenuAnchorStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle);
	// The placement first, while the popup is still a child of the anchor: the lift keeps the world
	// position it finds, so positioning against the anchor and THEN lifting is what puts a menu in
	// the right place without the layer knowing anything about anchors.
	PlacePopup(Active);
	if (!IsCurrentTransition(WeakThis, OpenSerial, true))return;
	PopupNode->SetWidgetActive(true);
	if (!IsCurrentTransition(WeakThis, OpenSerial, true))return;

	bPopupElevated = false;
	bool bPushed = false;
	if (UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this))
	{
		FDreamPopupParams Params;
		Params.Popup = PopupNode;
		// This anchor: what the menu follows, what takes it down when it goes, and what decides its parent -- an
		// anchor inside an open menu opens a submenu of it. Not where focus comes back to, an anchor being nothing
		// focus sits on: that is whatever had focus when the menu opened.
		Params.Opener = this;
		Params.UserIndex = GetOwningPlayerIndex();
		Params.OutsideClick = ResolveOutsideClick();
		Params.bFocusOnOpen = bFocusMenu;
		Params.InitialFocus = bFocusMenu ? FindFirstMenuControl() : nullptr;
		// Tab leaves a menu as it leaves a dropdown's list: the whole chain closes, submenus and all, the focus comes back
		// to what had it when the menu opened, and the Tab goes on from this anchor.
		Params.TabBehavior = EDreamPopupTabBehavior::CloseAndContinue;
		Params.Place = FDreamPopupPlaceDelegate::CreateUObject(this, &UDreamMenuAnchor::PlaceLiftedPopup);
		Params.OnDismissed = FDreamPopupDismissedDelegate::CreateWeakLambda(this, [WeakThis, OpenSerial](UDreamWidget* InPopup, EDreamPopupDismissReason InReason)
		{
			// Returning focus may have reopened this very popup before the old dismissal is
			// announced. That notification belongs to the old open, never to its replacement.
			if (UDreamMenuAnchor* Anchor = WeakThis.Get(); Anchor != nullptr && Anchor->MenuTransitionSerial == OpenSerial && Anchor->bIsOpen)
			{
				Anchor->HandleMenuDismissed(InPopup, InReason);
			}
		});
		bPushed = Layer->Push(Params);
	}
	if (!IsCurrentTransition(WeakThis, OpenSerial, true))
	{
		// A close made while Push replaced another popup can precede the stack entry itself.
		// Take that late entry down only if no newer open owns it; never dismiss a reopened menu.
		if (WeakThis.IsValid() && !bIsOpen)
		{
			UDreamUIPopupLayer* Layer = bPushed ? UDreamUIPopupLayer::Get(this) : nullptr;
			if (Layer != nullptr && Layer->IsOpen(PopupNode))
			{
				Layer->Dismiss(PopupNode, EDreamPopupDismissReason::Explicit);
			}
		}
		return;
	}
	bPopupElevated = bPushed;
	if (!bPopupElevated)
	{
		// No layer to lift it to -- no screen root in this world: the menu opens in place and closing it is the
		// caller's job, but focus a player moves into it still comes back when it closes.
		FallbackFocusReturn.Capture(this);
		if (bFocusMenu)
		{
			FocusMenuContent();
		}
		if (!IsCurrentTransition(WeakThis, OpenSerial, true))
		{
			// The same, for the focus moved here.
			return;
		}
	}

	// Asked again rather than held across the push: a handler it ran may have restyled this anchor.
	const float TransitionDuration = ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle).TransitionDuration;
	// Every open fades in from clear, as every push of a Slate menu is a new window that starts transparent
	// (MenuStack.cpp:490-503): a fade an earlier open started is stopped first. Left running, it went on writing the
	// opacity it had reached, and the new fade -- which reads where it starts from on its first step -- took that up and
	// came in from nearly opaque.
	StopOpenFade();
	if (TransitionDuration > 0.0f)
	{
		PopupNode->SetRenderOpacity(0.0f);
		UDreamTweener* Fade = PopupNode->RenderOpacityTo(1.0f, TransitionDuration, 0.0f, EDreamTweenEase::OutCubic);
		OpenFadeTweener = Fade;
		if (Fade == nullptr)
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
	if (!bOpenAnnounced)
	{
		// A close interrupted by reopening never completed its closed announcement.
		// The menu stayed announced as open; there is no second change to publish.
		bOpenAnnounced = true;
		OnMenuOpenChanged.Broadcast(true);
	}
}

UDreamWidget* UDreamMenuAnchor::FindFirstMenuControl()
{
	if (PopupNode == nullptr || GetWorld() == nullptr)
	{
		// No world means no event system -- an initialize-time open, or a headless test.
		return nullptr;
	}
	UUISelectable* First = UUISelectable::FindDefaultSelectableIn(this, PopupNode);
	return First != nullptr ? First->GetWidget() : nullptr;
}

void UDreamMenuAnchor::FocusMenuContent()
{
	UDreamWidget* First = FindFirstMenuControl();
	if (First == nullptr)
	{
		// A menu with nothing navigable in it (a tooltip, a picture) keeps focus where it is rather
		// than dropping it somewhere arbitrary. Same rule, same words, as UDreamTabView's.
		return;
	}
	// The player whose menu it is, not player 0 -- and the navigation cursor with the focus, so the next stick
	// press walks the menu rather than starting from wherever the cursor was left.
	if (UDreamUIInputServices* Services = UDreamUIInputServices::Get(this))
	{
		Services->FocusForNavigation(First, GetOwningPlayerIndex());
	}
}

bool UDreamMenuAnchor::HasOpenSubMenus() const
{
	if (PopupNode == nullptr)
	{
		return false;
	}
	TArray<UDreamWidget*> Inside;
	UDreamWidget::CollectChildrenWidgets(PopupNode, Inside, /*IncludeTarget*/false);
	for (UDreamWidget* Child : Inside)
	{
		// A submenu is another anchor of this class inside the content, so the question is a search
		// for one that is open -- there is no application-wide menu stack here to ask instead.
		if (const UDreamMenuAnchor* Sub = Cast<UDreamMenuAnchor>(Child); Sub != nullptr && Sub->IsOpen())
		{
			return true;
		}
	}
	return false;
}

void UDreamMenuAnchor::FitInWindow(bool bInFitInWindow)
{
	if (bFitInWindow == bInFitInWindow)
	{
		return;
	}
	bFitInWindow = bInFitInWindow;
	if (bIsOpen)
	{
		// An open menu moves now, for SetPlacement's reason: where the popup sits is placement work,
		// and a clamp that waited for the next open would read as a switch that does nothing.
		RefreshOpenPlacement();
	}
}

void UDreamMenuAnchor::Close()
{
	if (!IsValid(this) || !bIsOpen)
	{
		return;
	}
	const TWeakObjectPtr<UDreamMenuAnchor> WeakThis(this);
	const uint64 CloseSerial = ++MenuTransitionSerial;
	bIsOpen = false;
	if (IsValid(PopupNode))
	{
		UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this);
		if (bPopupElevated && Layer != nullptr && Layer->IsOpen(PopupNode))
		{
			// The layer's close, in its order: every menu opened from inside this one, then the focus that went
			// into it back where it was when it opened, then home under the anchor -- all before it is put to
			// sleep, which would otherwise clear that focus first. HandleMenuDismissed finds this anchor closed.
			Layer->Dismiss(PopupNode, EDreamPopupDismissReason::Explicit);
		}
		else
		{
			FallbackFocusReturn.Return(PopupNode);
		}
	}
	if (IsCurrentTransition(WeakThis, CloseSerial, false))
	{
		FinishClose(CloseSerial);
	}
}

void UDreamMenuAnchor::HandleMenuDismissed(UDreamWidget* InPopup, EDreamPopupDismissReason InReason)
{
	if (!bIsOpen)
	{
		// Close's own dismissal.
		return;
	}
	// Closed from outside -- a press elsewhere, Back, the menu this one was opened from closing, a menu opened in
	// its place, this anchor hidden or put to sleep. The layer has given focus back and the popup home already.
	const uint64 CloseSerial = ++MenuTransitionSerial;
	bIsOpen = false;
	if (InReason == EDreamPopupDismissReason::OutsideClick)
	{
		// The press that did it, whose click must not open the menu again (ShouldOpenDueToClick).
		UDreamPointerEventData* Press = DreamMenuAnchorLocal::FindPressGoingDown(this, GetOwningPlayerIndex());
		DismissingPress = Press;
		DismissingPressTime = Press != nullptr ? Press->PressTime : 0.0;
	}
	if (InReason == EDreamPopupDismissReason::WorldTeardown)
	{
		// The world comes down with the popup in it: nothing was put back, and there is nobody to tell.
		bPopupElevated = false;
		bOpenAnnounced = false;
		return;
	}
	FinishClose(CloseSerial);
}

void UDreamMenuAnchor::FinishClose(uint64 InCloseSerial)
{
	const TWeakObjectPtr<UDreamMenuAnchor> WeakThis(this);
	if (!IsCurrentTransition(WeakThis, InCloseSerial, false))return;
	bPopupElevated = false;
	// A closed menu is not fading in: the fade stops with the close, where it had got.
	StopOpenFade();
	if (IsValid(PopupNode))
	{
		// Home already -- the layer puts a closing popup back before it says so -- and now asleep. A popup put to
		// sleep while still lifted would be an inactive widget hanging off the screen root that nothing would ever
		// come back for.
		PopupNode->SetWidgetActive(false);
		if (!IsCurrentTransition(WeakThis, InCloseSerial, false))return;
		// The whole resting scheme, not just the numbers: the lift re-anchored the popup to a POINT on
		// the screen root and the trip home reparents plainly, so without this the next open works against
		// the wrong anchors. UDreamDropdown measured that one as a zero-width list on the second open.
		PlacePopup(ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle));
	}
	if (!IsCurrentTransition(WeakThis, InCloseSerial, false))return;
	// Only after an open that was announced: one that closed again before Open finished announced nothing, and its close
	// announces nothing either -- a listener never hears closed without having heard open.
	if (bOpenAnnounced)
	{
		bOpenAnnounced = false;
		OnMenuOpenChanged.Broadcast(false);
	}
}

void UDreamMenuAnchor::SetStyle(const FDreamMenuAnchorStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

EDreamMenuPlacement UDreamMenuAnchor::GetPlacement() const
{
	// The style in EFFECT, so an anchor driven by the project sheet answers the placement it opens
	// with rather than whatever this instance carries underneath the sheet.
	return ResolveStyle(Style, &UDreamUIStyleSheet::MenuAnchorStyle).Placement;
}

void UDreamMenuAnchor::SetPlacement(EDreamMenuPlacement InPlacement)
{
	Style.Placement = InPlacement;
	ApplyStyle();
	if (bIsOpen)
	{
		// An open menu moves now. ApplyStyle pushes the look; where the popup SITS is placement work,
		// and a menu that stayed put until it was closed and opened again would read as a knob that
		// does nothing. Lifted, it is placed in the plane it was lifted to rather than against this
		// anchor's rect, which it no longer hangs in.
		RefreshOpenPlacement();
	}
}

FVector2D UDreamMenuAnchor::GetMenuPosition() const
{
	return PopupNode != nullptr ? PopupNode->GetAnchoredPosition() : FVector2D::ZeroVector;
}

void UDreamMenuAnchor::SetMenuClass(TSubclassOf<UDreamUserWidget> InMenuClass)
{
	if (MenuClass == InMenuClass)
	{
		return;
	}
	if (bIsOpen)
	{
		// Swapping what the player is reading is not something to do quietly; closing first makes the
		// change something they can see happen.
		Close();
	}
	MenuClass = InMenuClass;
	if (MenuInstance != nullptr)
	{
		// The instance is built once and kept, so an old one left here would be the menu this anchor
		// keeps opening no matter what the class now says.
		MenuInstance->DestroyWidget();
		MenuInstance = nullptr;
	}
}

void UDreamMenuAnchor::SetMenuSize(FVector2D InMenuSize)
{
	MenuSize = InMenuSize;
	if (bIsOpen)
	{
		RefreshOpenPlacement();
	}
}

void UDreamMenuAnchor::SetCloseOnClickOutside(bool bInCloseOnClickOutside)
{
	bCloseOnClickOutside = bInCloseOnClickOutside;
	if (!bIsOpen || !bPopupElevated)
	{
		return;
	}
	// While open, the switch is about the press that comes next.
	if (UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this))
	{
		Layer->SetOutsideClick(PopupNode, ResolveOutsideClick());
	}
}

void UDreamMenuAnchor::ToggleOpen(bool bFocusOnOpen)
{
	if (bIsOpen)
	{
		Close();
	}
	else
	{
		Open(bFocusOnOpen);
	}
}

void UDreamMenuAnchor::NativeOnDisable()
{
	// A menu lifted to the screen root is not under this anchor, and does not go to sleep with it: an anchor put to
	// sleep -- or an ancestor of it -- would otherwise leave its menu up, answering for an anchor nobody can see.
	Close();
	Super::NativeOnDisable();
}

void UDreamMenuAnchor::NativeOnDestruct()
{
	// An anchor torn down while its menu is open would otherwise leave a lifted popup on the screen root
	// with nothing left that could close it -- which is the stranded-popup failure UUIDropdown was
	// fixed for.
	Close();
	Super::NativeOnDestruct();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "MenuAnchor", UDreamMenuAnchor)
