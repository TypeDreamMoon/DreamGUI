// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverPieRig.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIBPLibrary.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Extensions/DreamGameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "IAssetViewport.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Interaction/UITextInput.h"
#include "Layout/Geometry.h"
#include "Layout/WidgetPath.h"
#include "LevelEditor.h"
#include "CoreGlobals.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "RHIGlobals.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#include "Driver/DreamDriverGameHost.h"
// Complete, because IsValid has to see that the context's module is a UObject.
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverVirtualCamera.h"
#include "Driver/DreamDriverWorldSpace.h"

namespace DreamDriverPieRigLocal
{
	/**
	 * The step every sequence from FDreamDriverPieRig::Sequence begins with.
	 *
	 * Two jobs, neither of which costs a frame. The first is to stop a sequence whose rig never came
	 * up before any of the test's own steps run: a latent command runs whether or not the one before it
	 * failed, and a test's Then lambda has every right to assume the world is there -- run against an
	 * empty context it would dereference null rather than fail. The second is to refresh the context's
	 * camera from the player's view at the moment the sequence starts, so a world-space pixel is worked
	 * out through the view the player has now rather than the one they had when the rig came up.
	 *
	 * It also holds the rig by shared reference, which is what keeps the context alive for exactly as
	 * long as FDreamDriverSequence's latent command -- which holds the context raw -- has steps to run.
	 */
	class FDreamPieGateStep : public IDreamDriverStep
	{
	public:
		explicit FDreamPieGateStep(const TSharedRef<FDreamDriverPieRig>& InRig)
			: Rig(InRig)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& /*InContext*/, float /*InDeltaSeconds*/) override
		{
			if (!Rig->IsAlive())
			{
				FailureReason = Rig->HasFailed()
					? FString::Printf(TEXT("the play-in-editor rig did not come up (%s), so none of this sequence's steps ran"), *Rig->GetFailure())
					: FString(TEXT("the play-in-editor rig is not up -- Start was not queued before this sequence, or Finish ran before it"));
				return EDreamDriverStepResult::Failed;
			}
			Rig->RefreshCameraFromPlayer();
			return EDreamDriverStepResult::Done;
		}

