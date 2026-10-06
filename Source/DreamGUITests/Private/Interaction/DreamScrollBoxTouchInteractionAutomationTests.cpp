// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BOX UNDER A FINGER.
 *
 * SScrollBox pans under a finger (OnTouchMoved, once the finger has travelled the drag trigger distance: every move scrolls by
 * its own delta, so the content stays under the finger), and a finger lifted from a pan leaves the content coasting the way it
 * was going (OnTouchEnded calls BeginInertialScrolling; Slate/Private/Widgets/Layout/SScrollBox.cpp). Both are judged by the
 * box's offset and by OnUserScrolled, the event UMG raises for scrolling the user did.
 *
 * Twenty rows of 100 in a 300-by-400 window: a finger moving UP the screen pulls the content up, which is scrolling DOWN
 * through it -- the offset grows.
 */
namespace DreamScrollBoxTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamScrollBox* MakeFilledBox(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		// Begun play, as a game's UI has before anything is touched: a scroll view's inertia is its Tick.
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable())
			|| !InTest.TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(InRig.GetWorld())))
		{
			return nullptr;
		}
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
		if (!InTest.TestTrue(TEXT("The box came up with a viewport and a content node"),
			Box != nullptr && Box->ViewportNode != nullptr && Box->GetContentNode() != nullptr))
		{
			return nullptr;
		}
		for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
		{
			InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	double DragThresholdPixels(const FDreamDriverRig& InRig)
	{
		return FMath::Sqrt(static_cast<double>(InRig.Raycaster()->GetScaledDragThresholdSquare()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxFingerPanTest,
	"DreamGUI.ScrollBox.AFingerDraggedUpTheBoxPullsTheContentUpWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxFingerPanTest, "DreamGUI.ScrollBox.AFingerDraggedUpTheBoxPullsTheContentUpWithIt", "[Touch][Animated]")

/*
 * A finger lands in the box, crosses the drag distance and goes on up by sixty, then rests a while before lifting -- a pan
 * that is set down, not flung. The content moved with the finger past the drag distance: the offset grew by about the
 * distance travelled after the pan began, and the user's scrolling was announced.
 */
bool FDreamScrollBoxFingerPanTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamScrollBox* Box = MakeFilledBox(*this, Rig);
	if (Box == nullptr)
	{
		return false;
	}
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The box is on the viewport"), Centre.IsSet()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> UserScrolled(NewObject<UDreamDragInteractionProbe>());
	Box->OnUserScrolled.AddDynamic(UserScrolled.Get(), &UDreamDragInteractionProbe::RecordFloat);
	const double FirstMove = DragThresholdPixels(Rig) + 2.0;
	const FVector2D Landed = Centre.GetValue() + FVector2D(0.0, 100.0);

	TestTrue(TEXT("The finger's pan completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + 30.0))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + 60.0))
			.Perform());
	const float WhileHeld = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("With the finger still down, the content has followed it (offset %.1f)"), WhileHeld),
		WhileHeld >= 55.0f && WhileHeld <= FirstMove + 62.0);
	TestTrue(TEXT("...and the user's scrolling was announced"), UserScrolled->NumFloats() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxFingerFlingTest,
	"DreamGUI.ScrollBox.AFingerLiftedWhileItPansLeavesTheContentCoastingTheSameWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBoxFingerFlingTest, "DreamGUI.ScrollBox.AFingerLiftedWhileItPansLeavesTheContentCoastingTheSameWay", "[Touch][Animated]")

/*
 * The pan of the test above, the finger lifting the frame after its last move -- the way a finger flicks. SScrollBox::
 * OnTouchEnded begins inertial scrolling, so the box is still scrolling after the lift and the content goes on the way it was
 * pulled, slowing; nothing pulls it back the other way.
 */
bool FDreamScrollBoxFingerFlingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamScrollBox* Box = MakeFilledBox(*this, Rig);
	if (Box == nullptr)
	{
		return false;
	}
	const TOptional<FVector2D> Centre = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The box is on the viewport"), Centre.IsSet()))
	{
		return false;
	}
	const double FirstMove = DragThresholdPixels(Rig) + 2.0;
	const FVector2D Landed = Centre.GetValue() + FVector2D(0.0, 100.0);

	TestTrue(TEXT("The finger's flick completes"),
		Rig.Driver()->Sequence()
			.TouchDown(0, Landed)
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + 40.0))
			.TouchMoveTo(0, Landed - FVector2D(0.0, FirstMove + 80.0))
			.TouchUp(0)
			.Perform());

	const float AtLift = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The flick scrolled down through the content (offset %.1f)"), AtLift), AtLift > 0.0f);
	TestTrue(TEXT("The box is still scrolling after the finger lifted"), Box->GetIsScrolling());
	Rig.PumpFrames(1);
	const float OneFrameLater = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The content coasts on the way it was pulled (%.2f then %.2f)"), AtLift, OneFrameLater),
		OneFrameLater > AtLift + 0.5f);
	Rig.PumpFrames(5);
	TestTrue(FString::Printf(TEXT("...and keeps going that way (%.2f then %.2f)"), OneFrameLater, Box->GetScrollOffset()),
		Box->GetScrollOffset() >= OneFrameLater);
	return true;
}

#endif
