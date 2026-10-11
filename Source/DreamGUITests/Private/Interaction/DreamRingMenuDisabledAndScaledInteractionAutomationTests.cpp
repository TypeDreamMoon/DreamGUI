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
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRingMenu disabled, and on a canvas scaled to half and to double.
 *
 * UMG has no ring menu, so the rules are this control's header's: a wedge is a button over its slice -- with the Ring hit area
 * exactly the drawn annulus, the hub and everything past the rim dead -- and choosing a wedge activates its item. A pad reaches
 * a wedge through the virtual cursor: the stick moves it in viewport pixels, as Slate's analog cursor moves (FAnalogCursor's
 * speed is in pixels, Slate/Private/Framework/Application/AnalogCursor.cpp:368-373), and the confirm button clicks under it.
 * Disabled, the wheel's wedges are buttons that are off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/
 * HittestGrid.cpp:241), so neither a click, a tap nor the cursor's confirm activates anything. On a scaled layer the annulus is
 * where it is drawn: its radii, like everything else about it, are in the wheel's own units.
 *
 * The geometry is the wedge tests' (DreamRingMenuInteractionAutomationTests.cpp): angles clockwise from twelve about the centre
 * of a wedge's own rect, a point at angle A and radius R being (R sin A, R cos A) in its local space, Y up. The style is told
 * only to stay still under the pointer; its open animation is its own and is waited out.
 */
namespace DreamRingMenuConfigTestLocal
{
	const float OuterRadius = 140.0f;
	const float InnerRadius = 60.0f;

	FDreamRingMenuItem MakeItem(const TCHAR* InLabel)
	{
		FDreamRingMenuItem Item;
		Item.Label = FText::AsCultureInvariant(InLabel);
		Item.Tag = FName(InLabel);
		return Item;
	}

