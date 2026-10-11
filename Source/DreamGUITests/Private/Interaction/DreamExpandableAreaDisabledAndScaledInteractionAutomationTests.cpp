// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamExpandableArea.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamExpandableArea disabled, and on a canvas scaled to half and to double.
 *
 * UExpandableArea is SExpandableArea: its header is a button whose click flips the area (OnHeaderClicked ->
 * SetExpanded_Animated, Slate/Private/Widgets/Layout/SExpandableArea.cpp:114-134, 165-176) and its body has no click of its
 * own. Disabled, the header is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241)
 * and passed over by navigation (FindFocusableWidget, HittestGrid.cpp:471, which also asks every parent: IsParentsEnabled,
 * :336). On a scaled layer the header is hit and focused where it is painted, and the body beneath it is not the header.
 *
 * The area is pinned by its top edge so the header stays where it is while the body opens and closes beneath it, and opens
 * and closes at once (the control's default), so each gesture's result is there when the gesture is.
 */
namespace DreamExpandableAreaConfigTestLocal
{
	const FVector2D AreaSize(300.0, 200.0);

	struct FPlacedArea
	{
		UDreamExpandableArea* Area = nullptr;
		UDreamWidget* Body = nullptr;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;

		bool IsReady() const { return Area != nullptr && Area->HeaderNode != nullptr && Body != nullptr; }
	};

	/** An area pinned by its top edge at InPosition (canvas units, Y up), expanded, with a body 100 tall, its flips counted. */
	FPlacedArea PlaceArea(FDreamDriverRig& InRig, const FVector2D& InPosition)
	{
		FPlacedArea Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		UDreamExpandableArea* Area = InRig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, AreaSize, InPosition);
		if (Area == nullptr || Area->HeaderNode == nullptr || Area->ContentNode == nullptr)
		{
			return Placed;
		}
		// Pinned by its top edge: Y is up, so a pivot of one is the top.
		Area->SetPivot(FVector2D(0.5, 1.0));
		Placed.Body = InRig.MakeWidget(TEXT("Body"), Area->ContentNode.Get(), FVector2D(260.0, 100.0));
		Area->SetExpansionDuration(0.0f);
		Area->SetIsExpanded(true);
		Area->OnExpansionChanged.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleExpansionChanged);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		Placed.Area = Area;
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaDisabledTapTest,
	"DreamGUI.ExpandableArea.ATapOnTheHeaderOfADisabledAreaDoesNotFlipItUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaDisabledTapTest, "DreamGUI.ExpandableArea.ATapOnTheHeaderOfADisabledAreaDoesNotFlipItUntilItIsEnabledAgain", "[Touch][Disabled]")

/*
 * The area disabled with UMG's SetIsEnabled: a tap on its header reaches nothing, so the area stays open and says nothing.
 * Enabled again, the same tap closes it and says so once.
 */
bool FDreamExpandableAreaDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedArea Placed = PlaceArea(Rig, FVector2D(0.0, 200.0));
	if (!TestTrue(TEXT("An expandable area with a header and a body can be made"), Placed.IsReady()))
	{
		return false;
	}
	Placed.Area->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Header = Rig.Driver()->Find(FDreamBy::Widget(Placed.Area->HeaderNode.Get()));

	TestTrue(TEXT("Tapping the disabled area's header completes"), Header->Tap());
	TestTrue(TEXT("The area is still open"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...and said nothing"), Placed.Listener->ExpansionStates.Num(), 0);

	Placed.Area->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Tapping the header enabled again completes"), Header->Tap());
	TestFalse(TEXT("Enabled again, a tap closes the area"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...saying so once"), Placed.Listener->ExpansionStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaDisabledPadTest,
	"DreamGUI.ExpandableArea.TheDPadPassesOverTheHeaderOfADisabledAreaAndOnceEnabledAcceptFlipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaDisabledPadTest, "DreamGUI.ExpandableArea.TheDPadPassesOverTheHeaderOfADisabledAreaAndOnceEnabledAcceptFlipsIt", "[Nav][Disabled]")

/*
 * A button left of the area's header and one right of it, all on one line, the focus on the left button. With the area
 * disabled, D-pad right passes over its header to the right-hand button, and the pad's Accept presses that button and leaves
 * the area alone. Enabled again, D-pad right from the left button stops on the header, and Accept closes the area.
 */
bool FDreamExpandableAreaDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedArea Placed = PlaceArea(Rig, FVector2D(0.0, 200.0));
	if (!TestTrue(TEXT("An expandable area with a header and a body can be made"), Placed.IsReady()))
	{
		return false;
	}
	const TOptional<FBox2D> HeaderRect = DrawnRect(Placed.Area->HeaderNode.Get());
	if (!TestTrue(TEXT("The header is on the viewport"), HeaderRect.IsSet()))
	{
		return false;
	}
	// Level with the header, in canvas units with Y up: the viewport's middle is 360 pixels down, one unit to the pixel.
	const double HeaderY = 360.0 - HeaderRect->GetCenter().Y;
	UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, FVector2D(120.0, 40.0), FVector2D(-330.0, HeaderY));
	UDreamButton* Right = Rig.MakeControl<UDreamButton>(TEXT("Right"), nullptr, FVector2D(120.0, 40.0), FVector2D(330.0, HeaderY));
	if (!TestTrue(TEXT("Buttons either side of the header can be made"), Left != nullptr && Right != nullptr
		&& Left->FaceNode != nullptr && Right->FaceNode != nullptr))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> RightClicks(NewObject<UDreamPressInteractionListener>());
	Right->OnClicked.AddDynamic(RightClicks.Get(), &UDreamPressInteractionListener::HandleClicked);
	Placed.Area->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The focus goes onto the left button"), PutFocusOn(Rig, Left->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("D-pad right passes over the disabled area's header to the right-hand button"), FocusIsIn(Rig, Right));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept pressed the right-hand button"), RightClicks->ClickedCount, 1);
	TestTrue(TEXT("...and the area is still open"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...having said nothing"), Placed.Listener->ExpansionStates.Num(), 0);

	Placed.Area->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The focus goes back onto the left button"), PutFocusOn(Rig, Left->FaceNode.Get()));
	TestTrue(TEXT("D-pad right completes once the area is enabled"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("Enabled again, the header is the next stop"), FocusIsIn(Rig, Placed.Area->HeaderNode.Get()));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestFalse(TEXT("...and closes the area"), Placed.Area->GetIsExpanded());
	TestEqual(TEXT("...saying so once"), Placed.Listener->ExpansionStates.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaScaledHitTest,
	"DreamGUI.ExpandableArea.OnACanvasScaledToHalfAndToDoubleAClickAndATapFlipTheAreaOnlyOnTheHeaderAsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaScaledHitTest, "DreamGUI.ExpandableArea.OnACanvasScaledToHalfAndToDoubleAClickAndATapFlipTheAreaOnlyOnTheHeaderAsDrawn", "[Pointer][Touch][Scaled]")

/*
 * An open area off the middle of the screen, at each scale. A click four pixels below the header's drawn bottom edge is in
 * the body and flips nothing; a click three pixels inside the header's bottom-right corner closes the area. Then, closed, a
 * tap four pixels past the header's right edge flips nothing and a tap three pixels inside its top-left corner opens it.
 */
bool FDreamExpandableAreaScaledHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedArea Placed = PlaceArea(Rig, FVector2D(-120.0, 150.0));
		if (!TestTrue(At(Scale, TEXT("An expandable area with a header and a body can be made")), Placed.IsReady()))
		{
			return false;
		}
		const TOptional<FBox2D> Header = DrawnRect(Placed.Area->HeaderNode.Get());
		const TOptional<FBox2D> Body = DrawnRect(Placed.Body);
		if (!TestTrue(At(Scale, TEXT("The header and the body are on the viewport")), Header.IsSet() && Body.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, FString::Printf(TEXT("The header is drawn at the canvas's scale (%s)"), *Describe(Header.GetValue()))),
			IsDrawnAtScale(Header.GetValue(), Placed.Area->HeaderNode->GetSize(), Scale));
		TestTrue(At(Scale, FString::Printf(TEXT("...with the body drawn below it (%s)"), *Describe(Body.GetValue()))),
			Body->Min.Y >= Header->Max.Y - 1.5);
		const TArray<bool>& Heard = Placed.Listener->ExpansionStates;

		TestTrue(At(Scale, TEXT("A click just below the header's drawn bottom edge completes")),
			ClickAt(Rig, FVector2D(Header->GetCenter().X, Header->Max.Y + 4.0)));
		TestTrue(At(Scale, TEXT("A click in the body leaves the area open")), Placed.Area->GetIsExpanded());
		TestTrue(At(Scale, TEXT("A click just inside the header's bottom-right corner completes")),
			ClickAt(Rig, InsideBottomRight(Header.GetValue(), 3.0)));
		TestFalse(At(Scale, TEXT("It closes the area")), Placed.Area->GetIsExpanded());
		TestEqual(*At(Scale, TEXT("...said once")), Heard.Num(), 1);
		Rig.PumpFrames(1);

		const TOptional<FBox2D> ClosedHeader = DrawnRect(Placed.Area->HeaderNode.Get());
		if (!TestTrue(At(Scale, TEXT("The closed area's header is on the viewport")), ClosedHeader.IsSet()))
		{
			return false;
		}
		const FBox2D Outer = Covering(ClosedHeader.GetValue(), DrawnRect(Placed.Area));
		TestTrue(At(Scale, TEXT("A tap just past the header's right edge completes")), TapAt(Rig, PastRightEdge(Outer, 4.0)));
		TestFalse(At(Scale, TEXT("A tap past the drawn edge leaves it closed")), Placed.Area->GetIsExpanded());
		TestTrue(At(Scale, TEXT("A tap just inside the header's top-left corner completes")), TapAt(Rig, InsideTopLeft(ClosedHeader.GetValue(), 3.0)));
		TestTrue(At(Scale, TEXT("It opens the area")), Placed.Area->GetIsExpanded());
		TestEqual(*At(Scale, TEXT("...said once more")), Heard.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaScaledPadTest,
	"DreamGUI.ExpandableArea.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheHeaderWithTheRingOnItAndAcceptFlipsTheArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamExpandableAreaScaledPadTest, "DreamGUI.ExpandableArea.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheHeaderWithTheRingOnItAndAcceptFlipsTheArea", "[Nav][Scaled]")

/*
 * A button level with the area's header, to its left, focused. D-pad right takes the focus onto the header, the ring sits on
 * the header where it is drawn, and the pad's Accept closes the area.
 */
bool FDreamExpandableAreaScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamExpandableAreaConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedArea Placed = PlaceArea(Rig, FVector2D(80.0, 150.0));
		if (!TestTrue(At(Scale, TEXT("An expandable area with a header and a body can be made")), Placed.IsReady()))
		{
			return false;
		}
		const TOptional<FBox2D> Header = DrawnRect(Placed.Area->HeaderNode.Get());
		if (!TestTrue(At(Scale, TEXT("The header is on the viewport")), Header.IsSet()))
		{
			return false;
		}
		// Level with the header, in canvas units with Y up.
		const double HeaderY = (360.0 - Header->GetCenter().Y) / Scale;
		UDreamButton* Left = Rig.MakeControl<UDreamButton>(TEXT("Left"), nullptr, FVector2D(100.0, 40.0), FVector2D(-200.0, HeaderY));
		if (!TestTrue(At(Scale, TEXT("A button left of the header can be made")), Left != nullptr && Left->FaceNode != nullptr))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!TestTrue(At(Scale, TEXT("The focus goes onto the button")), PutFocusOn(Rig, Left->FaceNode.Get())))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("D-pad right completes")), PressKey(Rig, EKeys::Gamepad_DPad_Right));
		TestTrue(At(Scale, TEXT("The D-pad took the focus onto the header")), FocusIsIn(Rig, Placed.Area->HeaderNode.Get()));
		FString Where;
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the header where it is drawn (%s)"), *Where)), bRingSits);

		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestFalse(At(Scale, TEXT("Accept closed the area")), Placed.Area->GetIsExpanded());
		TestEqual(*At(Scale, TEXT("...said once")), Placed.Listener->ExpansionStates.Num(), 1);
	}
	return true;
}

#endif
