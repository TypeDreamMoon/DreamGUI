// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRingMenu.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRingMenu chosen from with a finger and with the pad's stick, opened with its own open animation and waited out.
 *
 * UMG has no ring menu, so the rules are this control's header's: a wedge is a button over its slice (its hit shape is the
 * slice), and choosing a wedge activates its item. A finger's tap is a click of a button (SButton.cpp:354, :410). The pad
 * reaches a wedge through the virtual cursor -- the stick moves the cursor, the confirm button clicks under it -- which is how
 * a pointer-shaped control is driven from a pad when it has no navigation of its own.
 *
 * The geometry is the wedge tests' (DreamRingMenuInteractionAutomationTests.cpp): angles clockwise from twelve about the
 * centre of a wedge's own rect, a point at angle A and radius R being (R sin A, R cos A) in its local space, Y up. The only
 * thing the style is told is to stay still under the pointer (no highlight growth); the open animation is the style's own and
 * is waited out.
 */
namespace DreamRingMenuTouchPadTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const float OuterRadius = 200.0f;
	const float InnerRadius = 80.0f;

	FDreamRingMenuItem MakeItem(const TCHAR* InLabel)
	{
		FDreamRingMenuItem Item;
		Item.Label = FText::AsCultureInvariant(InLabel);
		Item.Tag = FName(InLabel);
		return Item;
	}

	/** A four-item wheel on the rig, opened and its open animation over, its activations going to InListener. */
	UDreamRingMenu* PlaceWheel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return nullptr;
		}
		UDreamRingMenu* Wheel = InRig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), nullptr, FVector2D(OuterRadius * 2.0f, OuterRadius * 2.0f));
		if (!InTest.TestNotNull(TEXT("A ring menu can be made on the rig"), Wheel))
		{
			return nullptr;
		}
		Wheel->StyleSource = EDreamUIStyleSource::Inline;
		Wheel->Style.OuterRadius = OuterRadius;
		Wheel->Style.InnerRadius = InnerRadius;
		Wheel->Style.StartAngle = 0.0f;
		Wheel->Style.SweepAngle = 360.0f;
		Wheel->Style.HighlightGrowth = 0.0f;
		Wheel->ApplyStyle();
		Wheel->SetItems({ MakeItem(TEXT("Reload")), MakeItem(TEXT("Grenade")), MakeItem(TEXT("Heal")), MakeItem(TEXT("Melee")) });
		Wheel->OnItemActivated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemActivated);
		Wheel->Open();
		// The style's own open time, and two frames for the frame it started in and the one that settles the layout.
		const float OpenSeconds = Wheel->Style.OpenDuration;
		InTest.TestTrue(TEXT("Waiting out the wheel's open animation completes"),
			InRig.Driver()->Sequence().WaitSeconds(OpenSeconds).WaitFrames(2).Perform());

		if (!InTest.TestEqual(TEXT("The wheel has a wedge per item"), Wheel->WedgeNodes.Num(), 4)
			|| !InTest.TestTrue(TEXT("And is open"), Wheel->IsOpen()))
		{
			return nullptr;
		}
		return Wheel;
	}

	/** The pixel at InAngleDegrees (clockwise from twelve) and InRadius from the ring's centre, in InWedge's frame. */
	TOptional<FVector2D> RingPixel(const UDreamWidget* InWedge, float InAngleDegrees, float InRadius)
	{
		if (InWedge == nullptr)
		{
			return TOptional<FVector2D>();
		}
		const FVector2D RectCentre(
			(InWedge->GetLocalSpaceLeft() + InWedge->GetLocalSpaceRight()) * 0.5,
			(InWedge->GetLocalSpaceBottom() + InWedge->GetLocalSpaceTop()) * 0.5);
		const double Radians = FMath::DegreesToRadians(static_cast<double>(InAngleDegrees));
		const FVector2D Offset(InRadius * FMath::Sin(Radians), InRadius * FMath::Cos(Radians));
		return FDreamDriverProjection::WidgetLocalPointToPixel(InWedge, RectCentre + Offset);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuTapTest,
	"DreamGUI.RingMenu.ATapInTheMiddleOfAWedgeActivatesThatWedgesItemOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuTapTest, "DreamGUI.RingMenu.ATapInTheMiddleOfAWedgeActivatesThatWedgesItemOnce", "[Touch][Animated]")

bool FDreamRingMenuTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTouchPadTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Listener.Get());
	if (Wheel == nullptr)
	{
		return false;
	}
	const int32 Target = 2;
	const TOptional<FVector2D> Pixel = RingPixel(Wheel->GetWedgeWidget(Target), Wheel->GetItemMidAngle(Target), (InnerRadius + OuterRadius) * 0.5f);
	if (!TestTrue(TEXT("The middle of the third wedge is a pixel a finger can reach"), Pixel.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the middle of the third wedge completes"),
		Rig.Driver()->Sequence().TouchDown(0, Pixel.GetValue()).TouchUp(0).Perform());

	if (TestEqual(TEXT("One item was activated"), Listener->ActivatedIndices.Num(), 1))
	{
		TestEqual(TEXT("...the third"), Listener->ActivatedIndices[0], Target);
		TestEqual(TEXT("...carrying its tag"), Listener->ActivatedTags[0], FName(TEXT("Heal")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuVirtualCursorTest,
	"DreamGUI.RingMenu.TheStickPushedIntoAWedgeAndThePadsConfirmActivateThatWedgesItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuVirtualCursorTest, "DreamGUI.RingMenu.TheStickPushedIntoAWedgeAndThePadsConfirmActivateThatWedgesItem", "[Nav][Animated]")

/*
 * The virtual cursor starts on the hub; the stick is held toward the middle of the second wedge (lower right) for as long as
 * the cursor's speed takes to carry it into the band between the hub and the rim; the confirm button clicks there. The cursor
 * goes the way the stick points, so it is in that wedge's slice, and the wedge's item is activated -- once.
 */
bool FDreamRingMenuVirtualCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuTouchPadTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Listener.Get());
	if (Wheel == nullptr)
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller, so the rig needs one with its input up.
	Rig.EnsureGameInputHost();
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Hub = Rig.Driver()->Find(FDreamBy::Widget(Wheel))->GetCentrePixel();
	if (!TestTrue(TEXT("The wheel's centre is on the viewport"), Hub.IsSet()))
	{
		return false;
	}
	const int32 Target = 1;
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Wheel->GetItemMidAngle(Target)));
	// The stick's frame is X right, Y up -- the ring's own.
	const FVector2D Stick(FMath::Sin(Radians), FMath::Cos(Radians));
	// Aimed past the middle of the band: the cursor covers most of speed times time, less what the dead zone and the last
	// partial frame take off it.
	const float AimRadius = (InnerRadius + OuterRadius) * 0.5f + 30.0f;
	const float PushSeconds = AimRadius / UDreamGUISettings::Get()->VirtualCursorSpeed;

	TestTrue(TEXT("Taking the cursor from the hub and holding the stick toward the wedge completes"),
		Rig.Driver()->Sequence()
			.MoveToPixel(Hub.GetValue())
			.ActivateVirtualCursor()
			.VirtualCursorStick(Stick, PushSeconds)
			.Perform());
	UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The world has a virtual cursor"), Cursor))
	{
		return false;
	}
	const FVector2D FromHub = Cursor->GetVirtualCursorPosition() - Hub.GetValue();
	const double Radius = FromHub.Size();
	if (!TestTrue(FString::Printf(TEXT("The cursor is in the band between the hub and the rim (%.1f pixels out)"), Radius),
		Radius > InnerRadius + 4.0 && Radius < OuterRadius - 4.0))
	{
		return false;
	}

	TestTrue(TEXT("Pressing and releasing the cursor's confirm completes"),
		Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
	if (TestEqual(TEXT("One item was activated"), Listener->ActivatedIndices.Num(), 1))
	{
		TestEqual(TEXT("...the one whose wedge the stick pointed into"), Listener->ActivatedIndices[0], Target);
	}
	return true;
}

#endif
