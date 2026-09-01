// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTreeView.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"

namespace DreamTreeViewLocal
{
	/** Between the twisty and the text it belongs to. Hardcoded for the reason the row inset is. */
	static constexpr float TwistyGap = 4.0f;
}

void UDreamTreeView::DecorateRowTemplate(UDreamWidget& InTemplate)
{
	using namespace DreamUI;

	// Added to the TEMPLATE, so every row is copied with a twisty already in it. Building into an
	// existing parent is what the three-argument Realize is for; the base has already made the tree
	// this goes into, and no row has been copied yet -- that is the whole reason this hook exists
	// where it does.
	Realize(WidgetTree,
		Node<UDreamRectBlock>("Twisty").Out(TwistyTemplateNode)
			// Left of the label, vertically centred, and offset per row by that row's indent. The
			// row is an overlay, so this is a slot alignment rather than an anchor -- nothing here
			// asks a setter to resolve a parent's span.
			.Slot([](UDreamPanelSlot& InSlot)
			{
				InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
				InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
			})
			// An overlay so the glyph has a slot; a button so the twisty has a click of its own.
			// It is a CHILD of the row's button and consumes what it handles (AllowEventBubbleUp is
			// off by default), so clicking a twisty expands and clicking anywhere else selects --
			// no ordering rule, no flag, just which widget the pointer actually hit.
			.With<UDreamLayoutContainerOverlay>()
			.With<UUIButton>()
			.Children(
				DreamUI::Text("TwistyGlyph").Out(TwistyGlyphTemplateNode)
					.Visual([](UDreamText& InText)
					{
						InText.SetText(FText::AsCultureInvariant(TEXT("▼")));
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})),
		&InTemplate);
}

void UDreamTreeView::DecorateNewRow(UDreamWidget& InRow, int32 InPoolIndex)
{
	UDreamWidget* Twisty = InRow.FindChildByDisplayName(TEXT("Twisty"));
	if (!IsValid(Twisty))
	{
		return;
	}
	if (UUIButton* TwistyButton = Twisty->GetComponent<UUIButton>())
	{
		// Once, for the life of this widget. The POOL index is what gets captured and the ITEM index
		// is asked for at click time, because a recycled row shows a different item every time the
		// list scrolls past it -- and a subscription taken per bind would accumulate one per pass.
		//
		// The toggle this starts rebuilds the rows, which may destroy the very widget that is
		// mid-broadcast. That is the arrangement UUIDropdown's blocker already runs on -- its click
		// calls Hide(), which destroys the blocker -- and it holds for the same reason: a UObject's
		// memory outlives the call that let it go, and the multicast is iterating its own copy.
		TwistyButton->GetOnClickEvent().AddWeakLambda(this, [this, InPoolIndex]()
		{
			const int32 ItemIndex = GetRowItemIndex(InPoolIndex);
			if (ItemIndex != INDEX_NONE)
			{
				ToggleItemExpansion(ItemIndex);
			}
		});
	}
}

