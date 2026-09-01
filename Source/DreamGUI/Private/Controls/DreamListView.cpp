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
									// aligned and padded in; a button so a row has a hover and a click.
									.With<UDreamLayoutContainerOverlay>()
									.With<UUIButton>()
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
		// keep at its own defaults, and two of them are wrong by default for a list.
		ScrollBehaviour->SetHorizontal(false);
		ScrollBehaviour->SetVertical(true);
		ScrollBehaviour->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
		// One notch is one row, which is the only sensitivity a list can state without guessing.
		ScrollBehaviour->SetScrollSensitivity(GetRowPitch());
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

float UDreamListViewBase::GetRowTopOffset(int32 InDisplayIndex) const
{
	return ResolveListStyle().Padding.Top + InDisplayIndex * GetRowPitch();
}

int32 UDreamListViewBase::ResolveWindowSize() const
{
	const float ViewportHeight = ViewportNode != nullptr ? ViewportNode->GetHeight() : 0.0f;
	// A viewport with no resolvable height is one nothing has arranged and nobody authored -- the
	// first frame of a control a consumer's layout has not reached yet. A fixed window rather than a
	// guess of zero, because a zero-row window is a list that stays blank until something else
	// happens to resize it; the dimensions handler re-asks the moment there IS an answer.
	const int32 FromViewport = ViewportHeight > KINDA_SMALL_NUMBER
		? FMath::CeilToInt(ViewportHeight / GetRowPitch()) + 1
		: 16;
	return FMath::Max(1, FromViewport + FMath::Max(0, VirtualizationOverscan) * 2);
}

void UDreamListViewBase::RebuildRows()
{
	if (!IsValid(ColumnNode) || !IsValid(RowTemplateNode))
	{
		return;
	}

	const FDreamListStyle& Active = ResolveListStyle();
	CollectVisibleItemIndices(VisibleItemIndices);
	const int32 RowCount = VisibleItemIndices.Num();

	// The column's height is the scroll range, stated rather than measured: rows, gaps and the
	// viewport's own inset. The column is point-anchored vertically, so SetHeight writes SizeDelta.Y
	// straight through and leaves the anchored position -- where the scroll offset lives -- alone.
	const float ContentHeight = Active.Padding.Top + Active.Padding.Bottom
		+ RowCount * Active.RowHeight
		+ FMath::Max(0, RowCount - 1) * Active.RowSpacing;
	ColumnNode->SetHeight(ContentHeight);

	// The threshold decision, taken here and nowhere else. Everything downstream reads bVirtualizing
	// rather than re-deciding, so the pool size and the bind loop cannot disagree about which list
	// this is.
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

	// The template goes AWAKE for the duration. bWidgetActive is an ordinary property and the copy
	// inherits it, so duplicating a sleeping template yields a list of sleeping rows: present in the
	// tree, arranged by nobody, drawn by nobody. UUIDropdown::CreateListItems does the same dance.
	if (RowNodes.Num() != WantedPoolSize)
	{
		RowTemplateNode->SetWidgetActive(true);
		ResizePool(WantedPoolSize);
		RowTemplateNode->SetWidgetActive(false);
	}

	RefreshScrollFurniture(Active);
	RefreshVisibleWindow();
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
		const float Offset = ScrollBehaviour != nullptr
			? static_cast<float>(ScrollBehaviour->GetScrollOffset().Y)
			: 0.0f;
		// The first row whose bottom edge is still below the window's top, less the overscan. Clamped
		// so the LAST window is a full one rather than a short one with blank rows under it.
		const int32 FirstVisible = FMath::FloorToInt(FMath::Max(0.0f, Offset - Active.Padding.Top) / GetRowPitch());
		FirstDisplayIndex = FMath::Clamp(FirstVisible - FMath::Max(0, VirtualizationOverscan),
			0, FMath::Max(0, RowCount - RowNodes.Num()));
	}
	WindowStart = FirstDisplayIndex;

	for (int32 PoolIndex = 0; PoolIndex < RowNodes.Num(); ++PoolIndex)
	{
		const int32 DisplayIndex = FirstDisplayIndex + PoolIndex;
		if (!VisibleItemIndices.IsValidIndex(DisplayIndex))
		{
			ParkRow(PoolIndex);
			continue;
		}
		BindRow(PoolIndex, DisplayIndex, VisibleItemIndices[DisplayIndex], Active);
	}
}