		virtual FString Describe() const override { return TEXT("PlayInEditorRigIsUp"); }
		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		TSharedRef<FDreamDriverPieRig> Rig;
		FString FailureReason;
	};

	/** A widget, as a failure message names it. */
	FString DescribeWidget(const TSharedPtr<SWidget>& InWidget)
	{
		return InWidget.IsValid() ? InWidget->GetTypeAsString() : FString(TEXT("nothing"));
	}

	/**
	 * Whether InWidget is InAncestor or sits somewhere inside it -- which is whether a character offered to
	 * InWidget bubbles up through InAncestor, the way FSlateApplication::ProcessKeyCharEvent bubbles one up
	 * the focus path from the focused widget.
	 */
	bool IsOnOrInside(const TSharedPtr<SWidget>& InWidget, const SWidget* InAncestor)
	{
		for (TSharedPtr<SWidget> Walk = InWidget; Walk.IsValid(); Walk = Walk->GetParentWidget())
		{
			if (Walk.Get() == InAncestor)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Everything between Slate's keyboard focus and the DreamGUI field that can stop a typed character,
	 * read just before one is sent. When the character does not arrive, the failure names what was shut,
	 * in the order the character meets it, instead of saying only that the character vanished.
	 */
	struct FCharacterGates
	{
		/** What holds the keyboard focus: the viewport widget itself, or something inside it. */
		FString FocusedWidget;
		bool bFocusIsTheViewportItself = false;
		/** Slate routes a character along the focus path rebuilt from the widget tree, so the viewport has to be reachable in it. */
		bool bViewportInTheWidgetTree = false;
		/** SViewport::OnKeyChar hands a character to its viewport interface, which has to be the session's scene viewport. */
		bool bWidgetForwardsToTheSession = false;
		/** FSceneViewport::OnKeyChar asks the viewport client only when it has one and has a size; otherwise it drops the character and answers handled. */
		bool bSceneViewportHasTheClient = false;
		FIntPoint SceneViewportSize = FIntPoint::ZeroValue;
		/** UGameViewportClient::InputChar offers the console every character first. */
		bool bConsoleOpen = false;
		bool bConsoleSwallowsTheNextCharacter = false;
		bool bClientIgnoresInput = false;
		/** In a play-in-editor viewport the engine's base viewport client claims every character (FGameplayViewportClient::InputChar). */
		bool bPlayInEditorViewport = false;
		FString ClientClass;
		bool bClientRoutesToDreamGUI = false;
		/** UUITextInput::RouteCharacterInputToActiveInput's target, and whether it is in the live edit UUITextInput::HandleCharacterInput insists on. */
		bool bFieldBeingEdited = false;
		bool bFieldInALiveEdit = false;
	};

	FCharacterGates ReadCharacterGates(UGameViewportClient& InClient, FSceneViewport& InSceneViewport,
		SViewport& InViewportWidget, const TSharedPtr<SWidget>& InFocused)
	{
		FCharacterGates Gates;
		Gates.FocusedWidget = DescribeWidget(InFocused);
		Gates.bFocusIsTheViewportItself = InFocused.Get() == static_cast<SWidget*>(&InViewportWidget);
		FWidgetPath PathToViewport;
		Gates.bViewportInTheWidgetTree = FSlateApplication::Get().FindPathToWidget(InViewportWidget.AsShared(), PathToViewport);
		const TSharedPtr<ISlateViewport> Interface = InViewportWidget.GetViewportInterface().Pin();
		Gates.bWidgetForwardsToTheSession = Interface.Get() == static_cast<ISlateViewport*>(&InSceneViewport);
		Gates.bSceneViewportHasTheClient = InSceneViewport.GetClient() == static_cast<FViewportClient*>(&InClient);
		Gates.SceneViewportSize = InSceneViewport.GetSizeXY();
		if (const UConsole* Console = InClient.ViewportConsole.Get())
		{
			Gates.bConsoleOpen = Console->ConsoleActive();
			// Closed, the console still answers with bCaptureKeyInput: its own toggle key sets it, so that the
			// character the toggle key makes is not typed into whatever is behind the console.
			Gates.bConsoleSwallowsTheNextCharacter = !Gates.bConsoleOpen && Console->bCaptureKeyInput;
		}
		Gates.bClientIgnoresInput = InClient.IgnoreInput();
		Gates.bPlayInEditorViewport = InSceneViewport.IsSlateViewport() && InSceneViewport.IsPlayInEditorViewport();
		Gates.ClientClass = InClient.GetClass()->GetName();
		Gates.bClientRoutesToDreamGUI = InClient.IsA<UDreamGameViewportClient>();
		const UUITextInput* Field = UUITextInput::GetActiveTextInput();
		Gates.bFieldBeingEdited = Field != nullptr;
		Gates.bFieldInALiveEdit = Field != nullptr && Field->IsInputActive();
		return Gates;
	}

	/**
	 * Why a character that was sent did not reach the field, from the gates as they stood when it was sent.
	 *
	 * Every gate that was shut is named, in the order the character meets them. Only when none was shut
	 * does the answer come from what the client must have done -- a client that routes to DreamGUI, every
	 * gate open, and still no character: it answered for the character without offering it to the field.
	 */
	FString DescribeUndeliveredCharacter(const FCharacterGates& InGates, TCHAR InCharacter, bool bInHandled)
	{
		TArray<FString> Shut;
		if (bInHandled && !InGates.bFocusIsTheViewportItself)
		{
			Shut.Add(FString::Printf(
				TEXT("%s holds the keyboard focus inside the play session's viewport, so it was offered the character first and took it"),
				*InGates.FocusedWidget));
		}
		if (!InGates.bViewportInTheWidgetTree)
		{
			Shut.Add(TEXT("Slate finds no visible path to the viewport widget (FSlateApplication::FindPathToWidget), so the focus path a character is routed along stops short of it"));
		}
		if (!InGates.bWidgetForwardsToTheSession)
		{
			Shut.Add(TEXT("the viewport widget does not forward to the session's scene viewport (SViewport::OnKeyChar hands a character to its viewport interface, and that is some other viewport)"));
		}
		if (!InGates.bSceneViewportHasTheClient)
		{
			Shut.Add(TEXT("the session's scene viewport has no viewport client, and FSceneViewport::OnKeyChar then drops a character without asking anybody"));
		}
		if (InGates.SceneViewportSize.X <= 0 || InGates.SceneViewportSize.Y <= 0)
		{
			Shut.Add(FString::Printf(
				TEXT("the session's scene viewport is %dx%d, and FSceneViewport::OnKeyChar drops a character without asking the viewport client while the viewport has no size"),
				InGates.SceneViewportSize.X, InGates.SceneViewportSize.Y));
		}
		if (InGates.bConsoleOpen)
		{
			Shut.Add(TEXT("the console is open, and UGameViewportClient::InputChar gives the console every character before anything else"));
		}
		if (InGates.bConsoleSwallowsTheNextCharacter)
		{
			Shut.Add(TEXT("the console is swallowing the character that follows its own toggle key (UConsole::bCaptureKeyInput)"));
		}
		if (InGates.bClientIgnoresInput)
		{
			Shut.Add(FString::Printf(TEXT("the viewport client (%s) ignores input (UGameViewportClient::IgnoreInput)"), *InGates.ClientClass));
		}
		if (!InGates.bFieldBeingEdited)
		{
			Shut.Add(TEXT("no DreamGUI field was being edited (UUITextInput::GetActiveTextInput is null), so there was nothing to route it to"));
		}
		else if (!InGates.bFieldInALiveEdit)
		{
			Shut.Add(TEXT("the field UUITextInput::GetActiveTextInput names is not in a live edit, and UUITextInput::HandleCharacterInput refuses a character outside one"));
		}

		FString Why;
		if (Shut.Num() > 0)
		{
			Why = FString::Join(Shut, TEXT("; and "));
		}
		else if (!InGates.bClientRoutesToDreamGUI)
		{
			Why = FString::Printf(
				TEXT("the play session's viewport client is a %s, which does not route characters to DreamGUI; UDreamGameViewportClient does, as does any client that calls UUITextInput::RouteCharacterInputToActiveInput from its InputChar"),
				*InGates.ClientClass);
		}
		else if (bInHandled && InGates.bPlayInEditorViewport)
		{
			Why = FString::Printf(
				TEXT("every gate up to the field was open, and the viewport client (%s) answered for the character without offering it to the field. In a play-in-editor viewport the engine's base viewport client claims every character, so that none reaches the editor's own frame (FGameplayViewportClient::InputChar), and a client that asks its base first and returns on its yes -- taking that yes for the console's -- never gets as far as UUITextInput::RouteCharacterInputToActiveInput"),
				*InGates.ClientClass);
		}
		else
		{
			Why = FString::Printf(TEXT("every gate up to the field was open, and yet the field being edited never received it (viewport client %s)"), *InGates.ClientClass);
		}
		return FString::Printf(TEXT("character %d did not reach the DreamGUI field being edited (Slate reported it %s): %s"),
			static_cast<int32>(InCharacter), bInHandled ? TEXT("handled") : TEXT("unhandled"), *Why);
	}

	/**
	 * One character, typed the way the platform types it into a game.
	 *
	 * A key that makes a character reaches Slate as WM_CHAR; FSlateApplication::OnKeyChar makes an
	 * FCharacterEvent of it for the keyboard's user and hands it to ProcessKeyCharEvent, which routes it
	 * along that user's FOCUS path, from the focused widget up. With the focus on the play session's
	 * viewport widget that is SViewport::OnKeyChar, then FSceneViewport::OnKeyChar, then the viewport
	 * client's InputChar inside the play world -- the console first -- and, in UDreamGameViewportClient,
	 * UUITextInput::RouteCharacterInputToActiveInput to the field being edited. The step plays the
	 * operating system's part and nothing else: it calls ProcessKeyCharEvent rather than OnKeyChar only so
	 * that the event carries no modifier -- the desk's own Shift key is not the test's business -- and
	 * every gate after that is the engine's or the runtime's.
	 *
	 * DELIVERED means the field being edited received the character: UUITextInput::HandleCharacterInput
	 * ran for it. Not "Slate said handled", because a play-in-editor viewport answers handled for every
	 * character whether anything received it or not (see DescribeUndeliveredCharacter). The one trace a
	 * received character leaves, whether or not the field then accepts it, is the process-wide "a host
	 * delivers characters" switch, which HandleCharacterInput raises as it takes a character into a live
	 * edit. So the switch is lowered for exactly the length of the dispatch and read afterwards, and put
	 * back when nothing raised it: after the step it holds what it would have held had nobody looked. A
	 * field that received a character and refused it (a letter into a numeric field) has had it
	 * delivered; what the field made of it is the test's to assert.
	 *
	 * One frame per character, as a keyboard delivers them, and the same shape as the driver's own
	 * character step: apply, then give the next engine frame to it.
	 */
	class FDreamPieViewportCharacterStep : public IDreamDriverStep
	{
	public:
		explicit FDreamPieViewportCharacterStep(TCHAR InCharacter)
			: Character(InCharacter)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float /*InDeltaSeconds*/) override
		{
			if (bDelivered)
			{
				return EDreamDriverStepResult::Done;
			}
			UGameViewportClient* Client = InContext.World != nullptr ? InContext.World->GetGameViewport() : nullptr;
			FSceneViewport* SceneViewport = Client != nullptr ? Client->GetGameViewport() : nullptr;
			const TSharedPtr<SViewport> ViewportWidget = Client != nullptr ? Client->GetGameViewportWidget() : TSharedPtr<SViewport>();
			if (Client == nullptr)
			{
				FailureReason = TEXT("there is no play session viewport to type into");
				return EDreamDriverStepResult::Failed;
			}
			if (SceneViewport == nullptr || !ViewportWidget.IsValid())
			{
				FailureReason = TEXT("the play session's viewport client has no scene viewport or no viewport widget, so there is nothing a typed character could reach");
				return EDreamDriverStepResult::Failed;
			}
			if (!FSlateApplication::IsInitialized())
			{
				FailureReason = TEXT("Slate is not running, and a typed character is Slate's to deliver");
				return EDreamDriverStepResult::Failed;
			}
			FSlateApplication& SlateApp =FSlateApplication::Get();
			// The keyboard's user, the one FSlateApplication::OnKeyChar makes a typed character for. A
			// variable of the exact type, because FCharacterEvent also has a constructor taking an input device.
			const uint32 KeyboardUser = static_cast<uint32>(SlateApp.GetUserIndexForKeyboard());

			// The focus gate, before anything is sent. A typed character goes where the keyboard focus is,
			// and in an editor that could be whatever a person last clicked -- a details panel's text box --
			// so a character is only sent while the focus is on the session's viewport or inside it.
			const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(KeyboardUser);
			if (!IsOnOrInside(Focused, ViewportWidget.Get()))
			{
				FailureReason = FString::Printf(
					TEXT("Slate's keyboard focus is on %s rather than the play session's viewport, and a typed character goes where the keyboard focus is, so nothing was sent. The rig hands the keyboard to the viewport when the session comes up: either it could not (the probe reports whether it could), or something has taken the focus since"),
					*DescribeWidget(Focused));
				return EDreamDriverStepResult::Failed;
			}

			const FCharacterGates Gates = ReadCharacterGates(*Client, *SceneViewport, *ViewportWidget, Focused);
			const bool bSwitchBefore = UUITextInput::IsHostDeliveringCharacterEvents();
			UUITextInput::SetHostDeliversCharacterEventsForTesting(false);
			// No modifier held, whatever the real keyboard on this machine is doing: the character is the
			// whole of the event, and a test that read the desk's Shift key would depend on the desk.
			const FCharacterEvent Event(Character, FModifierKeysState(), KeyboardUser, false);
			const bool bHandled = SlateApp.ProcessKeyCharEvent(Event);
			const bool bReceived = UUITextInput::IsHostDeliveringCharacterEvents();
			if (!bReceived)
			{
				UUITextInput::SetHostDeliversCharacterEventsForTesting(bSwitchBefore);
				FailureReason = DescribeUndeliveredCharacter(Gates, Character, bHandled);
				return EDreamDriverStepResult::Failed;
			}
			bDelivered = true;
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override
		{
			// The code, not the glyph: a newline in an error message would split it across two lines.
			return FString::Printf(TEXT("TypeThroughViewport(character %d)"), static_cast<int32>(Character));
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		TCHAR Character;
		bool bDelivered = false;
		FString FailureReason;
	};

	/**
	 * A widget the rig made, found by the name it was made with rather than by searching under the
	 * context's root. FDreamBy searches under the root, and a world-space panel is a root of its own,
	 * so anything on one is invisible to it; the rig remembering what it made is the smallest thing
	 * that is not.
	 *
	 * Weak to the rig: a locator can be copied into a sequence that outlives the test body, and it
	 * must not be what keeps a finished rig -- and the pointers it no longer holds -- around.
	 */
	class FDreamPieMadeLocator : public IDreamElementLocator
	{
	public:
		FDreamPieMadeLocator(const TWeakPtr<const FDreamDriverPieRig>& InRig, const FString& InDisplayName)
			: Rig(InRig)
			, DisplayName(InDisplayName)
		{
		}

		virtual void Locate(UDreamWidget* /*InRoot*/, TArray<UDreamWidget*>& OutWidgets) const override
		{
			if (const TSharedPtr<const FDreamDriverPieRig> Pinned = Rig.Pin())
			{
				if (UDreamWidget* Found = Pinned->FindMade(DisplayName))
				{
					OutWidgets.Add(Found);
				}
			}
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("MadeOnThePlayInEditorRig(%s)"), *DisplayName);
		}

	private:
		TWeakPtr<const FDreamDriverPieRig> Rig;
		FString DisplayName;
	};

	/** "WxH", for the reports. */
	FString DescribeSize(const FIntPoint& InSize)
	{
		return FString::Printf(TEXT("%dx%d"), InSize.X, InSize.Y);
	}
}

TSharedRef<FDreamDriverPieRig> FDreamDriverPieRig::Create(FAutomationTestBase& InTest, const FDreamPieRigOptions& InOptions)
{
	return MakeShared<FDreamDriverPieRig>(InTest, InOptions);
}

FDreamDriverPieRig::FDreamDriverPieRig(FAutomationTestBase& InTest, const FDreamPieRigOptions& InOptions)
	: Test(&InTest)
	, Options(InOptions)
{
	DriverContext = MakeUnique<FDreamDriverContext>();
	DriverContext->CurrentTest = Test;
	DriverContext->InputHost = Options.InputHost;
	// From birth rather than from the moment the session is up: a context whose frames belong to the
	// engine must refuse to be pumped by hand at every point of its life, including the part before
	// it has a world -- an element action called on it by mistake then fails loudly instead of
	// driving a pump that is not there.
	DriverContext->bEnginePumped = true;
	// Created unconditionally, as the headless rig does, so Driver() is always answerable.
	DriverInstance = MakeShared<FDreamDriver>(*DriverContext);
	Report.DestinationUsed = Options.Destination;
}

FDreamDriverPieRig::~FDreamDriverPieRig()
{
	// Only reached with a session still running when Finish never got to the end -- a test whose
	// queued commands were thrown away. Ending the session is still this rig's job: the next test would
	// otherwise begin inside it. RequestEndPlayMap only sets a flag the editor's next tick acts on, so it
	// is safe from wherever the last reference happens to be dropped.
	if (bSessionStarted && !bSessionEnded && GEditor != nullptr && !IsEngineExitRequested()
		&& GEditor->IsPlayingSessionInEditor())
	{
		GEditor->RequestEndPlayMap();
	}
	// Not RestoreEditorState: the session has only been asked to end, and the viewport size it hands back
	// to the editor as it ends has not been handed back yet.
	ReleasePlayWorld();
	if (!IsEngineExitRequested())
	{
		RestoreProcessState();
	}
}

void FDreamDriverPieRig::Enqueue(TFunction<bool()> InUpdate)
{
	// Straight into the framework's queue rather than through ADD_LATENT_AUTOMATION_COMMAND: the macro
	// takes a class declaration, and a lambda's capture list would be cut at its first comma.
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(MoveTemp(InUpdate)));
}

