// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamScrollBar.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollbar.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamDragInteractionTestTypes.h"

/*
 * THE SCROLL BAR UNDER A FINGER.
 *
 * SScrollBar has no touch handlers of its own, so a finger reaches it through the mouse handlers Slate falls a touch back to
 * (bTouchFallbackToMouse; FSlateApplication::RoutePointerDownEvent and RoutePointerMoveEvent): a finger on the thumb grabs it
 * where it landed and the thumb follows it one pixel for one pixel, exactly as the mouse's does (see the mouse tests in
 * DreamScrollBarInteractionAutomationTests.cpp for the bar itself and why its value is a fraction of the thumb's travel).
 */
namespace DreamScrollBarTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarFingerDragTest,
	"DreamGUI.ScrollBar.AFingerDraggingTheHandleMovesItWithTheFingerPixelForPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamScrollBarFingerDragTest, "DreamGUI.ScrollBar.AFingerDraggingTheHandleMovesItWithTheFingerPixelForPixel", "[Touch][Animated]")

bool FDreamScrollBarFingerDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBarTouchTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// Begun play, as a game's UI has before anything is touched: the bar re-places its handle in OnEnable and Start.
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable())
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	// A vertical bar 400 tall, zero at the top, a quarter of the track under the handle.
	UDreamScrollBar* Bar = Rig.MakeControl<UDreamScrollBar>(TEXT("Bar"), nullptr, FVector2D(24.0, 400.0));
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The bar came up with a track and a handle"), Bar != nullptr && Bar->TrackNode != nullptr && Bar->HandleNode != nullptr))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	FDreamElementRef Handle = Driver->Find(FDreamBy::Widget(Bar->HandleNode.Get()));
	const TOptional<FBox2D> Track = Driver->Find(FDreamBy::Widget(Bar->TrackNode.Get()))->GetPixelRect();
	const TOptional<FBox2D> HandleBefore = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The track and the handle are on screen"), Track.IsSet() && HandleBefore.IsSet()))
	{
		return false;
	}
	const double TravelLength = (Track->Max.Y - Track->Min.Y) - (HandleBefore->Max.Y - HandleBefore->Min.Y);
	if (!TestTrue(FString::Printf(TEXT("The handle has room to travel (%.1f pixels)"), TravelLength), TravelLength > 150.0))
	{
		return false;
	}
	TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	Bar->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);

	const double DragPixels = 90.0;
	TestTrue(TEXT("The finger's drag down the handle completes"), Handle->TouchDragBy(FVector2D(0.0, DragPixels)));

	const TOptional<FBox2D> HandleAfter = Handle->GetPixelRect();
	if (!TestTrue(TEXT("The handle is still on screen"), HandleAfter.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The handle followed the finger down, pixel for pixel"),
		static_cast<float>(HandleAfter->Min.Y - HandleBefore->Min.Y), static_cast<float>(DragPixels), 1.0f);
	TestNearlyEqual(TEXT("The value moved by the distance dragged over the travel"),
		Bar->GetValue(), static_cast<float>(DragPixels / TravelLength), static_cast<float>(1.0 / TravelLength));
	TestTrue(TEXT("The value change was reported"), Values->NumFloats() >= 1);
	return true;
}

#endif
