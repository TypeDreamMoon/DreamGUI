// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamListView.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayoutSelfAuthoredSurface.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUIWidgetLibrary.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"

void UDreamListViewBase::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Viewport"), ViewportNode);
	OutParts.Emplace(TEXT("Column"), ColumnNode);
	OutParts.Emplace(TEXT("RowTemplate"), RowTemplateNode);
	// The stock row's label. A template whose row draws something else entirely is exactly the case
	// this feature exists for, so its absence is not an error -- DecorateRow null-checks.
	OutParts.Emplace(TEXT("RowLabel"), RowLabelNode, /*bRequired*/false);
	// ScrollBarNode is a UDreamScrollBar, not a UDreamWidget, so it cannot ride this list -- see
	// WireParts, which binds it by the same name.
}

void UDreamListViewBase::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The control's desired size is its AUTHORED size, not its scrolled content's: the column below
	// is as tall as ALL the rows -- that is the scroll range -- and without this the measure walk
	// hands exactly that to any Auto consumer, or (rebuilt rows, no text layout yet) echoes the
	// control's current height back as its desired one. Those are the two numbers the gallery
	// list's height flapped between in the designer (59 <-> 1299). A layout-self is the boundary
	// that fixes the MEASURE only: the invalidation walk still runs through it to the consumer,
	// where IgnoreLayout on the inner tree would break that walk and strand every inner dirty on a
	// container-less node -- 32 layout passes a frame, a details panel too busy to edit. The class
	// header tells that story in full.
	CreateNewLayoutSelf<UDreamLayoutSelfAuthoredSurface>();

	// UDreamScrollBox's anatomy, with a column of rows where its content stack would be: a face that
	// carries the look, a viewport clipped inside it holding the behaviour, the column that slides
	// within the viewport, and the bar along the viewport's edge.
	//
	// The behaviour is on the VIEWPORT and the bar is the viewport's SIBLING, both deliberately: a
	// scroll view accepts drags from anywhere inside its own widget, so a bar hung underneath it
	// would scroll the list every time somebody grabbed the handle.
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
					// that draws is raycast against. ApplyStyle tints it away -- a transparent rect
					// still hits, because hit testing is the rect range and not the pixels.
					.Stretch()
					.Self([](UDreamWidget& InViewport)
					{
						// Without the clip the rows past the visible count draw over whatever is under
						// the list: the behaviour moves content, it does not hide it.
						InViewport.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					.Children(
						// No layout container. Every row states its own rect from its index, which is
						// what makes the row height the style's number and the pool a window rather
						// than a wall -- see the class header.
						Widget("Column")
							// Top-anchored, stretch-X, its HEIGHT authored per rebuild: the column is
							// the scrolled content, so it has to be as tall as ALL the rows while the
							// viewport shows only what fits. A stretched vertical axis would pin it to
							// the viewport and nothing would ever scroll.
							.Anchors(FVector2D(0.0, 1.0), FVector2D(1.0, 1.0))
							.Self([](UDreamWidget& InColumn)
							{
								InColumn.SetPivot(FVector2D(0.5, 1.0));
								// The DELTA, not the width. SetWidth(0) on a stretched axis computes a
								// delta against the parent's span AT THIS MOMENT -- still the default
								// 100 here -- and bakes -100 in forever. A zero delta says "exactly
								// the span", whenever the span is decided. (UDreamDropdown measured
								// this one: a 300-wide column in a 400-wide list.)
								InColumn.SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D::ZeroVector);
							})
							.Children(
								Node<UDreamRectBlock>("RowTemplate")
									.Self([](UDreamWidget& InTemplate)
									{
										// The thing rows are copied from, not a row: asleep, so it
										// neither draws nor takes a place in the column's layout.
										InTemplate.SetWidgetActive(false);
									})
									// An overlay so the label (and the tree's twisty) have slots to be
									// aligned and padded in; a button so a row has a hover and a click --
									// the list's own, so a navigation press on a row is the list's to answer.
									.With<UDreamLayoutContainerOverlay>()
									.With<UDreamListRowButton>()
									.Children(
										DreamUI::Text("RowLabel")
											.Visual([](UDreamText& InText)
											{
												InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
												InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
											})
											.Slot([](UDreamPanelSlot& InSlot)
											{
												InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
												InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
												InSlot.SetPadding(UDreamListViewBase::GetRowPadding());
											})))),
				Nested<UDreamScrollBar>("ScrollBar")));
}

void UDreamListViewBase::WireParts()
{
	// A resize changes the gutter, the bar's shape and how many rows fit in the window. It no longer
	// has to re-publish anchors to make the stretched children agree: a parent's resolved size
	// invalidates its anchor-driven children at the source now (UDreamWidget::SetWidth), which is
	// what retired the per-frame watch this control used to run -- and the oscillation that watch
	// caused in the designer with it.
	GetDimensionChangedEvent().AddUObject(this, &UDreamListViewBase::HandleDimensionsChanged);

	ScrollBehaviour = EnsureComponent<UUIScrollView>(ViewportNode);
	if (ScrollBehaviour != nullptr)
	{
		// The view's floor, set HERE rather than in the built-in tree: a template's viewport gets a
		// freshly added behaviour carrying the library defaults, and a knob only the code tree ever
		// wrote is a knob the template road silently does without.
		//
		// Vertical only, explicitly, from the first moment: the behaviour ships with BOTH axes on,
		// and a zero-config scroll view drifts sideways the first time a drag lands even though
		// nothing in a list scrolls that way.
		ScrollBehaviour->SetHorizontal(false);
		ScrollBehaviour->SetVertical(true);
		// The column's height is rewritten on every rebuild. In RelativeLocation mode the view
		// scrolls by moving the widget WITHOUT touching its anchored position, so the next write of
		// the column's rect would restore a stale offset and snap the list back to the top on every
		// source change.
		ScrollBehaviour->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
	}
	// The one part whose field is TYPED, so the generic list cannot carry it. Bound here, by the
	// same name the built-in tree gives it.
	ScrollBarNode = Cast<UDreamScrollBar>(FindPart(TEXT("ScrollBar")));
	if (ScrollBehaviour != nullptr && ColumnNode != nullptr)
	{
		// What moves. The viewport is the window; the column is what slides behind it.
		ScrollBehaviour->SetContent(ColumnNode);
		// And what recycling listens to: the window is a function of the offset.
		ScrollBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamListViewBase::HandleScrollViewMoved);
		// The three touch events, forwarded from the drag the behaviour already handles rather than
		// from a second set of pointer subscriptions on this control: the behaviour is the one thing
		// that decides a gesture reaches these rows at all, and a list listening separately would
		// announce touches for drags the behaviour refused.
		ScrollBehaviour->GetOnDragGestureEvent().AddUObject(this, &UDreamListViewBase::HandleScrollGesture);
		// Focus arriving anywhere inside, raised by the navigation reveal. WireParts runs once per
		// control, so this subscribes once -- unlike the style push, which runs on every edit.
		ScrollBehaviour->GetOnContentFocusMovedEvent()
			.AddUObject(this, &UDreamListViewBase::HandleContentFocusMoved);
	}
	if (ScrollBarNode != nullptr)
	{
		// A nested user widget builds its own contents at Initialize, and nothing calls it here: the
		// walk that initializes nested widgets belongs to instancing a class TEMPLATE, and a class
		// that declares its hierarchy in code has no template to be instanced from. Without this the
		// bar is an empty node with no track and no handle, and every push into it lands on nothing.
		ScrollBarNode->Initialize();
		// The bar owns the two-way link, so the list hands it the view and stops thinking about
		// scroll values -- one implementation, shared with the scroll box.
		ScrollBarNode->SetScrollView(ScrollBehaviour);
	}
}

void UDreamListViewBase::OnPartsReady()
{
	// Before the first copy is taken. Whatever a subclass adds to the template has to be IN it by
	// the time rows are built, or the template and the rows disagree about what a row is.
	if (RowTemplateNode != nullptr)
	{
		DecorateRowTemplate(*RowTemplateNode);
	}
}