void FDreamDriverPieRig::Start()
{
	const TSharedRef<FDreamDriverPieRig> Self = AsShared();
	Enqueue([Self]() -> bool
	{
		return Self->UpdateStart();
	});
}

void FDreamDriverPieRig::WhenReady(TFunction<void(FDreamDriverPieRig&)> InAction)
{
	const TSharedRef<FDreamDriverPieRig> Self = AsShared();
	// Negative until the action has run; then the frames still owed to layout.
	const TSharedRef<int32> FramesLeft = MakeShared<int32>(-1);
	TFunction<void(FDreamDriverPieRig&)> Action = MoveTemp(InAction);
	Enqueue([Self, Action, FramesLeft]() -> bool
	{
		if (*FramesLeft < 0)
		{
			if (!Self->IsAlive())
			{
				// The rig said why when it failed; saying it again here would be one more line of the
				// same news in the report.
				return true;
			}
			if (Action)
			{
				Action(*Self);
			}
			*FramesLeft = FMath::Max(Self->Options.SettleFrames, 0);
		}
		// Yield until the count is spent and report done on the Update after the last yield. A latent
		// command that finishes lets the next one run in the SAME engine frame, so finishing on the
		// frame the widgets were made would hand the next step a tree nobody has laid out yet.
		if (*FramesLeft > 0)
		{
			--(*FramesLeft);
			return false;
		}
		return true;
	});
}

FDreamDriverSequence FDreamDriverPieRig::Sequence()
{
	FDreamDriverSequence NewSequence(*DriverContext);
	NewSequence.Add(MakeShared<DreamDriverPieRigLocal::FDreamPieGateStep>(AsShared()));
	return NewSequence;
}

void FDreamDriverPieRig::Finish()
{
	const TSharedRef<FDreamDriverPieRig> Self = AsShared();
	Enqueue([Self]() -> bool
	{
		return Self->UpdateFinish();
	});
}

FDreamDriverSequence& FDreamDriverPieRig::TypeThroughViewport(FDreamDriverSequence& InSequence, const FString& InText)
{
	for (int32 CharacterIndex = 0; CharacterIndex < InText.Len(); ++CharacterIndex)
	{
		InSequence.Add(MakeShared<DreamDriverPieRigLocal::FDreamPieViewportCharacterStep>(InText[CharacterIndex]));
	}
	return InSequence;
}

bool FDreamDriverPieRig::FailStart(const FString& InReason)
{
	Failure = InReason;
	bAlive = false;
	StartPhase = EStartPhase::Failed;
	if (Test != nullptr)
	{
		Test->AddError(FString::Printf(TEXT("The play-in-editor rig did not come up: %s."), *InReason));
	}
	// A half-attached input host is taken down now, while its pointers are still good; the rest of
	// what was half-built stays in the play world, which Finish ends.
	TearDownInputHost();
	ReleasePlayWorld();
	return true;
}

void FDreamDriverPieRig::TearDownInputHost()
{
	FDreamDriverContext& HostContext = *DriverContext;
	// Only while the world the context points into is the live play world: after the session ends the
	// pointers would be dangling, and the engine has already destroyed everything they named.
	const bool bContextIsLive = HostContext.World != nullptr && GEditor != nullptr && GEditor->PlayWorld == HostContext.World;
	if (bContextIsLive && (HostContext.InputActor != nullptr || HostContext.EventSystem != nullptr))
	{
		DreamDriverGameHost::Teardown(HostContext);
	}
}

bool FDreamDriverPieRig::IsPlayerReady() const
{
	UWorld* PlayWorld = GEditor != nullptr ? GEditor->PlayWorld.Get() : nullptr;
	if (PlayWorld == nullptr || !PlayWorld->AreActorsInitialized())
	{
		return false;
	}
	// The engine's own readiness test for a PIE world (FStartPIEForAutomationCommand): the game mode
	// has started the match, which is the point at which the player has been spawned and possessed.
	const AGameStateBase* GameState = PlayWorld->GetGameState();
	if (GameState == nullptr || !GameState->HasMatchStarted())
	{
		return false;
	}
	const APlayerController* Controller = PlayWorld->GetFirstPlayerController();
	return Controller != nullptr
		&& Controller->GetLocalPlayer() != nullptr
		&& Controller->PlayerCameraManager != nullptr
		&& PlayWorld->GetGameViewport() != nullptr;
}

