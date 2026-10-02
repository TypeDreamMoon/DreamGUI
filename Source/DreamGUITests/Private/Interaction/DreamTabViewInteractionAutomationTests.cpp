// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIToggle.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamTabView, switched, closed and reordered through the real pointer pipeline.
 *
 * UMG ships no tab view, so the behaviour asserted is the one this control's own header states, which
 * is a browser's: clicking a tab opens it and clicking the open one does nothing; closing the open
 * tab opens its right-hand neighbour; a tab dragged along the strip carries its place with it and the
 * strip announces the move. The tabs are addressed through the control's public Tabs array, which is
 * the parts a consumer is given.
 *
 * Three captions and no pages: the strip is as long as its labels or its pages, whichever is more, so
 * captions alone make three real tabs, and a caption is what a tab's identity is easiest to read by
 * after it has moved.
 */
namespace DreamPressTabViewTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FPlacedTabView
	{
		UDreamTabView* TabView = nullptr;

		bool IsReady() const { return TabView != nullptr && TabView->Tabs.Num() == 3; }
	};

	FPlacedTabView PlaceTabView(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		bool bInClosable, bool bInDraggable, int32 InStartTab)
	{
		FPlacedTabView Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamTabView* TabView = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
		if (!InTest.TestNotNull(TEXT("A tab view can be made on the rig"), TabView))
		{
			return Placed;
		}
		TabView->SetTabLabels({
			FText::AsCultureInvariant(TEXT("Video")),
			FText::AsCultureInvariant(TEXT("Audio")),
			FText::AsCultureInvariant(TEXT("Input")) });
		TabView->SetTabsClosable(bInClosable);
		TabView->SetTabsDraggable(bInDraggable);
		// Written before anyone listens, so the counts below are the pointer's and nothing else's.
		TabView->SetActiveTabIndex(InStartTab);
		TabView->OnTabChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleTabChanged);
		TabView->OnTabClosed.AddDynamic(InListener, &UDreamPressInteractionListener::HandleTabClosed);
		TabView->OnTabReordered.AddDynamic(InListener, &UDreamPressInteractionListener::HandleTabReordered);
		InRig.PumpFrames(2);

		Placed.TabView = TabView;
		InTest.TestTrue(TEXT("The strip has a tab per caption"), Placed.IsReady());
		return Placed;
	}

	FString CaptionAt(const UDreamTabView* InTabView, int32 InIndex)
	{
		return InTabView->TabLabels.IsValidIndex(InIndex) ? InTabView->TabLabels[InIndex].ToString() : FString();
	}

	/** The tab InIndex of the strip as it stands now, or null. */
	UDreamWidget* TabAt(const UDreamTabView* InTabView, int32 InIndex)
	{
		return InTabView->Tabs.IsValidIndex(InIndex) ? InTabView->Tabs[InIndex].TabNode.Get() : nullptr;
	}

	/** Whether player 0's focus is on InWidget or on something inside it. */
	bool IsFocusOnOrIn(UDreamUIInputServices* InServices, const UDreamWidget* InWidget)
	{
		const UDreamWidget* Focused = InServices != nullptr ? InServices->GetFocusedWidget(0) : nullptr;
		return Focused != nullptr && InWidget != nullptr && (Focused == InWidget || Focused->IsChildOf(InWidget));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewSwitchTest,
	"DreamGUI.TabView.ClickingTheThirdTabOpensItAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewSwitchTest, "DreamGUI.TabView.ClickingTheThirdTabOpensItAndSaysSoOnce", "[Pointer][Animated]")

bool FDreamPressTabViewSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, false, 0);
	if (!Placed.IsReady())
	{
		return false;
	}

	FDreamElementRef Third = Rig.Driver()->Find(FDreamBy::Widget(Placed.TabView->Tabs[2].TabNode.Get()));
	TestTrue(TEXT("Clicking the third tab completes"), Third->Click());

	TestEqual(TEXT("The third tab is the open one"), Placed.TabView->GetActiveTabIndex(), 2);
	if (TestEqual(TEXT("The switch was announced once"), Listener->TabChangedIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the third tab"), Listener->TabChangedIndices[0], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewReclickTest,
	"DreamGUI.TabView.ClickingTheOpenTabAgainChangesNothingAndSaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewReclickTest, "DreamGUI.TabView.ClickingTheOpenTabAgainChangesNothingAndSaysNothing", "[Pointer][Animated]")

bool FDreamPressTabViewReclickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, false, 1);
	if (!Placed.IsReady())
	{
		return false;
	}

	FDreamElementRef Open = Rig.Driver()->Find(FDreamBy::Widget(Placed.TabView->Tabs[1].TabNode.Get()));
	TestTrue(TEXT("Clicking the tab that is already open completes"), Open->Click());

	TestEqual(TEXT("It is still the open tab"), Placed.TabView->GetActiveTabIndex(), 1);
	TestEqual(TEXT("And nothing was announced"), Listener->TabChangedIndices.Num(), 0);
	// The index alone would not show a tab that switched itself OFF under the click -- the strip's
	// group is what forbids that, and the tab's own toggle is where it would show.
	UUIToggle* OpenToggle = Placed.TabView->Tabs[1].Toggle.Get();
	if (TestNotNull(TEXT("The tab is a toggle"), OpenToggle))
	{
		TestTrue(TEXT("And it is still lit"), OpenToggle->GetValue());
	}
	return true;
}

