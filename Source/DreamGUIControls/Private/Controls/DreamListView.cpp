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
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamKeyEventData.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"

UDreamListViewBase::UDreamListViewBase()
{
	// One Tab stop, entered at ResolveTabEntry's row: the arrows move inside a list, and Tab moves past it
	// -- what a browser's list box does, and what keeps a thousand rows from being a thousand Tabs.
	TabNavigation = EDreamWidgetTabNavigation::Once;
}

UDreamWidget* UDreamListViewBase::ResolveTabEntry(bool bInBackward)
{
	// The selected item first, while its row could show at all (a tree folds items away) and the veto lets
	// navigation land there; else the first item that may be landed on, from whichever end Tab came in at.
	int32 EntryItem = INDEX_NONE;
	if (SelectedIndex != INDEX_NONE && VisibleItemIndices.Contains(SelectedIndex) && IsItemSelectableOrNavigable(SelectedIndex))
	{
		EntryItem = SelectedIndex;
	}
	for (int32 Step = 0; EntryItem == INDEX_NONE && Step < VisibleItemIndices.Num(); ++Step)
	{
		const int32 Candidate = VisibleItemIndices[bInBackward ? VisibleItemIndices.Num() - 1 - Step : Step];
		if (IsItemSelectableOrNavigable(Candidate))
		{
			EntryItem = Candidate;
		}
	}
	if (EntryItem == INDEX_NONE)
	{
		return nullptr;
	}
	// By index, and at once, as a navigation press reveals what it lands on: a recycling list has rows only
	// for what shows, and the row standing for the item exists once the window has moved onto it.
	ScrollItemIntoView(EntryItem, /*bInAnimate*/false);
	UDreamWidget* Row = GetRowWidget(EntryItem);
	if (IsValid(Row))
	{
		// Focus goes to the row that shows the item, so an item a re-bound row was keeping for it is done with.
		ClearFocusAnchor();
	}
	return Row;
}

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
		// Never a stop, whatever a bar's own defaults say: the track used to be the second Tab's landing
		// place, between a row and whatever followed the list, and the pad's press along its axis moved
		// the bar instead of the rows. The list is one stop and the arrows step its rows.
		ScrollBarNode->SetTabNavigation(EDreamWidgetTabNavigation::None);
		if (ScrollBarNode->BarBehaviour != nullptr)
		{
			ScrollBarNode->BarBehaviour->SetCanNavigateHere(false);
		}
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
	const bool bHorizontal = IsHorizontalList();
	// The column's anchors for THIS orientation, before its size is written. The scrolled axis is a
	// POINT anchor -- a stretched one would pin the column to the viewport and nothing would ever
	// scroll -- and the other one stretched, so a row is as wide as the list whatever the list turns out
	// to be. Stated here rather than after the size: a size written onto an axis still stretched from
	// the other orientation is resolved against the viewport's span and stored as the difference, so a
	// list turned sideways kept its content one viewport short until the next rebuild.
	ColumnNode->SetPivot(bHorizontal ? FVector2D(0.0, 0.5) : FVector2D(0.5, 1.0));
	ColumnNode->SetHorizontalAndVerticalAnchorMinMax(
		bHorizontal ? FVector2D(0.0, 0.0) : FVector2D(0.0, 1.0),
		bHorizontal ? FVector2D(0.0, 1.0) : FVector2D(1.0, 1.0), false, false);
	// The DELTA on the stretched axis, never the size: a zero delta says "exactly the span", whenever
	// the span is decided. SetWidth / SetHeight would be the wrong verb there -- on a stretched axis
	// they resolve the parent's span at write time and bake the difference in, so a list built before
	// it was sized would carry minus its eventual width forever.
	FVector2D ColumnDelta = ColumnNode->GetSizeDelta();
	(bHorizontal ? ColumnDelta.Y : ColumnDelta.X) = 0.0;
	ColumnNode->SetSizeDelta(ColumnDelta);
	// And no offset across: that axis never scrolls, so a position left on it is what scrolling in the
	// OTHER orientation wrote there, and it would hold every row that far out of the window.
	FVector2D ColumnPosition = ColumnNode->GetAnchoredPosition();
	if ((bHorizontal ? ColumnPosition.Y : ColumnPosition.X) != 0.0)
	{
		(bHorizontal ? ColumnPosition.Y : ColumnPosition.X) = 0.0;
		ColumnNode->SetAnchoredPosition(ColumnPosition);
	}

	// The column's extent ALONG THE SCROLL AXIS is the scroll range, stated rather than measured:
	// lines, gaps and the viewport's own inset. The column is point-anchored on that axis, so the
	// setter writes the SizeDelta straight through and leaves the anchored position -- where the
	// scroll offset lives -- alone. Which setter it is is the only thing orientation changes.
	const int32 LineCount = GetLineCount();
	const float Extent = GetMainPadStart(InStyle) + GetMainPadEnd(InStyle)
		+ LineCount * InStyle.RowHeight
		+ FMath::Max(0, LineCount - 1) * InStyle.RowSpacing;
	if (bHorizontal)
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
	const float Pitch = GetRowPitch();
	// A viewport with no resolvable extent is one nothing has arranged and nobody authored -- the
	// first frame of a control a consumer's layout has not reached yet. A fixed window rather than a
	// guess of zero, because a zero-row window is a list that stays blank until something else
	// happens to resize it; the dimensions handler re-asks the moment there IS an answer.
	//
	// A pitch under one unit gets the same window, for the same reason: a style with no row height (an
	// old asset, a zero handed to SetStyle) stacks every row on the one before it, so "how many fit"
	// comes out as all of them -- and the widget per item that followed drew nothing at all.
	const int64 FromViewport = (ViewportMain > KINDA_SMALL_NUMBER && Pitch >= 1.0f)
		? static_cast<int64>(FMath::CeilToDouble(static_cast<double>(ViewportMain) / Pitch)) + 1
		: 16;
	// LINES times the column count: the arithmetic above is about how far there is to scroll, and a
	// line of a tile view holds several widgets. In int64 and clamped to the source, because a grid of
	// small tiles in a tall viewport can multiply past what an int32 holds, and no window is ever
	// larger than the items it shows.
	const int64 Lines = FMath::Max<int64>(1, FromViewport + FMath::Max(0, VirtualizationOverscan) * 2);
	const int64 Window = Lines * FMath::Max(1, ResolveColumnCount());
	return static_cast<int32>(FMath::Clamp<int64>(Window, 0, VisibleItemIndices.Num()));
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
	RefreshVisibleWindow(/*bInRebindKeptRows*/true);
	FinishDragFromDestroyedRow();
	// The batch boundary, and it IS the return of this function: this control rebuilds synchronously,
	// so there is no pending request for a later frame to complete and nothing else to wait for.
	OnRowsGenerated.Broadcast(RowNodes.Num());
	// A selection asked for before its items arrived landed in this rebuild (ReconcileSelection). Said
	// once, and after the rows it selects exist, the way a source edit that takes a selection away is:
	// a binding that pushed the index first hears it back only now, when it means a row.
	if (bAnnouncePendingSelection)
	{
		bAnnouncePendingSelection = false;
		AnnounceSelection();
	}
}

