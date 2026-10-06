// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverSequence.h"

#include "Controls/DreamInputKeySelector.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "Framework/Commands/InputChord.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputKeyEventArgs.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/DreamUIVirtualCursor.h"
#include "Interaction/UITextInput.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DefinePrivateMemberPtr.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"

#include "Driver/DreamDriverGameHost.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverKeys.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSlateHost.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDriver, Log, All);

/*
 * The world's Sequencer tick, as UWorld::Tick broadcasts it (LevelTick.cpp, "Tick level sequence actors first"). The
 * delegate is a private member of UWorld with no public broadcast: UMovieSceneSequenceTickManager::Get binds the
 * world's tick manager to it through the public AddMovieSceneSequenceTickHandler, and only UWorld::Tick ever calls it.
 * The headless pump stands in for UWorld::Tick, so it needs the one call; the engine's own way to name a private
 * member without friendship (Misc/DefinePrivateMemberPtr.h) is how it gets it, and nothing about the delegate, the
 * world or the tick manager is changed by doing so. At global scope, as the macro requires.
 */
UE_DEFINE_PRIVATE_MEMBER_PTR(FOnMovieSceneSequenceTick, GDreamDriverWorldMovieSceneSequenceTick, UWorld, MovieSceneSequenceTick);

namespace DreamDriverPumpLocal
{
	/**
	 * Tick one tickable world subsystem if, and only if, FTickableGameObject::TickObjects would tick
	 * it this frame -- the same four questions in the same order: is it initialized (what
	 * UTickableWorldSubsystem::IsAllowedToTick answers), is it registered to tick at all (Never is
	 * not on the list), is it tickable now (asked unless it ticks Always), and is this a frame it
	 * ticks in (a game world that is not paused, or one it ticks through while paused, or an editor
	 * that ticks it regardless). Anything the engine would skip, this skips.
	 *
	 * Through the UTickableWorldSubsystem pointer, where the engine interface is public, so a
	 * subclass that re-declares an override in a narrower section changes nothing here.
	 */
	void TickAsTheEngineWould(UWorld& InWorld, UTickableWorldSubsystem* InSubsystem, float InDeltaSeconds)
	{
		if (InSubsystem == nullptr || !InSubsystem->IsInitialized())
		{
			return;
		}
		const ETickableTickType TickType = InSubsystem->GetTickableTickType();
		if (TickType == ETickableTickType::Never)
		{
			return;
		}
		if (TickType != ETickableTickType::Always && !InSubsystem->IsTickable())
		{
			return;
		}
		if (InSubsystem->GetTickableGameObjectWorld() != &InWorld)
		{
			return;
		}
		const bool bTicksInEditor = GIsEditor && InSubsystem->IsTickableInEditor();
		const bool bTicksInGame = InWorld.IsGameWorld() && (!InWorld.IsPaused() || InSubsystem->IsTickableWhenPaused());
		if (!bTicksInEditor && !bTicksInGame)
		{
			return;
		}
		InSubsystem->Tick(InDeltaSeconds);
	}

	/**
	 * The world's clocks, advanced exactly as UWorld::Tick's "Update time" block advances them.
	 *
	 * The pointer pipeline asks the WORLD what time it is, not the pump what the delta was:
	 * UDreamPointerInputModule fires a long press when the pointer clock
	 * (UDreamEventSystem::GetPointerClockSeconds, the world's GetRealTimeSeconds) minus the press time
	 * has reached the threshold, decides a click continues a run when the gap since the last click is
	 * short enough, and schedules navigation repeat against the same clock, as do both raycasters'
	 * hold-to-drag; a text field times its held press on GetRealTimeSeconds too; the tween manager
	 * reads DeltaTimeSeconds and DeltaRealTimeSeconds. A world made with UWorld::CreateWorld and never
	 * ticked has every one of them frozen at zero, which makes every second click a double click and
	 * every press exactly as old as the one before it. Advancing them is what makes a pumped frame a
	 * frame rather than a repetition of the same instant.
	 *
	 * The engine's rules, not a simplification of them, because the differences are exactly what a
	 * pause or a slow-motion test is about: real time always advances by the real delta; the game
	 * delta is the real one times the world's effective time dilation, clamped by FixupDeltaSeconds;
	 * the unpaused clock advances by the game delta always; the game clock (TimeSeconds) and the
	 * audio clock only while the world is not paused. For a world that is neither paused nor
	 * dilated -- every rig that does not ask -- all four clocks move by the frame length, as before.
	 */
	void AdvanceWorldClock(UWorld& InWorld, float InDeltaSeconds)
	{
		const bool bIsPaused = InWorld.IsPaused();

		InWorld.RealTimeSeconds += InDeltaSeconds;
		if (!bIsPaused)
		{
			InWorld.AudioTimeSeconds += InDeltaSeconds;
		}

		float GameDeltaSeconds = InDeltaSeconds;
		// Unchecked: a world that has no settings actor is one to leave undilated, not one to assert on.
		if (AWorldSettings* Settings = InWorld.GetWorldSettings(/*bCheckStreamingPersistent*/false, /*bChecked*/false))
		{
			GameDeltaSeconds *= Settings->GetEffectiveTimeDilation();
			GameDeltaSeconds = Settings->FixupDeltaSeconds(GameDeltaSeconds, InDeltaSeconds);
		}
		InWorld.DeltaTimeSeconds = GameDeltaSeconds;
		InWorld.DeltaRealTimeSeconds = InDeltaSeconds;

		InWorld.UnpausedTimeSeconds += GameDeltaSeconds;
		if (!bIsPaused)
		{
			InWorld.TimeSeconds += GameDeltaSeconds;
		}
	}

	/**
	 * One tick group's worth of tweens: what ADreamTweenTickHelperActor (DuringPhysics, from its own
	 * Tick) and its three UDreamTweenTickHelperComponent (PrePhysics, PostPhysics, PostUpdateWork) step
	 * in a game, stepped here directly.
	 *
	 * Directly, because the helper cannot be the one to do it here: it only learns which manager to
	 * drive in its BeginPlay (SetupTick), and a rig's world never begins play as a whole -- the rig
	 * opens the UI manager's gate, not UWorld::BeginPlay -- so the helper that
	 * UDreamTweenTickHelperWorldSubsystem spawns into every Game world sits there with no target, and
	 * the world is never ticked anyway. Not through the call the helpers make,
	 * UDreamTweenManager::TickFromWorld: that steps a tick group at most once per engine frame, so
	 * that a second helper cannot double every tween's speed, and the headless pump runs many frames
	 * inside one engine frame, each a frame of its own for the tweens. Tick steps the group every time.
	 *
	 * Regardless of pause, because the helper's tick functions all set bTickEvenWhenPaused: the
	 * engine runs them in a paused frame too, and it is each tween that decides whether a pause
	 * reaches it (UDreamTweener::ToNext, with the pause and dilation flags that
	 * UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation gives every widget tween).
	 * The manager reads the world's own DeltaTimeSeconds and DeltaRealTimeSeconds for every tick type
	 * but Manual, so what is passed is the dilated delta, for the look of the thing.
	 */
	void TickTweens(UDreamTweenManager* InTweenManager, const UWorld* InWorld, EDreamTweenTickType InTickType)
	{
		if (InTweenManager == nullptr || InWorld == nullptr)
		{
			return;
		}
		InTweenManager->Tick(InTickType, InWorld->DeltaTimeSeconds);
	}

	/**
	 * The world's Sequencer tick: UWorld::MovieSceneSequenceTick broadcast with the frame's game delta, which is the call
	 * UWorld::Tick makes right after its "Update time" block and OnWorldPreActorTick, before any tick group.
	 *
	 * What is bound there is the world's UMovieSceneSequenceTickManager, made the first time anything asks for it
	 * (UMovieSceneSequenceTickManager::Get) -- in a rig, the first widget animation that plays, through DreamGUI's
	 * animation ticker (UDreamUIAnimationTicker::FindOrCreate), or a player registering with the world's manager directly
	 * under DreamUI.Animation.Ticker 0. Its TickSequenceActors keeps every rule of its own: each tick-interval group ticks
	 * once its interval has passed, measured on the world's unpaused and game clocks, a client that does not tick when
	 * paused is skipped in a paused world, and the runners are flushed and the latent actions run after the clients. The
	 * ticker ticks only its playing players, and they evaluate their animations in that flush. So the delta is the
	 * world's own DeltaTimeSeconds, dilated and fixed up exactly as UWorld::Tick hands it on (AdvanceWorldClock wrote it).
	 *
	 * A world nothing has asked has nothing bound and the broadcast is no call at all, which is every rig that never
	 * plays a Sequencer animation.
	 */
	void TickSequencer(UWorld& InWorld)
	{
		FOnMovieSceneSequenceTick& SequenceTick = InWorld.*GDreamDriverWorldMovieSceneSequenceTick;
		if (SequenceTick.IsBound())
		{
			SequenceTick.Broadcast(InWorld.DeltaTimeSeconds);
		}
	}

	/** Say something went wrong with the context itself, where a report will show it. */
	void ReportContextError(const FDreamDriverContext& InContext, const FString& InMessage)
	{
		FAutomationTestBase* ReportingTest = InContext.CurrentTest != nullptr
			? InContext.CurrentTest
			: FAutomationTestFramework::Get().GetCurrentTest();
		if (ReportingTest != nullptr)
		{
			ReportingTest->AddError(InMessage);
		}
		else
		{
			UE_LOG(LogDreamDriver, Error, TEXT("%s"), *InMessage);
		}
	}
}

TArray<UClass*> FDreamDriverContext::GetPumpedTickableWorldSubsystems()
{
	// In the order the pump ticks them. The four have no order between them in the engine either --
	// TickObjects walks them in registration order, which is the order the subsystem collection
	// happened to create them -- and none reads another's output from the same frame, so this is
	// simply the order they were added in. The UI manager is last, and is driven through TickDreamUI.
	return TArray<UClass*>{
		UDreamUIActionRouter::StaticClass(),
		UDreamUIDragDropSubsystem::StaticClass(),
		UDreamUITooltipSubsystem::StaticClass(),
		UDreamUIVirtualCursorSubsystem::StaticClass(),
		UDreamUIManagerWorldSubsystem::StaticClass(),
	};
}

