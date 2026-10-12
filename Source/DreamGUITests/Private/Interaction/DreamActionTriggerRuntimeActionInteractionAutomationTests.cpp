// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/DataTable.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionTrigger.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * AN ACTION NAMED ON A BUTTON WHILE THE GAME RUNS.
 *
 * CommonUI's button binds the action it is handed the moment it is handed it: UCommonButtonBase::SetTriggeringInputAction
 * unbinds the old one and binds the new one there and then (CommonUI/Private/CommonButtonBase.cpp:1394-1407, through
 * BindTriggeringInputActionToClick), so a screen that picks a button's action at run time -- a prompt that changes with the
 * selection, a key the player rebound -- has the new key click the button at once, the old key click it no more, and an
 * action cleared leave no key. The router only offers a key to a binding whose widget can take input
 * (FActionRouterBindingCollection::IsWidgetReachableForInput and CanWidgetReceiveInput, CommonUI/Private/Input/
 * UIActionRouterTypes.cpp:111-116), so on a disabled or hidden button the key clicks nothing until the button is back.
 *
 * DreamGUI's UDreamUIActionTrigger beside a button is that pairing ("the binding is registered when the widget goes live and
 * dropped the moment it stops being"): an action named on a live button is bound at once, and one named on a button that is
 * not live is bound when the button comes back.
 */
namespace DreamActionTriggerRuntimeTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Two actions: Confirm on X and the pad's left face button, Inspect on Z and the pad's top face button. */
	struct FActionTable
	{
		TStrongObjectPtr<UDataTable> Table;

		FActionTable()
			: Table(NewObject<UDataTable>(GetTransientPackage()))
		{
			Table->RowStruct = FDreamUIInputActionData::StaticStruct();
			FDreamUIInputActionData Confirm;
			Confirm.DisplayName = FText::FromString(TEXT("Confirm"));
			Confirm.KeyboardKey = EKeys::X;
			Confirm.GamepadKey = EKeys::Gamepad_FaceButton_Left;
			Table->AddRow(TEXT("Confirm"), Confirm);
			FDreamUIInputActionData Inspect;
			Inspect.DisplayName = FText::FromString(TEXT("Inspect"));
			Inspect.KeyboardKey = EKeys::Z;
			Inspect.GamepadKey = EKeys::Gamepad_FaceButton_Top;
			Table->AddRow(TEXT("Inspect"), Inspect);
		}

		FDataTableRowHandle Row(const TCHAR* InName) const
		{
			FDataTableRowHandle Handle;
			Handle.DataTable = Table.Get();
			Handle.RowName = InName;
			return Handle;
		}
	};

	/** A button on the screen whose face carries a click watcher counting into OutClicks, and an action trigger with no action. */
	UDreamUIActionTrigger* PlaceButtonWithTrigger(FAutomationTestBase& InTest, FDreamDriverRig& InRig, int32& OutClicks, UDreamButton*& OutButton)
	{
		OutButton = InRig.MakeControl<UDreamButton>(TEXT("Accept"), nullptr, FVector2D(160.0, 40.0), FVector2D::ZeroVector);
		UUIEventTrigger* ClickWatcher = OutButton != nullptr && OutButton->FaceNode != nullptr ? OutButton->FaceNode->AddComponent<UUIEventTrigger>() : nullptr;
		// Beside the button's face, where its click is taken.
		UDreamUIActionTrigger* Trigger = ClickWatcher != nullptr ? OutButton->FaceNode->AddComponent<UDreamUIActionTrigger>() : nullptr;
		if (!InTest.TestNotNull(TEXT("A button whose face carries a click watcher and an action trigger came up"), Trigger))
		{
			return nullptr;
		}
		int32* Clicks = &OutClicks;
		ClickWatcher->GetOnPointerClickEvent().AddLambda([Clicks](UDreamPointerEventData*) { ++*Clicks; });
		InRig.PumpFrames(1);
		return Trigger;
	}

	/** Each of InKeys pressed and let go through the rig's input host, a frame each. */
	bool PressKeys(FDreamDriverRig& InRig, const TArray<FKey>& InKeys)
	{
		FDreamDriverSequence Sequence = InRig.Driver()->Sequence();
		for (const FKey& Key : InKeys)
		{
			Sequence.Key(Key).WaitFrames(1);
		}
		return Sequence.Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionTriggerRuntimeLiveTest,
	"DreamGUI.Navigation.Actions.AnActionNamedOnALiveButtonWhileTheGameRunsIsItsKeyAtOnceAndChangingOrClearingItMovesOrDropsTheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamActionTriggerRuntimeLiveTest, "DreamGUI.Navigation.Actions.AnActionNamedOnALiveButtonWhileTheGameRunsIsItsKeyAtOnceAndChangingOrClearingItMovesOrDropsTheKey", "[Nav][Animated]")

/*
 * A button up on the screen, its trigger with no action: X clicks nothing. Confirm named on it: bound at once, and X and the
 * pad's left face button each click it, with no frame between the naming and the first key. Inspect named in its place: X
 * and the pad's left face button click nothing, Z and the pad's top face button click it. The action cleared: nothing is
 * bound, and none of the four keys clicks it.
 */
bool FDreamActionTriggerRuntimeLiveTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionTriggerRuntimeTestLocal;
	// The clicks the keys make, counted where they land, as the router's own tests count them. Declared before the rig.
	int32 Clicks = 0;
	const FActionTable Actions;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = nullptr;
	UDreamUIActionTrigger* Trigger = PlaceButtonWithTrigger(*this, Rig, Clicks, Button);
	if (Trigger == nullptr)
	{
		return false;
	}
	TestFalse(TEXT("A trigger with no action binds nothing"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X completes"), PressKeys(Rig, { EKeys::X }));
	TestEqual(TEXT("With no action, X clicks nothing"), Clicks, 0);

	Trigger->SetAction(Actions.Row(TEXT("Confirm")));
	TestTrue(TEXT("Confirm named on the live button is bound at once"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X at once completes"), PressKeys(Rig, { EKeys::X }));
	TestEqual(TEXT("X clicked the button"), Clicks, 1);
	TestTrue(TEXT("Pressing the pad's left face button completes"), PressKeys(Rig, { EKeys::Gamepad_FaceButton_Left }));
	TestEqual(TEXT("...and the pad's key once more"), Clicks, 2);

	Trigger->SetAction(Actions.Row(TEXT("Inspect")));
	TestTrue(TEXT("Inspect named in Confirm's place is bound at once"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X and the pad's left face button completes"), PressKeys(Rig, { EKeys::X, EKeys::Gamepad_FaceButton_Left }));
	TestEqual(TEXT("Confirm's keys click it no more"), Clicks, 2);
	TestTrue(TEXT("Pressing Z completes"), PressKeys(Rig, { EKeys::Z }));
	TestEqual(TEXT("Z, Inspect's key, clicks it"), Clicks, 3);
	TestTrue(TEXT("Pressing the pad's top face button completes"), PressKeys(Rig, { EKeys::Gamepad_FaceButton_Top }));
	TestEqual(TEXT("...and the pad's key once more"), Clicks, 4);

	Trigger->SetAction(FDataTableRowHandle());
	TestFalse(TEXT("The action cleared, nothing is bound"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing all four keys completes"),
		PressKeys(Rig, { EKeys::X, EKeys::Gamepad_FaceButton_Left, EKeys::Z, EKeys::Gamepad_FaceButton_Top }));
	TestEqual(TEXT("None of them clicks it"), Clicks, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionTriggerRuntimeAsleepTest,
	"DreamGUI.Navigation.Actions.AnActionNamedOnADisabledOrHiddenButtonWhileTheGameRunsIsBoundOnlyOnceTheButtonIsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamActionTriggerRuntimeAsleepTest, "DreamGUI.Navigation.Actions.AnActionNamedOnADisabledOrHiddenButtonWhileTheGameRunsIsBoundOnlyOnceTheButtonIsBack", "[Nav][Disabled]")

/*
 * A button up on the screen, its trigger with no action. Disabled, Confirm named on it binds nothing, and X clicks nothing;
 * enabled again, Confirm is bound and X clicks it. Its face hidden, Inspect named in Confirm's place binds nothing, and
 * neither X nor Z clicks it; shown again, Inspect is bound, Z clicks it and X does not.
 */
bool FDreamActionTriggerRuntimeAsleepTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionTriggerRuntimeTestLocal;
	int32 Clicks = 0;
	const FActionTable Actions;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Button = nullptr;
	UDreamUIActionTrigger* Trigger = PlaceButtonWithTrigger(*this, Rig, Clicks, Button);
	if (Trigger == nullptr)
	{
		return false;
	}

	Button->SetIsEnabled(false);
	Rig.PumpFrames(1);
	Trigger->SetAction(Actions.Row(TEXT("Confirm")));
	TestFalse(TEXT("Confirm named on the disabled button binds nothing"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X completes"), PressKeys(Rig, { EKeys::X }));
	TestEqual(TEXT("X clicks the disabled button nothing"), Clicks, 0);
	Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Enabled again, Confirm is bound"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X completes"), PressKeys(Rig, { EKeys::X }));
	TestEqual(TEXT("...and X clicks it"), Clicks, 1);

	Button->FaceNode->SetWidgetActive(false);
	Rig.PumpFrames(1);
	TestFalse(TEXT("Hidden, the face lets go of Confirm"), Trigger->IsActionBound());
	Trigger->SetAction(Actions.Row(TEXT("Inspect")));
	TestFalse(TEXT("Inspect named on the hidden face binds nothing"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing X and Z completes"), PressKeys(Rig, { EKeys::X, EKeys::Z }));
	TestEqual(TEXT("Neither clicks the hidden button"), Clicks, 1);
	Button->FaceNode->SetWidgetActive(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Shown again, Inspect is bound"), Trigger->IsActionBound());
	TestTrue(TEXT("Pressing Z completes"), PressKeys(Rig, { EKeys::Z }));
	TestEqual(TEXT("...and Z clicks it"), Clicks, 2);
	TestTrue(TEXT("Pressing X completes"), PressKeys(Rig, { EKeys::X }));
	TestEqual(TEXT("...and X, Confirm's key, does not"), Clicks, 2);
	return true;
}

#endif