bool FDreamDriverPieRig::UpdateStart()
{
	const double Now = FPlatformTime::Seconds();
	++FramesInPhase;

	switch (StartPhase)
	{
	case EStartPhase::NotStarted:
	{
		// What the process had before this test touched it, taken before anything could change it.
		bHostDeliveredCharacterEventsAtStart = UUITextInput::IsHostDeliveringCharacterEvents();
		bRememberedProcessState = true;

		if (GEditor == nullptr || GEngine == nullptr)
		{
			return FailStart(TEXT("there is no editor engine to play in"));
		}
		if (Options.InputHost == EDreamRigInputHost::ModuleOnly)
		{
			// The headless rig's arrangement feeds the module directly, which is exactly the shortcut
			// this layer exists to not take.
			return FailStart(TEXT("a play-in-editor rig takes its input through an input actor; ModuleOnly is the headless rig's arrangement"));
		}
		if (GEditor->IsPlaySessionInProgress())
		{
			// Not this test's session, and not one to join: whatever it holds was built by somebody
			// else. Ended, and this test starts its own once it is gone.
			if (Test != nullptr)
			{
				Test->AddWarning(TEXT("A play session was still running when this test began, and was ended first; the test that started it did not end it."));
			}
			if (GEditor->IsPlayingSessionInEditor())
			{
				GEditor->RequestEndPlayMap();
			}
			else
			{
				GEditor->CancelRequestPlaySession();
			}
			StartPhase = EStartPhase::WaitForLeftoverSession;
		}
		else
		{
			StartPhase = EStartPhase::MakeMap;
		}
		PhaseStartSeconds = Now;
		FramesInPhase = 0;
		return false;
	}

	case EStartPhase::WaitForLeftoverSession:
	{
		if (!GEditor->IsPlaySessionInProgress() && GEditor->PlayWorld == nullptr)
		{
			StartPhase = EStartPhase::MakeMap;
			PhaseStartSeconds = Now;
			FramesInPhase = 0;
			return false;
		}
		if (Now - PhaseStartSeconds > Options.StopTimeoutSeconds)
		{
			return FailStart(FString::Printf(TEXT("a play session left running by an earlier test did not end within %.0f seconds"), Options.StopTimeoutSeconds));
		}
		return false;
	}

	case EStartPhase::MakeMap:
	{
		// A blank map rather than whatever the project opens with: a heavy startup map makes every
		// session slow to duplicate and fills the world with things that are not under test.
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (EditorWorld != nullptr && !FApp::IsUnattended())
		{
			// Only when somebody may be sitting at this editor. Replacing the map discards it without
			// asking, and an unattended run has nobody's unsaved work to lose -- an interactive one might.
			const UPackage* MapPackage = EditorWorld->GetPackage();
			if (MapPackage != nullptr && MapPackage->IsDirty() && !FPackageName::IsTempPackage(MapPackage->GetName()))
			{
				return FailStart(FString::Printf(
					TEXT("the level open in the editor (%s) has unsaved changes, and a play-in-editor test replaces it with a blank map; save it, or run the tests unattended"),
					*MapPackage->GetName()));
			}
		}
		UWorld* BlankMap = nullptr;
#if WITH_AUTOMATION_TESTS
		BlankMap = FAutomationEditorCommonUtils::CreateNewMap();
#else
		BlankMap = GEditor->NewMap();
#endif
		if (BlankMap == nullptr)
		{
			return FailStart(TEXT("the editor would not open a blank map"));
		}
		// One frame on the new map before a session duplicates it, so the level viewports that follow
		// the editor world have moved over to it.
		StartPhase = EStartPhase::Launch;
		PhaseStartSeconds = Now;
		FramesInPhase = 0;
		return false;
	}

	case EStartPhase::Launch:
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		Params.SessionDestination = EPlaySessionDestinationType::InProcess;

		// A settings object of the rig's own, the way UEditorEngine::AutomationLoadMap makes one, so
		// nothing here writes into the editor's saved play settings. It starts from the user's values
		// (NewObject copies the class default, which is the loaded config) and states every one that
		// changes what a test sees.
		ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();
		PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
		PlaySettings->SetPlayNumberOfClients(1);
		PlaySettings->bLaunchSeparateServer = false;
		PlaySettings->SetRunUnderOneProcess(true);
		// Never the real mouse: the pointer is the driver's, and a session that captured the cursor
		// would take it from whoever is at the desk.
		PlaySettings->GameGetsMouseControl = false;
		PlaySettings->UseMouseForTouch = false;
		// A blank map has nothing to compile, and recompiling whatever else is dirty in memory would
		// make this test report other people's Blueprint errors.
		PlaySettings->AutoRecompileBlueprints = false;
		PlaySettings->EnableGameSound = false;
		PlaySettings->NewWindowWidth = Options.ViewportSize.X;
		PlaySettings->NewWindowHeight = Options.ViewportSize.Y;
		PlaySettings->CenterNewWindow = true;
		PlaySettings->PIEAlwaysOnTop = false;
		PlaySettings->bShouldMinimizeEditorOnNonVRPIE = false;
		Params.EditorPlaySettings = PlaySettings;

		// The engine's base game mode, not the project's: a project game mode spawns its own pawn, HUD
		// and UI, any of which can sit in front of what a test points at. The base one is still a real
		// game mode -- it logs the player in, spawns and possesses a default pawn, and starts the match.
		Params.GameModeOverride = AGameModeBase::StaticClass();
		// Where the player starts, stated rather than taken from the editor camera, so every run looks
		// at the same thing.
		Params.StartLocation = Options.PlayerStartLocation;
		Params.StartRotation = Options.PlayerStartRotation;

		Report.DestinationUsed = EDreamPieViewportDestination::NewWindow;
		if (Options.Destination == EDreamPieViewportDestination::LevelEditorViewport)
		{
			// The same test FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport makes
			// before trusting a level viewport: it has to be in a window, or the session has nowhere to
			// draw and the viewport never gets a size.
			if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
			{
				const TSharedPtr<IAssetViewport> LevelViewport = LevelEditor->GetFirstActiveViewport();
				if (LevelViewport.IsValid() && FSlateApplication::IsInitialized()
					&& FSlateApplication::Get().FindWidgetWindow(LevelViewport->AsWidget()).IsValid())
				{
					Params.DestinationSlateViewport = TWeakPtr<IAssetViewport>(LevelViewport);
					Report.DestinationUsed = EDreamPieViewportDestination::LevelEditorViewport;
					// The level viewport's own scene viewport, and its size, as the rig found them. The session
					// takes its first size from it (SLevelViewport::StartPlayInEditorSession swaps the two with
					// FSceneViewport::OnPlayWorldViewportSwapped) and hands its last size back to it when it ends
					// (EndPlayInEditorSession does the same the other way round), so a size the rig gives the
					// session ends up here -- and RestoreEditorState has to know what was here before.
					EditorViewport = LevelViewport->GetSharedActiveViewport();
					if (const TSharedPtr<FSceneViewport> FoundViewport = EditorViewport.Pin())
					{
						Report.EditorViewportSizeAtStart = FoundViewport->GetSizeXY();
					}
				}
			}
		}

		/*
		 * THE VIEWPORT CLIENT CLASS, swapped for exactly the span of the one call that reads it.
		 *
		 * UEditorEngine::CreateInnerProcessPIEGameInstance makes the session's viewport client with
		 * NewObject<UGameViewportClient>(this, GameViewportClientClass) -- the engine's member, read
		 * once, while the session is being created. StartQueuedPlaySessionRequest creates an
		 * in-process session synchronously (StartPlayInEditorSession -> CreateNewPlayInEditorInstance
		 * -> CreateInnerProcessPIEGameInstance), which is how the engine's own
		 * FStartPIEForAutomationCommand starts one, so the whole of the read happens inside these three
		 * lines. Putting the project's class back straight after -- rather than when the session ends --
		 * means no failure, abort or thrown-away latent queue can ever leave the editor making
		 * DreamGUI viewport clients for somebody else's play session.
		 */
		const TSubclassOf<UGameViewportClient> ProjectViewportClientClass = GEngine->GameViewportClientClass;
		GEngine->GameViewportClientClass = UDreamGameViewportClient::StaticClass();
		// The same span, for the same reason, for the one play setting a level viewport reads from the
		// user's saved settings rather than from the request: SLevelViewport::StartPlayInEditorSession
		// asks the class default whether the game gets mouse control. Held off, so a session in a level
		// viewport can never capture the cursor of whoever is at the desk; put back before the session
		// ends, because ending it saves the class default to the user's config.
		ULevelEditorPlaySettings* SavedPlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
		const bool bSavedGameGetsMouseControl = SavedPlaySettings->GameGetsMouseControl;
		SavedPlaySettings->GameGetsMouseControl = false;
		GEditor->RequestPlaySession(Params);
		GEditor->StartQueuedPlaySessionRequest();
		SavedPlaySettings->GameGetsMouseControl = bSavedGameGetsMouseControl;
		GEngine->GameViewportClientClass = ProjectViewportClientClass;

		LaunchSeconds = Now;
		if (GEditor->PlayWorld == nullptr)
		{
			if (GEditor->IsPlaySessionRequestQueued())
			{
				GEditor->CancelRequestPlaySession();
			}
			return FailStart(TEXT("the editor did not start a play session; see what PIE logged for why"));
		}
		bSessionStarted = true;
		StartPhase = EStartPhase::WaitForPlayer;
		PhaseStartSeconds = Now;
		FramesInPhase = 0;
		ReadyFramesSeen = 0;
		return false;
	}

	case EStartPhase::WaitForPlayer:
	{
		if (GEditor->PlayWorld == nullptr || !GEditor->IsPlayingSessionInEditor())
		{
			return FailStart(TEXT("the play session ended before its player was ready"));
		}
		if (IsPlayerReady())
		{
			// Two frames with the player in place, not one: the camera manager caches its view during
			// the player controller's tick, and the viewport is laid out for its players when it is
			// drawn, both of which the frame the match started may already be past.
			if (++ReadyFramesSeen < 2)
			{
				return false;
			}
			Report.SecondsToStart = Now - LaunchSeconds;
			Report.FramesToStart = FramesInPhase;
			const FString WhyNot = BuildOnPlayWorld();
			if (!WhyNot.IsEmpty())
			{
				return FailStart(WhyNot);
			}
			bAlive = true;
			SettleFramesLeft = FMath::Max(Options.SettleFrames, 0);
			StartPhase = EStartPhase::Settle;
			PhaseStartSeconds = Now;
			FramesInPhase = 0;
			if (SettleFramesLeft == 0)
			{
				StartPhase = EStartPhase::Up;
				return true;
			}
			return false;
		}
		ReadyFramesSeen = 0;
		if (Now - PhaseStartSeconds > Options.StartTimeoutSeconds)
		{
			UWorld* PlayWorld = GEditor->PlayWorld.Get();
			const AGameStateBase* GameState = PlayWorld != nullptr ? PlayWorld->GetGameState() : nullptr;
			const APlayerController* Controller = PlayWorld != nullptr ? PlayWorld->GetFirstPlayerController() : nullptr;
			return FailStart(FString::Printf(
				TEXT("the play session's player was not ready after %.1f seconds (actors initialized: %s, game state: %s, match started: %s, player controller: %s, local player: %s, camera manager: %s, viewport client: %s)"),
				Options.StartTimeoutSeconds,
				PlayWorld != nullptr && PlayWorld->AreActorsInitialized() ? TEXT("yes") : TEXT("no"),
				GameState != nullptr ? TEXT("yes") : TEXT("no"),
				GameState != nullptr && GameState->HasMatchStarted() ? TEXT("yes") : TEXT("no"),
				Controller != nullptr ? TEXT("yes") : TEXT("no"),
				Controller != nullptr && Controller->GetLocalPlayer() != nullptr ? TEXT("yes") : TEXT("no"),
				Controller != nullptr && Controller->PlayerCameraManager != nullptr ? TEXT("yes") : TEXT("no"),
				PlayWorld != nullptr && PlayWorld->GetGameViewport() != nullptr ? TEXT("yes") : TEXT("no")));
		}
		return false;
	}

	case EStartPhase::Settle:
	{
		// Same shape as WhenReady's count: yield while frames are owed, report done on the Update
		// after the last yield, so whatever is queued next meets a laid-out tree.
		if (SettleFramesLeft > 0)
		{
			--SettleFramesLeft;
			if (SettleFramesLeft > 0)
			{
				return false;
			}
		}
		StartPhase = EStartPhase::Up;
		return true;
	}

	case EStartPhase::Up:
	case EStartPhase::Failed:
	default:
		return true;
	}
}

