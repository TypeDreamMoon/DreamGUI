// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamExpandableArea.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIButton.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressExpandableAreaHeaderTest, "DreamGUI.ExpandableArea.EachClickOnTheHeaderFlipsTheAreaAndSaysWhichWay", "[Pointer][Animated]")

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
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressExpandableAreaBodyTest, "DreamGUI.ExpandableArea.ClickingTheBodyDoesNotFlipTheArea", "[Pointer][Animated]")

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressExpandableAreaCollapseFocusTest,
	"DreamGUI.ExpandableArea.AfterACollapseThePadsNextConfirmPressesTheHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressExpandableAreaCollapseFocusTest, "DreamGUI.ExpandableArea.AfterACollapseThePadsNextConfirmPressesTheHeader", "[Nav][Animated]")

/*
 * Collapsing puts the body to sleep, and a widget put to sleep gives up the focus it holds -- so a player whose pad focus
 * was on a button inside the section was left with focus nowhere, and the next stick press started from wherever the
 * navigation cursor had last been. Collapsing moves that focus onto the header first now, cursor and all.
 *
 * Checked here: the pad's focus on a button in the body, the area collapsed from code (a binding, say); focus is on the
 * header, and the pad's confirm presses the header -- which opens the section again.
 */
bool FDreamPressExpandableAreaCollapseFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressExpandableAreaTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamWidget* Inner = Rig.MakeWidget(TEXT("Inner"), Placed.Body, FVector2D(120.0, 40.0));
	if (!TestNotNull(TEXT("A button can be put in the body"), Inner) || !TestNotNull(TEXT("with its behaviour"), Inner->AddComponent<UUIButton>()))
	{
		return false;
	}
	Rig.PumpFrames(1);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has input"), Services)
		|| !TestTrue(TEXT("The pad's focus goes onto the button in the body"), Services->FocusForNavigation(Inner, 0)))
	{
		return false;
	}

	Placed.Area->SetIsExpanded(false);
	Rig.PumpFrames(1);
	TestFalse(TEXT("The area collapsed"), Placed.Area->GetIsExpanded());
	TestTrue(TEXT("Focus moved onto the header"), Services->GetFocusedWidget(0) == Placed.Area->HeaderNode.Get());
	TestTrue(TEXT("which is the event system's selection"),
		Rig.EventSystem()->GetCurrentSelectedComponent(0) == Placed.Area->HeaderNode.Get());

	// The cursor went with it: the confirm presses what the cursor is on, and that is the header now.
	TestTrue(TEXT("Pressing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());
	TestTrue(TEXT("and the header it pressed opened the section again"), Placed.Area->GetIsExpanded());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressExpandableAreaTurnRoundTest,
	"DreamGUI.ExpandableArea.ContentThatGrowsWhileTheAreaTurnsRoundNeverShowsTheWholeBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPressExpandableAreaTurnRoundTest, "DreamGUI.ExpandableArea.ContentThatGrowsWhileTheAreaTurnsRoundNeverShowsTheWholeBody", "[Animated]")

/*
 * The area's height and its content's share of it come from two places: the expansion push scales the content by
 * where the open or close has got to, and the handler for content that changes size wrote the whole content whenever
 * the flag said expanded -- so content that grew while a close was turned round into an open showed the entire body
 * for the frame before the next step, the flash Slate avoids by driving both from one curve. Both read one fraction
 * now. And an area authored collapsed started its first animated open from fully open, the class default of the
 * travel, and was open in one frame.
 *
 * Checked here: a body sixty tall, a half-second close turned round a fifth of the way shut, the body grown to two
 * hundred and the column's re-layout telling the area so -- the area stays short of its whole height then and on the
 * next frame, and arrives at it. Then an area set collapsed the way an author does it, opened with a duration: after
 * one frame it is still short of the whole.
 */
bool FDreamPressExpandableAreaTurnRoundTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressExpandableAreaTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedArea Placed = PlaceArea(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	UDreamExpandableArea* Area = Placed.Area;

	// The header's own height, read off the control collapsed in an instant, and the whole of it expanded.
	Area->SetIsExpanded(false);
	Rig.PumpFrames(1);
	const float HeaderHeight = Area->GetHeight();
	Area->SetIsExpanded(true);
	Rig.PumpFrames(1);
	const float ExpandedHeight = Area->GetHeight();
	if (!TestTrue(FString::Printf(TEXT("Expanded, the area is its header and its body (%.1f over %.1f)"), ExpandedHeight, HeaderHeight),
		ExpandedHeight > HeaderHeight + 50.0f))
	{
		return false;
	}

	Area->SetExpansionDuration(0.5f);
	Area->SetIsExpanded(false);
	Rig.PumpFrames(6);
	// Turned round partway shut, and the content grows while it opens again. The notice is the one the column's
	// re-layout sends: the one SetHeight sends itself goes out before the slot has taken the new size in.
	Area->SetIsExpanded(true);
	Placed.Body->SetHeight(200.0f);
	Area->HandleContentDimensionsChanged(Placed.Body, /*bPivotChanged*/false, /*bWidthChanged*/false, /*bHeightChanged*/true);
	const float WholeHeight = ExpandedHeight + (200.0f - 60.0f);
	TestTrue(FString::Printf(TEXT("Growing mid-travel does not show the whole body (%.1f against %.1f)"), Area->GetHeight(), WholeHeight),
		Area->GetHeight() < WholeHeight - 1.0f);
	Rig.PumpFrames(1);
	TestTrue(FString::Printf(TEXT("nor on the frame after (%.1f against %.1f)"), Area->GetHeight(), WholeHeight),
		Area->GetHeight() < WholeHeight - 1.0f);
	Rig.PumpFrames(40);
	TestNearlyEqual(TEXT("and it arrives at the whole of it"), Area->GetHeight(), WholeHeight, 1.0f);

	// Authored collapsed: the flag written the way a .dui line or the details panel writes it, then the style pushed.
	Area->SetExpansionDuration(0.0f);
	Area->bIsExpanded = false;
	Area->ApplyStyle();
	Rig.PumpFrames(1);
	Area->SetExpansionDuration(0.5f);
	Area->SetIsExpanded(true);
	Rig.PumpFrames(1);
	TestTrue(FString::Printf(TEXT("An area authored collapsed opens from shut (%.1f against %.1f)"), Area->GetHeight(), WholeHeight),
		Area->GetHeight() < WholeHeight - 1.0f);
	return true;
}

#endif