void UDreamListViewBase::ResizePool(int32 InPoolSize)
{
	InPoolSize = FMath::Max(0, InPoolSize);
	// The bindings are as long as the pool, whatever filled the arrays before: every reader indexes them by pool slot.
	RowBindings.SetNum(RowNodes.Num());
	while (RowNodes.Num() > InPoolSize)
	{
		const int32 Index = RowNodes.Num() - 1;
		// Released before it goes, as a parked row is: the widget stops standing for its item for good,
		// and a consumer that hung something on it at generation time has to hear that while the widget
		// is still there to undo it on. Destroying it silently left a removed item with no release at all.
		ReleaseRow(Index, /*bInItemChanges*/true);
		if (!RowNodes.IsValidIndex(Index))
		{
			// A release handler re-shaped the pool; what is left of it is measured again.
			continue;
		}
		HoveredPoolIndices.Remove(Index);
		if (Index == FocusAnchorPoolIndex)
		{
			ClearFocusAnchor();
		}
		if (bIsDragging && Index == DraggedPoolIndex)
		{
			// The row a drag started on: its source behaviour goes with it, and so does the end-of-drag
			// it would have heard. Ended once the rows are settled (FinishDragFromDestroyedRow).
			bDragSourceRowDestroyed = true;
		}
		UDreamWidget* Row = RowNodes[Index].Get();
		RowNodes.RemoveAt(Index);
		RowSourceIndices.RemoveAt(Index);
		RowBindings.RemoveAt(Index);
		if (IsValid(Row))
		{
			Row->DestroyWidget();
		}
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
		RowBindings.AddDefaulted();
	}
}

/**
 * The window the pool is showing, and the whole of what recycling is.
 *
 * Not virtualizing, the window is everything and this is one bind per item -- the same loop the
 * control has always run, reached by the same road, which is what keeps the two behaviours from
 * being two implementations.
 */
void UDreamListViewBase::RefreshVisibleWindow(bool bInRebindKeptRows)
{
	if (RowNodes.Num() == 0)
	{
		// Nothing is realized, so nothing will be realized "again" either: an item that comes back once
		// there are rows has arrived, and the set left from before would have kept that quiet.
		RealizedItemIndices.Reset();
		return;
	}
	const FDreamListStyle& Active = ResolveListStyle();
	const int32 RowCount = VisibleItemIndices.Num();
	RowBindings.SetNum(RowNodes.Num());

	int32 FirstDisplayIndex = 0;
	if (bVirtualizing)
	{
		const float Offset = GetScrollOffset();
		// The first LINE whose far edge is still past the window's near one, less the overscan, times
		// the column count. Clamped so the LAST window is a full one rather than a short one with
		// blank rows after it -- and the clamp is allowed to land mid-line, because a row's place is
		// computed from its own display index rather than from where the window happens to start.
		// In int64 for the reason ResolveWindowSize gives: a pitch of a fraction of a unit divides an
		// ordinary offset into more lines than an int32 can count.
		const int64 Columns = FMath::Max(1, ResolveColumnCount());
		const double LinesBefore = FMath::FloorToDouble(FMath::Max(0.0f, Offset - GetMainPadStart(Active)) / GetRowPitch());
		const int64 FirstVisibleLine = static_cast<int64>(FMath::Min(LinesBefore, static_cast<double>(MAX_int32)));
		const int64 FirstLine = FMath::Max<int64>(0, FirstVisibleLine - FMath::Max(0, VirtualizationOverscan));
		FirstDisplayIndex = static_cast<int32>(FMath::Clamp<int64>(FirstLine * Columns,
			0, FMath::Max(0, RowCount - RowNodes.Num())));
	}
	WindowStart = FirstDisplayIndex;
	const int32 WindowEnd = FMath::Min(RowCount, FirstDisplayIndex + RowNodes.Num());

	// Which row shows which slot of the window. A row whose item is still inside the window KEEPS it,
	// wherever the window moved, and only the rows whose items left are handed the ones that arrived.
	// Handing slot N of the window to pool row N instead re-bound every row on every line scrolled: the
	// widget under a resting pointer -- or holding focus -- swapped its item for the next one without
	// anything telling the hover or the focus, and every frame of a fling re-announced every row.
	TMap<int32, int32> DisplayOfItem;
	DisplayOfItem.Reserve(FMath::Max(0, WindowEnd - FirstDisplayIndex));
	for (int32 DisplayIndex = FirstDisplayIndex; DisplayIndex < WindowEnd; ++DisplayIndex)
	{
		DisplayOfItem.Add(VisibleItemIndices[DisplayIndex], DisplayIndex);
	}
	TArray<int32> DisplayOfRow;
	DisplayOfRow.Init(INDEX_NONE, RowNodes.Num());
	TSet<int32> SlotsTaken;
	for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		const int32* Kept = DisplayOfItem.Find(RowSourceIndices[PoolIndex]);
		if (Kept != nullptr && !SlotsTaken.Contains(*Kept))
		{
			DisplayOfRow[PoolIndex] = *Kept;
			SlotsTaken.Add(*Kept);
		}
	}
	// The arrivals, in display order, to the rows that are free, in pool order -- which from a standing
	// start is the pool in display order, the layout every rebuild from nothing has always produced.
	int32 NextFreeRow = 0;
	for (int32 DisplayIndex = FirstDisplayIndex; DisplayIndex < WindowEnd; ++DisplayIndex)
	{
		if (SlotsTaken.Contains(DisplayIndex))
		{
			continue;
		}
		while (NextFreeRow < RowNodes.Num() && DisplayOfRow[NextFreeRow] != INDEX_NONE)
		{
			++NextFreeRow;
		}
		if (NextFreeRow >= RowNodes.Num())
		{
			break;
		}
		DisplayOfRow[NextFreeRow] = DisplayIndex;
	}

	// The window that WAS, kept so the arrivals can be told from the stays: "this item came into
	// view" is an edge, and a consumer loading a thumbnail per row wants one call per arrival rather
	// than one per scroll. The new set is built aside and stored only once every row is bound, so a
	// handler that re-enters this function in the middle sees the window as it was.
	TSet<int32> NowRealized;
	// A handler of the row events below can rebuild the list from inside this loop. That nested pass
	// leaves the rows as they should be, and this one stops rather than binding on from stale slots.
	const uint32 Serial = ++WindowRefreshSerial;
	for (int32 PoolIndex = 0; PoolIndex < DisplayOfRow.Num() && PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		const int32 DisplayIndex = DisplayOfRow[PoolIndex];
		if (!VisibleItemIndices.IsValidIndex(DisplayIndex))
		{
			ParkRow(PoolIndex);
		}
		else
		{
			const int32 ItemIndex = VisibleItemIndices[DisplayIndex];
			// A row kept on its item and in its place has nothing to be told while the list scrolls: no
			// look to re-push and no release or generation to announce. A rebuild, a style push and a
			// resize re-push every row, because those are what changed the look.
			if (bInRebindKeptRows || !IsRowBoundTo(PoolIndex, ItemIndex, GetItemObject(ItemIndex))
				|| RowBindings[PoolIndex].DisplayIndex != DisplayIndex)
			{
				BindRow(PoolIndex, DisplayIndex, ItemIndex, Active);
			}
			NowRealized.Add(ItemIndex);
		}
		if (WindowRefreshSerial != Serial)
		{
			return;
		}
	}

	// After every bind, not inside the loop: a handler that asks the list what else is on screen has
	// to be told the whole answer rather than however much of it had been written when it was called.
	// From a list of its own, too: a handler that loads more items re-enters this function, which
	// rewrites the set this used to be walking -- a broken iteration, and arrivals said twice.
	TArray<int32> Arrivals;
	for (int32 ItemIndex : NowRealized)
	{
		if (!RealizedItemIndices.Contains(ItemIndex))
		{
			Arrivals.Add(ItemIndex);
		}
	}
	// In display order, as they were said when pool order was display order: source order is display
	// order for a list and for a tree's pre-order walk alike.
	Arrivals.Sort();
	RealizedItemIndices = MoveTemp(NowRealized);
	for (int32 ItemIndex : Arrivals)
	{
		// Asked again before each one: an earlier handler may have moved the window past it.
		if (RealizedItemIndices.Contains(ItemIndex))
		{
			OnItemScrolledIntoView.Broadcast(ItemIndex, GetRowWidget(ItemIndex), GetItemObject(ItemIndex));
		}
	}
}

