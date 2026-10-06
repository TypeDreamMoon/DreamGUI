// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRadioButton.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRadioButton in a group, chosen with a finger and with the pad, and disabled while held.
 *
 * UMG has no radio of its own; its game UI makes one from CommonUI's toggleable buttons in a button group that requires a
 * selection (UCommonButtonGroupBase, bSelectionRequired): choosing one deselects the one that was chosen, and the chosen one
 * stays chosen. The input rules are a button's: a touch is a press and a lift (SButton takes a touch the way it takes the
 * left mouse button, SButton.cpp:354, :410), and the pad's Accept presses the focused control (SButton::OnKeyDown, :296).
 */
namespace DreamRadioTouchPadTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A panel with a toggle group that forbids "none", and three radios in a row inside it. */
	struct FRadioGroup
	{
		UDreamWidget* Panel = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamRadioButton*> Radios;

		bool IsReady() const { return Panel != nullptr && Group != nullptr && Radios.Num() == 3 && !Radios.Contains(nullptr); }

		/** Which radio InWidget is or is part of, or INDEX_NONE. */
		int32 IndexOf(const UDreamWidget* InWidget) const
		{
			for (int32 Index = 0; Index < Radios.Num(); ++Index)
			{
				if (InWidget != nullptr && (InWidget == Radios[Index] || InWidget->IsChildOf(Radios[Index])))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}
	};

	/**
	 * One group on the rig, the first radio checked, wired through SetToggleGroup -- the call the radio's header gives for
	 * wiring a group from code after Initialize.
	 */
	FRadioGroup MakeRadioGroup(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FRadioGroup Made;
		Made.Panel = InRig.MakeWidget(TEXT("Quality"), nullptr, FVector2D(400.0, 100.0));
		if (!InTest.TestNotNull(TEXT("A panel for the group can be made on the rig"), Made.Panel))
		{
			return Made;
		}
		Made.Group = Made.Panel->AddComponent<UUIToggleGroup>();
		if (!InTest.TestNotNull(TEXT("The panel carries a toggle group"), Made.Group))
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(false);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamRadioButton* Radio = InRig.MakeControl<UDreamRadioButton>(FString::Printf(TEXT("Quality_Radio%d"), Index), Made.Panel,
				FVector2D(40.0, 40.0), FVector2D(-120.0 + 120.0 * Index, 0.0));
			if (Radio != nullptr)
			{
				Radio->SetToggleGroup(Made.Group);
			}
			Made.Radios.Add(Radio);
		}
		if (InTest.TestTrue(TEXT("Three radios were made in the group"), Made.IsReady()))
		{
			Made.Radios[0]->SetIsChecked(true);
		}
		return Made;
	}

	/** One listener per radio, bound after the starting choice so only the gestures are counted. */
	void Listen(const FRadioGroup& InGroup, TArray<TStrongObjectPtr<UDreamPressInteractionListener>>& OutListeners)
	{
		for (UDreamRadioButton* Radio : InGroup.Radios)
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			Radio->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			OutListeners.Add(MoveTemp(Listener));
		}
	}

	int32 CountAnnouncements(const TArray<TStrongObjectPtr<UDreamPressInteractionListener>>& InListeners)
	{
		int32 Count = 0;
		for (const TStrongObjectPtr<UDreamPressInteractionListener>& Listener : InListeners)
		{
			Count += Listener->CheckStates.Num();
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioTapTest,
	"DreamGUI.RadioButton.ATapOnAnotherRadioChoosesItAndTheOneThatWasChosenLetsGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioTapTest, "DreamGUI.RadioButton.ATapOnAnotherRadioChoosesItAndTheOneThatWasChosenLetsGo", "[Touch][Animated]")

/*
 * A tap is a press and a lift on the same radio, which is a click of it under the default touch method: the tapped radio is
 * chosen and says so, the radio that was chosen lets go and says so, and the third says nothing. A second tap on the radio
 * now chosen changes nothing, as a click on it would not.
 */
bool FDreamRadioTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioTouchPadTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(*this, Rig);
	if (!Radios.IsReady())
	{
		return false;
	}
	Listen(Radios, Listeners);
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);

	FDreamElementRef Second = Rig.Driver()->Find(FDreamBy::Widget(Radios.Radios[1]));
	TestTrue(TEXT("Tapping the second radio completes"), Second->Tap());

	TestTrue(TEXT("The tapped radio is chosen"), Radios.Radios[1]->IsChecked());
	TestFalse(TEXT("The radio that was chosen let go"), Radios.Radios[0]->IsChecked());
	TestFalse(TEXT("The third was never chosen"), Radios.Radios[2]->IsChecked());
	TestEqual(TEXT("The tapped radio announced one change"), Listeners[1]->CheckStates.Num(), 1);
	TestEqual(TEXT("The radio that let go announced one change"), Listeners[0]->CheckStates.Num(), 1);
	TestEqual(TEXT("The bystander announced nothing"), Listeners[2]->CheckStates.Num(), 0);

	TestTrue(TEXT("Tapping the chosen radio again completes"), Second->Tap());
	TestTrue(TEXT("It stays chosen"), Radios.Radios[1]->IsChecked());
	TestEqual(TEXT("...and nothing more is announced"), CountAnnouncements(Listeners), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioDisabledWhileHeldTest,
	"DreamGUI.RadioButton.ARadioDisabledWhileHeldIsNotChosenWhenLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioDisabledWhileHeldTest, "DreamGUI.RadioButton.ARadioDisabledWhileHeldIsNotChosenWhenLetGo", "[Pointer][Disabled]")

/*
 * Pressed on the second radio, which the game then disables, and let go over it. The press that lost its interaction is
 * released without acknowledging the click (CommonUI/Private/CommonButtonTypes.cpp:50-62), so nothing is chosen: the first
 * radio keeps the group's choice and nobody announces anything. Enabled again, a click chooses it.
 */
bool FDreamRadioDisabledWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioTouchPadTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(*this, Rig);
	if (!Radios.IsReady())
	{
		return false;
	}
	Listen(Radios, Listeners);
	Rig.PumpFrames(1);
	FDreamElementRef Second = Rig.Driver()->Find(FDreamBy::Widget(Radios.Radios[1]));

	TestTrue(TEXT("Pressing on the second radio completes"), Second->Press());
	Radios.Radios[1]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Letting go over the disabled radio completes"), Second->Release());

	TestFalse(TEXT("A radio disabled while held is not chosen"), Radios.Radios[1]->IsChecked());
	TestTrue(TEXT("The first radio keeps the choice"), Radios.Radios[0]->IsChecked());
	TestEqual(TEXT("...and the group says it is still the first"), Radios.Group->GetSelectedItem(), Radios.Radios[0]->ToggleBehaviour.Get());
	TestEqual(TEXT("Nothing in the group announced anything"), CountAnnouncements(Listeners), 0);

	Radios.Radios[1]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the second radio enabled again completes"), Second->Click());
	TestTrue(TEXT("Enabled again, a click chooses it"), Radios.Radios[1]->IsChecked());
	TestFalse(TEXT("...and the first lets go"), Radios.Radios[0]->IsChecked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioPadConfirmTest,
	"DreamGUI.RadioButton.ThePadsConfirmChoosesTheRadioThePadHasMovedOnto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioPadConfirmTest, "DreamGUI.RadioButton.ThePadsConfirmChoosesTheRadioThePadHasMovedOnto", "[Nav][Animated]")

/*
 * The stick moves the pad's focus onto a radio that is not chosen, and Accept presses it (SButton::OnKeyDown and OnKeyUp,
 * SButton.cpp:296-340): that radio is chosen, the one that was chosen lets go, and moving the focus alone chose nothing --
 * a group whose buttons select on focus is CommonUI's opt-in (bShouldSelectUponReceivingFocus), not its default.
 */
bool FDreamRadioPadConfirmTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioTouchPadTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(*this, Rig);
	if (!Radios.IsReady())
	{
		return false;
	}
	Listen(Radios, Listeners);
	Rig.PumpFrames(1);
	FDreamDriverRef Driver = Rig.Driver();

	// The first stick press lands on whichever selectable the module defaults to; one more to the right if that is the
	// radio already chosen, so the confirm below presses one that is not.
	TestTrue(TEXT("A stick press completes"), Driver->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
	int32 Target = Radios.IndexOf(Rig.EventSystem()->GetHighlightedComponentForNavigation(0));
	if (Target == 0)
	{
		TestTrue(TEXT("A second stick press completes"), Driver->Sequence().Navigate(EDreamUINavigationDirection::Right).Perform());
		Target = Radios.IndexOf(Rig.EventSystem()->GetHighlightedComponentForNavigation(0));
	}
	if (!TestTrue(FString::Printf(TEXT("The pad's focus is on a radio that is not chosen (radio %d)"), Target), Target == 1 || Target == 2))
	{
		return false;
	}
	TestEqual(TEXT("Moving the focus chose nothing"), CountAnnouncements(Listeners), 0);
	TestTrue(TEXT("...and the first radio is still chosen"), Radios.Radios[0]->IsChecked());

	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), Driver->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());

	TestTrue(TEXT("The confirm chose the radio the pad was on"), Radios.Radios[Target]->IsChecked());
	TestFalse(TEXT("...and the radio that was chosen let go"), Radios.Radios[0]->IsChecked());
	TestEqual(TEXT("The chosen radio announced one change"), Listeners[Target]->CheckStates.Num(), 1);
	TestEqual(TEXT("Two announcements in all: the one chosen and the one let go"), CountAnnouncements(Listeners), 2);
	return true;
}

#endif
