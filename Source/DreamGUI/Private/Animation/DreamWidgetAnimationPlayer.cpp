// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationPlayer.h"

#include "Core/Components/DreamWidget.h"
#include "Animation/DreamUIDirectAnimationEvaluation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "HAL/IConsoleManager.h"
#include "MovieScene.h"

static TAutoConsoleVariable<int32> CVarDreamUIDirectAnimationEvaluation(
	TEXT("DreamUI.Animation.DirectEvaluation"),
	1,
	TEXT("1: a widget animation made only of plain property tracks is evaluated straight from its channels by its player. ")
	TEXT("0: every animation goes through the sequencer's entity system. Turning it off hands playing animations to the sequencer; ")
	TEXT("turning it on applies from each animation's next play."),
	ECVF_Default);


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

void UDreamWidgetAnimationPlayer::UpdateMovieSceneInstance(FMovieSceneEvaluationRange InRange, EMovieScenePlayerStatus::Type PlayerStatus, const FMovieSceneUpdateArgs& Args)
{
	if (!TryEvaluateDirectly(InRange, PlayerStatus))
	{
		Super::UpdateMovieSceneInstance(InRange, PlayerStatus, Args);
	}
}

bool UDreamWidgetAnimationPlayer::TryEvaluateDirectly(const FMovieSceneEvaluationRange& InRange, EMovieScenePlayerStatus::Type PlayerStatus)
{
	UMovieSceneSequence* PlayedSequence = GetSequence();
	if (DirectEvaluationDecidedFor.Get() != PlayedSequence)
	{
		DirectEvaluationDecidedFor = PlayedSequence;
		DirectEvaluation.Reset();
		// Only what the component starts: it always says whether the values stay or go back when the animation ends, and
		// that is the one completion rule evaluated here. Weights blend against the sequencer's captured initial values,
		// and a replicated or warped playback is the sequencer's business too.
		const EMovieSceneCompletionModeOverride Completion = PlaybackSettings.FinishCompletionStateOverride;
		if (CVarDreamUIDirectAnimationEvaluation.GetValueOnGameThread() != 0 && PlayedSequence != nullptr
			&& !HasDynamicWeighting() && GetPlaybackClient() == nullptr
			&& (Completion == EMovieSceneCompletionModeOverride::ForceKeepState || Completion == EMovieSceneCompletionModeOverride::ForceRestoreState))
		{
			DirectEvaluation = FDreamUIDirectAnimationEvaluation::TryCreate(*PlayedSequence);
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
	const UMovieScene* MovieScene = PlayedSequence != nullptr ? PlayedSequence->GetMovieScene() : nullptr;
	if (!DirectEvaluation.IsValid() || MovieScene == nullptr)
	{
		return false;
	}
	// What the sequencer's UpdateMovieSceneInstance does around its runner: an update clears a pending skip, and the
	// pre- and post-evaluation callbacks run on either side of the values being written.
	bSkipNextUpdate = false;
	FMovieSceneContext Context(InRange, PlayerStatus);
	PreEvaluation(Context);
	const FFrameTime Time = ConvertFrameTime(InRange.GetTime(), InRange.GetFrameRate(), MovieScene->GetTickResolution());
	if (!DirectEvaluation->Evaluate(*this, Time))
	{
		// A property only the sequencer can write, found before anything was written: it takes over from here.
		DirectEvaluation.Reset();
		return false;
	}
	PostEvaluation(Context);
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
	// The next play decides again: its settings, or the sequence the player is given next, may differ.
	DirectEvaluation.Reset();
	DirectEvaluationDecidedFor.Reset();
}