	/** A four-item wheel at InPosition (canvas units, Y up), the drawn annulus its hit area, open and its open animation over. */
	UDreamRingMenu* PlaceWheel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamRingMenu* Wheel = InRig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), nullptr, FVector2D(OuterRadius * 2.0f, OuterRadius * 2.0f), InPosition);
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

	/** The pixel at InAngleDegrees (clockwise from twelve) and InRadius (wheel units) from the ring's centre, in InWedge's frame. */
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

	/** The pixel in the middle of item InIndex's slice at InRadius, or unset. */
	TOptional<FVector2D> WedgePixel(const UDreamRingMenu* InWheel, int32 InIndex, float InRadius)
	{
		return RingPixel(InWheel->GetWedgeWidget(InIndex), InWheel->GetItemMidAngle(InIndex), InRadius);
	}

	/**
	 * The virtual cursor taken from the hub toward item InIndex's slice, a light push at a time, until it is InRadius pixels out
	 * from the hub -- however fast the cursor goes -- and left there. False when it never got there.
	 */
	bool PushCursorOut(FDreamDriverRig& InRig, UDreamRingMenu* InWheel, int32 InIndex, double InRadiusPixels, FVector2D& OutHub)
	{
		const TOptional<FVector2D> Hub = InRig.Driver()->Find(FDreamBy::Widget(InWheel))->GetCentrePixel();
		UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(InRig.GetWorld());
		if (!Hub.IsSet() || Cursor == nullptr
			|| !InRig.Driver()->Sequence().MoveToPixel(Hub.GetValue()).ActivateVirtualCursor().Perform())
		{
			return false;
		}
		OutHub = Hub.GetValue();
		const double Radians = FMath::DegreesToRadians(static_cast<double>(InWheel->GetItemMidAngle(InIndex)));
		// The stick's frame is X right, Y up -- the ring's own. Six tenths of a push: a few pixels a frame. Not less: the cursor
		// reads the stick through the player's input, whose axes the engine gives a dead zone of a quarter each
		// (Engine/Config/BaseInput.ini, Gamepad_LeftX and Gamepad_LeftY), and a third of a push toward a corner -- under a
		// quarter on either axis -- reads as no push at all.
		const FVector2D Stick = FVector2D(FMath::Sin(Radians), FMath::Cos(Radians)) * 0.6;
		for (int32 Push = 0; Push < 240; ++Push)
		{
			if ((Cursor->GetVirtualCursorPosition() - OutHub).Size() >= InRadiusPixels)
			{
				return true;
			}
			if (!InRig.Driver()->Sequence().VirtualCursorStick(Stick, InRig.Context().FrameSeconds).Perform())
			{
				return false;
			}
		}
		return (Cursor->GetVirtualCursorPosition() - OutHub).Size() >= InRadiusPixels;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuDisabledPressTest,
	"DreamGUI.RingMenu.AClickOrATapOnAWedgeOfADisabledWheelActivatesNothingUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuDisabledPressTest, "DreamGUI.RingMenu.AClickOrATapOnAWedgeOfADisabledWheelActivatesNothingUntilItIsEnabledAgain", "[Pointer][Touch][Disabled]")

/*
 * The wheel disabled: a click in the middle of the third wedge and a tap in the middle of the second activate nothing and
 * choose nothing. Enabled again, the same click activates the third item, once.
 */
bool FDreamRingMenuDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuConfigTestLocal;
	using namespace DreamControlsConfigTest;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, FVector2D::ZeroVector, Listener.Get());
	if (!TestNotNull(TEXT("An open four-item wheel came up"), Wheel))
	{
		return false;
	}
	const float MidBand = (InnerRadius + OuterRadius) * 0.5f;
	const TOptional<FVector2D> Third = WedgePixel(Wheel, 2, MidBand);
	const TOptional<FVector2D> Second = WedgePixel(Wheel, 1, MidBand);
	if (!TestTrue(TEXT("The middles of the second and third wedges are on the viewport"), Third.IsSet() && Second.IsSet()))
	{
		return false;
	}
	Wheel->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the middle of the third wedge completes"), ClickAt(Rig, Third.GetValue()));
	TestTrue(TEXT("Tapping the middle of the second wedge completes"), TapAt(Rig, Second.GetValue()));
	TestEqual(TEXT("A disabled wheel activates nothing"), Listener->ActivatedIndices.Num(), 0);
	TestEqual(TEXT("...and chooses nothing"), Wheel->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));

	Wheel->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the third wedge with the wheel enabled again completes"), ClickAt(Rig, Third.GetValue()));
	if (TestEqual(TEXT("Enabled again, the click activates one item"), Listener->ActivatedIndices.Num(), 1))
	{
		TestEqual(TEXT("...the third"), Listener->ActivatedIndices[0], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuDisabledCursorTest,
	"DreamGUI.RingMenu.ThePadsConfirmWithTheCursorInAWedgeOfADisabledWheelActivatesNothingUntilItIsEnabledAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuDisabledCursorTest, "DreamGUI.RingMenu.ThePadsConfirmWithTheCursorInAWedgeOfADisabledWheelActivatesNothingUntilItIsEnabledAgain", "[Nav][Disabled]")

/*
 * The wheel disabled; the stick takes the virtual cursor from the hub into the second wedge's slice and the pad's confirm
 * clicks there: nothing is activated. Enabled again, with the cursor where it was, the same confirm activates the second item.
 */
bool FDreamRingMenuDisabledCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuConfigTestLocal;
	using namespace DreamControlsConfigTest;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, FVector2D::ZeroVector, Listener.Get());
	if (!TestNotNull(TEXT("An open four-item wheel came up"), Wheel))
	{
		return false;
	}
	// The cursor reads the stick from player 0's controller, so the rig needs one with its input up.
	Rig.EnsureGameInputHost();
	Wheel->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FVector2D Hub;
	if (!TestTrue(TEXT("The stick takes the cursor from the hub into the second wedge's slice"),
		PushCursorOut(Rig, Wheel, 1, InnerRadius + 20.0, Hub)))
	{
		return false;
	}

	TestTrue(TEXT("Pressing and releasing the cursor's confirm completes"),
		Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
	TestEqual(TEXT("On a disabled wheel the confirm activates nothing"), Listener->ActivatedIndices.Num(), 0);

	Wheel->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The same confirm with the wheel enabled again completes"),
		Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
	if (TestEqual(TEXT("Enabled again, the confirm activates one item"), Listener->ActivatedIndices.Num(), 1))
	{
		TestEqual(TEXT("...the one whose wedge the cursor is in"), Listener->ActivatedIndices[0], 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuScaledPressTest,
	"DreamGUI.RingMenu.OnACanvasScaledToHalfAndToDoubleAClickOrATapActivatesAWedgeOnlyBetweenTheDrawnHubAndRim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuScaledPressTest, "DreamGUI.RingMenu.OnACanvasScaledToHalfAndToDoubleAClickOrATapActivatesAWedgeOnlyBetweenTheDrawnHubAndRim", "[Pointer][Touch][Scaled]")

/*
 * A wheel off the middle, at each scale; its radii are 60 and 140 units, so 30 and 70 pixels at half scale and 120 and 280 at
 * double. Clicks ten units inside the hub's edge and ten past the rim, in the fourth wedge's direction, activate nothing; a
 * click eight units out from the hub's edge in the second wedge activates the second item, and one eight units in from the
 * rim in the third activates the third. Then a tap eight units out from the hub in the first wedge activates the first, and
 * one eight units in from the rim in the fourth activates the fourth.
 */
bool FDreamRingMenuScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, FVector2D(-100.0, 0.0), Listener.Get());
		if (!TestNotNull(*At(Scale, TEXT("An open four-item wheel came up")), Wheel))
		{
			return false;
		}
		const TOptional<FVector2D> InHub = WedgePixel(Wheel, 3, InnerRadius - 10.0f);
		const TOptional<FVector2D> PastRim = WedgePixel(Wheel, 3, OuterRadius + 10.0f);
		const TOptional<FVector2D> SecondNearHub = WedgePixel(Wheel, 1, InnerRadius + 8.0f);
		const TOptional<FVector2D> ThirdNearRim = WedgePixel(Wheel, 2, OuterRadius - 8.0f);
		const TOptional<FVector2D> FirstNearHub = WedgePixel(Wheel, 0, InnerRadius + 8.0f);
		const TOptional<FVector2D> FourthNearRim = WedgePixel(Wheel, 3, OuterRadius - 8.0f);
		const TOptional<FVector2D> Hub = Rig.Driver()->Find(FDreamBy::Widget(Wheel))->GetCentrePixel();
		if (!TestTrue(At(Scale, TEXT("Every point aimed at is on the viewport")), InHub.IsSet() && PastRim.IsSet() && SecondNearHub.IsSet()
			&& ThirdNearRim.IsSet() && FirstNearHub.IsSet() && FourthNearRim.IsSet() && Hub.IsSet()))
		{
			return false;
		}
		TestNearlyEqual(*At(Scale, TEXT("The rim is drawn at the canvas's scale")), (FourthNearRim.GetValue() - Hub.GetValue()).Size(),
			(OuterRadius - 8.0) * Scale, 1.5);

		TestTrue(At(Scale, TEXT("Clicks inside the hub and past the rim complete")), ClickAt(Rig, InHub.GetValue()) && ClickAt(Rig, PastRim.GetValue()));
		TestEqual(*At(Scale, TEXT("Neither activates anything")), Listener->ActivatedIndices.Num(), 0);
		TestTrue(At(Scale, TEXT("A click just out from the hub in the second wedge completes")), ClickAt(Rig, SecondNearHub.GetValue()));
		TestTrue(At(Scale, TEXT("A click just in from the rim in the third wedge completes")), ClickAt(Rig, ThirdNearRim.GetValue()));
		if (TestEqual(*At(Scale, TEXT("Each activated one item")), Listener->ActivatedIndices.Num(), 2))
		{
			TestEqual(*At(Scale, TEXT("...the second")), Listener->ActivatedIndices[0], 1);
			TestEqual(*At(Scale, TEXT("...then the third")), Listener->ActivatedIndices[1], 2);
		}

		TestTrue(At(Scale, TEXT("A tap just out from the hub in the first wedge completes")), TapAt(Rig, FirstNearHub.GetValue()));
		TestTrue(At(Scale, TEXT("A tap just in from the rim in the fourth wedge completes")), TapAt(Rig, FourthNearRim.GetValue()));
		if (TestEqual(*At(Scale, TEXT("Each tap activated one item")), Listener->ActivatedIndices.Num(), 4))
		{
			TestEqual(*At(Scale, TEXT("...the first")), Listener->ActivatedIndices[2], 0);
			TestEqual(*At(Scale, TEXT("...then the fourth")), Listener->ActivatedIndices[3], 3);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRingMenuScaledCursorTest,
	"DreamGUI.RingMenu.OnACanvasScaledToHalfAndToDoubleTheStickAndThePadsConfirmActivateTheWedgeTheCursorIsInAsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRingMenuScaledCursorTest, "DreamGUI.RingMenu.OnACanvasScaledToHalfAndToDoubleTheStickAndThePadsConfirmActivateTheWedgeTheCursorIsInAsDrawn", "[Nav][Scaled]")

/*
 * A wheel off the middle, at each scale. The stick takes the virtual cursor from the hub toward the second wedge until it is
 * fifteen units past the hub's edge as drawn -- 37.5 pixels out at half scale, 150 at double -- and the pad's confirm clicks
 * there: the second item is activated, once. Short of that the cursor would be in the hub at one scale or the other were the
 * wheel's radii read in pixels.
 */
bool FDreamRingMenuScaledCursorTest::RunTest(const FString& Parameters)
{
	using namespace DreamRingMenuConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, FVector2D(-100.0, 0.0), Listener.Get());
		if (!TestNotNull(*At(Scale, TEXT("An open four-item wheel came up")), Wheel))
		{
			return false;
		}
		Rig.EnsureGameInputHost();
		Rig.PumpFrames(1);
		FVector2D Hub;
		const double Radius = (InnerRadius + 15.0) * Scale;
		if (!TestTrue(At(Scale, FString::Printf(TEXT("The stick takes the cursor %.1f pixels out from the hub toward the second wedge"), Radius)),
			PushCursorOut(Rig, Wheel, 1, Radius, Hub)))
		{
			return false;
		}
		UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(Rig.GetWorld());
		const double Out = Cursor != nullptr ? (Cursor->GetVirtualCursorPosition() - Hub).Size() : 0.0;
		TestTrue(At(Scale, FString::Printf(TEXT("The cursor is in the drawn band, between %.1f and %.1f pixels out (it is %.1f)"),
			InnerRadius * Scale, OuterRadius * Scale, Out)), Out > InnerRadius * Scale && Out < OuterRadius * Scale);

		TestTrue(At(Scale, TEXT("Pressing and releasing the cursor's confirm completes")),
			Rig.Driver()->Sequence().VirtualCursorPress().VirtualCursorRelease().Perform());
		if (TestEqual(*At(Scale, TEXT("One item was activated")), Listener->ActivatedIndices.Num(), 1))
		{
			TestEqual(*At(Scale, TEXT("...the one whose wedge the stick pointed into")), Listener->ActivatedIndices[0], 1);
		}
	}
	return true;
}

#endif
