// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Demo/DreamUIShowcase.h"

#include "DreamControlTestScope.h"
#include "DreamListControlsTestTypes.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamTreeView.h"
#include "Core/Components/DreamLayoutSelfAuthoredSurface.h"
#include "Core/Components/DreamPanelLayouts.h"
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
	// The measure boundary rides the CONTROL, not the inner tree: an authored-surface layout-self
	// overrides the measure per authored axis while the invalidation walk still runs through it to
	// the consumer. IgnoreLayout on the face was tried and is asserted AGAINST here -- it broke the
	// dirty walk too, stranding every inner invalidation on a container-less node (32 layout passes
	// a frame in the designer). The class header on the surface tells that story.
	TestNotNull(TEXT("the control carries the authored-surface boundary"),
		Cast<UDreamLayoutSelfAuthoredSurface>(List->GetLayoutSelf()));
	TestFalse(TEXT("and the face stays inside the layout conversation"),
		List->FaceNode->GetIgnoreLayout());

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

	// THE claim, and the reason the column holds no layout container. A row is an overlay, and an
	// overlay's Auto measure is its content's -- the label's line height, which is nowhere near 44.
	// The row STATES its rect instead of being handed one: point-anchored to the column's top,
	// stretched across it, height straight from the style. The shape this replaced put the rows on
	// equal-weight Fill slots and authored the column's height to exactly rows*RowHeight so the box
	// would divide it back out -- one equation solved in two places, which stopped agreeing the
	// moment anything measured a row's content.
	TestEqual(TEXT("a row is exactly the style's row height"), FirstRow->GetHeight(), 44.0f);
	TestNull(TEXT("a row is placed, not arranged -- the column has no container to slot it into"),
		List->ColumnNode->GetLayoutContainer());
	TestEqual(TEXT("a row is point-anchored to the column's top -- min"),
		static_cast<float>(FirstRow->GetAnchorMin().Y), 1.0f);
	TestEqual(TEXT("-- and max"),
		static_cast<float>(FirstRow->GetAnchorMax().Y), 1.0f);
	TestEqual(TEXT("and stretched across it"),
		static_cast<float>(FirstRow->GetAnchorMax().X - FirstRow->GetAnchorMin().X), 1.0f);
	// One step down the column is RowHeight + RowSpacing, stated from the index and depending on no
	// sibling -- which is what makes recycling possible at all, and what makes the answer available
	// with no layout pass anywhere in sight.
	TestEqual(TEXT("the first row sits at the column's top"),
		static_cast<float>(FirstRow->GetAnchoredPosition().Y), 0.0f);
	TestEqual(TEXT("and the second one pitch below it"),
		static_cast<float>(List->RowNodes[1]->GetAnchoredPosition().Y), -(44.0f + 6.0f));
	// The column is the scroll range: rows, gaps and the viewport's own inset, stated rather than
	// measured.
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
	FDreamControlListViewMeasureTest,
	"DreamGUI.Controls.List.AnAutoConsumerMeasuresTheAuthoredControlNotTheScrolledColumn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewMeasureTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// Enough rows that the scrolled column is unmistakably taller than the control. The designer
	// measured exactly this shape going wrong: a gallery list in an Auto slot, its own height
	// flapping between one wrapped label's measure and the whole column's (59 <-> 1299), because
	// the measure walk crossed the viewport and read the scroll range as a footprint.
	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Style.RowHeight = 44.0f;
	TArray<FString> Sources;
	for (int32 Index = 0; Index < 22; ++Index)
	{
		Sources.Add(FString::Printf(TEXT("Track %d"), Index));
	}
	List->Items = Labels(Sources);
	List->Initialize();

	if (!TestNotNull(TEXT("the face exists"), List->FaceNode.Get()) ||
		!TestNotNull(TEXT("the column exists"), List->ColumnNode.Get()))
	{
		return false;
	}
	// The trap must actually be armed: the column really is all-rows tall -- that is the scroll
	// range, and the point of the assertion below is that it stays a scroll range.
	TestTrue(TEXT("the column is taller than the control"),
		List->ColumnNode->GetHeight() > 200.0f);

	// What an Auto slot actually asks. The authored-surface layout-self overrides the measure on
	// every authored axis, so nothing the walk would find below -- the column's rows, their labels'
	// text -- reaches the answer. A bare control has no panel slot, so the surface reads the
	// authored anchor data: point anchors on both axes, SizeDelta (320, 200).
	TDreamTestControl<UDreamWidget> Panel(NewObject<UDreamWidget>(GetTransientPackage()));
	UDreamPanelLayoutBase* Box = Cast<UDreamPanelLayoutBase>(
		Panel->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass()));
	if (!TestNotNull(TEXT("a consumer's box exists to ask"), Box))
	{
		return false;
	}
	const FVector2D Desired = Box->GetDesiredSize(List.Get());
	TestEqual(TEXT("the measure answers the authored width"),
		static_cast<float>(Desired.X), 320.0f);
	TestEqual(TEXT("-- and the authored height, not the column's"),
		static_cast<float>(Desired.Y), 200.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewRowStateColoursTest,
	"DreamGUI.Controls.List.ARowCarriesEveryStateColourAndNotJustThePointerThree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewRowStateColoursTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// The rows used to push three pointer colours -- normal, hovered, pressed -- and leave the other
	// two to the selectable's library defaults: a flat grey belonging to no theme, and focus visuals
	// that ship OFF. So a row that was switched off wore a colour no sheet could describe, and a
	// keyboard or a pad landing on one showed nothing at all. Values no default could be confused
	// with, so "the style reached it" and "the library default survived" cannot look alike.
	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Style.RowDisabled = FColor(11, 12, 13, 255);
	List->Style.RowFocused = FColor(21, 22, 23, 255);
	List->Style.TransitionDuration = 0.33f;
	List->Items = Labels({ TEXT("Alpha"), TEXT("Beta") });
	List->Initialize();

	UUIButton* RowButton = List->GetRowCount() > 0 && IsValid(List->RowNodes[0])
		? List->RowNodes[0]->GetComponent<UUIButton>()
		: nullptr;
	if (!TestNotNull(TEXT("the first row has a selectable"), RowButton))
	{
		return false;
	}
	TestEqual(TEXT("the style's disabled colour reached the row"),
		RowButton->GetDisabledColor(), FColor(11, 12, 13, 255));
	TestEqual(TEXT("and its focus colour"),
		RowButton->GetFocusedColor(), FColor(21, 22, 23, 255));
	// Pushing a focus colour is what switches the visuals on -- a control that states one means it.
	TestTrue(TEXT("stating a focus colour turns the focus visuals on"),
		RowButton->GetUseFocusedVisuals());
	TestEqual(TEXT("and the transition speed is the style's, not the library's"),
		RowButton->GetAnimDuration(), 0.33f);
	// The three that were already right stay right.
	TestEqual(TEXT("the hover colour is still the style's"),
		RowButton->GetHoveredColor(), FDreamListStyle().RowHovered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewMultiSelectionTest,
	"DreamGUI.Controls.List.MultiSelectionHoldsEveryChosenRowAndNarrowingTheModeKeepsTheAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewMultiSelectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// UUIListView has had four selection modes since it was written and the CONTROL had one index,
	// so a Native.List could never express "these three" -- the four answers existed one layer down
	// and no road reached them.
	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->SelectionMode = EUIListSelectionMode::Multi;
	List->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma"), TEXT("Delta") });
	List->Initialize();

	auto RowNormalColor = [&List](int32 InRowIndex) -> FColor
	{
		UDreamWidget* Row = List->RowNodes.IsValidIndex(InRowIndex) ? List->RowNodes[InRowIndex].Get() : nullptr;
		UUIButton* Button = IsValid(Row) ? Row->GetComponent<UUIButton>() : nullptr;
		return Button != nullptr ? Button->GetNormalColor() : FColor::White;
	};

	List->SetItemSelection(0, true, false);
	List->SetItemSelection(2, true, false);
	TestEqual(TEXT("both chosen rows are held"), List->GetSelectedIndices().Num(), 2);
	TestTrue(TEXT("the first one"), List->IsItemSelected(0));
	TestTrue(TEXT("and the third"), List->IsItemSelected(2));
	TestFalse(TEXT("and nothing else"), List->IsItemSelected(1));
	// The anchor is the row the last selection landed on, and it is what a `<->` binding reads.
	TestEqual(TEXT("the anchor names the last row chosen"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("and both are drawn as selected -- the first"),
		RowNormalColor(0), FDreamListStyle().RowSelected);
	TestEqual(TEXT("-- and the third"),
		RowNormalColor(2), FDreamListStyle().RowSelected);

	// Narrowing the mode narrows the selection, and the rows it dropped have to be REPAINTED. A
	// repaint gated on "did the anchor move" leaves them lit, which is the defect the behaviour-side
	// SetItemSelection carried in exactly this shape.
	List->SetSelectionMode(EUIListSelectionMode::Single);
	TestEqual(TEXT("a single mode keeps one row"), List->GetSelectedIndices().Num(), 1);
	TestEqual(TEXT("and it is the anchor"), List->GetSelectedIndex(), 2);
	TestEqual(TEXT("the row it dropped went back to normal"),
		RowNormalColor(0), FDreamListStyle().RowNormal);

	// And moving a single selection repaints the row it LEFT, not only the one it arrived at -- the
	// other half of the same rule, and the half a repaint gated on "did this item change" gets wrong.
	List->SetItemSelection(0, true, false);
	List->SetItemSelection(2, true, true);
	TestEqual(TEXT("re-selecting the chosen row clears the other"), List->GetSelectedIndices().Num(), 1);
	TestEqual(TEXT("and the cleared row is drawn as cleared"),
		RowNormalColor(0), FDreamListStyle().RowNormal);

	List->ClearSelection();
	TestEqual(TEXT("clearing leaves nothing selected"), List->GetSelectedIndex(), INDEX_NONE);
	TestEqual(TEXT("and no indices"), List->GetSelectedIndices().Num(), 0);
	TestEqual(TEXT("a text source has no objects to answer GetSelectedItems with"),
		List->GetSelectedItems().Num(), 0);

	// None ignores the selection entirely, and says so by dropping what was there.
	List->SetItemSelection(1, true, true);
	List->SetSelectionMode(EUIListSelectionMode::None);
	TestEqual(TEXT("a list nobody can select from holds nothing"), List->GetSelectedIndices().Num(), 0);
	List->SetItemSelection(1, true, true);
	TestEqual(TEXT("and refuses to start"), List->GetSelectedIndex(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewSourceChangeTest,
	"DreamGUI.Controls.List.ReplacingTheSourceMovesASelectionWithItsObjectAndDropsATextOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewSourceChangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// A selection meant an INDEX and nothing else, so handing the list a re-ordered source of the
	// same length left the highlight exactly where it was on screen -- pointing at a different item,
	// silently, with no event to say the choice had changed underneath its consumer.
	UObject* A = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* B = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* C = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());

	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Initialize();
	List->SetItemObjects({ A, B, C });
	List->SetSelectedIndex(2);
	TestEqual(TEXT("the third object is chosen"), List->GetSelectedIndex(), 2);
	if (!TestEqual(TEXT("and GetSelectedItems answers with the object itself"),
		List->GetSelectedItems().Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("-- that object"), (UObject*)List->GetSelectedItems()[0] == C);

	// Same three objects, different order. The selection follows the OBJECT.
	List->SetItemObjects({ C, A, B });
	TestEqual(TEXT("the selection moved with the object it names"), List->GetSelectedIndex(), 0);
	TestTrue(TEXT("and still answers with the same object"),
		List->GetSelectedItems().Num() == 1 && (UObject*)List->GetSelectedItems()[0] == C);

	// An object that left the source takes its selection with it.
	List->SetItemObjects({ A, B });
	TestEqual(TEXT("an object that left loses its selection"), List->GetSelectedIndex(), INDEX_NONE);

	// A text source has no identity at all, so index 1 of the new lines is a different line. Dropped
	// rather than drifted: an index nobody chose is worse than no choice.
	TDreamTestControl<UDreamListView> TextList(Author<UDreamListView>());
	TextList->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	TextList->Initialize();
	TextList->SetSelectedIndex(1);
	TextList->SetItems(Labels({ TEXT("Delta"), TEXT("Epsilon"), TEXT("Zeta") }));
	TestEqual(TEXT("a replaced text source leaves nothing selected"),
		TextList->GetSelectedIndex(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlListViewClickEventsTest,
	"DreamGUI.Controls.List.AClickIsAnnouncedOnceAndASecondOneOnTheSameRowIsADoubleClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlListViewClickEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// A click used to do exactly one thing -- move the selection -- so a consumer could not tell a
	// click on the already-chosen row from no click at all, and had no road to a double click
	// whatever (the event system has none of its own).
	TDreamTestControl<UDreamListView> List(Author<UDreamListView>());
	List->Items = Labels({ TEXT("Alpha"), TEXT("Beta"), TEXT("Gamma") });
	List->Initialize();

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>(GetTransientPackage()));
	List->OnItemClicked.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordItem);
	List->OnItemDoubleClicked.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordSecondItem);

	UUIButton* SecondRow = List->RowNodes.IsValidIndex(1) && IsValid(List->RowNodes[1])
		? List->RowNodes[1]->GetComponent<UUIButton>()
		: nullptr;
	if (!TestNotNull(TEXT("the second row has a selectable to click"), SecondRow))
	{
		return false;
	}

	SecondRow->GetOnClickEvent().Broadcast();
	TestEqual(TEXT("one click, announced once"), Probe->ItemIndices.Num(), 1);
	TestEqual(TEXT("naming the row that was clicked"), Probe->ItemIndices[0], 1);
	TestEqual(TEXT("and it moved the selection first, so a handler sees the new answer"),
		List->GetSelectedIndex(), 1);
	TestEqual(TEXT("one click is not a double click"), Probe->SecondItemIndices.Num(), 0);

	// The second click of a pair arrives as BOTH -- the click first, then the double click -- which
	// is the event system's stated contract, and what lets a row select on the way to opening. The
	// list keeps no clock of its own: the interval belongs to the event system, so a list and the
	// text field beside it cannot disagree about what a double click is.
	SecondRow->GetOnClickEvent().Broadcast();
	SecondRow->GetOnDoubleClickEvent().Broadcast();
	TestEqual(TEXT("the second click is announced as a click too"), Probe->ItemIndices.Num(), 2);
	if (!TestEqual(TEXT("and as a double click"), Probe->SecondItemIndices.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("on the same row"), Probe->SecondItemIndices[0], 1);

	// A plain click on its own is never a double click, whatever came before it.
	SecondRow->GetOnClickEvent().Broadcast();
	TestEqual(TEXT("a lone click adds no double click"), Probe->SecondItemIndices.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTreeViewFoldIdentityTest,
	"DreamGUI.Controls.TreeView.AFoldFollowsItsItemThroughAReorderedSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTreeViewFoldIdentityTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// The collapsed set was a set of INDICES and nothing re-mapped it, so replacing the source left
	// the fold on whatever landed at that number -- another subtree, or a leaf that draws no twisty
	// and therefore cannot be opened again from the screen.
	UObject* Folder = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* ChildA = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* ChildB = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());
	UObject* Other = NewObject<UDreamUIShowcaseTrack>(GetTransientPackage());

	TDreamTestControl<UDreamTreeView> Tree(Make<UDreamTreeView>());
	Tree->SetItemDepths({ 0, 0, 1, 1 });
	Tree->SetItemObjects({ Other, Folder, ChildA, ChildB });
	TestEqual(TEXT("four rows to begin with"), Tree->GetRowCount(), 4);

	Tree->SetItemExpanded(1, false);
	TestEqual(TEXT("folding the folder hides its two children"), Tree->GetRowCount(), 2);

	// The same four objects, the folder first. Index 1 is now a CHILD, so an index-keyed fold would
	// land on it -- and a child with nothing under it draws no twisty to undo the fold with.
	Tree->SetItemDepths({ 0, 1, 1, 0 });
	Tree->SetItemObjects({ Folder, ChildA, ChildB, Other });
	TestTrue(TEXT("the fold followed the folder to its new index"), !Tree->IsItemExpanded(0));
	TestTrue(TEXT("and did not land on the child that took its old one"), Tree->IsItemExpanded(1));
	TestEqual(TEXT("so the tree still shows exactly two rows"), Tree->GetRowCount(), 2);

	// An item that leaves the source takes its fold with it rather than leaving a number behind.
	Tree->SetItemDepths({ 0, 0 });
	Tree->SetItemObjects({ ChildA, Other });
	TestTrue(TEXT("a source without the folder is fully open"), Tree->IsItemExpanded(0));
	TestEqual(TEXT("and shows both of its rows"), Tree->GetRowCount(), 2);

	// A text source has no identity to carry a fold by, so a replacement opens everything rather
	// than folding whatever arrives at the remembered index.
	TDreamTestControl<UDreamTreeView> TextTree(Make<UDreamTreeView>());
	TextTree->SetItemsWithDepths(
		Labels({ TEXT("Folder"), TEXT("A"), TEXT("B"), TEXT("Other") }), { 0, 1, 1, 0 });
	TextTree->SetItemExpanded(0, false);
	TestEqual(TEXT("the text tree folds too"), TextTree->GetRowCount(), 2);
	TextTree->SetItemsWithDepths(
		Labels({ TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four") }), { 0, 1, 1, 0 });
	TestEqual(TEXT("and a replaced text source opens everything"), TextTree->GetRowCount(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTreeViewBatchExpansionEventTest,
	"DreamGUI.Controls.TreeView.ExpandingOrCollapsingEverythingAnnouncesEachRowItMoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTreeViewBatchExpansionEventTest::RunTest(const FString& Parameters)
{
	using namespace DreamListControlsTestLocal;

	// SetItemExpanded announced every toggle and the two batch operations announced nothing, so a
	// consumer keeping its own picture of which nodes are open was silently wrong after either of
	// them -- and had no way to notice.
	TDreamTestControl<UDreamTreeView> Tree(Make<UDreamTreeView>());
	Tree->SetItemsWithDepths(
		Labels({ TEXT("First"), TEXT("A"), TEXT("Second"), TEXT("B"), TEXT("Leaf") }),
		{ 0, 1, 0, 1, 0 });

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>(GetTransientPackage()));
	Tree->OnItemExpansionChanged.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordExpansion);

	Tree->CollapseAll();
	// The two parents, and only them: a leaf in the collapsed set is a fold nothing can undo from
	// the screen, so CollapseAll never takes one -- and must not announce one either.
	if (!TestEqual(TEXT("collapsing everything announces both parents"), Probe->ExpansionIndices.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the first one"), Probe->ExpansionIndices[0], 0);
	TestEqual(TEXT("and the second"), Probe->ExpansionIndices[1], 2);
	TestFalse(TEXT("both as collapsed"), Probe->ExpansionStates[0]);
	TestEqual(TEXT("and the rows agree"), Tree->GetRowCount(), 3);

	// A second CollapseAll moves nothing, so it says nothing.
	Tree->CollapseAll();
	TestEqual(TEXT("collapsing an already-collapsed tree announces nothing"),
		Probe->ExpansionIndices.Num(), 2);

	Tree->ExpandAll();
	if (!TestEqual(TEXT("expanding everything announces the two it opened"),
		Probe->ExpansionIndices.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("as expanded"), Probe->ExpansionStates[2] && Probe->ExpansionStates[3]);
	TestEqual(TEXT("and every row is back"), Tree->GetRowCount(), 5);

	Tree->ExpandAll();
	TestEqual(TEXT("expanding an already-open tree announces nothing"),
		Probe->ExpansionIndices.Num(), 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
