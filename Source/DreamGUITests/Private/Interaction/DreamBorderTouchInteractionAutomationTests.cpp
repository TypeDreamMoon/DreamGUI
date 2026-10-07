// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamBorder.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamBorder's pointer events under a finger, and disabled.
 *
 * UBorder binds SBorder's mouse handlers to its events, and SBorder has no touch handlers of its own: Slate gives a touch
 * nobody handles as a touch to the mouse handlers instead (bTouchFallbackToMouse, on by default; FSlateApplication::
 * RoutePointerDownEvent and RoutePointerUpEvent). So a finger's tap on a UBorder is one button down and one button up. And a
 * disabled widget is not on Slate's hit path at all (FHittestGrid::GetBubblePath stops at the first disabled widget): a
 * disabled border reports nothing.
 */
namespace DreamBorderTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A border reporting its pointer events to InListener; reporting is off by default, so it is turned on. */
	UDreamBorder* PlaceBorder(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return nullptr;
		}
		UDreamBorder* Border = InRig.MakeControl<UDreamBorder>(TEXT("Frame"), nullptr, FVector2D(300.0, 200.0));
		if (!InTest.TestNotNull(TEXT("A border can be made on the rig"), Border))
		{
			return nullptr;
		}
		Border->SetReportMouseEvents(true);
		Border->OnMouseButtonDownEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderButtonDown);
		Border->OnMouseButtonUpEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderButtonUp);
		Border->OnMouseDoubleClickEvent.AddDynamic(InListener, &UDreamPressInteractionListener::HandleBorderDoubleClick);
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(1);
		return Border;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderTapTest,
	"DreamGUI.Border.ATapReportsOneButtonDownAndOneButtonUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamBorderTapTest, "DreamGUI.Border.ATapReportsOneButtonDownAndOneButtonUp", "[Touch][Animated]")

bool FDreamBorderTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderTouchTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamBorder* Border = PlaceBorder(*this, Rig, Listener.Get());
	if (Border == nullptr)
	{
		return false;
	}
	FDreamElementRef Frame = Rig.Driver()->Find(FDreamBy::Widget(Border));

	TestTrue(TEXT("Tapping the border completes"), Frame->Tap());
	TestEqual(TEXT("The finger landing is one button down"), Listener->BorderButtonDownCount, 1);
	TestEqual(TEXT("...its lift one button up"), Listener->BorderButtonUpCount, 1);
	TestEqual(TEXT("...and a single tap is no double click"), Listener->BorderDoubleClickCount, 0);

	TestTrue(TEXT("A second finger's tap completes"), Frame->Tap(1));
	TestEqual(TEXT("Another finger's tap is another button down"), Listener->BorderButtonDownCount, 2);
	TestEqual(TEXT("...and another button up"), Listener->BorderButtonUpCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderDisabledTest,
	"DreamGUI.Border.ADisabledBorderReportsNoButtonDownOrUpForAClickOrATap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamBorderDisabledTest, "DreamGUI.Border.ADisabledBorderReportsNoButtonDownOrUpForAClickOrATap", "[Pointer][Touch][Disabled]")

bool FDreamBorderDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamBorderTouchTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	UDreamBorder* Border = PlaceBorder(*this, Rig, Listener.Get());
	if (Border == nullptr)
	{
		return false;
	}
	// UMG's SetIsEnabled.
	Border->SetIsEnabled(false);
	Rig.PumpFrames(1);
	FDreamElementRef Frame = Rig.Driver()->Find(FDreamBy::Widget(Border));

	TestTrue(TEXT("Clicking the disabled border completes"), Frame->Click());
	TestTrue(TEXT("Tapping it completes"), Frame->Tap());
	TestEqual(TEXT("A disabled border reports no button down"), Listener->BorderButtonDownCount, 0);
	TestEqual(TEXT("...and no button up"), Listener->BorderButtonUpCount, 0);

	Border->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking it enabled again completes"), Frame->Click());
	TestEqual(TEXT("Enabled again, a click is one button down"), Listener->BorderButtonDownCount, 1);
	TestEqual(TEXT("...and one button up"), Listener->BorderButtonUpCount, 1);
	return true;
}

#endif
