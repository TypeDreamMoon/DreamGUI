// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamScrollBox.h"

#include "DreamGUI.h"
#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIScrollView.h"

const FName UDreamScrollBox::ContentSlotName(TEXT("Content"));

void UDreamScrollBox::PostLoad()
{
	Super::PostLoad();
	// ScrollSensitivity is LOCAL UNITS per notch now; it used to be documented as a multiplier on the
	// raw wheel axis and shipped at 1. The arithmetic never changed, so an asset carrying a
	// multiplier-era number still travels that many units a notch -- which reads as a broken wheel
	// rather than as a setting, and nothing said so. Five units is the ceiling under which a notch
	// cannot visibly move anything; the same number, for the same reason, as UUIScrollView's.
	if (ScrollSensitivity > 0.0f && ScrollSensitivity < 5.0f)
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("[%s].%d '%s' has ScrollSensitivity %.3f. That is LOCAL UNITS travelled per wheel notch now (it used to be a multiplier on the raw axis), so this is a wheel that barely moves. The library default is 40."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), ScrollSensitivity);
	}
}

void UDreamScrollBox::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Viewport"), ViewportNode);
	OutParts.Emplace(UDreamScrollBox::ContentSlotName, ContentNode);
	// ScrollBarNode is a UDreamScrollBar, not a UDreamWidget, so it cannot ride this list -- see
	// WireParts, which binds it by the same name.
}

void UDreamScrollBox::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The scroll behaviour sits on the VIEWPORT, not on the face, and that placement is the whole
	// reason this control needs no equivalent of UUIScrollViewWithScrollbar::CheckValidHit: a view
	// only accepts drags from inside its own widget, and the bar is the viewport's SIBLING. Hang the
	// bar under the viewport instead and every grab of the handle would scroll the content as well.
	//
	// The content is the view's one child, which is what makes the viewport its ContentParent -- the
	// rect the content slides inside. A stack container on it so children pile up in order, the way
	// UMG's scroll box does; the control authors the extent (see RefreshContentExtent).
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.Self([](UDreamWidget& InFace)
			{
				// The face carries the rounded silhouette, so it has to be what cuts content off at it.
				InFace.SetClipping(EDreamWidgetClipping::ClipToBounds);
			})
			.Children(
				Node<UDreamRectBlock>("Viewport")
					// A rect, not a bare Widget: the viewport is the drag surface, and only something
					// that draws is raycast against. It is tinted away in ApplyStyle -- a transparent
					// rect still hits, because hit testing is the rect range and not the pixels.
					.Stretch()
					.Self([](UDreamWidget& InViewport)
					{
						InViewport.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					.Children(
						// The scrolled column, and this control's hole. It was reachable only from
						// code (AddContent) until it became a named slot: `Native.ScrollBox { ... }`
						// left its children hanging beside the control's own root, drawn nowhere and
						// scrolled by nothing. bAcceptsSeveral because the stack is already here --
						// a scroll box that took one child would be a scroll box nobody wants.
						Widget("Content")
							.With<UDreamLayoutContainerStackBox>()
							.With<UDreamNamedSlot>([](UDreamNamedSlot& InSlot)
							{
								InSlot.bAcceptsSeveral = true;
							})),
				Nested<UDreamScrollBar>("ScrollBar")));
}