FString FDreamDriverPieRig::BuildOnPlayWorld()
{
	UWorld* PlayWorld = GEditor != nullptr ? GEditor->PlayWorld.Get() : nullptr;
	if (PlayWorld == nullptr)
	{
		return TEXT("the editor has no play world");
	}
	Report.PlayWorldName = PlayWorld->GetName();

	APlayerController* Controller = PlayWorld->GetFirstPlayerController();
	ULocalPlayer* Player = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
	if (Player == nullptr)
	{
		return TEXT("the play world's first player controller has no local player");
	}
	Report.PlayerControllerClass = Controller->GetClass()->GetName();
	if (const AGameModeBase* GameMode = PlayWorld->GetAuthGameMode())
	{
		Report.GameModeClass = GameMode->GetClass()->GetName();
	}

	UGameViewportClient* Client = PlayWorld->GetGameViewport();
	if (Client == nullptr)
	{
		return TEXT("the play world has no game viewport client");
	}
	Report.ViewportClientClass = Client->GetClass()->GetName();
	if (!Client->IsA<UDreamGameViewportClient>())
	{
		// Said as a finding about the engine, because that is what it would be: the class was the
		// engine's member for the whole of session creation, so a different client means PIE no longer
		// reads it where UEditorEngine::CreateInnerProcessPIEGameInstance read it in 5.8.
		return FString::Printf(
			TEXT("the play session's viewport client is a %s although GameViewportClientClass named UDreamGameViewportClient while the session was created"),
			*Report.ViewportClientClass);
	}
	if (Player->ViewportClient != Client)
	{
		return TEXT("the local player is not attached to the play session's viewport client");
	}

	FSceneViewport* SceneViewport = Client->GetGameViewport();
	if (SceneViewport == nullptr)
	{
		return TEXT("the play session's viewport client has no scene viewport");
	}
	Report.ViewportSizeAsStarted = SceneViewport->GetSizeXY();
	if (Report.ViewportSizeAsStarted.X <= 0 || Report.ViewportSizeAsStarted.Y <= 0)
	{
		/*
		 * A viewport with no size is a headless one: FSceneViewport only learns its size when Slate
		 * paints the widget it sits in, and a headless editor paints nothing. With no size every road
		 * this layer is about is shut -- FSceneViewport::OnKeyChar drops characters, the canvas reads
		 * a zero viewport through the player controller, and ULocalPlayer::GetProjectionData refuses
		 * to project -- so the viewport is given one, through the same public call the designer
		 * driver uses for its headless viewport. A viewport that has a size keeps it.
		 *
		 * The size outlives the session when it plays in the level editor's viewport: ending the session
		 * copies it into the editor's own scene viewport (see EditorViewport), which the next session
		 * starts from. RestoreEditorState puts that one back once the session is gone.
		 */
		SceneViewport->SetFixedViewportSize(static_cast<uint32>(Options.ViewportSize.X), static_cast<uint32>(Options.ViewportSize.Y));
		Report.bViewportSizeWasGiven = true;
	}
	Report.ViewportSize = SceneViewport->GetSizeXY();
	if (Report.ViewportSize.X <= 0 || Report.ViewportSize.Y <= 0)
	{
		return FString::Printf(
			TEXT("the play session's viewport came up %s and would not take %s (FSceneViewport::SetFixedViewportSize needs its widget to be in a window)"),
			*DreamDriverPieRigLocal::DescribeSize(Report.ViewportSizeAsStarted), *DreamDriverPieRigLocal::DescribeSize(Options.ViewportSize));
	}
	// The keyboard next, the way a player's click into the viewport hands it over; see the function.
	GiveViewportKeyboardFocus(Client);
	Report.FrameSecondsAtStart = FApp::GetDeltaTime();

	// The default pawn binds the arrow keys and the mouse axes to moving and turning. Held still, so a
	// key a test presses moves focus and not the camera a world-space pixel was worked out through.
	Controller->SetIgnoreMoveInput(true);
	Controller->SetIgnoreLookInput(true);

	FDreamDriverContext& BuildContext = *DriverContext;
	BuildContext.World = PlayWorld;
	BuildContext.GameInstance = PlayWorld->GetGameInstance();
	BuildContext.PlayerController = Controller;
	BuildContext.LocalPlayer = Player;
	BuildContext.Manager = UDreamUIManagerWorldSubsystem::GetInstance(PlayWorld);
	BuildContext.InputHost = Options.InputHost;
	BuildContext.bEnginePumped = true;
	BuildContext.CurrentTest = Test;
	ViewportClient = Client;
	if (!IsValid(BuildContext.Manager))
	{
		return TEXT("the play world has no DreamUI manager");
	}

	// The input actor, the game's own class with the driver's module in it, on the player the engine
	// logged in. Its BeginPlay runs as it spawns -- the play world has begun -- which is where it
	// registers its module and binds its keys on the player's input stack, exactly as a placed one does.
	FString WhyNot;
	if (!DreamDriverGameHost::AttachInputActor(BuildContext, Controller, Options.InputHost, WhyNot))
	{
		return FString::Printf(TEXT("the input actor could not be attached: %s"), *WhyNot);
	}
	if (!IsValid(BuildContext.EventSystem) || !IsValid(BuildContext.InputModule))
	{
		return TEXT("the input actor was attached but the context has no event system or no input module");
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	HostActor = PlayWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (HostActor == nullptr)
	{
		return TEXT("the rig's host actor would not spawn in the play world");
	}

	/*
	 * The screen-space root, built the way the headless rig builds it (register, then the canvas, then
	 * its render mode) with two differences a play world forces:
	 *  - NO VIEWPORT OVERRIDE. The canvas asks the player controller for the viewport size, which is
	 *    the real viewport's -- that is the path this layer is here to exercise.
	 *  - BEGUN BY HAND. The play world's UI manager has already begun play, so nothing will begin a root
	 *    made now; the runtime's own creation roads (RegisterAndPark, RegisterDreamWidgetHierarchy) begin
	 *    a widget made after the manager has begun, and this does what they do.
	 */
	UDreamWidget* BuiltRoot = NewObject<UDreamWidget>(PlayWorld, NAME_None, RF_Transient);
	BuiltRoot->SetDisplayName(TEXT("Root"));
	BuiltRoot->OnRegister();
	BuildContext.Root = BuiltRoot;
	UDreamCanvas* BuiltCanvas = BuiltRoot->AddComponent<UDreamCanvas>();
	if (BuiltCanvas == nullptr)
	{
		return TEXT("the screen root would not take a canvas");
	}
	BuiltCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	BuildContext.RootCanvas = BuiltCanvas;
	if (BuildContext.Manager->HasBegunPlay() && !BuiltRoot->HasBegunPlay())
	{
		BuiltRoot->BeginPlay();
	}

	UDreamScreenSpaceRaycaster* BuiltRaycaster = NewObject<UDreamScreenSpaceRaycaster>(HostActor, NAME_None, RF_Transient);
	// The player the input actor speaks for, stated rather than assumed: LineTrace only asks raycasters
	// whose user index is the event system's.
	BuiltRaycaster->SetUserIndex(BuildContext.EventSystem->GetUserIndex());
	BuiltRaycaster->SetRootCanvas(BuiltCanvas);
	HostActor->AddInstanceComponent(BuiltRaycaster);
	BuiltRaycaster->RegisterComponent();
	// Explicit, as in the headless rig: AddRaycaster refuses duplicates, and a raycaster that missed the
	// list the pointer module walks would make every test fail the same unhelpful way.
	BuiltRaycaster->ActivateRaycaster();
	BuildContext.Raycaster = BuiltRaycaster;

	Report.CanvasViewportSize = BuiltCanvas->GetViewportSize();

	FString CameraWhyNot;
	if (!RefreshCameraFromPlayer(&CameraWhyNot))
	{
		return FString::Printf(TEXT("the player's view could not be mirrored into the context's camera: %s"), *CameraWhyNot);
	}
	return FString();
}

bool FDreamDriverPieRig::RefreshCameraFromPlayer(FString* OutWhyNot)
{
	FDreamDriverContext& CameraContext = *DriverContext;
	APlayerController* Controller = CameraContext.PlayerController;
	if (Controller == nullptr)
	{
		if (OutWhyNot != nullptr)
		{
			*OutWhyNot = TEXT("there is no player controller to look through");
		}
		return false;
	}
	/*
	 * World's DreamDriverWorld::MirrorPlayerView, the one copy of "what this player sees": the three
	 * reads of ULocalPlayer::GetViewPoint (protected, so made there in its order -- the camera manager's
	 * cached view, its FOV, the controller's view point), the viewport client's size, and the player's
	 * own aspect-ratio axis constraint. That is what GetProjectionData -- and so the production
	 * world-space raycaster -- starts from. What GetViewPoint adds after it (a locked view, scene view
	 * extensions moving the view point) is absent from a plain session, and would show up as the
	 * world-space smoke test's ray check disagreeing, which is what that check is for.
	 */
	FDreamDriverVirtualCamera Mirrored;
	FString WhyNot;
	if (!DreamDriverWorld::MirrorPlayerView(Controller, Mirrored, WhyNot))
	{
		if (OutWhyNot != nullptr)
		{
			*OutWhyNot = WhyNot;
		}
		return false;
	}
	if (CameraContext.Camera.IsValid())
	{
		*CameraContext.Camera = Mirrored;
	}
	else
	{
		CameraContext.Camera = MakeShared<FDreamDriverVirtualCamera>(Mirrored);
	}
	return true;
}

bool FDreamDriverPieRig::UpdateFinish()
{
	const double Now = FPlatformTime::Seconds();
	switch (FinishPhase)
	{
	case EFinishPhase::NotStarted:
	{
		// The input host first, while the play world is still whole: DreamDriverGameHost::Teardown
		// destroys the input actor it attached (its EndPlay takes the event system out of the UI
		// manager) and forgets what it was still owed for this player, and leaves the session's own
		// player controller and local player alone -- those belong to the session.
		TearDownInputHost();
		// Then everything this rig points at in the play world is let go BEFORE the session is asked to
		// end. Ending it collects the play world and treats anything of it still referenced as a leak
		// (UEditorEngine::EndPlayMap), and after it ends these pointers would dangle.
		bAlive = false;
		ReleasePlayWorld();
		if (bSessionStarted && !bSessionEnded && GEditor != nullptr && GEditor->IsPlaySessionInProgress())
		{
			// Requested, not called: EndPlayMap tears down worlds and collects garbage, and the engine
			// wants that done from its own tick (the request is acted on at the end of the next
			// UEditorEngine::Tick) rather than from inside whatever is running a latent command.
			if (GEditor->IsPlayingSessionInEditor())
			{
				GEditor->RequestEndPlayMap();
			}
			else
			{
				GEditor->CancelRequestPlaySession();
			}
			FinishPhase = EFinishPhase::WaitForSessionToEnd;
			PhaseStartSeconds = Now;
			return false;
		}
		// No session to end -- it never started, or it ended on its own -- so whatever it carried out of
		// the play world has already landed, and can be put back now.
		bSessionEnded = true;
		RestoreEditorState();
		RestoreProcessState();
		FinishPhase = EFinishPhase::Done;
		return true;
	}

	case EFinishPhase::WaitForSessionToEnd:
	{
		if (GEditor == nullptr || (!GEditor->IsPlaySessionInProgress() && GEditor->PlayWorld == nullptr))
		{
			Report.SecondsToStop = Now - PhaseStartSeconds;
			bSessionEnded = true;
			// Only now: the session hands its viewport's size back to the editor's viewport as it ends, so
			// putting that back any earlier would be undone by the ending itself.
			RestoreEditorState();
			RestoreProcessState();
			FinishPhase = EFinishPhase::Done;
			return true;
		}
		if (Now - PhaseStartSeconds > Options.StopTimeoutSeconds)
		{
			if (Test != nullptr)
			{
				Test->AddError(FString::Printf(
					TEXT("The play session did not end within %.0f seconds of being asked to; the tests after this one may start inside it."),
					Options.StopTimeoutSeconds));
			}
			// The editor's own state stays as it is: the session that would hand its viewport's size back
			// has not ended, and the error above already says the next test inherits it.
			RestoreProcessState();
			FinishPhase = EFinishPhase::Done;
			return true;
		}
		return false;
	}

	case EFinishPhase::Done:
	default:
		return true;
	}
}

void FDreamDriverPieRig::ReleasePlayWorld()
{
	if (DriverContext.IsValid())
	{
		FDreamDriverContext& ReleasedContext = *DriverContext;
		ReleasedContext.World = nullptr;
		ReleasedContext.EventSystem = nullptr;
		ReleasedContext.InputModule = nullptr;
		ReleasedContext.Manager = nullptr;
		ReleasedContext.Raycaster = nullptr;
		ReleasedContext.Root = nullptr;
		ReleasedContext.RootCanvas = nullptr;
		ReleasedContext.GameInstance = nullptr;
		ReleasedContext.PlayerController = nullptr;
		ReleasedContext.LocalPlayer = nullptr;
		ReleasedContext.InputActor = nullptr;
		ReleasedContext.Camera.Reset();
	}
	HostActor = nullptr;
	ViewportClient = nullptr;
	WorldPointers.Reset();
	MadeWidgets.Reset();
}

void FDreamDriverPieRig::RestoreProcessState()
{
	if (!bRememberedProcessState || bRestoredProcessState)
	{
		return;
	}
	bRestoredProcessState = true;
	// Typing through the viewport client turns this switch on for the whole process (the first
	// HandleCharacterInput sets it), after which the key road stops guessing characters in every test
	// that follows. Put back as it was found, as the headless rig does.
	UUITextInput::SetHostDeliversCharacterEventsForTesting(bHostDeliveredCharacterEventsAtStart);
}

void FDreamDriverPieRig::GiveViewportKeyboardFocus(UGameViewportClient* InClient)
{
	/*
	 * A typed character goes where Slate's keyboard focus is -- FSlateApplication::ProcessKeyCharEvent
	 * routes it along the focus path -- so it reaches the session only if the session's viewport has the
	 * focus. The engine gives the viewport the focus only when the game takes the mouse:
	 * SLevelViewport::StartPlayInEditorSession clears the keyboard focus; the ActivateGameViewport that
	 * FSlateApplication::RegisterGameViewport runs needs an active application and then only focuses
	 * through FSceneViewport::OnViewportActivated, which acquires focus only when the session captures
	 * the mouse on activation; and UEditorEngine's own PIE start focuses the game viewport only when the
	 * game gets mouse control. The rig holds that off (see Launch), so this is a session a player hands
	 * the keyboard to by clicking into its viewport -- and the rig's pointer does not go through Slate,
	 * so the rig does to the keyboard what that click does.
	 *
	 * Through FSlateApplication::SetUserFocusToGameViewport, the engine's own call for it (a player being
	 * added, a paused session resuming), for the keyboard's user, whom a typed character belongs to. With
	 * EFocusCause::SetDirectly rather than Mouse: for a focus that came from the mouse,
	 * FSceneViewport::OnFocusReceived in the editor captures and locks the real cursor, and the rig never
	 * takes the cursor of whoever is at the desk. Not ActivateGameViewport either: that also brings the
	 * editor's window to the front of the desktop.
	 */
	if (InClient == nullptr || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const TSharedPtr<SViewport> ViewportWidget = InClient->GetGameViewportWidget();
	if (!ViewportWidget.IsValid())
	{
		return;
	}
	FSlateApplication& SlateApp =FSlateApplication::Get();
	const uint32 KeyboardUser = static_cast<uint32>(SlateApp.GetUserIndexForKeyboard());
	const SWidget* const ViewportAsWidget = ViewportWidget.Get();
	const TSharedPtr<SWidget> FocusedAtStart = SlateApp.GetUserFocusedWidget(KeyboardUser);
	// On or inside: a widget inside the viewport with the focus -- a game's own -- already receives the
	// character first, and taking the focus from it would be changing the game rather than typing into it.
	Report.bKeyboardFocusOnViewport = DreamDriverPieRigLocal::IsOnOrInside(FocusedAtStart, ViewportAsWidget);
	if (!Report.bKeyboardFocusOnViewport)
	{
		SlateApp.SetUserFocusToGameViewport(KeyboardUser, EFocusCause::SetDirectly);
		const TSharedPtr<SWidget> FocusedNow = SlateApp.GetUserFocusedWidget(KeyboardUser);
		if (FocusedNow != FocusedAtStart)
		{
			// Both ends remembered, so that Finish takes back exactly this move and nothing a test or a
			// person did with the focus afterwards.
			Report.bKeyboardFocusGiven = true;
			KeyboardFocusGivenTo = FocusedNow;
			KeyboardFocusBeforeGiving = FocusedAtStart;
		}
	}
	Report.bKeyboardFocusOnViewportWhenUp = DreamDriverPieRigLocal::IsOnOrInside(SlateApp.GetUserFocusedWidget(KeyboardUser), ViewportAsWidget);
}

bool FDreamDriverPieRig::IsKeyboardFocusStillGiven() const
{
	const TSharedPtr<SWidget> GivenTo = KeyboardFocusGivenTo.Pin();
	if (!GivenTo.IsValid() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const FSlateApplication& SlateApp =FSlateApplication::Get();
	return SlateApp.GetUserFocusedWidget(static_cast<uint32>(SlateApp.GetUserIndexForKeyboard())) == GivenTo;
}

void FDreamDriverPieRig::RestoreEditorState()
{
	if (bRestoredEditorState)
	{
		return;
	}
	bRestoredEditorState = true;

	// The keyboard, taken back only while it is still where the rig put it: focus that has moved on since
	// belongs to whatever moved it. What had it before gets it back; if nothing had, nothing has.
	if (IsKeyboardFocusStillGiven())
	{
		FSlateApplication& SlateApp =FSlateApplication::Get();
		const uint32 KeyboardUser = static_cast<uint32>(SlateApp.GetUserIndexForKeyboard());
		if (const TSharedPtr<SWidget> Before = KeyboardFocusBeforeGiving.Pin())
		{
			SlateApp.SetUserFocus(KeyboardUser, Before, EFocusCause::SetDirectly);
		}
		else
		{
			SlateApp.ClearUserFocus(KeyboardUser, EFocusCause::SetDirectly);
		}
	}

	/*
	 * The editor's level viewport, which the ended session left at the size the rig gave the session
	 * (SLevelViewport::EndPlayInEditorSession -> FSceneViewport::OnPlayWorldViewportSwapped: "play world
	 * viewports should always be the same size"). Put back only when that is exactly what happened: the
	 * rig gave a size, the viewport holds that size now, and it held another before. A size it has been
	 * given since by anything else -- a window finally laid out -- is kept.
	 */
	const TSharedPtr<FSceneViewport> LevelViewport = EditorViewport.Pin();
	if (!Report.bViewportSizeWasGiven || !LevelViewport.IsValid())
	{
		return;
	}
	const FIntPoint SizeAtStart = Report.EditorViewportSizeAtStart;
	const FIntPoint SizeNow = LevelViewport->GetSizeXY();
	if (SizeNow == SizeAtStart || SizeNow != Report.ViewportSize)
	{
		return;
	}
	if ((SizeAtStart.X <= 0 || SizeAtStart.Y <= 0) && !GUsingNullRHI)
	{
		// Putting a viewport back to no size re-creates its render target at no size, and a real RHI's
		// texture validation refuses a zero extent. Under the null RHI -- where a viewport comes up with no
		// size in the first place, because nothing paints it -- there is no such target to refuse.
		if (Test != nullptr)
		{
			Test->AddWarning(FString::Printf(
				TEXT("The level editor viewport this session played in had no size, the rig gave the session %s, and ending the session carried that size into the editor's viewport. It is left at %s, because giving a viewport back no size re-creates its render target at no size, which a real RHI refuses; the next play session in this editor starts at that size."),
				*DreamDriverPieRigLocal::DescribeSize(Report.ViewportSize), *DreamDriverPieRigLocal::DescribeSize(SizeNow)));
		}
		return;
	}
	// The very call the engine carries the size across with, back the other way.
	LevelViewport->UpdateViewportRHI(false, static_cast<uint32>(SizeAtStart.X), static_cast<uint32>(SizeAtStart.Y), EWindowMode::Windowed, PF_Unknown);
	Report.bEditorViewportSizeRestored = true;
}

UWorld* FDreamDriverPieRig::GetWorld() const
{
	return bAlive ? DriverContext->World : nullptr;
}

APlayerController* FDreamDriverPieRig::GetPlayerController() const
{
	return bAlive ? DriverContext->PlayerController : nullptr;
}

ULocalPlayer* FDreamDriverPieRig::GetLocalPlayer() const
{
	return bAlive ? DriverContext->LocalPlayer : nullptr;
}

UGameViewportClient* FDreamDriverPieRig::GetViewportClient() const
{
	return bAlive ? ViewportClient : nullptr;
}

FSceneViewport* FDreamDriverPieRig::GetSceneViewport() const
{
	UGameViewportClient* Client = GetViewportClient();
	return Client != nullptr ? Client->GetGameViewport() : nullptr;
}

UDreamWidget* FDreamDriverPieRig::Root() const
{
	return bAlive ? DriverContext->Root : nullptr;
}

UDreamCanvas* FDreamDriverPieRig::RootCanvas() const
{
	return bAlive ? DriverContext->RootCanvas : nullptr;
}

UDreamScreenSpaceRaycaster* FDreamDriverPieRig::Raycaster() const
{
	return bAlive ? DriverContext->Raycaster : nullptr;
}

AActor* FDreamDriverPieRig::GetHostActor() const
{
	return bAlive ? HostActor : nullptr;
}

FDreamDriverContext& FDreamDriverPieRig::Context() const
{
	return *DriverContext;
}

FDreamDriverRef FDreamDriverPieRig::Driver() const
{
	return DriverInstance.ToSharedRef();
}

void FDreamDriverPieRig::RememberMade(const FString& InDisplayName, UDreamWidget* InWidget)
{
	if (InWidget != nullptr)
	{
		// Last one wins for a repeated name, as a later widget of the same name is the one a test
		// just made and is about to aim at.
		MadeWidgets.Add(InDisplayName, InWidget);
	}
}

FDreamLocatorRef FDreamDriverPieRig::Made(const FString& InDisplayName) const
{
	return MakeShared<DreamDriverPieRigLocal::FDreamPieMadeLocator>(TWeakPtr<const FDreamDriverPieRig>(AsShared()), InDisplayName);
}

UDreamWidget* FDreamDriverPieRig::FindMade(const FString& InDisplayName) const
{
	if (!bAlive)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UDreamWidget>* Found = MadeWidgets.Find(InDisplayName);
	UDreamWidget* Widget = Found != nullptr ? Found->Get() : nullptr;
	return IsValid(Widget) ? Widget : nullptr;
}

UDreamWidget* FDreamDriverPieRig::MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
	const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	return MakeWidgetWithVisual(UDreamVisualEmpty::StaticClass(), InDisplayName, InParent, InSize, InAnchoredPosition);
}

UDreamWidget* FDreamDriverPieRig::MakeWidgetWithVisual(TSubclassOf<UDreamVisual> InVisualClass, const FString& InDisplayName,
	UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	FDreamDriverContext& MakeContext = *DriverContext;
	if (!bAlive || MakeContext.World == nullptr)
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : MakeContext.Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}

	// FDreamDriverRig::MakeWidget's order, for its reason: the visual last, once the widget has a
	// parent and therefore a render canvas to be enrolled with.
	UDreamWidget* NewWidget = NewObject<UDreamWidget>(MakeContext.World, NAME_None, RF_Transient);
	NewWidget->SetDisplayName(InDisplayName);
	NewWidget->SetWidth(InSize.X);
	NewWidget->SetHeight(InSize.Y);
	NewWidget->OnRegister();
	NewWidget->TrySetParent(Parent, false);
	NewWidget->SetAnchoredPosition(InAnchoredPosition);
	if (InVisualClass.Get() != nullptr)
	{
		NewWidget->CreateNewVisual(InVisualClass);
	}
	// Begun, because the manager has: the rule the runtime's own creation roads apply.
	if (IsValid(MakeContext.Manager) && MakeContext.Manager->HasBegunPlay() && !NewWidget->HasBegunPlay())
	{
		NewWidget->BeginPlay();
	}
	RememberMade(InDisplayName, NewWidget);
	return NewWidget;
}

UDreamWidget* FDreamDriverPieRig::MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName,
	UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	FDreamDriverContext& MakeContext = *DriverContext;
	if (!bAlive || MakeContext.World == nullptr || !IsValid(InClass))
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : MakeContext.Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}
	// The runtime's own factory, exactly as the headless rig calls it; see FDreamDriverRig::MakeControl.
	// Nothing like its EnsureGameInputHost is needed first: a play world has its player controller and
	// its registered event system already.
	UDreamUserWidget* Control = CreateDreamWidget(MakeContext.World, InClass, Parent,
		[&InDisplayName, &InSize](UDreamUserWidget* InBuilt)
		{
			InBuilt->SetDisplayName(InDisplayName);
			InBuilt->SetWidth(InSize.X);
			InBuilt->SetHeight(InSize.Y);
		});
	if (Control == nullptr)
	{
		return nullptr;
	}
	Control->SetAnchoredPosition(InAnchoredPosition);
	RememberMade(InDisplayName, Control);
	return Control;
}

