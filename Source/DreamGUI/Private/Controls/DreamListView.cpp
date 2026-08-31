// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamListView.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"

void UDreamListViewBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// UDreamScrollBox's anatomy, with a column of rows where its content stack would be: a face that
	// carries the look, a viewport clipped inside it holding the behaviour, the column that slides
	// within the viewport, and the bar along the viewport's edge.
	//
	// The behaviour is on the VIEWPORT and the bar is the viewport's SIBLING, both deliberately: a
	// scroll view accepts drags from anywhere inside its own widget, so a bar hung underneath it
	// would scroll the list every time somebody grabbed the handle.
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
					// that draws is raycast against. ApplyStyle tints it away -- a transparent rect
					// still hits, because hit testing is the rect range and not the pixels.
					.Stretch()
					.Self([](UDreamWidget& InViewport)
					{
						// Without the clip the rows past the visible count draw over whatever is under
						// the list: the behaviour moves content, it does not hide it.
						InViewport.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					.With<UUIScrollView>([](UUIScrollView& InScroll)
					{
						// Vertical only, explicitly, from the first moment: the behaviour ships with
						// BOTH axes on, and a zero-config scroll view drifts sideways the first time
						// a drag lands even though nothing in a list scrolls that way.
						InScroll.SetHorizontal(false);
						InScroll.SetVertical(true);
						// The column's height is rewritten on every rebuild. In RelativeLocation mode
						// the view scrolls by moving the widget WITHOUT touching its anchored
						// position, so the next write of the column's rect would restore a stale
						// offset and snap the list back to the top on every source change.
						InScroll.SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
					})
					.Children(
						Widget("Column").Out(ColumnNode)
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
							.With<UDreamLayoutContainerVerticalBox>()
							.Children(
								Node<UDreamRectBlock>("RowTemplate").Out(RowTemplateNode)
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
										DreamUI::Text("RowLabel").Out(RowLabelNode)
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
				Nested<UDreamScrollBar>("ScrollBar").Out(ScrollBarNode))
			.Then([this](UDreamWidget& InRoot)
			{
				ScrollBehaviour = ViewportNode != nullptr ? ViewportNode->GetComponent<UUIScrollView>() : nullptr;
				if (ScrollBehaviour != nullptr && ColumnNode != nullptr)
				{
					// What moves. The viewport is the window; the column is what slides behind it.
					ScrollBehaviour->SetContent(ColumnNode);
				}
				if (ScrollBarNode != nullptr)
				{
					// A nested user widget builds its own contents at Initialize, and nothing calls it
					// here: the walk that initializes nested widgets belongs to instancing a class
					// TEMPLATE, and a class that declares its hierarchy in code has no template to be
					// instanced from. Without this the bar is an empty node with no track and no
					// handle, and every push into it lands on nothing.
					ScrollBarNode->Initialize();
					// The bar owns the two-way link, so the list hands it the view and stops thinking
					// about scroll values -- one implementation, shared with the scroll box.
					ScrollBarNode->SetScrollView(ScrollBehaviour);
				}
			}));

	// Before the first copy is taken. Whatever a subclass adds to the template has to be IN it by
	// the time rows are built, or the template and the rows disagree about what a row is.
	if (RowTemplateNode != nullptr)
	{
		DecorateRowTemplate(*RowTemplateNode);
	}

	ApplyStyle();
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
	}

	// The viewport's inset and the gap between rows belong to the COLUMN, because the column is what
	// arranges rows. Putting them anywhere else would leave the height maths in RebuildRows solving
	// a different equation than the one the vertical box solves.
	if (UDreamLayoutContainerVerticalBox* Box = ColumnNode != nullptr
		? Cast<UDreamLayoutContainerVerticalBox>(ColumnNode->GetLayoutContainer())
		: nullptr)
	{
		Box->SetPadding(Active.Padding);
		Box->SetSpacing(Active.RowSpacing);
	}

	// The template only. Every row is a copy of it and every rebuild re-copies, so a style edit
	// reaches the rows by way of the thing they are made from. Rows are left unrounded on purpose:
	// they sit square against the list's own rounded edge, the way the dropdown's items do.
	SkinFace(RowTemplateNode, Active.RowBrush);
	if (RowTemplateNode != nullptr)
	{
		// A rect block states no size of its own, so this is what an Auto measure would read of it.
		// The rows do not depend on it -- their slots do the sizing, see BuildRow -- but a copy is
		// then already the right height before any layout pass runs, which is what makes the height
		// answerable in a headless test and in the designer's first frame.
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

void UDreamListViewBase::RebuildRows()
{
	if (!IsValid(ColumnNode) || !IsValid(RowTemplateNode))
	{
		return;
	}

	for (const TObjectPtr<UDreamWidget>& Row : RowNodes)
	{
		if (IsValid(Row))
		{
			Row->DestroyWidget();
		}
	}
	RowNodes.Reset();
	RowSourceIndices.Reset();

	const FDreamListStyle& Active = ResolveListStyle();
	TArray<int32> VisibleItems;
	CollectVisibleItemIndices(VisibleItems);

	// The template goes AWAKE for the duration. bWidgetActive is an ordinary property and the copy
	// inherits it, so duplicating a sleeping template yields a list of sleeping rows: present in the
	// tree, arranged by nobody, drawn by nobody. UUIDropdown::CreateListItems does the same dance.
	RowTemplateNode->SetWidgetActive(true);
	for (int32 RowIndex = 0; RowIndex < VisibleItems.Num(); ++RowIndex)
	{
		BuildRow(RowIndex, VisibleItems[RowIndex], Active);
	}
	RowTemplateNode->SetWidgetActive(false);

	// The column's height is the equation the vertical box solves in reverse. Every row sits on a
	// Fill slot of equal weight, so the box hands each of them
	//     (ColumnHeight - padding - gaps) / RowCount
	// and writing exactly rows*RowHeight + gaps + padding here makes that RowHeight, whatever the
	// rows' content measures to. The column is point-anchored vertically, so this writes SizeDelta.Y
	// straight through with no parent span for the setter to resolve.
	const int32 RowCount = RowNodes.Num();
	const float ContentHeight = Active.Padding.Top + Active.Padding.Bottom
		+ RowCount * Active.RowHeight
		+ FMath::Max(0, RowCount - 1) * Active.RowSpacing;
	// SetHeight, not SetAnchoredPositionAndSizeDelta: the vertical axis is a point anchor, so this
	// writes SizeDelta.Y straight through -- and it leaves the anchored position alone, which is
	// where the scroll offset lives. A list that grew would otherwise jump back to the top.
	ColumnNode->SetHeight(ContentHeight);

	RefreshScrollFurniture(Active);
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

void UDreamListViewBase::BuildRow(int32 InRowIndex, int32 InItemIndex, const FDreamListStyle& InStyle)
{
	// Outered to the column's outer -- the widget tree -- which is where every widget in this
	// hierarchy lives. Duplication brings the whole subtree, its visual, its slot and its behaviours,
	// then registers the copy under its new parent; no world is required for any of it.
	UDreamWidget* Row = DuplicateDreamWidgetHierarchy(ColumnNode->GetOuter(), RowTemplateNode, ColumnNode);
	if (!IsValid(Row))
	{
		return;
	}
	Row->SetDisplayName(FString::Printf(TEXT("Row_%d"), InRowIndex));
	RowNodes.Add(Row);
	RowSourceIndices.Add(InItemIndex);

	// Height through the SLOT, not through the authored number. A row is an overlay, and an
	// overlay's Auto measure is its content's -- for the built-in row that is the LABEL'S LINE
	// HEIGHT (19.7 for the default font, the figure UDreamDropdown measured), and no authored height
	// ever outruns a content measure. Fill does, against a column of known height. The authored
	// height is written as well, so the widget answers the same number the layout will give it.
	if (UDreamPanelSlot* RowSlot = Row->GetPanelSlot())
	{
		RowSlot->SetSizeRule(EDreamPanelSizeRule::Fill);
		RowSlot->SetFillWeight(1.0f);
		RowSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
		RowSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
		Row->SetHeight(InStyle.RowHeight);
		// The slot snapshots the authored size at registration, which happened during duplication --
		// before this write. Without the re-sync an Auto measure would read the pre-style default.
		RowSlot->SyncAuthoredDesiredSizeFromWidget();
	}
	else
	{
		Row->SetHeight(InStyle.RowHeight);
	}

	// Where a row's content starts: the row's own inset, plus whatever the subclass wants in front
	// of it (for a tree, the indent and room for the twisty).
	const FMargin BasePadding = GetRowPadding();
	const FMargin ContentPadding(
		BasePadding.Left + GetRowContentInset(InItemIndex),
		BasePadding.Top,
		BasePadding.Right,
		BasePadding.Bottom);

	// An authored row, when the consumer supplied a class for one. It fills the row and the built-in
	// label steps aside; the row's face, height, hover and selection stay the control's, so a
	// template only has to draw an item. Instancing a user widget needs a world (CreateDreamWidget
	// says so), and with none this simply does not happen -- the built-in label row is a correct
	// list, where half an authored row would not be.
	bool bAuthoredContent = false;
	if (RowTemplateClass != nullptr && GetWorld() != nullptr)
	{
		if (UDreamUserWidget* Content = CreateDreamWidget(GetWorld(), RowTemplateClass, Row))
		{
			Content->SetDisplayName(TEXT("RowContent"));
			if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
			{
				ContentSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
				ContentSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
				ContentSlot->SetPadding(ContentPadding);
			}
			bAuthoredContent = true;
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
		// Re-aimed, every time. TransitionTarget is a weak pointer copied by value, so a fresh row
		// starts out pointing at the TEMPLATE's visual -- left alone, every hover anywhere in the
		// list would repaint the one widget nobody can see. Registration only fills the target in
		// when it is EMPTY, and a copied one is not empty, it is wrong.
		RowButton->SetTransitionTarget(Row->GetVisual());
		RowButton->SetHoveredColor(InStyle.RowHovered);
		// There is no RowPressed in the style, deliberately: pressing a row is the beginning of
		// selecting it, so it previews the selected colour rather than inventing a fourth one.
		RowButton->SetPressedColor(InStyle.RowSelected);
		RowButton->GetOnClickEvent().AddWeakLambda(this, [this, InItemIndex]()
		{
			HandleRowClicked(InItemIndex);
		});
	}
	ApplyRowColor(Row, InRowIndex, InItemIndex, InStyle);

	// The subclass's turn (the tree's twisty), then the consumer's.
	DecorateRow(*Row, InRowIndex, InItemIndex);
	OnRowGenerated.Broadcast(InItemIndex, Row, GetItemObject(InItemIndex));
}

void UDreamListViewBase::ApplyRowColor(UDreamWidget* InRow, int32 InRowIndex, int32 InItemIndex, const FDreamListStyle& InStyle)
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
		: ((bAlternatingRowColors && (InRowIndex % 2) == 1) ? InStyle.RowAlternate : InStyle.RowNormal);

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
	for (int32 RowIndex = 0; RowIndex < RowNodes.Num(); ++RowIndex)
	{
		if (RowSourceIndices.IsValidIndex(RowIndex))
		{
			ApplyRowColor(RowNodes[RowIndex], RowIndex, RowSourceIndices[RowIndex], Active);
		}
	}
}

void UDreamListViewBase::HandleRowClicked(int32 InItemIndex)
{
	SetSelectedIndex(InItemIndex);
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
	const int32 RowIndex = RowSourceIndices.IndexOfByKey(InItemIndex);
	return RowNodes.IsValidIndex(RowIndex) ? RowNodes[RowIndex].Get() : nullptr;
}

bool UDreamListViewBase::ScrollItemIntoView(int32 InItemIndex, bool bInAnimate)
{
	UDreamWidget* Row = GetRowWidget(InItemIndex);
	if (Row == nullptr || ScrollBehaviour == nullptr)
	{
		return false;
	}
	// The least movement that reveals it, not ScrollTo's centring: stepping one row down should not
	// heave the whole list to put that row in the middle.
	return ScrollBehaviour->ScrollWidgetIntoView(Row, bInAnimate);
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
