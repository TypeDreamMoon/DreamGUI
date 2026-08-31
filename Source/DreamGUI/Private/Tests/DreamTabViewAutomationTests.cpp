// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamTabView.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The tab view, aimed the same way as the rest of the control suite: at the wiring that fails
 * SILENTLY. A page the switcher never adopted is a page nobody can reach and nothing says so; a
 * selected tint aimed at the same visual as the hover tint survives until the next mouse move; a
 * strip and a switcher that disagree about how many things there are put the wrong page under the
 * wrong caption; and an indicator fed a ratio anchor is correct on full-layout frames and zero-wide
 * on every other one, which is a flicker rather than a failure.
 *
 * Everything runs headless: no world, no registration, no layout pass. That shapes three things
 * here. Pages are authored the way a .dui node arrives -- attached to the control BEFORE Initialize
 * -- because that is literally the mechanism, not a stand-in for it. Colours are read either from
 * behaviour state or through the immediate application paths (SetOnColor/SetOffColor apply at once
 * for the state the toggle is actually in; the tween manager returns null without a world). And the
 * arranged rect a real strip would have is written by hand before the indicator is asked to follow
 * it, because with no layout pass every tab otherwise sits at its birth rect.
 *
 * With no project style sheet under a test, ResolveStyle falls back to the inline Style without
 * StyleSource being touched, so an inline value IS the value in effect.
 */
namespace DreamTabViewTestLocal
{
	/** A page, exactly as a nested .dui node arrives: a child of the control, before Initialize. */
	UDreamWidget* AuthorPage(UDreamTabView* InView, const TCHAR* InName)
	{
		UDreamWidget* Page = NewObject<UDreamWidget>(GetTransientPackage());
		Page->SetDisplayName(InName);
		Page->SetParentBeforeRegister(InView);
		return Page;
	}

