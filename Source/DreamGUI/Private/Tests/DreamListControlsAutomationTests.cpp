// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Demo/DreamUIShowcase.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The two list controls, aimed the way the rest of the control suite is: at the wiring that fails
 * SILENTLY. A row copied from a sleeping template is a row nobody ever sees; a row sized by its own
 * text is a list whose row height quietly stops being the style's; a duplicated transition still
 * pointing at the template repaints a widget that is not on screen; and a value that reaches the
 * property but neither event is a selection nobody downstream hears about.
 *
 * Everything runs headless: no world, no registration of the control itself, no layout pass. Two
 * consequences shape the assertions below.
 *
 * Row BUILDING still works, which is the whole reason these controls duplicate a template rather
 * than hosting UUIRecyclableScrollView: DuplicateDreamWidgetHierarchy takes its outer from the
 * parent and every world lookup on the registration path is null-guarded, where the recycler makes
 * no cell at all before Start() and sizes its pool from an arranged viewport.
 *
 * Row COLOURS are read from the selectable's state rather than from the visual, the way the spin
 * box's are: a transition with a non-zero duration hands the colour to the tween manager, and the
 * tween manager returns null without a world. (The control also writes the resting colour straight
 * onto the visual for exactly that reason, and that write is asserted separately.)
 *
 * Every control here is built with StyleSource set to Inline, so what these assertions compare
 * against is the instance's own Style and never a sheet the running editor happens to have
 * configured. That is what lets a test author a row height no font could produce and then insist on
 * seeing exactly it.
 */
namespace DreamListControlsTestLocal
{
	/**
	 * A control with a known rect and its OWN style, not yet built.
	 *
	 * Inline rather than the sheet, deliberately: a project sheet configured in the running editor
	 * would otherwise decide what every assertion below is comparing against. The rect is authored
	 * before Initialize, which is what a .dui line or a designer does anyway, so the viewport and the
	 * bar have exact numbers to derive from instead of whatever the default happened to be.
	 */
	template<class T>
	T* Author(float InWidth = 320.0f, float InHeight = 200.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}

	template<class T>
	T* Make()
	{
		T* Control = Author<T>();
		Control->Initialize();
		return Control;
	}

	/** Culture-invariant, because these are identifiers standing in for a source, not prose. */
	TArray<FText> Labels(const TArray<FString>& InLabels)
	{
		TArray<FText> Result;
		Result.Reserve(InLabels.Num());
		for (const FString& Label : InLabels)
		{
			Result.Add(FText::AsCultureInvariant(Label));
		}
		return Result;
	}