void UDreamScrollBox::WireParts()
{
	// A resize changes the viewport, and the content's extent is measured against it. It no longer
	// has to re-publish anchors to make the stretched children agree: a parent's resolved size
	// invalidates its anchor-driven children at the source now (UDreamWidget::SetWidth), which
	// retired the per-frame watch this control kept -- and the designer oscillation with it.
	GetDimensionChangedEvent().AddUObject(this, &UDreamScrollBox::HandleDimensionsChanged);

	ScrollView = EnsureComponent<UUIScrollView>(ViewportNode);
	if (ScrollView != nullptr)
	{
		// The view's floor, set HERE rather than in the built-in tree: a template's viewport gets a
		// freshly added behaviour carrying the library defaults, and a knob that only the code tree
		// ever wrote is a knob the template road silently does without.
		//
		// Both axes explicitly, from the first moment: the behaviour ships with BOTH on, so a
		// zero-config view drifts sideways the first time a drag lands even though nothing in the box
		// scrolls that way. ApplyStyle re-states them from Orientation.
		ScrollView->SetHorizontal(false);
		ScrollView->SetVertical(true);
		// The content is layout-managed and this control writes its rect, so the scroll offset has to
		// live somewhere both parties read. In RelativeLocation mode the view moves the widget
		// without touching its anchored position, and the next extent refresh would then restore a
		// stale offset and snap the content back to the top.
		ScrollView->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
	}
	ContentStack = ContentNode != nullptr
		? Cast<UDreamLayoutContainerStackBox>(ContentNode->GetLayoutContainer())
		: nullptr;
	// The one part whose field is TYPED, so the generic list cannot carry it. Bound here, by the
	// same name the built-in tree gives it, and a template offering something that is not a scroll
	// bar leaves it null rather than half-bound.
	ScrollBarNode = Cast<UDreamScrollBar>(FindPart(TEXT("ScrollBar")));
	if (ScrollView != nullptr)
	{
		ScrollView->SetContent(ContentNode);
		ScrollView->GetOnValueChangedEvent().AddUObject(this, &UDreamScrollBox::HandleScrollViewChanged);
	}
	if (ScrollBarNode != nullptr)
	{
		// A nested user widget builds its own contents at Initialize, and nothing calls it here: the
		// walk that initializes nested widgets belongs to instancing a class TEMPLATE, and a class
		// that declares its hierarchy in code has no template to be instanced from. Without this the
		// bar is an empty node with no track and no handle, and every push into it lands on nothing.
		ScrollBarNode->Initialize();
		// The bar owns the two-way link, so the box hands it the view and stops thinking about
		// scroll values -- one implementation whether the bar is this one or a standalone bar
		// somebody points at GetScrollView().
		ScrollBarNode->SetScrollView(ScrollView);
	}
}

void UDreamScrollBox::HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged)
{
	if (!bWidthChanged && !bHeightChanged)
	{
		return;
	}
	// The content is never smaller than the window, so a window that just grew is a content rect
	// that has to grow with it -- and a scroll range that has to be re-stated either way.
	RefreshContentExtent();
}