void UDreamListViewBase::ApplyStyle()
{
	const FDreamListStyle& Active = ResolveListStyle();

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
		// The bar wears the LIST's style, whole: FDreamListStyle::Bar is an FDreamScrollBarStyle for
		// exactly this, so a project that restyles its lists restyles their bars with them. Inline,
		// because the sheet entry the bar would otherwise resolve is the standalone bar's.
		ScrollBarNode->StyleSource = EDreamUIStyleSource::Inline;
		ScrollBarNode->Style = Active.Bar;
		ScrollBarNode->Direction = EUIScrollbarDirectionType::TopToBottom;
	}
	if (ScrollBehaviour != nullptr)
	{
		// Restated on every push, not just at build: these are the three the behaviour would happily
		// keep at its own defaults, and the axis it does NOT run on has to be turned off explicitly
		// or a zero-config view drifts sideways the first time a drag lands.
		const bool bHorizontalAxis = IsHorizontalList();
		ScrollBehaviour->SetHorizontal(bHorizontalAxis);
		ScrollBehaviour->SetVertical(!bHorizontalAxis);
		ScrollBehaviour->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
		// One notch is one row, which is the only sensitivity a list can state without guessing --
		// times the multiplier, for a long list where a row at a time is too slow. Folded into the
		// distance here rather than handed to the behaviour's own multiplier, because a list's unit
		// is a ROW and the behaviour would be scaling something else.
		ScrollBehaviour->SetScrollSensitivity(GetRowPitch() * FMath::Max(0.0f, WheelScrollMultiplier));
		PushScrollBehaviourSettings();
	}

	// The template only. Every row is a copy of it and every rebuild re-copies, so a style edit
	// reaches the rows by way of the thing they are made from. Rows are left unrounded on purpose:
	// they sit square against the list's own rounded edge, the way the dropdown's items do.
	SkinFace(RowTemplateNode, Active.RowBrush);
	if (RowTemplateNode != nullptr)
	{
		// A rect block states no size of its own, so this is what an Auto measure would read of it.
		RowTemplateNode->SetHeight(Active.RowHeight);
	}
	if (UDreamText* LabelVisual = RowLabelNode != nullptr ? Cast<UDreamText>(RowLabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetColor(Active.TextColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}

	// Row geometry and row colour are both style, so there is no re-styling without rebuilding.
	RebuildRows();
}

float UDreamListViewBase::GetRowPitch() const
{
	const FDreamListStyle& Active = ResolveListStyle();
	// Never zero: a pitch of zero would put every row on top of every other one and make the window
	// arithmetic divide by it.
	return FMath::Max(Active.RowHeight + Active.RowSpacing, KINDA_SMALL_NUMBER);
}

float UDreamListViewBase::GetMainPadStart(const FDreamListStyle& InStyle) const
{
	return IsHorizontalList() ? InStyle.Padding.Left : InStyle.Padding.Top;
}

float UDreamListViewBase::GetMainPadEnd(const FDreamListStyle& InStyle) const
{
	return IsHorizontalList() ? InStyle.Padding.Right : InStyle.Padding.Bottom;
}

float UDreamListViewBase::GetCrossPadStart(const FDreamListStyle& InStyle) const
{
	return IsHorizontalList() ? InStyle.Padding.Top : InStyle.Padding.Left;
}

float UDreamListViewBase::GetCrossPadEnd(const FDreamListStyle& InStyle) const
{
	return IsHorizontalList() ? InStyle.Padding.Bottom : InStyle.Padding.Right;
}

float UDreamListViewBase::GetViewportMainExtent() const
{
	if (ViewportNode == nullptr)
	{
		return 0.0f;
	}
	return IsHorizontalList() ? ViewportNode->GetWidth() : ViewportNode->GetHeight();
}

float UDreamListViewBase::GetViewportCrossExtent() const
{
	if (ViewportNode == nullptr)
	{
		return 0.0f;
	}
	return IsHorizontalList() ? ViewportNode->GetHeight() : ViewportNode->GetWidth();
}

float UDreamListViewBase::GetRowTopOffset(int32 InDisplayIndex) const
{
	// The LINE a row is on, not the row: with one column those are the same number, and with several
	// they are not -- which is the whole of what a tile view adds. "Top" is the name it was born
	// with; in a horizontal list it is the distance from the LEFT edge, and the arithmetic is the
	// same because it was always about the scroll axis rather than about down.
	const int32 Columns = FMath::Max(1, ResolveColumnCount());
	return GetMainPadStart(ResolveListStyle()) + (InDisplayIndex / Columns) * GetRowPitch();
}

int32 UDreamListViewBase::GetLineCount() const
{
	const int32 Columns = FMath::Max(1, ResolveColumnCount());
	return FMath::DivideAndRoundUp(VisibleItemIndices.Num(), Columns);
}

void UDreamListViewBase::RefreshContentHeight(const FDreamListStyle& InStyle)
{
	if (!IsValid(ColumnNode))
	{
		return;
	}
	// The column's extent ALONG THE SCROLL AXIS is the scroll range, stated rather than measured:
	// lines, gaps and the viewport's own inset. The column is point-anchored on that axis, so the
	// setter writes the SizeDelta straight through and leaves the anchored position -- where the
	// scroll offset lives -- alone. Which setter it is is the only thing orientation changes.
	const int32 LineCount = GetLineCount();
	const float Extent = GetMainPadStart(InStyle) + GetMainPadEnd(InStyle)
		+ LineCount * InStyle.RowHeight
		+ FMath::Max(0, LineCount - 1) * InStyle.RowSpacing;
	if (IsHorizontalList())
	{
		ColumnNode->SetWidth(Extent);
	}
	else
	{
		ColumnNode->SetHeight(Extent);
	}
}

int32 UDreamListViewBase::ResolveWindowSize() const
{
	const float ViewportMain = GetViewportMainExtent();
	// A viewport with no resolvable extent is one nothing has arranged and nobody authored -- the
	// first frame of a control a consumer's layout has not reached yet. A fixed window rather than a
	// guess of zero, because a zero-row window is a list that stays blank until something else
	// happens to resize it; the dimensions handler re-asks the moment there IS an answer.
	const int32 FromViewport = ViewportMain > KINDA_SMALL_NUMBER
		? FMath::CeilToInt(ViewportMain / GetRowPitch()) + 1
		: 16;
	// LINES times the column count: the arithmetic above is about how far there is to scroll, and a
	// line of a tile view holds several widgets.
	const int32 Lines = FMath::Max(1, FromViewport + FMath::Max(0, VirtualizationOverscan) * 2);
	return Lines * FMath::Max(1, ResolveColumnCount());
}

void UDreamListViewBase::RebuildRows()
{
	if (RebuildSuppressionDepth > 0)
	{
		// A batch edit is half applied. Rebuilding here would lay rows out against a source that is
		// about to change again, and -- worse -- fire OnRowReleased for items the caller is still in
		// the middle of moving. One rebuild happens on the way out instead.
		bRebuildRequestedWhileSuppressed = true;
		return;
	}
	if (!IsValid(ColumnNode) || !IsValid(RowTemplateNode))
	{
		return;
	}

	const FDreamListStyle& Active = ResolveListStyle();
	// Before the rows, not after: what a row is PAINTED as depends on whether it is selected, and an
	// index the source no longer answers to (or an anchor an author just wrote into the details
	// panel) has to be settled where every road into this control passes through.
	ReconcileSelection();
	CollectVisibleItemIndices(VisibleItemIndices);
	const int32 RowCount = VisibleItemIndices.Num();

	RefreshContentHeight(Active);

	// The threshold decision, taken here and nowhere else. Everything downstream reads bVirtualizing
	// rather than re-deciding, so the pool size and the bind loop cannot disagree about which list
	// this is.
	const bool bWasVirtualizing = bVirtualizing;
	bVirtualizing = RowCount > FMath::Max(0, VirtualizationThreshold);
	WindowStart = 0;

	// The pool is kept whenever it can be, and this is the most performance-critical decision in the
	// control -- not for the frame rate, but for the DESIGNER.
	//
	// Creating or destroying a widget marks the UI outliner dirty, and the designer answers an
	// outliner change by FORCE-REFRESHING the engine's details view (DreamWidgetBlueprintEditor.cpp,
	// where OnDreamUIWidgetOutlinerChanged is subscribed). That is a full property-tree rebuild --
	// measured at 18.7 ms for this class, plus ~20 ms of Slate slow-path repaint behind it. Since
	// ApplyStyle runs on EVERY PostEditChangeProperty, a pool that tore itself down each time made
	// every single click in the details panel cost ~40 ms. This control is the only one in the
	// library whose ApplyStyle touches the widget tree at all, which is exactly why it was the only
	// one that felt broken to edit.
	//
	// So the pool is re-BOUND instead, and BindRow pushes the whole look -- brush, radius, colours,
	// geometry -- rather than relying on the rows having been copied from a freshly styled template.
	// A teardown is kept for the two changes a rebind genuinely cannot carry: a different number of
	// widgets, and a different authored row class (whose instance lives inside the row and is made
	// once per pool row).
	const int32 WantedPoolSize = bVirtualizing ? FMath::Min(RowCount, ResolveWindowSize()) : RowCount;
	if (PoolRowTemplateClass != RowTemplateClass)
	{
		PoolRowTemplateClass = RowTemplateClass;
		ResizePool(0);
	}

	// And while recycling the pool GROWS here but never shrinks, which is the same designer argument
	// one step further in. The window size is Ceil(viewport / pitch), and the pitch is RowHeight +
	// RowSpacing -- so dragging the RowHeight slider moves the window, and a pool sized exactly to it
	// tore itself down and rebuilt on EVERY FRAME of that drag, at the ~40 ms a click costs above.
	// A spare row is parked by RefreshVisibleWindow and costs nothing; the trim happens on a real
	// resize (HandleDimensionsChanged), where the widget count is genuinely wrong rather than merely
	// generous. A list crossing the virtualization threshold still resizes exactly, because that is
	// a change of KIND -- one widget per item to a window of them -- and carrying two hundred spare
	// rows past it would be the opposite of what the threshold is for.
	const int32 PoolSize = (bVirtualizing && bWasVirtualizing)
		? FMath::Max(RowNodes.Num(), WantedPoolSize)
		: WantedPoolSize;

	// The template goes AWAKE for the duration. bWidgetActive is an ordinary property and the copy
	// inherits it, so duplicating a sleeping template yields a list of sleeping rows: present in the
	// tree, arranged by nobody, drawn by nobody. UUIDropdown::CreateListItems does the same dance.
	if (RowNodes.Num() != PoolSize)
	{
		RowTemplateNode->SetWidgetActive(true);
		ResizePool(PoolSize);
		RowTemplateNode->SetWidgetActive(false);
	}

	RefreshScrollFurniture(Active);
	RefreshVisibleWindow();
	// The batch boundary, and it IS the return of this function: this control rebuilds synchronously,
	// so there is no pending request for a later frame to complete and nothing else to wait for.
	OnRowsGenerated.Broadcast(RowNodes.Num());
}

void UDreamListViewBase::ResizePool(int32 InPoolSize)
{
	InPoolSize = FMath::Max(0, InPoolSize);
	for (int32 Index = RowNodes.Num() - 1; Index >= InPoolSize; --Index)
	{
		if (IsValid(RowNodes[Index]))
		{
			RowNodes[Index]->DestroyWidget();
		}
		RowNodes.RemoveAt(Index);
		RowSourceIndices.RemoveAt(Index);
	}
	while (RowNodes.Num() < InPoolSize)
	{
		const int32 PoolIndex = RowNodes.Num();
		UDreamWidget* Row = CreatePoolRow(PoolIndex);
		if (Row == nullptr)
		{
			// Duplication failed, and going round again would spin: stop with the pool short rather
			// than never returning.
			break;
		}
		RowNodes.Add(Row);
		RowSourceIndices.Add(INDEX_NONE);
	}
}

/**
 * The window the pool is showing, and the whole of what recycling is.
 *
 * Not virtualizing, the window is everything and this is one bind per item -- the same loop the
 * control has always run, reached by the same road, which is what keeps the two behaviours from
 * being two implementations.
 */
void UDreamListViewBase::RefreshVisibleWindow()
{
	if (RowNodes.Num() == 0)
	{
		return;
	}
	const FDreamListStyle& Active = ResolveListStyle();
	const int32 RowCount = VisibleItemIndices.Num();

	int32 FirstDisplayIndex = 0;
	if (bVirtualizing)
	{
		const float Offset = GetScrollOffset();
		// The first LINE whose far edge is still past the window's near one, less the overscan, times
		// the column count. Clamped so the LAST window is a full one rather than a short one with
		// blank rows after it -- and the clamp is allowed to land mid-line, because a row's place is
		// computed from its own display index rather than from where the window happens to start.
		const int32 Columns = FMath::Max(1, ResolveColumnCount());
		const int32 FirstVisibleLine = FMath::FloorToInt(FMath::Max(0.0f, Offset - GetMainPadStart(Active)) / GetRowPitch());
		const int32 FirstLine = FMath::Max(0, FirstVisibleLine - FMath::Max(0, VirtualizationOverscan));
		FirstDisplayIndex = FMath::Clamp(FirstLine * Columns,
			0, FMath::Max(0, RowCount - RowNodes.Num()));
	}
	WindowStart = FirstDisplayIndex;

	// The window that WAS, kept so the arrivals can be told from the stays: "this item came into
	// view" is an edge, and a consumer loading a thumbnail per row wants one call per arrival rather
	// than one per scroll. Swapped rather than rebuilt in place, so the old set survives the loop.
	TSet<int32> PreviousRealized = MoveTemp(RealizedItemIndices);
	RealizedItemIndices.Reset();

	for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		const int32 DisplayIndex = FirstDisplayIndex + PoolIndex;
		if (!VisibleItemIndices.IsValidIndex(DisplayIndex))
		{
			ParkRow(PoolIndex);
			continue;
		}
		const int32 ItemIndex = VisibleItemIndices[DisplayIndex];
		BindRow(PoolIndex, DisplayIndex, ItemIndex, Active);
		RealizedItemIndices.Add(ItemIndex);
	}

	// After every bind, not inside the loop: a handler that asks the list what else is on screen has
	// to be told the whole answer rather than however much of it had been written when it was called.
	for (int32 ItemIndex : RealizedItemIndices)
	{
		if (!PreviousRealized.Contains(ItemIndex))
		{
			OnItemScrolledIntoView.Broadcast(ItemIndex, GetRowWidget(ItemIndex), GetItemObject(ItemIndex));
		}
	}
}

void UDreamListViewBase::HandleScrollViewMoved(FVector2D InProgress)
{
	if (bVirtualizing)
	{
		RefreshVisibleWindow();
	}
	const float Offset = GetScrollOffset();
	if (bEnableShadowBrush)
	{
		// The rect is layout and is settled by the style push; WHICH of the two is awake is state and
		// changes with every move, so it is re-asked here and nowhere else.
		RefreshShadowBrushes(ResolveListStyle());
	}
	OnListViewScrolled.Broadcast(Offset, GetViewFraction());
	// "Finished" is a move that lands with nothing left to carry it -- one call per gesture rather
	// than one per frame of a flick, which is what a consumer loading thumbnails for what is now on
	// screen actually wants. A programmatic jump and a plain wheel notch are each such a move on their
	// own; a flick is one only on the frame its momentum runs out; a drag is never one until it ends.
	const bool bStillMoving = bScrollDragInProgress || (ScrollBehaviour != nullptr && ScrollBehaviour->IsScrolling());
	if (!bStillMoving)
	{
		OnListViewFinishedScrolling.Broadcast(Offset, GetViewFraction());
	}
}

UDreamDragDropOperation* UDreamListRowDragSource::CreateDragOperation_Implementation(UDreamPointerEventData* EventData)
{
	// The base builds it from this behaviour's own Payload/Tag/DragVisualClass, which the list has
	// already written for this pool slot -- so there is nothing to re-do here, only somebody to tell.
	UDreamDragDropOperation* Operation = Super::CreateDragOperation_Implementation(EventData);
	if (OwningList != nullptr)
	{
		OwningList->HandleRowDragDetected(PoolIndex, Operation);
	}
	return Operation;
}

bool UDreamListRowDragSource::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	// The switch is asked HERE because a behaviour is a plain UObject with no enable to flip: a list
	// that turned dragging back off keeps this component (destroying it costs the designer a details
	// rebuild) and refuses instead, which is what "off" has to mean from outside. Refusing lets the
	// press keep bubbling, so the scroll view underneath gets the gesture as it would have before.
	if (OwningList == nullptr || !OwningList->GetAllowDragging())
	{
		return true;
	}
	return Super::OnPointerBeginDrag_Implementation(EventData);
}

bool UDreamListRowDragSource::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	// The operation read BEFORE the base ends the drag: the base clears the pointer's operation, and
	// afterwards there is nothing left to say which drag just finished.
	UDreamDragDropOperation* Operation = EventData != nullptr ? EventData->DragOperation : nullptr;
	const bool bResult = Super::OnPointerEndDrag_Implementation(EventData);
	if (OwningList != nullptr)
	{
		OwningList->HandleRowDragEnded(Operation);
	}
	return bResult;
}

bool UDreamListRowDropTarget::CanAcceptDrop_Implementation(UDreamDragDropOperation* Operation)
{
	// The switch first, then the base's tag and payload filters: a list with dropping off is not a
	// target at all, whatever the operation carries.
	if (OwningList == nullptr || !OwningList->GetAllowDragDrop())
	{
		return false;
	}
	return Super::CanAcceptDrop_Implementation(Operation);
}

bool UDreamListRowDropTarget::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	// The zone is a question about this row's rect and the pointer, which is why it is answered here
	// rather than in the base: a drop target in general has no rows and no order to insert into.
	if (OwningList != nullptr && EventData != nullptr)
	{
		LastDropZone = OwningList->GetDropZoneForPoint(PoolIndex, EventData->GetWorldPointInPlane());
		if (!OwningList->HandleRowDrop(PoolIndex, EventData->DragOperation, LastDropZone))
		{
			// Refused: keep bubbling, so an outer target still gets its chance. The same answer the
			// base gives for a tag it does not want.
			return true;
		}
	}
	return Super::OnPointerDragDrop_Implementation(EventData);
}

