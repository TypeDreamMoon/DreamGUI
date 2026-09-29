// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamExpandableArea.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamExpandableArea, toggled through the real pointer pipeline and held to UMG's UExpandableArea.
 *
 * SExpandableArea's header is a button whose OnClicked flips the area and reports the new state
 * (UExpandableArea::SlateExpansionChanged broadcasts OnExpansionChanged with it); the body is a
 * separate widget with no click behaviour at all. So a click on the header flips it and says which
 * way, and a click on the body flips nothing.
 */
namespace DreamPressExpandableAreaTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FPlacedArea
	{
		UDreamExpandableArea* Area = nullptr;
		/** Something in the body, sized, so there is body to click on. */
		UDreamWidget* Body = nullptr;

		bool IsReady() const { return Area != nullptr && Area->HeaderNode != nullptr && Body != nullptr; }
	};

	FPlacedArea PlaceArea(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FPlacedArea Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamExpandableArea* Area = InRig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0));
		if (!InTest.TestNotNull(TEXT("An expandable area can be made on the rig"), Area)
			|| !InTest.TestNotNull(TEXT("It has a content column"), Area->ContentNode.Get()))
		{
			return Placed;
		}
		// Hung under the content column the way a host's nested content arrives there.
		UDreamWidget* Body = InRig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(200.0, 60.0));
		// Starting expanded, which is this control's default, so the body is there to be clicked.
		Area->SetIsExpanded(true);
		Area->OnExpansionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleExpansionChanged);
		InRig.PumpFrames(2);

		Placed.Area = Area;
		Placed.Body = Body;
		InTest.TestTrue(TEXT("The area has a header and a body to click"), Placed.IsReady());
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressExpandableAreaHeaderTest,
	"DreamGUI.ExpandableArea.EachClickOnTheHeaderFlipsTheAreaAndSaysWhichWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressExpandableAreaHeaderTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressExpandableAreaTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	FDreamElementRef Header = Rig.Driver()->Find(FDreamBy::Widget(Placed.Area->HeaderNode.Get()));

	TestTrue(TEXT("Clicking the header completes"), Header->Click());
	TestFalse(TEXT("One click on the header collapses the area"), Placed.Area->GetIsExpanded());
	if (TestEqual(TEXT("And says so once"), Listener->ExpansionStates.Num(), 1))
	{
		TestFalse(TEXT("As collapsed"), Listener->ExpansionStates[0]);
	}
	// Collapsing changes how tall the control is, and so where its header is; let that land before
	// the second click aims at the header again.
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the header again completes"), Header->Click());
	TestTrue(TEXT("A second click expands it again"), Placed.Area->GetIsExpanded());
	if (TestEqual(TEXT("And says so once more"), Listener->ExpansionStates.Num(), 2))
	{
		TestTrue(TEXT("As expanded"), Listener->ExpansionStates[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressExpandableAreaBodyTest,
	"DreamGUI.ExpandableArea.ClickingTheBodyDoesNotFlipTheArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressExpandableAreaBodyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressExpandableAreaTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	FDreamElementRef Body = Rig.Driver()->Find(FDreamBy::Widget(Placed.Body));
	TestTrue(TEXT("Clicking the body completes"), Body->Click());

	TestTrue(TEXT("A click in the body leaves the area expanded"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("And announces nothing"), Listener->ExpansionStates.Num(), 0);
	return true;
}

#endif