bool FDreamDriverContext::IsUsable() const
{
	return World != nullptr
		&& IsValid(EventSystem)
		&& IsValid(InputModule)
		&& IsValid(Manager)
		&& IsValid(Root);
}

FDreamDriverContext* FDreamDriverContext::FindPlayer(int32 InPlayerIndex)
{
	if (InPlayerIndex == PlayerIndex)
	{
		return this;
	}
	for (FDreamDriverContext* Player : Players)
	{
		if (Player != nullptr && Player->PlayerIndex == InPlayerIndex)
		{
			return Player;
		}
	}
	return nullptr;
}

const FDreamDriverContext* FDreamDriverContext::FindPlayer(int32 InPlayerIndex) const
{
	return const_cast<FDreamDriverContext*>(this)->FindPlayer(InPlayerIndex);
}

void FDreamDriverContext::PumpOneFrame(float InDeltaSeconds)
{
	using namespace DreamDriverPumpLocal;

	if (bEnginePumped)
	{
		// Refused, not tolerated: under the engine pump every frame already runs all of the below,
		// and a second run inside the same frame would tick the event system, the subsystems and the
		// layout twice -- two clicks for one, two layout passes where the runtime makes one.
		ReportContextError(*this, TEXT("PumpOneFrame was called on a context whose frames belong to the engine (a PIE rig). Build a sequence and PerformLatent it; the engine's own frame is the pump there."));
		return;
	}

	// A frame is the world's. Another player's context hands it to player 0's, which ticks every player's controller
	// in it; pumping from here as well would be a second frame for everything but this player's input.
	if (PlayerIndex != 0)
	{
		if (FDreamDriverContext* PumpContext = FindPlayer(0); PumpContext != nullptr && PumpContext != this)
		{
			PumpContext->PumpOneFrame(InDeltaSeconds);
			return;
		}
	}

	/*
	 * The order below is UWorld::Tick's (LevelTick.cpp), with each piece at the point where the
	 * engine runs it:
	 *
	 *   clock                     "Update time", before any tick group
	 *   Sequencer animations      MovieSceneSequenceTick, after OnWorldPreActorTick and before the tick groups
	 *   player input              every player controller ticks in TG_PrePhysics (PlayerTick -> TickPlayerInput)
	 *   PrePhysics tweens         ADreamTweenTickHelperActor's TG_PrePhysics component
	 *   DuringPhysics tweens      the helper actor's own tick, TG_DuringPhysics
	 *   input                     the input subsystem's tick function, TG_PostPhysics
	 *   PostPhysics tweens        the helper's TG_PostPhysics component
	 *   tickable world subsystems FTickableGameObject::TickObjects, after the physics groups
	 *   PostUpdateWork tweens     the helper's TG_PostUpdateWork component -- see below
	 *   UI manager                TickObjects too, and deliberately last -- see below
	 *
	 * Within one tick group the engine fixes no order between unrelated tick functions, so where two
	 * share a group (the input frame and the PostPhysics tweens) the order here is a choice: input
	 * first, so a tween that input started this frame takes its first step in the same frame.
	 */
	if (World != nullptr)
	{
		AdvanceWorldClock(*World, InDeltaSeconds);
		// Before every tick group, as UWorld::Tick runs it: an animation that moves a widget this frame has moved it
		// before input traces and before the UI manager lays out. See TickSequencer.
		TickSequencer(*World);
	}

	if (IsActorInputHost(InputHost))
	{
		/*
		 * Each player controller's own input frame, when input comes through one: the keys and
		 * buttons queued on it since the last frame go through its input stack now, the input
		 * actor's bindings hand them to its module, and the module queues them for the event system
		 * below -- the same frame, in the same order, as TG_PrePhysics then TG_DuringPhysics in a game.
		 * In player order: the engine fixes none between controllers, which tick in one group, and
		 * the order they were spawned in is the one a game's level list walks. Under ModuleOnly and
		 * SlateSource nothing is queued on a controller, so there is nothing to tick.
		 */
		if (Players.Num() == 0)
		{
			if (PlayerController != nullptr)
			{
				DreamDriverGameHost::TickPlayerInput(*this, InDeltaSeconds);
			}
		}
		else
		{
			for (FDreamDriverContext* Player : Players)
			{
				if (Player != nullptr && Player->PlayerController != nullptr)
				{
					DreamDriverGameHost::TickPlayerInput(*Player, InDeltaSeconds);
				}
			}
		}
	}

	UDreamTweenManager* TweenManager = GameInstance != nullptr ? GameInstance->GetSubsystem<UDreamTweenManager>() : nullptr;
	TickTweens(TweenManager, World, EDreamTweenTickType::PrePhysics);
	TickTweens(TweenManager, World, EDreamTweenTickType::DuringPhysics);

	if (UDreamUIInputSubsystem* InputSubsystem = World != nullptr ? UDreamUIInputSubsystem::Get(World) : nullptr)
	{
		/*
		 * The world's input frame, as the input subsystem's tick function runs it: every player in
		 * player order, each through its module's ProcessInput -- the queued presses, every pointer's
		 * trace and dispatch, the wheel -- and each player's raycast-enabled switch honoured, so a test
		 * can turn input off the way a game can. Going straight to the module's ProcessInput would be a
		 * pipeline one step shorter than the real one.
		 */
		InputSubsystem->ProcessFrame(InDeltaSeconds);
	}

	TickTweens(TweenManager, World, EDreamTweenTickType::PostPhysics);

	if (World != nullptr)
	{
		/*
		 * The other tickable UI subsystems, which in a game the engine ticks and a world nobody ticks
		 * never does. Each of them does something a test can see: the action router advances holds,
		 * the drag-drop subsystem subscribes to the event system and follows drags (without this it
		 * never subscribes, and row drags and drop-target enter/leave are unobservable), the tooltip
		 * subsystem subscribes and times hovers, the virtual cursor integrates a stick.
		 *
		 * AFTER the event system, as in UWorld::Tick: the event system is a component and ticks in its
		 * tick group (TG_DuringPhysics, the default it keeps), and every tickable world subsystem ticks
		 * later in the same frame from FTickableGameObject::TickObjects. Each is gated exactly as
		 * TickObjects gates it; see TickAsTheEngineWould.
		 *
		 * The list is GetPumpedTickableWorldSubsystems, and it is walked rather than written out, so
		 * the coverage guard (Driver.Pump.*) reads the same list this ticks: a new tickable world
		 * subsystem in the plugin that is not on it makes that test fail instead of making every
		 * interaction with it silently wrong. The UI manager is on the list too, as its last entry,
		 * and is skipped here -- it is driven through TickDreamUI below.
		 *
		 * The virtual cursor is ticked like the rest, although it can own the pointer, because it only
		 * does so while ACTIVE -- ActivateVirtualCursor, or bAutoVirtualCursorOnGamepad (off by default)
		 * plus a gamepad being reported, which nothing in a rig reports. Inactive, its tick is a no-op;
		 * a test that activates it is asking it to drive the pointer, which is what it would do.
		 */
		for (UClass* SubsystemClass : GetPumpedTickableWorldSubsystems())
		{
			if (SubsystemClass == nullptr || SubsystemClass == UDreamUIManagerWorldSubsystem::StaticClass())
			{
				continue;
			}
			TickAsTheEngineWould(*World, Cast<UTickableWorldSubsystem>(World->GetSubsystemBase(SubsystemClass)), InDeltaSeconds);
		}
	}

	/*
	 * The PostUpdateWork tweens are the one piece NOT at the engine's position. In UWorld::Tick the
	 * helper's TG_PostUpdateWork component runs after TickObjects, which is to say after the UI manager;
	 * here they run just before it, so that the rule below -- the frame ends laid out -- holds for
	 * every tween. What that changes is one frame of layout latency for a tween somebody explicitly
	 * put on PostUpdateWork, which nothing in the plugin does (no DreamGUI code calls SetTickType);
	 * everything the controls start is a DuringPhysics tween, at its engine position above.
	 */
	TickTweens(TweenManager, World, EDreamTweenTickType::PostUpdateWork);

	if (IsValid(Manager))
	{
		/*
		 * LAST: the UI manager is one more tickable world subsystem in the same TickObjects pass, and
		 * the engine fixes no order within that pass -- it is the order the subsystems happened to
		 * initialize in, which nothing in the source pins. Last is the one position under which
		 * everything this frame produced -- a drag visual moved, a tooltip opened, a widget a router
		 * callback created -- is laid out before the frame ends, so the next frame and any assertion
		 * between frames read settled geometry rather than last frame's.
		 *
		 * Layout, widget transforms and clip rectangles -- everything the NEXT frame's hit test reads.
		 *
		 * Called explicitly rather than left to the world, because nothing ticks a world built with
		 * UWorld::CreateWorld, and UDreamUIManagerWorldSubsystem would not run even if something did:
		 * it is a UTickableWorldSubsystem whose IsTickableInEditor is false and whose Tick is gated
		 * behind bShouldTickInEditor. TickDreamUI rather than Tick is also what every layout test in
		 * this module already calls, so the pump and they agree on what a frame of UI is.
		 */
		Manager->TickDreamUI(InDeltaSeconds);
	}

	/*
	 * What this pump deliberately does NOT drive, so nobody goes looking for it here:
	 *
	 *  - UDreamGUIEditorSubsystem, and the editor tick each manager broadcasts from Tick (which this
	 *    pump does not call; it calls TickDreamUI). Both are editor concerns -- a recompile's release
	 *    and rebuild, a designer preview animating -- none of which exists in a game, so a rig that
	 *    drove them would be driving the editor, not a game. The rebuild after a recompile runs on the
	 *    core ticker, below.
	 *  - The core ticker (FTSTicker), which only an engine frame advances -- and with it anything that
	 *    waits on one: FEnhancedInputModule's mapping rebuild (the game host applies it immediately
	 *    instead), and the automation driver's steps.
	 *
	 * Sequencer-based widget animations (UDreamWidgetAnimationComponent, its players and DreamGUI's
	 * animation ticker) ARE driven, by the world's own UMovieSceneSequenceTickManager at the top of the
	 * frame: see TickSequencer. Tweens, which every control transition uses, are driven above.
	 */
}

