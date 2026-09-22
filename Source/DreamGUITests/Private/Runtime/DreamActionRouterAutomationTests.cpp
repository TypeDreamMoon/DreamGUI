// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUIInputAction.h"
#include "DreamNavigationTestTypes.h"
#include "DreamPointerEventTestTypes.h"
#include "Interaction/UIEventTrigger.h"
#include "Event/DreamPointerEventData.h"
#include "InputAction.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

/*
 * Which screen a key belongs to.
 *
 * The preset's keys used to be a static array in its cpp: a project could not add an action, rebind
 * one, or draw a prompt for one without editing the plugin, and nothing knew that a key pressed while
 * a dialog is open belongs to the dialog. Bindings live and die with the screen that made them, and
 * only the screen in front is offered the key.
 */

namespace DreamActionRouterTestLocal
{
	using DreamTests::FScopedGameWorld;

	UDataTable* MakeActionTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		return Table;
	}

	void AddAction(UDataTable* Table, FName RowName, const FKey& KeyboardKey, const FKey& GamepadKey, float HoldTime = 0.0f)
	{
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromName(RowName);
		Row.KeyboardKey = KeyboardKey;
		Row.GamepadKey = GamepadKey;
		Row.HoldTime = HoldTime;
		Table->AddRow(RowName, Row);
	}

	FDataTableRowHandle MakeHandle(UDataTable* Table, FName RowName)
	{
		FDataTableRowHandle Handle;
		Handle.DataTable = Table;
		Handle.RowName = RowName;
		return Handle;
	}

	UDreamUINavigationScope* MakeScope(UWorld* World, const TCHAR* Name)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		UDreamUINavigationScope* Scope = Widget->AddComponent<UDreamUINavigationScope>();
		Scope->SetActivateWhenEnabled(false);
		return Scope;
	}

	FDreamUIActionExecutedDelegate BindTo(UDreamActionCallCounter* Counter)
	{
		FDreamUIActionExecutedDelegate Delegate;
		Delegate.BindUFunction(Counter, TEXT("Fire"));
		return Delegate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterScopeOwnershipTest,
	"DreamGUI.Navigation.Actions.OnlyTheScreenInFrontIsOfferedTheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterScopeOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Delete"), EKeys::Delete, EKeys::Gamepad_FaceButton_Left);

	UDreamUINavigationScope* List = MakeScope(TestWorld.World, TEXT("List"));
	UDreamUINavigationScope* Dialog = MakeScope(TestWorld.World, TEXT("Dialog"));
	UDreamActionCallCounter* ListDelete = NewObject<UDreamActionCallCounter>();
	UDreamActionCallCounter* DialogDelete = NewObject<UDreamActionCallCounter>();

	Router->RegisterAction(List, MakeHandle(Table, TEXT("Delete")), BindTo(ListDelete));
	Router->RegisterAction(Dialog, MakeHandle(Table, TEXT("Delete")), BindTo(DialogDelete));

	// Registered but nothing is open: a binding belongs to a screen, so with no screen in front there
	// is nobody the key belongs to.
	TestFalse(TEXT("With no screen open the key goes unclaimed"), Router->HandleKey(0, EKeys::Delete, true));
	TestEqual(TEXT("...and nothing fired"), ListDelete->CallCount, 0);

	List->ActivateScope();
	TestTrue(TEXT("The open list takes Delete"), Router->HandleKey(0, EKeys::Delete, true));
	TestEqual(TEXT("...and only the list's"), ListDelete->CallCount, 1);
	TestEqual(TEXT("...not the dialog's"), DialogDelete->CallCount, 0);

	// This is the whole point: while the dialog is up, Delete means the dialog's Delete.
	Dialog->ActivateScope();
	Router->HandleKey(0, EKeys::Delete, true);
	TestEqual(TEXT("The dialog in front takes it instead"), DialogDelete->CallCount, 1);
	TestEqual(TEXT("...and the list underneath does not"), ListDelete->CallCount, 1);

	Dialog->DeactivateScope();
	Router->HandleKey(0, EKeys::Delete, true);
	TestEqual(TEXT("Closing it hands the key back"), ListDelete->CallCount, 2);

	// Either spelling reaches the same binding: the key itself says which device produced it, and a
	// player with a pad plugged in can still reach over to the keyboard.
	Router->HandleKey(0, EKeys::Gamepad_FaceButton_Left, true);
	TestEqual(TEXT("The gamepad spelling reaches it too"), ListDelete->CallCount, 3);

	TestFalse(TEXT("An unbound key is left alone"), Router->HandleKey(0, EKeys::F9, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterGlobalAndUserTest,
	"DreamGUI.Navigation.Actions.GlobalBindingsYieldToTheScreenInFront",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterGlobalAndUserTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Menu"), EKeys::M, EKeys::Gamepad_Special_Right);

	UDreamActionCallCounter* Global = NewObject<UDreamActionCallCounter>();
	UDreamActionCallCounter* Screen = NewObject<UDreamActionCallCounter>();
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Menu")), BindTo(Global));

	// A global binding works with nothing open, which is what makes it global.
	TestTrue(TEXT("A global binding answers with no screen open"), Router->HandleKey(0, EKeys::M, true));
	TestEqual(TEXT("...and fires"), Global->CallCount, 1);

	UDreamUINavigationScope* Page = MakeScope(TestWorld.World, TEXT("Page"));
	Router->RegisterAction(Page, MakeHandle(Table, TEXT("Menu")), BindTo(Screen));
	Page->ActivateScope();
	Router->HandleKey(0, EKeys::M, true);
	TestEqual(TEXT("An open screen outranks the global binding"), Screen->CallCount, 1);
	TestEqual(TEXT("...which stays silent"), Global->CallCount, 1);

	// Players do not share bindings; player 0's key must not fire player 1's action.
	UDreamActionCallCounter* SecondPlayer = NewObject<UDreamActionCallCounter>();
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Menu")), BindTo(SecondPlayer), 1);
	Router->HandleKey(0, EKeys::M, true);
	TestEqual(TEXT("Player 1's binding is untouched by player 0's key"), SecondPlayer->CallCount, 0);
	Router->HandleKey(1, EKeys::M, true);
	TestEqual(TEXT("...and answers its own"), SecondPlayer->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterHoldTest,
	"DreamGUI.Navigation.Actions.HoldFiresWhenTheTimeIsUpNotOnRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterHoldTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("HoldToDelete"), EKeys::X, EKeys::Gamepad_FaceButton_Left, 1.0f);
	UDreamActionCallCounter* Counter = NewObject<UDreamActionCallCounter>();
	const FDreamUIActionHandle Handle = Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("HoldToDelete")), BindTo(Counter));

	TestTrue(TEXT("The press is taken"), Router->HandleKey(0, EKeys::X, true));
	TestEqual(TEXT("...but nothing has happened yet"), Counter->CallCount, 0);

	Router->Tick(0.25f);
	TestEqual(TEXT("A quarter of the way through, still nothing"), Counter->CallCount, 0);
	// The progress is what a filling ring is drawn from, so it has to be readable mid-hold rather than
	// only at the end.
	TestEqual(TEXT("...and the progress says a quarter"), Router->GetHoldProgress(Handle), 0.25f);

	Router->Tick(0.8f);
	// Fires at the threshold, not on release: a hold-to-confirm that waits for the release cannot show
	// a filled ring and then act on it.
	TestEqual(TEXT("Past the threshold it fires"), Counter->CallCount, 1);
	Router->Tick(1.0f);
	TestEqual(TEXT("One press fires once, however long it is held"), Counter->CallCount, 1);

	// Letting go early is a cancel.
	Router->HandleKey(0, EKeys::X, false);
	Router->HandleKey(0, EKeys::X, true);
	Router->Tick(0.5f);
	Router->HandleKey(0, EKeys::X, false);
	Router->Tick(1.0f);
	TestEqual(TEXT("Released before the threshold, nothing fires"), Counter->CallCount, 1);
	TestEqual(TEXT("...and the progress is back to nothing"), Router->GetHoldProgress(Handle), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterDisplayTest,
	"DreamGUI.Navigation.Actions.PromptsListWhatTheKeyWouldActuallyDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterDisplayTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Confirm"), EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom);
	AddAction(Table, TEXT("Hidden"), EKeys::H, EKeys::Gamepad_FaceButton_Top);
	UDreamActionCallCounter* Counter = NewObject<UDreamActionCallCounter>();

	UDreamUINavigationScope* Page = MakeScope(TestWorld.World, TEXT("Page"));
	Router->RegisterAction(Page, MakeHandle(Table, TEXT("Confirm")), BindTo(Counter));
	Router->RegisterAction(Page, MakeHandle(Table, TEXT("Hidden")), BindTo(Counter), 0, false);

	TArray<FDreamUIActionBinding> Prompts;
	Router->GetDisplayBindings(0, Prompts);
	TestEqual(TEXT("A closed screen advertises nothing"), Prompts.Num(), 0);

	Page->ActivateScope();
	Router->GetDisplayBindings(0, Prompts);
	// Exactly one: the caller asked for the second not to be advertised, and a prompt for something
	// hidden is a prompt for something the player was never meant to know about.
	TestEqual(TEXT("Only the advertised action is listed"), Prompts.Num(), 1);
	if (Prompts.Num() == 1)
	{
		TestEqual(TEXT("...with the keyboard spelling, which is the default device"), Prompts[0].Key, EKeys::Enter);
	}

	Page->DeactivateScope();
	Router->GetDisplayBindings(0, Prompts);
	TestEqual(TEXT("Closing the screen takes its prompts with it"), Prompts.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterModifierTest,
	"DreamGUI.Navigation.Actions.AChordIsADifferentActionFromItsBareKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterModifierTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	// Ctrl+S and S. Until actions could carry modifiers there was no way to spell the first of these:
	// MatchesKey compared the FKey alone, so the two were the same binding and one of them had to go.
	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Save"), EKeys::S, FKey());
	AddAction(Table, TEXT("SaveAs"), EKeys::S, FKey());
	FDreamUIInputActionData* SaveAsRow = Table->FindRow<FDreamUIInputActionData>(TEXT("SaveAs"), TEXT("test"));
	if (!TestNotNull(TEXT("The chord row exists"), SaveAsRow))
	{
		return false;
	}
	SaveAsRow->bRequiresCtrl = true;

	UDreamActionCallCounter* Save = NewObject<UDreamActionCallCounter>();
	UDreamActionCallCounter* SaveAs = NewObject<UDreamActionCallCounter>();
	// Plain S registered LAST on purpose: newest wins on a tie, so if specificity were not consulted
	// this is the one that would answer a Ctrl+S.
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("SaveAs")), BindTo(SaveAs));
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Save")), BindTo(Save));

	// The modifiers are stated rather than read off a player controller, because a bare test world has
	// none -- which is the whole reason HandleKeyWithModifiers exists next to HandleKey.
	TestTrue(TEXT("S alone is taken"), Router->HandleKeyWithModifiers(0, EKeys::S, true, false, false, false, false));
	TestEqual(TEXT("...by the bare-key action"), Save->CallCount, 1);
	TestEqual(TEXT("...and not by the chord"), SaveAs->CallCount, 0);

	TestTrue(TEXT("Ctrl+S is taken"), Router->HandleKeyWithModifiers(0, EKeys::S, true, false, true, false, false));
	TestEqual(TEXT("...by the chord, which is the more specific spelling"), SaveAs->CallCount, 1);
	TestEqual(TEXT("...not by the bare key, though it was registered later"), Save->CallCount, 1);

	// A modifier the action does not ask for does not disqualify it. Existing bindings were authored
	// before modifiers existed at all, and Shift resting under a finger must not silence them.
	TestTrue(TEXT("Shift+S is still S"), Router->HandleKeyWithModifiers(0, EKeys::S, true, true, false, false, false));
	TestEqual(TEXT("...and reaches the bare-key action"), Save->CallCount, 2);
	TestEqual(TEXT("...while the Ctrl chord stays out of it"), SaveAs->CallCount, 1);

	// And the plain HandleKey path still works, modifier-free, for everything that was already bound.
	TestTrue(TEXT("The modifier-free entry point still routes"), Router->HandleKey(0, EKeys::S, true));
	TestEqual(TEXT("...to the bare-key action"), Save->CallCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterHoldProgressBroadcastTest,
	"DreamGUI.Navigation.Actions.HoldProgressIsPushedToWhoeverIsDrawingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterHoldProgressBroadcastTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("HoldToDelete"), EKeys::X, EKeys::Gamepad_FaceButton_Left, 1.0f);
	UDreamActionCallCounter* Counter = NewObject<UDreamActionCallCounter>();
	const FDreamUIActionHandle Handle = Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("HoldToDelete")), BindTo(Counter));

	// The progress was computed into FDreamUIActionBinding::HoldProgress from the beginning, and the
	// only thing that ever read it was a full prompt-bar rebuild -- which happens when bindings change
	// or the device changes, and never during a hold. So the ring on a hold-to-confirm sat at zero.
	int32 Updates = 0;
	float LastProgress = -1.0f;
	FDreamUIActionHandle LastHandle;
	Router->GetHoldProgressEvent().AddLambda(
		[&Updates, &LastProgress, &LastHandle](FDreamUIActionHandle InHandle, float InProgress)
		{
			++Updates;
			LastHandle = InHandle;
			LastProgress = InProgress;
		});

	Router->Tick(0.5f);
	TestEqual(TEXT("Nothing is held, so nothing is pushed"), Updates, 0);

	Router->HandleKey(0, EKeys::X, true);
	Router->Tick(0.25f);
	TestEqual(TEXT("A frame of a live hold pushes its progress"), Updates, 1);
	TestEqual(TEXT("...naming the binding it belongs to"), LastHandle.Id, Handle.Id);
	TestEqual(TEXT("...and how far through it is"), LastProgress, 0.25f);

	Router->Tick(0.25f);
	TestEqual(TEXT("...again on the next frame"), Updates, 2);
	TestEqual(TEXT("...with the new value"), LastProgress, 0.5f);

	// Letting go early has to empty the ring, not leave it stranded half full.
	Router->HandleKey(0, EKeys::X, false);
	TestEqual(TEXT("Releasing pushes one last update"), Updates, 3);
	TestEqual(TEXT("...back to nothing"), LastProgress, 0.0f);

	Router->Tick(1.0f);
	TestEqual(TEXT("...and a released hold pushes nothing further"), Updates, 3);
	TestEqual(TEXT("...and never fired"), Counter->CallCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionTriggerTest,
	"DreamGUI.Navigation.Actions.AnActionBoundToAWidgetClicksItAndDiesWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionTriggerTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Confirm"), EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom);

	UDreamWidget* Button = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Button->SetDisplayName(TEXT("Button"));
	Button->SetWidth(100.0f);
	Button->SetHeight(40.0f);
	Button->OnRegister();

	// The click is counted by the production component that exists for it, on the widget the action is
	// supposed to press -- so what is being asserted is delivery, not a flag somewhere.
	UUIEventTrigger* ClickWatcher = Button->AddComponent<UUIEventTrigger>();
	UDreamTestActionTrigger* ActionTrigger = Button->AddComponent<UDreamTestActionTrigger>();
	if (!TestTrue(TEXT("A button with a click watcher and an action trigger"),
		ClickWatcher != nullptr && ActionTrigger != nullptr))
	{
		return false;
	}
	int32 ClickCount = 0;
	ClickWatcher->GetOnPointerClickEvent().AddLambda([&ClickCount](UDreamPointerEventData*) { ++ClickCount; });
	ActionTrigger->SetAction(MakeHandle(Table, TEXT("Confirm")));

	// Nothing is bound until the widget goes live: a button that has not been shown yet cannot be the
	// thing a key presses.
	TestFalse(TEXT("An action trigger on a widget that is not live binds nothing"), ActionTrigger->IsActionBound());
	TestFalse(TEXT("...so the key goes unclaimed"), Router->HandleKey(0, EKeys::Enter, true));

	ActionTrigger->ForceEnable();
	TestTrue(TEXT("Going live binds the action"), ActionTrigger->IsActionBound());
	TestTrue(TEXT("The key is taken"), Router->HandleKey(0, EKeys::Enter, true));
	TestEqual(TEXT("...and the button is clicked by it"), ClickCount, 1);
	// The gamepad spelling of the same row reaches it too, which is the point of binding an action
	// rather than a key.
	Router->HandleKey(0, EKeys::Gamepad_FaceButton_Bottom, true);
	TestEqual(TEXT("...from either device"), ClickCount, 2);

	// The prompt bar learns about it for free, which is the other half of what CommonUI's
	// TriggeringInputAction buys: the key and the hint cannot drift apart because they are one binding.
	TArray<FDreamUIActionBinding> Prompts;
	Router->GetDisplayBindings(0, Prompts);
	TestEqual(TEXT("The bound action is advertised"), Prompts.Num(), 1);

	// A greyed-out button is not clickable, so its key must stop working -- and the prompt must go with
	// it. OnDisable does not run for a merely uninteractable widget, which is why this is its own path.
	ActionTrigger->ForceInteractableChanged(false);
	TestFalse(TEXT("An uninteractable button drops its binding"), ActionTrigger->IsActionBound());
	TestFalse(TEXT("...and the key is unclaimed again"), Router->HandleKey(0, EKeys::Enter, true));
	TestEqual(TEXT("...and nothing was clicked"), ClickCount, 2);
	Router->GetDisplayBindings(0, Prompts);
	TestEqual(TEXT("...and nothing is advertised"), Prompts.Num(), 0);

	ActionTrigger->ForceInteractableChanged(true);
	TestTrue(TEXT("Becoming interactable again binds it again"), ActionTrigger->IsActionBound());
	Router->HandleKey(0, EKeys::Enter, true);
	TestEqual(TEXT("...and the key works again"), ClickCount, 3);

	// And hiding the widget takes it away for the same reason.
	ActionTrigger->ForceDisable();
	TestFalse(TEXT("A hidden button drops its binding"), ActionTrigger->IsActionBound());
	TestFalse(TEXT("...and its key with it"), Router->HandleKey(0, EKeys::Enter, true));
	TestEqual(TEXT("...clicking nothing"), ClickCount, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamActionRouterInputActionBridgeTest,
	"DreamGUI.Navigation.Actions.AnInputActionIsAnExtraKeySourceAndNeverBeatsTheTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamActionRouterInputActionBridgeTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionRouterTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIActionRouter* Router = TestWorld.World->GetSubsystem<UDreamUIActionRouter>();
	if (!TestNotNull(TEXT("Action router subsystem exists"), Router))
	{
		return false;
	}

	UDataTable* Table = MakeActionTable();
	AddAction(Table, TEXT("Typed"), EKeys::J, FKey());
	// A row that names an Input Action instead of a key. Which keys that action stands for can only be
	// answered by a local player's mapping contexts, and a headless world has neither -- so what this
	// pins down is the part that does not need them: an unresolvable action claims nothing, and it
	// never takes a key away from a row that names it outright.
	AddAction(Table, TEXT("Enhanced"), FKey(), FKey());
	FDreamUIInputActionData* EnhancedRow = Table->FindRow<FDreamUIInputActionData>(TEXT("Enhanced"), TEXT("test"));
	if (!TestNotNull(TEXT("The Enhanced Input row exists"), EnhancedRow))
	{
		return false;
	}
	// Any non-empty path does: resolution gives up at "this world has no local player" long before it
	// would try to load the asset, which is what makes this branch reachable at all without hardware.
	EnhancedRow->InputAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/DreamGUITests/IA_NeverLoadedHeadless.IA_NeverLoadedHeadless")));

	UDreamActionCallCounter* Typed = NewObject<UDreamActionCallCounter>();
	UDreamActionCallCounter* Enhanced = NewObject<UDreamActionCallCounter>();
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Typed")), BindTo(Typed));
	Router->RegisterAction(nullptr, MakeHandle(Table, TEXT("Enhanced")), BindTo(Enhanced));

	TestTrue(TEXT("The typed key still routes"), Router->HandleKey(0, EKeys::J, true));
	TestEqual(TEXT("...to the row that names it"), Typed->CallCount, 1);
	TestEqual(TEXT("...and not to the Input Action row"), Enhanced->CallCount, 0);

	// An unresolvable action is silent rather than greedy: it must not answer for keys it cannot prove
	// belong to it.
	TestFalse(TEXT("An unbound key is still unbound"), Router->HandleKey(0, EKeys::K, true));
	TestEqual(TEXT("...and the Input Action row did not take it"), Enhanced->CallCount, 0);

	// Prompts: a row with neither a key nor resolvable action keys draws nothing, rather than a blank.
	TArray<FDreamUIActionBinding> Prompts;
	Router->GetDisplayBindings(0, Prompts);
	TestEqual(TEXT("Only the row with a real key is advertised"), Prompts.Num(), 1);
	if (Prompts.Num() == 1)
	{
		TestEqual(TEXT("...with that key"), Prompts[0].Key, EKeys::J);
	}

	// Re-resolving is the escape hatch for a project that swaps mapping contexts while screens are
	// open. It must tell the prompt bars, or they keep drawing the keys from before the swap.
	int32 BindingsChanged = 0;
	Router->GetBindingsChangedEvent().AddLambda([&BindingsChanged](int32) { ++BindingsChanged; });
	Router->RefreshInputActionKeys();
	TestEqual(TEXT("Refreshing tells the prompt bars once"), BindingsChanged, 1);
	TestTrue(TEXT("...and routing still works afterwards"), Router->HandleKey(0, EKeys::J, true));
	return true;
}

#endif
