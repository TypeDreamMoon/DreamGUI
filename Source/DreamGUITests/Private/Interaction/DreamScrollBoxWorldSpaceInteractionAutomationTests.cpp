// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamWidget.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BOX ON A PANEL IN THE LEVEL, UNDER A FINGER AND THE PAD.
 *
 * SScrollBox scrolls under a finger that has travelled the drag distance (OnMouseMove, which a touch reaches: every move
 * scrolls by its own delta over the geometry's scale -- in the box's own units, so the content stays under the finger;
 * Slate/Private/Widgets/Layout/SScrollBox.cpp:1040-1045), and the pad walking down a column of buttons scrolls each one it
 * reaches into the box's window (SScrollBox::OnNavigation, ScrollDescendantIntoView). In a UWidgetComponent the finger is
 * where its pointer index's ray lands on the component's window, so its pan scrolls by its travel across the panel in the
 * panel's units; and the window the pad keeps the focus inside is the one standing in the level.
 */
namespace DreamScrollBoxWorldTestLocal
{
	/** Pumped until the box has come to rest, for as long as its own glide takes. */
	bool WaitUntilStill(FDreamDriverRig& InRig, const UDreamScrollBox* InBox)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(5.0);
		return InRig.Driver()->Wait(FDreamUntil::Condition([InBox]() { return !InBox->GetIsScrolling(); }, Timeout),
			Timeout, TEXT("the scroll box comes to rest"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxWorldFingerTest,
	"DreamGUI.ScrollBox.OnAWorldPanelAFingersPanScrollsTheContentByItsTravelInThePanelsUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxWorldFingerTest, "DreamGUI.ScrollBox.OnAWorldPanelAFingersPanScrollsTheContentByItsTravelInThePanelsUnits", "[Touch][World]")

/*
 * Twenty rows 100 units tall in a 240-unit window on a panel three metres ahead. A finger lands near the window's bottom as the
 * camera sees it, crosses the world pointer's drag distance and goes on 150 pixels up: while it is still down the content has
 * scrolled by those 150 pixels in the panel's units -- about seventy -- give or take the stretch that only decided this was a
 * drag, and the user's scrolling was announced.
 */
bool FDreamScrollBoxWorldFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	// A scroll view's inertia is its Tick: begun play, as a game's UI has.
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), Stage.Panel, FVector2D(300.0, 240.0), FVector2D(-60.0, 0.0));
	if (!TestTrue(TEXT("A box with a viewport came up on the panel"), Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	const TOptional<FBox2D> Window = SeenRect(Rig, Box->ViewportNode.Get());
	if (!TestTrue(TEXT("The camera sees the box's window"), Window.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The window is seen at its height in panel units"), Window->Max.Y - Window->Min.Y,
		static_cast<double>(Box->ViewportNode->GetHeight()) * PixelsPerUnit(), 1.5);
	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);
	const double FirstMove = DreamControlsWorldTest::PastDragThreshold(Stage.Pointer);
	const double Travel = 150.0;
	const FVector2D Landed(Window->GetCenter().X, Window->Max.Y - 30.0);

	TestTrue(TEXT("The finger's pan completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel * 0.5))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + Travel))
			.Perform());
	const double WhileHeld = Box->GetScrollOffset();
	const double Least = (Travel - 6.0) / PixelsPerUnit();
	const double Most = (FirstMove + Travel + 2.0) / PixelsPerUnit();
	TestTrue(FString::Printf(TEXT("With the finger still down the content followed it by its travel in the panel's units: between %.1f and %.1f (it is %.1f)"),
		Least, Most, WhileHeld), WhileHeld >= Least && WhileHeld <= Most);
	TestTrue(TEXT("...and the user's scrolling was announced"), UserScrolled->NumFloats() >= 1);
	TestTrue(TEXT("The finger lifts"), Rig.Driver()->Sequence().TouchUp(0).Perform());
	WaitUntilStill(Rig, Box);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxWorldPadTest,
	"DreamGUI.ScrollBox.OnAWorldPanelTheDPadScrollsEachButtonItReachesIntoTheWindowAsSeenWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxWorldPadTest, "DreamGUI.ScrollBox.OnAWorldPanelTheDPadScrollsEachButtonItReachesIntoTheWindowAsSeenWithTheRingOnIt", "[Nav][World]")

/*
 * Thirty buttons in a box 200 units tall on the panel, the focus on the first. How many the window shows is the layout's to
 * say, so the D-pad goes down until it has reached two buttons past the first one seen below the window. After every press the
 * focus is on the next button and that button lies inside the box's window as the camera sees it, the box having scrolled to
 * show it once it was below; and the ring sits on the last one where the camera sees it.
 */
bool FDreamScrollBoxWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), Stage.Panel, FVector2D(320.0, 200.0), FVector2D(40.0, 0.0));
	if (!TestTrue(TEXT("A box with a viewport came up on the panel"), Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	TArray<UDreamButton*> Buttons;
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Buttons.Add(Rig.MakeControl<UDreamButton>(FString::Printf(TEXT("Row%02d"), Index), Box->GetContentNode(), FVector2D(300.0, 60.0)));
	}
	if (!TestFalse(TEXT("Thirty buttons were put in the box"), Buttons.Contains(nullptr)))
	{
		return false;
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	const TOptional<FBox2D> Window = SeenRect(Rig, Box->ViewportNode.Get());
	if (!TestTrue(TEXT("The camera sees the box's window"), Window.IsSet())
		|| !TestTrue(TEXT("The focus goes onto the first button"), PutFocusOn(Rig, Buttons[0]->FaceNode.Get())))
	{
		return false;
	}

	// The first button the window does not show all of, as the box laid the column out.
	int32 FirstBelow = INDEX_NONE;
	for (int32 Index = 0; Index < Buttons.Num() && FirstBelow == INDEX_NONE; ++Index)
	{
		const TOptional<FBox2D> Row = SeenRect(Rig, Buttons[Index]);
		if (Row.IsSet() && Row->Max.Y > Window->Max.Y + 1.5)
		{
			FirstBelow = Index;
		}
	}
	AddInfo(FString::Printf(TEXT("The window is seen at %s; the first button seen below it is %d"), *Describe(Window.GetValue()), FirstBelow));
	if (!TestTrue(TEXT("The column runs on below the window"), FirstBelow > 0 && FirstBelow + 2 < Buttons.Num()))
	{
		return false;
	}

	const int32 Presses = FirstBelow + 2;
	for (int32 Press = 1; Press <= Presses; ++Press)
	{
		TestTrue(FString::Printf(TEXT("D-pad down %d completes"), Press), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		// The scroll into view may glide; the focused button is judged once the box has come to rest.
		WaitUntilStill(Rig, Box);
		const TOptional<FBox2D> Row = SeenRect(Rig, Buttons[Press]);
		const bool bFocused = FocusIsIn(Rig, Buttons[Press]);
		const bool bInside = Row.IsSet() && Row->Min.Y >= Window->Min.Y - 1.5 && Row->Max.Y <= Window->Max.Y + 1.5;
		if (!TestTrue(FString::Printf(TEXT("After D-pad down %d the focus is on button %d, seen inside the window %s (it is at %s)"),
			Press, Press, *Describe(Window.GetValue()), Row.IsSet() ? *Describe(Row.GetValue()) : TEXT("nowhere")), bFocused && bInside))
		{
			return false;
		}
	}
	TestTrue(FString::Printf(TEXT("The box scrolled to show the buttons the pad went to (offset %.1f)"), Box->GetScrollOffset()),
		Box->GetScrollOffset() > 1.0f);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the last button where the camera sees it (%s)"), *Where), bRingSits);
	return true;
}

#endif