EDreamItemDropZone UDreamListViewBase::ResolveDropZone(float InFractionAlongRow, float InEdgeFraction)
{
	// Clamped so a pointer slightly outside the row still answers, and so an edge fraction of a half
	// or more cannot make the two seams overlap and leave OntoItem unreachable.
	const float Edge = FMath::Clamp(InEdgeFraction, 0.0f, 0.5f);
	const float Fraction = FMath::Clamp(InFractionAlongRow, 0.0f, 1.0f);
	if (Fraction < Edge)
	{
		return EDreamItemDropZone::AboveItem;
	}
	if (Fraction > 1.0f - Edge)
	{
		return EDreamItemDropZone::BelowItem;
	}
	return EDreamItemDropZone::OntoItem;
}

EDreamItemDropZone UDreamListViewBase::GetDropZoneForPoint(int32 InPoolIndex, const FVector& InWorldPoint) const
{
	UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
	if (!IsValid(Row))
	{
		return EDreamItemDropZone::OntoItem;
	}
	// Into the row's own space, then along the MAIN axis: which screen direction that is depends on
	// the orientation, which is the whole reason the axis helpers exist. DreamGUI local space is
	// Y right, Z up -- and Z runs UP, so a vertical row's fraction counts DOWN from its top.
	const FVector Local = Row->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	const float Extent = IsHorizontalList() ? Row->GetWidth() : Row->GetHeight();
	if (Extent <= KINDA_SMALL_NUMBER)
	{
		return EDreamItemDropZone::OntoItem;
	}
	// The pivot is where local zero is, so the near edge is at minus pivot times extent.
	const FVector2D Pivot = Row->GetPivot();
	const float Along = IsHorizontalList()
		? static_cast<float>(Local.Y) + static_cast<float>(Pivot.X) * Extent
		: Extent - (static_cast<float>(Local.Z) + static_cast<float>(Pivot.Y) * Extent);
	return ResolveDropZone(Along / Extent);
}

void UDreamListViewBase::RefreshRowDragBehaviour(UDreamWidget& InRow, int32 InPoolIndex)
{
	// Nothing at all while both switches are off, which is what makes the default free: an existing
	// list gains no behaviour, no subscription and nothing that could change what a press means.
	// Behaviours here are plain UObjects with no enable switch, so a list that turns a switch back
	// off KEEPS whatever it made and refuses in the behaviour instead -- destroying one would mark
	// the outliner dirty and cost the designer a full details rebuild.
	if (!bAllowDragging && !bAllowDragDrop)
	{
		return;
	}

	if (bAllowDragging)
	{
		UDreamListRowDragSource* Source = InRow.GetComponent<UDreamListRowDragSource>();
		if (Source == nullptr)
		{
			Source = InRow.AddComponent<UDreamListRowDragSource>();
		}
		if (Source != nullptr)
		{
			// Re-aimed every time, like the row button's transition target: the pool slot is the
			// row's identity here, and a source carried over from a duplicated template would point
			// at the wrong one.
			Source->OwningList = this;
			Source->PoolIndex = InPoolIndex;
			// Re-stated rather than set once: the visual class and the tag are editable properties,
			// and a row made before an author changed them would otherwise keep the old ones.
			Source->Tag = DragOperationTag;
			// Null falls back to the row's own class, which is the answer that needs no authoring:
			// the thing under the cursor should look like the row it came from.
			Source->DragVisualClass = DragDropVisualEntryClass != nullptr ? DragDropVisualEntryClass : RowTemplateClass;
			Source->DragVisualOffset = DragDropVisualOffset;
			// Deliberately false, and this is the arbitration the whole family turns on: once a press
			// on a row becomes an item drag, that gesture is no longer the list's to scroll with.
			// Letting it bubble would hand the same movement to the scroll view underneath, and the
			// list would slide away from under the thing being dragged.
			Source->bAllowEventBubbleUp = false;
		}
	}

	if (bAllowDragDrop)
	{
		UDreamListRowDropTarget* Target = InRow.GetComponent<UDreamListRowDropTarget>();
		if (Target == nullptr)
		{
			Target = InRow.AddComponent<UDreamListRowDropTarget>();
			if (Target != nullptr)
			{
				// Bound to the TARGET's own handlers, not the list's: the base's enter/leave carry
				// only the operation, and the operation cannot say which row fired. The target
				// already knows its pool slot, so letting it forward removes the search entirely --
				// and a search would have had to guess on leave, where the hovered flag is already
				// down by the time anyone is told.
				Target->OnDragEnter.AddDynamic(Target, &UDreamListRowDropTarget::HandleDragEnter);
				Target->OnDragLeave.AddDynamic(Target, &UDreamListRowDropTarget::HandleDragLeave);
			}
		}
		if (Target != nullptr)
		{
			Target->OwningList = this;
			Target->PoolIndex = InPoolIndex;
			// No tag filter at this level: WHICH drags a list accepts is the consumer's decision,
			// made in OnItemAcceptDrop where the payload and the zone are both in hand. A tag here
			// would be a second, coarser filter the consumer cannot see.
			Target->RequiredTag = NAME_None;
		}
	}
}

void UDreamListViewBase::RefreshRowDragBehaviours()
{
	for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		if (UDreamWidget* Row = RowNodes[PoolIndex].Get(); IsValid(Row))
		{
			RefreshRowDragBehaviour(*Row, PoolIndex);
		}
	}
}

void UDreamListViewBase::SetAllowDragging(bool bInAllow)
{
	if (bAllowDragging == bInAllow)
	{
		return;
	}
	bAllowDragging = bInAllow;
	// Rows already made need it too, or the switch would only reach whatever is built after it.
	RefreshRowDragBehaviours();
	if (!bInAllow)
	{
		// A drag in flight belongs to a switch that is now off. Ending it is the only honest answer:
		// leaving it would mean a visual on screen with nothing left that could drop it.
		CancelListViewDragDrop();
	}
}

void UDreamListViewBase::SetShadowBrush(const FDreamUIFaceBrush& InShadowBrush)
{
	ShadowBrush = InShadowBrush;
	// The shadows' rects and skins are settled by the style push, which is a no-op for them while the
	// switch is off.
	ApplyStyle();
}

void UDreamListViewBase::SetDragDropVisualPivot(FVector2D InPivot)
{
	DragDropVisualPivot = InPivot;
}

void UDreamListViewBase::SetDragDropVisualOffset(FVector2D InOffset)
{
	DragDropVisualOffset = InOffset;
}

void UDreamListViewBase::SetDragDropVisualEntryClass(TSubclassOf<UDreamUserWidget> InEntryClass)
{
	DragDropVisualEntryClass = InEntryClass;
}

void UDreamListViewBase::SetDragDropOperationClass(TSubclassOf<UDreamDragDropOperation> InOperationClass)
{
	DragDropOperationClass = InOperationClass;
}

void UDreamListViewBase::SetDragOperationTag(FName InTag)
{
	DragOperationTag = InTag;
}

void UDreamListViewBase::SetAllowDragDrop(bool bInAllow)
{
	if (bAllowDragDrop == bInAllow)
	{
		return;
	}
	bAllowDragDrop = bInAllow;
	RefreshRowDragBehaviours();
}

void UDreamListViewBase::HandleRowDragDetected(int32 InPoolIndex, UDreamDragDropOperation* InOperation)
{
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (InOperation != nullptr)
	{
		// The PAYLOAD is the item, never the row widget: a recycled row stands for a different item
		// a few scrolls later, and a drop that arrived after that would be about the wrong thing.
		InOperation->Payload = GetItemObject(ItemIndex);
		UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
		InOperation->SourceWidget = Row;
		// The pivot folded INTO the offset, because the library's operation carries one number and
		// UMG's two say the same thing: a pivot is an offset measured in the visual's own size. Done
		// here rather than on the behaviour because it needs the row's size, which only exists once
		// there is a row -- and Y runs up, so the vertical half is negated to read "down from the top".
		if (IsValid(Row))
		{
			InOperation->DragVisualOffset = DragDropVisualOffset + FVector2D(
				-DragDropVisualPivot.X * Row->GetWidth(),
				DragDropVisualPivot.Y * Row->GetHeight());
		}
	}
	bIsDragging = true;
	DraggedItemIndex = ItemIndex;
	ActiveDragOperation = InOperation;
	// The item index goes out with it, because the payload alone cannot answer "which row" for a
	// text-only source -- which has no objects at all.
	OnItemDragDetected.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation);
	OnDraggingStateChanged.Broadcast(true);
}

void UDreamListViewBase::HandleRowDragEnded(UDreamDragDropOperation* InOperation)
{
	if (!bIsDragging)
	{
		return;
	}
	// Whether anything took it. The operation is the one thing that outlives the row: a recycled row
	// or a closed screen can destroy the source mid-flight, and the flag would go with it.
	const bool bHandled = InOperation != nullptr && InOperation->bDropWasHandled;
	const int32 ItemIndex = DraggedItemIndex;
	bIsDragging = false;
	DraggedItemIndex = INDEX_NONE;
	ActiveDragOperation = nullptr;
	if (!bHandled)
	{
		OnItemDragCancelled.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation);
	}
	OnDraggingStateChanged.Broadcast(false);
}

void UDreamListViewBase::HandleRowDragEnter(int32 InPoolIndex, UDreamDragDropOperation* InOperation)
{
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex != INDEX_NONE)
	{
		OnItemDragEnter.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation);
	}
}

void UDreamListViewBase::HandleRowDragLeave(int32 InPoolIndex, UDreamDragDropOperation* InOperation)
{
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex != INDEX_NONE)
	{
		OnItemDragLeave.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation);
	}
}

bool UDreamListViewBase::HandleRowDrop(int32 InPoolIndex, UDreamDragDropOperation* InOperation, EDreamItemDropZone InZone)
{
	if (!bAllowDragDrop)
	{
		return false;
	}
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex == INDEX_NONE)
	{
		// A parked row stands for nothing, so a drop on it is a drop on nothing -- refused, and left
		// to bubble to whatever is behind the list.
		return false;
	}
	// The list does NOT re-order itself here, and that is deliberate: what a drop MEANS is the
	// consumer's to decide. A list that moved its own source would be guessing at an edit only the
	// data's owner can make, and would fight every consumer that made the same edit itself.
	OnItemAcceptDrop.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation, InZone);
	return true;
}

void UDreamListRowDropTarget::HandleDragEnter(UDreamDragDropOperation* InOperation)
{
	if (OwningList != nullptr)
	{
		OwningList->HandleRowDragEnter(PoolIndex, InOperation);
	}
}

void UDreamListRowDropTarget::HandleDragLeave(UDreamDragDropOperation* InOperation)
{
	if (OwningList != nullptr)
	{
		OwningList->HandleRowDragLeave(PoolIndex, InOperation);
	}
}

bool UDreamListViewBase::CancelListViewDragDrop()
{
	if (!bIsDragging)
	{
		return false;
	}
	// Through the subsystem, not by clearing our own flags: the operation, the visual and the
	// pointer's state are ITS, and a list that only forgot its half would leave a visual on screen
	// with nothing left to end it. The end-of-drag road then runs as it would for any other cancel.
	const bool bCancelled = UDreamUIWidgetLibrary::CancelDragDrop(this);
	if (bIsDragging)
	{
		// The subsystem had nothing to cancel -- a headless test, or a drag whose pointer is already
		// gone. The state is still this control's to clear, and leaving it set would make every
		// later drag look like it was already in flight.
		HandleRowDragEnded(ActiveDragOperation);
	}
	return bCancelled;
}

