// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamExpandableArea.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamExpandableArea under a finger, clicked again while it opens, and disabled while its header is held.
 *
 * UMG's UExpandableArea is SExpandableArea: the header is a button whose click flips the area and announces the new state
 * (OnHeaderClicked -> OnToggleContentVisibility -> SetExpanded_Animated, Slate/Private/Widgets/Layout/SExpandableArea.cpp:
 * 114-134, 165-176). A flip while the rollout is still playing is a flip like any other: the area is now the other way and
 * says so, and the rollout heads for the new end.
 *
 * The area is pinned by its top edge so its header stays where it is while the body opens and closes beneath it, and given
 * an expansion time, because the control's default is instant.
 */
namespace DreamExpandableAreaMidGestureTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const float ExpansionSeconds = 0.4f;

	struct FPlacedArea
	{
		UDreamExpandableArea* Area = nullptr;
		UDreamWidget* Body = nullptr;
		TSharedPtr<FDreamDriverElement> Header;

		bool IsReady() const { return Area != nullptr && Body != nullptr && Header.IsValid() && Header->Exists(); }
	};

	/** An area with a body 120 tall, starting collapsed, its announcements going to InListener. */
	FPlacedArea PlaceArea(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener, float InExpansionSeconds)
	{
		FPlacedArea Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamExpandableArea* Area = InRig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0), FVector2D(0.0, 250.0));
		if (!InTest.TestNotNull(TEXT("An expandable area can be made on the rig"), Area)
			|| !InTest.TestNotNull(TEXT("It has a header"), Area->HeaderNode.Get())
			|| !InTest.TestNotNull(TEXT("It has a content column"), Area->ContentNode.Get()))
		{
			return Placed;
		}
		// Pinned by its top edge: Y is up, so a pivot of one is the top.
		Area->SetPivot(FVector2D(0.5, 1.0));
		UDreamWidget* Body = InRig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(200.0, 120.0));
		Area->SetExpansionDuration(0.0f);
		Area->SetIsExpanded(false);
		InRig.PumpFrames(2);
		Area->SetExpansionDuration(InExpansionSeconds);
		if (InListener != nullptr)
		{
			Area->OnExpansionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleExpansionChanged);
		}
		// Every click below is a click of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);

		Placed.Area = Area;
		Placed.Body = Body;
		Placed.Header = InRig.Driver()->Find(FDreamBy::Widget(Area->HeaderNode.Get()));
		InTest.TestTrue(TEXT("The area has a header to click and a body"), Placed.IsReady());
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaTapTest,
	"DreamGUI.ExpandableArea.ATapOnTheHeaderFlipsTheAreaAndSaysWhichWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaTapTest, "DreamGUI.ExpandableArea.ATapOnTheHeaderFlipsTheAreaAndSaysWhichWay", "[Touch][Animated]")

/*
 * The header is an SButton, and a touch is a click of one (SButton.cpp:354, :410): a tap on the header opens the area and
 * says so, and a second tap closes it.
 */
bool FDreamExpandableAreaTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get(), 0.0f);
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Tapping the header completes"), Placed.Header->Tap());
	TestTrue(TEXT("A tap on the header opens the area"), Placed.Area->GetIsExpanded());
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping the header again completes"), Placed.Header->Tap());
	TestFalse(TEXT("A second tap closes it"), Placed.Area->GetIsExpanded());
	if (TestEqual(TEXT("Each tap announced the way it went"), Listener->ExpansionStates.Num(), 2))
	{
		TestTrue(TEXT("Open first"), Listener->ExpansionStates[0]);
		TestFalse(TEXT("Then closed"), Listener->ExpansionStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaClickWhileOpeningTest,
	"DreamGUI.ExpandableArea.ClickingTheHeaderWhileTheAreaOpensTurnsItRoundToClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaClickWhileOpeningTest, "DreamGUI.ExpandableArea.ClickingTheHeaderWhileTheAreaOpensTurnsItRoundToClosed", "[Pointer][Animated]")

/*
 * The header clicked, and clicked again while the area is still opening. SExpandableArea::SetExpanded_Animated flips on
 * each click and announces each (:114-134): opened, then closed. The click lands on the header, which has not moved, and the
 * area ends at its header's height with the body asleep, in no longer than an open takes.
 */
bool FDreamExpandableAreaClickWhileOpeningTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get(), ExpansionSeconds);
	if (!Placed.IsReady())
	{
		return false;
	}
	const float CollapsedHeight = Placed.Area->GetHeight();
	const TOptional<FBox2D> HeaderBefore = Placed.Header->GetPixelRect();

	TestTrue(TEXT("Clicking the header completes"), Placed.Header->Click());
	TestTrue(TEXT("The click opened the area"), Placed.Area->GetIsExpanded());
	TestTrue(TEXT("Two frames pass"), Rig.Driver()->Sequence().WaitFrames(2).Perform());
	const float Partway = Placed.Area->GetHeight();
	if (!TestTrue(FString::Printf(TEXT("The area is partway open (%.1f, collapsed %.1f)"), Partway, CollapsedHeight), Partway > CollapsedHeight + 1.0f))
	{
		return false;
	}
	const TOptional<FBox2D> HeaderPartway = Placed.Header->GetPixelRect();
	TestTrue(TEXT("The header has not moved while the body opens below it"),
		HeaderBefore.IsSet() && HeaderPartway.IsSet() && FMath::Abs(HeaderBefore->Min.Y - HeaderPartway->Min.Y) <= 1.0);

	TestTrue(TEXT("Clicking the header again while the area opens completes"), Placed.Header->Click());
	TestFalse(TEXT("The second click turned the area round to closed"), Placed.Area->GetIsExpanded());
	if (TestEqual(TEXT("Each click announced the way it went"), Listener->ExpansionStates.Num(), 2))
	{
		TestTrue(TEXT("Open first"), Listener->ExpansionStates[0]);
		TestFalse(TEXT("Then closed"), Listener->ExpansionStates[1]);
	}
	TestTrue(TEXT("An open's length passes"), Rig.Driver()->Sequence().WaitSeconds(ExpansionSeconds).Perform());
	TestNearlyEqual(TEXT("The area is back at its header's height"), Placed.Area->GetHeight(), CollapsedHeight, 1.0f);
	TestFalse(TEXT("...with the body asleep"), Placed.Body->GetWidgetActiveInHierarchy());
	TestEqual(TEXT("...and nothing more announced"), Listener->ExpansionStates.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaDisabledWhileHeldTest,
	"DreamGUI.ExpandableArea.AnAreaDisabledWhileItsHeaderIsHeldDoesNotFlipWhenLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaDisabledWhileHeldTest, "DreamGUI.ExpandableArea.AnAreaDisabledWhileItsHeaderIsHeldDoesNotFlipWhenLetGo", "[Pointer][Disabled]")

/*
 * The header pressed, the area disabled by the game, the header let go. The header is an SButton, and SButton clicks on the
 * release only if it is enabled (SButton.cpp:408-416): the area does not open and says nothing. Enabled again, a click opens
 * it.
 */
bool FDreamExpandableAreaDisabledWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaMidGestureTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get(), 0.0f);
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing on the header completes"), Placed.Header->Press());
	Placed.Area->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Letting go over the header completes"), Placed.Header->Release());
	TestFalse(TEXT("An area disabled while its header was held stays closed"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...and announces nothing"), Listener->ExpansionStates.Num(), 0);

	Placed.Area->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the header with the area enabled again completes"), Placed.Header->Click());
	TestTrue(TEXT("Enabled again, a click opens it"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...and says so once"), Listener->ExpansionStates.Num(), 1);
	return true;
}

#endif
