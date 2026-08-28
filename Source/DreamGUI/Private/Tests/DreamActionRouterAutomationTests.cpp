// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUIInputAction.h"
#include "Tests/DreamNavigationTestTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

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
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

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

#endif