void UDreamScrollBox::ApplyStyle()
{
	// RefreshContentExtent re-decides the bar by calling back into this push; the push decides it
	// itself, so for its length the re-measures it makes stay re-measures.
	TGuardValue<bool> StyleGuard(bApplyingStyle, true);
	const FDreamScrollBoxStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ScrollBoxStyle);
	const bool bHorizontal = IsHorizontal();

	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.BackgroundBrush);
	if (UDreamVisual* FaceVisual = FaceNode != nullptr ? FaceNode->GetVisual() : nullptr)
	{
		FaceVisual->SetColor(Active.Background);
	}
	if (UDreamVisual* ViewportVisual = ViewportNode != nullptr ? ViewportNode->GetVisual() : nullptr)
	{
		// The face already drew the background, gutter included. The viewport is here to clip and to
		// be grabbed, so it draws nothing of its own rather than a second slab over the first.
		ViewportVisual->SetColor(FColor(0, 0, 0, 0));
	}

	if (ScrollBarNode != nullptr)
	{
		// The bar wears the BOX's style, whole: FDreamScrollBoxStyle::Bar is an FDreamScrollBarStyle
		// for exactly this, so a project that restyles its scroll boxes restyles their bars with them.
		// Inline, because the sheet entry the bar would otherwise resolve is the standalone bar's.
		ScrollBarNode->StyleSource = EDreamUIStyleSource::Inline;
		ScrollBarNode->Style = Active.Bar;
		ScrollBarNode->Direction = bHorizontal
			? EUIScrollbarDirectionType::LeftToRight
			: EUIScrollbarDirectionType::TopToBottom;
	}

	if (ContentStack != nullptr)
	{
		ContentStack->SetOrientation(bHorizontal ? EDreamPanelOrientation::Horizontal : EDreamPanelOrientation::Vertical);
		// Padding belongs to the stack rather than to the viewport: it has to be part of what scrolls,
		// or the last row would slide out from under an inset that stayed behind.
		ContentStack->SetPadding(Active.Padding);
	}

	if (ScrollView != nullptr)
	{
		// Restated on every style push, not just at build: Orientation is an editable property, and
		// the axis it does not name has to be turned OFF or the view keeps whatever it had.
		ScrollView->SetHorizontal(bHorizontal);
		ScrollView->SetVertical(!bHorizontal);
		ScrollView->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
		// Once, not on every push: a second Add on the same handle would fire OnFocusUpdated twice
		// for one navigation press, which is the classic double-subscribe this library keeps hitting.
		if (!ContentFocusHandle.IsValid())
		{
			ContentFocusHandle = ScrollView->GetOnContentFocusMovedEvent()
				.AddUObject(this, &UDreamScrollBox::HandleContentFocusMoved);
		}
		PushScrollBehaviourSettings();
	}

	// Measured before the gutter is decided and again after it is applied. The circle is real -- the
	// gutter shrinks the viewport, the viewport decides whether anything overflows, and overflow
	// decides the gutter -- so it is cut by answering with the sizes in hand and then re-stating the
	// content against the viewport that came out. UMG lives with the same one-notch wobble.
	RefreshContentExtent();

	const bool bBarVisible = ShouldShowScrollBar();
	// The bar's margin is part of the gutter, because the gutter is "everything the viewport gives up
	// so the bar can sit there" -- a margin taken out of the bar's own thickness instead would make a
	// padded bar thinner rather than better spaced.
	const FMargin& BarPad = Active.Bar.BarPadding;
	const float BarSpan = Active.Bar.Thickness + (bHorizontal
		? BarPad.Top + BarPad.Bottom
		: BarPad.Left + BarPad.Right);
	// The groove left behind when the bar has nothing to say. It costs the same gutter, which is the
	// point: a list that gains one row must not reflow its content because a bar appeared beside it.
	const bool bShowTrackOnly = !bBarVisible && bAlwaysShowScrollbarTrack;
	const float Gutter = (bBarVisible || bShowTrackOnly) ? BarSpan : 0.0f;

	if (ViewportNode != nullptr)
	{
		// Stretched, deliberately, unlike the handle: the viewport has to track the box's live size on
		// every arrange, and a stretched axis is exactly how to say "the parent's span, less the
		// gutter" -- a SizeDelta on a stretched axis is the DIFFERENCE from that span, so the gutter
		// goes in negative and the position shifts by half of it to leave the opposite edge put.
		ViewportNode->SetPivot(FVector2D(0.5, 0.5));
		ViewportNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
		ViewportNode->SetAnchoredPositionAndSizeDelta(
			bHorizontal ? FVector2D(0.0, Gutter * 0.5) : FVector2D(-Gutter * 0.5, 0.0),
			bHorizontal ? FVector2D(0.0, -Gutter) : FVector2D(-Gutter, 0.0));
	}

	if (ScrollBarNode != nullptr)
	{
		// Along one edge: stretched on the bar's own axis so it always spans the box, a POINT anchor
		// with the pivot on that edge across it so the thickness is an absolute number pinned flush.
		// The margin is spent on the bar's rect rather than on the track's: the track has to reach both
		// ends of whatever the bar spans, or the handle would travel past it. Y runs UP here, which is
		// why the bottom inset is a POSITIVE shift and the top one a negative.
		if (bHorizontal)
		{
			ScrollBarNode->SetPivot(FVector2D(0.5, 0.0));
			ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), false, false);
			ScrollBarNode->SetAnchoredPositionAndSizeDelta(
				FVector2D((BarPad.Left - BarPad.Right) * 0.5, BarPad.Bottom),
				FVector2D(-(BarPad.Left + BarPad.Right), Active.Bar.Thickness));
		}
		else
		{
			ScrollBarNode->SetPivot(FVector2D(1.0, 0.5));
			ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), false, false);
			ScrollBarNode->SetAnchoredPositionAndSizeDelta(
				FVector2D(-BarPad.Right, (BarPad.Bottom - BarPad.Top) * 0.5),
				FVector2D(Active.Bar.Thickness, -(BarPad.Top + BarPad.Bottom)));
		}
		// After its rect, never before: the bar reads its own track's live size to lay the handle out.
		ScrollBarNode->ApplyStyle();
		// Last of all, so a bar that is about to appear is already the right shape when it does.
		ScrollBarNode->SetWidgetActive(bBarVisible || bShowTrackOnly);
		if (ScrollBarNode->HandleNode != nullptr)
		{
			// Track only means exactly that: the groove stays, the thumb goes. Written here rather
			// than left to the bar's own auto-hide, because it is the BOX that knows whether this bar
			// has anything to say -- the bar is wearing the box's style and following the box's view.
			ScrollBarNode->HandleNode->SetWidgetActive(!bShowTrackOnly);
		}
	}

	RefreshContentExtent();
	{
		// The style push is this control moving its own content, not the player: the progress it
		// re-states is the one that was already there.
		FScopedProgrammaticScroll Guard(*this);
		PushScrollProgress();
	}

	// Announced after the bar is actually in its new state, and only on a change -- a style push runs
	// for every property edit, and a consumer that heard "still visible" on each of them would have to
	// filter the event this control is better placed to filter.
	if (bScrollBarWasVisible != bBarVisible)
	{
		bScrollBarWasVisible = bBarVisible;
		OnScrollBarVisibilityChanged.Broadcast(bBarVisible);
	}
}

