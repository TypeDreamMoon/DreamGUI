// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamInputKeySelector.h"
#include "Controls/DreamTextInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Event/DreamBaseEventData.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputActors.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * WHAT STILL WORKS WHEN THE GAME IS PAUSED, ENTERING WHERE A PLAYER'S INPUT DOES.
 *
 * A pause menu is UI, and this library says UI keeps working while the game is paused
 * (UDreamUISettings::bScreenSpaceUIAffectByGamePause, false by default; Slate is not paused by a game
 * pause at all). A paused world runs the controller's input frame as a paused one, and every legacy
 * binding that does not execute while paused, and every Input Action that does not trigger while
 * paused, is silenced there -- while a CONSUMING binding still takes its key. The fields and key
 * binders push input components of their own above the preset actor, so a paused game used to lose
 * their keys twice over: their bindings did not run, and the keys were eaten on the way past.
 *
 * Every test here is paused the way a game is (a player state standing as the world's pauser, which
 * is what UWorld::IsPaused asks for) and enters through the player controller of a rig built with one
 * of the preset actors as its input host.
 *
 * The last two are about the Enhanced Input preset's runtime copies of its actions: it plays with
 * copies (so their pause flag can follow the setting without writing to an asset) and keeps the
 * originals bound beside them, so a key a project's own context maps to an original still reaches the
 * UI -- once, however many of the two roads one input comes down.
 */
