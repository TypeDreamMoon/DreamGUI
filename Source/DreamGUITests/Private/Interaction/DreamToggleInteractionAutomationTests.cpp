// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamToggle, clicked through the real pointer pipeline and held to UMG's UCheckBox.
 *
 * UCheckBox is SCheckBox underneath, and SCheckBox::ToggleCheckedState (Slate/Private/Widgets/Input/
 * SCheckBox.cpp) is the whole of what a click does to its state: Checked or Undetermined becomes
 * Unchecked, Unchecked becomes Checked, and OnCheckStateChanged is told the new state each time. A
 * click never moves a check box INTO Undetermined -- that state is only ever authored -- so there is
 * no three-step cycle to walk; what there is to check is where a click takes each of the three.
 *
 * The toggle is deliberately label-less (see its header), so UMG's "the label is part of the hit area"
 * has nothing here to be tested against.
 */
namespace DreamPressToggleTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FPlacedToggle
	{
		UDreamToggle* Toggle = nullptr;
		TSharedPtr<FDreamDriverElement> Element;

		bool IsReady() const { return Toggle != nullptr && Element.IsValid() && Element->Exists(); }
	};

	/**
	 * Rig, toggle, a frame of layout and the driver's handle on it. The listener is bound here, AFTER
	 * any state a test wants to start from has been written -- see InStartState -- so the counts it
	 * collects are the clicks' and nothing else's.
	 */
	FPlacedToggle PlaceToggle(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		EDreamCheckState InStartState = EDreamCheckState::Unchecked)
	{
		FPlacedToggle Placed;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Placed;
		}
		UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(TEXT("Mute"), nullptr, FVector2D(40.0, 40.0));
		if (!InTest.TestNotNull(TEXT("A toggle can be made on the rig"), Toggle))
		{
			return Placed;
		}
		Toggle->SetCheckedState(InStartState);
		Toggle->OnCheckStateChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleCheckStateChanged);
		InRig.PumpFrames(1);

		Placed.Toggle = Toggle;
		Placed.Element = InRig.Driver()->Find(FDreamBy::Widget(Toggle));
		InTest.TestTrue(TEXT("The driver can find the toggle it is about to act on"), Placed.IsReady());
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressToggleClickFlipsTest,
	"DreamGUI.Toggle.EachClickFlipsTheCheckAndAnnouncesTheNewState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressToggleClickFlipsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressToggleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	TestFalse(TEXT("The toggle starts unchecked"), Placed.Toggle->IsChecked());

	TestTrue(TEXT("The first click completes"), Placed.Element->Click());
	TestTrue(TEXT("One click checks it"), Placed.Toggle->IsChecked());
	if (TestEqual(TEXT("And says so once"), Listener->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("Carrying Checked"), Listener->CheckStates[0], EDreamCheckState::Checked);
	}

	TestTrue(TEXT("The second click completes"), Placed.Element->Click());
	TestFalse(TEXT("A second click unchecks it"), Placed.Toggle->IsChecked());
	if (TestEqual(TEXT("And says so once more"), Listener->CheckStates.Num(), 2))
	{
		TestEqual(TEXT("Carrying Unchecked"), Listener->CheckStates[1], EDreamCheckState::Unchecked);
	}
	return true;
}

/**
 * The branch that decides this, verbatim in intent: "If the current check box state is checked OR
 * undetermined we set the check box to unchecked" (SCheckBox::ToggleCheckedState). A mixed selection
 * clicked once is cleared, not ticked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressToggleUndeterminedClickTest,
	"DreamGUI.Toggle.ClickingAnUndeterminedToggleUnchecksItAsUMGsCheckBoxDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressToggleUndeterminedClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressToggleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get(), EDreamCheckState::Undetermined);
	if (!Placed.IsReady())
	{
		return false;
	}
	if (!TestEqual(TEXT("The toggle starts undetermined"), Placed.Toggle->GetCheckedState(), EDreamCheckState::Undetermined))
	{
		return false;
	}

	TestTrue(TEXT("Clicking it completes"), Placed.Element->Click());

	TestEqual(TEXT("A click takes an undetermined check box to Unchecked"),
		Placed.Toggle->GetCheckedState(), EDreamCheckState::Unchecked);
	if (TestEqual(TEXT("And announces the change once"), Listener->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("Carrying Unchecked"), Listener->CheckStates[0], EDreamCheckState::Unchecked);
	}
	return true;
}

/**
 * SCheckBox::OnMouseButtonDown acts on EKeys::LeftMouseButton alone (the right button only opens a
 * context menu, and only when one is bound), and OnMouseButtonUp toggles on the left button or a touch.
 * A right click on a check box therefore changes nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressToggleRightClickTest,
	"DreamGUI.Toggle.ARightClickDoesNotFlipTheToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressToggleRightClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressToggleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Right-clicking the toggle completes"), Placed.Element->Click(EDreamUIMouseButtonType::Right));

	TestFalse(TEXT("A right click leaves the toggle unchecked"), Placed.Toggle->IsChecked());
	TestEqual(TEXT("And announces nothing"), Listener->CheckStates.Num(), 0);
	return true;
}

/**
 * SCheckBox::OnMouseButtonDown and OnMouseButtonUp both begin with IsEnabled(), and a disabled widget
 * is not even on the hit path (FHittestGrid::GetBubblePath) -- so a disabled check box is inert to
 * the pointer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressToggleDisabledTest,
	"DreamGUI.Toggle.ClickingADisabledToggleChangesNothingAndSaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressToggleDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressToggleTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FPlacedToggle Placed = PlaceToggle(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	Placed.Toggle->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the disabled toggle completes"), Placed.Element->Click());

	TestFalse(TEXT("A disabled toggle stays unchecked"), Placed.Toggle->IsChecked());
	TestEqual(TEXT("And announces nothing"), Listener->CheckStates.Num(), 0);
	return true;
}

#endif