void UDreamScrollBox::RefreshFocusTarget()
{
	UDreamWidget* Face = FaceNode;
	if (Face == nullptr)
	{
		return;
	}
	if (!GetIsFocusable())
	{
		// Nothing is made, and nothing already made is destroyed: a behaviour here is a plain UObject
		// with no enable switch, so the gate is in the HANDLER instead. Destroying it would mark the
		// outliner dirty, which the designer answers with a full details rebuild -- an author
		// toggling the checkbox would pay that twice per click.
		return;
	}
	if (FocusSelectable == nullptr)
	{
		FocusSelectable = Face->AddComponent<UUISelectable>();
		if (FocusSelectable == nullptr)
		{
			return;
		}
		// The selectable is what the event system hands focus to, and what already tells a resting
		// pointer from focus -- so its state IS received and lost, with nothing else to subscribe to.
		FocusSelectable->GetOnSelectionStateChangedEvent().AddUObject(
			this, &UDreamScrollBox::HandleFaceSelectionStateChanged);
	}
}

void UDreamScrollBox::HandleFaceSelectionStateChanged(EUISelectableSelectionState InState, bool /*bInImmediate*/)
{
	// The gate lives here because the selectable cannot be switched off: a box whose bIsFocusable was
	// turned back off keeps the behaviour (destroying it costs the designer a details rebuild) but
	// stops announcing, which is what "not a focus target" has to mean from outside.
	if (!GetIsFocusable())
	{
		return;
	}
	// The EDGE, not the state: the selectable re-applies its state for a repaint as well as for a
	// change, and a box that announced "focused" on every repaint would fire dozens of times for one
	// press.
	const bool bFocused = InState == EUISelectableSelectionState::Focused;
	if (bFocused == bWasFocused)
	{
		return;
	}
	bWasFocused = bFocused;
	// Through the base's own notify rather than a broadcast of our own: it is the same event every
	// other widget raises, and it carries the accessibility announcement with it. The selectable's
	// state change says nothing about WHICH pointer, so that half of the payload is honestly empty.
	const int32 FocusUserIndex = FMath::Max(GetOwningPlayerIndex(), 0);
	if (bFocused)
	{
		NotifyFocusReceived(FocusUserIndex, INDEX_NONE);
	}
	else
	{
		NotifyFocusLost(FocusUserIndex, INDEX_NONE);
	}
}

void UDreamScrollBox::HandleContentFocusMoved(UDreamWidget* InFocusedWidget)
{
	// The box's own face reaching here is not "focus moved INSIDE the box" -- that is the box taking
	// focus, which OnFocusReceived already said. Everything else under it is content.
	if (InFocusedWidget != nullptr && InFocusedWidget != FaceNode && InFocusedWidget != this)
	{
		OnFocusUpdated.Broadcast(InFocusedWidget);
	}
}

void UDreamScrollBox::PushScrollBehaviourSettings()
{
	RefreshFocusTarget();
	if (ScrollView == nullptr)
	{
		return;
	}
	// One list, called from the style push and from every setter, because the TEMPLATE road gives
	// this control a view it just added -- carrying the library's defaults rather than what the
	// control was authored with. A knob that only the setter pushed would be a knob a template-built
	// box silently did without.
	ScrollView->SetScrollSensitivity(ScrollSensitivity);
	ScrollView->SetDecelerateRate(DecelerateRate);
	ScrollView->SetAnimateWheelScrolling(bAnimateWheelScrolling);
	ScrollView->SetWheelScrollAnimationDuration(WheelScrollAnimationDuration);
	ScrollView->SetWheelScrollMultiplier(WheelScrollMultiplier);
	ScrollView->SetConsumeMouseWheel(ConsumeMouseWheel);
	ScrollView->SetAllowOverscroll(bAllowOverscroll);
	ScrollView->SetBackPadScrolling(bBackPadScrolling);
	ScrollView->SetFrontPadScrolling(bFrontPadScrolling);
	ScrollView->SetEnableTouchScrolling(bEnableTouchScrolling);
	ScrollView->SetAllowRightClickDragScrolling(bAllowRightClickDragScrolling);
	ScrollView->SetConsumePointerInput(bConsumePointerInput);
	ScrollView->SetAnalogMouseWheelKey(AnalogMouseWheelKey);
	ScrollView->SetScrollWhenFocusChanges(ScrollWhenFocusChanges);
	ScrollView->SetNavigationDestination(NavigationDestination);
	ScrollView->SetNavigationScrollPadding(NavigationScrollPadding);
}