void UDreamListViewBase::RefreshShadowBrushes(const FDreamListStyle& InStyle)
{
	if (!bEnableShadowBrush)
	{
		// Slept rather than destroyed, for the designer's sake: creating and destroying widgets marks
		// the outliner dirty, and an author toggling the checkbox would pay a full details rebuild
		// each way.
		if (ShadowStartNode != nullptr) { ShadowStartNode->SetWidgetActive(false); }
		if (ShadowEndNode != nullptr) { ShadowEndNode->SetWidgetActive(false); }
		return;
	}
	if (!IsValid(ViewportNode))
	{
		return;
	}
	// Made on demand: a list that never turns the setting on never carries the two extra nodes.
	// Children of the VIEWPORT, not of the scrolled column -- a fade that scrolled with the content
	// would be a stripe travelling through the list rather than an edge on the window.
	if (ShadowStartNode == nullptr)
	{
		ShadowStartNode = NewObject<UDreamWidget>(ViewportNode->GetOuter(), NAME_None, RF_Public | RF_Transactional);
		ShadowStartNode->SetDisplayName(TEXT("ShadowStart"));
		ShadowStartNode->TrySetParent(ViewportNode, false);
	}
	if (ShadowEndNode == nullptr)
	{
		ShadowEndNode = NewObject<UDreamWidget>(ViewportNode->GetOuter(), NAME_None, RF_Public | RF_Transactional);
		ShadowEndNode->SetDisplayName(TEXT("ShadowEnd"));
		ShadowEndNode->TrySetParent(ViewportNode, false);
	}

	const bool bHorizontal = IsHorizontalList();
	const float Depth = FMath::Max(0.0f, ShadowBrushThickness);
	// Stretched across, pinned to its own end of the main axis, the depth an absolute number: the
	// same shape the scroll bar uses, for the same reason.
	auto PlaceEdge = [bHorizontal, Depth](UDreamWidget* InNode, bool bInAtStart)
	{
		if (InNode == nullptr) { return; }
		if (bHorizontal)
		{
			InNode->SetPivot(FVector2D(bInAtStart ? 0.0 : 1.0, 0.5));
			InNode->SetHorizontalAndVerticalAnchorMinMax(
				FVector2D(bInAtStart ? 0.0 : 1.0, 0.0), FVector2D(bInAtStart ? 0.0 : 1.0, 1.0), false, false);
			InNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(Depth, 0.0));
		}
		else
		{
			// Y runs UP, so the list's START edge is the TOP one: anchor max, not min.
			InNode->SetPivot(FVector2D(0.5, bInAtStart ? 1.0 : 0.0));
			InNode->SetHorizontalAndVerticalAnchorMinMax(
				FVector2D(0.0, bInAtStart ? 1.0 : 0.0), FVector2D(1.0, bInAtStart ? 1.0 : 0.0), false, false);
			InNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(0.0, Depth));
		}
	};
	PlaceEdge(ShadowStartNode, true);
	PlaceEdge(ShadowEndNode, false);
	SkinFace(ShadowStartNode, ShadowBrush);
	SkinFace(ShadowEndNode, ShadowBrush);

	// Awake only while there IS more list that way, which is the whole point: a permanent fade is
	// decoration, and this one is a statement about where the content goes on.
	const float Offset = GetScrollOffset();
	const float End = ScrollBehaviour != nullptr
		? static_cast<float>(bHorizontal ? ScrollBehaviour->GetScrollableExtent().X : ScrollBehaviour->GetScrollableExtent().Y)
		: 0.0f;
	ShadowStartNode->SetWidgetActive(Offset > KINDA_SMALL_NUMBER);
	ShadowEndNode->SetWidgetActive(Offset < End - KINDA_SMALL_NUMBER);
}

void UDreamListViewBase::SetEnableShadowBrush(bool bInEnable)
{
	if (bEnableShadowBrush == bInEnable)
	{
		return;
	}
	bEnableShadowBrush = bInEnable;
	ApplyStyle();
}

void UDreamListViewBase::SetShadowBrushThickness(float InThickness)
{
	ShadowBrushThickness = FMath::Max(0.0f, InThickness);
	ApplyStyle();
}

bool UDreamListViewBase::FocusSelectedRow()
{
	if (SelectedIndex == INDEX_NONE)
	{
		return false;
	}
	// The realized row, which a recycled list may simply not have: an off-screen item has no widget
	// to focus, and saying so is more useful than scrolling to make one and focusing that -- the
	// caller asked to move FOCUS, not the list.
	UDreamWidget* Row = GetRowWidget(SelectedIndex);
	if (!IsValid(Row))
	{
		return false;
	}
	Row->SetKeyboardFocus();
	return true;
}

void UDreamListViewBase::HandleContentFocusMoved(UDreamWidget* InFocusedWidget)
{
	if (!bReturnFocusToSelection || InFocusedWidget == nullptr)
	{
		return;
	}
	// Only when focus landed on the LIST itself. Focus that already reached a row is focus doing
	// exactly what this setting wants, and redirecting it would drag the player off whichever row
	// they just navigated to -- turning the list into something they cannot move around inside.
	if (InFocusedWidget != this && InFocusedWidget != FaceNode)
	{
		return;
	}
	FocusSelectedRow();
}

void UDreamListViewBase::HandleScrollGesture(EDreamScrollDragPhase InPhase, bool bInTouch)
{
	const float Offset = GetScrollOffset();
	const float Fraction = GetViewFraction();
	// Whatever the device: this is what keeps "finished" from being announced in the middle of a drag.
	bScrollDragInProgress = InPhase != EDreamScrollDragPhase::End;
	if (InPhase == EDreamScrollDragPhase::End && (ScrollBehaviour == nullptr || !ScrollBehaviour->IsScrolling()))
	{
		// Let go with no momentum: no further move is coming to announce the end, so this is it.
		OnListViewFinishedScrolling.Broadcast(Offset, Fraction);
	}
	// TOUCH only from here, which is what UMG's three events are about: a mouse drag on the same rows
	// is a different gesture with different handlers, and folding them together would make a consumer
	// that only cares about fingers filter an event this control is better placed to filter.
	if (!bInTouch)
	{
		return;
	}
	switch (InPhase)
	{
	case EDreamScrollDragPhase::Begin: OnListViewTouchStart.Broadcast(Offset, Fraction); break;
	case EDreamScrollDragPhase::Move:  OnListViewTouchMove.Broadcast(Offset, Fraction); break;
	case EDreamScrollDragPhase::End:   OnListViewTouchEnd.Broadcast(Offset, Fraction); break;
	}
}

void UDreamListViewBase::SkinRowForState(int32 InPoolIndex, EUISelectableSelectionState InState)
{
	UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
	if (!IsValid(Row))
	{
		return;
	}
	// Resolved fresh rather than remembered: a style edit between a hover in and a hover out has to
	// reach the row the pointer is still sitting on, and the list has no other moment to notice.
	const FDreamListStyle Active = ResolveListStyle();
	SkinFace(Row, Active.StateFaces.BrushFor(InState, Active.RowBrush));
}

void UDreamListViewBase::HandleRowSelectionStateChanged(int32 InPoolIndex, bool bInHovered)
{
	// The ITEM is what is hovered, not the widget: a row re-bound under a resting pointer is a
	// different item hovered, and a consumer keyed by index would otherwise be told about the old one
	// forever. Tracked as a set of pool indices rather than an array parallel to the pool, so nothing
	// has to be resized when the pool grows or shrinks.
	const bool bWasHovered = HoveredPoolIndices.Contains(InPoolIndex);
	if (bWasHovered == bInHovered)
	{
		return;
	}
	if (bInHovered)
	{
		HoveredPoolIndices.Add(InPoolIndex);
	}
	else
	{
		HoveredPoolIndices.Remove(InPoolIndex);
	}
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex != INDEX_NONE)
	{
		OnItemIsHoveredChanged.Broadcast(ItemIndex, GetItemObject(ItemIndex), bInHovered);
	}
}

void UDreamListViewBase::HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged)
{
	if (!bWidthChanged && !bHeightChanged)
	{
		return;
	}
	const FDreamListStyle& Active = ResolveListStyle();
	// A WIDER viewport is a different number of columns for a grid, which is a different number of
	// lines, which is a different scroll range. A list's answer does not move, so this costs it the
	// arithmetic and nothing else.
	RefreshContentHeight(Active);
	RefreshScrollFurniture(Active);
	if (bVirtualizing)
	{
		// A taller viewport needs more widgets, and a shorter one is holding some it no longer shows.
		const int32 Wanted = FMath::Min(VisibleItemIndices.Num(), ResolveWindowSize());
		if (Wanted != RowNodes.Num() && IsValid(RowTemplateNode))
		{
			RowTemplateNode->SetWidgetActive(true);
			ResizePool(Wanted);
			RowTemplateNode->SetWidgetActive(false);
		}
	}
	RefreshVisibleWindow();
}

void UDreamListViewBase::RefreshScrollFurniture(const FDreamListStyle& InStyle)
{
	const bool bBarVisible = ShouldShowScrollBar();
	const float Gutter = bBarVisible ? InStyle.Bar.Thickness : 0.0f;
	const bool bHorizontal = IsHorizontalList();

	// No circle here, unlike UDreamScrollBox's: the gutter takes from the viewport's CROSS axis and
	// the overflow question is about the main one, and a row's length does not depend on how wide it
	// is. So one pass answers it, where the box has to measure, decide and re-state.
	if (ViewportNode != nullptr)
	{
		// Stretched, deliberately: the viewport has to track the list's live size on every arrange,
		// and a stretched axis is exactly how to say "the parent's span, less the gutter" -- a
		// SizeDelta on a stretched axis is the DIFFERENCE from that span, so the gutter goes in
		// negative and the position shifts by half of it to leave the opposite edge where it was.
		ViewportNode->SetPivot(FVector2D(0.5, 0.5));
		ViewportNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
		ViewportNode->SetAnchoredPositionAndSizeDelta(
			bHorizontal ? FVector2D(0.0, Gutter * 0.5) : FVector2D(-Gutter * 0.5, 0.0),
			bHorizontal ? FVector2D(0.0, -Gutter) : FVector2D(-Gutter, 0.0));
	}

	if (ColumnNode != nullptr)
	{
		// Re-stated on every push, not just at build: the built-in tree authors the vertical answer,
		// and Orientation is an editable property. The scrolled axis has to be a POINT anchor -- a
		// stretched one would pin the column to the viewport and nothing would ever scroll -- and the
		// other one stretched, so a row is as wide as the list whatever the list turns out to be.
		ColumnNode->SetPivot(bHorizontal ? FVector2D(0.0, 0.5) : FVector2D(0.5, 1.0));
		ColumnNode->SetHorizontalAndVerticalAnchorMinMax(
			bHorizontal ? FVector2D(0.0, 0.0) : FVector2D(0.0, 1.0),
			bHorizontal ? FVector2D(0.0, 1.0) : FVector2D(1.0, 1.0), false, false);
		// The DELTA on the stretched axis, never the size: a zero delta says "exactly the span",
		// whenever the span is decided. SetWidth / SetHeight would be the wrong verb -- on a stretched
		// axis they resolve the parent's span at write time and bake the difference in, so a list
		// built before it was sized would carry minus its eventual width forever.
		// RefreshContentHeight writes the other axis.
		FVector2D ColumnDelta = ColumnNode->GetSizeDelta();
		(bHorizontal ? ColumnDelta.Y : ColumnDelta.X) = 0.0;
		ColumnNode->SetSizeDelta(ColumnDelta);
	}

	if (ScrollBarNode != nullptr)
	{
		// Along the far edge -- right for a column, bottom for a band: stretched on the bar's own
		// axis so it always spans the list, a POINT anchor with the pivot on that edge across it so
		// the thickness is an absolute number pinned flush.
		ScrollBarNode->SetDirection(bHorizontal
			? EUIScrollbarDirectionType::LeftToRight
			: EUIScrollbarDirectionType::TopToBottom);
		ScrollBarNode->SetPivot(bHorizontal ? FVector2D(0.5, 0.0) : FVector2D(1.0, 0.5));
		ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(
			bHorizontal ? FVector2D(0.0, 0.0) : FVector2D(1.0, 0.0),
			bHorizontal ? FVector2D(1.0, 0.0) : FVector2D(1.0, 1.0), false, false);
		ScrollBarNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector,
			bHorizontal ? FVector2D(0.0, InStyle.Bar.Thickness) : FVector2D(InStyle.Bar.Thickness, 0.0));
		// After its rect, never before: the bar reads its own track's live size to lay the handle out.
		ScrollBarNode->ApplyStyle();
		// Last of all, so a bar that is about to appear is already the right shape when it does.
		ScrollBarNode->SetWidgetActive(bBarVisible);
	}

	RefreshShadowBrushes(InStyle);

	if (ScrollBehaviour != nullptr)
	{
		// The scroll range was computed against the old content height and nothing recomputes it on
		// its own -- a list that grows while it is on screen would otherwise refuse to reach its end.
		ScrollBehaviour->RectRangeChanged();
	}
	if (ScrollBarNode != nullptr)
	{
		// A range change moves the visible fraction, and nothing broadcasts that.
		ScrollBarNode->RefreshFromScrollView();
	}
}

bool UDreamListViewBase::ShouldShowScrollBar() const
{
	if (!bShowScrollBar)
	{
		return false;
	}
	if (ScrollBarVisibility == EDreamScrollBoxScrollbarVisibility::Permanent)
	{
		return true;
	}
	if (ColumnNode == nullptr || ViewportNode == nullptr)
	{
		return true;
	}
	return ColumnNode->GetHeight() > ViewportNode->GetHeight() + KINDA_SMALL_NUMBER;
}

/**
 * One row widget, and everything about it that outlives the item it is showing.
 *
 * The click handler above all: it is added HERE and captures the POOL index, then asks which item
 * that slot is showing at the moment it fires. Subscribing per bind instead would leave a row that
 * had been round the list ten times firing ten selections.
 */