void FDreamDriverContext::PumpFrames(int32 InFrameCount)
{
	if (bEnginePumped && InFrameCount > 0)
	{
		// Once, rather than once per frame through PumpOneFrame: one mistake, one error.
		DreamDriverPumpLocal::ReportContextError(*this, TEXT("PumpFrames was called on a context whose frames belong to the engine (a PIE rig). Wait with a sequence and PerformLatent instead; the engine's own frame is the pump there."));
		return;
	}
	for (int32 FrameIndex = 0; FrameIndex < InFrameCount; ++FrameIndex)
	{
		PumpOneFrame(FrameSeconds);
	}
}

UDreamPointerEventData* FDreamDriverContext::GetPointerEventData(int32 InPointerID) const
{
	if (!IsValid(EventSystem))
	{
		return nullptr;
	}
	// Not created on demand: asking where the pointer is should not bring a pointer into existence,
	// and a test that reads one before any input has happened wants to see that there is none.
	return EventSystem->GetPointerEventData(InPointerID, false);
}

UDreamWidget* FDreamDriverContext::FindOne(const FDreamLocatorRef& InLocator) const
{
	if (!IsValid(Root))
	{
		return nullptr;
	}
	TArray<UDreamWidget*> Found;
	InLocator->Locate(Root, Found);
	// Exactly one. Several is as much a failure as none: a driver that silently took the first of
	// three would act on whichever the tree happened to sort first, and the test would pass or fail
	// on child order rather than on the thing it is about.
	return Found.Num() == 1 ? Found[0] : nullptr;
}

UUITextInput* FDreamDriverContext::FindEditingTextInput(FString& OutWhyNot) const
{
	if (!IsValid(EventSystem))
	{
		OutWhyNot = TEXT("the driver has no event system");
		return nullptr;
	}
	UDreamWidget* Selected = EventSystem->GetCurrentSelectedComponent(0);
	if (!IsValid(Selected))
	{
		OutWhyNot = TEXT("nothing is selected, so no text field has the keyboard");
		return nullptr;
	}
	UUITextInput* TextInput = Selected->GetComponent<UUITextInput>();
	if (TextInput == nullptr)
	{
		OutWhyNot = FString::Printf(TEXT("the selected widget '%s' is not a text field"), *Selected->GetDisplayName());
		return nullptr;
	}
	if (!TextInput->IsInputActive())
	{
		// Selected is not the same as being typed into: a field stays selected after Enter ended its
		// edit, and a keyboard press then goes nowhere -- which is what a test asserting "the edit is
		// over" needs this to say.
		OutWhyNot = FString::Printf(TEXT("the text field '%s' is selected but not being edited"), *Selected->GetDisplayName());
		return nullptr;
	}
	return TextInput;
}

UDreamInputKeySelector* FDreamDriverContext::FindListeningKeySelector() const
{
	if (!IsValid(Root))
	{
		return nullptr;
	}
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(Root, AllWidgets, false);
	for (UDreamWidget* Candidate : AllWidgets)
	{
		UDreamInputKeySelector* Selector = Cast<UDreamInputKeySelector>(Candidate);
		if (IsValid(Selector) && Selector->GetIsListening())
		{
			return Selector;
		}
	}
	return nullptr;
}

namespace DreamDriverSequenceLocal
{
	/** A pixel worked out when the step runs, rather than when the sequence was written. */
	using FDreamPixelResolver = TFunction<TOptional<FVector2D>(FDreamDriverContext&)>;

	/**
	 * Whether this context's buttons, wheel, navigation, keys and touches go through a player
	 * controller and an input actor rather than straight into the driver's module.
	 *
	 * The pointer's POSITION is the one thing that never does: under both actor hosts it is written
	 * through the module's override seam, because there is no system mouse to move and the one on the
	 * desk is the user's. Everything a real device would send as an event, an actor host sends as one.
	 */
	bool IsActorHost(const FDreamDriverContext& InContext)
	{
		return IsActorInputHost(InContext.InputHost);
	}

	/**
	 * Whether this context's input reaches the world's Slate input source as Slate's own events --
	 * the pointer's position included, as mouse moves (DreamDriverSlateHost).
	 */
	bool IsSlateHost(const FDreamDriverContext& InContext)
	{
		return InContext.InputHost == EDreamRigInputHost::SlateSource;
	}

	/** The host's explanation as a step failure, never an empty one. */
	FString DescribeHostFailure(const TCHAR* InWhat, const FString& InWhyNot)
	{
		return FString::Printf(TEXT("the input host could not deliver %s: %s"), InWhat,
			InWhyNot.IsEmpty() ? TEXT("the host gave no reason") : *InWhyNot);
	}

	/**
	 * Where the pointer the steps move is, in viewport pixels: Slate's mouse under SlateSource, where
	 * the module keeps no cursor of its own, and the module's cursor under every other host.
	 */
	FVector2D PointerPixelOf(const FDreamDriverContext& InContext)
	{
		return IsSlateHost(InContext) ? DreamDriverSlateHost::GetMousePixel(InContext) : InContext.InputModule->GetVirtualCursor();
	}

	/** The pointer put at InPixel by the host's road. False having set OutFailureReason. */
	bool MovePointerTo(FDreamDriverContext& InContext, const FVector2D& InPixel, FString& OutFailureReason)
	{
		if (IsSlateHost(InContext))
		{
			FString WhyNot;
			if (!DreamDriverSlateHost::MovePointer(InContext, InPixel, WhyNot))
			{
				OutFailureReason = DescribeHostFailure(TEXT("a mouse move"), WhyNot);
				return false;
			}
			return true;
		}
		InContext.InputModule->MoveTo(InPixel);
		return true;
	}

	/** The player whose keyboard the context's keys and characters are typed on: its event system's, else the context's own. */
	int32 KeyboardPlayerOf(const FDreamDriverContext& InContext)
	{
		return IsValid(InContext.EventSystem) ? InContext.EventSystem->GetUserIndex() : InContext.PlayerIndex;
	}

	/**
	 * A key's press or release down DreamUIKeyRouting::RouteKey for the context's player, the chord as
	 * the modifier state a key event carries: the one road every input source takes, so the field
	 * being edited, a binding, navigation, Back, paging and tab switching each get the key in the
	 * order a game gives it to them. What took it is dropped -- that is the pipeline's decision, and
	 * what a test is there to watch. False having set OutFailureReason when there is no player.
	 */
	bool RouteKeyForPlayer(FDreamDriverContext& InContext, const FKey& InKey, EDreamDriverModifierKeys InModifiers, bool bInPressed, FString& OutFailureReason)
	{
		UDreamUIInputUser* User = IsValid(InContext.EventSystem) ? InContext.EventSystem->GetInputUser() : nullptr;
		if (User == nullptr)
		{
			OutFailureReason = TEXT("the event system speaks for no player, so the key has nobody to be routed for: it was never registered with the world's input (UDreamUIInputSubsystem::AddEventSystem), which the rig does as it opens its begin-play gate");
			return false;
		}
		bool bTyped = false;
		DreamUIKeyRouting::RouteKey(User, InKey, bInPressed, DreamDriverKeys::MakeModifierKeysState(InModifiers), bTyped);
		return true;
	}

	/**
	 * A character by the viewport's road -- what a game viewport client's InputChar hands DreamGUI,
	 * UDreamUIInputSubsystem::HandleViewportCharacter -- whether or not anything is there to take it:
	 * the answer is dropped. The character Tab makes goes this way right after the key, as a WM_CHAR
	 * follows a WM_KEYDOWN.
	 */
	void TypeViewportCharacter(const FDreamDriverContext& InContext, TCHAR InCharacter)
	{
		UDreamUIInputSubsystem* Input = InContext.World != nullptr ? UDreamUIInputSubsystem::Get(InContext.World) : nullptr;
		if (Input != nullptr)
		{
			Input->HandleViewportCharacter(KeyboardPlayerOf(InContext), InCharacter);
		}
	}

	void ReportStepFailure(const FDreamDriverContext& InContext, const FDreamDriverStepRef& InStep)
	{
		const FString Reason = InStep->GetFailureReason();
		const FString Message = FString::Printf(TEXT("Driver step %s failed: %s"),
			*InStep->Describe(),
			Reason.IsEmpty() ? TEXT("no reason given") : *Reason);

		// The context's test if it has one, otherwise whatever the framework says is running. The
		// second is what makes a failure inside a latent command land on the right test: by then the
		// test body has long returned, and the framework is the only thing that still knows.
		FAutomationTestBase* ReportingTest = InContext.CurrentTest != nullptr
			? InContext.CurrentTest
			: FAutomationTestFramework::Get().GetCurrentTest();
		if (ReportingTest != nullptr)
		{
			ReportingTest->AddError(Message);
		}
		else
		{
			UE_LOG(LogDreamDriver, Error, TEXT("%s"), *Message);
		}
	}

	/**
	 * A step that does something once and then spends one frame letting the pipeline see it.
	 *
	 * Every input entry point in UDreamStandaloneInputModule queues rather than dispatches, so the
	 * effect of a press is not observable until a frame has been pumped. Returning Again once is how
	 * a step says "the frame after mine belongs to me".
	 */
	class FDreamInputStep : public IDreamDriverStep
	{
	public:
		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (bHasApplied)
			{
				FinishAfterFrame(InContext);
				return EDreamDriverStepResult::Done;
			}
			if (!InContext.IsUsable())
			{
				FailureReason = TEXT("the driver context is missing a world, event system, input module, UI manager or root widget");
				return EDreamDriverStepResult::Failed;
			}
			if (!Apply(InContext))
			{
				return EDreamDriverStepResult::Failed;
			}
			bHasApplied = true;
			return EDreamDriverStepResult::Again;
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	protected:
		/** Returns false having set FailureReason. InDeltaSeconds is deliberately not offered: input is instantaneous. */
		virtual bool Apply(FDreamDriverContext& InContext) = 0;

		/**
		 * Once, when the frame after Apply's has passed and before the step reports done: where a
		 * whole keystroke sent as Slate's events lets its key go, so the pipeline has acted on the
		 * press before the release comes -- as it has through the controller, which releases after
		 * its next input frame.
		 */
		virtual void FinishAfterFrame(FDreamDriverContext& InContext) {}

		FString FailureReason;

	private:
		bool bHasApplied = false;
	};

	class FDreamMoveStep : public FDreamInputStep
	{
	public:
		FDreamMoveStep(FDreamPixelResolver InResolver, const FString& InDescription)
			: Resolver(MoveTemp(InResolver))
			, Description(InDescription)
		{
		}