void UDreamScrollBox::SetStyle(const FDreamScrollBoxStyle& InStyle)
{
	Style = InStyle;
	// The whole look, and the geometry that follows from it: the bar's thickness and margin are the
	// gutter, so a style write is a re-layout rather than a repaint.
	ApplyStyle();
}

float UDreamScrollBox::GetScrollProgress() const
{
	if (ScrollView != nullptr)
	{
		const FVector2D Progress = ScrollView->GetScrollProgress();
		return static_cast<float>(IsHorizontal() ? Progress.X : Progress.Y);
	}
	return ScrollProgress;
}

void UDreamScrollBox::SetScrollProgress(float InProgress)
{
	ScrollProgress = InProgress;
	// Code moving the content, so the event that comes back out is not the player's.
	FScopedProgrammaticScroll Guard(*this);
	PushScrollProgress();
}

void UDreamScrollBox::SetOrientation(EDreamPanelOrientation InOrientation)
{
	if (Orientation == InOrientation)
	{
		return;
	}
	Orientation = InOrientation;
	// The whole style: the axis decides which way the view scrolls, which way the stack piles, which
	// edge the bar sits on and where the gutter is cut -- all of it written in ApplyStyle.
	ApplyStyle();
}

void UDreamScrollBox::SetShowScrollBar(bool bInShowScrollBar)
{
	if (bShowScrollBar == bInShowScrollBar)
	{
		return;
	}
	bShowScrollBar = bInShowScrollBar;
	// The gutter moves with the bar, so this is a re-layout and not a visibility flip.
	ApplyStyle();
}

void UDreamScrollBox::SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility InVisibility)
{
	if (ScrollBarVisibility == InVisibility)
	{
		return;
	}
	ScrollBarVisibility = InVisibility;
	ApplyStyle();
}

void UDreamScrollBox::SetScrollSensitivity(float InSensitivity)
{
	ScrollSensitivity = InSensitivity;
	if (ScrollView != nullptr)
	{
		// Straight to the behaviour: nothing about the box's geometry depends on it, so a whole style
		// push would be work with one line of effect.
		ScrollView->SetScrollSensitivity(InSensitivity);
	}
}

void UDreamScrollBox::SetDecelerateRate(float InRate)
{
	DecelerateRate = InRate;
	if (ScrollView != nullptr)
	{
		ScrollView->SetDecelerateRate(InRate);
	}
}

void UDreamScrollBox::SetAnimateWheelScrolling(bool bInAnimate)
{
	bAnimateWheelScrolling = bInAnimate;
	if (ScrollView != nullptr)
	{
		// Straight to the behaviour: the wheel's feel is spent where a notch becomes a position, and
		// nothing about the box's geometry depends on it.
		ScrollView->SetAnimateWheelScrolling(bInAnimate);
	}
}

void UDreamScrollBox::SetNavigationDestination(EDreamUIScrollDestination InDestination)
{
	NavigationDestination = InDestination;
	if (ScrollView != nullptr)
	{
		ScrollView->SetNavigationDestination(InDestination);
	}
}

void UDreamScrollBox::SetNavigationScrollPadding(float InPadding)
{
	NavigationScrollPadding = FMath::Max(0.0f, InPadding);
	if (ScrollView != nullptr)
	{
		ScrollView->SetNavigationScrollPadding(NavigationScrollPadding);
	}
}

void UDreamScrollBox::ScrollToStart()
{
	if (ScrollView != nullptr)
	{
		FScopedProgrammaticScroll Guard(*this);
		ScrollView->ScrollToStart();
	}
}

void UDreamScrollBox::ScrollToEnd()
{
	if (ScrollView != nullptr)
	{
		FScopedProgrammaticScroll Guard(*this);
		ScrollView->ScrollToEnd();
	}
}

bool UDreamScrollBox::ScrollWidgetIntoView(UDreamWidget* InWidget, bool bInAnimate,
	EDreamUIScrollDestination InDestination, float InPadding)
{
	if (ScrollView == nullptr)
	{
		return false;
	}
	FScopedProgrammaticScroll Guard(*this);
	// The duration stays the behaviour's default: UMG's box has no per-call duration either, and a
	// fifth parameter for it would be a number nobody at this level has an opinion about.
	return ScrollView->ScrollWidgetIntoView(InWidget, bInAnimate, 0.25f, InDestination, InPadding);
}

void UDreamScrollBox::EndInertialScrolling()
{
	if (ScrollView != nullptr)
	{
		ScrollView->EndInertialScrolling();
	}
}

bool UDreamScrollBox::GetIsScrolling() const
{
	return ScrollView != nullptr && ScrollView->IsScrolling();
}