void UDreamListViewBase::HandleScrollViewMoved(FVector2D InProgress)
{
	if (bVirtualizing)
	{
		RefreshVisibleWindow();
	}
}

void UDreamListViewBase::HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged)
{
	if (!bWidthChanged && !bHeightChanged)
	{
		return;
	}
	const FDreamListStyle& Active = ResolveListStyle();
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

	// No circle here, unlike UDreamScrollBox's: the gutter takes from the viewport's WIDTH and the
	// overflow question is about its HEIGHT, and a row's height does not depend on how wide it is.
	// So one pass answers it, where the box has to measure, decide and re-state.
	if (ViewportNode != nullptr)
	{
		// Stretched, deliberately: the viewport has to track the list's live size on every arrange,
		// and a stretched axis is exactly how to say "the parent's span, less the gutter" -- a
		// SizeDelta on a stretched axis is the DIFFERENCE from that span, so the gutter goes in
		// negative and the position shifts by half of it to leave the left edge where it was.
		ViewportNode->SetPivot(FVector2D(0.5, 0.5));
		ViewportNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
		ViewportNode->SetAnchoredPositionAndSizeDelta(FVector2D(-Gutter * 0.5, 0.0), FVector2D(-Gutter, 0.0));
	}

	if (ScrollBarNode != nullptr)
	{
		// Down the right edge: stretched vertically so it always spans the list, a POINT anchor with
		// the pivot on that edge across it so the thickness is an absolute number pinned flush.
		ScrollBarNode->SetPivot(FVector2D(1.0, 0.5));
		ScrollBarNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), false, false);
		ScrollBarNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(InStyle.Bar.Thickness, 0.0));
		// After its rect, never before: the bar reads its own track's live size to lay the handle out.
		ScrollBarNode->ApplyStyle();
		// Last of all, so a bar that is about to appear is already the right shape when it does.
		ScrollBarNode->SetWidgetActive(bBarVisible);
	}

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
		RowButton->GetOnClickEvent().AddWeakLambda(this, [this, InPoolIndex]()
		{
			HandleRowClicked(InPoolIndex);
		});
	}

	DecorateNewRow(*Row, InPoolIndex);
	return Row;
}

void UDreamListViewBase::BindRow(int32 InPoolIndex, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle)
{
	UDreamWidget* Row = RowNodes.IsValidIndex(InPoolIndex) ? RowNodes[InPoolIndex].Get() : nullptr;
	if (!IsValid(Row))
	{
		return;
	}
	RowSourceIndices[InPoolIndex] = InItemIndex;
	Row->SetWidgetActive(true);

	// The row's LOOK, pushed here rather than inherited from the template it was copied from. That
	// is what lets the pool survive a style edit -- see the note in RebuildRows about what creating
	// a widget costs the designer. Rows stay unrounded on purpose: they sit square against the
	// list's own rounded edge, the way the dropdown's items do.
	SkinFace(Row, InStyle.RowBrush);

	// The rect, stated from the index. Top-anchored and stretched across, so a row is as wide as the
	// column whatever the column turns out to be, and as tall as the style says whatever its content
	// measures to. The horizontal inset is the viewport's Padding, applied as a negative delta the
	// way every stretched axis in this library states an inset.
	const float Inset = InStyle.Padding.Left + InStyle.Padding.Right;
	Row->SetPivot(FVector2D(0.5, 1.0));
	Row->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(1.0, 1.0), false, false);
	Row->SetAnchoredPositionAndSizeDelta(
		FVector2D((InStyle.Padding.Left - InStyle.Padding.Right) * 0.5, -GetRowTopOffset(InDisplayIndex)),
		FVector2D(-Inset, InStyle.RowHeight));

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

	if (UUIButton* RowButton = Row->GetComponent<UUIButton>())
	{
		RowButton->SetHoveredColor(InStyle.RowHovered);
		// There is no RowPressed in the style, deliberately: pressing a row is the beginning of
		// selecting it, so it previews the selected colour rather than inventing a fourth one.
		RowButton->SetPressedColor(InStyle.RowSelected);
	}
	ApplyRowColor(Row, InDisplayIndex, InItemIndex, InStyle);

	// The subclass's turn (the tree's twisty), then the consumer's. Both run on every BIND, which is
	// every time a recycled row comes round to a new item -- UMG's OnEntryGenerated contract.
	DecorateRow(*Row, InPoolIndex, InItemIndex);
	OnRowGenerated.Broadcast(InItemIndex, Row, GetItemObject(InItemIndex));
}