		virtual FString Describe() const override { return FString::Printf(TEXT("MoveTo(%s)"), *Description); }

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			const TOptional<FVector2D> Pixel = Resolver(InContext);
			if (!Pixel.IsSet())
			{
				FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
				return false;
			}
			return MovePointerTo(InContext, Pixel.GetValue(), FailureReason);
		}

	private:
		FDreamPixelResolver Resolver;
		FString Description;
	};

	class FDreamTriggerStep : public FDreamInputStep
	{
	public:
		FDreamTriggerStep(EDreamUIMouseButtonType InButton, bool bInPress)
			: Button(InButton)
			, bPress(bInPress)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("%s(button %d)"), bPress ? TEXT("Press") : TEXT("Release"), (int32)Button);
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				// As the mouse button key, through the controller's input stack, where the input actor's
				// own binding turns it into the module's trigger -- at the position the module already
				// holds, which the move steps wrote through the override seam.
				FString WhyNot;
				if (!DreamDriverGameHost::PressMouseButton(InContext, Button, bPress, WhyNot))
				{
					FailureReason = DescribeHostFailure(bPress ? TEXT("a button press") : TEXT("a button release"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				// As Slate's mouse button event, at the mouse.
				FString WhyNot;
				if (!DreamDriverSlateHost::PressMouseButton(InContext, Button, bPress, WhyNot))
				{
					FailureReason = DescribeHostFailure(bPress ? TEXT("a button press") : TEXT("a button release"), WhyNot);
					return false;
				}
				return true;
			}
			if (bPress)
			{
				InContext.InputModule->Press(Button);
			}
			else
			{
				InContext.InputModule->Release(Button);
			}
			return true;
		}

	private:
		EDreamUIMouseButtonType Button;
		bool bPress;
	};

	class FDreamScrollStep : public FDreamInputStep
	{
	public:
		explicit FDreamScrollStep(const FVector2D& InAxisValue)
			: AxisValue(InAxisValue)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("ScrollBy(%s)"), *AxisValue.ToString());
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::Scroll(InContext, AxisValue, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a wheel turn"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverSlateHost::Scroll(InContext, AxisValue, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a wheel turn"), WhyNot);
					return false;
				}
				return true;
			}
			InContext.InputModule->Scroll(AxisValue);
			return true;
		}

	private:
		FVector2D AxisValue;
	};

	class FDreamNavigateStep : public FDreamInputStep
	{
	public:
		FDreamNavigateStep(EDreamUINavigationDirection InDirection, bool bInPressOrRelease)
			: Direction(InDirection)
			, bPressOrRelease(bInPressOrRelease)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Navigate(direction %d, %s)"), (int32)Direction,
				bPressOrRelease ? TEXT("press") : TEXT("release"));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::Navigate(InContext, Direction, bPressOrRelease, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a navigation direction"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				// The same keys the actor hosts press, as Slate's key events.
				FString WhyNot;
				if (!DreamDriverSlateHost::Navigate(InContext, Direction, bPressOrRelease, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a navigation direction"), WhyNot);
					return false;
				}
				return true;
			}
			InContext.InputModule->Navigate(Direction, bPressOrRelease, 0);
			return true;
		}

	private:
		EDreamUINavigationDirection Direction;
		bool bPressOrRelease;
	};

	class FDreamNavigationTriggerStep : public FDreamInputStep
	{
	public:
		explicit FDreamNavigationTriggerStep(bool bInTriggerPress)
			: bTriggerPress(bInTriggerPress)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("NavigationTrigger(%s)"), bTriggerPress ? TEXT("press") : TEXT("release"));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::NavigationTrigger(InContext, bTriggerPress, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("the accept button"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				// The pad's accept button, the key the actor hosts press for it, as Slate's key event.
				FString WhyNot;
				if (!DreamDriverSlateHost::SendKey(InContext, EKeys::Gamepad_FaceButton_Bottom, EDreamDriverModifierKeys::None, bTriggerPress, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("the accept button"), WhyNot);
					return false;
				}
				return true;
			}
			InContext.InputModule->NavigationTrigger(bTriggerPress, 0);
			return true;
		}

	private:
		bool bTriggerPress;
	};

	/** The modifier state a real key event would carry with InModifier held. A null key holds nothing. */
	FModifierKeysState MakeModifierState(const FKey& InModifier)
	{
		return FModifierKeysState(
			InModifier == EKeys::LeftShift, InModifier == EKeys::RightShift,
			InModifier == EKeys::LeftControl, InModifier == EKeys::RightControl,
			InModifier == EKeys::LeftAlt, InModifier == EKeys::RightAlt,
			InModifier == EKeys::LeftCommand, InModifier == EKeys::RightCommand,
			false);
	}

	/** One character into the field that owns the keyboard. */
	class FDreamTypeCharacterStep : public FDreamInputStep
	{
	public:
		explicit FDreamTypeCharacterStep(TCHAR InCharacter)
			: Character(InCharacter)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Type(character %d)"), static_cast<int32>(Character));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				// The game's own road for a character: what UDreamGameViewportClient::InputChar calls,
				// which hands it to whichever field owns the keyboard process-wide.
				FString HostWhyNot;
				if (!DreamDriverGameHost::TypeCharacter(InContext, Character, HostWhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a character"), HostWhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				// Slate hands a typed character to the focused viewport, not to its input pre-processors: the
				// viewport's road, UDreamUIInputSubsystem::HandleViewportCharacter.
				FString HostWhyNot;
				if (!DreamDriverSlateHost::TypeCharacter(InContext, Character, HostWhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a character"), HostWhyNot);
					return false;
				}
				return true;
			}
			FString WhyNot;
			UUITextInput* TextInput = InContext.FindEditingTextInput(WhyNot);
			if (TextInput == nullptr)
			{
				FailureReason = FString::Printf(TEXT("there is no text field to type into: %s"), *WhyNot);
				return false;
			}
			// The answer is deliberately dropped. A refused character -- a read-only field, a full
			// one, a letter in a number field -- is the field deciding, not the driver failing.
			TextInput->HandleCharacterInput(Character);
			return true;
		}

	private:
		TCHAR Character;
	};

	/**
	 * One key, optionally with a modifier held, routed the way a game routes it when nothing but the
	 * driver's module stands between the key and the UI. See FDreamDriverSequence::Type(const FKey&).
	 * Returns false having set OutFailureReason.
	 */
	bool RouteKeyInModule(FDreamDriverContext& InContext, const FKey& InKey, const FKey& InModifier, FString& OutFailureReason)
	{
		const FModifierKeysState HeldModifiers = MakeModifierState(InModifier);

		// An armed selector first: its capture agent is at the top of the input stack, at the
		// highest priority, so in a game it hears the key before anything under it -- Escape
		// included, which is its own way out.
		if (UDreamInputKeySelector* Selector = InContext.FindListeningKeySelector())
		{
			Selector->NotifyChordPressed(FInputChord(InKey,
				HeldModifiers.IsShiftDown(), HeldModifiers.IsControlDown(),
				HeldModifiers.IsAltDown(), HeldModifiers.IsCommandDown()));
			return true;
		}

		// Escape is Back. A text field does not bind it on purpose -- it would swallow every
		// project's own Escape action -- and the standalone input actor sends a Back key nobody
		// bound to exactly this call, which is where an edit in progress gets cancelled.
		if (InKey == EKeys::Escape)
		{
			UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(InContext.World);
			if (Stack == nullptr)
			{
				OutFailureReason = TEXT("Escape is Back, and this world has no navigation stack to send it down");
				return false;
			}
			Stack->HandleBack(InContext.EventSystem->GetUserIndex());
			return true;
		}

		FString WhyNot;
		UUITextInput* TextInput = InContext.FindEditingTextInput(WhyNot);
		if (TextInput == nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("no key selector is listening and %s"), *WhyNot);
			return false;
		}
		// As with characters, what the field makes of the key is the field's business: an ignored
		// key, or Home with the caret already at the start, is still a key delivered.
		TextInput->HandleKeyInput(InKey, true, HeldModifiers);
		return true;
	}

	/** One key, optionally with a modifier held, routed where a game would route it. See FDreamDriverSequence::Type. */
	class FDreamTypeKeyStep : public FDreamInputStep
	{
	public:
		FDreamTypeKeyStep(const FKey& InKey, const FKey& InModifier)
			: Key(InKey)
			, Modifier(InModifier)
		{
		}

		virtual FString Describe() const override
		{
			return Modifier.IsValid()
				? FString::Printf(TEXT("TypeChord(%s+%s)"), *Modifier.ToString(), *Key.ToString())
				: FString::Printf(TEXT("Type(%s)"), *Key.ToString());
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (Modifier.IsValid() && !Modifier.IsModifierKey())
			{
				FailureReason = FString::Printf(TEXT("%s is not a modifier key, so it cannot be held with %s"),
					*Modifier.ToString(), *Key.ToString());
				return false;
			}
			if (IsActorHost(InContext))
			{
				// Through the controller's input stack, where the input actor, a text field's key agent
				// and an armed selector's capture agent are all bound: which of them hears the key is
				// the game's decision there, not the driver's.
				FString WhyNot;
				if (!DreamDriverGameHost::TypeKey(InContext, Key, Modifier, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a key"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				// As Slate's key events, the source deciding who hears them; the press now and the release once
				// the frame after it has passed (FinishAfterFrame), as the controller's road releases.
				FString WhyNot;
				const TArray<FKey> Held = HeldModifierKeys();
				if (!DreamDriverSlateHost::SendKey(InContext, Key, Held, true, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a key"), WhyNot);
					return false;
				}
				bReleaseOwed = true;
				return true;
			}
			return RouteKeyInModule(InContext, Key, Modifier, FailureReason);
		}

		virtual void FinishAfterFrame(FDreamDriverContext& InContext) override
		{
			if (!bReleaseOwed)
			{
				return;
			}
			bReleaseOwed = false;
			// Nothing to report: the press went through the same source a frame ago.
			FString WhyNot;
			const TArray<FKey> Held = HeldModifierKeys();
			DreamDriverSlateHost::SendKey(InContext, Key, Held, false, WhyNot);
		}

	private:
		TArray<FKey> HeldModifierKeys() const
		{
			TArray<FKey> Held;
			if (Modifier.IsValid())
			{
				Held.Add(Modifier);
			}
			return Held;
		}

		FKey Key;
		/** Unset for a bare key. */
		FKey Modifier;
		/** Under SlateSource, between the press and the release FinishAfterFrame sends. */
		bool bReleaseOwed = false;
	};

	/**
	 * Half a keystroke -- the press, or the release, of a key with modifiers held -- the way the
	 * context's input host delivers keys: ModuleOnly down DreamUIKeyRouting::RouteKey for the
	 * context's player, the actor hosts through the controller's input stack with the modifier keys
	 * pressed around it, SlateSource as Slate's key events to the world's source. See
	 * FDreamDriverSequence::Key. The press of Tab() and ShiftTab() types the character '\t' right
	 * after the key, by the viewport's road for characters.
	 */
	class FDreamKeyStep : public FDreamInputStep
	{
	public:
		FDreamKeyStep(const FKey& InKey, EDreamDriverModifierKeys InModifiers, bool bInPressed, bool bInTypesTabCharacter)
			: Key(InKey)
			, Modifiers(InModifiers)
			, bPressed(bInPressed)
			, bTypesTabCharacter(bInTypesTabCharacter)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("%s(%s)"), bPressed ? TEXT("KeyDown") : TEXT("KeyUp"), *DreamDriverKeys::Describe(Key, Modifiers));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (!Key.IsValid())
			{
				FailureReason = TEXT("the key is not a valid key");
				return false;
			}
			const TCHAR* const What = bPressed ? TEXT("a key press") : TEXT("a key release");
			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::PressKey(InContext, Key, Modifiers, bPressed, WhyNot))
				{
					FailureReason = DescribeHostFailure(What, WhyNot);
					return false;
				}
			}
			else if (IsSlateHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverSlateHost::SendKey(InContext, Key, Modifiers, bPressed, WhyNot))
				{
					FailureReason = DescribeHostFailure(What, WhyNot);
					return false;
				}
			}
			else if (!RouteKeyForPlayer(InContext, Key, Modifiers, bPressed, FailureReason))
			{
				return false;
			}
			if (bPressed && bTypesTabCharacter)
			{
				// After the key, as the platform's character message follows its key-down message. Through the
				// controller the key itself waits for the controller's next input frame and the character does
				// not -- which is a game's order too: the viewport hands a character on as it arrives.
				TypeViewportCharacter(InContext, TEXT('\t'));
			}
			return true;
		}

	private:
		FKey Key;
		EDreamDriverModifierKeys Modifiers;
		bool bPressed;
		bool bTypesTabCharacter;
	};

	/**
	 * The first move of a drag: far enough from the press to be a drag rather than a twitch.
	 *
	 * The distance is read from the raycaster rather than assumed, because the threshold is authored
	 * in canvas units and compared in viewport pixels -- UDreamScreenSpaceRaycaster::ShouldStartDrag
	 * measures against GetScaledDragThresholdSquare, which is the authored value times the canvas
	 * scale. Hard-coding a number here would work at scale 1 and silently stop starting drags on the
	 * first fixture that scales its canvas.
	 */
	class FDreamDragCrossThresholdStep : public FDreamInputStep
	{
	public:
		FDreamDragCrossThresholdStep(FDreamPixelResolver InTargetResolver, const FString& InDescription)
			: TargetResolver(MoveTemp(InTargetResolver))
			, Description(InDescription)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("DragCrossThreshold(towards %s)"), *Description);
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			const TOptional<FVector2D> Target = TargetResolver(InContext);
			if (!Target.IsSet())
			{
				FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
				return false;
			}
			const UDreamPointerEventData* EventData = InContext.GetPointerEventData(0);
			if (EventData == nullptr)
			{
				FailureReason = TEXT("no pointer has been pressed, so there is nothing to drag from");
				return false;
			}
			const FVector2D PressPixel(EventData->PressPointerPosition.X, EventData->PressPointerPosition.Y);

			// The threshold of the raycaster that took the press, because that is the one whose
			// ShouldStartDrag decides: a screen raycaster scales its authored threshold by the canvas,
			// a world raycaster following the mouse compares its own unscaled one in pixels. The rig's
			// screen raycaster is the answer only when the press came through nothing else.
			double ThresholdPixels = 0.0;
			const UDreamBaseRaycaster* PressRaycaster = EventData->PressRaycaster;
			if (const UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(PressRaycaster))
			{
				ThresholdPixels = FMath::Sqrt((double)ScreenRaycaster->GetScaledDragThresholdSquare());
			}
			else if (const UDreamWorldSpaceRaycaster* WorldRaycaster = Cast<UDreamWorldSpaceRaycaster>(PressRaycaster))
			{
				ThresholdPixels = FMath::Sqrt((double)WorldRaycaster->GetDragThresholdSquare());
			}
			else if (IsValid(InContext.Raycaster))
			{
				ThresholdPixels = FMath::Sqrt((double)InContext.Raycaster->GetScaledDragThresholdSquare());
			}

			FVector2D Direction = Target.GetValue() - PressPixel;
			if (Direction.IsNearlyZero())
			{
				// Dragging onto the thing being dragged has no direction of its own, and a drag that
				// never moved is not one. Rightward is as good as any and is at least reproducible.
				Direction = FVector2D(1.0, 0.0);
			}
			// Past the threshold rather than onto it: the comparison is strictly greater than, so
			// landing exactly on it is still a press.
			const FVector2D Next = PressPixel + Direction.GetSafeNormal() * (ThresholdPixels + 2.0);
			return MovePointerTo(InContext, Next, FailureReason);
		}

	private:
		FDreamPixelResolver TargetResolver;
		FString Description;
	};

	class FDreamWaitFramesStep : public IDreamDriverStep
	{
	public:
		explicit FDreamWaitFramesStep(int32 InFrameCount)
			: RemainingFrames(FMath::Max(InFrameCount, 0))
			, RequestedFrames(FMath::Max(InFrameCount, 0))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (RemainingFrames <= 0)
			{
				return EDreamDriverStepResult::Done;
			}
			--RemainingFrames;
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override { return FString::Printf(TEXT("WaitFrames(%d)"), RequestedFrames); }

	private:
		int32 RemainingFrames;
		int32 RequestedFrames;
	};

	class FDreamWaitDelegateStep : public IDreamDriverStep
	{
	public:
		FDreamWaitDelegateStep(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription)
			: WaitDelegate(InWaitDelegate)
			, Timeout(InTimeout.Timespan)
			, Description(InDescription)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (!WaitDelegate.IsBound())
			{
				FailureReason = TEXT("the wait delegate is not bound to anything");
				return EDreamDriverStepResult::Failed;
			}
			const FDriverWaitResponse Response = WaitDelegate.Execute(Elapsed);
			if (Response.State == FDriverWaitResponse::EState::PASSED)
			{
				return EDreamDriverStepResult::Done;
			}
			if (Response.State == FDriverWaitResponse::EState::FAILED)
			{
				FailureReason = FString::Printf(TEXT("%s did not happen; the wait gave up after %.3f seconds"),
					Description.IsEmpty() ? TEXT("the awaited condition") : *Description, Elapsed.GetTotalSeconds());
				return EDreamDriverStepResult::Failed;
			}
			// Response.NextWait is deliberately ignored: it is the engine driver's way of asking not
			// to be polled so hard, and re-evaluating every frame is never less correct than polling
			// less often. A headless pump also has no clock to sleep against.
			Elapsed += FTimespan::FromSeconds(InDeltaSeconds);
			if (Elapsed >= Timeout)
			{
				FailureReason = FString::Printf(TEXT("%s did not happen within %.3f seconds"),
					Description.IsEmpty() ? TEXT("the awaited condition") : *Description, Timeout.GetTotalSeconds());
				return EDreamDriverStepResult::Failed;
			}
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Wait(%s, timeout %.3f seconds)"),
				Description.IsEmpty() ? TEXT("a condition") : *Description, Timeout.GetTotalSeconds());
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		FDriverWaitDelegate WaitDelegate;
		FTimespan Timeout;
		FString Description;
		FTimespan Elapsed = FTimespan::Zero();
		FString FailureReason;
	};

	class FDreamThenStep : public IDreamDriverStep
	{
	public:
		explicit FDreamThenStep(TFunction<void(FDreamDriverContext&)> InAction)
			: Action(MoveTemp(InAction))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (!Action)
			{
				FailureReason = TEXT("the action is empty");
				return EDreamDriverStepResult::Failed;
			}
			Action(InContext);
			// Costs no frame. An assertion is a reading of the state the previous step produced, and
			// making it wait a frame would mean reading one frame later than the step it is about.
			return EDreamDriverStepResult::Done;
		}

		virtual FString Describe() const override { return TEXT("Then(...)"); }
		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		TFunction<void(FDreamDriverContext&)> Action;
		FString FailureReason;
	};

	class FDreamFailStep : public IDreamDriverStep
	{
	public:
		explicit FDreamFailStep(const FString& InReason)
			: Reason(InReason)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			return EDreamDriverStepResult::Failed;
		}

		virtual FString Describe() const override { return TEXT("Fail(...)"); }
		virtual FString GetFailureReason() const override { return Reason; }

	private:
		FString Reason;
	};

	/**
	 * Another player's step: the same step, given that player's context instead of the one the sequence runs over.
	 *
	 * Only the context a step is handed changes. Its frames are still the executor's -- the world's -- and the step
	 * itself reads everything it reads from the context it is given, which is what makes the press player 1's press:
	 * player 1's module or controller, player 1's pointer, player 1's screen for a locator to search and a projection to
	 * aim through. A player the rig has not got is a failure that says so; it is never sent as anybody else.
	 */
	class FDreamAsPlayerStep : public IDreamDriverStep
	{
	public:
		FDreamAsPlayerStep(int32 InPlayerIndex, const FDreamDriverStepRef& InStep)
			: PlayerIndex(InPlayerIndex)
			, Step(InStep)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			FDreamDriverContext* Player = InContext.FindPlayer(PlayerIndex);
			if (Player == nullptr)
			{
				const int32 PlayerCount = FMath::Max(InContext.Players.Num(), 1);
				FailureReason = FString::Printf(TEXT("the rig has %d player(s) and so no player %d to send it as; build it with FDreamRigOptions::PlayerCount of at least %d"),
					PlayerCount, PlayerIndex, PlayerIndex + 1);
				return EDreamDriverStepResult::Failed;
			}
			FailureReason.Reset();
			return Step->Execute(*Player, InDeltaSeconds);
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("as player %d: %s"), PlayerIndex, *Step->Describe());
		}

		virtual FString GetFailureReason() const override
		{
			return FailureReason.IsEmpty() ? Step->GetFailureReason() : FailureReason;
		}

	private:
		int32 PlayerIndex;
		FDreamDriverStepRef Step;
		FString FailureReason;
	};

	/**
	 * A finger landing, moving or lifting -- one step, one frame, like every other input.
	 *
	 * A finger is its own pointer: the standalone module keys touches by the finger's index, so
	 * finger 1 and finger 2 are pointers DreamUIPointerIds::ForTouch(1) and ForTouch(2) -- 101 and
	 * 102 -- each with its own press, hover and drag. No finger shares a pointer with the mouse, which
	 * is pointer 0: finger 0 is pointer 100.
	 *
	 * A lift is aimed where the finger IS -- the pointer's last position, read back from the event
	 * system -- because a finger comes off the glass where it was, and a lift at some other pixel is a
	 * gesture no screen can report. Moving or lifting a finger that is not down fails the step.
	 */
	class FDreamTouchStep : public FDreamInputStep
	{
	public:
		FDreamTouchStep(EDreamDriverTouchPhase InPhase, int32 InFingerId, FDreamPixelResolver InResolver, const FString& InDescription)
			: Phase(InPhase)
			, FingerId(InFingerId)
			, Resolver(MoveTemp(InResolver))
			, Description(InDescription)
		{
		}

		virtual FString Describe() const override
		{
			const TCHAR* PhaseName = Phase == EDreamDriverTouchPhase::Began ? TEXT("TouchDown")
				: Phase == EDreamDriverTouchPhase::Moved ? TEXT("TouchMoveTo")
				: TEXT("TouchUp");
			return Description.IsEmpty()
				? FString::Printf(TEXT("%s(finger %d)"), PhaseName, FingerId)
				: FString::Printf(TEXT("%s(finger %d, %s)"), PhaseName, FingerId, *Description);
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			FVector2D Pixel = FVector2D::ZeroVector;
			if (Phase == EDreamDriverTouchPhase::Began)
			{
				const TOptional<FVector2D> Resolved = Resolver ? Resolver(InContext) : TOptional<FVector2D>();
				if (!Resolved.IsSet())
				{
					FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
					return false;
				}
				Pixel = Resolved.GetValue();
			}
			else
			{
				// Down means a pointer of that index exists and its trigger is held. The press a
				// TouchDown queued has been processed by now -- its step spent the frame after it.
				const UDreamPointerEventData* EventData = InContext.GetPointerEventData(UDreamStandaloneInputModule::GetTouchPointerID(FingerId));
				if (EventData == nullptr || !EventData->bNowIsTriggerPressed)
				{
					FailureReason = FString::Printf(TEXT("finger %d is not down, so it cannot %s"), FingerId,
						Phase == EDreamDriverTouchPhase::Moved ? TEXT("move") : TEXT("lift"));
					return false;
				}
				if (Phase == EDreamDriverTouchPhase::Moved)
				{
					const TOptional<FVector2D> Resolved = Resolver ? Resolver(InContext) : TOptional<FVector2D>();
					if (!Resolved.IsSet())
					{
						FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
						return false;
					}
					Pixel = Resolved.GetValue();
				}
				else
				{
					Pixel = FVector2D(EventData->PointerPosition.X, EventData->PointerPosition.Y);
				}
			}

			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::Touch(InContext, Phase, FingerId, Pixel, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a touch"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverSlateHost::Touch(InContext, Phase, FingerId, Pixel, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("a touch"), WhyNot);
					return false;
				}
				return true;
			}
			switch (Phase)
			{
			case EDreamDriverTouchPhase::Began:
				InContext.InputModule->TouchPress(FingerId, Pixel);
				break;
			case EDreamDriverTouchPhase::Moved:
				InContext.InputModule->TouchMoveTo(FingerId, Pixel);
				break;
			case EDreamDriverTouchPhase::Ended:
				InContext.InputModule->TouchRelease(FingerId, Pixel);
				break;
			}
			return true;
		}

	private:
		EDreamDriverTouchPhase Phase;
		int32 FingerId;
		/** Unused for a lift, which goes where the finger is. */
		FDreamPixelResolver Resolver;
		FString Description;
	};

	/**
	 * Let a span of time pass: as many frames as it takes, the last one included.
	 *
	 * Measured in the frames' own lengths rather than converted once, so under the headless pump --
	 * constant frames -- it is exactly ceil(seconds / frame) frames, and under the engine pump it is
	 * however many real frames the span actually took. The small tolerance is for the float frame
	 * length: 1/60 is not exact, and thirty of them must still be half a second.
	 */
	class FDreamWaitSecondsStep : public IDreamDriverStep
	{
	public:
		explicit FDreamWaitSecondsStep(float InSeconds)
			: Seconds(FMath::Max(InSeconds, 0.0f))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			constexpr double Tolerance = 1.0e-6;
			if (Elapsed + Tolerance >= (double)Seconds)
			{
				return EDreamDriverStepResult::Done;
			}
			// A zero-length frame would never finish the wait; count it as the pump's own frame.
			Elapsed += (double)(InDeltaSeconds > 0.0f ? InDeltaSeconds : InContext.FrameSeconds);
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override { return FString::Printf(TEXT("WaitSeconds(%.3f)"), Seconds); }

	private:
		float Seconds;
		double Elapsed = 0.0;
	};

	/**
	 * Back, as a gamepad or keyboard sends it.
	 *
	 * Under an actor host it is the gamepad's Back button (Gamepad_FaceButton_Right) through the
	 * player controller, which reaches ADreamStandaloneInputEventSystemActor::OnAnyKeyPressed: an
	 * action bound to it first, then a drag in flight, then UDreamUINavigationStack::HandleBack.
	 * Under SlateSource it is the same button as Slate's key events, pressed now and let go of once
	 * the frame after has passed. Under ModuleOnly it is exactly Type(EKeys::Escape) -- the same
	 * entry, an armed key selector first, then HandleBack -- because there is no actor to offer it
	 * to first.
	 */
	class FDreamBackStep : public FDreamInputStep
	{
	public:
		virtual FString Describe() const override { return TEXT("Back()"); }

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (IsActorHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverGameHost::TypeKey(InContext, EKeys::Gamepad_FaceButton_Right, FKey(), WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("Back"), WhyNot);
					return false;
				}
				return true;
			}
			if (IsSlateHost(InContext))
			{
				FString WhyNot;
				if (!DreamDriverSlateHost::SendKey(InContext, EKeys::Gamepad_FaceButton_Right, EDreamDriverModifierKeys::None, true, WhyNot))
				{
					FailureReason = DescribeHostFailure(TEXT("Back"), WhyNot);
					return false;
				}
				bReleaseOwed = true;
				return true;
			}
			return RouteKeyInModule(InContext, EKeys::Escape, FKey(), FailureReason);
		}

		virtual void FinishAfterFrame(FDreamDriverContext& InContext) override
		{
			if (bReleaseOwed)
			{
				bReleaseOwed = false;
				FString WhyNot;
				DreamDriverSlateHost::SendKey(InContext, EKeys::Gamepad_FaceButton_Right, EDreamDriverModifierKeys::None, false, WhyNot);
			}
		}

	private:
		/** Under SlateSource, between the press and the release FinishAfterFrame sends. */
		bool bReleaseOwed = false;
	};

	/** The world's virtual cursor, or null with the reason why there is none to drive. */
	UDreamUIVirtualCursorSubsystem* FindVirtualCursor(const FDreamDriverContext& InContext, FString& OutWhyNot)
	{
		UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(InContext.World);
		if (Cursor == nullptr)
		{
			OutWhyNot = TEXT("this world has no virtual cursor subsystem (it is only made for Game and PIE worlds)");
		}
		return Cursor;
	}

	/**
	 * The context's player's controller as the virtual cursor finds it (UDreamUIInputUser::GetPlayerController): the
	 * context's, else -- for player 0 only, as UDreamEventSystem::GetPlayerControllerForUser answers -- the world's first.
	 * Another player without one of its own has no stick to read.
	 */
	APlayerController* FindStickController(const FDreamDriverContext& InContext)
	{
		if (IsValid(InContext.PlayerController))
		{
			return InContext.PlayerController;
		}
		if (KeyboardPlayerOf(InContext) != 0)
		{
			return nullptr;
		}
		return InContext.World != nullptr ? InContext.World->GetFirstPlayerController() : nullptr;
	}

	/**
	 * The left stick at InStick, as a pad reports it: one analog sample per axis to the controller,
	 * which is where UDreamUIVirtualCursorSubsystem reads the stick from (GetInputAnalogStickState).
	 *
	 * The controller only turns samples into the value that read returns during its input frame
	 * (UPlayerInput::ProcessInputStack -> EvaluateKeyMapState). Under an actor host the pump runs that
	 * frame; under ModuleOnly nothing does, so the key map is evaluated here the way that frame would,
	 * with an empty input stack so that no binding anywhere hears the stick -- only its value changes.
	 */
	void FeedLeftStick(const FDreamDriverContext& InContext, APlayerController& InController, const FVector2D& InStick, float InDeltaSeconds)
	{
		InController.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftX, IE_Axis, (float)InStick.X, 1));
		InController.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftY, IE_Axis, (float)InStick.Y, 1));
		if (!IsActorHost(InContext) && InController.PlayerInput != nullptr)
		{
			const bool bPaused = InContext.World != nullptr && InContext.World->IsPaused();
			InController.PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), InDeltaSeconds, bPaused);
		}
	}

	/** Turn the virtual cursor on, from wherever the pointer is. */
	class FDreamVirtualCursorActivateStep : public FDreamInputStep
	{
	public:
		virtual FString Describe() const override { return TEXT("ActivateVirtualCursor()"); }

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			FString WhyNot;
			UDreamUIVirtualCursorSubsystem* Cursor = FindVirtualCursor(InContext, WhyNot);
			if (Cursor == nullptr)
			{
				FailureReason = WhyNot;
				return false;
			}
			// The context's player's cursor: the first player's is ActivateVirtualCursorForUser(0), which is all
			// ActivateVirtualCursor does.
			const int32 CursorPlayer = KeyboardPlayerOf(InContext);
			Cursor->ActivateVirtualCursorForUser(CursorPlayer);
			if (!Cursor->IsVirtualCursorActiveForUser(CursorPlayer))
			{
				// ActivateVirtualCursorForUser refuses quietly (a Warning) when it cannot find a standalone
				// input module through the player's registered event system.
				FailureReason = FString::Printf(TEXT("the virtual cursor would not activate: it needs player %d's event system registered with the UI manager and a standalone input module on it"), CursorPlayer);
				return false;
			}
			return true;
		}
	};

	/**
	 * Hold the left stick at a value for a span of time, then let it spring back to the centre and
	 * give the cursor one frame to see it there. Every frame of the span carries one sample, as a pad
	 * does; the cursor integrates them in its tick (speed times stick times delta).
	 */
	class FDreamVirtualCursorStickStep : public IDreamDriverStep
	{
	public:
		FDreamVirtualCursorStickStep(const FVector2D& InStick, float InSeconds)
			: Stick(InStick)
			, Seconds(FMath::Max(InSeconds, 0.0f))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (bReleased)
			{
				return EDreamDriverStepResult::Done;
			}
			FString WhyNot;
			UDreamUIVirtualCursorSubsystem* Cursor = FindVirtualCursor(InContext, WhyNot);
			const int32 CursorPlayer = KeyboardPlayerOf(InContext);
			if (Cursor == nullptr || !Cursor->IsVirtualCursorActiveForUser(CursorPlayer))
			{
				FailureReason = Cursor == nullptr
					? WhyNot
					: FString::Printf(TEXT("player %d's virtual cursor is not active; activate it first, as a screen that needs it would"), CursorPlayer);
				return EDreamDriverStepResult::Failed;
			}
			APlayerController* Controller = FindStickController(InContext);
			if (Controller == nullptr || Controller->PlayerInput == nullptr)
			{
				FailureReason = FString::Printf(TEXT("there is no player controller with its input up to carry the stick; the virtual cursor reads player %d's stick from that player's controller"), CursorPlayer);
				return EDreamDriverStepResult::Failed;
			}
			const float FrameDelta = InDeltaSeconds > 0.0f ? InDeltaSeconds : InContext.FrameSeconds;
			constexpr double Tolerance = 1.0e-6;
			if (Elapsed + Tolerance >= (double)Seconds)
			{
				FeedLeftStick(InContext, *Controller, FVector2D::ZeroVector, FrameDelta);
				bReleased = true;
				return EDreamDriverStepResult::Again;
			}
			FeedLeftStick(InContext, *Controller, Stick, FrameDelta);
			Elapsed += (double)FrameDelta;
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("VirtualCursorStick(%s, %.3f seconds)"), *Stick.ToString(), Seconds);
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		FVector2D Stick;
		float Seconds;
		double Elapsed = 0.0;
		bool bReleased = false;
		FString FailureReason;
	};

	/**
	 * The confirm button, as the virtual cursor hears it: SetConfirmPressed, which it delivers to the
	 * module as the left mouse button at the cursor. That is the call the preset input actor makes
	 * after offering the key to the action router, under every host, so it is made directly here.
	 */
	class FDreamVirtualCursorConfirmStep : public FDreamInputStep
	{
	public:
		explicit FDreamVirtualCursorConfirmStep(bool bInPressed)
			: bPressed(bInPressed)
		{
		}

		virtual FString Describe() const override
		{
			return bPressed ? TEXT("VirtualCursorPress()") : TEXT("VirtualCursorRelease()");
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			FString WhyNot;
			UDreamUIVirtualCursorSubsystem* Cursor = FindVirtualCursor(InContext, WhyNot);
			if (Cursor == nullptr)
			{
				FailureReason = WhyNot;
				return false;
			}
			const int32 CursorPlayer = KeyboardPlayerOf(InContext);
			if (!Cursor->IsVirtualCursorActiveForUser(CursorPlayer))
			{
				FailureReason = FString::Printf(TEXT("player %d's virtual cursor is not active, so it has no confirm button to press"), CursorPlayer);
				return false;
			}
			Cursor->SetConfirmPressedForUser(CursorPlayer, bPressed);
			return true;
		}

	private:
		bool bPressed;
	};

	FDreamPixelResolver MakeLocatorCentreResolver(const FDreamLocatorRef& InLocator)
	{
		return [InLocator](FDreamDriverContext& InContext) -> TOptional<FVector2D>
		{
			UDreamWidget* Widget = InContext.FindOne(InLocator);
			if (Widget == nullptr)
			{
				return TOptional<FVector2D>();
			}
			// Through the context's camera when a world-space pointer has one; with none, exactly the
			// screen-space projection this always was.
			return FDreamDriverProjection::WidgetCentrePixel(Widget, InContext.Camera.Get());
		};
	}

	/**
	 * Runs a latent sequence one step per engine frame.
	 *
	 * The context is held by pointer and the rig that owns it has to outlive the command. That is not
	 * a rule this class can enforce -- a latent command outlives the test body that queued it, which
	 * is the whole point -- so a rig driven latently belongs to the test fixture, not to RunTest's
	 * stack frame.
	 */
	class FDreamDriverSequenceLatentCommand : public IAutomationLatentCommand
	{
	public:
		FDreamDriverSequenceLatentCommand(FDreamDriverContext* InContext, TArray<FDreamDriverStepRef> InSteps)
			: Context(InContext)
			, Steps(MoveTemp(InSteps))
		{
		}

		virtual bool Update() override
		{
			if (Context == nullptr)
			{
				if (FAutomationTestBase* ReportingTest = FAutomationTestFramework::Get().GetCurrentTest())
				{
					ReportingTest->AddError(TEXT("A driver sequence was performed without a context."));
				}
				return true;
			}
			// The engine's own frame time, not the headless pump's constant: under this pump the
			// frames are real ones, and a wait's timeout should be measured in the seconds that
			// actually passed.
			const float DeltaSeconds = FApp::GetDeltaTime();
			while (Steps.IsValidIndex(StepIndex))
			{
				const EDreamDriverStepResult Result = Steps[StepIndex]->Execute(*Context, DeltaSeconds);
				if (Result == EDreamDriverStepResult::Failed)
				{
					ReportStepFailure(*Context, Steps[StepIndex]);
					return true;
				}
				if (Result == EDreamDriverStepResult::Again)
				{
					// This engine frame IS the pump; nothing is advanced from here.
					return false;
				}
				++StepIndex;
			}
			return true;
		}

	private:
		FDreamDriverContext* Context = nullptr;
		TArray<FDreamDriverStepRef> Steps;
		int32 StepIndex = 0;
	};
}