UDreamWidget* UDreamListViewBase::CreatePoolRow(int32 InPoolIndex)
{
	// Outered to the column's outer -- the widget tree -- which is where every widget in this
	// hierarchy lives. Duplication brings the whole subtree, its visual, its slot and its behaviours,
	// then registers the copy under its new parent; no world is required for any of it.
	UDreamWidget* Row = DuplicateDreamWidgetHierarchy(ColumnNode->GetOuter(), RowTemplateNode, ColumnNode);
	if (!IsValid(Row))
	{
		return nullptr;
	}
	Row->SetDisplayName(FString::Printf(TEXT("Row_%d"), InPoolIndex));

	// An authored row, when the consumer supplied a class for one. It fills the row and the built-in
	// label steps aside; the row's face, height, hover and selection stay the control's, so a
	// template only has to draw an item. Instancing a user widget needs a world (CreateDreamWidget
	// says so), and with none this simply does not happen -- the built-in label row is a correct
	// list, where half an authored row would not be. Created once per POOL row, so it survives every
	// rebind; OnRowGenerated is where a consumer updates what it shows.
	if (RowTemplateClass != nullptr && GetWorld() != nullptr)
	{
		if (UDreamUserWidget* Content = CreateDreamWidget(GetWorld(), RowTemplateClass, Row))
		{
			Content->SetDisplayName(TEXT("RowContent"));
			if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
			{
				ContentSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
				ContentSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
			}
		}
	}

	if (UUIButton* RowButton = Row->GetComponent<UUIButton>())
	{
		// Re-aimed, every time. TransitionTarget is a weak pointer copied by value, so a fresh row
		// starts out pointing at the TEMPLATE's visual -- left alone, every hover anywhere in the
		// list would repaint the one widget nobody can see. Registration only fills the target in
		// when it is EMPTY, and a copied one is not empty, it is wrong.
		RowButton->SetTransitionTarget(Row->GetVisual());
		// Whose row it is, for the navigation press it hands to the list. Written here rather than
		// inherited from the template for the drag source's reason: the pool slot IS the identity, and
		// a copy carries the template's slot, which is none.
		if (UDreamListRowButton* RowNavigation = Cast<UDreamListRowButton>(RowButton))
		{
			RowNavigation->OwningList = this;
			RowNavigation->PoolIndex = InPoolIndex;
		}
		RowButton->GetOnClickEvent().AddWeakLambda(this, [this, InPoolIndex]()
		{
			HandleRowClicked(InPoolIndex);
		});
		// Once, for the life of this widget, like the click above: the event system decides what a
		// double click is and the button re-broadcasts it, so the list neither keeps a clock nor
		// disagrees with the text field beside it about the interval.
		RowButton->GetOnDoubleClickEvent().AddWeakLambda(this, [this, InPoolIndex]()
		{
			HandleRowDoubleClicked(InPoolIndex);
		});
		// Hover, from the state the selectable already tracks rather than from a second pair of enter
		// and exit subscriptions: the selectable is the one thing that knows a pointer resting on a
		// row is not the same as focus landing on it, and it has drawn that distinction since the
		// focus state was added.
		RowButton->GetOnSelectionStateChangedEvent().AddWeakLambda(this,
			[this, InPoolIndex](EUISelectableSelectionState InState, bool /*bInImmediate*/)
			{
				HandleRowSelectionStateChanged(InPoolIndex,
					InState == EUISelectableSelectionState::Hovered || InState == EUISelectableSelectionState::Pressed);
				// The row's DRAWING per state, on the same subscription: the selectable already tints
				// the row's colour per state, and this is the picture under that tint. One
				// subscription rather than two, so the colour and the brush can never be a frame
				// apart on the same row.
				SkinRowForState(InPoolIndex, InState);
			});
	}

	// Drag and drop, if this list uses them at all. With both switches off this adds nothing to the
	// row -- no behaviour, no subscription -- which is what makes the default free.
	RefreshRowDragBehaviour(*Row, InPoolIndex);

	DecorateNewRow(*Row, InPoolIndex);
	// Once in this widget's life -- UMG's OnEntryInitialized. Announced after the subclass has had
	// its own one-time hook, so a consumer sees a row that is already wired.
	OnRowInitialized.Broadcast(INDEX_NONE, Row, nullptr);
	return Row;
}

void UDreamListViewBase::BindRow(int32 InPoolIndex, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle)
{
	UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
	if (!IsValid(Row))
	{
		return;
	}
	// The row is about to stop standing for whatever it was showing -- UMG's OnEntryReleased, and the
	// place a consumer undoes what OnRowGenerated did to this widget. Before the new index is written,
	// so a handler asking GetRowItemIndex is still told the one being released.
	const int32 ReleasedItemIndex = RowSourceIndices[InPoolIndex];
	if (ReleasedItemIndex != INDEX_NONE && ReleasedItemIndex != InItemIndex)
	{
		OnRowReleased.Broadcast(ReleasedItemIndex, Row, GetItemObject(ReleasedItemIndex));
	}
	RowSourceIndices[InPoolIndex] = InItemIndex;
	Row->SetWidgetActive(true);

	// The row's LOOK, pushed here rather than inherited from the template it was copied from. That
	// is what lets the pool survive a style edit -- see the note in RebuildRows about what creating
	// a widget costs the designer. Rows stay unrounded on purpose: they sit square against the
	// list's own rounded edge, the way the dropdown's items do.
	//
	// The row's CURRENT state, not Normal: a row re-bound under a resting pointer is already hovered
	// and would otherwise be drawn resting until the pointer moved again -- the same trap the hover
	// bookkeeping above exists for. An empty state group answers RowBrush for every state, which is
	// what every existing style has and why none of them changes appearance.
	const UUIButton* RowButton = Row->GetComponent<UUIButton>();
	SkinRowForState(InPoolIndex, RowButton != nullptr
		? RowButton->GetSelectionState()
		: EUISelectableSelectionState::Normal);

	// The rect, stated from the display index and from nothing else. What that means differs between
	// a list and a grid, which is why it is a hook rather than four lines here.
	PlaceRow(*Row, InDisplayIndex, InStyle);

	// Where a row's content starts: the row's own inset, plus whatever the subclass wants in front
	// of it (for a tree, the indent and room for the twisty).
	const FMargin BasePadding = GetRowPadding();
	const FMargin ContentPadding(
		BasePadding.Left + GetRowContentInset(InItemIndex),
		BasePadding.Top,
		BasePadding.Right,
		BasePadding.Bottom);

	UDreamWidget* Content = Row->FindChildByDisplayName(TEXT("RowContent"));
	const bool bAuthoredContent = IsValid(Content);
	if (bAuthoredContent)
	{
		if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
		{
			ContentSlot->SetPadding(ContentPadding);
		}
	}

	if (UDreamWidget* LabelNode = Row->FindChildByDisplayName(TEXT("RowLabel")))
	{
		LabelNode->SetWidgetActive(!bAuthoredContent);
		if (UDreamText* LabelText = Cast<UDreamText>(LabelNode->GetVisual()))
		{
			LabelText->SetText(GetItemLabel(InItemIndex));
			LabelText->SetColor(InStyle.TextColor);
			LabelText->SetFontSize(InStyle.FontSize);
		}
		if (UDreamPanelSlot* LabelSlot = LabelNode->GetPanelSlot())
		{
			LabelSlot->SetPadding(ContentPadding);
		}
	}

	// The whole state set, including the resting colour, is ApplyRowColor's -- see it for why a row
	// goes through PushSelectableState rather than setting two colours here.
	ApplyRowColor(Row, InDisplayIndex, InItemIndex, InStyle);

	// The subclass's turn (the tree's twisty), then the consumer's. Both run on every BIND, which is
	// every time a recycled row comes round to a new item -- UMG's OnEntryGenerated contract.
	DecorateRow(*Row, InPoolIndex, InItemIndex);
	OnRowGenerated.Broadcast(InItemIndex, Row, GetItemObject(InItemIndex));
}

void UDreamListViewBase::PlaceRow(UDreamWidget& InRow, int32 InDisplayIndex, const FDreamListStyle& InStyle)
{
	// Pinned to the column's NEAR edge and stretched across, so a row is as wide as the column
	// whatever the column turns out to be, and as long as the style says whatever its content
	// measures to. The cross inset is the viewport's Padding, applied as a negative delta the way
	// every stretched axis in this library states an inset.
	//
	// Y runs UP, which is why the vertical list's offset goes in negative and the horizontal list's
	// positive: both mean "further along the scroll axis", and the axis points opposite ways.
	const float Inset = GetCrossPadStart(InStyle) + GetCrossPadEnd(InStyle);
	const float MainOffset = GetRowTopOffset(InDisplayIndex);
	const float CrossNudge = (GetCrossPadStart(InStyle) - GetCrossPadEnd(InStyle)) * 0.5f;
	if (IsHorizontalList())
	{
		InRow.SetPivot(FVector2D(0.0, 0.5));
		InRow.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(0.0, 1.0), false, false);
		// The cross nudge is negated: the cross axis is Y here, and its "start" is the TOP, which is
		// the larger Y. Left and Top are both the near edge; only their sign differs.
		InRow.SetAnchoredPositionAndSizeDelta(
			FVector2D(MainOffset, -CrossNudge), FVector2D(InStyle.RowHeight, -Inset));
	}
	else
	{
		InRow.SetPivot(FVector2D(0.5, 1.0));
		InRow.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(1.0, 1.0), false, false);
		InRow.SetAnchoredPositionAndSizeDelta(
			FVector2D(CrossNudge, -MainOffset), FVector2D(-Inset, InStyle.RowHeight));
	}
}

void UDreamListViewBase::ParkRow(int32 InPoolIndex)
{
	if (!RowNodes.IsValidIndex(InPoolIndex))
	{
		return;
	}
	const int32 ReleasedItemIndex = RowSourceIndices[InPoolIndex];
	RowSourceIndices[InPoolIndex] = INDEX_NONE;
	if (ReleasedItemIndex != INDEX_NONE)
	{
		// Parking is a release too: the row stops standing for its item, and a consumer that hung
		// something on it at generation time has to hear about that on both roads out.
		OnRowReleased.Broadcast(ReleasedItemIndex, RowNodes[InPoolIndex].Get(), GetItemObject(ReleasedItemIndex));
	}
	if (UDreamWidget* Row = RowNodes[InPoolIndex].Get())
	{
		// Asleep rather than destroyed: a parked row is the pool's spare, and the next scroll wants
		// it back. Asleep it neither draws nor answers a pointer, which is the whole requirement.
		Row->SetWidgetActive(false);
	}
}

void UDreamListViewBase::ApplyRowColor(UDreamWidget* InRow, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle)
{
	if (!IsValid(InRow))
	{
		return;
	}
	// Selection is not a pointer state -- it has to survive the pointer leaving -- so it rides the
	// selectable's NORMAL colour rather than a fourth transition it does not have. FDreamTabViewStyle
	// makes the same call in the same words.
	const bool bSelected = IsItemSelected(InItemIndex);
	const FColor Resting = bSelected
		? InStyle.RowSelected
		// Striped by DISPLAY position, not by pool position: a recycled row that landed in slot 0
		// showing item 37 has to wear item 37's stripe, or the whole list flickers as it scrolls.
		: ((bAlternatingRowColors && (InDisplayIndex % 2) == 1) ? InStyle.RowAlternate : InStyle.RowNormal);

	if (UUIButton* RowButton = InRow->GetComponent<UUIButton>())
	{
		// All five states and the speed, in one call, rather than the three pointer colours the rows
		// used to push: the two left out were a flat grey belonging to no theme and focus visuals
		// that ship OFF, so a keyboard or a pad landing on a row showed nothing at all. There is no
		// RowPressed in the style, deliberately -- pressing a row is the beginning of selecting it,
		// so it previews the selected colour rather than inventing a sixth one.
		PushSelectableState(RowButton, Resting, InStyle.RowHovered, InStyle.RowSelected,
			InStyle.RowDisabled, InStyle.RowFocused, InStyle.TransitionDuration);
	}
	// And onto the visual directly. SetNormalColor repaints only while the row is in its Normal
	// state AND a transition can actually run -- the tween manager needs a world -- so a row's
	// resting colour is data this control owns, not something to hope a transition will deliver.
	if (UDreamVisual* RowVisual = InRow->GetVisual())
	{
		RowVisual->SetColor(Resting);
	}
}

void UDreamListViewBase::RefreshRowColors()
{
	const FDreamListStyle& Active = ResolveListStyle();
	for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		const int32 ItemIndex = RowSourceIndices.IsValidIndex(PoolIndex) ? RowSourceIndices[PoolIndex] : INDEX_NONE;
		if (ItemIndex != INDEX_NONE)
		{
			ApplyRowColor(RowNodes[PoolIndex], WindowStart + PoolIndex, ItemIndex, Active);
		}
	}
}

void UDreamListViewBase::HandleRowClicked(int32 InPoolIndex)
{
	// Asked at the moment of the click, never captured: which item this slot shows changes every
	// time the list scrolls past it.
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex == INDEX_NONE)
	{
		return;
	}
	// What the mode makes of the click, in UUIListView's four answers. None still reports the click:
	// a list nobody can select from is a perfectly ordinary menu.
	switch (SelectionMode)
	{
	case EUIListSelectionMode::Single:
		SetItemSelection(ItemIndex, true, true);
		break;
	case EUIListSelectionMode::SingleToggle:
		SetItemSelection(ItemIndex, !IsItemSelected(ItemIndex), true);
		break;
	case EUIListSelectionMode::Multi:
		SetItemSelection(ItemIndex, !IsItemSelected(ItemIndex), false);
		break;
	default:
		break;
	}

	// After the selection, so a handler asking GetSelectedIndex sees the answer the user just gave.
	OnItemClicked.Broadcast(ItemIndex, GetItemObject(ItemIndex));
}