	/** The row's label node, by the display name every row carries a copy of. */
	UDreamWidget* RowLabel(UDreamWidget* InRow)
	{
		return IsValid(InRow) ? InRow->FindChildByDisplayName(TEXT("RowLabel")) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewPartsTest,
	"DreamGUI.Controls.List.TheViewportTheColumnAndTheRowTemplateNestInThatOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewPartsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	TDreamTestControl<UDreamListView> List(Make<UDreamListView>());

	if (!TestNotNull(TEXT("the face exists"), List->FaceNode.Get()) ||
		!TestNotNull(TEXT("the viewport exists"), List->ViewportNode.Get()) ||
		!TestNotNull(TEXT("the scrolled column exists"), List->ColumnNode.Get()) ||
		!TestNotNull(TEXT("the row template exists"), List->RowTemplateNode.Get()) ||
		!TestNotNull(TEXT("the template's label exists"), List->RowLabelNode.Get()) ||
		!TestNotNull(TEXT("the bar exists"), List->ScrollBarNode.Get()) ||
		!TestNotNull(TEXT("the scroll behaviour is always there"), List->ScrollBehaviour.Get()))
	{
		return false;
	}

	// The nesting is the design: a face carrying the look, a viewport that clips, a column that
	// slides inside it, and one template row in the column.
	TestTrue(TEXT("the viewport lives inside the face"),
		(UObject*)List->ViewportNode->GetParent() == (UObject*)List->FaceNode.Get());
	TestTrue(TEXT("the column lives inside the viewport"),
		(UObject*)List->ColumnNode->GetParent() == (UObject*)List->ViewportNode.Get());
	TestTrue(TEXT("the template lives inside the column"),
		(UObject*)List->RowTemplateNode->GetParent() == (UObject*)List->ColumnNode.Get());
	TestTrue(TEXT("the label lives inside the template"),
		(UObject*)List->RowLabelNode->GetParent() == (UObject*)List->RowTemplateNode.Get());

	// THE placement claim, and it is not cosmetic: a scroll view accepts drags from anywhere inside
	// its own widget, so a bar hung under the viewport would scroll the list every time the handle
	// was grabbed. It is the viewport's SIBLING, and the behaviour is on the viewport.
	TestTrue(TEXT("the bar is the viewport's sibling, not its child"),
		(UObject*)List->ScrollBarNode->GetParent() == (UObject*)List->FaceNode.Get());
	TestTrue(TEXT("the behaviour rides the viewport"),
		(UObject*)List->ViewportNode->GetComponent<UUIScrollView>() == (UObject*)List->ScrollBehaviour.Get());
	TestTrue(TEXT("and the bar follows that same view"),
		(UObject*)List->ScrollBarNode->GetScrollView() == (UObject*)List->ScrollBehaviour.Get());
	// A nested code-built widget builds nothing until somebody calls Initialize on it; without that
	// the bar is an empty node and every push into it lands on nothing.
	TestNotNull(TEXT("the bar built its own parts"), List->ScrollBarNode->HandleNode.Get());

	// What the scroll view moves, and along which axis. The behaviour ships with BOTH axes on, and
	// a zero-config scroll view drifts horizontally the first time a drag lands.
	TestTrue(TEXT("the column is what scrolls"),
		(UObject*)List->ScrollBehaviour->GetContent() == (UObject*)List->ColumnNode.Get());
	TestFalse(TEXT("the horizontal axis is off, explicitly"), List->ScrollBehaviour->GetHorizontal());
	TestTrue(TEXT("the vertical axis is on, explicitly"), List->ScrollBehaviour->GetVertical());
	// Anchored position, because the column's rect is rewritten on every rebuild: in the other mode
	// the view scrolls without touching the anchored position, and the next rewrite would restore a
	// stale offset and snap the list back to the top.
	TestEqual(TEXT("the scroll offset lives where both writers can see it"),
		List->ScrollBehaviour->GetCoordinateMode(), EDreamScrollCoordinateMode::AnchoredPosition);

	// Two clips, two jobs: the face cuts content off at the rounded silhouette, the viewport hides
	// the rows past the visible count.
	TestEqual(TEXT("the face clips"),
		List->FaceNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);
	TestEqual(TEXT("and so does the viewport"),
		List->ViewportNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);

	// The template is the thing rows are copied from, not a row.
	TestFalse(TEXT("the template is not a row"), List->RowTemplateNode->GetWidgetActive());
	TestEqual(TEXT("an empty source builds no rows"), List->GetRowCount(), 0);

	// A stretched axis whose SizeDelta was written as a DELTA, not as a width: SetWidth on a
	// stretched axis resolves the parent's span at write time -- against the default 100 here -- and
	// bakes the difference in forever. Zero means "exactly the span", whenever the span is decided.
	TestEqual(TEXT("the column stretches across the viewport"),
		static_cast<float>(List->ColumnNode->GetAnchorMin().X), 0.0f);
	TestEqual(TEXT("-- to its far edge"),
		static_cast<float>(List->ColumnNode->GetAnchorMax().X), 1.0f);
	TestEqual(TEXT("with a zero width delta, not a zero width"),
		static_cast<float>(List->ColumnNode->GetSizeDelta().X), 0.0f);
	// And a POINT anchor vertically, because the column's height is authored per rebuild: a
	// stretched vertical axis would pin the content to the viewport and nothing would ever scroll.
	TestEqual(TEXT("the column is point-anchored to the viewport's top -- min"),
		static_cast<float>(List->ColumnNode->GetAnchorMin().Y), 1.0f);
	TestEqual(TEXT("-- and max"),
		static_cast<float>(List->ColumnNode->GetAnchorMax().Y), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewRowsTest,
	"DreamGUI.Controls.List.RowsComeFromTheSourceAndWearTheStylesHeightNotTheirTexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewRowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// A row height no font's line height could produce, so "the style's height" and "the text's
	// height" cannot be confused for one another. Authored before initialization, the way a .dui
	// line would leave it.
	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Style.RowHeight = 44.0f;
	List->Style.RowSpacing = 6.0f;
	List->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	List->Initialize();

	if (!TestEqual(TEXT("the authored source built one row each"), List->GetRowCount(), 3))
	{
		return false;
	}
	TestEqual(TEXT("and each row knows which item it stands for"), List->RowSourceIndices.Num(), 3);

	UDreamWidget* FirstRow = List->RowNodes[0].Get();
	if (!TestNotNull(TEXT("the first row exists"), FirstRow))
	{
		return false;
	}
	// Duplicated under the column, not left loose: a row with no parent is arranged by nobody.
	TestTrue(TEXT("rows live in the scrolled column"),
		(UObject*)FirstRow->GetParent() == (UObject*)List->ColumnNode.Get());
	// The template goes awake only for the duration of the copy loop -- bWidgetActive is an ordinary
	// property and the copy inherits it, so a sleeping template yields a list of sleeping rows.
	TestTrue(TEXT("a row is awake"), FirstRow->GetWidgetActive());
	TestFalse(TEXT("and the template went back to sleep"), List->RowTemplateNode->GetWidgetActive());

	// THE claim. A row is an overlay, and an overlay's Auto measure is its content's -- the label's
	// line height, which is nowhere near 44. Two things make the style win: the authored height on
	// the widget, and a Fill slot against a column of known height (the second is what survives a
	// real layout pass, where no authored number outruns a content measure).
	TestEqual(TEXT("a row is exactly the style's row height"), FirstRow->GetHeight(), 44.0f);
	if (UDreamPanelSlot* RowSlot = FirstRow->GetPanelSlot())
	{
		TestEqual(TEXT("and it gets there through its slot, not its own measure"),
			RowSlot->SizeRule, EDreamPanelSizeRule::Fill);
		TestEqual(TEXT("with the weight every other row has"), RowSlot->FillWeight, 1.0f);
	}
	// The column is the other half of that equation: rows*RowHeight + gaps, so equal fill weights
	// divide back into exactly RowHeight apiece.
	TestEqual(TEXT("the column is as tall as all the rows plus their gaps"),
		List->ColumnNode->GetHeight(), 3.0f * 44.0f + 2.0f * 6.0f);

	// The source reached the glyphs.
	if (UDreamWidget* LabelNode = RowLabel(FirstRow))
	{
		if (UDreamText* LabelText = Cast<UDreamText>(LabelNode->GetVisual()))
		{
			TestEqual(TEXT("the first row says the first item"),
				LabelText->GetText().ToString(), FString(TEXT("Alpha")));
		}
	}

	// Replacing the source rebuilds, and the count follows it down as well as up.
	List->SetItems(Labels({ TEXT("One"), TEXT("Two") }));
	TestEqual(TEXT("a shorter source leaves a shorter list"), List->GetRowCount(), 2);
	TestEqual(TEXT("and the column shrank with it"),
		List->ColumnNode->GetHeight(), 2.0f * 44.0f + 1.0f * 6.0f);
	if (UDreamWidget* LabelNode = RowLabel(List->RowNodes[1].Get()))
	{
		if (UDreamText* LabelText = Cast<UDreamText>(LabelNode->GetVisual()))
		{
			TestEqual(TEXT("the new source is what the rows say"),
				LabelText->GetText().ToString(), FString(TEXT("Two")));
		}
	}

	// Objects decide the count when the source has any; the parallel texts stay the labels.
	TArray<UObject*> Objects;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// A concrete class, deliberately: UObject itself is abstract, and NewObject<UObject> trips an
		// ensure inside StaticConstructObject_Internal. A showcase track is the nearest thing this
		// module has to "an arbitrary list item", which is exactly what the list takes.
		Objects.Add(NewObject<UDreamUIShowcaseTrack>(GetTransientPackage()));
	}
	List->SetItemObjects(Objects);
	TestEqual(TEXT("objects decide the count when there are any"), List->GetRowCount(), 3);
	TestTrue(TEXT("and a row can be found by the item it stands for"),
		(UObject*)List->GetRowWidget(2) == (UObject*)List->RowNodes[2].Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewSelectionTest,
	"DreamGUI.Controls.List.SelectionMovesTheHighlightAndReBroadcastsOnBothNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	List->SelectedIndex = 1;
	List->Initialize();

	if (!TestEqual(TEXT("the list built its rows"), List->GetRowCount(), 3))
	{
		return false;
	}

	auto RowNormalColor = [&List](int32 InRowIndex) -> FColor
	{
		UDreamWidget* Row = List->RowNodes.IsValidIndex(InRowIndex) ? List->RowNodes[InRowIndex].Get() : nullptr;
		UUIButton* Button = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
		return Button != nullptr ? Button->GetNormalColor() : FColor::White;
	};

	// Selection is not a pointer state -- it has to survive the pointer leaving -- so it rides the
	// selectable's NORMAL colour. The authored index reached the rows through the first build.
	TestEqual(TEXT("the authored selection is the one wearing the selected colour"),
		RowNormalColor(1), FDreamListStyle().RowSelected);
	TestEqual(TEXT("and its neighbours are not"),
		RowNormalColor(0), FDreamListStyle().RowNormal);

	// The white trap, restated for a row: a selectable whose transition colours are never set ships
	// white. All three are pushed, and the hover is the style's.
	if (UUIButton* Button = List->RowNodes[0]->GetComponent<UUIButton>())
	{
		TestEqual(TEXT("a row's hover colour is the style's"),
			Button->GetHoveredColor(), FDreamListStyle().RowHovered);
		// Re-aimed after the copy: TransitionTarget is a weak pointer copied by value, so an
		// un-retargeted row would tint the TEMPLATE -- a widget nobody can see -- on every hover.
		TestTrue(TEXT("a row's transition tints its own face"),
			(UObject*)Button->GetTransitionTarget() == (UObject*)List->RowNodes[0]->GetVisual());
		TestTrue(TEXT("and not the template's"),
			(UObject*)Button->GetTransitionTarget() != (UObject*)List->RowTemplateNode->GetVisual());
	}
	// The control writes the resting colour onto the visual as well, because a transition with a
	// duration needs a tween manager and a tween manager needs a world.
	if (UDreamVisual* RowVisual = List->RowNodes[1]->GetVisual())
	{
		TestEqual(TEXT("the selected row is wearing the colour, not just holding it"),
			RowVisual->GetColor(), FDreamListStyle().RowSelected);
	}

	// The re-broadcast, watched through two probes: a dynamic multicast can only reach a UFUNCTION,
	// and SetSelectedIndex is one of exactly the right shape. Both names have to fire, because the
	// `<->` desugar synthesizes its reverse route against OnValueChangedBP and nothing else.
	TDreamTestControl<UDreamListView> SpokenProbe(Author<UDreamListView>());
	SpokenProbe->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	SpokenProbe->Initialize();
	TDreamTestControl<UDreamListView> ValueProbe(Author<UDreamListView>());
	ValueProbe->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	ValueProbe->Initialize();
	List->OnSelectionChanged.AddDynamic(SpokenProbe.Get(), &UDreamListView::SetSelectedIndex);
	List->OnValueChangedBP.AddDynamic(ValueProbe.Get(), &UDreamListView::SetSelectedIndex);

	List->SetSelectedIndex(2);
	TestEqual(TEXT("the selection moved"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("the highlight moved with it"),
		RowNormalColor(2), FDreamListStyle().RowSelected);
	TestEqual(TEXT("and the row it left went back to normal"),
		RowNormalColor(1), FDreamListStyle().RowNormal);
	TestEqual(TEXT("the spoken event carried it"), SpokenProbe->GetSelectedIndex(), 2);
	TestEqual(TEXT("and so did the one two-way bindings bind to"), ValueProbe->GetSelectedIndex(), 2);

	// An index nothing answers to is no selection at all -- and it is still a change, so it is still
	// announced.
	List->SetSelectedIndex(9);
	TestEqual(TEXT("an out-of-range selection selects nothing"), List->GetSelectedIndex(), INDEX_NONE);
	TestEqual(TEXT("and that reached the probes too"), ValueProbe->GetSelectedIndex(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTreeViewIndentTest,
	"DreamGUI.Controls.TreeView.EveryRowIsIndentedByItsOwnDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTreeViewIndentTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// A pre-order chain: root, its child, its grandchild. The flat source IS the tree.
	TDreamTestControl<UDreamTreeView> Tree(Author<UDreamTreeView>());
	Tree->Items = Labels({ TEXT("Root"), TEXT("Child"), TEXT("Grandchild") });
	Tree->ItemDepths = { 0, 1, 2 };
	Tree->Initialize();

	if (!TestEqual(TEXT("a fully expanded tree shows every item"), Tree->GetRowCount(), 3))
	{
		return false;
	}
	// The tree gets the list's whole look through FDreamTreeViewStyle::List, which is why the two
	// controls share an implementation rather than a resemblance.
	TestEqual(TEXT("its rows are the list style's height"),
		Tree->RowNodes[0]->GetHeight(), FDreamTreeViewStyle().List.RowHeight);

	const float PerLevel = FDreamTreeViewStyle().IndentPerLevel;
	TestEqual(TEXT("a root is not indented"), Tree->GetRowIndent(0), 0.0f);
	TestEqual(TEXT("a child is indented one level"), Tree->GetRowIndent(1), PerLevel);
	TestEqual(TEXT("a grandchild, two"), Tree->GetRowIndent(2), 2.0f * PerLevel);

	// And the indent is not just arithmetic: it is on the row, as the label's left padding. Read the
	// difference between two rows rather than the absolute number, so the row's own inset and the
	// room kept for the twisty do not have to be restated here.
	auto LabelLeft = [](UDreamWidget* InRow) -> float
	{
		UDreamWidget* LabelNode = RowLabel(InRow);
		UDreamPanelSlot* Slot = IsValid(LabelNode) ? LabelNode->GetPanelSlot() : nullptr;
		return Slot != nullptr ? Slot->Padding.Left : 0.0f;
	};
	const float RootLeft = LabelLeft(Tree->RowNodes[0].Get());
	TestEqual(TEXT("the child's text starts one indent further in"),
		LabelLeft(Tree->RowNodes[1].Get()), RootLeft + PerLevel);
	TestEqual(TEXT("and the grandchild's, two"),
		LabelLeft(Tree->RowNodes[2].Get()), RootLeft + 2.0f * PerLevel);

	// The twisty: awake on the rows that have children, asleep on the leaf. A leaf's twisty is not
	// merely invisible -- it must not take the click that belongs to the row.
	UDreamWidget* RootTwisty = Tree->RowNodes[0]->FindChildByDisplayName(TEXT("Twisty"));
	UDreamWidget* ChildTwisty = Tree->RowNodes[1]->FindChildByDisplayName(TEXT("Twisty"));
	UDreamWidget* LeafTwisty = Tree->RowNodes[2]->FindChildByDisplayName(TEXT("Twisty"));
	if (!TestNotNull(TEXT("a parent row carries a twisty"), RootTwisty) ||
		!TestNotNull(TEXT("and so does every other copy of the template"), ChildTwisty) ||
		!TestNotNull(TEXT("-- the leaf's copy included"), LeafTwisty))
	{
		return false;
	}
	TestTrue(TEXT("the parent's twisty is awake"), RootTwisty->GetWidgetActive());
	TestFalse(TEXT("the leaf's is not"), LeafTwisty->GetWidgetActive());

	// The twisty rides the same indent, so it sits in front of its own row's text rather than in
	// front of the row above it.
	UDreamPanelSlot* RootTwistySlot = RootTwisty->GetPanelSlot();
	UDreamPanelSlot* ChildTwistySlot = ChildTwisty->GetPanelSlot();
	if (RootTwistySlot != nullptr && ChildTwistySlot != nullptr)
	{
		TestEqual(TEXT("a child's twisty is one indent further in than its parent's"),
			ChildTwistySlot->Padding.Left, RootTwistySlot->Padding.Left + PerLevel);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTreeViewCollapseTest,
	"DreamGUI.Controls.TreeView.CollapsingARowHidesExactlyTheRunBeneathIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTreeViewCollapseTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// Two roots. The first has two children; the second stands alone -- so a collapse that took one
	// row too many, or one too few, is visible in the count.
	TDreamTestControl<UDreamTreeView> Tree(Make<UDreamTreeView>());
	Tree->SetItemsWithDepths(
		Labels({ TEXT("Folder"), TEXT("A"), TEXT("B"), TEXT("Other") }),
		{ 0, 1, 1, 0 });

	TestEqual(TEXT("everything is expanded to begin with"), Tree->GetRowCount(), 4);
	TestTrue(TEXT("a row with a deeper row after it is a parent"), Tree->ItemHasChildren(0));
	TestFalse(TEXT("a row with a shallower row after it is not"), Tree->ItemHasChildren(2));
	TestFalse(TEXT("and neither is the last row"), Tree->ItemHasChildren(3));

	Tree->SetItemExpanded(0, false);
	TestFalse(TEXT("the folder is collapsed"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("its two children left, and nothing else did"), Tree->GetRowCount(), 2);
	TestTrue(TEXT("the second root is still there"),
		(UObject*)Tree->GetRowWidget(3) == (UObject*)Tree->RowNodes[1].Get());
	TestNull(TEXT("and a hidden item has no row to find"), Tree->GetRowWidget(1));

	// Selection is an index into the SOURCE, not into what is on screen, so it means the same thing
	// on either side of a collapse -- which is the whole reason it is spelled that way.
	Tree->SetSelectedIndex(3);
	TestEqual(TEXT("the surviving row is selected"), Tree->GetSelectedIndex(), 3);
	Tree->ToggleItemExpansion(0);
	TestTrue(TEXT("toggling put the folder back"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and its children with it"), Tree->GetRowCount(), 4);
	TestEqual(TEXT("the selection still names the same item"), Tree->GetSelectedIndex(), 3);
	if (UDreamWidget* SelectedRow = Tree->GetRowWidget(3))
	{
		if (UUIButton* Button = SelectedRow->GetComponent<UUIButton>())
		{
			TestEqual(TEXT("and the rebuilt row is the one wearing the highlight"),
				Button->GetNormalColor(), FDreamTreeViewStyle().List.RowSelected);
		}
	}

	Tree->CollapseAll();
	TestEqual(TEXT("collapsing everything leaves the roots"), Tree->GetRowCount(), 2);
	Tree->ExpandAll();
	TestEqual(TEXT("and expanding everything brings it all back"), Tree->GetRowCount(), 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