float UDreamScrollBox::GetScrollOffsetOfEnd() const
{
	if (ScrollView == nullptr)
	{
		return 0.0f;
	}
	// The extent IS the offset at which the end is in view: the offset runs from zero to it.
	const FVector2D Extent = ScrollView->GetScrollableExtent();
	return static_cast<float>(IsHorizontal() ? Extent.X : Extent.Y);
}

float UDreamScrollBox::GetViewFraction() const
{
	if (ScrollView == nullptr)
	{
		return 1.0f;
	}
	const bool bHorizontal = IsHorizontal();
	const double Viewport = bHorizontal ? ScrollView->GetViewportSize().X : ScrollView->GetViewportSize().Y;
	const double Content = bHorizontal ? ScrollView->GetContentSize().X : ScrollView->GetContentSize().Y;
	// Content that fits shows all of itself, which is one -- not a fraction over a smaller number.
	if (Content <= Viewport || Content <= UE_SMALL_NUMBER)
	{
		return 1.0f;
	}
	return static_cast<float>(FMath::Clamp(Viewport / Content, 0.0, 1.0));
}

float UDreamScrollBox::GetViewOffsetFraction() const
{
	// The progress the behaviour already maintains: offset over extent, zero on an axis with nowhere
	// to go. A second division here would be the same arithmetic in a second place.
	return GetScrollProgress();
}

float UDreamScrollBox::GetOverscrollOffset() const
{
	if (ScrollView == nullptr)
	{
		return 0.0f;
	}
	const FVector2D Overscroll = ScrollView->GetOverscrollOffset();
	return static_cast<float>(IsHorizontal() ? Overscroll.X : Overscroll.Y);
}

float UDreamScrollBox::GetOverscrollPercentage() const
{
	if (ScrollView == nullptr)
	{
		return 0.0f;
	}
	const FVector2D Percentage = ScrollView->GetOverscrollPercentage();
	return static_cast<float>(IsHorizontal() ? Percentage.X : Percentage.Y);
}

float UDreamScrollBox::GetScrollbarThickness() const
{
	// The RESOLVED style, not the inline one: a box wearing the project sheet has a thickness the
	// sheet decided, and answering with the untouched inline field would describe a bar nobody sees.
	return ResolveStyle(Style, &UDreamUIStyleSheet::ScrollBoxStyle).Bar.Thickness;
}

void UDreamScrollBox::SetScrollbarThickness(float InThickness)
{
	Style.Bar.Thickness = FMath::Max(0.0f, InThickness);
	// Written onto the inline style AND switched to it: a thickness pushed at runtime is this
	// instance's own answer, and leaving StyleSource pointing at the sheet would mean the next style
	// push quietly threw the number away. The same move UMG's setter makes by having no sheet at all.
	StyleSource = EDreamUIStyleSource::Inline;
	ApplyStyle();
}

FMargin UDreamScrollBox::GetScrollbarPadding() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ScrollBoxStyle).Bar.BarPadding;
}

void UDreamScrollBox::SetScrollbarPadding(FMargin InPadding)
{
	Style.Bar.BarPadding = InPadding;
	StyleSource = EDreamUIStyleSource::Inline;
	ApplyStyle();
}

void UDreamScrollBox::SetAlwaysShowScrollbar(bool bInAlwaysShow)
{
	SetScrollBarVisibility(bInAlwaysShow
		? EDreamScrollBoxScrollbarVisibility::Permanent
		: EDreamScrollBoxScrollbarVisibility::AutoHide);
}

void UDreamScrollBox::SetAlwaysShowScrollbarTrack(bool bInAlwaysShow)
{
	if (bAlwaysShowScrollbarTrack == bInAlwaysShow)
	{
		return;
	}
	bAlwaysShowScrollbarTrack = bInAlwaysShow;
	// The gutter moves with it, so this is a re-layout and not a visibility flip.
	ApplyStyle();
}

void UDreamScrollBox::SetWheelScrollAnimationDuration(float InDuration)
{
	WheelScrollAnimationDuration = FMath::Max(0.0f, InDuration);
	if (ScrollView != nullptr)
	{
		ScrollView->SetWheelScrollAnimationDuration(WheelScrollAnimationDuration);
	}
}

void UDreamScrollBox::SetWheelScrollMultiplier(float InMultiplier)
{
	WheelScrollMultiplier = FMath::Max(0.0f, InMultiplier);
	if (ScrollView != nullptr)
	{
		ScrollView->SetWheelScrollMultiplier(WheelScrollMultiplier);
	}
}