namespace DreamPausedGameHostTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	const FHostCase ActorHosts[] = {
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};
	const FHostCase& EnhancedHost = ActorHosts[1];

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	FString RigCameUp(const FHostCase& InCase, const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? Under(InCase, TEXT("The rig came up"))
			: FString::Printf(TEXT("[%s] The rig came up -- it did not: %s"), InCase.Name, *WhyNot);
	}

	/**
	 * Pause InWorld as a game is paused: a player state standing as the pauser, which is what
	 * UWorld::IsPaused asks the world settings for. There is no game mode here to call SetPause on.
	 * @return whether the world now answers paused.
	 */
	bool PauseTheGame(UWorld* InWorld)
	{
		AWorldSettings* Settings = InWorld != nullptr ? InWorld->GetWorldSettings() : nullptr;
		APlayerState* Pauser = InWorld != nullptr ? InWorld->SpawnActor<APlayerState>() : nullptr;
		if (Settings == nullptr || Pauser == nullptr)
		{
			return false;
		}
		Settings->SetPauserPlayerState(Pauser);
		return InWorld->IsPaused();
	}

	void UnpauseTheGame(UWorld* InWorld)
	{
		if (AWorldSettings* Settings = InWorld != nullptr ? InWorld->GetWorldSettings() : nullptr)
		{
			Settings->SetPauserPlayerState(nullptr);
		}
	}

	/** Whether InKey reached the rig's player controller -- the proof a step went the player's way round. */
	bool ControllerSawKey(const FDreamDriverRig& InRig, const FKey& InKey)
	{
		const APlayerController* Controller = InRig.GetPlayerController();
		return Controller != nullptr && Controller->PlayerInput != nullptr && Controller->PlayerInput->GetKeyState(InKey) != nullptr;
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	UDreamButton* MakeListenedButton(FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Resume"), nullptr, ButtonSize);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		}
		return Button;
	}

	/** Whether the player's Enhanced Input mappings map InKey to InAction -- through the public view. */
	bool PlayerInputMaps(const UEnhancedPlayerInput& InPlayerInput, const FKey& InKey, const UInputAction* InAction)
	{
		for (const FEnhancedActionKeyMapping& Mapping : InPlayerInput.GetEnhancedActionMappingsView())
		{
			if (Mapping.Key == InKey && Mapping.Action == InAction)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * A key nothing in either preset binds by name, standing for the pad button a project maps to the
	 * UI click in a context of its own. The preset's AnyKey binding sees it and offers it to the action
	 * router, which has nothing for it -- so the only way it can press a button is through the action.
	 */
	const FKey& ProjectKey()
	{
		return EKeys::Gamepad_FaceButton_Left;
	}
}

/**
 * A field in a paused game's menu takes its keys: clicked into through the controller while paused,
 * then two letter keys, Backspace and Enter, all through the controller -- which is where the field's
 * key agent listens (UUITextInput::BindKeys pushes it above the preset actor). SEditableText in a paused
 * game does all of this; so did this field unpaused. Paused, its bindings did not execute and still
 * consumed the keys, so nothing reached it.
 *
 * Letters come by the key road -- no host delivering characters, so the field types a key's own letter
 * from its table (US layout, caps lock read from the machine's keyboard, as in the key-road test) --
 * because the game's character road goes through the viewport client, not the input stack a pause
 * gates. The switch is process-wide and put back afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPausedGameHostFieldTypingTest,
	"DreamGUI.Driver.GameHost.Paused.AFieldTakesLetterKeysBackspaceAndEnterThroughTheControllerWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPausedGameHostFieldTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPausedGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		const bool bWasDelivering = UUITextInput::IsHostDeliveringCharacterEvents();
		UUITextInput::SetHostDeliversCharacterEventsForTesting(false);
		ON_SCOPE_EXIT
		{
			UUITextInput::SetHostDeliversCharacterEventsForTesting(bWasDelivering);
		};

		UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FVector2D(320.0, 40.0));
		if (!TestNotNull(*Under(Case, TEXT("A field can be made on the rig")), Field))
		{
			continue;
		}
		Field->OnTextChanged.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleTextChanged);
		Field->OnTextCommitted.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleTextCommitted);
		Rig.PumpFrames(1);

		UWorld* World = Rig.GetWorld();
		if (!TestTrue(Under(Case, TEXT("The game is paused")), PauseTheGame(World)))
		{
			continue;
		}
		ON_SCOPE_EXIT
		{
			UnpauseTheGame(World);
		};

		TestTrue(Under(Case, TEXT("Clicking the field while paused completes")),
			Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Click());
		if (!TestTrue(Under(Case, TEXT("The paused click through the controller began an edit")), IsEditing(Field)))
		{
			continue;
		}

		const bool bCapsLocked = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().AreCapsLocked();
		const FString LetterA = bCapsLocked ? TEXT("A") : TEXT("a");
		const FString LetterB = bCapsLocked ? TEXT("B") : TEXT("b");

		TestTrue(Under(Case, TEXT("The A key through the controller completes")), Rig.Driver()->Sequence().Type(EKeys::A).Perform());
		TestTrue(Under(Case, TEXT("The B key through the controller completes")), Rig.Driver()->Sequence().Type(EKeys::B).Perform());
		TestEqual(Under(Case, TEXT("The paused game's field typed both keys' letters")), Field->GetText(), LetterA + LetterB);
		TestEqual(Under(Case, TEXT("one edit each")), Listener->TextChangedCount, 2);

		TestTrue(Under(Case, TEXT("Backspace through the controller completes")), Rig.Driver()->Sequence().Type(EKeys::BackSpace).Perform());
		TestEqual(Under(Case, TEXT("Backspace removed the last letter")), Field->GetText(), LetterA);

		TestTrue(Under(Case, TEXT("Enter, and the frame its release lands in, complete")),
			Rig.Driver()->Sequence().Type(EKeys::Enter).WaitFrames(1).Perform());
		TestEqual(Under(Case, TEXT("Enter committed the field once")), Listener->TextCommittedCount, 1);
		TestEqual(Under(Case, TEXT("with what the edit left")), Listener->LastCommittedText, LetterA);
		TestFalse(Under(Case, TEXT("and ended the edit")), IsEditing(Field));

		TestTrue(Under(Case, TEXT("Every key went through the player controller")),
			ControllerSawKey(Rig, EKeys::A) && ControllerSawKey(Rig, EKeys::BackSpace) && ControllerSawKey(Rig, EKeys::Enter));
		TestTrue(Under(Case, TEXT("and the game stayed paused throughout")), World->IsPaused());
	}
	return true;
}

/**
 * A key binder in a paused game's settings screen: armed with a click through the controller while
 * paused, it takes the next key pressed through the controller as its binding and stands down --
 * UMG's InputKeySelector in a paused game does. Its capture agent sits at the top of the player's
 * input stack and binds every key; paused, those bindings did not execute and still consumed every
 * key, so an armed selector heard nothing and swallowed everything.
 *
 * If the paused click does not arm it, it is armed directly and the failure is reported, so the
 * capture is still asked about.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPausedGameHostKeySelectorCaptureTest,
	"DreamGUI.Driver.GameHost.Paused.AnArmedKeySelectorCapturesTheNextKeyThroughTheControllerWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPausedGameHostKeySelectorCaptureTest::RunTest(const FString& Parameters)
{
	using namespace DreamPausedGameHostTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamInputKeySelector* Selector = Rig.MakeControl<UDreamInputKeySelector>(TEXT("Jump"), nullptr, FVector2D(220.0, 60.0));
		if (!TestNotNull(*Under(Case, TEXT("A key selector can be made on the rig")), Selector))
		{
			continue;
		}
		Selector->OnKeySelected.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleKeySelected);
		Rig.PumpFrames(1);

		UWorld* World = Rig.GetWorld();
		if (!TestTrue(Under(Case, TEXT("The game is paused")), PauseTheGame(World)))
		{
			continue;
		}
		ON_SCOPE_EXIT
		{
			UnpauseTheGame(World);
		};

		TestTrue(Under(Case, TEXT("Clicking the selector while paused completes")),
			Rig.Driver()->Find(FDreamBy::Name(TEXT("Jump")))->Click());
		if (!TestTrue(Under(Case, TEXT("The paused click armed the selector")), Selector->GetIsListening()))
		{
			Selector->BeginListening();
		}
		if (!TestTrue(Under(Case, TEXT("The selector is armed")), Selector->GetIsListening()))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The F key through the controller completes")), Rig.Driver()->Sequence().Type(EKeys::F).Perform());
		TestTrue(Under(Case, TEXT("The armed selector captured F in the paused game")), Selector->GetSelectedKey() == EKeys::F);
		TestEqual(Under(Case, TEXT("and reported it once")), Listener->KeySelectedCount, 1);
		TestFalse(Under(Case, TEXT("and stood down")), Selector->GetIsListening());
		TestTrue(Under(Case, TEXT("F went through the player controller")), ControllerSawKey(Rig, EKeys::F));
	}
	return true;
}

/**
 * The Enhanced Input preset plays with copies of its actions, and a project's own context may map one
 * more key to the ORIGINAL -- a pad button on the UI click. That key clicks the button, because the
 * original stays bound beside its copy; and it pauses as the original's own asset says (here, as the
 * shipped actions do, not while paused), while the preset's own key, through the copy, follows the
 * DreamUI setting and still clicks in the paused game.
 *
 * Standalone has no Input Actions, so only the Enhanced preset is asked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPausedGameHostOriginalActionKeyTest,
	"DreamGUI.Driver.GameHost.Paused.AKeyAProjectMapsToTheOriginalActionClicksAndPausesAsThatActionSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPausedGameHostOriginalActionKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPausedGameHostTestLocal;
	const FHostCase& Case = EnhancedHost;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
	{
		return false;
	}
	ADreamDriverEnhancedInputActor* InputActor = Cast<ADreamDriverEnhancedInputActor>(Rig.Context().InputActor);
	ULocalPlayer* LocalPlayer = Rig.Context().LocalPlayer;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer != nullptr ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!TestNotNull(*Under(Case, TEXT("The input actor is the Enhanced Input one")), InputActor)
		|| !TestNotNull(*Under(Case, TEXT("The local player has the Enhanced Input subsystem")), Subsystem))
	{
		return false;
	}
	const UInputAction* Copy = InputActor->GetDriverTriggerAction(EDreamUIMouseButtonType::Left);
	const UInputAction* Original = InputActor->GetOriginalAction(Copy);
	if (!TestNotNull(*Under(Case, TEXT("The actor has its left-button action")), Copy)
		|| !TestTrue(Under(Case, TEXT("and in play it is a copy of the one that was set, which is still there")), Original != nullptr && Original != Copy))
	{
		return false;
	}
	TestFalse(Under(Case, TEXT("The original, like the shipped action, does not trigger while paused")), Original->bTriggerWhenPaused);

	UDreamButton* Button = MakeListenedButton(Rig, Listener.Get());
	if (!TestNotNull(*Under(Case, TEXT("A button can be made on the rig")), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The project's own context: one more key, mapped to the original, above the preset's context.
	UInputMappingContext* ProjectContext = NewObject<UInputMappingContext>(GetTransientPackage(), NAME_None, RF_Transient);
	ProjectContext->MapKey(Original, ProjectKey());
	FModifyContextOptions ApplyNow;
	ApplyNow.bForceImmediately = true;
	Subsystem->AddMappingContext(ProjectContext, 1, ApplyNow);
	ON_SCOPE_EXIT
	{
		Subsystem->RemoveMappingContext(ProjectContext, ApplyNow);
	};

	TestTrue(Under(Case, TEXT("The pointer moves onto the button")), Rig.Driver()->Find(FDreamBy::Widget(Button))->Hover());
	TestTrue(Under(Case, TEXT("The project's key, and the frame its release lands in, complete")),
		Rig.Driver()->Sequence().Type(ProjectKey()).WaitFrames(1).Perform());
	TestEqual(Under(Case, TEXT("The key the project mapped to the original pressed the button once")), Listener->PressedCount, 1);
	TestEqual(Under(Case, TEXT("and clicked it once")), Listener->ClickedCount, 1);

	UWorld* World = Rig.GetWorld();
	if (!TestTrue(Under(Case, TEXT("The game is paused")), PauseTheGame(World)))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		UnpauseTheGame(World);
	};
	TestTrue(Under(Case, TEXT("The project's key while paused completes")),
		Rig.Driver()->Sequence().Type(ProjectKey()).WaitFrames(1).Perform());
	TestEqual(Under(Case, TEXT("Paused, the original -- which its asset keeps from triggering while paused -- clicked nothing")),
		Listener->ClickedCount, 1);
	TestTrue(Under(Case, TEXT("A click while paused completes")), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(Under(Case, TEXT("while the preset's own button, through the copy, clicked in the paused game")), Listener->ClickedCount, 2);
	TestTrue(Under(Case, TEXT("The project's key went through the player controller")), ControllerSawKey(Rig, ProjectKey()));
	return true;
}

/**
 * One input down both roads is one input. When the project's context maps the same key to the
 * original -- above the preset's context, with an action that lets its key through to lower contexts
 * (bConsumeInput false, which Enhanced Input then does not stop at) -- a single press starts both the
 * original and the copy in the same frame. Both are bound to the same handler; the button is pressed,
 * released and clicked once each, as SButton is by one mouse click.
 *
 * The test first shows the premise -- both mappings are on the player, and both actions did start --
 * so a single press cannot pass it by only ever having taken one road.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPausedGameHostOneInputTwoRoadsTest,
	"DreamGUI.Driver.GameHost.Paused.OneMouseClickComingDownTheCopyAndTheOriginalAtOnceIsOneClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPausedGameHostOneInputTwoRoadsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPausedGameHostTestLocal;
	const FHostCase& Case = EnhancedHost;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
	{
		return false;
	}
	ADreamDriverEnhancedInputActor* InputActor = Cast<ADreamDriverEnhancedInputActor>(Rig.Context().InputActor);
	ULocalPlayer* LocalPlayer = Rig.Context().LocalPlayer;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer != nullptr ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	APlayerController* Controller = Rig.GetPlayerController();
	UEnhancedPlayerInput* PlayerInput = Controller != nullptr ? Cast<UEnhancedPlayerInput>(Controller->PlayerInput) : nullptr;
	UEnhancedInputComponent* InputComponent = InputActor != nullptr ? Cast<UEnhancedInputComponent>(InputActor->InputComponent) : nullptr;
	if (!TestNotNull(*Under(Case, TEXT("The input actor is the Enhanced Input one")), InputActor)
		|| !TestNotNull(*Under(Case, TEXT("The local player has the Enhanced Input subsystem")), Subsystem)
		|| !TestNotNull(*Under(Case, TEXT("The player's PlayerInput is an EnhancedPlayerInput")), PlayerInput)
		|| !TestNotNull(*Under(Case, TEXT("The actor's input component is an EnhancedInputComponent")), InputComponent))
	{
		return false;
	}
	const UInputAction* Copy = InputActor->GetDriverTriggerAction(EDreamUIMouseButtonType::Left);
	const UInputAction* Original = InputActor->GetOriginalAction(Copy);
	if (!TestTrue(Under(Case, TEXT("The left-button action in play is a copy of one that is still there")),
		Copy != nullptr && Original != nullptr && Original != Copy))
	{
		return false;
	}

	UDreamButton* Button = MakeListenedButton(Rig, Listener.Get());
	if (!TestNotNull(*Under(Case, TEXT("A button can be made on the rig")), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// The original is the driver's in-memory stand-in for the shipped action, not an asset, so the test
	// may let it pass its key on; put back afterwards all the same.
	UInputAction* MutableOriginal = const_cast<UInputAction*>(Original);
	const bool bOriginalConsumed = MutableOriginal->bConsumeInput;
	MutableOriginal->bConsumeInput = false;
	ON_SCOPE_EXIT
	{
		MutableOriginal->bConsumeInput = bOriginalConsumed;
	};
	UInputMappingContext* ProjectContext = NewObject<UInputMappingContext>(GetTransientPackage(), NAME_None, RF_Transient);
	ProjectContext->MapKey(Original, EKeys::LeftMouseButton);
	FModifyContextOptions ApplyNow;
	ApplyNow.bForceImmediately = true;
	Subsystem->AddMappingContext(ProjectContext, 1, ApplyNow);
	ON_SCOPE_EXIT
	{
		Subsystem->RemoveMappingContext(ProjectContext, ApplyNow);
	};
	if (!TestTrue(Under(Case, TEXT("The player maps the left mouse button to the original, from the project's context")),
			PlayerInputMaps(*PlayerInput, EKeys::LeftMouseButton, Original))
		|| !TestTrue(Under(Case, TEXT("and to the copy, from the preset's")), PlayerInputMaps(*PlayerInput, EKeys::LeftMouseButton, Copy)))
	{
		return false;
	}

	const TSharedRef<int32> CopyStarted = MakeShared<int32>(0);
	const TSharedRef<int32> OriginalStarted = MakeShared<int32>(0);
	InputComponent->BindActionInstanceLambda(Copy, ETriggerEvent::Started,
		[CopyStarted](const FInputActionInstance&) { ++(*CopyStarted); });
	InputComponent->BindActionInstanceLambda(Original, ETriggerEvent::Started,
		[OriginalStarted](const FInputActionInstance&) { ++(*OriginalStarted); });

	TestTrue(Under(Case, TEXT("Clicking the button completes")), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(Under(Case, TEXT("The press came down the copy's road")), *CopyStarted, 1);
	TestEqual(Under(Case, TEXT("and down the original's, in the same frame")), *OriginalStarted, 1);
	TestEqual(Under(Case, TEXT("yet the button was pressed once")), Listener->PressedCount, 1);
	TestEqual(Under(Case, TEXT("released once")), Listener->ReleasedCount, 1);
	TestEqual(Under(Case, TEXT("and clicked once")), Listener->ClickedCount, 1);
	return true;
}

#endif