void UDreamListViewBase::HandleRowDoubleClicked(int32 InPoolIndex)
{
	// Which item this slot shows, asked now: the pool row that was double clicked may have come round
	// to a different item since the pair started, and the second click is the one that counts.
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	if (ItemIndex == INDEX_NONE)
	{
		return;
	}
	// No selection work here: the single click that came with this pair already did it. The event
	// system delivers the click first and the double click after (see IDreamPointerDoubleClickInterface),
	// which is what lets a row select on the way to opening.
	OnItemDoubleClicked.Broadcast(ItemIndex, GetItemObject(ItemIndex));
}

void UDreamListViewBase::SetItems(const TArray<FText>& InItems)
{
	Items = InItems;
	// A text source has no identity to move a selection by -- index 2 of the new source is a
	// different line than index 2 of the old one -- so the selection is dropped rather than left
	// pointing at a row nobody chose. A source that DOES have identity keeps its selection; see
	// SetItemObjects.
	const bool bDropsSelection = ItemObjects.Num() == 0 && (SelectedIndices.Num() > 0 || SelectedIndex != INDEX_NONE);
	if (ItemObjects.Num() == 0)
	{
		SelectedIndices.Reset();
		SelectedIndex = INDEX_NONE;
	}
	// The object source did not move, so it is its own "previous" -- a tree whose rows are objects
	// keeps everything it keyed by them when only the LABELS are replaced.
	OnSourceChanged(ItemObjects);
	RebuildRows();
	// And said, once: a consumer holding "the selected line" has to hear that it no longer has one,
	// which is what SListView::UpdateSelectionSet does for items that left the source. This used to be
	// silent on the grounds that a source edit is authoring; a binding left holding an index nothing
	// is selected at any more is the cost of that, and UMG does not pay it.
	if (bDropsSelection)
	{
		AnnounceSelectionLostToSource();
	}
}

void UDreamListViewBase::AnnounceSelectionLostToSource()
{
	if (RebuildSuppressionDepth > 0)
	{
		// Mid-batch: the batch announces once, after its one rebuild.
		bSelectionLostWhileSuppressed = true;
		return;
	}
	// Both names, as every other selection change says them: the `<->` desugar listens on the second.
	OnSelectionChanged.Broadcast(SelectedIndex);
	OnValueChangedBP.Broadcast(SelectedIndex);
}

void UDreamListViewBase::SetItemObjects(const TArray<UObject*>& InItems)
{
	// The selection as OBJECTS, before the source moves underneath it. An object is what a selection
	// means; the index is only where that object happened to be, and keeping the number across a
	// re-order silently hands the consumer a different row.
	TArray<UObject*> PreviouslySelected;
	PreviouslySelected.Reserve(SelectedIndices.Num());
	for (int32 Index : SelectedIndices)
	{
		if (UObject* Item = GetItemObject(Index))
		{
			PreviouslySelected.Add(Item);
		}
	}
	UObject* PreviousAnchor = GetItemObject(SelectedIndex);
	// The outgoing array, kept whole for OnSourceChanged: a subclass holding INDICES can only turn
	// them back into items while this still exists.
	const TArray<TObjectPtr<UObject>> PreviousItemObjects = ItemObjects;

	ItemObjects.Reset(InItems.Num());
	for (UObject* Item : InItems)
	{
		ItemObjects.Add(Item);
	}

	// Re-located rather than re-used: an object still in the source keeps its selection wherever it
	// landed, and one that left loses it. The first of those is silent -- what is chosen has not
	// changed, only where it sits -- and the second is not.
	const int32 SelectedBefore = SelectedIndices.Num();
	SelectedIndices.Reset();
	for (UObject* Item : PreviouslySelected)
	{
		const int32 Found = ItemObjects.IndexOfByKey(Item);
		if (Found != INDEX_NONE)
		{
			SelectedIndices.AddUnique(Found);
		}
	}
	const int32 Anchor = PreviousAnchor != nullptr ? ItemObjects.IndexOfByKey(PreviousAnchor) : INDEX_NONE;
	SelectedIndex = SelectedIndices.Contains(Anchor)
		? Anchor
		: (SelectedIndices.Num() > 0 ? SelectedIndices[0] : INDEX_NONE);
	const bool bLostSelectedItems = SelectedIndices.Num() < SelectedBefore;
	OnSourceChanged(PreviousItemObjects);
	RebuildRows();
	// SListView::UpdateSelectionSet: selected items that are no longer in the source leave the
	// selection, and that is signalled once (ESelectInfo::Direct) -- after the rebuild here, so a
	// handler asking which rows exist is told about the list it now has.
	if (bLostSelectedItems)
	{
		AnnounceSelectionLostToSource();
	}
}

TArray<UObject*> UDreamListViewBase::GetListItems() const
{
	// A copy: a UFUNCTION return is a value, and handing out the live array would let a caller resize
	// the very thing every row in this control is indexed into.
	TArray<UObject*> Result;
	Result.Reserve(ItemObjects.Num());
	for (const TObjectPtr<UObject>& Item : ItemObjects)
	{
		Result.Add(Item.Get());
	}
	return Result;
}

void UDreamListViewBase::AddItem(UObject* InItem)
{
	TArray<UObject*> Next = GetListItems();
	Next.Add(InItem);
	// Through the setter, not onto the array: it is what settles the selection and the subclass's
	// index-keyed state against the source that is going away.
	SetItemObjects(Next);
}

void UDreamListViewBase::AddTextItem(FText InItem)
{
	TArray<FText> Next = Items;
	Next.Add(InItem);
	SetItems(Next);
}

void UDreamListViewBase::AddItemAt(UObject* InItem, int32 InItemIndex)
{
	AddItemsAt({InItem}, InItemIndex);
}

void UDreamListViewBase::AddItems(const TArray<UObject*>& InItems)
{
	AddItemsAt(InItems, GetItemCount());
}

void UDreamListViewBase::AddItemsAt(const TArray<UObject*>& InItems, int32 InItemIndex)
{
	if (InItems.Num() == 0)
	{
		return;
	}
	// One rebuild for the whole batch. Without this every insert would lay the rows out against a
	// source the caller is still editing, and fire a release for every item that shifted along.
	FScopedRebuildSuppression Batch(*this);

	TArray<UObject*> Next = GetListItems();
	const int32 At = FMath::Clamp(InItemIndex, 0, Next.Num());
	Next.Insert(InItems, At);
	// The parallel text at the SAME place, with a blank per new row: the two arrays describe the
	// same rows, and inserting into one alone re-labels every row after the seam. Blank rather than
	// absent, because GetItemLabel falls back to the object's name for an index the texts do not
	// reach -- which is right at the END of a shorter array and wrong in the middle of one.
	if (Items.Num() > 0)
	{
		TArray<FText> NextTexts = Items;
		const int32 TextAt = FMath::Clamp(InItemIndex, 0, NextTexts.Num());
		NextTexts.InsertDefaulted(TextAt, InItems.Num());
		Items = NextTexts;
	}
	SetItemObjects(Next);
}

void UDreamListViewBase::RemoveItem(UObject* InItem)
{
	RemoveItemAt(GetIndexForItem(InItem));
}

void UDreamListViewBase::RemoveItems(const TArray<UObject*>& InItems)
{
	if (InItems.Num() == 0)
	{
		return;
	}
	FScopedRebuildSuppression Batch(*this);
	// Indices first, then removed from the BACK: every removal shifts what is after it, so taking
	// them in ascending order would make each index after the first one wrong.
	TArray<int32> Targets;
	Targets.Reserve(InItems.Num());
	for (UObject* Item : InItems)
	{
		const int32 Index = GetIndexForItem(Item);
		if (Index != INDEX_NONE)
		{
			Targets.AddUnique(Index);
		}
	}
	Targets.Sort();
	for (int32 Slot = Targets.Num() - 1; Slot >= 0; --Slot)
	{
		RemoveItemAt(Targets[Slot]);
	}
}

void UDreamListViewBase::RemoveItemAt(int32 InItemIndex)
{
	if (InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return;
	}
	// Both arrays, together. They are parallel, and shortening only one of them would re-label every
	// row after the hole -- which reads as the list having scrambled itself.
	if (Items.IsValidIndex(InItemIndex))
	{
		TArray<FText> NextTexts = Items;
		NextTexts.RemoveAt(InItemIndex);
		Items = NextTexts;
	}
	if (ItemObjects.IsValidIndex(InItemIndex))
	{
		TArray<UObject*> NextObjects = GetListItems();
		NextObjects.RemoveAt(InItemIndex);
		SetItemObjects(NextObjects);
		return;
	}
	// Text-only source: SetItemObjects did not run, so the rebuild is this call's to make.
	SetItems(Items);
}

void UDreamListViewBase::ClearListItems()
{
	Items.Reset();
	SetItemObjects(TArray<UObject*>());
}

UObject* UDreamListViewBase::GetItemAt(int32 InItemIndex) const
{
	return GetItemObject(InItemIndex);
}

int32 UDreamListViewBase::GetIndexForItem(UObject* InItem) const
{
	if (InItem == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < ItemObjects.Num(); ++Index)
	{
		if (ItemObjects[Index].Get() == InItem)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UDreamListViewBase::SetRowTemplateClass(TSubclassOf<UDreamUserWidget> InClass)
{
	if (RowTemplateClass == InClass)
	{
		return;
	}
	RowTemplateClass = InClass;
	// A teardown, not a restyle: the authored instance lives INSIDE each pool row and is made once
	// per row, so a rebind can carry a new style into an existing row but never a new class.
	RebuildRows();
}

void UDreamListViewBase::SetSelectedIndices(const TArray<int32>& InIndices)
{
	SelectedIndices = InIndices;
	// The anchor is re-derived from the set rather than left pointing at whatever it was, and the
	// rows are repainted: a raw write here used to be picked up only on the next rebuild, which is
	// the difference between a property and a setter.
	SelectedIndex = SelectedIndices.Num() > 0 ? SelectedIndices.Last() : INDEX_NONE;
	RebuildRows();
}

void UDreamListViewBase::NavigateToIndex(int32 InItemIndex)
{
	// The veto governs navigation as well as selection, and has to be asked on BOTH roads or the two
	// disagree about which rows exist -- a row that cannot be clicked but can be navigated to is a
	// dead end the player can reach.
	if (!IsItemSelectableOrNavigable(InItemIndex))
	{
		return;
	}
	// Both halves, because doing them separately is always wrong the same way: a selection that is
	// off screen is a selection nobody can see they made. Unless the author asked for navigation to
	// reveal only -- a list you scroll through without losing the line you were on.
	if (bSelectItemOnNavigation)
	{
		SetSelectedIndex(InItemIndex);
	}
	else if (bClearScrollVelocityOnSelection)
	{
		// SetSelectedIndex would have done this; with selection off, the reveal still counts as the
		// player choosing where to be.
		EndInertialScrolling();
	}
	ScrollIndexIntoView(InItemIndex);
}

bool UDreamListRowButton::OnNavigate_Implementation(EDreamUINavigationDirection InDirection, TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	// The list first: it steps by item, the way SListView does. Only a press it has no answer for --
	// past either end, across a one-column list -- falls through to the geometric scan, which is how
	// focus leaves a list for whatever is beside it (STableViewBase::OnNavigation's answer).
	if (InDirection != EDreamUINavigationDirection::None && OwningList != nullptr
		&& OwningList->HandleRowNavigation(PoolIndex, InDirection, OutResult))
	{
		return true;
	}
	return Super::OnNavigate_Implementation(InDirection, OutResult);
}

bool UDreamListViewBase::HandleRowNavigation(int32 InPoolIndex, EDreamUINavigationDirection InDirection,
	TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	// Which item the row shows, asked NOW: a recycled row stands for a different item every few scrolls.
	const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
	const int32 DisplayIndex = ItemIndex != INDEX_NONE ? VisibleItemIndices.IndexOfByKey(ItemIndex) : INDEX_NONE;
	if (DisplayIndex == INDEX_NONE)
	{
		return false;
	}
	// The veto governs this road as it does the click: an item that may not be navigated to is stepped
	// OVER, in the same direction, the way SListView's Private_FindNextSelectableOrNavigable walk does.
	// Bounded by the row count, so a list that vetoes everything ends the walk rather than looping.
	int32 TargetDisplay = ResolveNavigationTarget(DisplayIndex, InDirection);
	for (int32 Remaining = VisibleItemIndices.Num(); Remaining > 0 && VisibleItemIndices.IsValidIndex(TargetDisplay); --Remaining)
	{
		if (IsItemSelectableOrNavigable(VisibleItemIndices[TargetDisplay]))
		{
			break;
		}
		TargetDisplay = ResolveNavigationTarget(TargetDisplay, InDirection);
	}
	if (!VisibleItemIndices.IsValidIndex(TargetDisplay) || !IsItemSelectableOrNavigable(VisibleItemIndices[TargetDisplay]))
	{
		// Nowhere to go inside the list: the press is the scan's, and the scan leaves.
		return false;
	}
	return MoveNavigationToItem(VisibleItemIndices[TargetDisplay], OutResult);
}

int32 UDreamListViewBase::ResolveNavigationTarget(int32 InDisplayIndex, EDreamUINavigationDirection InDirection) const
{
	// Along the scroll axis only, one LINE per press -- which for a list is one row, and is why the
	// column count is in the arithmetic at all. Everything else is not the list's to answer.
	const bool bHorizontal = IsHorizontalList();
	const EDreamUINavigationDirection Backward = bHorizontal ? EDreamUINavigationDirection::Left : EDreamUINavigationDirection::Up;
	const EDreamUINavigationDirection Forward = bHorizontal ? EDreamUINavigationDirection::Right : EDreamUINavigationDirection::Down;
	int32 Step = 0;
	if (InDirection == Backward)
	{
		Step = -1;
	}
	else if (InDirection == Forward)
	{
		Step = 1;
	}
	if (Step == 0)
	{
		return INDEX_NONE;
	}
	const int32 Target = InDisplayIndex + Step * FMath::Max(1, ResolveColumnCount());
	return VisibleItemIndices.IsValidIndex(Target) ? Target : INDEX_NONE;
}

bool UDreamListViewBase::MoveNavigationToItem(int32 InItemIndex, TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	if (InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return false;
	}
	// SListView::NavigationSelect: select what the press lands on -- through SetItemSelection, the road
	// a click takes, because it is the one that knows SelectionMode None selects nothing and that a
	// press in Multi without modifier keys REPLACES the selection.
	if (bSelectItemOnNavigation)
	{
		SetItemSelection(InItemIndex, true, /*bInClearOthers*/true);
	}
	else if (bClearScrollVelocityOnSelection)
	{
		// NavigateToIndex's reason: with selection off, landing somewhere still counts as the player
		// choosing where to be, and a fling carrying on would slide it away.
		EndInertialScrolling();
	}
	// Revealed BEFORE the row is named: a recycling list re-binds its window while it scrolls, and the
	// row that shows the item afterwards is the one focus has to land on.
	ScrollItemIntoView(InItemIndex);
	return KeepNavigationOnItem(InItemIndex, OutResult);
}

bool UDreamListViewBase::KeepNavigationOnItem(int32 InItemIndex, TScriptInterface<IDreamNavigationInterface>& OutResult) const
{
	UDreamWidget* Row = GetRowWidget(InItemIndex);
	UUIButton* RowButton = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
	if (!IsValid(RowButton))
	{
		return false;
	}
	OutResult.SetObject(RowButton);
	OutResult.SetInterface(Cast<IDreamNavigationInterface>(RowButton));
	return true;
}

bool UDreamListViewBase::IsItemSelectableOrNavigable(int32 InItemIndex) const
{
	if (InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return false;
	}
	// Unbound is "all of them", which is what every list did before the delegate existed. Single-cast
	// because this is a QUESTION, and two answers to a question is an ambiguity nothing can resolve --
	// the same reason OnGetItemChildren is single-cast on the tree.
	if (!OnIsItemSelectableOrNavigable.IsBound())
	{
		return true;
	}
	return OnIsItemSelectableOrNavigable.Execute(InItemIndex, GetItemObject(InItemIndex));
}

void UDreamListViewBase::ScrollIndexIntoView(int32 InItemIndex)
{
	ScrollItemIntoView(InItemIndex, /*bInAnimate*/true);
}

void UDreamListViewBase::ScrollToTop()
{
	SetScrollOffset(0.0f);
}

void UDreamListViewBase::ScrollToBottom()
{
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->ScrollToEnd();
	}
}

void UDreamListViewBase::EndInertialScrolling()
{
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->EndInertialScrolling();
	}
}

