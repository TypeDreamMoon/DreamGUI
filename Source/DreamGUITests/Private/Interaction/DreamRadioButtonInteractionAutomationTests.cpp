// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRadioButton.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRadioButton, clicked through the real pointer pipeline, in the arrangement its header names
 * as the one that makes a radio a radio: a UUIToggleGroup on a shared parent, with none-selected
 * disallowed. UMG has no radio widget of its own (a UMG radio group is check boxes bound to one
 * value), so the rules asserted here are the ones this library's own header states -- clicking one
 * radio of a group selects it and deselects the one that was selected; clicking the selected one in a
 * group that forbids "none" changes nothing.
 *
 * Each radio gets a listener of its own, because "which radio announced what" is the whole claim.
 */
namespace DreamPressRadioTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A panel with a toggle group on it, and three radios in a row inside it. */
	struct FRadioGroup
	{
		UDreamWidget* Panel = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamRadioButton*> Radios;

		bool IsReady() const { return Panel != nullptr && Group != nullptr && Radios.Num() == 3 && !Radios.Contains(nullptr); }
	};

	/**
	 * One group on the rig. Membership goes through SetToggleGroup, the call the radio's header gives
	 * for wiring a group from code after Initialize -- the automatic search it would otherwise make
	 * runs at begin play, which a headless world never reaches. The radio named by InCheckedIndex is
	 * checked through the same public setter a game would use; nothing is listening yet.
	 */
	FRadioGroup MakeRadioGroup(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName,
		const FVector2D& InPanelPosition, int32 InCheckedIndex)
	{
		FRadioGroup Made;
		Made.Panel = InRig.MakeWidget(InName, nullptr, FVector2D(400.0, 100.0), InPanelPosition);
		if (!InTest.TestNotNull(TEXT("A panel for the group can be made on the rig"), Made.Panel))
		{
			return Made;
		}
		Made.Group = Made.Panel->AddComponent<UUIToggleGroup>();
		if (!InTest.TestNotNull(TEXT("The panel carries a toggle group"), Made.Group))
		{
			return Made;
		}
		// The rule a radio group exists for: one is always chosen once one has been.
		Made.Group->SetAllowNoneSelected(false);

		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FString RadioName = FString::Printf(TEXT("%s_Radio%d"), *InName, Index);
			UDreamRadioButton* Radio = InRig.MakeControl<UDreamRadioButton>(RadioName, Made.Panel,
				FVector2D(40.0, 40.0), FVector2D(-120.0 + 120.0 * Index, 0.0));
			if (Radio != nullptr)
			{
				Radio->SetToggleGroup(Made.Group);
			}
			Made.Radios.Add(Radio);
		}
		if (InTest.TestTrue(TEXT("Three radios were made in the group"), Made.IsReady())
			&& Made.Radios.IsValidIndex(InCheckedIndex))
		{
			Made.Radios[InCheckedIndex]->SetIsChecked(true);
		}
		return Made;
	}

	/** One listener per radio, bound after the starting selection was made so only clicks are counted. */
	void Listen(const FRadioGroup& InGroup, TArray<TStrongObjectPtr<UDreamPressInteractionListener>>& OutListeners)
	{
		for (UDreamRadioButton* Radio : InGroup.Radios)
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			Radio->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			OutListeners.Add(MoveTemp(Listener));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressRadioExclusiveTest,
	"DreamGUI.RadioButton.ClickingAnotherRadioInTheGroupSelectsItAndDeselectsTheOldOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressRadioExclusiveTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressRadioTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(*this, Rig, TEXT("Quality"), FVector2D::ZeroVector, 0);
	if (!Radios.IsReady())
	{
		return false;
	}
	Listen(Radios, Listeners);
	Rig.PumpFrames(1);

	FDreamElementRef Second = Rig.Driver()->Find(FDreamBy::Widget(Radios.Radios[1]));
	TestTrue(TEXT("Clicking the second radio completes"), Second->Click());

	TestFalse(TEXT("The first radio is no longer chosen"), Radios.Radios[0]->IsChecked());
	TestTrue(TEXT("The second radio is chosen"), Radios.Radios[1]->IsChecked());
	TestFalse(TEXT("The third radio was never chosen"), Radios.Radios[2]->IsChecked());

	if (TestEqual(TEXT("The second radio announced one change"), Listeners[1]->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("To Checked"), Listeners[1]->CheckStates[0], EDreamCheckState::Checked);
	}
	if (TestEqual(TEXT("The radio that lost the selection announced one change"), Listeners[0]->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("To Unchecked"), Listeners[0]->CheckStates[0], EDreamCheckState::Unchecked);
	}
	TestEqual(TEXT("The bystander announced nothing"), Listeners[2]->CheckStates.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressRadioReclickTest,
	"DreamGUI.RadioButton.ClickingTheChosenRadioAgainKeepsItChosenAndSaysNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressRadioReclickTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressRadioTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(*this, Rig, TEXT("Quality"), FVector2D::ZeroVector, 1);
	if (!Radios.IsReady())
	{
		return false;
	}
	Listen(Radios, Listeners);
	Rig.PumpFrames(1);

	FDreamElementRef Chosen = Rig.Driver()->Find(FDreamBy::Widget(Radios.Radios[1]));
	TestTrue(TEXT("Clicking the chosen radio completes"), Chosen->Click());

	TestTrue(TEXT("In a group that forbids none, the chosen radio stays chosen"), Radios.Radios[1]->IsChecked());
	TestEqual(TEXT("And, having not changed, it announces nothing"), Listeners[1]->CheckStates.Num(), 0);
	TestEqual(TEXT("Nor does anything else in the group"), Listeners[0]->CheckStates.Num() + Listeners[2]->CheckStates.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPressRadioSeparateGroupsTest,
	"DreamGUI.RadioButton.AClickInOneGroupLeavesAnotherGroupsChoiceAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPressRadioSeparateGroupsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPressRadioTestLocal;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> QualityListeners;
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> ModeListeners;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Two panels, one above the other, each with its own group and its own starting choice.
	const FRadioGroup Quality = MakeRadioGroup(*this, Rig, TEXT("Quality"), FVector2D(0.0, 120.0), 0);
	const FRadioGroup Mode = MakeRadioGroup(*this, Rig, TEXT("Mode"), FVector2D(0.0, -120.0), 0);
	if (!Quality.IsReady() || !Mode.IsReady())
	{
		return false;
	}
	Listen(Quality, QualityListeners);
	Listen(Mode, ModeListeners);
	Rig.PumpFrames(1);

	FDreamElementRef QualityThird = Rig.Driver()->Find(FDreamBy::Widget(Quality.Radios[2]));
	TestTrue(TEXT("Clicking the third radio of the first group completes"), QualityThird->Click());

	TestTrue(TEXT("The click chose in its own group"), Quality.Radios[2]->IsChecked());
	TestFalse(TEXT("And moved its own group's choice"), Quality.Radios[0]->IsChecked());
	TestTrue(TEXT("The other group's choice is where it was"), Mode.Radios[0]->IsChecked());
	int32 ModeAnnouncements = 0;
	for (const TStrongObjectPtr<UDreamPressInteractionListener>& Listener : ModeListeners)
	{
		ModeAnnouncements += Listener->CheckStates.Num();
	}
	TestEqual(TEXT("And nothing in the other group announced anything"), ModeAnnouncements, 0);
	return true;
}

#endif
