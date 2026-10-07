// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamTabView's strip under a finger.
 *
 * UMG ships no tab view; the control's header states a browser's rules (clicking a tab opens it, clicking the open one does
 * nothing), and a tab is a button, which takes a touch as it takes the left mouse button (SButton.cpp:354, :410). So a tap is
 * a click: it opens the tab it lands on, once, and a tap on the open tab changes nothing.
 */
namespace DreamTabViewTouchTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewTapTest,
	"DreamGUI.TabView.ATapOnAnotherTabOpensItAndATapOnTheOpenOneChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewTapTest, "DreamGUI.TabView.ATapOnAnotherTabOpensItAndATapOnTheOpenOneChangesNothing", "[Touch][Animated]")

bool FDreamTabViewTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewTouchTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTabView* TabView = Rig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
	if (!TestNotNull(TEXT("A tab view can be made on the rig"), TabView))
	{
		return false;
	}
	TabView->SetTabLabels({
		FText::AsCultureInvariant(TEXT("Video")),
		FText::AsCultureInvariant(TEXT("Audio")),
		FText::AsCultureInvariant(TEXT("Input")) });
	TabView->SetActiveTabIndex(0);
	TabView->OnTabChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleTabChanged);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("The strip has a tab per caption"), TabView->Tabs.Num(), 3))
	{
		return false;
	}

	FDreamElementRef Third = Rig.Driver()->Find(FDreamBy::Widget(TabView->Tabs[2].TabNode.Get()));
	TestTrue(TEXT("Tapping the third tab completes"), Third->Tap());
	TestEqual(TEXT("The tapped tab is the open one"), TabView->GetActiveTabIndex(), 2);
	if (TestEqual(TEXT("The switch was announced once"), Listener->TabChangedIndices.Num(), 1))
	{
		TestEqual(TEXT("Naming the third tab"), Listener->TabChangedIndices[0], 2);
	}

	TestTrue(TEXT("Tapping the open tab again completes"), Third->Tap());
	TestEqual(TEXT("The open tab is still the third"), TabView->GetActiveTabIndex(), 2);
	TestEqual(TEXT("...and nothing more was announced"), Listener->TabChangedIndices.Num(), 1);
	return true;
}

#endif
