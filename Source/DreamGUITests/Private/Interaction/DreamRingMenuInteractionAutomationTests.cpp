// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRingMenu.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRingMenu, clicked through the real pointer pipeline: a click on a wedge chooses that wedge's
 * item, and a click in the hub chooses nothing. UMG has no ring menu, so the rules are this control's
 * header's.
 *
 * The pixels are worked out rather than found, because every wedge is a square the size of the whole
 * ring and a widget's centre says nothing about which slice it owns. The arithmetic follows
 * UDreamRingSectorRaycast exactly: angles are degrees CLOCKWISE FROM TWELVE, measured about the
 * centre of the wedge's own local rect, in local space with X to the right and Y up (so twelve o'clock
 * is +Y and three o'clock is +X). A point at angle A and radius R from that centre is therefore
 * (R sin A, R cos A), and FDreamDriverProjection turns a local point into the pixel the raycaster will
 * deproject back onto it.
 *
 * The style is made inline and deterministic before anything is measured: no highlight growth (a
 * hovered wedge would otherwise grow its hit shape under the pointer) and no open animation (a
 * headless world does not advance tweens, and a ring left at its opening scale would put every pixel
 * worked out below in the wrong place).
 */
namespace DreamPressRingMenuTestLocal
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

	/** A four-item wheel on the rig, open, its selection and activation going to InListener. */
	UDreamRingMenu* PlaceWheel(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return nullptr;
		}
		UDreamRingMenu* Wheel = InRig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), nullptr,
			FVector2D(OuterRadius * 2.0f, OuterRadius * 2.0f));
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
		Wheel->Style.OpenDuration = 0.0f;
		// Written in place and then pushed, which is what the family's ApplyStyle is for; nothing
		// re-derives a control from a style edited in place.
		Wheel->ApplyStyle();
		Wheel->SetItems({ MakeItem(TEXT("Reload")), MakeItem(TEXT("Grenade")), MakeItem(TEXT("Heal")), MakeItem(TEXT("Melee")) });
		Wheel->Open();
		Wheel->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Wheel->OnItemActivated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemActivated);
		InRig.PumpFrames(2);

		if (!InTest.TestEqual(TEXT("The wheel has a wedge per item"), Wheel->WedgeNodes.Num(), 4)
			|| !InTest.TestTrue(TEXT("And is open"), Wheel->IsOpen()))
		{
			return nullptr;
		}
		return Wheel;
	}

	/**
	 * The pixel at InAngleDegrees (clockwise from twelve) and InRadius from the ring's centre, worked
	 * out in the frame of InWedge -- the frame its hit sector measures in.
	 */
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

	bool ClickAtPixel(FDreamDriverRig& InRig, const FVector2D& InPixel)
	{
		return InRig.Driver()->Sequence().MoveToPixel(InPixel).Press().Release().Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressRingMenuWedgeTest,
	"DreamGUI.RingMenu.ClickingTheMiddleOfAWedgeChoosesThatWedgesItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressRingMenuWedgeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressRingMenuTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Listener.Get());
	if (Wheel == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Nothing is chosen before the click"), Wheel->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));

	// The third item, lower left: the middle of its slice, halfway between the hub and the rim.
	const int32 Target = 2;
	const TOptional<FVector2D> Pixel = RingPixel(Wheel->GetWedgeWidget(Target),
		Wheel->GetItemMidAngle(Target), (InnerRadius + OuterRadius) * 0.5f);
	if (!TestTrue(TEXT("The middle of the third wedge is a pixel the pointer can reach"), Pixel.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the middle of the third wedge completes"), ClickAtPixel(Rig, Pixel.GetValue()));

	TestEqual(TEXT("The third item is chosen"), Wheel->GetSelectedIndex(), Target);
	if (TestEqual(TEXT("The choice was announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the third item"), Listener->SelectionIndices[0], Target);
	}
	if (TestEqual(TEXT("And the item was activated once"), Listener->ActivatedTags.Num(), 1))
	{
		TestEqual(TEXT("By its tag"), Listener->ActivatedTags[0], FName(TEXT("Heal")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressRingMenuHubTest,
	"DreamGUI.RingMenu.ClickingInsideTheHubChoosesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressRingMenuHubTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressRingMenuTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamRingMenu* Wheel = PlaceWheel(*this, Rig, Listener.Get());
	if (Wheel == nullptr)
	{
		return false;
	}
	// Inside the inner radius, and in the third item's direction: were the dead zone missing, this is
	// the wedge a click here would pick.
	const int32 Direction = 2;
	const TOptional<FVector2D> Pixel = RingPixel(Wheel->GetWedgeWidget(Direction),
		Wheel->GetItemMidAngle(Direction), InnerRadius * 0.5f);
	if (!TestTrue(TEXT("A point in the hub is a pixel the pointer can reach"), Pixel.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking inside the hub completes"), ClickAtPixel(Rig, Pixel.GetValue()));

	TestEqual(TEXT("Nothing is chosen"), Wheel->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("No choice was announced"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("And nothing was activated"), Listener->ActivatedTags.Num(), 0);
	return true;
}

#endif