/**
 * The browser's rule, which the header states for CloseTab: closing the OPEN tab leaves the index
 * where it is, which is now its right-hand neighbour. The middle tab is the one closed here so that
 * the neighbour exists on both sides and "which one opened" is a real question.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewCloseTest,
	"DreamGUI.TabView.ClickingTheOpenTabsCloseButtonClosesItAndOpensItsRightNeighbour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewCloseTest, "DreamGUI.TabView.ClickingTheOpenTabsCloseButtonClosesItAndOpensItsRightNeighbour", "[Pointer][Animated]")

bool FDreamPressTabViewCloseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), true, false, 1);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamWidget* CloseButton = Placed.TabView->Tabs[1].CloseNode.Get();
	if (!TestNotNull(TEXT("A closable tab carries a close button"), CloseButton))
	{
		return false;
	}

	FDreamElementRef Close = Rig.Driver()->Find(FDreamBy::Widget(CloseButton));
	TestTrue(TEXT("Clicking the open tab's close button completes"), Close->Click());
	Rig.PumpFrames(1);

	if (TestEqual(TEXT("The close was announced once"), Listener->TabClosedIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the tab that was closed"), Listener->TabClosedIndices[0], 1);
	}
	TestEqual(TEXT("Two tabs remain"), Placed.TabView->Tabs.Num(), 2);
	TestEqual(TEXT("The open place is still the second one"), Placed.TabView->GetActiveTabIndex(), 1);
	TestEqual(TEXT("Which is now the closed tab's right-hand neighbour"), CaptionAt(Placed.TabView, 1), FString(TEXT("Input")));
	return true;
}

/**
 * Pressed on the first tab and let go over the third, having crossed the drag threshold on the way:
 * the first tab ends up third and the strip says so. Driven by raw pixels worked out before the drag
 * starts, because the tabs MOVE under the drag -- a locator pinned to the old third tab would aim at
 * wherever that tab had gone by the time the drag arrived there.
 *
 * The move goes straight from just past the threshold to the third tab, never resting over the
 * second: the reorder is live, one swap per tab the pointer passes, and this claim is about the one
 * move from first to third.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewReorderTest,
	"DreamGUI.TabView.DraggingTheFirstTabOntoTheThirdMovesItThereAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewReorderTest, "DreamGUI.TabView.DraggingTheFirstTabOntoTheThirdMovesItThereAndSaysSoOnce", "[Pointer][Animated]")

bool FDreamPressTabViewReorderTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, true, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	const TOptional<FVector2D> FirstPixel = FDreamDriverProjection::WidgetCentrePixel(Placed.TabView->Tabs[0].TabNode.Get());
	const TOptional<FVector2D> ThirdPixel = FDreamDriverProjection::WidgetCentrePixel(Placed.TabView->Tabs[2].TabNode.Get());
	if (!TestTrue(TEXT("Both tabs are somewhere the pointer can reach"), FirstPixel.IsSet() && ThirdPixel.IsSet()))
	{
		return false;
	}
	// Past the raycaster's own threshold, read rather than assumed -- the comparison is strictly
	// greater-than, so landing exactly on it would still be a press.
	const double ThresholdPixels = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));
	const FVector2D Towards = (ThirdPixel.GetValue() - FirstPixel.GetValue()).GetSafeNormal();
	const FVector2D PastThreshold = FirstPixel.GetValue() + Towards * (ThresholdPixels + 2.0);

	TestTrue(TEXT("Dragging the first tab onto the third completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(FirstPixel.GetValue())
			.Press()
			.MoveToPixel(PastThreshold)
			.MoveToPixel(ThirdPixel.GetValue())
			.WaitFrames(1)
			.Release()
			.Perform());
	Rig.PumpFrames(1);

	TestEqual(TEXT("The tab that was first is now third"), CaptionAt(Placed.TabView, 2), FString(TEXT("Video")));
	TestEqual(TEXT("And the other two closed up in front of it"), CaptionAt(Placed.TabView, 0), FString(TEXT("Audio")));
	if (TestEqual(TEXT("The move was announced once"), Listener->TabReorders.Num(), 1))
	{
		TestEqual(TEXT("From the first place"), Listener->TabReorders[0].X, 0);
		TestEqual(TEXT("To the third"), Listener->TabReorders[0].Y, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewDisabledTabTest,
	"DreamGUI.TabView.ClickingADisabledTabLeavesTheOpenTabWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewDisabledTabTest, "DreamGUI.TabView.ClickingADisabledTabLeavesTheOpenTabWhereItWas", "[Pointer][Disabled]")

/*
 * The header promises a disabled tab cannot be clicked. The tab is disabled through its toggle's own
 * switch, which keeps it hit-testable on purpose, so the click arrives -- and the toggle's click
 * handler, unlike the button's, never asked whether it was enabled: the disabled tab switched itself on
 * and opened. Clicking it now changes nothing and announces nothing.
 */
bool FDreamPressTabViewDisabledTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, false, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.TabView->SetTabEnabled(2, false);
	Rig.PumpFrames(1);

	FDreamElementRef Third = Rig.Driver()->Find(FDreamBy::Widget(Placed.TabView->Tabs[2].TabNode.Get()));
	TestTrue(TEXT("Clicking the disabled third tab completes"), Third->Click());

	TestEqual(TEXT("The first tab is still the open one"), Placed.TabView->GetActiveTabIndex(), 0);
	TestEqual(TEXT("And no switch was announced"), Listener->TabChangedIndices.Num(), 0);
	UUIToggle* ThirdToggle = Placed.TabView->Tabs[2].Toggle.Get();
	if (TestNotNull(TEXT("The tab is a toggle"), ThirdToggle))
	{
		TestFalse(TEXT("And it did not switch itself on"), ThirdToggle->GetValue());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewDragPastTwoTest,
	"DreamGUI.TabView.DraggingTheFirstTabAcrossTheOtherTwoSwapsItPastEachInTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewDragPastTwoTest, "DreamGUI.TabView.DraggingTheFirstTabAcrossTheOtherTwoSwapsItPastEachInTurn", "[Pointer][Animated]")

/*
 * The reorder is live, one swap per tab the pointer passes. It stopped after the first: the move
 * rebuilt the strip, destroying the tab under the drag, and the event system drops a drag whose widget
 * is gone without ending it -- so dragging the first tab over the second and on to the third swapped it
 * once and then nothing. Here the drag rests a frame over the second tab, then goes on to rest over the
 * third: the strip ends Audio, Input, Video, and says so in two moves.
 */
bool FDreamPressTabViewDragPastTwoTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, true, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	// Worked out before the drag, for the reason the single-move test gives.
	const TOptional<FVector2D> FirstPixel = FDreamDriverProjection::WidgetCentrePixel(Placed.TabView->Tabs[0].TabNode.Get());
	const TOptional<FVector2D> SecondPixel = FDreamDriverProjection::WidgetCentrePixel(Placed.TabView->Tabs[1].TabNode.Get());
	const TOptional<FVector2D> ThirdPixel = FDreamDriverProjection::WidgetCentrePixel(Placed.TabView->Tabs[2].TabNode.Get());
	if (!TestTrue(TEXT("All three tabs are somewhere the pointer can reach"), FirstPixel.IsSet() && SecondPixel.IsSet() && ThirdPixel.IsSet()))
	{
		return false;
	}
	const double ThresholdPixels = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare()));
	const FVector2D Towards = (SecondPixel.GetValue() - FirstPixel.GetValue()).GetSafeNormal();
	const FVector2D PastThreshold = FirstPixel.GetValue() + Towards * (ThresholdPixels + 2.0);

	TestTrue(TEXT("Dragging the first tab over the second and on to the third completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(FirstPixel.GetValue())
			.Press()
			.MoveToPixel(PastThreshold)
			.MoveToPixel(SecondPixel.GetValue())
			.WaitFrames(1)
			.MoveToPixel(ThirdPixel.GetValue())
			.WaitFrames(1)
			.Release()
			.Perform());
	Rig.PumpFrames(1);

	TestEqual(TEXT("The second caption is now first"), CaptionAt(Placed.TabView, 0), FString(TEXT("Audio")));
	TestEqual(TEXT("The third is now second"), CaptionAt(Placed.TabView, 1), FString(TEXT("Input")));
	TestEqual(TEXT("And the dragged tab is last"), CaptionAt(Placed.TabView, 2), FString(TEXT("Video")));
	if (TestEqual(TEXT("Each tab passed was announced as its own move"), Listener->TabReorders.Num(), 2))
	{
		TestEqual(TEXT("The first move went from the first place to the second"), Listener->TabReorders[0], FIntPoint(0, 1));
		TestEqual(TEXT("The second from the second place to the third"), Listener->TabReorders[1], FIntPoint(1, 2));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewCloseLastOpenTest,
	"DreamGUI.TabView.ClosingTheOpenLastTabOpensItsLeftNeighbourAndSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewCloseLastOpenTest, "DreamGUI.TabView.ClosingTheOpenLastTabOpensItsLeftNeighbourAndSaysSo", "[Pointer][Animated]")

/*
 * Closing the open tab opens another, and when it was the last tab the index itself moves -- 2 to 1
 * here. OnTabChanged is fired whoever changes the open tab, and it is the road a two-way binding hears
 * the index by, yet a close changed both silently: a bound variable went on holding 2 with two tabs
 * left. Closing the open last tab now announces the neighbour it opened, once.
 */
bool FDreamPressTabViewCloseLastOpenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), true, false, 2);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamWidget* CloseButton = Placed.TabView->Tabs[2].CloseNode.Get();
	if (!TestNotNull(TEXT("A closable tab carries a close button"), CloseButton))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the open last tab's close button completes"), Rig.Driver()->Find(FDreamBy::Widget(CloseButton))->Click());
	Rig.PumpFrames(1);

	if (TestEqual(TEXT("The close was announced once"), Listener->TabClosedIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the last tab"), Listener->TabClosedIndices[0], 2);
	}
	TestEqual(TEXT("Its left neighbour is the open one now"), Placed.TabView->GetActiveTabIndex(), 1);
	if (TestEqual(TEXT("And the change of open tab was announced once"), Listener->TabChangedIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the neighbour"), Listener->TabChangedIndices[0], 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewFocusPageTest,
	"DreamGUI.TabView.AfterOpeningATabWithThePadTheNextConfirmPressesItsPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewFocusPageTest, "DreamGUI.TabView.AfterOpeningATabWithThePadTheNextConfirmPressesItsPage", "[Nav][Animated]")

/*
 * bFocusPageOnTabChange exists so a pad player who opens a tab lands in its page, and it moved focus nowhere: the switcher
 * only marks itself for layout when its index moves, so the page was still collapsed when it was searched, and nothing in
 * a collapsed page counts as navigable. The switcher is laid out first now, and the page's first control is focused the
 * way a stick press focuses -- the navigation cursor goes with it.
 *
 * Checked here: two pages, each with a button; the pad's focus on the second tab and its confirm pressed. The second
 * page is open, its button has focus, and the next confirm presses that button.
 */
bool FDreamPressTabViewFocusPageTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTabView* TabView = Rig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
	UDreamWidget* PageA = Rig.MakeWidget(TEXT("Video"), nullptr, FVector2D(400.0, 200.0));
	UDreamWidget* PageB = Rig.MakeWidget(TEXT("Audio"), nullptr, FVector2D(400.0, 200.0));
	UDreamWidget* ButtonA = PageA != nullptr ? Rig.MakeWidget(TEXT("VideoButton"), PageA, FVector2D(120.0, 40.0)) : nullptr;
	UDreamWidget* ButtonB = PageB != nullptr ? Rig.MakeWidget(TEXT("AudioButton"), PageB, FVector2D(120.0, 40.0)) : nullptr;
	if (!TestNotNull(TEXT("A tab view can be made on the rig"), TabView)
		|| !TestNotNull(TEXT("with a first page and a button on it"), ButtonA)
		|| !TestNotNull(TEXT("and a second page and a button on it"), ButtonB))
	{
		return false;
	}
	UUIButton* PressableB = ButtonB->AddComponent<UUIButton>();
	ButtonA->AddComponent<UUIButton>();
	if (!TestNotNull(TEXT("The second page's button is a button"), PressableB))
	{
		return false;
	}
	PressableB->GetOnClickEvent().AddUObject(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	TabView->AddPage(PageA);
	TabView->AddPage(PageB);
	TabView->SetFocusPageOnTabChange(true);
	Rig.PumpFrames(2);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestEqual(TEXT("A tab per page"), TabView->Tabs.Num(), 2)
		|| !TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The pad's focus goes onto the second tab"), Services->FocusForNavigation(TabAt(TabView, 1), 0)))
	{
		return false;
	}

	TestTrue(TEXT("Pressing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	Rig.PumpFrames(1);
	TestEqual(TEXT("The second tab is open"), TabView->GetActiveTabIndex(), 1);
	TestTrue(TEXT("Focus went into its page, onto its button"), Services->GetFocusedWidget(0) == ButtonB);
	TestTrue(TEXT("which is the event system's selection"), Rig.EventSystem()->GetCurrentSelectedComponent(0) == ButtonB);

	// The cursor went with the focus: the confirm presses what the cursor is on, which is the page's button now.
	const int32 ClicksBefore = Listener->ClickedCount;
	TestTrue(TEXT("Pressing the pad's confirm again completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	TestEqual(TEXT("and it pressed the page's button"), Listener->ClickedCount, ClicksBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewPadCloseTest,
	"DreamGUI.TabView.ClosingTheFocusedTabWithThePadLeavesFocusOnItsRightNeighbour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewPadCloseTest, "DreamGUI.TabView.ClosingTheFocusedTabWithThePadLeavesFocusOnItsRightNeighbour", "[Nav][Animated]")

/*
 * Closing a tab rebuilds the whole strip, and a pad player closes a tab by pressing its close button -- the very widget
 * the rebuild destroys -- so their focus went with it, and the next stick press started from nowhere. Every rebuild now
 * carries pad focus across: to the same tab where it survives, and from a tab that is gone to the one that took its
 * place, its right neighbour.
 *
 * Checked here: Video, Audio, Input, closable; the pad's focus on Audio's close button and its confirm pressed. Audio is
 * gone, focus is on Input, and a press of Left from there reaches Video, so the cursor is on Input as well.
 */
bool FDreamPressTabViewPadCloseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), true, false, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	UDreamWidget* AudioClose = Placed.TabView->Tabs[1].CloseNode.Get();
	if (!TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestNotNull(TEXT("The second tab has a close button"), AudioClose)
		|| !TestTrue(TEXT("The pad's focus goes onto it"), Services->FocusForNavigation(AudioClose, 0)))
	{
		return false;
	}

	TestTrue(TEXT("Pressing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The confirm closed the tab"), Placed.TabView->Tabs.Num(), 2)
		|| !TestEqual(TEXT("Its right neighbour took its place"), CaptionAt(Placed.TabView, 1), FString(TEXT("Input"))))
	{
		return false;
	}
	TestTrue(TEXT("Focus is on that neighbour"), Services->GetFocusedWidget(0) == TabAt(Placed.TabView, 1));
	TestTrue(TEXT("which is the event system's selection"), Rig.EventSystem()->GetCurrentSelectedComponent(0) == TabAt(Placed.TabView, 1));

	// From the neighbour, a step left reaches the first tab -- or its close button, which sits inside it.
	TestTrue(TEXT("A press of Left completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Left).Perform());
	TestTrue(TEXT("and lands on the first tab, so the cursor was on the neighbour"), IsFocusOnOrIn(Services, TabAt(Placed.TabView, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewRelabelFocusTest,
	"DreamGUI.TabView.RelabellingTheStripKeepsPadFocusOnTheSameTab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewRelabelFocusTest, "DreamGUI.TabView.RelabellingTheStripKeepsPadFocusOnTheSameTab", "[Nav][Animated]")

/*
 * SetTabLabels, AddPage and SetTabTemplateClass all rebuild the strip, which destroyed the tab under the pad's cursor and
 * left focus nowhere. The rebuild now finds the same tab again -- by its page, else by its caption, else by its place --
 * and puts focus back on it.
 *
 * Checked here: focus on the second of Video, Audio, Input. Renamed in place to Sound, it is still the second tab focus is
 * on; moved to the front by a relabelling, focus follows the caption there.
 */
bool FDreamPressTabViewRelabelFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), false, false, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The pad's focus goes onto the second tab"), Services->FocusForNavigation(TabAt(Placed.TabView, 1), 0)))
	{
		return false;
	}
	UDreamWidget* SecondBefore = TabAt(Placed.TabView, 1);

	Placed.TabView->SetTabLabels({
		FText::AsCultureInvariant(TEXT("Video")),
		FText::AsCultureInvariant(TEXT("Sound")),
		FText::AsCultureInvariant(TEXT("Input")) });
	Rig.PumpFrames(1);
	TestTrue(TEXT("The relabelling rebuilt the strip"), TabAt(Placed.TabView, 1) != SecondBefore);
	TestTrue(TEXT("and focus is on the second tab it built"), Services->GetFocusedWidget(0) == TabAt(Placed.TabView, 1));
	TestTrue(TEXT("which is the event system's selection"), Rig.EventSystem()->GetCurrentSelectedComponent(0) == TabAt(Placed.TabView, 1));

	Placed.TabView->SetTabLabels({
		FText::AsCultureInvariant(TEXT("Sound")),
		FText::AsCultureInvariant(TEXT("Video")),
		FText::AsCultureInvariant(TEXT("Input")) });
	Rig.PumpFrames(1);
	TestTrue(TEXT("Moved to the front by its caption, the focused tab takes focus with it"),
		Services->GetFocusedWidget(0) == TabAt(Placed.TabView, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressTabViewHiddenPartFocusTest,
	"DreamGUI.TabView.DisablingTheFocusedTabOrHidingItsCloseButtonMovesPadFocusOffIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressTabViewHiddenPartFocusTest, "DreamGUI.TabView.DisablingTheFocusedTabOrHidingItsCloseButtonMovesPadFocusOffIt", "[Nav][Disabled]")

/*
 * SetTabEnabled(false) and SetTabsClosable(false) are restyles, not rebuilds, and left the pad's focus where it was: on a
 * tab that now refuses every press, or on a close button that had gone to sleep. A disabled tab hands focus to its right
 * neighbour now, and a hidden close button to its own tab.
 */
bool FDreamPressTabViewHiddenPartFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressTabViewTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedTabView Placed = PlaceTabView(*this, Rig, Listener.Get(), true, false, 0);
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The pad's focus goes onto the second tab"), Services->FocusForNavigation(TabAt(Placed.TabView, 1), 0)))
	{
		return false;
	}

	Placed.TabView->SetTabEnabled(1, false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Disabling the focused tab moves focus to its right neighbour"),
		Services->GetFocusedWidget(0) == TabAt(Placed.TabView, 2));

	UDreamWidget* InputClose = Placed.TabView->Tabs[2].CloseNode.Get();
	if (!TestNotNull(TEXT("The third tab has a close button"), InputClose)
		|| !TestTrue(TEXT("The pad's focus goes onto it"), Services->FocusForNavigation(InputClose, 0)))
	{
		return false;
	}
	Placed.TabView->SetTabsClosable(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Hiding the close buttons moves focus from one onto its tab"),
		Services->GetFocusedWidget(0) == TabAt(Placed.TabView, 2));
	return true;
}

#endif
