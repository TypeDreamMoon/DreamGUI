// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRingMenu.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIVirtualCursor.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRingMenu on a panel in the level.
 *
 * UMG has no ring menu, so the rules are this control's header's: a wedge is a button over its slice -- with the Ring hit area
 * exactly the drawn annulus, the hub and everything past the rim dead -- and choosing a wedge activates its item. On a panel
 * in the level a button is hit where the pointer's ray lands on it (UWidgetInteractionComponent hands Slate that point), so the
 * annulus is where the camera sees it, its radii in the panel's units. A pad reaches a wedge through the virtual cursor: the
 * stick moves it in viewport pixels, as Slate's analog cursor moves (FAnalogCursor, Slate/Private/Framework/Application/
 * AnalogCursor.cpp:368-373), the world pointer's ray goes out from wherever it is, and the confirm button clicks there.
 *
 * The geometry is the wedge tests' (DreamRingMenuInteractionAutomationTests.cpp): angles clockwise from twelve about the centre
 * of a wedge's own rect, a point at angle A and radius R being (R sin A, R cos A) in its local space, Y up -- up on the face-on
 * panel as on the screen. The style is told only to stay still under the pointer; its open animation is its own and is waited
 * out.
 */
namespace DreamRingMenuWorldTestLocal
{
	const float OuterRadius = 120.0f;
	const float InnerRadius = 50.0f;

	FDreamRingMenuItem MakeItem(const TCHAR* InLabel)
	{
		FDreamRingMenuItem Item;
		Item.Label = FText::AsCultureInvariant(InLabel);
		Item.Tag = FName(InLabel);
		return Item;
	}