void UDreamScrollBox::SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel InConsume)
{
	ConsumeMouseWheel = InConsume;
	if (ScrollView != nullptr)
	{
		ScrollView->SetConsumeMouseWheel(InConsume);
	}
}

void UDreamScrollBox::SetAllowOverscroll(bool bInAllow)
{
	bAllowOverscroll = bInAllow;
	if (ScrollView != nullptr)
	{
		ScrollView->SetAllowOverscroll(bInAllow);
	}
}

void UDreamScrollBox::SetBackPadScrolling(bool bInPad)
{
	bBackPadScrolling = bInPad;
	if (ScrollView != nullptr)
	{
		ScrollView->SetBackPadScrolling(bInPad);
		// The pad changes how far there is to go, so the bar's visible fraction moved and the
		// auto-hide answer may have too -- both of which are decided in the style push.
		ApplyStyle();
	}
}

void UDreamScrollBox::SetFrontPadScrolling(bool bInPad)
{
	bFrontPadScrolling = bInPad;
	if (ScrollView != nullptr)
	{
		ScrollView->SetFrontPadScrolling(bInPad);
		ApplyStyle();
	}
}

void UDreamScrollBox::SetEnableTouchScrolling(bool bInEnable)
{
	bEnableTouchScrolling = bInEnable;
	if (ScrollView != nullptr)
	{
		ScrollView->SetEnableTouchScrolling(bInEnable);
	}
}

void UDreamScrollBox::SetAllowRightClickDragScrolling(bool bInAllow)
{
	bAllowRightClickDragScrolling = bInAllow;
	if (ScrollView != nullptr)
	{
		ScrollView->SetAllowRightClickDragScrolling(bInAllow);
	}
}

void UDreamScrollBox::SetConsumePointerInput(bool bInConsume)
{
	bConsumePointerInput = bInConsume;
	if (ScrollView != nullptr)
	{
		ScrollView->SetConsumePointerInput(bInConsume);
	}
}

void UDreamScrollBox::SetAnalogMouseWheelKey(FKey InKey)
{
	AnalogMouseWheelKey = InKey;
	if (ScrollView != nullptr)
	{
		ScrollView->SetAnalogMouseWheelKey(InKey);
	}
}

void UDreamScrollBox::SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges InRule)
{
	ScrollWhenFocusChanges = InRule;
	if (ScrollView != nullptr)
	{
		ScrollView->SetScrollWhenFocusChanges(InRule);
	}
}

float UDreamScrollBox::GetScrollOffset() const
{
	if (ScrollView == nullptr)
	{
		return 0.0f;
	}
	const FVector2D Offset = ScrollView->GetScrollOffset();
	return static_cast<float>(IsHorizontal() ? Offset.X : Offset.Y);
}

void UDreamScrollBox::SetScrollOffset(float InOffset)
{
	if (ScrollView == nullptr)
	{
		return;
	}
	FScopedProgrammaticScroll Guard(*this);
	// The other axis is read back rather than zeroed: this control drives one, and the behaviour's
	// setter takes both at once -- the same shape PushScrollProgress uses.
	FVector2D Offset = ScrollView->GetScrollOffset();
	if (IsHorizontal())
	{
		Offset.X = InOffset;
	}
	else
	{
		Offset.Y = InOffset;
	}
	ScrollView->SetScrollOffset(Offset);
}

UDreamWidget* UDreamScrollBox::GetContentNode() const
{
	return ContentNode;
}

UUIScrollView* UDreamScrollBox::GetScrollView() const
{
	return ScrollView;
}

bool UDreamScrollBox::AddContent(UDreamWidget* InWidget)
{
	if (InWidget == nullptr || ContentNode == nullptr)
	{
		return false;
	}
	// Not keeping the world position: a widget joining a stack is being handed to a layout, and where
	// it used to sit is the layout's answer to give.
	if (!InWidget->TrySetParent(ContentNode, false))
	{
		return false;
	}
	RefreshContentExtent();
	return true;
}