FDreamDriverSequence::FDreamDriverSequence(FDreamDriverContext& InContext)
	: Context(&InContext)
{
}

FDreamDriverSequence& FDreamDriverSequence::Add(const FDreamDriverStepRef& InStep)
{
	using namespace DreamDriverSequenceLocal;
	// Every step, the custom ones included: a step added after AsPlayer is that player's, whoever wrote it. The
	// context's own player needs no wrapping -- the executor hands it that context anyway.
	if (StepPlayerIndex != INDEX_NONE && (Context == nullptr || StepPlayerIndex != Context->PlayerIndex))
	{
		Steps.Add(MakeShared<FDreamAsPlayerStep>(StepPlayerIndex, InStep));
		return *this;
	}
	Steps.Add(InStep);
	return *this;
}

FDreamDriverSequence& FDreamDriverSequence::AsPlayer(int32 InPlayerIndex)
{
	StepPlayerIndex = InPlayerIndex;
	return *this;
}

FDreamDriverSequence& FDreamDriverSequence::MoveTo(const FDreamLocatorRef& InLocator)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamMoveStep>(MakeLocatorCentreResolver(InLocator), InLocator->Describe()));
}

FDreamDriverSequence& FDreamDriverSequence::MoveToPixel(const FVector2D& InPixel)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D TargetPixel = InPixel;
	return Add(MakeShared<FDreamMoveStep>(
		[TargetPixel](FDreamDriverContext&) -> TOptional<FVector2D> { return TargetPixel; },
		TargetPixel.ToString()));
}