float UDreamListViewBase::GetOverscroll() const
{
	if (ScrollBehaviour == nullptr)
	{
		return 0.0f;
	}
	const FVector2D Overscroll = ScrollBehaviour->GetOverscrollOffset();
	return static_cast<float>(IsHorizontalList() ? Overscroll.X : Overscroll.Y);
}

float UDreamListViewBase::GetViewFraction() const
{
	if (ScrollBehaviour == nullptr)
	{
		return 1.0f;
	}
	const bool bHorizontal = IsHorizontalList();
	const FVector2D ViewportSize = ScrollBehaviour->GetViewportSize();
	const FVector2D ContentSize = ScrollBehaviour->GetContentSize();
	const double Window = bHorizontal ? ViewportSize.X : ViewportSize.Y;
	const double Content = bHorizontal ? ContentSize.X : ContentSize.Y;
	// Rows that fit show all of themselves, which is one -- not a fraction over a smaller number.
	if (Content <= Window || Content <= UE_SMALL_NUMBER)
	{
		return 1.0f;
	}
	return static_cast<float>(FMath::Clamp(Window / Content, 0.0, 1.0));
}

void UDreamListViewBase::PushScrollBehaviourSettings()
{
	if (ScrollBehaviour == nullptr)
	{
		return;
	}
	// One list, called from the style push and from every setter, because a TEMPLATE-built list gets
	// a behaviour it just added -- carrying library defaults rather than what the list was authored
	// with. A knob only the setter pushed would be a knob that road silently did without.
	ScrollBehaviour->SetConsumeMouseWheel(ConsumeMouseWheel);
	ScrollBehaviour->SetAllowOverscroll(bAllowOverscroll);
	ScrollBehaviour->SetEnableTouchScrolling(bEnableTouchScrolling);
	ScrollBehaviour->SetAllowRightClickDragScrolling(bEnableRightClickScrolling);
	ScrollBehaviour->SetNavigationDestination(ScrollIntoViewDestination);
	ScrollBehaviour->SetPointerScrollingEnabled(bIsPointerScrollingEnabled);
	ScrollBehaviour->SetGamepadScrollingEnabled(bIsGamepadScrollingEnabled);
	// A SPEED here, a DURATION there. The two describe the same ease and the behaviour only knows
	// one of them, so the conversion is made once, at the push: a higher speed is a shorter glide.
	// The reciprocal, because that is the relationship, and the clamp keeps a zero out of it.
	ScrollBehaviour->SetAnimateWheelScrolling(bEnableScrollAnimation);
	ScrollBehaviour->SetWheelScrollAnimationDuration(
		1.0f / FMath::Max(0.01f, ScrollingAnimationInterpolationSpeed));
	ScrollBehaviour->SetAnimateTouchScrolling(bEnableScrollAnimation && bEnableTouchAnimatedScrolling);
}

void UDreamListViewBase::SetOrientation(EDreamPanelOrientation InOrientation)
{
	if (Orientation == InOrientation)
	{
		return;
	}
	Orientation = InOrientation;
	// Everything: the behaviour's axis, the column's anchors, the bar's edge, the gutter, and where
	// every row goes. ApplyStyle is the one road that settles all five.
	ApplyStyle();
	// And back to the near edge, because the offset it was holding was measured on the other axis:
	// carrying a vertical list's 400 into a horizontal one would land it somewhere arbitrary.
	SetScrollOffset(0.0f);
}

void UDreamListViewBase::SetEnableFixedLineOffset(bool bInEnable)
{
	bEnableFixedLineOffset = bInEnable;
	// Re-revealed immediately, so turning it on pins the row that is already chosen rather than
	// waiting for the next navigation press to do it.
	if (SelectedIndex != INDEX_NONE)
	{
		ScrollItemIntoView(SelectedIndex, /*bInAnimate*/false);
	}
}

void UDreamListViewBase::SetFixedLineScrollOffset(float InOffset)
{
	FixedLineScrollOffset = FMath::Clamp(InOffset, 0.0f, 1.0f);
	if (bEnableFixedLineOffset && SelectedIndex != INDEX_NONE)
	{
		ScrollItemIntoView(SelectedIndex, /*bInAnimate*/false);
	}
}

void UDreamListViewBase::SetAllowKeepPreselectedItems(bool bInAllow)
{
	bAllowKeepPreselectedItems = bInAllow;
	if (!bInAllow)
	{
		// Turning it off is what discards the parked set: leaving it would mean a selection landing
		// later from a source the author has stopped waiting for.
		PendingSelectedIndices.Reset();
	}
}

void UDreamListViewBase::SetEnableScrollAnimation(bool bInEnable)
{
	bEnableScrollAnimation = bInEnable;
	PushScrollBehaviourSettings();
}

void UDreamListViewBase::SetScrollingAnimationInterpolationSpeed(float InSpeed)
{
	ScrollingAnimationInterpolationSpeed = FMath::Max(0.01f, InSpeed);
	PushScrollBehaviourSettings();
}

void UDreamListViewBase::SetEnableTouchAnimatedScrolling(bool bInEnable)
{
	bEnableTouchAnimatedScrolling = bInEnable;
	PushScrollBehaviourSettings();
}

void UDreamListViewBase::SetIsPointerScrollingEnabled(bool bInEnable)
{
	bIsPointerScrollingEnabled = bInEnable;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetPointerScrollingEnabled(bInEnable);
	}
}

void UDreamListViewBase::SetIsGamepadScrollingEnabled(bool bInEnable)
{
	bIsGamepadScrollingEnabled = bInEnable;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetGamepadScrollingEnabled(bInEnable);
	}
}

void UDreamListViewBase::SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel InConsume)
{
	ConsumeMouseWheel = InConsume;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetConsumeMouseWheel(InConsume);
	}
}

void UDreamListViewBase::SetAllowOverscroll(bool bInAllow)
{
	bAllowOverscroll = bInAllow;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetAllowOverscroll(bInAllow);
	}
}

void UDreamListViewBase::SetEnableTouchScrolling(bool bInEnable)
{
	bEnableTouchScrolling = bInEnable;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetEnableTouchScrolling(bInEnable);
	}
}

void UDreamListViewBase::SetEnableRightClickScrolling(bool bInEnable)
{
	bEnableRightClickScrolling = bInEnable;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetAllowRightClickDragScrolling(bInEnable);
	}
}

void UDreamListViewBase::SetScrollIntoViewDestination(EDreamUIScrollDestination InDestination)
{
	// Configured is an ARGUMENT value -- "whatever this list is set up with" -- so storing it would
	// make the property its own answer.
	if (InDestination == EDreamUIScrollDestination::Configured)
	{
		return;
	}
	ScrollIntoViewDestination = InDestination;
	if (ScrollBehaviour != nullptr)
	{
		ScrollBehaviour->SetNavigationDestination(InDestination);
	}
}

int32 UDreamListViewBase::GetItemCount() const
{
	// Objects decide the count when there are any: a consumer that models rows as objects and hands
	// over fewer labels than objects means the extra rows to exist and be unlabelled, not to vanish.
	return ItemObjects.Num() > 0 ? ItemObjects.Num() : Items.Num();
}

FText UDreamListViewBase::GetItemLabel(int32 InItemIndex) const
{
	if (Items.IsValidIndex(InItemIndex))
	{
		return Items[InItemIndex];
	}
	if (const UObject* Item = GetItemObject(InItemIndex))
	{
		// A name, not a word: culture-invariant, the way every other glyph-or-identifier the library
		// puts on screen is.
		return FText::AsCultureInvariant(Item->GetName());
	}
	return FText::GetEmpty();
}

UObject* UDreamListViewBase::GetItemObject(int32 InItemIndex) const
{
	return ItemObjects.IsValidIndex(InItemIndex) ? ItemObjects[InItemIndex].Get() : nullptr;
}

void UDreamListViewBase::SetSelectedIndex(int32 InIndex)
{
	const int32 Previous = SelectedIndex;
	SetSelectedIndexWithoutNotify(InIndex);
	if (SelectedIndex != Previous)
	{
		OnSelectionChanged.Broadcast(SelectedIndex);
		OnValueChangedBP.Broadcast(SelectedIndex);
	}
}

void UDreamListViewBase::SetSelectedIndexWithoutNotify(int32 InIndex)
{
	// The veto is asked on this road too, not only on a click or a navigation step: a row the list
	// says cannot be chosen must not become the choice because the caller happened to be code. Only
	// for a real row -- clearing the selection is never refused, or a veto would be a trap.
	if (InIndex >= 0 && InIndex < GetItemCount() && !IsItemSelectableOrNavigable(InIndex))
	{
		return;
	}
	// Clamped where it becomes a selection, not where it is stored by an author: an index nothing
	// answers to is no selection at all.
	SelectedIndex = (InIndex >= 0 && InIndex < GetItemCount()) ? InIndex : INDEX_NONE;
	// "The selected row" is exactly one row, whatever the mode: this is the single-selection road,
	// and a multi selection that survived it would leave rows highlighted that this call did not name.
	SelectedIndices.Reset();
	if (SelectedIndex != INDEX_NONE)
	{
		SelectedIndices.Add(SelectedIndex);
	}
	RefreshRowColors();
}

void UDreamListViewBase::SetSelectionMode(EUIListSelectionMode InMode)
{
	if (SelectionMode == InMode)
	{
		return;
	}
	SelectionMode = InMode;
	// Narrowing the mode narrows the selection -- ReconcileSelection is where that rule lives, and
	// the repaint has to follow it or rows stay painted as selected after the mode says they are not.
	ReconcileSelection();
	RefreshRowColors();
}