void UDreamTreeView::DecorateRow(UDreamWidget& InRow, int32 InPoolIndex, int32 InItemIndex)
{
	UDreamWidget* Twisty = InRow.FindChildByDisplayName(TEXT("Twisty"));
	if (!IsValid(Twisty))
	{
		return;
	}

	const FDreamTreeViewStyle& Active = ResolveTreeStyle();
	const bool bHasChildren = ItemHasChildren(InItemIndex);

	// A leaf has no twisty at all: asleep, so it neither draws nor takes the click that would
	// otherwise be the row's. The label's inset does NOT shrink to match -- a leaf's text still
	// lines up with its siblings', which is exactly why the inset counts the twisty's WIDTH rather
	// than the twisty's presence.
	Twisty->SetWidgetActive(bHasChildren);
	if (!bHasChildren)
	{
		return;
	}

	const bool bExpanded = IsItemExpanded(InItemIndex);
	// The check box's convention, restated: a state whose brush holds an image draws the image
	// (sized by the brush's own ImageSize, else the style's TwistySize); a state whose brush is
	// empty keeps the built-in glyph.
	const FDreamUIFaceBrush& StateBrush = bExpanded ? Active.ExpandedBrush : Active.CollapsedBrush;
	const bool bImageTwisty = (StateBrush.Image != nullptr);

	SizeFace(Twisty, BrushSizeOr(StateBrush, Active.TwistySize));
	SkinFace(Twisty, StateBrush);
	if (UDreamPanelSlot* TwistySlot = Twisty->GetPanelSlot())
	{
		// The indent, as a slot padding on an aligned child -- an absolute number against the row's
		// left edge. The row's own inset is in front of it so the twisty and a depth-0 label start
		// from the same place.
		TwistySlot->SetPadding(FMargin(GetRowPadding().Left + GetRowIndent(InItemIndex), 0.0f, 0.0f, 0.0f));
	}

	// In glyph mode the rect underneath has to be invisible, or "no image" ships as a coloured
	// square with a triangle drawn on it. Transparent, not absent: the face still exists, because
	// the button standing on it needs something to tint and a state brush may fill it later.
	const FColor TwistyFace = bImageTwisty
		? Active.TwistyColor
		: FColor(Active.TwistyColor.R, Active.TwistyColor.G, Active.TwistyColor.B, 0);
	if (UDreamVisual* TwistyVisual = Twisty->GetVisual())
	{
		TwistyVisual->SetColor(TwistyFace);
	}
	if (UUIButton* TwistyButton = Twisty->GetComponent<UUIButton>())
	{
		// Re-aimed like the row's own button, and for the same reason: a copied weak pointer still
		// names the TEMPLATE's visual. All three state colours are stated even though they are one
		// colour -- a selectable with none set renders WHITE. The twisty has no hover of its own
		// (UMG's has none either); the row underneath is what lights up.
		TwistyButton->SetTransitionTarget(Twisty->GetVisual());
		TwistyButton->SetNormalColor(TwistyFace);
		TwistyButton->SetHoveredColor(TwistyFace);
		TwistyButton->SetPressedColor(TwistyFace);
		// The click handler is NOT taken here -- see DecorateNewRow for why a per-bind subscription
		// on a recycled row is one subscription per pass through the list.
	}

	if (UDreamWidget* Glyph = Twisty->FindChildByDisplayName(TEXT("TwistyGlyph")))
	{
		Glyph->SetWidgetActive(!bImageTwisty);
		if (UDreamText* GlyphText = Cast<UDreamText>(Glyph->GetVisual()))
		{
			// U+25BC is the glyph UDreamDropdown already proves renders in the default SDF font;
			// U+25B6 is its neighbour in the same Geometric Shapes run and ships with it in every
			// font that carries either. A project whose font disagrees fills CollapsedBrush and gets
			// an image instead -- which is what those two brush fields are for.
			GlyphText->SetText(FText::AsCultureInvariant(bExpanded ? TEXT("▼") : TEXT("▶")));
			GlyphText->SetColor(Active.TwistyColor);
			GlyphText->SetFontSize(static_cast<float>(Active.TwistySize.Y));
		}
	}
}

void UDreamTreeView::CollectVisibleItemIndices(TArray<int32>& OutIndices) const
{
	const int32 Count = GetItemCount();
	OutIndices.Reset(Count);
	// The subtree of a row is exactly the run of rows that follow it at a greater depth, so a
	// collapsed row is a SKIP -- no recursion, no per-item ancestry question, and no way for a
	// deeper descendant of a collapsed ancestor to leak back into the list.
	int32 Index = 0;
	while (Index < Count)
	{
		OutIndices.Add(Index);
		if (IsItemExpanded(Index))
		{
			++Index;
			continue;
		}
		const int32 Depth = GetItemDepth(Index);
		int32 Next = Index + 1;
		while (Next < Count && GetItemDepth(Next) > Depth)
		{
			++Next;
		}
		Index = Next;
	}
}