FDreamDriverSequence& FDreamDriverSequence::MoveBy(const FVector2D& InPixelDelta)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D Delta = InPixelDelta;
	return Add(MakeShared<FDreamMoveStep>(
		[Delta](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			return PointerPixelOf(InStepContext) + Delta;
		},
		FString::Printf(TEXT("offset %s"), *Delta.ToString())));
}

FDreamDriverSequence& FDreamDriverSequence::Press(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTriggerStep>(InButton, true));
}

FDreamDriverSequence& FDreamDriverSequence::Release(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTriggerStep>(InButton, false));
}

FDreamDriverSequence& FDreamDriverSequence::Press()
{
	return Press(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Release()
{
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Click(const FDreamLocatorRef& InLocator)
{
	return Click(InLocator, EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Click(const FDreamLocatorRef& InLocator, EDreamUIMouseButtonType InButton)
{
	// Move, frame, press, frame, release, frame. Three frames, because each of the three is queued
	// input and queued input is not observable until the frame after it was queued.
	MoveTo(InLocator);
	Press(InButton);
	return Release(InButton);
}

FDreamDriverSequence& FDreamDriverSequence::DragTo(const FDreamLocatorRef& InFrom, const FDreamLocatorRef& InTo)
{
	using namespace DreamDriverSequenceLocal;
	MoveTo(InFrom);
	Press(EDreamUIMouseButtonType::Left);
	Add(MakeShared<FDreamDragCrossThresholdStep>(MakeLocatorCentreResolver(InTo), InTo->Describe()));
	// A midpoint before the destination, so the drag is carried by at least three separate moves on
	// three separate frames. A drag delivered in one jump never produces an OnPointerDrag between its
	// ends, and a list that scrolls by accumulated delta would see one impulse instead of a motion.
	Add(MakeShared<FDreamMoveStep>(
		[InTo](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			UDreamWidget* Widget = InStepContext.FindOne(InTo);
			if (Widget == nullptr)
			{
				return TOptional<FVector2D>();
			}
			const TOptional<FVector2D> TargetPixel = FDreamDriverProjection::WidgetCentrePixel(Widget, InStepContext.Camera.Get());
			if (!TargetPixel.IsSet())
			{
				return TOptional<FVector2D>();
			}
			return (PointerPixelOf(InStepContext) + TargetPixel.GetValue()) * 0.5;
		},
		FString::Printf(TEXT("halfway to %s"), *InTo->Describe())));
	MoveTo(InTo);
	// One frame resting on the target before letting go, so whatever the drop lands on is what the
	// pointer was already over rather than what it arrived at in the same frame.
	WaitFrames(1);
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::DragBy(const FDreamLocatorRef& InFrom, const FVector2D& InPixelDelta)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D Delta = InPixelDelta;
	// Resolved from the PRESS position rather than from wherever the cursor has got to, so every one
	// of the moves below aims at the same destination.
	FDreamPixelResolver DestinationResolver = [Delta](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
	{
		const UDreamPointerEventData* EventData = InStepContext.GetPointerEventData(0);
		if (EventData == nullptr)
		{
			return TOptional<FVector2D>();
		}
		return FVector2D(EventData->PressPointerPosition.X, EventData->PressPointerPosition.Y) + Delta;
	};

	MoveTo(InFrom);
	Press(EDreamUIMouseButtonType::Left);
	Add(MakeShared<FDreamDragCrossThresholdStep>(DestinationResolver, Delta.ToString()));
	Add(MakeShared<FDreamMoveStep>(
		[DestinationResolver](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			const TOptional<FVector2D> Destination = DestinationResolver(InStepContext);
			if (!Destination.IsSet())
			{
				return TOptional<FVector2D>();
			}
			return (PointerPixelOf(InStepContext) + Destination.GetValue()) * 0.5;
		},
		FString::Printf(TEXT("halfway to offset %s"), *Delta.ToString())));
	Add(MakeShared<FDreamMoveStep>(DestinationResolver, FString::Printf(TEXT("offset %s"), *Delta.ToString())));
	WaitFrames(1);
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::ScrollBy(const FVector2D& InAxisValue)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamScrollStep>(InAxisValue));
}

FDreamDriverSequence& FDreamDriverSequence::Navigate(EDreamUINavigationDirection InDirection)
{
	using namespace DreamDriverSequenceLocal;
	// Pressed, given a frame to move focus, then released. A direction that was never released stays
	// held, and the repeat timer would move focus again on the frame after.
	Add(MakeShared<FDreamNavigateStep>(InDirection, true));
	return Add(MakeShared<FDreamNavigateStep>(InDirection, false));
}

FDreamDriverSequence& FDreamDriverSequence::NavigationTrigger(bool bInTriggerPress)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamNavigationTriggerStep>(bInTriggerPress));
}

FDreamDriverSequence& FDreamDriverSequence::Back()
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamBackStep>());
}

FDreamDriverSequence& FDreamDriverSequence::TouchDown(int32 InFingerId, const FVector2D& InPixel)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D TargetPixel = InPixel;
	return Add(MakeShared<FDreamTouchStep>(EDreamDriverTouchPhase::Began, InFingerId,
		[TargetPixel](FDreamDriverContext&) -> TOptional<FVector2D> { return TargetPixel; },
		TargetPixel.ToString()));
}

