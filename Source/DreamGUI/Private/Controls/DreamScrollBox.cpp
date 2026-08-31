// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamScrollBox.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"

void UDreamScrollBox::NativeOnInitialized()
{
	Super::NativeOnInitialized();

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
		Node<UDreamRectBlock>("Face").Out(FaceNode)
			.Stretch()
			.Self([](UDreamWidget& InFace)
			{
				// The face carries the rounded silhouette, so it has to be what cuts content off at it.
				InFace.SetClipping(EDreamWidgetClipping::ClipToBounds);
			})
			.Children(
				Node<UDreamRectBlock>("Viewport").Out(ViewportNode)
					// A rect, not a bare Widget: the viewport is the drag surface, and only something
					// that draws is raycast against. It is tinted away in ApplyStyle -- a transparent
					// rect still hits, because hit testing is the rect range and not the pixels.
					.Stretch()
					.Self([](UDreamWidget& InViewport)
					{
						InViewport.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					.With<UUIScrollView>([](UUIScrollView& InScroll)
					{
						// Both axes, explicitly, from the first moment: the behaviour ships with BOTH
						// on, so a zero-config view drifts sideways the first time a drag lands even
						// though nothing in the box scrolls that way. ApplyStyle re-states them from
						// Orientation; this is the build-time floor.
						InScroll.SetHorizontal(false);
						InScroll.SetVertical(true);
						// The content is layout-managed and this control writes its rect, so the
						// scroll offset has to live somewhere both parties read. In RelativeLocation
						// mode the view moves the widget without touching its anchored position, and
						// the next extent refresh would then restore a stale offset and snap the
						// content back to the top.
						InScroll.SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
					})
					.Children(
						Widget("Content").Out(ContentNode)
							.With<UDreamLayoutContainerStackBox>()),
				Nested<UDreamScrollBar>("ScrollBar").Out(ScrollBarNode))
			.Then([this](UDreamWidget& InRoot)
			{
				// See UDreamListView's twin: a consumer's layout writes this control's rect, and a
				// stretched child caches the span it resolved back when that rect was zero.
				GetDimensionChangedEvent().AddUObject(this, &UDreamScrollBox::HandleDimensionsChanged);
				ScrollView = ViewportNode != nullptr ? ViewportNode->GetComponent<UUIScrollView>() : nullptr;
				ContentStack = ContentNode != nullptr
					? Cast<UDreamLayoutContainerStackBox>(ContentNode->GetLayoutContainer())
					: nullptr;
				if (ScrollView != nullptr)
				{
					ScrollView->SetContent(ContentNode);
					ScrollView->GetOnValueChangedEvent().AddUObject(this, &UDreamScrollBox::HandleScrollViewChanged);
				}
				if (ScrollBarNode != nullptr)
				{
					// A nested user widget builds its own contents at Initialize, and nothing calls it
					// here: the walk that initializes nested widgets belongs to instancing a class
					// TEMPLATE, and a class that declares its hierarchy in code has no template to be
					// instanced from. Without this the bar is an empty node with no track and no
					// handle, and every push into it lands on nothing.
					ScrollBarNode->Initialize();
					// The bar owns the two-way link, so the box hands it the view and stops thinking
					// about scroll values -- one implementation whether the bar is this one or a
					// standalone bar somebody points at GetScrollView().
					ScrollBarNode->SetScrollView(ScrollView);
				}
			}));

	ApplyStyle();
}

void UDreamScrollBox::HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged)
{
	if (!bWidthChanged && !bHeightChanged)
	{
		return;
	}
	RepublishAnchors(ViewportNode);
	RepublishAnchors(ContentNode);
}

void UDreamScrollBox::ApplyStyle()
{
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
		ScrollView->SetScrollSensitivity(ScrollSensitivity);
		ScrollView->SetDecelerateRate(DecelerateRate);
	}

	// Measured before the gutter is decided and again after it is applied. The circle is real -- the
	// gutter shrinks the viewport, the viewport decides whether anything overflows, and overflow
	// decides the gutter -- so it is cut by answering with the sizes in hand and then re-stating the
	// content against the viewport that came out. UMG lives with the same one-notch wobble.
	RefreshContentExtent();

	const bool bBarVisible = ShouldShowScrollBar();
	const float Gutter = bBarVisible ? Active.Bar.Thickness : 0.0f;

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
		if (bHorizontal)
		{
			ScrollBarNode->SetPivot(FVector2D(0.5, 0.0));
			ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), false, false);
			ScrollBarNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(0.0, Active.Bar.Thickness));
		}
		else
		{
			ScrollBarNode->SetPivot(FVector2D(1.0, 0.5));
			ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), false, false);
			ScrollBarNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(Active.Bar.Thickness, 0.0));
		}
		// After its rect, never before: the bar reads its own track's live size to lay the handle out.
		ScrollBarNode->ApplyStyle();
		// Last of all, so a bar that is about to appear is already the right shape when it does.
		ScrollBarNode->SetWidgetActive(bBarVisible);
	}

	RefreshContentExtent();
	PushScrollProgress();
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
	PushScrollProgress();
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
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ScrollBox", UDreamScrollBox)