void UDreamListViewBase::ParkRow(int32 InPoolIndex)
{
	if (!RowNodes.IsValidIndex(InPoolIndex))
	{
		return;
	}
	RowSourceIndices[InPoolIndex] = INDEX_NONE;
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
	const bool bSelected = (InItemIndex == SelectedIndex);
	const FColor Resting = bSelected
		? InStyle.RowSelected
		// Striped by DISPLAY position, not by pool position: a recycled row that landed in slot 0
		// showing item 37 has to wear item 37's stripe, or the whole list flickers as it scrolls.
		: ((bAlternatingRowColors && (InDisplayIndex % 2) == 1) ? InStyle.RowAlternate : InStyle.RowNormal);

	if (UUIButton* RowButton = InRow->GetComponent<UUIButton>())
	{
		RowButton->SetNormalColor(Resting);
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
	if (ItemIndex != INDEX_NONE)
	{
		SetSelectedIndex(ItemIndex);
	}
}

void UDreamListViewBase::SetItems(const TArray<FText>& InItems)
{
	Items = InItems;
	// A selection that no longer names anything is dropped SILENTLY: replacing the source is
	// authoring, not the user choosing, and nothing downstream should hear a selection event for it.
	if (SelectedIndex >= GetItemCount())
	{
		SelectedIndex = INDEX_NONE;
	}
	RebuildRows();
}

void UDreamListViewBase::SetItemObjects(const TArray<UObject*>& InItems)
{
	ItemObjects.Reset(InItems.Num());
	for (UObject* Item : InItems)
	{
		ItemObjects.Add(Item);
	}
	if (SelectedIndex >= GetItemCount())
	{
		SelectedIndex = INDEX_NONE;
	}
	RebuildRows();
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
	// Clamped where it becomes a selection, not where it is stored by an author: an index nothing
	// answers to is no selection at all.
	SelectedIndex = (InIndex >= 0 && InIndex < GetItemCount()) ? InIndex : INDEX_NONE;
	RefreshRowColors();
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
	const float ViewportHeight = ViewportNode != nullptr ? ViewportNode->GetHeight() : 0.0f;
	if (ViewportHeight <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float RowTop = GetRowTopOffset(DisplayIndex);
	const float RowBottom = RowTop + ResolveListStyle().RowHeight;
	const float Offset = static_cast<float>(ScrollBehaviour->GetScrollOffset().Y);

	float Target = Offset;
	if (RowTop < Offset)
	{
		Target = RowTop;
	}
	else if (RowBottom > Offset + ViewportHeight)
	{
		// A row taller than the window can never be framed, so show its top -- the same answer the
		// scroll view gives for an oversized child.
		Target = (RowBottom - RowTop) > ViewportHeight ? RowTop : RowBottom - ViewportHeight;
	}
	if (FMath::IsNearlyEqual(Target, Offset, 0.01f))
	{
		return false;
	}
	ScrollBehaviour->SetScrollOffset(FVector2D(0.0, Target));
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

const FDreamListStyle& UDreamListViewBase::ResolveListStyle() const
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

const FDreamListStyle& UDreamListView::ResolveListStyle() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::ListStyle);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "List", UDreamListView)