void UDreamScrollBox::RefreshContentExtent()
{
	if (ContentNode == nullptr || ViewportNode == nullptr)
	{
		return;
	}
	const bool bHorizontal = IsHorizontal();
	const FVector2D ViewportSize(ViewportNode->GetWidth(), ViewportNode->GetHeight());
	FVector2D Extent = ViewportSize;
	if (ContentStack != nullptr)
	{
		// The stack's own measurement of its children, computed on demand rather than read from a
		// layout pass -- which is what lets an extent be re-taken the moment content is added instead
		// of a frame later. Never below the viewport: a content rect smaller than the window it sits
		// in has a negative scroll range, which reads as content that slides when it should not.
		const FVector2f Preferred = ContentStack->GetLayoutPreferredSize();
		if (bHorizontal)
		{
			Extent.X = FMath::Max(ViewportSize.X, static_cast<double>(Preferred.X));
		}
		else
		{
			Extent.Y = FMath::Max(ViewportSize.Y, static_cast<double>(Preferred.Y));
		}
	}

	// The scroll offset, read back and written again: this rewrite goes through the same anchored
	// position the behaviour scrolls with, so leaving it out would return the content to the top
	// every time anything was added to it.
	const FVector2D Offset = ContentNode->GetAnchoredPosition();
	// Both axes absolute, off the viewport's live arranged size, with the anchor collapsed to a POINT
	// on the edge the content hangs from -- the same rule the bar's handle follows and for the same
	// reason. A ratio anchor here would ask the setter to resolve the viewport's span at write time,
	// and the viewport is stretched, so on any frame that is not a full layout it would resolve
	// against a zero SizeDelta and hand the content a zero cross-axis size.
	const FVector2D Anchor = bHorizontal ? FVector2D(0.0, 0.5) : FVector2D(0.5, 1.0);
	ContentNode->SetPivot(Anchor);
	ContentNode->SetHorizontalAndVerticalAnchorMinMax(Anchor, Anchor, false, false);
	ContentNode->SetAnchoredPositionAndSizeDelta(Offset, Extent);

	if (ScrollView != nullptr)
	{
		ScrollView->RectRangeChanged();
	}
	if (ScrollBarNode != nullptr)
	{
		// A range change moves the visible fraction, and nothing broadcasts that.
		ScrollBarNode->RefreshFromScrollView();
	}

	// Whether the bar should be out is a question about exactly the measurement just taken, so it is
	// asked again HERE and not only in the style push: this is the call that hears the content change
	// -- AddContent, a caller re-measuring after adding or removing rows, the box being resized -- and
	// that is exactly when a box starts or stops overflowing. SScrollBar shows and hides itself on the
	// same change (its visibility follows the track's IsNeeded()); an auto-hiding bar decided once, at
	// Initialize, when a box built empty had nothing to scroll, stayed hidden for good. The whole push,
	// because the answer decides the viewport's gutter as well as the bar, and only when the answer
	// MOVED, so an ordinary re-measure costs nothing extra.
	if (!bApplyingStyle && ShouldShowScrollBar() != bScrollBarWasVisible)
	{
		ApplyStyle();
	}
}

void UDreamScrollBox::PushScrollProgress()
{
	if (ScrollView == nullptr)
	{
		return;
	}
	// The other axis is read back rather than zeroed: this control drives one, and SetScrollProgress
	// takes both at once.
	FVector2D Progress = ScrollView->GetScrollProgress();
	const float Clamped = FMath::Clamp(ScrollProgress, 0.0f, 1.0f);
	if (IsHorizontal())
	{
		Progress.X = Clamped;
	}
	else
	{
		Progress.Y = Clamped;
	}
	ScrollView->SetScrollProgress(Progress);
}

bool UDreamScrollBox::ShouldShowScrollBar() const
{
	if (!bShowScrollBar)
	{
		return false;
	}
	if (ScrollBarVisibility == EDreamScrollBoxScrollbarVisibility::Permanent)
	{
		return true;
	}
	if (ContentNode == nullptr || ViewportNode == nullptr)
	{
		return true;
	}
	const bool bHorizontal = IsHorizontal();
	const float Visible = bHorizontal ? ViewportNode->GetWidth() : ViewportNode->GetHeight();
	const float Total = bHorizontal ? ContentNode->GetWidth() : ContentNode->GetHeight();
	return Total > Visible + KINDA_SMALL_NUMBER;
}

void UDreamScrollBox::HandleScrollViewChanged(FVector2D InProgress)
{
	const float Axis = static_cast<float>(IsHorizontal() ? InProgress.X : InProgress.Y);
	ScrollProgress = Axis;
	// The bar follows the view through its own subscription, so there is nothing to push here -- only
	// the control-level re-broadcast a consumer binds to.
	OnScrolled.Broadcast(Axis);
	OnValueChangedBP.Broadcast(Axis);
	// UMG's OnUserScrolled, and the reason the guard exists: everything this control pushed itself
	// arrives here too, and after the fact the value cannot say which road it came down.
	if (!bPushingScroll)
	{
		OnUserScrolled.Broadcast(GetScrollOffset());
	}
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ScrollBox", UDreamScrollBox)
