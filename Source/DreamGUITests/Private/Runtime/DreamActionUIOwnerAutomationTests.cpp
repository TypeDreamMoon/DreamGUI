// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "DreamNavigationTestTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIActionBar.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIActionTrigger.h"
#include "Interaction/DreamUIInputAction.h"

#include "Driver/DreamDriverRig.h"

/*
 * WHOSE KEY, WHOSE PROMPTS: THE OWNER'S.
 *
 * An action trigger and an action bar used to answer to player 0 unless somebody set their index by hand, so a
 * button on the second player's half of a split screen answered the first player's key, and its prompts showed on
 * the first player's bar. Both now default to -1, the player who owns the widget they sit on -- found the way every
 * widget finds it, through the user widget hosting it -- and an index written on them still wins. The bar also
 * rebuilds when the player picks up another make of pad, whose glyphs differ, not only when the device changes.
 *
 * Which player owns a widget is its local player's place in the game instance, and a local player can only say so
 * through a game viewport client (ULocalPlayer::GetGameInstance), which the headless rig does not have: every widget
 * here is the first player's. So the owner is read off the widget and the other player is any other index -- the
 * resolution a second player's half of a split screen gets too, short of a two-player PIE session.
 */
namespace DreamActionUIOwnerTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const TCHAR* const ConfirmName = TEXT("Confirm");

	UDataTable* MakeConfirmTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromString(ConfirmName);
		Row.KeyboardKey = EKeys::Enter;
		Row.GamepadKey = EKeys::Gamepad_FaceButton_Bottom;
		Table->AddRow(ConfirmName, Row);
		return Table;
	}

	FDataTableRowHandle MakeHandle(UDataTable* InTable, FName InRowName)
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = InTable;
		Handle.RowName = InRowName;
		return Handle;
	}

	int32 CountBindings(const UDreamUIActionRouter* InRouter, int32 InUserIndex)
	{
		TArray<FDreamUIActionBinding> Bindings;
		InRouter->GetDisplayBindings(InUserIndex, Bindings);
		return Bindings.Num();
	}

	/** Whether the bar shows the confirm action -- by its name, which is the same whichever device's key it names. */
	bool ShowsConfirm(const UDreamUIActionBar* InBar)
	{
		for (const FDreamUIActionBinding& Prompt : InBar->GetPrompts())
		{
			if (Prompt.DisplayName.ToString() == ConfirmName)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionUIFollowsOwnerTest,
	"DreamGUI.Navigation.ActionBar.TheTriggerAndTheBarAnswerToThePlayerWhoOwnsThemUnlessToldOtherwise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A screen with a button carrying a trigger and a bar, neither given an index. Both answer to the player who owns
 * them: the trigger's binding is that player's -- another player has none -- and the screen's bar shows it. An index
 * written on the trigger moves the binding to that player, and the owner's bar lets go of its prompt; -1 hands both
 * back to the owner. An index written on the bar shows that player's prompts.
 */
bool FDreamActionUIFollowsOwnerTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionUIOwnerTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamWidget* Screen = Rig.MakeWidget(TEXT("Screen"), nullptr, FVector2D(600.0, 400.0));
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The rig, the screen and the action router came up"), Rig.IsUsable() && Screen != nullptr && Router != nullptr))
	{
		return false;
	}
	UDreamWidget* ButtonWidget = Rig.MakeWidget(TEXT("Confirm"), Screen, FVector2D(160.0, 40.0));
	UDreamWidget* BarWidget = Rig.MakeWidget(TEXT("Prompts"), Screen, FVector2D(400.0, 40.0), FVector2D(0.0, -150.0));
	UDreamUIActionTrigger* Trigger = ButtonWidget != nullptr ? ButtonWidget->AddComponent<UDreamUIActionTrigger>() : nullptr;
	UDreamUIActionBar* Bar = BarWidget != nullptr ? BarWidget->AddComponent<UDreamUIActionBar>() : nullptr;
	if (!TestTrue(TEXT("The trigger and the bar went onto the screen"), Trigger != nullptr && Bar != nullptr))
	{
		return false;
	}
	const int32 Owner = ButtonWidget->GetOwningPlayerIndex();
	const int32 Other = Owner == 0 ? 1 : 0;
	TestEqual(TEXT("The trigger answers to the player who owns its widget"), Trigger->GetUserIndex(), Owner);
	TestEqual(TEXT("...and so does the bar"), Bar->GetUserIndex(), BarWidget->GetOwningPlayerIndex());

	UDataTable* Table = MakeConfirmTable();
	Trigger->SetAction(MakeHandle(Table, ConfirmName));
	// A trigger binds as its widget comes alive, and this one was alive before it had an action.
	ButtonWidget->SetWidgetActive(false);
	ButtonWidget->SetWidgetActive(true);
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The trigger bound its action"), Trigger->IsActionBound()))
	{
		return false;
	}
	TestEqual(TEXT("The binding is the owner's"), CountBindings(Router, Owner), 1);
	TestEqual(TEXT("...and not another player's"), CountBindings(Router, Other), 0);
	TestTrue(TEXT("The screen's bar shows it"), ShowsConfirm(Bar));

	Trigger->SetUserIndex(Other);
	TestEqual(TEXT("An index written on the trigger wins"), Trigger->GetUserIndex(), Other);
	TestEqual(TEXT("...moving the binding to that player"), CountBindings(Router, Other), 1);
	TestEqual(TEXT("...off the owner"), CountBindings(Router, Owner), 0);
	TestFalse(TEXT("...and off the owner's bar"), ShowsConfirm(Bar));
	Trigger->SetUserIndex(-1);
	TestEqual(TEXT("-1 hands it back to the owner"), Trigger->GetUserIndex(), Owner);
	TestEqual(TEXT("...and the binding with it"), CountBindings(Router, Owner), 1);
	TestTrue(TEXT("...back on the owner's bar"), ShowsConfirm(Bar));
	Bar->SetUserIndex(Other);
	TestEqual(TEXT("An index written on the bar wins too"), Bar->GetUserIndex(), Other);
	TestTrue(TEXT("...and it shows that player's prompts, which are none"), Bar->GetPrompts().Num() == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionBarPadModelTest,
	"DreamGUI.Navigation.ActionBar.RebuildsWhenThePlayerPicksUpAnotherModelOfPad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The glyph a prompt shows is per make of pad, and swapping one pad for another keeps the device a pad: the bar
 * used to rebuild on the device alone and went on showing the old pad's glyphs.
 */
bool FDreamActionBarPadModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionUIOwnerTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	// The event system registered as player 0's before the bar looks for it.
	Rig.EnsureGameInputHost();
	UDreamWidget* BarWidget = Rig.MakeWidget(TEXT("Prompts"), nullptr, FVector2D(400.0, 40.0));
	UDreamCountingActionBar* Bar = BarWidget != nullptr ? BarWidget->AddComponent<UDreamCountingActionBar>() : nullptr;
	UDreamEventSystem* Events = Rig.EventSystem();
	if (!TestTrue(TEXT("The rig, the bar and the event system came up"), Rig.IsUsable() && Bar != nullptr && Events != nullptr))
	{
		return false;
	}
	// Subscribed whether or not the widget's begin play reached the bar: subscribing again replaces the first.
	Bar->ForceEnable();
	const EDreamUIGamepadModel Other = Events->GetCurrentGamepadModel() == EDreamUIGamepadModel::PlayStation
		? EDreamUIGamepadModel::Xbox : EDreamUIGamepadModel::PlayStation;
	const int32 Before = Bar->RebuildCount;
	Events->SetGamepadModelOverride(true, Other);
	TestTrue(TEXT("Another model of pad rebuilt the bar"), Bar->RebuildCount > Before);
	const int32 AfterSwap = Bar->RebuildCount;
	Events->SetGamepadModelOverride(true, Other);
	TestEqual(TEXT("The same model again rebuilds nothing"), Bar->RebuildCount, AfterSwap);
	Events->SetGamepadModelOverride(false);
	return true;
}

#endif