	FText MakeLabel(const TCHAR* InText)
	{
		return FText::AsCultureInvariant(InText);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTabViewStripTest,
	"DreamGUI.Controls.TabView.TheStripGrowsOneTabPerLabelAndTheGroupKeepsOneOfThemLit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTabViewStripTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTestLocal;

	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	View->TabLabels = { MakeLabel(TEXT("Video")), MakeLabel(TEXT("Audio")), MakeLabel(TEXT("Controls")) };
	View->Initialize();

	if (!TestNotNull(TEXT("the strip exists"), View->StripNode.Get())
		|| !TestNotNull(TEXT("the group is always there"), View->TabGroup.Get())
		|| !TestNotNull(TEXT("the switcher is always there"), View->PageSwitcher.Get()))
	{
		return false;
	}
	if (!TestEqual(TEXT("one tab per label"), View->Tabs.Num(), 3))
	{
		return false;
	}

	for (int32 Index = 0; Index < View->Tabs.Num(); ++Index)
	{
		const FDreamTabViewTab& Tab = View->Tabs[Index];
		TestTrue(TEXT("every tab hangs in the strip"),
			(UObject*)(Tab.TabNode != nullptr ? Tab.TabNode->GetParent() : nullptr) == (UObject*)View->StripNode.Get());
		if (!TestNotNull(TEXT("every tab has its toggle"), Tab.Toggle.Get()))
		{
			return false;
		}
		// Wired here rather than found in Awake: the control makes both ends in NativeOnInitialized,
		// and a group the toggle only discovers at begin play is a group headless code cannot see.
		TestTrue(TEXT("every toggle joined the one group"),
			(UObject*)Tab.Toggle->GetToggleGroup() == (UObject*)View->TabGroup.Get());

		// The library's recurring split, restated for a tab: the pointer states tint the face, the
		// checked state tints the plate. Aimed at one visual they overwrite each other and the
		// selected colour survives only until the next hover.
		UDreamVisual* Pointer = Tab.Toggle->GetTransitionTarget();
		UDreamVisual* Checked = Tab.Toggle->GetToggleTransitionTarget();
		TestNotNull(TEXT("the pointer transition has a target"), Pointer);
		TestNotNull(TEXT("the checked transition has a target"), Checked);
		TestTrue(TEXT("they are two visuals, not one"), (UObject*)Pointer != (UObject*)Checked);
		TestTrue(TEXT("the pointer transition tints the face"),
			(UObject*)Pointer == (UObject*)(Tab.TabNode != nullptr ? Tab.TabNode->GetVisual() : nullptr));
		TestTrue(TEXT("the checked transition tints the plate"),
			(UObject*)Checked == (UObject*)(Tab.SelectedNode != nullptr ? Tab.SelectedNode->GetVisual() : nullptr));
	}

	if (UDreamText* FirstCaption = Cast<UDreamText>(View->Tabs[0].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the caption is the label at that index"),
			FirstCaption->GetText().ToString(), FString(TEXT("Video")));
	}
	if (UDreamText* LastCaption = Cast<UDreamText>(View->Tabs[2].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("and so is the last one"),
			LastCaption->GetText().ToString(), FString(TEXT("Controls")));
	}

	// Exactly one tab is lit to start with, and it is the authored index.
	TestTrue(TEXT("the first tab starts lit"), View->Tabs[0].Toggle->GetValue());
	TestFalse(TEXT("and no sibling is"), View->Tabs[1].Toggle->GetValue());
	TestFalse(TEXT("nor the other one"), View->Tabs[2].Toggle->GetValue());

	// Mutual exclusion, driven through the seam a click lands on: UUIToggle::SetValue is what
	// OnPointerClick calls, so this is the click path exercised directly, which is all a headless
	// test may claim.
	View->Tabs[2].Toggle->SetValue(true);
	TestTrue(TEXT("the clicked tab is lit"), View->Tabs[2].Toggle->GetValue());
	TestFalse(TEXT("and the group switched the old one off"), View->Tabs[0].Toggle->GetValue());
	TestEqual(TEXT("the control followed its own tab"), View->GetActiveTabIndex(), 2);

	// A second click on the open tab must not close it: the toggle's click flips the value, and a
	// group that allowed an empty selection would leave the strip dark with a page still showing.
	View->Tabs[2].Toggle->SetValue(false);
	TestTrue(TEXT("the open tab refuses to close itself"), View->Tabs[2].Toggle->GetValue());
	TestEqual(TEXT("so the view stayed where it was"), View->GetActiveTabIndex(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTabViewSwitchTest,
	"DreamGUI.Controls.TabView.MovingTheActiveIndexMovesThePageAndReTintsTheTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTabViewSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTestLocal;

	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	View->TabLabels = { MakeLabel(TEXT("One")), MakeLabel(TEXT("Two")), MakeLabel(TEXT("Three")) };
	UDreamWidget* PageOne = AuthorPage(View.Get(), TEXT("One"));
	UDreamWidget* PageTwo = AuthorPage(View.Get(), TEXT("Two"));
	UDreamWidget* PageThree = AuthorPage(View.Get(), TEXT("Three"));
	View->Initialize();

	if (!TestEqual(TEXT("three pages, three tabs"), View->Tabs.Num(), 3)
		|| !TestEqual(TEXT("the switcher holds all three"), View->GetPageCount(), 3))
	{
		return false;
	}

	// Dynamic delegates need a UFUNCTION to land on and a test cpp cannot declare a UCLASS of its
	// own, so a second, uninitialized tab view is the listener: SetActiveTabIndexWithoutNotify is
	// signature-compatible and, with no parts underneath, just records what the event carried.
	TDreamTestControl<UDreamTabView> Probe(NewObject<UDreamTabView>(GetTransientPackage()));
	View->OnTabChanged.AddDynamic(Probe.Get(), &UDreamTabView::SetActiveTabIndexWithoutNotify);

	const FDreamTabViewStyle Defaults;
	auto CaptionColor = [](const FDreamTabViewTab& InTab) -> FColor
	{
		UDreamText* Text = InTab.LabelNode != nullptr ? Cast<UDreamText>(InTab.LabelNode->GetVisual()) : nullptr;
		return Text != nullptr ? Text->GetColor() : FColor::Transparent;
	};

	TestTrue(TEXT("the first page is the one showing"), (UObject*)View->GetActivePage() == (UObject*)PageOne);
	TestEqual(TEXT("the open tab's caption wears the selected colour"),
		CaptionColor(View->Tabs[0]), Defaults.LabelSelectedColor);
	TestEqual(TEXT("a closed tab's caption wears the plain one"),
		CaptionColor(View->Tabs[1]), Defaults.LabelColor);

	View->SetActiveTabIndex(2);

	// The page moved -- through the switcher, which is the one thing that decides what is on screen.
	TestEqual(TEXT("the switcher took the new index"), View->PageSwitcher->ActiveWidgetIndex, 2);
	TestTrue(TEXT("and shows the third page"), (UObject*)View->GetActivePage() == (UObject*)PageThree);
	TestTrue(TEXT("the second page is still parked in the switcher"),
		(UObject*)PageTwo->GetParent() == (UObject*)View->PageHostNode.Get());

	// The tabs re-tinted, both halves of it: the checked transition's DRIVER moved (its colour rides
	// a tween, which does nothing without a world), and the caption colour -- the control's own push,
	// because a selectable's two transitions are already spoken for -- moved with it.
	TestTrue(TEXT("the new tab is lit"), View->Tabs[2].Toggle->GetValue());
	TestFalse(TEXT("the old tab is not"), View->Tabs[0].Toggle->GetValue());
	TestEqual(TEXT("the new tab's caption is the selected colour"),
		CaptionColor(View->Tabs[2]), Defaults.LabelSelectedColor);
	TestEqual(TEXT("the old tab's caption went back to the plain one"),
		CaptionColor(View->Tabs[0]), Defaults.LabelColor);

	TestEqual(TEXT("OnTabChanged fired with the new index"), Probe->ActiveTabIndex, 2);

	// The value convention: a two-way binding synthesizes its reverse route against OnValueChangedBP,
	// so it has to fire alongside the spoken event rather than instead of it.
	TDreamTestControl<UDreamTabView> ValueProbe(NewObject<UDreamTabView>(GetTransientPackage()));
	View->OnValueChangedBP.AddDynamic(ValueProbe.Get(), &UDreamTabView::SetActiveTabIndexWithoutNotify);
	View->SetActiveTabIndex(1);
	TestEqual(TEXT("OnValueChangedBP fired with the same index"), ValueProbe->ActiveTabIndex, 1);
	TestEqual(TEXT("and the spoken event did too"), Probe->ActiveTabIndex, 1);

	// Setting the index it already holds is not a change, so nobody is told about it.
	Probe->ActiveTabIndex = 99;
	View->SetActiveTabIndex(1);
	TestEqual(TEXT("an index that did not move broadcasts nothing"), Probe->ActiveTabIndex, 99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTabViewPagesTest,
	"DreamGUI.Controls.TabView.TheChildrenAConsumerNestedBecomeThePagesAndNameTheTabs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTabViewPagesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTestLocal;

	// No labels at all: this is the whole of what a .dui author writes to get a working tab view.
	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	UDreamWidget* Video = AuthorPage(View.Get(), TEXT("Video"));
	UDreamWidget* Audio = AuthorPage(View.Get(), TEXT("Audio"));
	View->Initialize();

	if (!TestNotNull(TEXT("the page host exists"), View->PageHostNode.Get()))
	{
		return false;
	}
	TestEqual(TEXT("both authored children became pages"), View->GetPageCount(), 2);
	TestTrue(TEXT("the first is the first page"), (UObject*)View->GetPage(0) == (UObject*)Video);
	TestTrue(TEXT("the second is the second, in authoring order"), (UObject*)View->GetPage(1) == (UObject*)Audio);
	TestTrue(TEXT("a page lives under the switcher now"), (UObject*)Video->GetParent() == (UObject*)View->PageHostNode.Get());

	// The adoption MOVED them: left as direct children of the control they would draw over the whole
	// tab view at once, and the switcher would have nothing to switch.
	TestEqual(TEXT("the control keeps only its own root"), View->GetChildren().Num(), 1);
	TestTrue(TEXT("and that root is the body it built"),
		(UObject*)View->GetChildren()[0] == (UObject*)View->BodyNode.Get());

	// With no TabLabels, the strip is still a strip: a tab per page, each wearing its page's node id.
	if (!TestEqual(TEXT("a tab per page, with no label list at all"), View->Tabs.Num(), 2))
	{
		return false;
	}
	if (UDreamText* Caption = Cast<UDreamText>(View->Tabs[0].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the tab wears the page's node id"), Caption->GetText().ToString(), FString(TEXT("Video")));
	}
	if (UDreamText* Caption = Cast<UDreamText>(View->Tabs[1].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("and so does the next"), Caption->GetText().ToString(), FString(TEXT("Audio")));
	}

	// A label, once supplied, wins over the node id -- that is the whole point of having both.
	View->SetTabLabels({ MakeLabel(TEXT("Display")) });
	if (UDreamText* Caption = Cast<UDreamText>(View->Tabs[0].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the label overrides the node id"), Caption->GetText().ToString(), FString(TEXT("Display")));
	}
	TestEqual(TEXT("a shorter label list does not shorten the strip"), View->Tabs.Num(), 2);
	if (UDreamText* Caption = Cast<UDreamText>(View->Tabs[1].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("and the unnamed tab keeps its page's id"), Caption->GetText().ToString(), FString(TEXT("Audio")));
	}

	// A page added from code brings its own tab: the strip is a function of the page count, so the
	// two can never disagree about how many things there are.
	UDreamWidget* Controls = NewObject<UDreamWidget>(GetTransientPackage());
	Controls->SetDisplayName(TEXT("Controls"));
	View->AddPage(Controls);
	TestEqual(TEXT("the new page joined the switcher"), View->GetPageCount(), 3);
	TestEqual(TEXT("and the strip grew with it"), View->Tabs.Num(), 3);
	TestTrue(TEXT("its toggle joined the group like every other"),
		(UObject*)View->Tabs[2].Toggle->GetToggleGroup() == (UObject*)View->TabGroup.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTabViewIndicatorTest,
	"DreamGUI.Controls.TabView.TheIndicatorTakesTheActiveTabsLiveRectRatherThanAnAnchorRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTabViewIndicatorTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTestLocal;

	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	View->Style.IndicatorThickness = 6.0f;
	View->TabLabels = { MakeLabel(TEXT("A")), MakeLabel(TEXT("B")) };
	View->Initialize();

	if (!TestNotNull(TEXT("the indicator exists"), View->IndicatorNode.Get())
		|| !TestEqual(TEXT("two tabs"), View->Tabs.Num(), 2))
	{
		return false;
	}

	// It is in the strip so its anchors resolve in the strip's frame, and out of the row so the box
	// never counts it as a tab-shaped gap. Both halves matter and both are invisible if wrong.
	TestTrue(TEXT("the line hangs in the strip"),
		(UObject*)View->IndicatorNode->GetParent() == (UObject*)View->StripNode.Get());
	TestTrue(TEXT("but takes no place in the row"), View->IndicatorNode->GetIgnoreLayout());
	TestNull(TEXT("so it never asked the row for a slot"), View->IndicatorNode->GetPanelSlot());

	// The arrangement a real horizontal box would have written. Headless there is no layout pass, so
	// these ARE the live numbers the control must read -- which is the claim: absolute values off the
	// tab's own rect, never a ratio the anchor setter has to resolve against a stretching parent's
	// SizeDelta (zero) on every frame that is not a full layout.
	View->StripNode->SetHeight(40.0f);
	View->Tabs[1].TabNode->SetWidth(80.0f);
	View->Tabs[1].TabNode->SetAnchoredPosition(FVector2D(120.0, 0.0));
	View->SetActiveTabIndex(1);

	TestEqual(TEXT("the line is exactly the open tab's width"),
		static_cast<float>(View->IndicatorNode->GetSizeDelta().X), 80.0f);
	TestEqual(TEXT("and exactly the style's thickness"),
		static_cast<float>(View->IndicatorNode->GetSizeDelta().Y), 6.0f);
	TestEqual(TEXT("centred on that tab, not on the strip"),
		static_cast<float>(View->IndicatorNode->GetAnchoredPosition().X), 120.0f);
	TestEqual(TEXT("sitting on the strip's bottom edge"),
		static_cast<float>(View->IndicatorNode->GetAnchoredPosition().Y), -40.0f * 0.5f + 6.0f * 0.5f);

	// Point anchors on both axes: a span the setter would have to resolve is exactly what this
	// control refuses to hand it.
	TestEqual(TEXT("the horizontal anchor is a point"),
		static_cast<float>(View->IndicatorNode->GetAnchorMin().X),
		static_cast<float>(View->IndicatorNode->GetAnchorMax().X));
	TestEqual(TEXT("and so is the vertical one"),
		static_cast<float>(View->IndicatorNode->GetAnchorMin().Y),
		static_cast<float>(View->IndicatorNode->GetAnchorMax().Y));

	// Moving back re-derives from the other tab, so the line is never left under the tab that closed.
	View->Tabs[0].TabNode->SetWidth(50.0f);
	View->Tabs[0].TabNode->SetAnchoredPosition(FVector2D(-30.0, 0.0));
	View->SetActiveTabIndex(0);
	TestEqual(TEXT("the line took the other tab's width"),
		static_cast<float>(View->IndicatorNode->GetSizeDelta().X), 50.0f);
	TestEqual(TEXT("and its position"),
		static_cast<float>(View->IndicatorNode->GetAnchoredPosition().X), -30.0f);

	// Zero thickness turns the line off, which is what the style says it does.
	View->Style.IndicatorThickness = 0.0f;
	View->ApplyStyle();
	TestFalse(TEXT("a thickness of zero puts the line away"), View->IndicatorNode->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTabViewStyleTest,
	"DreamGUI.Controls.TabView.TheStylesNumbersReachTheStripTheTabsAndThePage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTabViewStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTestLocal;

	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	View->Style.TabHeight = 41.0f;
	View->Style.TabSpacing = 9.0f;
	View->Style.TabPadding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	View->Style.TabNormal = FColor(11, 12, 13, 255);
	View->Style.TabHovered = FColor(21, 22, 23, 255);
	View->Style.TabPressed = FColor(31, 32, 33, 255);
	View->Style.TabSelected = FColor(41, 42, 43, 255);
	View->Style.LabelColor = FColor(51, 52, 53, 255);
	View->Style.LabelSelectedColor = FColor(61, 62, 63, 255);
	View->Style.FontSize = 17.0f;
	View->Style.PageBackground = FColor(71, 72, 73, 255);
	View->Style.PagePadding = FMargin(7.0f, 8.0f, 9.0f, 10.0f);
	View->Style.CornerRadius = 3.0f;
	View->TabLabels = { MakeLabel(TEXT("A")), MakeLabel(TEXT("B")) };
	View->Initialize();

	if (!TestEqual(TEXT("two tabs"), View->Tabs.Num(), 2))
	{
		return false;
	}

	// The strip: the gap between tabs is the row's, because the row is the only thing that knows
	// where one tab ends.
	if (UDreamLayoutContainerStackBox* Row = Cast<UDreamLayoutContainerStackBox>(View->StripNode->GetLayoutContainer()))
	{
		TestEqual(TEXT("the row spaces its tabs by the style"), Row->Spacing, 9.0f);
	}
	else
	{
		AddError(TEXT("the strip has no stack box to space its tabs"));
	}

	// TabHeight, made a floor rather than a size: an overlay measures as the MAX over its children,
	// so an authored plate height is the shortest a tab can be while its width still hugs its label.
	TestEqual(TEXT("the plate carries the style's tab height"),
		View->Tabs[0].SelectedNode->GetHeight(), 41.0f);
	TestEqual(TEXT("and states no width of its own"),
		View->Tabs[0].SelectedNode->GetWidth(), 0.0f);

	// The padding is the LABEL's, which is what makes the tab's Auto width hug its text.
	if (UDreamPanelSlot* LabelSlot = View->Tabs[0].LabelNode->GetPanelSlot())
	{
		TestEqual(TEXT("the label's left padding is the style's"), LabelSlot->Padding.Left, 3.0f);
		TestEqual(TEXT("its top padding too"), LabelSlot->Padding.Top, 4.0f);
		TestEqual(TEXT("its right padding too"), LabelSlot->Padding.Right, 5.0f);
		TestEqual(TEXT("its bottom padding too"), LabelSlot->Padding.Bottom, 6.0f);
	}
	else
	{
		AddError(TEXT("the label has no slot to be padded in"));
	}

	if (UDreamText* Caption = Cast<UDreamText>(View->Tabs[0].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the caption is set at the style's size"), Caption->GetFontSize(), 17.0f);
		TestEqual(TEXT("and the open tab's caption is the selected colour"),
			Caption->GetColor(), FColor(61, 62, 63, 255));
	}
	if (UDreamText* Closed = Cast<UDreamText>(View->Tabs[1].LabelNode->GetVisual()))
	{
		TestEqual(TEXT("a closed tab's caption is the plain colour"), Closed->GetColor(), FColor(51, 52, 53, 255));
	}

	// The white trap: a UUISelectable with no explicit colours ships white, so the three pointer
	// states must be ON the behaviour, and the selected colour is a fourth thing that is not one of
	// them -- it rides the checked transition instead.
	TestEqual(TEXT("the tab's normal colour is the style's"),
		View->Tabs[0].Toggle->GetNormalColor(), FColor(11, 12, 13, 255));
	TestEqual(TEXT("its hovered colour too"),
		View->Tabs[0].Toggle->GetHoveredColor(), FColor(21, 22, 23, 255));
	TestEqual(TEXT("its pressed colour too"),
		View->Tabs[0].Toggle->GetPressedColor(), FColor(31, 32, 33, 255));
	TestEqual(TEXT("and selected is its own colour, on the checked transition"),
		View->Tabs[0].Toggle->GetOnColor(), FColor(41, 42, 43, 255));
	TestEqual(TEXT("whose off state is the same colour at zero alpha, so the face shows through"),
		View->Tabs[0].Toggle->GetOffColor(), FColor(41, 42, 43, 0));

	// SetOnColor/SetOffColor apply at once for the state the toggle is in, so the plates are already
	// wearing the right thing with no world and no tween.
	if (UDreamVisual* OpenPlate = View->Tabs[0].SelectedNode->GetVisual())
	{
		TestEqual(TEXT("the open tab's plate is wearing the selected colour"),
			OpenPlate->GetColor(), FColor(41, 42, 43, 255));
	}
	if (UDreamVisual* ClosedPlate = View->Tabs[1].SelectedNode->GetVisual())
	{
		TestEqual(TEXT("a closed tab's plate is invisible"), ClosedPlate->GetColor(), FColor(41, 42, 43, 0));
	}

	// The page area.
	if (UDreamVisual* PageVisual = View->PageHostNode->GetVisual())
	{
		TestEqual(TEXT("the page area wears the style's background"), PageVisual->GetColor(), FColor(71, 72, 73, 255));
	}
	TestEqual(TEXT("the switcher insets its page by the style's left padding"),
		View->PageSwitcher->Padding.Left, 7.0f);
	TestEqual(TEXT("and its bottom padding"), View->PageSwitcher->Padding.Bottom, 10.0f);

	if (UDreamRectBlock* PageRect = Cast<UDreamRectBlock>(View->PageHostNode->GetVisual()))
	{
		TestEqual(TEXT("the page area is rounded by the style"), PageRect->GetCornerRadius().X, 3.0f);
	}
	if (UDreamRectBlock* TabRect = Cast<UDreamRectBlock>(View->Tabs[0].TabNode->GetVisual()))
	{
		TestEqual(TEXT("and so is a tab"), TabRect->GetCornerRadius().X, 3.0f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