FDreamDriverSequence& FDreamDriverSequence::TouchMoveTo(int32 InFingerId, const FVector2D& InPixel)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D TargetPixel = InPixel;
	return Add(MakeShared<FDreamTouchStep>(EDreamDriverTouchPhase::Moved, InFingerId,
		[TargetPixel](FDreamDriverContext&) -> TOptional<FVector2D> { return TargetPixel; },
		TargetPixel.ToString()));
}

FDreamDriverSequence& FDreamDriverSequence::TouchUp(int32 InFingerId)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTouchStep>(EDreamDriverTouchPhase::Ended, InFingerId, FDreamPixelResolver(), FString()));
}

FDreamDriverSequence& FDreamDriverSequence::WaitSeconds(float InSeconds)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamWaitSecondsStep>(InSeconds));
}

FDreamDriverSequence& FDreamDriverSequence::ActivateVirtualCursor()
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamVirtualCursorActivateStep>());
}

FDreamDriverSequence& FDreamDriverSequence::VirtualCursorStick(const FVector2D& InStick, float InSeconds)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamVirtualCursorStickStep>(InStick, InSeconds));
}

FDreamDriverSequence& FDreamDriverSequence::VirtualCursorPress()
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamVirtualCursorConfirmStep>(true));
}

FDreamDriverSequence& FDreamDriverSequence::VirtualCursorRelease()
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamVirtualCursorConfirmStep>(false));
}