void UDreamListViewBase::SetAlternatingRowColors(bool bInAlternating)
{
	if (bAlternatingRowColors == bInAlternating)
	{
		return;
	}
	bAlternatingRowColors = bInAlternating;
	// The one thing it decides, and nothing else: which colour each row wears. Not ApplyStyle, which
	// would re-push the whole sheet to answer a question about stripes.
	RefreshRowColors();
}

void UDreamListViewBase::SetShowScrollBar(bool bInShowScrollBar)
{
	if (bShowScrollBar == bInShowScrollBar)
	{
		return;
	}
	bShowScrollBar = bInShowScrollBar;
	// The gutter moves with the bar, so this is a re-layout rather than a visibility flip -- and the
	// gutter is cut in the style push.
	ApplyStyle();
}

void UDreamListViewBase::SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility InVisibility)
{
	if (ScrollBarVisibility == InVisibility)
	{
		return;
	}
	ScrollBarVisibility = InVisibility;
	ApplyStyle();
}

void UDreamListViewBase::SetVirtualizationThreshold(int32 InThreshold)
{
	const int32 Clamped = FMath::Max(0, InThreshold);
	if (VirtualizationThreshold == Clamped)
	{
		return;
	}
	VirtualizationThreshold = Clamped;
	// Whether the list POOLS is decided against this number, and the pool's size with it, so the rows
	// themselves are the thing that has to be re-derived. The only knob on this control for which
	// that is true -- which is why the other four restyle instead.
	RebuildRows();
}

void UDreamListViewBase::SetVirtualizationOverscan(int32 InOverscan)
{
	const int32 Clamped = FMath::Clamp(InOverscan, 0, 16);
	if (VirtualizationOverscan == Clamped)
	{
		return;
	}
	VirtualizationOverscan = Clamped;
	RebuildRows();
}

void UDreamListViewBase::SetWheelScrollMultiplier(float InMultiplier)
{
	const float Clamped = FMath::Max(0.0f, InMultiplier);
	if (WheelScrollMultiplier == Clamped)
	{
		return;
	}
	WheelScrollMultiplier = Clamped;
	// Pushed onto the scroll behaviour by the style push, which is where the wheel's units are
	// turned from rows into local units.
	ApplyStyle();
}

bool UDreamListViewBase::IsItemSelected(int32 InItemIndex) const
{
	return InItemIndex != INDEX_NONE && SelectedIndices.Contains(InItemIndex);
}

void UDreamListViewBase::SetItemSelection(int32 InItemIndex, bool bInSelected, bool bInClearOthers)
{
	if (SelectionMode == EUIListSelectionMode::None)
	{
		return;
	}
	if (InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return;
	}
	// The per-item veto, asked before anything moves. Deselecting is never vetoed: a row that became
	// unselectable while it was chosen has to be able to stop being chosen, or the veto becomes a
	// trap the consumer cannot get out of.
	if (bInSelected && !IsItemSelectableOrNavigable(InItemIndex))
	{
		return;
	}
	if (bClearScrollVelocityOnSelection)
	{
		// Before the selection, not after: the row the player picked must not slide out from under
		// the cursor while the click is still being handled.
		EndInertialScrolling();
	}
	const int32 PreviousAnchor = SelectedIndex;
	bool bSelectionMoved = false;

	// Every mode but Multi means one row, whatever the caller asked for.
	const bool bMustClearOthers = bInSelected
		&& (bInClearOthers || SelectionMode != EUIListSelectionMode::Multi);
	if (bMustClearOthers)
	{
		for (int32 Slot = SelectedIndices.Num() - 1; Slot >= 0; --Slot)
		{
			if (SelectedIndices[Slot] != InItemIndex)
			{
				SelectedIndices.RemoveAt(Slot);
				bSelectionMoved = true;
			}
		}
	}

	const bool bWasSelected = SelectedIndices.Contains(InItemIndex);
	if (bInSelected && !bWasSelected)
	{
		SelectedIndices.Add(InItemIndex);
		bSelectionMoved = true;
	}
	else if (!bInSelected && bWasSelected)
	{
		SelectedIndices.Remove(InItemIndex);
		bSelectionMoved = true;
	}

	// The anchor: the row this call landed on while it is still selected, else whatever is left.
	SelectedIndex = SelectedIndices.Contains(InItemIndex) && bInSelected
		? InItemIndex
		: (SelectedIndices.Contains(SelectedIndex)
			? SelectedIndex
			: (SelectedIndices.Num() > 0 ? SelectedIndices[0] : INDEX_NONE));

	// Repainted whenever the SET moved, not only when the anchor did: clicking the already-selected
	// row in Single mode leaves the anchor where it was while dropping every other row, and a repaint
	// gated on the anchor leaves those rows painted as selected. (UUIListView::SetItemSelection had
	// exactly this gate, and exactly that symptom.)
	if (bSelectionMoved || SelectedIndex != PreviousAnchor)
	{
		RefreshRowColors();
		OnSelectionChanged.Broadcast(SelectedIndex);
		OnValueChangedBP.Broadcast(SelectedIndex);
	}
}

void UDreamListViewBase::ClearSelection()
{
	if (SelectedIndices.Num() == 0 && SelectedIndex == INDEX_NONE)
	{
		return;
	}
	SelectedIndices.Reset();
	SelectedIndex = INDEX_NONE;
	RefreshRowColors();
	OnSelectionChanged.Broadcast(INDEX_NONE);
	OnValueChangedBP.Broadcast(INDEX_NONE);
}

TArray<UObject*> UDreamListViewBase::GetSelectedItems() const
{
	TArray<UObject*> Result;
	Result.Reserve(SelectedIndices.Num());
	for (int32 Index : SelectedIndices)
	{
		if (UObject* Item = GetItemObject(Index))
		{
			Result.Add(Item);
		}
	}
	return Result;
}

float UDreamListViewBase::GetScrollOffset() const
{
	if (ScrollBehaviour == nullptr)
	{
		return 0.0f;
	}
	const FVector2D Offset = ScrollBehaviour->GetScrollOffset();
	return static_cast<float>(IsHorizontalList() ? Offset.X : Offset.Y);
}

void UDreamListViewBase::SetScrollOffset(float InOffset)
{
	if (ScrollBehaviour != nullptr)
	{
		// The other half stays zero rather than being preserved: a list scrolls ONE way, and the
		// behaviour is told which on every style push.
		ScrollBehaviour->SetScrollOffset(IsHorizontalList()
			? FVector2D(InOffset, 0.0)
			: FVector2D(0.0, InOffset));
	}
}

void UDreamListViewBase::ReconcileSelection()
{
	const int32 Count = GetItemCount();
	// A selection parked earlier, now that the source is long enough to hold it. Before the sweep
	// below, so an index that has just become valid is kept rather than dropped a second time.
	if (bAllowKeepPreselectedItems && PendingSelectedIndices.Num() > 0)
	{
		for (int32 Slot = PendingSelectedIndices.Num() - 1; Slot >= 0; --Slot)
		{
			const int32 Parked = PendingSelectedIndices[Slot];
			if (Parked >= 0 && Parked < Count)
			{
				SelectedIndices.AddUnique(Parked);
				PendingSelectedIndices.RemoveAt(Slot);
			}
		}
	}
	// An index the source no longer answers to is no selection at all -- unless the author said a
	// selection made before its data arrived is worth keeping, in which case it is parked instead of
	// dropped. Parking it is the whole feature: a screen restoring "row 7 was selected" before its
	// source loads would otherwise lose it silently.
	if (bAllowKeepPreselectedItems)
	{
		for (int32 Index : SelectedIndices)
		{
			if (Index < 0 || Index >= Count)
			{
				PendingSelectedIndices.AddUnique(Index);
			}
		}
	}
	SelectedIndices.RemoveAll([Count](int32 InIndex)
	{
		return InIndex < 0 || InIndex >= Count;
	});

	if (SelectionMode == EUIListSelectionMode::None)
	{
		SelectedIndices.Reset();
		SelectedIndex = INDEX_NONE;
		return;
	}

	// An author who wrote SelectedIndex -- a .dui line, a details-panel edit, a `<->` binding -- means
	// that row selected, and the set is what the rows are painted from.
	if (SelectedIndex >= 0 && SelectedIndex < Count && !SelectedIndices.Contains(SelectedIndex))
	{
		if (SelectionMode != EUIListSelectionMode::Multi)
		{
			SelectedIndices.Reset();
		}
		SelectedIndices.Add(SelectedIndex);
	}
	if (SelectionMode != EUIListSelectionMode::Multi && SelectedIndices.Num() > 1)
	{
		// Narrowed from Multi with several rows already chosen: the anchor is the one that survives.
		const int32 Keep = SelectedIndices.Contains(SelectedIndex) ? SelectedIndex : SelectedIndices[0];
		SelectedIndices.Reset();
		SelectedIndices.Add(Keep);
	}
	// The mirror, last: SelectedIndex must name a row that is actually selected, because it is what a
	// two-way binding reads back out.
	if (!SelectedIndices.Contains(SelectedIndex))
	{
		SelectedIndex = SelectedIndices.Num() > 0 ? SelectedIndices[0] : INDEX_NONE;
	}
}

UDreamWidget* UDreamListViewBase::GetRowWidget(int32 InItemIndex) const
{
	const int32 PoolIndex = RowSourceIndices.IndexOfByKey(InItemIndex);
	return RowNodes.IsValidIndex(PoolIndex) ? RowNodes[PoolIndex].Get() : nullptr;
}

int32 UDreamListViewBase::GetRowItemIndex(int32 InPoolIndex) const
{
	return RowSourceIndices.IsValidIndex(InPoolIndex) ? RowSourceIndices[InPoolIndex] : INDEX_NONE;
}

/**
 * The least movement that brings an item's row fully into the window -- computed from the pitch,
 * not from a widget.
 *
 * Which is the only version that answers while recycling: the row for the item being scrolled to is
 * usually the one that does not exist yet. It is also exact where the widget version was
 * approximate, because the column's geometry is authored rather than measured.
 */
bool UDreamListViewBase::ScrollItemIntoView(int32 InItemIndex, bool bInAnimate)
{
	if (ScrollBehaviour == nullptr)
	{
		return false;
	}
	const int32 DisplayIndex = VisibleItemIndices.IndexOfByKey(InItemIndex);
	if (DisplayIndex == INDEX_NONE)
	{
		return false;
	}
	const float Window = GetViewportMainExtent();
	if (Window <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float RowTop = GetRowTopOffset(DisplayIndex);
	const float RowBottom = RowTop + ResolveListStyle().RowHeight;
	const float Offset = GetScrollOffset();

	float Target = Offset;
	if (bEnableFixedLineOffset)
	{
		// The row is PINNED at a fixed share of the window rather than merely brought inside it --
		// UMG's fixed line offset, and what a cursor-driven menu wants: the highlighted row stays
		// put and the list moves underneath it, instead of the row sliding to whichever edge it
		// happened to come in from. Clamped at both ends, because the ends have no line to spare.
		Target = RowTop - Window * FMath::Clamp(FixedLineScrollOffset, 0.0f, 1.0f);
	}
	else if (RowTop < Offset)
	{
		Target = RowTop;
	}
	else if (RowBottom > Offset + Window)
	{
		// A row longer than the window can never be framed, so show its near edge -- the same answer
		// the scroll view gives for an oversized child.
		Target = (RowBottom - RowTop) > Window ? RowTop : RowBottom - Window;
	}
	Target = FMath::Max(0.0f, Target);
	if (FMath::IsNearlyEqual(Target, Offset, 0.01f))
	{
		return false;
	}
	SetScrollOffset(Target);
	return true;
}

void UDreamListViewBase::CollectVisibleItemIndices(TArray<int32>& OutIndices) const
{
	const int32 Count = GetItemCount();
	OutIndices.Reset(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutIndices.Add(Index);
	}
}

FDreamListStyle UDreamListViewBase::ResolveListStyle() const
{
	// Abstract in spirit only: a CDO is constructed for an abstract class too, so this stays
	// callable rather than pure. Every concrete control overrides it with its own family.
	static const FDreamListStyle Fallback;
	return Fallback;
}

FMargin UDreamListViewBase::GetRowPadding()
{
	// Hardcoded, the way UDreamDropdown hardcodes its caption inset. FDreamListStyle::Padding is
	// already the VIEWPORT's inset; a second margin in the same struct would read as the same number
	// to everyone who saw the panel, and the two mean different things.
	return FMargin(10.0f, 0.0f, 10.0f, 0.0f);
}

FDreamListStyle UDreamListView::ResolveListStyle() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ListStyle);
}

void UDreamListView::SetStyle(const FDreamListStyle& InStyle)
{
	Style = InStyle;
	// A row's height, spacing and colours are all style, so this is a rebuild rather than a repaint --
	// which is what ApplyStyle does anyway, and the reason RebuildRows sits inside it.
	ApplyStyle();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "List", UDreamListView)