void UDreamListViewBase::HandleScrollViewMoved(FVector2D InProgress)
{
	if (bVirtualizing)
	{
		// Only the rows the move handed a new item: nothing else about a row changes when the list scrolls.
		RefreshVisibleWindow(/*bInRebindKeptRows*/false);
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
	// own; a flick is one only on the frame its momentum runs out; a drag is never one until it ends;
	// and neither is a step of edge scrolling, which says it finished when the scroll stops.
	const bool bStillMoving = bScrollDragInProgress || bDragEdgeScrollStepping
		|| (ScrollBehaviour != nullptr && ScrollBehaviour->IsScrolling());
	if (!bStillMoving)
	{
		// A reveal's glide that has landed, or been stopped, is no longer where the list is heading.
		bRevealGlideActive = false;
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
	// A finger's drag is the list's unless the list says its rows are picked up by finger: STableRow::OnDragDetected
	// captures a touch for its table and returns before the row's own drag (OnDragDetected_Handler) is asked, so a finger
	// scrolls a list of draggable rows as it scrolls any other. Refused the same way, so the scroll view gets it.
	const bool bFinger = EventData != nullptr && EventData->InputType == EDreamUIPointerInputType::Pointer
		&& DreamUIPointerIds::IsTouch(EventData->PointerID);
	if (bFinger && OwningList->GetFingerDrag() == EDreamListFingerDrag::ScrollList)
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
	// Only a drag and drop drops. A drag that carries no operation -- a finger that scrolled the list and lifted over a
	// row -- reaches here too, as the pipeline tells whatever a drag ends over, and it is no drop: Slate calls OnDrop only
	// for a drag-drop event, which only an operation makes. The base refuses it the same way.
	if (EventData == nullptr || !IsValid(EventData->DragOperation))
	{
		return true;
	}
	// The zone is a question about this row's rect and the pointer, which is why it is answered here
	// rather than in the base: a drop target in general has no rows and no order to insert into.
	if (OwningList != nullptr)
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
				// And the frames in between, which is where edge scrolling steps.
				Target->OnDragOver.AddDynamic(Target, &UDreamListRowDropTarget::HandleDragOver);
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

void UDreamListViewBase::SetFingerDrag(EDreamListFingerDrag InFingerDrag)
{
	FingerDrag = InFingerDrag;
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
	if (!bInAllow)
	{
		// No row accepts a drop any more, so none will be hovered -- and a scroll that was riding the
		// hover would otherwise never hear that it had stopped.
		StopDragEdgeScroll();
	}
}

void UDreamListViewBase::SetEnableDragEdgeScrolling(bool bInEnable)
{
	if (bEnableDragEdgeScrolling == bInEnable)
	{
		return;
	}
	bEnableDragEdgeScrolling = bInEnable;
	if (!bInEnable)
	{
		StopDragEdgeScroll();
	}
}

void UDreamListViewBase::SetDragEdgeScrollBandSize(float InBandSize)
{
	// Nothing to push: the band is read on every step, which is also what makes a change reach a drag
	// already under way.
	DragEdgeScrollBandSize = FMath::Max(0.0f, InBandSize);
}

void UDreamListViewBase::SetDragEdgeScrollSpeed(float InSpeed)
{
	DragEdgeScrollSpeed = FMath::Max(0.0f, InSpeed);
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
	// The row, so destroying it can end the drag (ResizePool), and the item's object, so a source edit
	// in mid-flight can say where the item went (SetItemObjects).
	DraggedPoolIndex = InPoolIndex;
	DraggedItemObject = GetItemObject(ItemIndex);
	ActiveDragOperation = InOperation;
	// The item index goes out with it, because the payload alone cannot answer "which row" for a
	// text-only source -- which has no objects at all.
	OnItemDragDetected.Broadcast(ItemIndex, GetItemObject(ItemIndex), InOperation);
	OnDraggingStateChanged.Broadcast(true);
}

void UDreamListViewBase::HandleRowDragEnded(UDreamDragDropOperation* InOperation)
{
	// Letting go ends an edge scroll whatever became of the drop. Before the early return: a scroll
	// can be carrying a drag that started somewhere else, which this list has no flag for.
	StopDragEdgeScroll();
	if (!bIsDragging)
	{
		// Already ended -- by the row that started it being destroyed, say -- so a later end from the
		// pipeline has nothing left to say.
		return;
	}
	// Whether anything took it. The operation is the one thing that outlives the row: a recycled row
	// or a closed screen can destroy the source mid-flight, and the flag would go with it.
	const bool bHandled = InOperation != nullptr && InOperation->bDropWasHandled;
	// The item as it is NOW: the index was re-located with the source (SetItemObjects), and the object
	// is the one picked up, not whatever the source holds at the index the drag started on.
	const int32 ItemIndex = DraggedItemIndex;
	UObject* const Item = DraggedItemObject.Get();
	bIsDragging = false;
	DraggedItemIndex = INDEX_NONE;
	DraggedPoolIndex = INDEX_NONE;
	DraggedItemObject.Reset();
	bDragSourceRowDestroyed = false;
	ActiveDragOperation = nullptr;
	if (!bHandled)
	{
		OnItemDragCancelled.Broadcast(ItemIndex, Item, InOperation);
	}
	OnDraggingStateChanged.Broadcast(false);
}

void UDreamListViewBase::FinishDragFromDestroyedRow()
{
	if (!bDragSourceRowDestroyed)
	{
		return;
	}
	bDragSourceRowDestroyed = false;
	// The row the drag started on is gone, and with it the source behaviour that would have heard the
	// drag end: the pointer pipeline has no widget left to deliver that end to. So it ends here, as
	// whatever it has become -- a drop, when a target already took it (a consumer re-ordering the
	// source from OnItemAcceptDrop is one way the row goes), and a cancel otherwise. Left alone the
	// list stayed dragging for good, holding the operation, and never said the drag was over.
	HandleRowDragEnded(ActiveDragOperation);
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
	if (bDragEdgeScrollActive)
	{
		// A scroll moves rows under a pointer that is holding still, so leaving ONE row is routine and
		// the next row's Over follows in the same frame. Leaving the viewport is the end of it -- and
		// so is a pointer that is no longer dragging at all, which is what a release looks like here.
		FVector PointerWorldPoint;
		if (!FindDragPointerWorldPoint(InOperation, PointerWorldPoint) || !IsWorldPointOverViewport(PointerWorldPoint))
		{
			StopDragEdgeScroll();
		}
	}
}

void UDreamListViewBase::HandleRowDragOver(int32 InPoolIndex, UDreamDragDropOperation* InOperation)
{
	StepDragEdgeScroll(InOperation);
}

void UDreamListViewBase::StepDragEdgeScroll(UDreamDragDropOperation* InOperation)
{
	UWorld* World = GetWorld();
	if (!bEnableDragEdgeScrolling || DragEdgeScrollBandSize <= 0.0f || DragEdgeScrollSpeed <= 0.0f
		|| ScrollBehaviour == nullptr || !IsValid(ViewportNode) || World == nullptr)
	{
		StopDragEdgeScroll();
		return;
	}
	// One step a frame. Over arrives twice in one: from the pointer's Drag event, which the subsystem
	// follows, and again from the subsystem's own tick. The REAL clock, because the subsystem ticks
	// through a pause and so does a drag under the player's finger.
	const double Now = World->GetRealTimeSeconds();
	if (Now == LastDragEdgeScrollTime)
	{
		return;
	}
	LastDragEdgeScrollTime = Now;

	FVector PointerWorldPoint;
	const float Direction = FindDragPointerWorldPoint(InOperation, PointerWorldPoint)
		? ResolveDragEdgeScrollDirection(PointerWorldPoint)
		: 0.0f;
	if (Direction == 0.0f)
	{
		StopDragEdgeScroll();
		return;
	}
	const float Step = Direction * DragEdgeScrollSpeed * World->GetDeltaSeconds();
	{
		// Scoped, so the move this step makes is not announced as finished while the scroll is still
		// going -- and so nothing can leave the flag up.
		TGuardValue<bool> Stepping(bDragEdgeScrollStepping, true);
		SetScrollOffset(GetScrollOffset() + Step);
	}
	// Active even when the step was clamped at an end: the drag is still asking to scroll, and the
	// scroll ends when the drag stops asking, not when the list runs out.
	bDragEdgeScrollActive = true;
}

void UDreamListViewBase::StopDragEdgeScroll()
{
	if (!bDragEdgeScrollActive)
	{
		return;
	}
	bDragEdgeScrollActive = false;
	// The one "finished" the scroll owes, now that it has one: every step was a move, none an end.
	OnListViewFinishedScrolling.Broadcast(GetScrollOffset(), GetViewFraction());
}

bool UDreamListViewBase::FindDragPointerWorldPoint(UDreamDragDropOperation* InOperation, FVector& OutWorldPoint)
{
	if (!IsValid(InOperation))
	{
		return false;
	}
	// Every player's pointers: the operation does not say whose drag it is, and a second player's drag over
	// this list edge-scrolls it as the first player's does.
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(this);
	if (Input == nullptr)
	{
		return false;
	}
	TArray<UDreamUIInputUser*> Users;
	Input->GetUsers(Users);
	for (const UDreamUIInputUser* User : Users)
	{
		for (const TPair<int32, TObjectPtr<UDreamPointerEventData>>& Pair : User->GetPointerEventDataMap())
		{
			const UDreamPointerEventData* EventData = Pair.Value.Get();
			if (IsValid(EventData) && EventData->bIsDragging && EventData->DragOperation == InOperation)
			{
				// On the plane the press was made on, which is the canvas this list is drawn on -- the same
				// point a drop's zone is worked out from.
				OutWorldPoint = EventData->GetWorldPointInPlane();
				return true;
			}
		}
	}
	return false;
}

float UDreamListViewBase::ResolveDragEdgeScrollDirection(const FVector& InWorldPoint) const
{
	if (!IsWorldPointOverViewport(InWorldPoint))
	{
		return 0.0f;
	}
	// Local space runs Y right and Z up. The scroll offset runs the other way on the vertical axis --
	// forward is DOWN the list -- so the far band of a vertical list is its bottom edge.
	const FVector Local = ViewportNode->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	float FromNearEdge = 0.0f;
	float FromFarEdge = 0.0f;
	if (IsHorizontalList())
	{
		FromNearEdge = static_cast<float>(Local.Y) - ViewportNode->GetLocalSpaceLeft();
		FromFarEdge = ViewportNode->GetLocalSpaceRight() - static_cast<float>(Local.Y);
	}
	else
	{
		FromNearEdge = ViewportNode->GetLocalSpaceTop() - static_cast<float>(Local.Z);
		FromFarEdge = static_cast<float>(Local.Z) - ViewportNode->GetLocalSpaceBottom();
	}
	// Nearer edge first, for a viewport so short the two bands overlap.
	if (FromFarEdge < DragEdgeScrollBandSize && FromFarEdge <= FromNearEdge)
	{
		return 1.0f;
	}
	if (FromNearEdge < DragEdgeScrollBandSize)
	{
		return -1.0f;
	}
	return 0.0f;
}

bool UDreamListViewBase::IsWorldPointOverViewport(const FVector& InWorldPoint) const
{
	if (!IsValid(ViewportNode))
	{
		return false;
	}
	const FVector Local = ViewportNode->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	return Local.Y >= ViewportNode->GetLocalSpaceLeft() && Local.Y <= ViewportNode->GetLocalSpaceRight()
		&& Local.Z >= ViewportNode->GetLocalSpaceBottom() && Local.Z <= ViewportNode->GetLocalSpaceTop();
}

bool UDreamListViewBase::HandleRowDrop(int32 InPoolIndex, UDreamDragDropOperation* InOperation, EDreamItemDropZone InZone)
{
	// A drop is the end of any edge scroll that brought the pointer here.
	StopDragEdgeScroll();
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
	//
	// Marked handled BEFORE the consumer hears of it, the order UDreamUIDropTarget keeps for its own
	// handlers: a consumer that re-orders the source from here can destroy the row the drag started on,
	// and the drag that ends with it has to end as the drop it is, not as a cancel.
	if (InOperation != nullptr)
	{
		InOperation->bDropWasHandled = true;
	}
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

void UDreamListRowDropTarget::HandleDragOver(UDreamDragDropOperation* InOperation)
{
	if (OwningList != nullptr)
	{
		OwningList->HandleRowDragOver(PoolIndex, InOperation);
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
	if (InPhase == EDreamScrollDragPhase::Begin)
	{
		// A drag stops a reveal's glide where it got to, so the list is no longer heading for its end.
		bRevealGlideActive = false;
	}
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

void UDreamListViewBase::HandleRowSelectionStateChanged(int32 InPoolIndex, EUISelectableSelectionState InState)
{
	// A row that was handed another item while focus stayed on it answers navigation from the item focus
	// was on (GetNavigationItemIndex) -- until the pointer takes the row over or focus leaves it. Whether
	// focus left is the row's button's to say, not the state it draws: focus is drawn only after keys or
	// a pad, so a row focused by a click draws Normal while it keeps the focus, and every re-bind repaints
	// it -- reading the drawn state dropped the anchor the moment it was set.
	if (InPoolIndex == FocusAnchorPoolIndex)
	{
		const UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
		const UUIButton* RowButton = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
		const bool bPointerTookRow = InState == EUISelectableSelectionState::Hovered || InState == EUISelectableSelectionState::Pressed;
		if (bPointerTookRow || RowButton == nullptr || !RowButton->IsFocused())
		{
			ClearFocusAnchor();
		}
	}
	// The ITEM is what is hovered, not the widget: a row re-bound under a resting pointer is a
	// different item hovered, and a consumer keyed by index would otherwise be told about the old one
	// forever. Tracked as a set of pool indices rather than an array parallel to the pool, so nothing
	// has to be resized when the pool grows or shrinks.
	const bool bInHovered = InState == EUISelectableSelectionState::Hovered || InState == EUISelectableSelectionState::Pressed;
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
	// Every row: a tile's place across its line moves with the width.
	RefreshVisibleWindow(/*bInRebindKeptRows*/true);
	FinishDragFromDestroyedRow();
}

void UDreamListViewBase::RefreshScrollFurniture(const FDreamListStyle& InStyle)
{
	const bool bBarVisible = ShouldShowScrollBar();
	// A Hidden bar keeps its room while the rows overflow, as a Hidden Slate bar keeps its slot (UDreamScrollBox's
	// rule), so a list that stops overflowing gives the room back and one that starts takes it, drawn or not.
	const bool bReserveGutter = bBarVisible || (bShowScrollBar
		&& ScrollBarVisibility == EDreamScrollBoxScrollbarVisibility::Hidden && IsListContentOverflowing());
	const float Gutter = bReserveGutter ? InStyle.Bar.Thickness : 0.0f;
	const bool bHorizontal = IsHorizontalList();
	// What the content was just measured with, by the caller's RefreshContentHeight.
	const int32 ColumnsMeasured = ResolveColumnCount();

	// The gutter takes from the viewport's CROSS axis and the overflow question is about the main one,
	// and a row's length does not depend on how wide it is -- but how many tiles a LINE holds does: a
	// tile view counts its columns across the very axis the gutter just narrowed. So this is
	// UDreamScrollBox's measure, decide and re-state, with the re-statement below. The decision cannot
	// flip on it: a gutter that appears only ever takes columns away, making a content that already
	// overflowed longer still, and one that goes only ever gives them back to a content that fitted.
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

	// The column's anchors are re-stated with its extent, in RefreshContentHeight -- every push, since
	// the built-in tree authors the vertical answer and Orientation is an editable property. Here it is
	// only measured again, when the gutter changed how many tiles fit across.
	if (ResolveColumnCount() != ColumnsMeasured)
	{
		RefreshContentHeight(InStyle);
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
	switch (ScrollBarVisibility)
	{
	case EDreamScrollBoxScrollbarVisibility::Permanent:
		return true;
	case EDreamScrollBoxScrollbarVisibility::Hidden:
		// Never drawn; the list still scrolls by wheel, drag and code. Its room is RefreshScrollFurniture's question.
		return false;
	default:
		return IsListContentOverflowing();
	}
}

bool UDreamListViewBase::IsListContentOverflowing() const
{
	if (ColumnNode == nullptr || ViewportNode == nullptr)
	{
		return true;
	}
	// Along the SCROLL axis, whichever that is -- UDreamScrollBox's question. A horizontal list's
	// column is exactly as tall as its viewport by construction, so asking about heights there said
	// "fits" of every band however far it ran, and the auto-hiding bar never came out.
	const float ColumnMain = IsHorizontalList() ? ColumnNode->GetWidth() : ColumnNode->GetHeight();
	return ColumnMain > GetViewportMainExtent() + KINDA_SMALL_NUMBER;
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
				HandleRowSelectionStateChanged(InPoolIndex, InState);
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
	// Whether the row comes to stand for a different ITEM: another index, or another object at the same
	// index -- a source edit shifts objects under rows whose indices stay put.
	UObject* const Item = GetItemObject(InItemIndex);
	const bool bItemChanges = !IsRowBoundTo(InPoolIndex, InItemIndex, Item);
	// The row is about to stop standing for whatever it was showing -- UMG's OnEntryReleased, and the
	// place a consumer undoes what OnRowGenerated did to this widget. Before the new index is written,
	// so a handler asking GetRowItemIndex is still told the one being released. On EVERY bind, the same
	// item's included: a bind re-pushes the whole look below, and announces OnRowGenerated after it, and
	// a generation with no release before it handed a consumer pairing the two a second one to undo.
	// The release below runs handlers, and one that edits the source or scrolls rebuilds the rows from inside it: that
	// pass bound this slot as it should be, or took it away, and this bind stops rather than writing over it.
	const uint32 Serial = WindowRefreshSerial;
	ReleaseRow(InPoolIndex, bItemChanges);
	if (WindowRefreshSerial != Serial || !RowNodes.IsValidIndex(InPoolIndex) || RowNodes[InPoolIndex].Get() != Row)
	{
		return;
	}
	RowSourceIndices[InPoolIndex] = InItemIndex;
	FRowBinding& Binding = RowBindings[InPoolIndex];
	Binding.Item = Item;
	Binding.SourceSerial = TextSourceSerial;
	Binding.DisplayIndex = InDisplayIndex;
	if (InPoolIndex == FocusAnchorPoolIndex && GetNavigationItemIndex(InPoolIndex) == InItemIndex)
	{
		// Back on the item focus was on, so the row answers for itself again.
		ClearFocusAnchor();
	}
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
	if (bItemChanges && HoveredPoolIndices.Contains(InPoolIndex))
	{
		// The pointer is still on this widget and it shows another item now, so that item is the one
		// hovered -- ReleaseRow has already said the old one no longer is.
		OnItemIsHoveredChanged.Broadcast(InItemIndex, Item, true);
	}
	OnRowGenerated.Broadcast(InItemIndex, Row, Item);
}

void UDreamListViewBase::ReleaseRow(int32 InPoolIndex, bool bInItemChanges)
{
	const int32 ReleasedItemIndex = RowSourceIndices.IsValidIndex(InPoolIndex) ? RowSourceIndices[InPoolIndex] : INDEX_NONE;
	if (ReleasedItemIndex == INDEX_NONE)
	{
		return;
	}
	UDreamWidget* Row = RowNodes[InPoolIndex].Get();
	// The object the row was BOUND to, not whatever the source holds at that index now: after a removal
	// the index names the next item along, and the removed one was never released at all.
	UObject* ReleasedItem = RowBindings[InPoolIndex].Item.Get();
	if (bInItemChanges)
	{
		// The hover belonged to the item, and the item is leaving this widget.
		if (HoveredPoolIndices.Contains(InPoolIndex))
		{
			OnItemIsHoveredChanged.Broadcast(ReleasedItemIndex, ReleasedItem, false);
		}
		// So does focus, and focus cannot follow it: a row whose item left the window has nowhere for it
		// to go. What it can keep is the item it was on, for the next navigation press to step from --
		// SListView's selector item -- rather than from whatever this row is about to show. Kept once:
		// a row already answering for an earlier item is not showing the item focus is on.
		const UUIButton* RowButton = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
		if (RowButton != nullptr && RowButton->IsFocused() && InPoolIndex != FocusAnchorPoolIndex)
		{
			FocusAnchorPoolIndex = InPoolIndex;
			FocusAnchorItemIndex = ReleasedItemIndex;
			FocusAnchorItem = ReleasedItem;
		}
	}
	OnRowReleased.Broadcast(ReleasedItemIndex, Row, ReleasedItem);
}

bool UDreamListViewBase::IsRowBoundTo(int32 InPoolIndex, int32 InItemIndex, UObject* InItem) const
{
	if (!RowSourceIndices.IsValidIndex(InPoolIndex) || !RowBindings.IsValidIndex(InPoolIndex)
		|| RowSourceIndices[InPoolIndex] != InItemIndex)
	{
		return false;
	}
	const FRowBinding& Binding = RowBindings[InPoolIndex];
	// An object is its own identity. Text is not, so a text row is the same item only while the texts
	// it was bound from are still the source -- index 2 of new texts is a different line.
	return InItem != nullptr
		? Binding.Item.Get() == InItem
		: (Binding.Item.Get() == nullptr && Binding.SourceSerial == TextSourceSerial);
}

int32 UDreamListViewBase::GetNavigationItemIndex(int32 InPoolIndex) const
{
	const int32 RowItemIndex = GetRowItemIndex(InPoolIndex);
	if (InPoolIndex != FocusAnchorPoolIndex)
	{
		return RowItemIndex;
	}
	// Found again by its object when it has one, so a source edit since cannot point this at a neighbour.
	int32 AnchorIndex = INDEX_NONE;
	if (UObject* AnchorItem = FocusAnchorItem.Get())
	{
		AnchorIndex = GetIndexForItem(AnchorItem);
	}
	else if (FocusAnchorItemIndex >= 0 && FocusAnchorItemIndex < GetItemCount())
	{
		AnchorIndex = FocusAnchorItemIndex;
	}
	// An item that is not shown any more -- gone from the source, or folded away under a tree node -- is
	// nowhere to step from, and the row's own item is the honest answer then.
	return (AnchorIndex != INDEX_NONE && VisibleItemIndices.Contains(AnchorIndex)) ? AnchorIndex : RowItemIndex;
}

void UDreamListViewBase::ClearFocusAnchor()
{
	FocusAnchorPoolIndex = INDEX_NONE;
	FocusAnchorItemIndex = INDEX_NONE;
	FocusAnchorItem.Reset();
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
	// Parking is a release too: the row stops standing for its item, and a consumer that hung
	// something on it at generation time has to hear about that on every road out. Released with the
	// index still written, as a re-bind releases, so a handler is told the item being let go.
	// See BindRow: a handler of the release can rebuild the rows, and this park then has nothing left to do.
	const uint32 Serial = WindowRefreshSerial;
	ReleaseRow(InPoolIndex, /*bInItemChanges*/true);
	if (WindowRefreshSerial != Serial || !RowNodes.IsValidIndex(InPoolIndex))
	{
		return;
	}
	RowSourceIndices[InPoolIndex] = INDEX_NONE;
	RowBindings[InPoolIndex] = FRowBinding();
	// The item's hover ended above, and an asleep row has no item for a later exit to report.
	HoveredPoolIndices.Remove(InPoolIndex);
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
		if (ItemIndex != INDEX_NONE && RowBindings.IsValidIndex(PoolIndex))
		{
			// The display index the row was placed at: a row keeps its item while the window moves round
			// it, so its pool slot no longer says where in the window it sits.
			ApplyRowColor(RowNodes[PoolIndex], RowBindings[PoolIndex].DisplayIndex, ItemIndex, Active);
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
	{
		// The click's own event, which the row's button holds while it announces the click: whose
		// pointer it was, and whether a finger's.
		const UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
		const UUIButton* RowButton = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
		SelectFromMultiClick(ItemIndex, RowButton != nullptr ? RowButton->GetClickEventData() : nullptr);
		break;
	}
	default:
		break;
	}

	// After the selection, so a handler asking GetSelectedIndex sees the answer the user just gave.
	OnItemClicked.Broadcast(ItemIndex, GetItemObject(ItemIndex));
}

void UDreamListViewBase::SelectFromMultiClick(int32 InItemIndex, const UDreamPointerEventData* InClick)
{
	// A refused row is no place for a click to land, so it is not where a later range starts either.
	// Taking a row away is never refused (SetItemSelection), and a Ctrl click on one still does that.
	const bool bMayLandHere = IsItemSelectableOrNavigable(InItemIndex);
	const bool bFinger = InClick != nullptr && InClick->InputType == EDreamUIPointerInputType::Pointer
		&& DreamUIPointerIds::IsTouch(InClick->PointerID);
	if (bFinger)
	{
		// STableRow::OnTouchEnded: in Multi mode a tap adds the row it lifts on, and a tap on a chosen row
		// leaves it chosen -- a finger has no Ctrl to take one away with, nor a Shift to range with.
		SetItemSelection(InItemIndex, true, /*bInClearOthers*/ false);
		if (bMayLandHere)
		{
			RangeAnchorIndex = InItemIndex;
		}
		return;
	}
	bool bShiftDown = false;
	bool bCtrlDown = false;
	ReadSelectionModifiers(InClick, bShiftDown, bCtrlDown);
	if (bShiftDown)
	{
		// STableRow::OnMouseButtonDown asks Shift first, so Ctrl with it ranges too: the rows from the
		// anchor to this one join whatever is chosen, and the anchor stays.
		if (bMayLandHere)
		{
			SelectRangeToItem(InItemIndex, /*bInClearFirst*/ false);
		}
		return;
	}
	if (bCtrlDown)
	{
		// The row in or out, the rest kept; the anchor moves here either way, as Private_SetItemSelection's
		// user-directed change moves RangeSelectionStart whether it selected or deselected.
		const bool bWasSelected = IsItemSelected(InItemIndex);
		SetItemSelection(InItemIndex, !bWasSelected, /*bInClearOthers*/ false);
		if (bMayLandHere || bWasSelected)
		{
			RangeAnchorIndex = InItemIndex;
		}
		return;
	}
	// A plain click: this row and nothing else. STableRow selects an unchosen row on the press and narrows
	// a chosen one on the release; a click that completes ends in the same place either way.
	SetItemSelection(InItemIndex, true, /*bInClearOthers*/ true);
	if (bMayLandHere)
	{
		RangeAnchorIndex = InItemIndex;
	}
}

void UDreamListViewBase::SelectRangeToItem(int32 InItemIndex, bool bInClearFirst)
{
	if (SelectionMode == EUIListSelectionMode::None || InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return;
	}
	// In DISPLAY order, between the two ends' rows: a tree's folded children are not between two rows
	// that show, and a tile view's display order is its source order. An anchor that does not show starts
	// the range at the first row, as SListView's does with an anchor it cannot find.
	const int32 EndDisplay = VisibleItemIndices.IndexOfByKey(InItemIndex);
	if (EndDisplay == INDEX_NONE)
	{
		return;
	}
	// Pinned where this range starts, so the next one starts there too: a range moves SelectedIndex to the
	// row it ends on, and an anchor still read from SelectedIndex would follow it.
	RangeAnchorIndex = GetRangeAnchorItemIndex();
	const int32 AnchorDisplay = FMath::Max(0, VisibleItemIndices.IndexOfByKey(RangeAnchorIndex));
	PendingSelectedIndex = INDEX_NONE;
	if (bClearScrollVelocityOnSelection)
	{
		EndInertialScrolling();
	}
	const int32 PreviousAnchor = SelectedIndex;
	const TArray<int32> PreviousSelection = SelectedIndices;
	if (bInClearFirst)
	{
		SelectedIndices.Reset();
	}
	// From the anchor towards the end, so the order chosen in is the order walked -- Slate's "if selecting
	// upwards then make sure the top element is last-selected".
	const int32 Direction = EndDisplay >= AnchorDisplay ? 1 : -1;
	for (int32 Display = AnchorDisplay; ; Display += Direction)
	{
		const int32 Item = VisibleItemIndices[Display];
		if (IsItemSelectableOrNavigable(Item))
		{
			SelectedIndices.AddUnique(Item);
		}
		if (Display == EndDisplay)
		{
			break;
		}
	}
	SelectedIndex = SelectedIndices.Contains(InItemIndex)
		? InItemIndex
		: (SelectedIndices.Num() > 0 ? SelectedIndices.Last() : INDEX_NONE);
	if (SelectedIndices != PreviousSelection || SelectedIndex != PreviousAnchor)
	{
		RefreshRowColors();
		OnSelectionChanged.Broadcast(SelectedIndex);
		OnValueChangedBP.Broadcast(SelectedIndex);
	}
}

void UDreamListViewBase::SelectAllItems()
{
	if (SelectionMode != EUIListSelectionMode::Multi)
	{
		return;
	}
	PendingSelectedIndex = INDEX_NONE;
	const int32 PreviousAnchor = SelectedIndex;
	const TArray<int32> PreviousSelection = SelectedIndices;
	// What was chosen keeps its place at the front, and the rest join in source order: the order chosen in.
	for (int32 Item = 0; Item < GetItemCount(); ++Item)
	{
		if (IsItemSelectableOrNavigable(Item))
		{
			SelectedIndices.AddUnique(Item);
		}
	}
	if (!SelectedIndices.Contains(SelectedIndex))
	{
		SelectedIndex = SelectedIndices.Num() > 0 ? SelectedIndices[0] : INDEX_NONE;
	}
	if (SelectedIndices != PreviousSelection || SelectedIndex != PreviousAnchor)
	{
		RefreshRowColors();
		OnSelectionChanged.Broadcast(SelectedIndex);
		OnValueChangedBP.Broadcast(SelectedIndex);
	}
}

int32 UDreamListViewBase::GetRangeAnchorItemIndex() const
{
	const int32 Count = GetItemCount();
	if (RangeAnchorIndex >= 0 && RangeAnchorIndex < Count)
	{
		return RangeAnchorIndex;
	}
	if (SelectedIndex >= 0 && SelectedIndex < Count)
	{
		return SelectedIndex;
	}
	return Count > 0 ? 0 : INDEX_NONE;
}

void UDreamListViewBase::ReadSelectionModifiers(const UDreamPointerEventData* InClick, bool& bOutShiftDown, bool& bOutCtrlDown) const
{
	bOutShiftDown = false;
	bOutCtrlDown = false;
	if (InClick == nullptr)
	{
		return;
	}
	// The clicking player's controller, which is where the router reads a key's chord when the input source
	// does not state one: on a split screen the other player's Shift is not this click's. A pointer event
	// carries no chord of its own.
	const APlayerController* Controller = UDreamEventSystem::GetPlayerControllerForUser(this, InClick->UserIndex);
	if (Controller == nullptr)
	{
		return;
	}
	bOutShiftDown = Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift);
	// Cmd beside Ctrl, as the router's chords and a Mac's Slate read it.
	bOutCtrlDown = Controller->IsInputKeyDown(EKeys::LeftControl) || Controller->IsInputKeyDown(EKeys::RightControl)
		|| Controller->IsInputKeyDown(EKeys::LeftCommand) || Controller->IsInputKeyDown(EKeys::RightCommand);
}

int32 UDreamListViewBase::FindRowPoolIndexAround(const UDreamWidget* InWidget) const
{
	// The same depth guard as the key dispatch's walk, for a malformed parent chain.
	int32 DepthGuard = 0;
	for (const UDreamWidget* Walker = InWidget; IsValid(Walker) && Walker != this && DepthGuard < 256; Walker = Walker->GetParent(), ++DepthGuard)
	{
		for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
		{
			if (RowNodes[PoolIndex].Get() == Walker)
			{
				return PoolIndex;
			}
		}
		if (Walker->IsA<UDreamListViewBase>())
		{
			return INDEX_NONE;
		}
	}
	return INDEX_NONE;
}

bool UDreamListViewBase::NativeOnKeyDown(UDreamKeyEventData* EventData)
{
	if (Super::NativeOnKeyDown(EventData))
	{
		return true;
	}
	// SListView::OnKeyDown_Internal ignores a press with Alt in it, and its keys belong to a Multi selection.
	if (EventData == nullptr || SelectionMode != EUIListSelectionMode::Multi || EventData->bAltDown
		|| !GetInteractableInHierarchy())
	{
		return false;
	}
	const int32 PoolIndex = FindRowPoolIndexAround(EventData->FocusedWidget);
	if (PoolIndex == INDEX_NONE)
	{
		return false;
	}
	if (EventData->Key == EKeys::A && EventData->bCtrlDown && !EventData->bShiftDown)
	{
		SelectAllItems();
		return true;
	}
	// A plain arrow is navigation's, which already selects the way NavigationSelect does without modifiers
	// (MoveNavigationToItem), and so is everything while navigation selects nothing.
	if (!bSelectItemOnNavigation || (!EventData->bShiftDown && !EventData->bCtrlDown))
	{
		return false;
	}
	const EDreamUINavigationDirection Direction = DreamUIKeyRouting::GetDirectionForKey(EventData->Key, /*bInShiftDown*/ false);
	if (Direction != EDreamUINavigationDirection::Up && Direction != EDreamUINavigationDirection::Down
		&& Direction != EDreamUINavigationDirection::Left && Direction != EDreamUINavigationDirection::Right)
	{
		return false;
	}
	const int32 TargetItem = FindNavigationTargetItem(PoolIndex, Direction);
	if (TargetItem == INDEX_NONE)
	{
		// Off the list: the press is the scan's, and the scan leaves, as it does without the modifier.
		return false;
	}
	// SListView::NavigationSelect in Multi mode with a modifier: Shift selects from the anchor to the row
	// it lands on, clearing the rest unless Ctrl is held too; Ctrl alone adds that row and moves the anchor.
	if (EventData->bShiftDown)
	{
		SelectRangeToItem(TargetItem, /*bInClearFirst*/ !EventData->bCtrlDown);
	}
	else
	{
		SetItemSelection(TargetItem, true, /*bInClearOthers*/ false);
		RangeAnchorIndex = TargetItem;
	}
	// The focus goes where the arrow points, as the plain step's does: revealed first, because a recycling
	// list re-binds its window while it scrolls and the row that shows the item afterwards is the one.
	ScrollItemIntoView(TargetItem, /*bInAnimate*/ false);
	TScriptInterface<IDreamNavigationInterface> Landing;
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(this);
	if (KeepNavigationOnItem(TargetItem, Landing) && Services != nullptr)
	{
		if (const UDreamUIBehaviour* LandingBehaviour = Cast<UDreamUIBehaviour>(Landing.GetObject()))
		{
			Services->FocusForNavigation(LandingBehaviour->GetWidget(), EventData->UserIndex);
		}
	}
	return true;
}

int32 UDreamListViewBase::FindNavigationTargetItem(int32 InPoolIndex, EDreamUINavigationDirection InDirection) const
{
	// Which item the press steps FROM, asked NOW: a recycled row stands for a different item every few
	// scrolls, and a row that was handed another one while focus stayed on it steps from the item focus
	// was on (GetNavigationItemIndex) -- not from the stranger it shows.
	const int32 ItemIndex = GetNavigationItemIndex(InPoolIndex);
	const int32 DisplayIndex = ItemIndex != INDEX_NONE ? VisibleItemIndices.IndexOfByKey(ItemIndex) : INDEX_NONE;
	if (DisplayIndex == INDEX_NONE)
	{
		return INDEX_NONE;
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
		return INDEX_NONE;
	}
	return VisibleItemIndices[TargetDisplay];
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
		// And for the same reason every row's binding is a new one: the rebuild releases each line and
		// generates the new one in its place (IsRowBoundTo), where an object source keeps a row on its item.
		++TextSourceSerial;
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
	// A drag in flight follows its item the same way, before the rebuild below can end it: the index it
	// started on now names whatever slid into that place, and the drag's end reports where the item went
	// -- or -1, when the edit took it out of the source.
	if (bIsDragging)
	{
		if (UObject* Dragged = DraggedItemObject.Get())
		{
			DraggedItemIndex = ItemObjects.IndexOfByKey(Dragged);
		}
	}
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
	// A selection stated outright replaces one still waiting for its items, and the range anchor a gesture
	// left: a range now starts at the anchor re-derived below.
	PendingSelectedIndex = INDEX_NONE;
	RangeAnchorIndex = INDEX_NONE;
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
	// reveal only -- a list you scroll through without losing the line you were on -- or the list
	// selects nothing at all, which SelectionMode None says and SetSelectedIndex now honours too.
	if (bSelectItemOnNavigation && SelectionMode != EUIListSelectionMode::None)
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

UDreamListRowButton::UDreamListRowButton()
{
	// What UUIButton declares, for its own class only: a row is a button, with nothing to do each frame or when it moves.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
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
	if (InDirection == EDreamUINavigationDirection::Next || InDirection == EDreamUINavigationDirection::Prev)
	{
		// Tab's, never the rows': the list is one Tab stop, so Tab and Shift+Tab leave it rather than step
		// from row to row -- whatever a subclass's arithmetic would answer for them.
		return false;
	}
	// The step the arrow takes by item, the veto stepped over (FindNavigationTargetItem).
	const int32 TargetItem = FindNavigationTargetItem(InPoolIndex, InDirection);
	if (TargetItem == INDEX_NONE)
	{
		// Nowhere to go inside the list: the press is the scan's, and the scan leaves.
		return false;
	}
	return MoveNavigationToItem(TargetItem, OutResult);
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
		// And the range anchor with it, as Private_SetSelection's user-directed change moves
		// RangeSelectionStart: the next Shift step ranges from the row this one landed on.
		if (IsItemSelected(InItemIndex))
		{
			RangeAnchorIndex = InItemIndex;
		}
	}
	else if (bClearScrollVelocityOnSelection)
	{
		// NavigateToIndex's reason: with selection off, landing somewhere still counts as the player
		// choosing where to be, and a fling carrying on would slide it away.
		EndInertialScrolling();
	}
	// Revealed BEFORE the row is named: a recycling list re-binds its window while it scrolls, and the
	// row that shows the item afterwards is the one focus has to land on. At once, whatever
	// bEnableScrollAnimation says -- a glide would name a row the window has not reached yet.
	ScrollItemIntoView(InItemIndex, /*bInAnimate*/false);
	return KeepNavigationOnItem(InItemIndex, OutResult);
}

bool UDreamListViewBase::KeepNavigationOnItem(int32 InItemIndex, TScriptInterface<IDreamNavigationInterface>& OutResult)
{
	UDreamWidget* Row = GetRowWidget(InItemIndex);
	UUIButton* RowButton = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
	if (!IsValid(RowButton))
	{
		return false;
	}
	OutResult.SetObject(RowButton);
	OutResult.SetInterface(Cast<IDreamNavigationInterface>(RowButton));
	// Focus goes to the row that shows this item, so the item a re-bound row was keeping for it
	// (GetNavigationItemIndex) has been stepped from and is done with -- even when that row is this one.
	ClearFocusAnchor();
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
		// later from a source the author has stopped waiting for. The one index kept for an EMPTY
		// source stays, because that one is kept whatever this says.
		PendingSelectedIndices.Reset();
		if (GetItemCount() > 0)
		{
			PendingSelectedIndex = INDEX_NONE;
		}
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
	// None selects nothing, on this road as on a click and a navigation press: a list nobody can select
	// from has no selection for code to put there either. ReconcileSelection keeps it empty, so there is
	// nothing to clear.
	if (SelectionMode == EUIListSelectionMode::None)
	{
		return;
	}
	const int32 Count = GetItemCount();
	// The veto is asked on this road too, not only on a click or a navigation step: a row the list
	// says cannot be chosen must not become the choice because the caller happened to be code. Only
	// for a real row -- clearing the selection is never refused, or a veto would be a trap.
	if (InIndex >= 0 && InIndex < Count && !IsItemSelectableOrNavigable(InIndex))
	{
		return;
	}
	// An index the source cannot answer YET is kept for when it can (ReconcileSelection): a screen that
	// restores "row 7 was selected" before its data loads, or a binding that pushes its value ahead of
	// the items, used to lose it here. Only while the source is empty -- one that HAS items and stops
	// short of the index has answered -- unless the author asked for preselections to be kept. Any
	// other call replaces it, a clear included.
	const bool bKeepForLater = InIndex >= Count && (Count == 0 || bAllowKeepPreselectedItems);
	PendingSelectedIndex = bKeepForLater ? InIndex : INDEX_NONE;
	// The one row this names is where the next range starts (GetRangeAnchorItemIndex falls back to it).
	RangeAnchorIndex = INDEX_NONE;
	// Clamped where it becomes a selection, not where it is stored by an author: an index nothing
	// answers to is no selection at all -- not even while it waits.
	SelectedIndex = (InIndex >= 0 && InIndex < Count) ? InIndex : INDEX_NONE;
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
	const int32 AnchorBefore = SelectedIndex;
	const TArray<int32> SelectionBefore = SelectedIndices;
	SelectionMode = InMode;
	// Narrowing the mode narrows the selection -- ReconcileSelection is where that rule lives, and
	// the repaint has to follow it or rows stay painted as selected after the mode says they are not.
	ReconcileSelection();
	RefreshRowColors();
	// And said, once, when it moved anything. SListView is silent here (SetSelectionMode only clears),
	// but a `<->` binding then went on holding a row nothing selects any more. Rows a narrowing kept
	// are no news, so a widening -- Single to Multi -- says nothing.
	if (SelectedIndex != AnchorBefore || SelectedIndices != SelectionBefore)
	{
		// This one announcement covers a kept index ReconcileSelection may have landed on the way.
		bAnnouncePendingSelection = false;
		AnnounceSelection();
	}
}

void UDreamListViewBase::AnnounceSelection()
{
	// Both names, as every other selection change says them: the `<->` desugar listens on the second.
	OnSelectionChanged.Broadcast(SelectedIndex);
	OnValueChangedBP.Broadcast(SelectedIndex);
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
	// A selection made now replaces one still waiting for its items: the later arrival must not undo it.
	PendingSelectedIndex = INDEX_NONE;
	// And a range from here on starts at SelectedIndex, until a gesture names its own anchor: the clicks
	// and navigation steps that do set it after calling this.
	RangeAnchorIndex = INDEX_NONE;
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
	// A selection still waiting for its items goes too, silently: nothing was selected yet to say
	// goodbye to, but clearing means it never lands. So does the range anchor.
	PendingSelectedIndex = INDEX_NONE;
	RangeAnchorIndex = INDEX_NONE;
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
	// Asked before the sweep below takes anything out of the set: an anchor that named a row actually selected is a
	// selection the source has since let go of, not a request still waiting for its items.
	const bool bAnchorWasSelected = SelectedIndices.Contains(SelectedIndex);
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
		// Nor anything later: a list nobody can select from does not land a selection when its items come.
		PendingSelectedIndex = INDEX_NONE;
		return;
	}

	// An anchor written before the source could answer it -- a .dui line, or a binding whose value
	// arrives ahead of its items -- is the same request SetSelectedIndexWithoutNotify keeps, and is kept
	// the same way rather than dropped by the mirror below. Not one the parked set above already holds,
	// though: that one comes back as part of the set it was in. Nor a row that WAS selected and whose
	// items have gone (ClearListItems, a source emptied to be refilled): that is the old selection, and
	// landing it again on whatever the next items are would select a row nobody chose.
	if (SelectedIndex >= Count && !bAnchorWasSelected && !SelectedIndices.Contains(SelectedIndex)
		&& !PendingSelectedIndices.Contains(SelectedIndex)
		&& (Count == 0 || bAllowKeepPreselectedItems))
	{
		PendingSelectedIndex = SelectedIndex;
	}
	// A kept index, once the source can answer it: the one row, as SetSelectedIndex makes it, whatever
	// bAllowKeepPreselectedItems says -- and announced by the rebuild that got here, once its rows exist
	// (RebuildRows). Dropped when the source arrived without that row and nobody asked to wait longer.
	if (PendingSelectedIndex != INDEX_NONE && Count > 0)
	{
		const int32 Landing = PendingSelectedIndex;
		if (Landing < Count)
		{
			PendingSelectedIndex = INDEX_NONE;
			const bool bAlreadyTheSelection = SelectedIndex == Landing
				&& SelectedIndices.Num() == 1 && SelectedIndices[0] == Landing;
			if (!bAlreadyTheSelection && IsItemSelectableOrNavigable(Landing))
			{
				SelectedIndices.Reset();
				SelectedIndices.Add(Landing);
				SelectedIndex = Landing;
				bAnnouncePendingSelection = true;
			}
		}
		else if (!bAllowKeepPreselectedItems)
		{
			PendingSelectedIndex = INDEX_NONE;
		}
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
	// While a reveal of this list's own is still gliding, the list is on its way to that glide's end,
	// and that is where the row has to be in view -- measured from wherever the glide has got to this
	// frame, a row about to slide out reads as already shown and the second reveal does nothing.
	const bool bHeadingForRevealTarget = bRevealGlideActive && ScrollBehaviour->IsScrolling();
	const float Offset = bHeadingForRevealTarget ? RevealGlideTarget : GetScrollOffset();

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
	if (bInAnimate && bEnableScrollAnimation)
	{
		// UMG's animated reveal, with the wheel's glide: the same ease, and the duration the speed knob
		// turns into at the push (PushScrollBehaviourSettings). IsScrolling counts the glide, so
		// OnListViewFinishedScrolling is said once, when it lands, rather than on every step of the way.
		// Where it will come to rest is clamped as the view clamps it, so the next reveal measures from
		// a place the list can actually reach.
		const FVector2D Extent = ScrollBehaviour->GetScrollableExtent();
		RevealGlideTarget = FMath::Clamp(Target, 0.0f, FMath::Max(0.0f, static_cast<float>(IsHorizontalList() ? Extent.X : Extent.Y)));
		bRevealGlideActive = true;
		ScrollBehaviour->GlideToScrollOffset(IsHorizontalList() ? FVector2D(Target, 0.0) : FVector2D(0.0, Target),
			1.0f / FMath::Max(0.01f, ScrollingAnimationInterpolationSpeed));
		return true;
	}
	// A jump ends a reveal's glide where it stands: the list now rests where this puts it.
	bRevealGlideActive = false;
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