FDreamDriverSequence& FDreamDriverSequence::Type(const FString& InText)
{
	using namespace DreamDriverSequenceLocal;
	for (int32 CharacterIndex = 0; CharacterIndex < InText.Len(); ++CharacterIndex)
	{
		// One step, so one frame, per character: a field that reacts to each character -- a counter,
		// a live validation -- sees them arrive one at a time, as they do from a keyboard.
		Add(MakeShared<FDreamTypeCharacterStep>(InText[CharacterIndex]));
	}
	return *this;
}

FDreamDriverSequence& FDreamDriverSequence::Type(const TCHAR* InText)
{
	return Type(FString(InText));
}

FDreamDriverSequence& FDreamDriverSequence::Type(const FKey& InKey)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTypeKeyStep>(InKey, FKey()));
}

FDreamDriverSequence& FDreamDriverSequence::TypeChord(const FKey& InModifier, const FKey& InKey)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTypeKeyStep>(InKey, InModifier));
}

FDreamDriverSequence& FDreamDriverSequence::Key(const FKey& InKey, EDreamDriverModifierKeys InModifiers)
{
	// Pressed, given a frame for the pipeline to act on the press, then released and given a frame: Navigate's shape.
	KeyDown(InKey, InModifiers);
	return KeyUp(InKey, InModifiers);
}

FDreamDriverSequence& FDreamDriverSequence::KeyDown(const FKey& InKey, EDreamDriverModifierKeys InModifiers)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamKeyStep>(InKey, InModifiers, /*bInPressed*/ true, /*bInTypesTabCharacter*/ false));
}

FDreamDriverSequence& FDreamDriverSequence::KeyUp(const FKey& InKey, EDreamDriverModifierKeys InModifiers)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamKeyStep>(InKey, InModifiers, /*bInPressed*/ false, /*bInTypesTabCharacter*/ false));
}

FDreamDriverSequence& FDreamDriverSequence::Tab()
{
	using namespace DreamDriverSequenceLocal;
	Add(MakeShared<FDreamKeyStep>(EKeys::Tab, EDreamDriverModifierKeys::None, /*bInPressed*/ true, /*bInTypesTabCharacter*/ true));
	return KeyUp(EKeys::Tab, EDreamDriverModifierKeys::None);
}

FDreamDriverSequence& FDreamDriverSequence::ShiftTab()
{
	using namespace DreamDriverSequenceLocal;
	// The character is '\t' with Shift held too: a keyboard makes the same character for both.
	Add(MakeShared<FDreamKeyStep>(EKeys::Tab, EDreamDriverModifierKeys::Shift, /*bInPressed*/ true, /*bInTypesTabCharacter*/ true));
	return KeyUp(EKeys::Tab, EDreamDriverModifierKeys::Shift);
}

FDreamDriverSequence& FDreamDriverSequence::WaitFrames(int32 InFrameCount)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamWaitFramesStep>(InFrameCount));
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamWaitDelegateStep>(InWaitDelegate, InTimeout, InDescription));
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout)
{
	return Wait(InWaitDelegate, InTimeout, FString());
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate)
{
	// Five seconds is not a meaningful limit, it is a backstop: the delegates FDreamUntil builds
	// carry their own timeout and fail on their own, and this only catches a hand-rolled one that
	// never does.
	return Wait(InWaitDelegate, FWaitTimeout::InSeconds(5.0), FString());
}

FDreamDriverSequence& FDreamDriverSequence::Then(TFunction<void(FDreamDriverContext&)> InAction)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamThenStep>(MoveTemp(InAction)));
}

FDreamDriverSequence& FDreamDriverSequence::Fail(const FString& InReason)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamFailStep>(InReason));
}

bool FDreamDriverSequence::Perform()
{
	using namespace DreamDriverSequenceLocal;
	if (Context == nullptr)
	{
		if (FAutomationTestBase* ReportingTest = FAutomationTestFramework::Get().GetCurrentTest())
		{
			ReportingTest->AddError(TEXT("A driver sequence was performed without a context."));
		}
		return false;
	}
	if (Context->bEnginePumped)
	{
		// This pump would be a second pump: every frame it asked for would run the event system,
		// the subsystems and the layout again inside an engine frame that already ran them.
		DreamDriverPumpLocal::ReportContextError(*Context, TEXT("Perform was called on a context whose frames belong to the engine (a PIE rig). Under PIE, build a Sequence and PerformLatent it; element actions (Click, Type, DragTo, ...) go through Perform and are refused for the same reason."));
		return false;
	}

	// A step that never finishes would otherwise hang the whole suite rather than fail one test.
	// Generous, because a Wait's own timeout should be what stops it in the ordinary case; this is
	// the backstop for a step whose timeout is longer than anyone meant.
	constexpr int32 MaxFramesPerStep = 100000;

	for (const FDreamDriverStepRef& Step : Steps)
	{
		int32 FramesSpent = 0;
		for (;;)
		{
			const EDreamDriverStepResult Result = Step->Execute(*Context, Context->FrameSeconds);
			if (Result == EDreamDriverStepResult::Done)
			{
				break;
			}
			if (Result == EDreamDriverStepResult::Failed)
			{
				ReportStepFailure(*Context, Step);
				return false;
			}
			if (++FramesSpent > MaxFramesPerStep)
			{
				if (FAutomationTestBase* ReportingTest = Context->CurrentTest != nullptr
					? Context->CurrentTest
					: FAutomationTestFramework::Get().GetCurrentTest())
				{
					ReportingTest->AddError(FString::Printf(
						TEXT("Driver step %s never finished: still asking for frames after %d of them."),
						*Step->Describe(), MaxFramesPerStep));
				}
				return false;
			}
			Context->PumpOneFrame(Context->FrameSeconds);
		}
	}
	return true;
}

void FDreamDriverSequence::PerformLatent()
{
	using namespace DreamDriverSequenceLocal;
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FDreamDriverSequenceLatentCommand>(Context, MoveTemp(Steps)));
}