	/** A four-item wheel at InPosition on InPanel (panel units, Y up), the drawn annulus its hit area, open and its open animation over. */
	UDreamRingMenu* PlaceWheel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition,
		UDreamPressInteractionListener* InListener)
	{
		UDreamRingMenu* Wheel = InRig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), InPanel, FVector2D(OuterRadius * 2.0f, OuterRadius * 2.0f), InPosition);
		if (Wheel == nullptr)
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
		Wheel->SetHitArea(EDreamRingHitArea::Ring);
		Wheel->SetItems({ MakeItem(TEXT("Reload")), MakeItem(TEXT("Grenade")), MakeItem(TEXT("Heal")), MakeItem(TEXT("Melee")) });
		Wheel->OnItemActivated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemActivated);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		Wheel->Open();
		// The style's own open time, and two frames for the frame it started in and the one that settles the layout.
		InTest.TestTrue(TEXT("Waiting out the wheel's open animation completes"),
			InRig.Driver()->Sequence().WaitSeconds(Wheel->Style.OpenDuration).WaitFrames(2).Perform());
		return Wheel->WedgeNodes.Num() == 4 && Wheel->IsOpen() ? Wheel : nullptr;
	}

	/** The pixel the camera sees at InAngleDegrees (clockwise from twelve) and InRadius (panel units) from the ring's centre, in InWedge's frame. */
	TOptional<FVector2D> RingPixel(const FDreamDriverRig& InRig, const UDreamWidget* InWedge, float InAngleDegrees, float InRadius)
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
		return DreamControlsWorldTest::SeenLocalPoint(InRig, InWedge, RectCentre + Offset);
	}

	/** The pixel the camera sees in the middle of item InIndex's slice at InRadius, or unset. */
	TOptional<FVector2D> WedgePixel(const FDreamDriverRig& InRig, const UDreamRingMenu* InWheel, int32 InIndex, float InRadius)
	{
		return RingPixel(InRig, InWheel->GetWedgeWidget(InIndex), InWheel->GetItemMidAngle(InIndex), InRadius);
	}

	/**
	 * The active virtual cursor pushed toward item InIndex's slice, a light push at a time, until it is InRadiusPixels out from
	 * InHub -- however fast the cursor goes -- and left there. False when it never got there.
	 */
	bool PushCursorOn(FDreamDriverRig& InRig, const UDreamRingMenu* InWheel, int32 InIndex, double InRadiusPixels, const FVector2D& InHub)
	{
		const UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(InRig.GetWorld());
		if (Cursor == nullptr)
		{
			return false;
		}
		const double Radians = FMath::DegreesToRadians(static_cast<double>(InWheel->GetItemMidAngle(InIndex)));
		// The stick's frame is X right, Y up -- the ring's own on a face-on panel. Six tenths of a push: past the quarter dead
		// zone the engine gives each axis (Engine/Config/BaseInput.ini, Gamepad_LeftX and Gamepad_LeftY), a few pixels a frame.
		const FVector2D Stick = FVector2D(FMath::Sin(Radians), FMath::Cos(Radians)) * 0.6;
		for (int32 Push = 0; Push < 240; ++Push)
		{
			if ((Cursor->GetVirtualCursorPosition() - InHub).Size() >= InRadiusPixels)
			{
				return true;
			}
			if (!InRig.Driver()->Sequence().VirtualCursorStick(Stick, InRig.Context().FrameSeconds).Perform())
			{
				return false;
			}
		}
		return (Cursor->GetVirtualCursorPosition() - InHub).Size() >= InRadiusPixels;
	}

	/**
	 * The virtual cursor turned on at the hub as the camera sees it and pushed toward item InIndex's slice until it is
	 * InRadiusPixels out. OutHub is the hub's pixel. False when it never got there.
	 */
	bool PushCursorOut(FDreamDriverRig& InRig, UDreamRingMenu* InWheel, int32 InIndex, double InRadiusPixels, FVector2D& OutHub)
	{
		const TOptional<FVector2D> Hub = InRig.Driver()->Find(FDreamBy::Widget(InWheel))->GetCentrePixel();
		if (!Hub.IsSet() || UDreamUIVirtualCursorSubsystem::Get(InRig.GetWorld()) == nullptr
			|| !InRig.Driver()->Sequence().MoveToPixel(Hub.GetValue()).ActivateVirtualCursor().Perform())
		{
			return false;
		}
		OutHub = Hub.GetValue();
		return PushCursorOn(InRig, InWheel, InIndex, InRadiusPixels, OutHub);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuWorldPressTest,
	"DreamGUI.RingMenu.OnAWorldPanelAClickOrATapActivatesAWedgeOnlyBetweenTheHubAndRimAsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuWorldPressTest, "DreamGUI.RingMenu.OnAWorldPanelAClickOrATapActivatesAWedgeOnlyBetweenTheHubAndRimAsSeen", "[Pointer][Touch][World]")

/*
 * A wheel on a panel three metres ahead; its radii are 50 and 120 units, seen about 107 and 256 pixels out. Clicks ten units
 * inside the hub's edge and ten past the rim, in the fourth wedge's direction, activate nothing; a click eight units out from
 * the hub's edge in the second wedge activates the second item, and one eight units in from the rim in the third activates the
 * third. Then a tap eight units out from the hub in the first wedge activates the first, and one eight units in from the rim in
 * the fourth activates the fourth.
 */
bool FDreamRingMenuWorldPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Stage.Panel, FVector2D(-40.0, 0.0), Listener.Get());
	if (!TestNotNull(TEXT("An open four-item wheel came up on the panel"), Wheel))
	{
		return false;
	}
	const TOptional<FVector2D> InHub = WedgePixel(Rig, Wheel, 3, InnerRadius - 10.0f);
	const TOptional<FVector2D> PastRim = WedgePixel(Rig, Wheel, 3, OuterRadius + 10.0f);
	const TOptional<FVector2D> SecondNearHub = WedgePixel(Rig, Wheel, 1, InnerRadius + 8.0f);
	const TOptional<FVector2D> ThirdNearRim = WedgePixel(Rig, Wheel, 2, OuterRadius - 8.0f);
	const TOptional<FVector2D> FirstNearHub = WedgePixel(Rig, Wheel, 0, InnerRadius + 8.0f);
	const TOptional<FVector2D> FourthNearRim = WedgePixel(Rig, Wheel, 3, OuterRadius - 8.0f);
	const TOptional<FVector2D> Hub = Rig.Driver()->Find(FDreamBy::Widget(Wheel))->GetCentrePixel();
	if (!TestTrue(TEXT("The camera sees every point aimed at"), InHub.IsSet() && PastRim.IsSet() && SecondNearHub.IsSet()
		&& ThirdNearRim.IsSet() && FirstNearHub.IsSet() && FourthNearRim.IsSet() && Hub.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The rim is seen at its radius in panel units"), (FourthNearRim.GetValue() - Hub.GetValue()).Size(),
		(OuterRadius - 8.0) * PixelsPerUnit(), 1.5);

	TestTrue(TEXT("Clicks inside the hub and past the rim complete"), ClickAt(Rig, InHub.GetValue()) && ClickAt(Rig, PastRim.GetValue()));
	TestEqual(TEXT("Neither activates anything"), Listener->ActivatedIndices.Num(), 0);
	TestTrue(TEXT("A click just out from the hub in the second wedge completes"), ClickAt(Rig, SecondNearHub.GetValue()));
	TestTrue(TEXT("A click just in from the rim in the third wedge completes"), ClickAt(Rig, ThirdNearRim.GetValue()));
	if (TestEqual(TEXT("Each activated one item"), Listener->ActivatedIndices.Num(), 2))
	{
		TestEqual(TEXT("...the second"), Listener->ActivatedIndices[0], 1);
		TestEqual(TEXT("...then the third"), Listener->ActivatedIndices[1], 2);
	}

	TestTrue(TEXT("A tap just out from the hub in the first wedge completes"), TapAt(Rig, FirstNearHub.GetValue()));
	TestTrue(TEXT("A tap just in from the rim in the fourth wedge completes"), TapAt(Rig, FourthNearRim.GetValue()));
	if (TestEqual(TEXT("Each tap activated one item"), Listener->ActivatedIndices.Num(), 4))
	{
		TestEqual(TEXT("...the first"), Listener->ActivatedIndices[2], 0);
		TestEqual(TEXT("...then the fourth"), Listener->ActivatedIndices[3], 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuWorldCursorTest,
	"DreamGUI.RingMenu.OnAWorldPanelTheStickTakesTheVirtualCursorIntoAWedgeAsSeenAndThePadsConfirmActivatesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuWorldCursorTest, "DreamGUI.RingMenu.OnAWorldPanelTheStickTakesTheVirtualCursorIntoAWedgeAsSeenAndThePadsConfirmActivatesIt", "[Nav][World]")

/*
 * A wheel on the panel. The stick takes the virtual cursor from the hub, as the camera sees it, toward the second wedge until
 * it is fifteen units past the hub's edge as seen; it is then between the hub and the rim as the camera sees them, and the
 * pad's confirm clicks there through the world pointer: the second item is activated, once. The stick then takes it on toward
 * the rim and past it, where the confirm activates nothing.
 */
bool FDreamRingMenuWorldCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Stage.Panel, FVector2D(-40.0, 0.0), Listener.Get());
	if (!TestNotNull(TEXT("An open four-item wheel came up on the panel"), Wheel))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller, so the rig needs one with its input up.
	Rig.EnsureGameInputHost();
	Rig.PumpFrames(1);
	FVector2D Hub;
	const double Radius = (InnerRadius + 15.0) * PixelsPerUnit();
	if (!TestTrue(FString::Printf(TEXT("The stick takes the cursor %.1f pixels out from the hub toward the second wedge"), Radius),
		PushCursorOut(Rig, Wheel, 1, Radius, Hub)))
	{
		return false;
	}
	UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(Rig.GetWorld());
	const double Out = Cursor != nullptr ? (Cursor->GetVirtualCursorPosition() - Hub).Size() : 0.0;
	TestTrue(FString::Printf(TEXT("The cursor is in the band as seen, between %.1f and %.1f pixels out (it is %.1f)"),
		InnerRadius * PixelsPerUnit(), OuterRadius * PixelsPerUnit(), Out),
		Out > InnerRadius * PixelsPerUnit() && Out < OuterRadius * PixelsPerUnit());

	TestTrue(TEXT("Pressing and releasing the cursor's confirm completes"),
		Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
	if (TestEqual(TEXT("One item was activated"), Listener->ActivatedIndices.Num(), 1))
	{
		TestEqual(TEXT("...the one whose wedge the stick pointed into"), Listener->ActivatedIndices[0], 1);
	}

	const double PastTheRim = (OuterRadius + 20.0) * PixelsPerUnit();
	if (!TestTrue(FString::Printf(TEXT("The stick takes the cursor on past the rim, %.1f pixels out"), PastTheRim),
		PushCursorOn(Rig, Wheel, 1, PastTheRim, Hub)))
	{
		return false;
	}
	TestTrue(TEXT("Pressing and releasing the cursor's confirm past the rim completes"),
		Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
	TestEqual(TEXT("Past the rim as seen the confirm activates nothing more"), Listener->ActivatedIndices.Num(), 1);
	return true;
}

#endif