UDreamWidget* FDreamDriverPieRig::MakeWorldPanel(const FString& InDisplayName, const FTransform& InTransform, const FVector2D& InSize)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AActor* PanelHost = World->SpawnActor<AActor>(AActor::StaticClass(), InTransform, SpawnParameters);
	if (PanelHost == nullptr)
	{
		return nullptr;
	}
	// A scene root for the panel to hang on, made the way the world-space raycast fixture makes one.
	USceneComponent* HostRoot = NewObject<USceneComponent>(PanelHost, TEXT("PanelHost"), RF_Transient);
	PanelHost->SetRootComponent(HostRoot);
	HostRoot->RegisterComponent();
	HostRoot->SetWorldTransform(InTransform);

	// The runtime's two calls for a world-space tree, the ones a prefab presenter makes: construct the
	// root (registered, begun, parked), give it a WorldSpace canvas, then attach it to the scene
	// component, which places it and takes it out of the parked state.
	UDreamWidget* Panel = UDreamUIBPLibrary::ConstructWidget(World, InDisplayName, nullptr);
	if (Panel == nullptr)
	{
		return nullptr;
	}
	Panel->SetWidth(InSize.X);
	Panel->SetHeight(InSize.Y);
	UDreamCanvas* PanelCanvas = Panel->AddComponent<UDreamCanvas>();
	if (PanelCanvas == nullptr)
	{
		Panel->DestroyWidget();
		return nullptr;
	}
	PanelCanvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	if (!UDreamUIBPLibrary::AttachWidgetToSceneComponent(Panel, HostRoot))
	{
		Panel->DestroyWidget();
		return nullptr;
	}
	RememberMade(InDisplayName, Panel);
	return Panel;
}

UDreamWorldSpaceRaycaster* FDreamDriverPieRig::AddWorldPointer()
{
	AActor* Host = GetHostActor();
	if (Host == nullptr)
	{
		return nullptr;
	}
	UDreamWorldSpaceRaycaster* Pointer = NewObject<UDreamWorldSpaceRaycaster>(Host, NAME_None, RF_Transient);
	UDreamEventSystem* Events = DriverContext->EventSystem;
	Pointer->SetUserIndex(IsValid(Events) ? Events->GetUserIndex() : 0);
	Host->AddInstanceComponent(Pointer);
	Pointer->RegisterComponent();
	Pointer->ActivateRaycaster();
	WorldPointers.Add(Pointer);
	return Pointer;
}
