// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationPlayer.h"

#include "Animation/DreamUIAnimationClock.h"
#include "Animation/DreamUIAnimationTicker.h"

#include "Core/Components/DreamWidget.h"
#include "Animation/DreamUIDirectAnimationEvaluation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "DreamGUI.h"
#include "Channels/MovieSceneTimeWarpChannel.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "Evaluation/MovieSceneSequenceHierarchy.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeController.h"
#include "MovieSceneTimeHelpers.h"

static TAutoConsoleVariable<int32> CVarDreamUIDirectAnimationEvaluation(
	TEXT("DreamUI.Animation.DirectEvaluation"),
	1,
	TEXT("1: a widget animation made only of plain property tracks is evaluated straight from its channels by its player. ")
	TEXT("0: every animation goes through the sequencer's entity system. Turning it off hands playing animations to the sequencer; ")
	TEXT("turning it on applies from each animation's next play."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDreamUILiteAnimationPlayer(
	TEXT("DreamUI.Animation.LitePlayer"),
	1,
	TEXT("1: a widget animation its player evaluates itself also keeps its own time between loop boundaries, instead of going ")
	TEXT("through the sequencer's per-frame update, and a player that is not playing skips that update, which has nothing to do for it. ")
	TEXT("0: every player goes through the sequencer's per-frame update. Either way applies from the next frame."),
	ECVF_Default);

// Per frame, under `stat DreamGUI`: how many playing animations kept their own time, and how many ticks went to the
// sequencer's update instead -- a play's first frame, and whatever the player's own tick leaves to it.
DECLARE_DWORD_COUNTER_STAT(TEXT("Animation players keeping their own time"), STAT_DreamUIAnimationPlayersOwnTick, STATGROUP_DreamGUI);
DECLARE_DWORD_COUNTER_STAT(TEXT("Animation players updated by the sequencer"), STAT_DreamUIAnimationPlayersSequencerTick, STATGROUP_DreamGUI);

UObject* UDreamWidgetAnimationPlayer::GetPlaybackContext() const
{
	// The OUTER, not the sequence: a standalone UDreamUISequence asset is outered to its package
	// and a cast of the sequence to the embedded type would assert on it, while the component
	// that created this player is the same for either kind and is what the bindings resolve from.
	if (const UDreamWidgetAnimationComponent* Component = GetTypedOuter<UDreamWidgetAnimationComponent>())
	{
		return Component->GetWidget();
	}
	return nullptr;
}

TArray<UObject*> UDreamWidgetAnimationPlayer::GetEventContexts() const
{
	TArray<UObject*> Contexts;
	if (UObject* PlaybackContext = GetPlaybackContext())
	{
		Contexts.Add(PlaybackContext);
	}
	return Contexts;
}

void UDreamWidgetAnimationPlayer::SetLoopCount(int32 InLoopCount)
{
	PlaybackSettings.LoopCount.Value = InLoopCount;
}

void UDreamWidgetAnimationPlayer::FlushQueuedEvaluation()
{
	TSharedPtr<FMovieSceneEntitySystemRunner> Runner = RootTemplateInstance.GetRunner();
	if (Runner.IsValid() && Runner->HasQueuedUpdates())
	{
		Runner->Flush();
	}
}

void UDreamWidgetAnimationPlayer::TrustTimeController()
{
	// Not known to be DreamGUI's clock: ticked and read through its virtuals. One trusted before keeps its state again.
	if (TrustedClock != nullptr)
	{
		TrustedClock->Detach();
		TrustedClock = nullptr;
	}
	TrustedTimeController = TimeController;
}

void UDreamWidgetAnimationPlayer::TrustClock(const TSharedPtr<FDreamUIAnimationClock>& InClock)
{
	if (TrustedClock != nullptr && TrustedClock != InClock.Get())
	{
		TrustedClock->Detach();
	}
	SetTimeController(InClock);
	TrustedTimeController = InClock;
	TrustedClock = InClock.Get();
	if (TrustedClock != nullptr)
	{
		TrustedClock->AttachTo(ClockState);
		bClockTicksAsGiven = TrustedClock->bTicksAsGiven;
	}
}

void UDreamWidgetAnimationPlayer::UpdateMovieSceneInstance(FMovieSceneEvaluationRange InRange, EMovieScenePlayerStatus::Type PlayerStatus, const FMovieSceneUpdateArgs& Args)
{
	// A play started by whatever path -- the component asks for ticks after its own (KeepTicked) -- evaluates here first.
	if (Ticker != nullptr && TickerActiveIndex == INDEX_NONE && Status == EMovieScenePlayerStatus::Playing)
	{
		Ticker->Activate(*this);
	}
	if (!TryEvaluateDirectly(InRange, PlayerStatus))
	{
		Super::UpdateMovieSceneInstance(InRange, PlayerStatus, Args);
	}
}

void UDreamWidgetAnimationPlayer::UseTicker(UDreamUIAnimationTicker* InTicker)
{
	Ticker = InTicker;
	if (InTicker != nullptr)
	{
		// What Initialize registers with, as InitializeForTick would have set the world's.
		TickManager = InTicker->GetRegistry();
	}
}

void UDreamWidgetAnimationPlayer::KeepTicked()
{
	if (Ticker != nullptr)
	{
		Ticker->Activate(*this);
	}
}

bool UDreamWidgetAnimationPlayer::HasNothingToTick() const
{
	return Status != EMovieScenePlayerStatus::Playing && TimeControllerState == ETimeControllerState::ReadyToPlay && !bUpdateNetSync;
}

void UDreamWidgetAnimationPlayer::BeginDestroy()
{
	if (Ticker != nullptr)
	{
		Ticker->Forget(*this);
	}
	// The clock may outlive this player, held elsewhere: it keeps its state again.
	if (TrustedClock != nullptr)
	{
		TrustedClock->Detach();
		TrustedClock = nullptr;
	}
	Super::BeginDestroy();
}

void UDreamWidgetAnimationPlayer::Initialize(UMovieSceneSequence* InSequence, const FMovieSceneSequencePlaybackSettings& InSettings)
{
	Super::Initialize(InSequence, InSettings);
	// Whatever the sequence: see DirectEvaluationDecidedFor.
	DirectEvaluationDecidedFor = nullptr;
}

void UDreamWidgetAnimationPlayer::Initialize(UMovieSceneSequence* InSequence)
{
	Super::Initialize(InSequence);
	DirectEvaluationDecidedFor = nullptr;
}

bool UDreamWidgetAnimationPlayer::TryEvaluateDirectly(const FMovieSceneEvaluationRange& InRange, EMovieScenePlayerStatus::Type PlayerStatus, bool bInOwnTick)
{
	UMovieSceneSequence* PlayedSequence = GetSequence();
	if (DirectEvaluationDecidedFor != PlayedSequence)
	{
		DirectEvaluationDecidedFor = PlayedSequence;
		DirectEvaluation.Reset();
		// The play before's, if it still does for this sequence: see KeptDirectEvaluation.
		const TSharedPtr<FDreamUIDirectAnimationEvaluation> Kept = MoveTemp(KeptDirectEvaluation);
		// Only what the component starts: it always says whether the values stay or go back when the animation ends, and
		// that is the one completion rule evaluated here. Weights blend against the sequencer's captured initial values,
		// and a replicated or warped playback is the sequencer's business too.
		const EMovieSceneCompletionModeOverride Completion = PlaybackSettings.FinishCompletionStateOverride;
		if (CVarDreamUIDirectAnimationEvaluation.GetValueOnGameThread() != 0 && PlayedSequence != nullptr
			&& !HasDynamicWeighting() && GetPlaybackClient() == nullptr
			&& (Completion == EMovieSceneCompletionModeOverride::ForceKeepState || Completion == EMovieSceneCompletionModeOverride::ForceRestoreState)
			// A time warp in the play-rate domain at the root remaps the time the clock gives (UpdateTimeCursorPosition_Internal),
			// which the player's own tick does not: looked for here, once a play, rather than by that tick every frame, which
			// only ever runs for an animation evaluated directly.
			&& !IsRootPlayRateWarped())
		{
			DirectEvaluation = Kept.IsValid() && Kept->IsStillPlanFor(*PlayedSequence) ? Kept : FDreamUIDirectAnimationEvaluation::TryCreate(*PlayedSequence);
		}
		if (DirectEvaluation.IsValid())
		{
			LiteDisplayRate = GetDisplayRate();
		}
	}
	if (DirectEvaluation.IsValid() && CVarDreamUIDirectAnimationEvaluation.GetValueOnGameThread() == 0)
	{
		// Turned off mid-play: the sequencer takes over from the values it would have found. Restored, it captures the
		// originals as its own state to restore; kept, the channels without keys still hold them.
		if (PlaybackSettings.FinishCompletionStateOverride == EMovieSceneCompletionModeOverride::ForceRestoreState)
		{
			DirectEvaluation->RestoreInitialValues();
		}
		else
		{
			DirectEvaluation->DiscardInitialValues();
		}
		DirectEvaluation.Reset();
	}
	// A plan is only made for a sequence with a movie scene, and holds its tick resolution: the frame reads neither.
	if (!DirectEvaluation.IsValid())
	{
		return false;
	}
	// What the sequencer's UpdateMovieSceneInstance does around its runner: an update clears a pending skip, and the
	// pre- and post-evaluation callbacks run on either side of the values being written. From the player's own tick between
	// boundaries they have nothing to run unless the update event is listened to: the post-evaluation callbacks are only
	// queued by the sequencer's cursor update (UpdateTimeCursorPosition_Internal), which has not run, and nothing queues
	// pre-evaluation ones. Every playing widget of a wall paid for both every frame.
	bSkipNextUpdate = false;
	const bool bAroundEvaluation = !bInOwnTick || OnSequenceUpdated().IsBound();
	const FMovieSceneContext Context(InRange, PlayerStatus);
	if (bAroundEvaluation)
	{
		PreEvaluation(Context);
	}
	const FFrameTime Time = ConvertFrameTime(InRange.GetTime(), InRange.GetFrameRate(), DirectEvaluation->GetTickResolution());
	// Held for the evaluation: a listener of one of its writes may stop the animation, and OnStopped lets it go. Made for
	// the sequence this player plays and holds (DirectEvaluationDecidedFor), which is so alive while it evaluates.
	const TSharedPtr<FDreamUIDirectAnimationEvaluation> Evaluation = DirectEvaluation;
	if (!Evaluation->Evaluate(*this, Time))
	{
		// A property only the sequencer can write, found before anything was written: it takes over from here.
		if (DirectEvaluation == Evaluation)
		{
			DirectEvaluation.Reset();
		}
		return false;
	}
	if (bAroundEvaluation)
	{
		PostEvaluation(Context);
	}
	return true;
}

void UDreamWidgetAnimationPlayer::OnStopped()
{
	Super::OnStopped();
	if (DirectEvaluation.IsValid())
	{
		if (PlaybackSettings.FinishCompletionStateOverride == EMovieSceneCompletionModeOverride::ForceRestoreState)
		{
			DirectEvaluation->RestoreInitialValues();
		}
		else
		{
			DirectEvaluation->DiscardInitialValues();
		}
	}
	// The next play decides again: its settings, or the sequence the player is given next, may differ. What it decides on
	// is kept for it, its values put back or forgotten above (KeptDirectEvaluation).
	KeptDirectEvaluation = MoveTemp(DirectEvaluation);
	DirectEvaluation.Reset();
	DirectEvaluationDecidedFor = nullptr;
}

void UDreamWidgetAnimationPlayer::OnStartedPlaying()
{
	Super::OnStartedPlaying();
	// The sequencer's HasAuthority walks the outers for the actor each time it is asked; here the walk, and the question, are
	// made once a play. The actor a widget's animation plays under does not change while it plays, nor does its network
	// role, and a look at the actor every frame -- a thousand playing widgets' thousand actors -- is part of what TickLite
	// saves.
	const AActor* Actor = GetTypedOuter<AActor>();
	bAuthorityAtStart = Actor != nullptr && Actor->HasAuthority();
}

bool UDreamWidgetAnimationPlayer::TryPrepareReplay(const FMovieSceneSequencePlaybackSettings& InSettings)
{
	UMovieSceneSequence* PlayedSequence = GetSequence();
	const UMovieScene* MovieScene = PlayedSequence != nullptr ? PlayedSequence->GetMovieScene() : nullptr;
	if (MovieScene == nullptr || !TimeController.IsValid() || IsPlaying() || IsPaused() || IsEvaluating() || !RootTemplateInstance.IsValid()
		// What Initialize registered the player with, and what it set up the sequence's instance for.
		|| InSettings.TickInterval != PlaybackSettings.TickInterval
		|| InSettings.bInheritTickIntervalFromOwner != PlaybackSettings.bInheritTickIntervalFromOwner
		|| InSettings.bDynamicWeighting || PlaybackSettings.bDynamicWeighting
		|| InSettings.bRandomStartTime)
	{
		return false;
	}
	PlaybackSettings = InSettings;
	// Initialize's frame range: the sequence's playback range, in the display rate, with the rates it falls back to.
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (!TickResolution.IsValid() || TickResolution.Numerator <= 0)
	{
		TickResolution = FFrameRate(60000, 1);
	}
	if (!DisplayRate.IsValid() || DisplayRate.Numerator <= 0)
	{
		DisplayRate = FFrameRate(30, 1);
	}
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	const FFrameNumber SrcStartFrame = UE::MovieScene::DiscreteInclusiveLower(PlaybackRange);
	const FFrameNumber SrcEndFrame = UE::MovieScene::DiscreteExclusiveUpper(PlaybackRange);
	const FFrameTime EndingTime = ConvertFrameTime(SrcEndFrame, TickResolution, DisplayRate);
	const FFrameNumber StartingFrame = ConvertFrameTime(SrcStartFrame, TickResolution, DisplayRate).FloorToFrame();
	const FFrameNumber EndingFrame = EndingTime.FloorToFrame();
	SetFrameRange(StartingFrame.Value, (EndingFrame - StartingFrame).Value, EndingTime.GetSubFrame());
	// ...and its start: the offset asked for, in the range, from where the stop before left the cursor.
	const FFrameTime StartingTimeOffset = FMath::Clamp<FFrameTime>(PlaybackSettings.StartTime * DisplayRate, 0, GetFrameDuration() - 1);
	LatentActionManager.ClearLatentActions();
	PlayPosition.Reset(StartTime + StartingTimeOffset);
	TimeController->Reset(GetCurrentTime());
	return true;
}

bool UDreamWidgetAnimationPlayer::IsRootPlayRateWarped() const
{
	const FMovieSceneSequenceHierarchy* Hierarchy = RootTemplateInstance.GetHierarchy();
	return Hierarchy != nullptr && Hierarchy->GetRootTransform().FindFirstWarpDomain() == UE::MovieScene::ETimeWarpChannelDomain::PlayRate;
}

void UDreamWidgetAnimationPlayer::TickFromSequenceTickManager(float DeltaSeconds, FMovieSceneEntitySystemRunner* InRunner)
{
	const ELiteTick Choice = ChooseLiteTick();
	bTickedLite = Choice != ELiteTick::Sequencer;
	if (Choice == ELiteTick::Sequencer)
	{
		INC_DWORD_STAT(STAT_DreamUIAnimationPlayersSequencerTick);
		// Nothing has been touched: the sequencer's update finds the player exactly as the tick manager left it.
		Super::TickFromSequenceTickManager(DeltaSeconds, InRunner);
		return;
	}
	if (Choice == ELiteTick::Advance)
	{
		INC_DWORD_STAT(STAT_DreamUIAnimationPlayersOwnTick);
		// What the base wraps its update in (UpdateAsync): the runner of the group being ticked is the current one, and this
		// is the main level update, the one whose evaluations may be queued rather than flushed. The flag is a bitfield, so
		// it is set and cleared by hand; it is clear again before this returns, and nothing in between hands a tick to the
		// base, whose UpdateAsync checks that it is clear.
		TGuardValue<FMovieSceneEntitySystemRunner*> RunnerGuard(CurrentRunner, InRunner);
		check(!bIsAsyncUpdate);
		bIsAsyncUpdate = true;
		TickLite(DeltaSeconds);
		bIsAsyncUpdate = false;
	}
}

UDreamWidgetAnimationPlayer::ELiteTick UDreamWidgetAnimationPlayer::ChooseLiteTick() const
{
	if (CVarDreamUILiteAnimationPlayer.GetValueOnGameThread() == 0
		// The two things the sequencer's update does whether or not the player plays: a replicated client's correction
		// (UpdateNetworkSync), and starting a time controller that was not yet ready when play was asked for.
		|| bUpdateNetSync || TimeControllerState != ETimeControllerState::ReadyToPlay)
	{
		return ELiteTick::Sequencer;
	}
	if (Status != EMovieScenePlayerStatus::Playing)
	{
		// With neither, the update has nothing for a player that is not playing: it only looks up the world to note the
		// game time it ticked at (LastTickGameTimeSeconds), which nothing reads.
		return ELiteTick::Idle;
	}
	// The first update of a play starts it (OnStartedPlaying), and one after a network correction skips its evaluation.
	if (bPendingOnStartedPlaying || bSkipNextUpdate
		// A play of no length only warns.
		|| (DurationFrames == 0 && DurationSubFrames == 0.f)
		// A rate that is not positive plays against the direction the loop and end checks are made for; what the sequencer
		// does with that stays its own.
		|| PlaybackSettings.PlayRate <= 0.f
		// Only the clock the component vouched for. TickLite does not work out the dilated rate the update asks the time
		// with, and that clock does not read it.
		|| !TimeController.IsValid() || TimeController != TrustedTimeController
		// Only an animation the player evaluates itself: the sequencer's own evaluation of anything else is most of its
		// frame, and handing a direct evaluation over to it, when that is turned off, belongs in the sequencer's update.
		|| !IsEvaluatingDirectly() || CVarDreamUIDirectAnimationEvaluation.GetValueOnGameThread() == 0
		// What the direct evaluation declines when it starts, should any of it have been given to the player since.
		|| HasDynamicWeighting()
		|| Observer.GetObject() != nullptr || Observer.GetInterface() != nullptr
		|| PlaybackClient.GetObject() != nullptr || PlaybackClient.GetInterface() != nullptr
		// The sequencer's update refuses to run inside an evaluation; the refusal stays its own.
		|| IsEvaluating())
	{
		return ELiteTick::Sequencer;
	}
	// A play-rate time warp at the root, which TickLite does not remap the time by, has no direct evaluation
	// (TryEvaluateDirectly), and so never gets this far.
	return ELiteTick::Advance;
}

void UDreamWidgetAnimationPlayer::TickLite(float DeltaSeconds)
{
	// UMovieSceneSequencePlayer::Update, for a playing player whose clock is ready and which has nothing to sync or skip.
	// The clock is ticked once, with the delta the tick manager handed over -- dilated or not, paused or not, as the world
	// and the tick interval say -- and the signed play rate, and it is the one source of the time.
	const float PlayRate = bReversePlayback ? -PlaybackSettings.PlayRate : PlaybackSettings.PlayRate;
	// DreamGUI's clock, the one vouched for (ChooseLiteTick), is ticked and read in line: what its virtuals do, without a
	// call into the engine and a virtual call for each, for every playing widget every frame. One whose tick changes the
	// delta first is ticked through its virtual. Its state is kept here (ClockState): the clock itself is not read.
	FDreamUIAnimationClock* const Clock = TrustedClock;
	if (Clock != nullptr && bClockTicksAsGiven)
	{
		FDreamUIAnimationClock::TickInLine(ClockState, DeltaSeconds, PlayRate);
	}
	else
	{
		TimeController->Tick(DeltaSeconds, PlayRate);
	}
	// The update multiplies the rate by the world's dilation before it asks for the time. A clock vouched for does not read
	// it -- FMovieSceneTimeController_Tick answers from what its ticks added up to -- so the world is not looked up. The
	// display rate is the one this play decided on its evaluation with (LiteDisplayRate).
	const FFrameTime NewTime = Clock != nullptr
		? FDreamUIAnimationClock::TimeInLine(ClockState, FQualifiedFrameTime(PlayPosition.GetCurrentPosition(), PlayPosition.GetInputRate()))
		: TimeController->RequestCurrentTime(GetCurrentTime(), PlayRate, LiteDisplayRate);

	if (GetPauseTimeForNewPosition(NewTime).IsSet() || ShouldStopOrLoop(NewTime))
	{
		// A loop boundary, the end, or where PlayTo asked to pause: the sequencer's own cursor update takes the time the
		// clock has just given and does the rest. The loop count, the direction, finishing and the pause, and the events
		// around them, all change there and only there; and the clock is not ticked a second time.
		UpdateTimeCursorPosition(NewTime, EUpdatePositionMethod::Play);
	}
	else
	{
		// UpdateTimeCursorPosition_Internal between boundaries. A play with a length arms the zero-duration warning again...
		bWarnZeroDuration = true;
		// ...the cursor moves only by PlayTo, the way the player is playing...
		const FMovieSceneEvaluationRange Range = PlayPosition.PlayTo(NewTime, bReversePlayback ? EPlayDirection::Backwards : EPlayDirection::Forwards);
		// ...and the range goes to UpdateMovieSceneInstance: the direct evaluation takes it, between the pre- and
		// post-evaluation callbacks, and should it hand the animation over to the sequencer, the sequencer is given it with
		// the arguments its own update gives, so that the update is queued rather than flushed, as in the main level update.
		// Worked out only then: the direct evaluation needs none of them.
		if (!TryEvaluateDirectly(Range, EMovieScenePlayerStatus::Playing, /*bInOwnTick*/ true))
		{
			const UMovieSceneSequence* RootSequence = RootTemplateInstance.GetSequence(MovieSceneSequenceID::Root);
			FMovieSceneUpdateArgs Args;
			Args.bIsAsync = bIsAsyncUpdate
				&& !(RootSequence != nullptr && EnumHasAnyFlags(RootSequence->GetFlags(), EMovieSceneSequenceFlags::BlockingEvaluation));
			Super::UpdateMovieSceneInstance(Range, EMovieScenePlayerStatus::Playing, Args);
		}

		// A listener of one of the writes may have stopped or paused the player from inside them. That has run already --
		// the direct evaluation does not count as evaluating, so nothing was deferred -- exactly as inside the sequencer's
		// update, and what follows reads the player as the listener left it, as the sequencer's does.
		//
		// The sequencer's update leaves one more step to run after the evaluation, UpdateNetworkSyncProperties, which is
		// private: the snapshot a replicated player sends its clients, kept by the authority. This is that step. Nothing
		// reads the snapshot of a player that is not replicated, and nothing in DreamGUI replicates one; it is kept all the
		// same, so that what a frame leaves behind does not depend on which update ran it.
		if (bAuthorityAtStart)
		{
			NetSyncProps.LastKnownPosition = PlayPosition.GetCurrentPosition();
			NetSyncProps.LastKnownStatus = Status;
			NetSyncProps.LastKnownNumLoops = CurrentNumLoops;
			NetSyncProps.LastKnownSerialNumber = SerialNumber;
		}
	}
	// And what the update does last, whichever way the frame went.
	bSkipNextUpdate = false;
}