int32 UDreamTreeView::GetItemDepth(int32 InItemIndex) const
{
	// A missing depth is a root. A tree given no depths at all is a flat list, which is the right
	// thing for it to be rather than an error nobody can see.
	return ItemDepths.IsValidIndex(InItemIndex) ? FMath::Max(0, ItemDepths[InItemIndex]) : 0;
}

float UDreamTreeView::GetRowIndent(int32 InItemIndex) const
{
	return GetItemDepth(InItemIndex) * ResolveTreeStyle().IndentPerLevel;
}

float UDreamTreeView::GetRowContentInset(int32 InItemIndex) const
{
	// The style's TwistySize, not the state brush's: a twisty whose width changed with its state
	// would make every label in the tree jitter as rows opened and closed.
	const FDreamTreeViewStyle& Active = ResolveTreeStyle();
	return GetRowIndent(InItemIndex)
		+ static_cast<float>(Active.TwistySize.X)
		+ DreamTreeViewLocal::TwistyGap;
}

bool UDreamTreeView::ItemHasChildren(int32 InItemIndex) const
{
	const int32 Next = InItemIndex + 1;
	return Next < GetItemCount() && GetItemDepth(Next) > GetItemDepth(InItemIndex);
}

bool UDreamTreeView::IsItemExpanded(int32 InItemIndex) const
{
	return !CollapsedItems.Contains(InItemIndex);
}

void UDreamTreeView::SetItemExpanded(int32 InItemIndex, bool bInExpanded)
{
	if (InItemIndex < 0 || InItemIndex >= GetItemCount())
	{
		return;
	}
	if (IsItemExpanded(InItemIndex) == bInExpanded)
	{
		return;
	}
	if (bInExpanded)
	{
		CollapsedItems.Remove(InItemIndex);
	}
	else
	{
		CollapsedItems.Add(InItemIndex);
	}
	// Rows first, event second: a handler that asks which rows exist should be told about the tree
	// it is being notified of, not the one before it.
	RebuildRows();
	OnItemExpansionChanged.Broadcast(InItemIndex, bInExpanded);
}

void UDreamTreeView::ToggleItemExpansion(int32 InItemIndex)
{
	SetItemExpanded(InItemIndex, !IsItemExpanded(InItemIndex));
}

void UDreamTreeView::ExpandAll()
{
	if (CollapsedItems.Num() == 0)
	{
		return;
	}
	CollapsedItems.Reset();
	RebuildRows();
}

void UDreamTreeView::CollapseAll()
{
	const int32 Count = GetItemCount();
	const int32 Previous = CollapsedItems.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Only the parents: a leaf in the collapsed set is a state nothing can undo from the screen,
		// because a leaf never draws a twisty to undo it with.
		if (ItemHasChildren(Index))
		{
			CollapsedItems.Add(Index);
		}
	}
	if (CollapsedItems.Num() != Previous)
	{
		RebuildRows();
	}
}

void UDreamTreeView::SetItemsWithDepths(const TArray<FText>& InItems, const TArray<int32>& InDepths)
{
	// Depths first: SetItems rebuilds, and a rebuild that ran against the old depths would draw the
	// new items at the old indents for exactly one frame -- and forever, in a headless caller.
	ItemDepths = InDepths;
	SetItems(InItems);
}

void UDreamTreeView::SetItemDepths(const TArray<int32>& InDepths)
{
	ItemDepths = InDepths;
	RebuildRows();
}

FDreamListStyle UDreamTreeView::ResolveListStyle() const
{
	// The list half of the tree's own style -- the reason FDreamTreeViewStyle embeds a whole
	// FDreamListStyle instead of restating its fields. BY VALUE, like everything downstream of
	// ResolveStyle now: a resolved style can be a MERGE of the sheet's and this instance's, which is
	// a value neither of them stores, so there is nothing left to return a reference into.
	return ResolveTreeStyle().List;
}

FDreamTreeViewStyle UDreamTreeView::ResolveTreeStyle() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::TreeViewStyle);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TreeView", UDreamTreeView)
