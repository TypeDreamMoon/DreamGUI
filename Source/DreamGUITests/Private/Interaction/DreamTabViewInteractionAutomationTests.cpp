// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
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

#endif
