// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

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
 * UUIToggleGroup with "none" allowed, clicked through the real pointer pipeline.
 *
 * The UMG counterpart is CommonUI's button group with bSelectionRequired off (CommonUI/Public/Groups/CommonButtonGroupBase.h:141),
 * over toggleable buttons: a click on a toggleable button flips its selection (UCommonButtonBase::HandleButtonClicked,
 * CommonUI/Private/CommonButtonBase.cpp:1525-1550, and SetIsSelected, :1041-1050, which lets a toggleable button go), and a
 * group that does not require a selection lets the chosen one go with nothing taking its place.
 */
namespace DreamToggleGroupTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FToggleRow
	{
		UDreamWidget* Panel = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamToggle*> Toggles;

		bool IsReady() const { return Panel != nullptr && Group != nullptr && Toggles.Num() == 3 && !Toggles.Contains(nullptr); }
	};

	/** Three toggles in a row under one group that allows none, the second chosen before anyone listens. */
	FToggleRow MakeToggleRow(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FToggleRow Made;
		Made.Panel = InRig.MakeWidget(TEXT("Filters"), nullptr, FVector2D(400.0, 100.0));
		Made.Group = Made.Panel != nullptr ? Made.Panel->AddComponent<UUIToggleGroup>() : nullptr;
		if (!InTest.TestNotNull(TEXT("A panel with a toggle group can be made on the rig"), Made.Group))
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(true);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Filters_Toggle%d"), Index), Made.Panel,
				FVector2D(40.0, 40.0), FVector2D(-120.0 + 120.0 * Index, 0.0));
			if (Toggle != nullptr && Toggle->ToggleBehaviour != nullptr)
			{
				Toggle->ToggleBehaviour->SetToggleGroup(Made.Group);
			}
			Made.Toggles.Add(Toggle);
		}
		if (InTest.TestTrue(TEXT("Three toggles were made in the group"), Made.IsReady()))
		{
			Made.Toggles[1]->SetIsChecked(true);
		}
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupNoneAllowedTest,
	"DreamGUI.ToggleGroup.WithNoneAllowedClickingTheChosenToggleAgainLeavesNothingChosen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupNoneAllowedTest, "DreamGUI.ToggleGroup.WithNoneAllowedClickingTheChosenToggleAgainLeavesNothingChosen", "[Pointer][Animated]")

/*
 * The chosen toggle clicked again lets go -- unchecked, said once -- and the group holds no choice; the other two stay
 * unchecked and say nothing. A click on another toggle afterwards chooses it as from scratch: nothing else had the choice to
 * give up, so it alone announces.
 */
bool FDreamToggleGroupNoneAllowedTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(*this, Rig);
	if (!Row.IsReady())
	{
		return false;
	}
	TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;
	for (UDreamToggle* Toggle : Row.Toggles)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
		Listeners.Add(MoveTemp(Listener));
	}
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The second toggle is the group's choice to begin with"), Row.Group->GetSelectedItem(), Row.Toggles[1]->ToggleBehaviour.Get()))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the chosen toggle completes"), Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[1]))->Click());

	TestFalse(TEXT("With none allowed, the chosen toggle lets go"), Row.Toggles[1]->IsChecked());
	TestNull(TEXT("...and the group holds no choice"), Row.Group->GetSelectedItem());
	if (TestEqual(TEXT("It announced one change"), Listeners[1]->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("To unchecked"), Listeners[1]->CheckStates[0], EDreamCheckState::Unchecked);
	}
	TestEqual(TEXT("The other two said nothing"), Listeners[0]->CheckStates.Num() + Listeners[2]->CheckStates.Num(), 0);

	TestTrue(TEXT("Clicking the third toggle completes"), Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[2]))->Click());
	TestTrue(TEXT("The third toggle is chosen"), Row.Toggles[2]->IsChecked());
	TestEqual(TEXT("...and is the group's choice"), Row.Group->GetSelectedItem(), Row.Toggles[2]->ToggleBehaviour.Get());
	TestFalse(TEXT("The second stays unchecked"), Row.Toggles[1]->IsChecked());
	TestEqual(TEXT("Only the third announced anything this time"), Listeners[2]->CheckStates.Num(), 1);
	TestEqual(TEXT("...the second nothing more"), Listeners[1]->CheckStates.Num(), 1);
	return true;
}

#endif
