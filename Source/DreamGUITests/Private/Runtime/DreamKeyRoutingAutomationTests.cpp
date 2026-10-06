// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformInput.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamKeyRoutingTestTypes.h"
#include "DreamNavigationTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * WHAT A KEY ACTS ON, AND WHEN IT IS A STEP AT ALL.
 *
 * Every key the player presses reaches DreamGUI through one road (DreamUIKeyRouting): the field being typed into, the
 * bindings, the virtual cursor, then the built-in meaning. These hold that road to what round 5 decided: a step and a
 * confirm start from the player's focus and never from where the mouse rests; Tab is a step only without Ctrl, Alt or
 * Cmd; the key tables are the project's settings, the platform's accept and back included; a screen in input mode Game
 * leaves its player's navigation, confirm and Back to the game; the shoulder buttons switch tabs after the bindings had
 * their turn; and a step asked for from inside a key handler is taken on the next frame. Driven on the headless rig,
 * whose key steps go through DreamUIKeyRouting::RouteKey with their chord, as a Slate source's key would.
 */
namespace DreamKeyRoutingTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);
	const FVector2D FieldSize(320.0, 40.0);

	UDreamButton* MakeListenedButton(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InPosition,
		UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		}
		return Button;
	}

	UDreamWidget* FaceOf(const UDreamButton* InButton)
	{
		return InButton != nullptr ? InButton->FaceNode.Get() : nullptr;
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig, int32 InUserIndex = 0)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(InUserIndex) : nullptr;
	}

	bool FocusFor(const FDreamDriverRig& InRig, UDreamWidget* InWidget, int32 InUserIndex = 0)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr && InWidget != nullptr && Services->FocusForNavigation(InWidget, InUserIndex);
	}

	/** An action row bound for player 0 with nobody's screen in front: a global binding, as a project's own shortcut is. */
	struct FBoundAction
	{
		TStrongObjectPtr<UDataTable> Table;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;
		FDreamUIActionHandle Handle;
		TWeakObjectPtr<UDreamUIActionRouter> Router;

		FBoundAction(UWorld* InWorld, const FName InRowName, const FDreamUIInputActionData& InRow)
		{
			UDreamUIActionRouter* FoundRouter = UDreamUIActionRouter::Get(InWorld);
			if (FoundRouter == nullptr)
			{
				return;
			}
			Table.Reset(NewObject<UDataTable>(GetTransientPackage()));
			Table->RowStruct = FDreamUIInputActionData::StaticStruct();
			Table->AddRow(InRowName, InRow);
			FDataTableRowHandle RowHandle;
			RowHandle.DataTable = Table.Get();
			RowHandle.RowName = InRowName;
			Counter.Reset(NewObject<UDreamActionCallCounter>());
			FDreamUIActionExecutedDelegate OnFire;
			OnFire.BindUFunction(Counter.Get(), TEXT("Fire"));
			Handle = FoundRouter->RegisterAction(nullptr, RowHandle, OnFire);
			Router = FoundRouter;
		}

		~FBoundAction()
		{
			if (UDreamUIActionRouter* FoundRouter = Router.Get())
			{
				FoundRouter->UnregisterAction(Handle);
			}
		}

		bool IsBound() const { return Handle.IsValidHandle() && Counter.IsValid(); }
		int32 Calls() const { return Counter.IsValid() ? Counter->CallCount : 0; }
	};

	/** A key and its release, routed for InUser as a Slate source routes one, outside the driver: another player's keys. */
	void RouteKeyFor(FDreamDriverRig& InRig, UDreamUIInputUser* InUser, const FKey& InKey)
	{
		const FModifierKeysState NoModifiers;
		bool bTyped = false;
		DreamUIKeyRouting::RouteKey(InUser, InKey, true, NoModifiers, bTyped);
		InRig.PumpFrames(1);
		DreamUIKeyRouting::RouteKey(InUser, InKey, false, NoModifiers, bTyped);
		InRig.PumpFrames(1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysTabStartsFromTheFocusTest,
	"DreamGUI.Input.Keys.TabFromTheFirstFieldGoesToTheSecondWhileTheMouseRestsOnTheThird",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysTabStartsFromTheFocusTest, "DreamGUI.Input.Keys.TabFromTheFirstFieldGoesToTheSecondWhileTheMouseRestsOnTheThird", "[Nav][Animated]")

/*
 * Tab started from pointer 0's navigation highlight, and every hover rewrote the highlight: with the first field focused
 * and the mouse resting on the third, Tab went to whatever followed the third. A step starts from the player's focus now,
 * wherever the mouse is. Three fields in a column, the first focused by code, the mouse moved onto the third, then Tab.
 */
bool FDreamKeysTabStartsFromTheFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTextInput* First = Rig.MakeControl<UDreamTextInput>(TEXT("First"), nullptr, FieldSize, FVector2D(0.0, 150.0));
	UDreamTextInput* Second = Rig.MakeControl<UDreamTextInput>(TEXT("Second"), nullptr, FieldSize, FVector2D(0.0, 50.0));
	UDreamTextInput* Third = Rig.MakeControl<UDreamTextInput>(TEXT("Third"), nullptr, FieldSize, FVector2D(0.0, -50.0));
	if (!TestTrue(TEXT("Three fields in a column"), First != nullptr && Second != nullptr && Third != nullptr
		&& First->InputBehaviour != nullptr && Second->InputBehaviour != nullptr && Third->InputBehaviour != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	UDreamWidget* FirstWidget = First->InputBehaviour->GetWidget();
	UDreamWidget* SecondWidget = Second->InputBehaviour->GetWidget();
	const TOptional<FVector2D> OnThird = FDreamDriverProjection::WidgetCentrePixel(Third->InputBehaviour->GetWidget());
	if (!TestTrue(TEXT("The first field takes the focus"), FocusFor(Rig, FirstWidget))
		|| !TestTrue(TEXT("The third field is somewhere the mouse can rest"), OnThird.IsSet()))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("The mouse comes to rest on the third field"), Driver->Sequence().MoveToPixel(OnThird.GetValue()).WaitFrames(2).Perform());
	TestEqual(TEXT("Resting there moved no focus"), FocusOf(Rig), FirstWidget);

	TestTrue(TEXT("Tab completes"), Driver->Sequence().Tab().Perform());
	TestEqual(TEXT("Tab went on from the focused first field to the second, not from where the mouse rests"), FocusOf(Rig), SecondWidget);
	const UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	TestTrue(TEXT("...and the focus is recorded as Tab's"), Services != nullptr && Services->GetFocusCause(0) == EDreamUIFocusCause::Tab);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysEnterPressesTheFocusTest,
	"DreamGUI.Input.Keys.EnterPressesTheFocusedButtonNotTheOneTheMouseIsOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysEnterPressesTheFocusTest, "DreamGUI.Input.Keys.EnterPressesTheFocusedButtonNotTheOneTheMouseIsOver", "[Nav][Animated]")

/*
 * Enter asked the navigation highlight first, which the mouse's hover had written: the button under a resting mouse was
 * pressed rather than the one the keys had reached. The keys act on the focus now. One button focused by code, the mouse
 * resting on the other, then Enter: the focused one is clicked, the hovered one never pressed.
 */
bool FDreamKeysEnterPressesTheFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> FocusedListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> HoveredListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Focused = MakeListenedButton(Rig, TEXT("Focused"), nullptr, FVector2D(-250.0, 0.0), FocusedListener.Get());
	UDreamButton* Hovered = MakeListenedButton(Rig, TEXT("Hovered"), nullptr, FVector2D(250.0, 0.0), HoveredListener.Get());
	if (!TestTrue(TEXT("Two buttons"), FaceOf(Focused) != nullptr && FaceOf(Hovered) != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const TOptional<FVector2D> OnHovered = FDreamDriverProjection::WidgetCentrePixel(FaceOf(Hovered));
	if (!TestTrue(TEXT("The left button takes the focus"), FocusFor(Rig, FaceOf(Focused)))
		|| !TestTrue(TEXT("The right one is somewhere the mouse can rest"), OnHovered.IsSet()))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	TestTrue(TEXT("The mouse comes to rest on the right button"), Driver->Sequence().MoveToPixel(OnHovered.GetValue()).WaitFrames(2).Perform());
	const UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	TestTrue(TEXT("The right button is hovered"), Services != nullptr && Services->IsHovered(FaceOf(Hovered), 0));

	TestTrue(TEXT("Enter completes"), Driver->Sequence().Key(EKeys::Enter).WaitFrames(1).Perform());
	TestEqual(TEXT("Enter pressed the focused button"), FocusedListener->PressedCount, 1);
	TestEqual(TEXT("...and clicked it"), FocusedListener->ClickedCount, 1);
	TestEqual(TEXT("The button the mouse is over was never pressed"), HoveredListener->PressedCount, 0);
	TestEqual(TEXT("...nor clicked"), HoveredListener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysCtrlTabTest,
	"DreamGUI.Input.Keys.CtrlTabReachesABindingAndNeverMovesTheFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysCtrlTabTest, "DreamGUI.Input.Keys.CtrlTabReachesABindingAndNeverMovesTheFocus", "[Nav][Animated]")

/*
 * Tab stepped whatever modifiers were held -- a legacy key binding fires for Tab with Ctrl down as well -- so a project's
 * Ctrl+Tab shortcut stepped the focus too, and Alt+Tab on the way out of the game stepped it. As in Slate, Tab is a step
 * only with neither Ctrl, Alt nor Cmd held; with one it is an ordinary key: the bindings, and nothing after them.
 */
bool FDreamKeysCtrlTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Left = MakeListenedButton(Rig, TEXT("Left"), nullptr, FVector2D(-250.0, 0.0), nullptr);
	UDreamButton* Right = MakeListenedButton(Rig, TEXT("Right"), nullptr, FVector2D(250.0, 0.0), nullptr);
	if (!TestTrue(TEXT("Two buttons"), FaceOf(Left) != nullptr && FaceOf(Right) != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	FDreamUIInputActionData Row;
	Row.DisplayName = FText::FromString(TEXT("Next document"));
	Row.KeyboardKey = EKeys::Tab;
	Row.bRequiresCtrl = true;
	const FBoundAction NextDocument(Rig.GetWorld(), TEXT("NextDocument"), Row);
	if (!TestTrue(TEXT("Ctrl+Tab is bound"), NextDocument.IsBound()) || !TestTrue(TEXT("The left button takes the focus"), FocusFor(Rig, FaceOf(Left))))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Ctrl+Tab completes"), Driver->Sequence().Key(EKeys::Tab, EDreamDriverModifierKeys::Ctrl).Perform());
	TestEqual(TEXT("Ctrl+Tab reached its binding"), NextDocument.Calls(), 1);
	TestEqual(TEXT("...and moved no focus"), FocusOf(Rig), FaceOf(Left));

	TestTrue(TEXT("Alt+Tab completes"), Driver->Sequence().Key(EKeys::Tab, EDreamDriverModifierKeys::Alt).Perform());
	TestEqual(TEXT("Alt+Tab is no step either"), FocusOf(Rig), FaceOf(Left));
	TestEqual(TEXT("...and not the Ctrl+Tab binding's"), NextDocument.Calls(), 1);

	TestTrue(TEXT("Tab completes"), Driver->Sequence().Key(EKeys::Tab).Perform());
	TestEqual(TEXT("Tab alone still steps"), FocusOf(Rig), FaceOf(Right));
	TestEqual(TEXT("...and is no Ctrl+Tab"), NextDocument.Calls(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysSwappedAcceptBackTest,
	"DreamGUI.Input.Keys.WithAcceptAndBackSwappedInTheSettingsTheRightFaceButtonConfirms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysSwappedAcceptBackTest, "DreamGUI.Input.Keys.WithAcceptAndBackSwappedInTheSettingsTheRightFaceButtonConfirms", "[Nav][Animated]")

/*
 * The confirm and Back keys were a table written into the code: the pad's bottom face button confirmed and the right one
 * went Back on every platform, the other way round from a Switch. The tables are the project's settings now, and with
 * bUsePlatformAcceptBack the platform's own accept and back keys go in, taking a face button the platform uses the other
 * way out of the other table. Here the settings swap the two, as a Switch would: the right face button confirms and the
 * bottom one is Back. Then the platform's rule is checked against this platform's own keys.
 */
bool FDreamKeysSwappedAcceptBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	TGuardValue<bool> PlatformGuard(Settings->bUsePlatformAcceptBack, false);
	TGuardValue<TArray<FKey>> ConfirmGuard(Settings->ConfirmKeys, TArray<FKey>{ EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Right });
	TGuardValue<TArray<FKey>> BackGuard(Settings->BackKeys, TArray<FKey>{ EKeys::Escape, EKeys::Gamepad_FaceButton_Bottom });
	TestTrue(TEXT("The right face button confirms"), DreamUIKeyRouting::IsConfirmKey(EKeys::Gamepad_FaceButton_Right));
	TestFalse(TEXT("...and is not Back"), DreamUIKeyRouting::IsBackKey(EKeys::Gamepad_FaceButton_Right));
	TestTrue(TEXT("The bottom face button is Back"), DreamUIKeyRouting::IsBackKey(EKeys::Gamepad_FaceButton_Bottom));
	TestFalse(TEXT("...and confirms no more"), DreamUIKeyRouting::IsConfirmKey(EKeys::Gamepad_FaceButton_Bottom));

	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamWidget* Screen = Rig.MakeWidget(TEXT("Screen"), nullptr, FVector2D(1000.0, 600.0));
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), Screen, FVector2D::ZeroVector, Listener.Get());
	UDreamBackHandlingScope* Scope = IsValid(Screen) ? Screen->AddComponent<UDreamBackHandlingScope>() : nullptr;
	if (!TestTrue(TEXT("A screen with a button and a scope that hears Back"), FaceOf(Button) != nullptr && Scope != nullptr))
	{
		return false;
	}
	Scope->SetActivateWhenEnabled(false);
	Scope->SetUserIndex(0);
	Scope->bHandleBack = true;
	Rig.PumpFrames(2);
	Scope->ActivateScope();
	if (!TestTrue(TEXT("The button has the focus"), FocusFor(Rig, FaceOf(Button))))
	{
		Scope->DeactivateScope();
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("The right face button completes"), Driver->Sequence().Key(EKeys::Gamepad_FaceButton_Right).WaitFrames(1).Perform());
	TestEqual(TEXT("The right face button clicked the focused button"), Listener->ClickedCount, 1);
	TestEqual(TEXT("...and was no Back"), Scope->BackOfferCount, 0);
	TestTrue(TEXT("The bottom face button completes"), Driver->Sequence().Key(EKeys::Gamepad_FaceButton_Bottom).WaitFrames(1).Perform());
	TestEqual(TEXT("The bottom face button is Back"), Scope->BackOfferCount, 1);
	TestEqual(TEXT("...and clicked nothing"), Listener->ClickedCount, 1);
	Scope->DeactivateScope();

	// The platform's own keys, when the settings ask for them: in whatever the tables say, and a face button the platform
	// uses the other way left out of the other table.
	Settings->bUsePlatformAcceptBack = true;
	const FKey PlatformAccept = FPlatformInput::GetGamepadAcceptKey();
	const FKey PlatformBack = FPlatformInput::GetGamepadBackKey();
	Settings->ConfirmKeys = { EKeys::Enter, PlatformBack };
	Settings->BackKeys = { EKeys::Escape, PlatformAccept };
	TestTrue(TEXT("The pad's accept key is the platform's"), DreamUIKeyRouting::GetGamepadAcceptKey() == PlatformAccept);
	TestTrue(TEXT("...and so is its back key"), DreamUIKeyRouting::GetGamepadBackKey() == PlatformBack);
	TestTrue(TEXT("The platform's accept confirms though the table left it out"), DreamUIKeyRouting::IsConfirmKey(PlatformAccept));
	TestTrue(TEXT("...and its back is Back"), DreamUIKeyRouting::IsBackKey(PlatformBack));
	TestFalse(TEXT("The platform's back key confirms no more though the table lists it"), DreamUIKeyRouting::IsConfirmKey(PlatformBack));
	TestFalse(TEXT("...nor is its accept key Back"), DreamUIKeyRouting::IsBackKey(PlatformAccept));
	TestTrue(TEXT("The table's own keys stay"), DreamUIKeyRouting::IsConfirmKey(EKeys::Enter) && DreamUIKeyRouting::IsBackKey(EKeys::Escape));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysInputModeGameTest,
	"DreamGUI.Input.Keys.InputModeGameTurnsOffNavigationConfirmAndBackForThatPlayerAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysInputModeGameTest, "DreamGUI.Input.Keys.InputModeGameTurnsOffNavigationConfirmAndBackForThatPlayerAlone", "[Nav][Animated]")

/*
 * There was no way to say "this screen is gameplay": a HUD's buttons were walked onto by the D-pad and pressed by the jump
 * button. A navigation scope's input mode Game turns DreamGUI's navigation, confirm and Back off for its player while it
 * is their active scope -- for that player alone -- and Menu gives them back. Player 0 has a gameplay scope in front; a
 * second player has none; both have the upper of two buttons focused.
 */
bool FDreamKeysInputModeGameTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TStrongObjectPtr<UDreamPressInteractionListener> UpperListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Upper = MakeListenedButton(Rig, TEXT("Upper"), nullptr, FVector2D(0.0, 80.0), UpperListener.Get());
	UDreamButton* Lower = MakeListenedButton(Rig, TEXT("Lower"), nullptr, FVector2D(0.0, -80.0), nullptr);
	UDreamWidget* Gameplay = Rig.MakeWidget(TEXT("Gameplay"), nullptr, FVector2D(100.0, 100.0), FVector2D(-500.0, 0.0));
	UDreamBackHandlingScope* Scope = IsValid(Gameplay) ? Gameplay->AddComponent<UDreamBackHandlingScope>() : nullptr;
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("Two buttons, a gameplay screen's scope, the world's input and its screens"),
		FaceOf(Upper) != nullptr && FaceOf(Lower) != nullptr && Scope != nullptr && Input != nullptr && Stack != nullptr))
	{
		return false;
	}
	Scope->SetActivateWhenEnabled(false);
	Scope->SetConfineNavigation(false);
	Scope->SetUserIndex(0);
	Scope->SetInputMode(EDreamUIScopeInputMode::Game);
	Scope->bHandleBack = true;
	UDreamUIInputUser* Second = Input->GetOrCreateUser(1);
	Rig.PumpFrames(2);
	Scope->ActivateScope();
	ON_SCOPE_EXIT{ Scope->DeactivateScope(); Input->RemoveUser(1); };
	if (!TestNotNull(TEXT("A second player"), Second)
		|| !TestTrue(TEXT("Player 0's upper button has the focus"), FocusFor(Rig, FaceOf(Upper), 0))
		|| !TestTrue(TEXT("...and the second player's"), FocusFor(Rig, FaceOf(Upper), 1)))
	{
		return false;
	}
	TestEqual(TEXT("Player 0 is in gameplay"), Stack->GetEffectiveInputMode(0), EDreamUIScopeInputMode::Game);
	TestEqual(TEXT("The second player, with no scope, is not"), Stack->GetEffectiveInputMode(1), EDreamUIScopeInputMode::All);
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Player 0's D-pad down completes"), Driver->Sequence().Key(EKeys::Gamepad_DPad_Down).Perform());
	TestEqual(TEXT("In gameplay the D-pad does not walk the HUD"), FocusOf(Rig, 0), FaceOf(Upper));
	TestTrue(TEXT("Player 0's accept button completes"), Driver->Sequence().Key(EKeys::Gamepad_FaceButton_Bottom).WaitFrames(1).Perform());
	TestEqual(TEXT("...the accept button presses nothing"), UpperListener->PressedCount, 0);
	TestTrue(TEXT("Player 0's Back completes"), Driver->Sequence().Key(EKeys::Escape).Perform());
	TestEqual(TEXT("...and Back is the game's"), Scope->BackOfferCount, 0);
	TestFalse(TEXT("Player 0's keys have no built-in meaning in gameplay"), DreamUIKeyRouting::HasBuiltInMeanings(Rig.EventSystem()->GetInputUser()));

	RouteKeyFor(Rig, Second, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("The second player, outside it, still navigates"), FocusOf(Rig, 1), FaceOf(Lower));

	Scope->SetInputMode(EDreamUIScopeInputMode::Menu);
	TestTrue(TEXT("Player 0's D-pad down completes again"), Driver->Sequence().Key(EKeys::Gamepad_DPad_Down).Perform());
	TestEqual(TEXT("In a menu the D-pad navigates"), FocusOf(Rig, 0), FaceOf(Lower));
	TestTrue(TEXT("Player 0's Back completes again"), Driver->Sequence().Key(EKeys::Escape).Perform());
	TestEqual(TEXT("...and Back reaches the screen"), Scope->BackOfferCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysTabSwitchTest,
	"DreamGUI.Input.Keys.TheShouldersSwitchTabsUnlessABindingTakesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamKeysTabSwitchTest, "DreamGUI.Input.Keys.TheShouldersSwitchTabsUnlessABindingTakesThem", "[Nav][Animated]")

/*
 * The shoulder buttons reached nothing but the action router. They switch tabs now on the tab view around the player's
 * focus (IDreamUITabSwitchTarget), as the settings' PreviousTabKeys and NextTabKeys say -- after the bindings had their
 * turn, so a screen that binds the right shoulder keeps it. A stand-in tab view with a button inside, focused.
 */
bool FDreamKeysTabSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTabSwitchProbeWidget* Tabs = NewObject<UDreamTabSwitchProbeWidget>(Rig.GetWorld(), NAME_None, RF_Public | RF_Transactional);
	Tabs->SetDisplayName(TEXT("Tabs"));
	Tabs->SetWidth(600.0f);
	Tabs->SetHeight(400.0f);
	Tabs->OnRegister();
	Tabs->TrySetParent(Rig.Root(), false);
	if (Rig.Root() != nullptr && Rig.Root()->HasBegunPlay() && !Tabs->HasBegunPlay())
	{
		Tabs->BeginPlay();
	}
	UDreamButton* Inside = MakeListenedButton(Rig, TEXT("Inside"), Tabs, FVector2D::ZeroVector, nullptr);
	if (!TestNotNull(TEXT("A button on the tab view's page"), FaceOf(Inside)))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The button has the focus"), FocusFor(Rig, FaceOf(Inside))))
	{
		return false;
	}
	TestEqual(TEXT("The tab view around the focus is what the shoulders switch"),
		DreamUIKeyRouting::FindTabSwitchTarget(Rig.EventSystem()->GetInputUser()), static_cast<UDreamWidget*>(Tabs));
	TestEqual(TEXT("The left shoulder is the previous tab"), DreamUIKeyRouting::GetTabSwitchDelta(EKeys::Gamepad_LeftShoulder), -1);
	TestEqual(TEXT("...the right the next"), DreamUIKeyRouting::GetTabSwitchDelta(EKeys::Gamepad_RightShoulder), 1);
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("The right shoulder completes"), Driver->Sequence().Key(EKeys::Gamepad_RightShoulder).Perform());
	TestEqual(TEXT("It switched to the next tab"), Tabs->SwitchCount, 1);
	TestEqual(TEXT("...forward"), Tabs->LastDelta, 1);
	TestEqual(TEXT("...for player 0"), Tabs->LastUserIndex, 0);
	TestTrue(TEXT("The left shoulder completes"), Driver->Sequence().Key(EKeys::Gamepad_LeftShoulder).Perform());
	TestEqual(TEXT("It switched back"), Tabs->SwitchCount, 2);
	TestEqual(TEXT("...backward"), Tabs->LastDelta, -1);

	FDreamUIInputActionData Row;
	Row.DisplayName = FText::FromString(TEXT("Next hero"));
	Row.GamepadKey = EKeys::Gamepad_RightShoulder;
	const FBoundAction NextHero(Rig.GetWorld(), TEXT("NextHero"), Row);
	if (!TestTrue(TEXT("The right shoulder is bound"), NextHero.IsBound()))
	{
		return false;
	}
	TestTrue(TEXT("The right shoulder completes again"), Driver->Sequence().Key(EKeys::Gamepad_RightShoulder).Perform());
	TestEqual(TEXT("The binding on the right shoulder took it"), NextHero.Calls(), 1);
	TestEqual(TEXT("...and the tab view did not switch"), Tabs->SwitchCount, 2);
	Tabs->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamKeysRequestedStepTest,
	"DreamGUI.Input.Keys.ARequestedStepIsTakenOnTheNextFrameFromTheFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What a text field asks for when Tab ends its edit (UDreamUIInputUser::RequestNavigationStep): one step from the
 * player's focus on their next input frame, as the key's own step would be -- Next and Prev through the tab order and
 * recorded as Tab, a direction as Navigation -- and a second request before that frame replaces the first.
 */
bool FDreamKeysRequestedStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamKeyRoutingTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Top = MakeListenedButton(Rig, TEXT("Top"), nullptr, FVector2D(0.0, 120.0), nullptr);
	UDreamButton* Middle = MakeListenedButton(Rig, TEXT("Middle"), nullptr, FVector2D(0.0, 0.0), nullptr);
	UDreamButton* Bottom = MakeListenedButton(Rig, TEXT("Bottom"), nullptr, FVector2D(0.0, -120.0), nullptr);
	UDreamUIInputUser* User = Rig.EventSystem() != nullptr ? Rig.EventSystem()->GetInputUser() : nullptr;
	const UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("Three buttons in a column, a player and the world's input"),
		FaceOf(Top) != nullptr && FaceOf(Middle) != nullptr && FaceOf(Bottom) != nullptr && User != nullptr && Services != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The top button has the focus"), FocusFor(Rig, FaceOf(Top))))
	{
		return false;
	}

	User->RequestNavigationStep(EDreamUINavigationDirection::Next);
	TestEqual(TEXT("A request moves nothing by itself"), FocusOf(Rig), FaceOf(Top));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The next frame took the step, from the focus"), FocusOf(Rig), FaceOf(Middle));
	TestEqual(TEXT("...recorded as Tab's"), Services->GetFocusCause(0), EDreamUIFocusCause::Tab);
	Rig.PumpFrames(1);
	TestEqual(TEXT("...once"), FocusOf(Rig), FaceOf(Middle));

	User->RequestNavigationStep(EDreamUINavigationDirection::Next);
	User->RequestNavigationStep(EDreamUINavigationDirection::Prev);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A second request before the frame replaced the first"), FocusOf(Rig), FaceOf(Top));

	User->RequestNavigationStep(EDreamUINavigationDirection::Down);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A direction is a step too"), FocusOf(Rig), FaceOf(Middle));
	TestEqual(TEXT("...recorded as Navigation's"), Services->GetFocusCause(0), EDreamUIFocusCause::Navigation);
	return true;
}

#endif
