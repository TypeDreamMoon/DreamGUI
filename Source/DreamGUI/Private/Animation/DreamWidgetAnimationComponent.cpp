// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamUISequence.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Animation/DreamUIMovieScenePropertyAccessors.h"
#include "Core/DreamUserWidget.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

#include "MovieSceneSequencePlaybackSettings.h"
#include "MovieSceneSequenceTickManager.h"

UDreamWidgetAnimationComponent::UDreamWidgetAnimationComponent()
{
}

bool FDreamUIAnimationHandle::IsValid() const
{
	// Live, not merely allocated: a stopped instance's player object lingers until the collector
	// takes it, and a handle to it must read as done the moment it stops.
	if (!::IsValid(Player.Get()))
	{
		return false;
	}
	const UDreamWidgetAnimationComponent* Owner = Player->GetTypedOuter<UDreamWidgetAnimationComponent>();
	return Owner != nullptr && Owner->OwnsLiveInstance(Player.Get());
}

bool UDreamWidgetAnimationComponent::OwnsLiveInstance(const UDreamWidgetAnimationPlayer* Player) const
{
	return IsActiveSequencePlayer(Player);
}

UMovieSceneSequence* FDreamUIAnimationHandle::GetAnimation() const
{
	return IsValid() ? Player->GetSequence() : nullptr;
}

void UDreamWidgetAnimationComponent::Awake()
{
	Super::Awake();
	InitSequencePlayer();

	if (PlaybackSettings.bAutoPlay && SequencePlayer && SequencePlayer->IsValid())
	{
		SequencePlayer->Play();
	}
}

void UDreamWidgetAnimationComponent::OnDestroy()
{
	Super::OnDestroy();
	StopAllAnimations();

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer->TearDown();
	}
}

// ---------------------------------------------------------------------------------------- play

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationByDisplayName(
	const FString& Name,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	UMovieSceneSequence* Sequence = GetSequenceByDisplayName(Name);
	if (!IsValid(Sequence))
	{
		// Fall back to the standalone assets; their address is the asset name.
		for (UDreamUISequence* Asset : SequenceAssets)
		{
			if (IsValid(Asset) && Asset->GetName() == Name)
			{
				Sequence = Asset;
				break;
			}
		}
	}
	if (!IsValid(Sequence))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Animation '%s' was not found on '%s'."), *Name, *GetPathName());
		return FDreamUIAnimationHandle();
	}
	return PlayAnimation(Sequence, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimation(
	UMovieSceneSequence* Animation,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	return PlayAnimationInternal(Animation, StartAtTime, TOptional<float>(), NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationTimeRange(
	UMovieSceneSequence* Animation,
	float StartAtTime,
	float EndAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	const TOptional<float> End = EndAtTime > 0.0f ? TOptional<float>(EndAtTime) : TOptional<float>();
	return PlayAnimationInternal(Animation, StartAtTime, End, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationForward(UMovieSceneSequence* Animation, float PlaybackSpeed, bool bRestoreState)
{
	return PlayAnimationRelative(Animation, EDreamUIAnimationPlayMode::Forward, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationReverse(UMovieSceneSequence* Animation, float PlaybackSpeed, bool bRestoreState)
{
	return PlayAnimationRelative(Animation, EDreamUIAnimationPlayMode::Reverse, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationRelative(UMovieSceneSequence* Animation, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	const bool bWantReverse = PlayMode == EDreamUIAnimationPlayMode::Reverse;
	const FDreamUIAnimationHandle Live = FindAnimationInstance(Animation);
	if (Live.IsValid())
	{
		// Turn the instance that is already there around from where it is. Starting a second one
		// from the far end would fight the first for every property they share.
		UDreamWidgetAnimationPlayer* Player = Live.Player;
		Player->SetPlayRate(FMath::Max(FMath::Abs(PlaybackSpeed), UE_SMALL_NUMBER));
		if (Player->IsPlaying())
		{
			if (Player->IsReversed() != bWantReverse)
			{
				Player->ChangePlaybackDirection();
			}
		}
		else if (bWantReverse)
		{
			Player->PlayReverse();
		}
		else
		{
			Player->Play();
		}
		return Live;
	}
	return PlayAnimationInternal(Animation, 0.0f, TOptional<float>(), 1, PlayMode, PlaybackSpeed, bRestoreState);
}

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::PlayAnimationInternal(
	UMovieSceneSequence* Animation,
	float StartAtTime,
	TOptional<float> EndAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	FDreamUIAnimationHandle Handle;
	if (!IsValid(Animation))
	{
		UE_LOG(DreamGUI, Warning, TEXT("PlayAnimation on '%s' was handed no animation."), *GetPathName());
		return Handle;
	}
	UDreamWidget* HostWidget = GetWidget();
	if (!IsValid(HostWidget) || HostWidget->GetWorld() == nullptr)
	{
		// The sequence tick manager lives on the world and asserts without one; an authoring tree
		// has no world and nothing to tick it, so there is nothing to play into.
		UE_LOG(DreamGUI, Warning, TEXT("PlayAnimation on '%s': the widget is not in a world, so nothing could tick the animation."), *GetPathName());
		return Handle;
	}

	// Sequencer writes the widget's vector properties through these. Registered at module start
	// too; this covers a process that never ran that hook, and costs a bool test otherwise.
	DreamUI::EnsureMovieScenePropertyAccessorsRegistered();

	FMovieSceneSequencePlaybackSettings Settings = PlaybackSettings;
	Settings.bAutoPlay = false;
	Settings.bRandomStartTime = false;
	Settings.StartTime = PlayMode == EDreamUIAnimationPlayMode::Forward ? FMath::Max(0.0f, StartAtTime) : 0.0f;
	Settings.PlayRate = FMath::Max(FMath::Abs(PlaybackSpeed), UE_SMALL_NUMBER);
	Settings.LoopCount.Value = NumLoopsToPlay == 0 ? -1 : FMath::Max(0, NumLoopsToPlay - 1);
	Settings.FinishCompletionStateOverride = bRestoreState
		? EMovieSceneCompletionModeOverride::ForceRestoreState
		: EMovieSceneCompletionModeOverride::ForceKeepState;

	UDreamWidgetAnimationPlayer* Player = NewObject<UDreamWidgetAnimationPlayer>(this);
	Player->InitializeForTick(this);
	Player->Initialize(Animation, Settings);

	if (EndAtTime.IsSet())
	{
		// The player's frame range is in display-rate frames, the unit Initialize set it up in.
		// A range is what the engine loops within, so the end applies to every loop rather than
		// only the last one; UMG's old player did the same.
		const FQualifiedFrameTime PlayerStart = Player->GetStartTime();
		const FFrameRate DisplayRate = PlayerStart.Rate;
		const FFrameNumber StartFrame = PlayerStart.Time.FrameNumber;
		const FFrameNumber SequenceEnd = Player->GetEndTime().Time.FrameNumber;
		const FFrameNumber EndFrame = FMath::Clamp((EndAtTime.GetValue() * DisplayRate).CeilToFrame(), StartFrame + 1, SequenceEnd);
		Player->SetFrameRange(StartFrame.Value, (EndFrame - StartFrame).Value);
	}

	Player->OnNativeFinished.BindUObject(this, &UDreamWidgetAnimationComponent::HandleActiveSequencePlayerFinished, Player);
	ActiveSequencePlayers.Add(Player);

	if (PlayMode == EDreamUIAnimationPlayMode::Reverse)
	{
		if (StartAtTime > 0.0f)
		{
			const double ReverseStartTime = FMath::Max(0.0, Player->GetEndTime().AsSeconds() - StartAtTime);
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(static_cast<float>(ReverseStartTime), EUpdatePositionMethod::Jump));
		}
		Player->PlayReverse();
	}
	else
	{
		Player->Play();
	}

	Handle.Player = Player;
	NotifyInstanceStarted(Player);
	return Handle;
}

// --------------------------------------------------------------------------------------- queue

void UDreamWidgetAnimationComponent::QueueAnimationAction(TFunction<void()> Action)
{
	UDreamWidget* HostWidget = GetWidget();
	if (!IsValid(HostWidget) || HostWidget->GetWorld() == nullptr)
	{
		Action();
		return;
	}
	UMovieSceneSequenceTickManager* TickManager = UMovieSceneSequenceTickManager::Get(HostWidget);
	if (TickManager == nullptr)
	{
		Action();
		return;
	}
	TWeakObjectPtr<UDreamWidgetAnimationComponent> WeakThis(this);
	TickManager->AddLatentAction(FMovieSceneSequenceLatentActionDelegate::CreateLambda([WeakThis, Action = MoveTemp(Action)]()
	{
		if (WeakThis.IsValid())
		{
			Action();
		}
	}));
}

void UDreamWidgetAnimationComponent::QueuePlayAnimation(UMovieSceneSequence* Animation, float StartAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	TWeakObjectPtr<UMovieSceneSequence> WeakAnimation(Animation);
	QueueAnimationAction([this, WeakAnimation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState]()
	{
		PlayAnimation(WeakAnimation.Get(), StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
	});
}

void UDreamWidgetAnimationComponent::QueuePlayAnimationTimeRange(UMovieSceneSequence* Animation, float StartAtTime, float EndAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState)
{
	TWeakObjectPtr<UMovieSceneSequence> WeakAnimation(Animation);
	QueueAnimationAction([this, WeakAnimation, StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState]()
	{
		PlayAnimationTimeRange(WeakAnimation.Get(), StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed, bRestoreState);
	});
}

void UDreamWidgetAnimationComponent::QueueStopAnimation(FDreamUIAnimationHandle Handle)
{
	TWeakObjectPtr<UDreamWidgetAnimationPlayer> WeakPlayer(Handle.Player);
	QueueAnimationAction([this, WeakPlayer]()
	{
		FDreamUIAnimationHandle Later;
		Later.Player = WeakPlayer.Get();
		StopAnimation(Later);
	});
}

void UDreamWidgetAnimationComponent::QueueStopAllAnimations()
{
	QueueAnimationAction([this]()
	{
		StopAllAnimations();
	});
}

void UDreamWidgetAnimationComponent::QueuePauseAnimation(FDreamUIAnimationHandle Handle)
{
	TWeakObjectPtr<UDreamWidgetAnimationPlayer> WeakPlayer(Handle.Player);
	QueueAnimationAction([this, WeakPlayer]()
	{
		FDreamUIAnimationHandle Later;
		Later.Player = WeakPlayer.Get();
		PauseAnimation(Later);
	});
}

// ------------------------------------------------------------------------------- one instance

float UDreamWidgetAnimationComponent::PauseAnimation(FDreamUIAnimationHandle Handle)
{
	if (!IsActiveSequencePlayer(Handle.Player))
	{
		return 0.0f;
	}
	if (Handle.Player->IsPlaying())
	{
		Handle.Player->Pause();
	}
	return GetAnimationCurrentTime(Handle);
}

void UDreamWidgetAnimationComponent::ResumeAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPaused())
	{
		if (Handle.Player->IsReversed())
		{
			Handle.Player->PlayReverse();
		}
		else
		{
			Handle.Player->Play();
		}
	}
}

void UDreamWidgetAnimationComponent::StopAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		ReleaseActiveSequencePlayer(Handle.Player);
	}
}

void UDreamWidgetAnimationComponent::ReverseAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->ChangePlaybackDirection();
	}
}

bool UDreamWidgetAnimationComponent::IsAnimationPlaying(FDreamUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPlaying();
}

bool UDreamWidgetAnimationComponent::IsAnimationPaused(FDreamUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPaused();
}

bool UDreamWidgetAnimationComponent::IsAnimationPlayingForward(FDreamUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) && !Handle.Player->IsReversed();
}

float UDreamWidgetAnimationComponent::GetAnimationCurrentTime(FDreamUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) ? static_cast<float>(Handle.Player->GetCurrentTime().AsSeconds()) : 0.0f;
}

void UDreamWidgetAnimationComponent::SetAnimationCurrentTime(FDreamUIAnimationHandle Handle, float InTime)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FMath::Max(0.0f, InTime), EUpdatePositionMethod::Jump));
	}
}

void UDreamWidgetAnimationComponent::SetNumLoopsToPlay(FDreamUIAnimationHandle Handle, int32 NumLoopsToPlay)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->SetLoopCount(NumLoopsToPlay == 0 ? -1 : FMath::Max(0, NumLoopsToPlay - 1));
	}
}

void UDreamWidgetAnimationComponent::SetPlaybackSpeed(FDreamUIAnimationHandle Handle, float PlaybackSpeed)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->SetPlayRate(FMath::Max(FMath::Abs(PlaybackSpeed), UE_SMALL_NUMBER));
	}
}

// -------------------------------------------------------------------------- by animation object

FDreamUIAnimationHandle UDreamWidgetAnimationComponent::FindAnimationInstance(UMovieSceneSequence* Animation) const
{
	FDreamUIAnimationHandle Handle;
	if (!IsValid(Animation))
	{
		return Handle;
	}
	for (int32 Index = ActiveSequencePlayers.Num() - 1; Index >= 0; --Index)
	{
		UDreamWidgetAnimationPlayer* Player = ActiveSequencePlayers[Index];
		if (IsValid(Player) && Player->GetSequence() == Animation)
		{
			Handle.Player = Player;
			break;
		}
	}
	return Handle;
}

bool UDreamWidgetAnimationComponent::HasPlayingAnimation(UMovieSceneSequence* Animation) const
{
	for (UDreamWidgetAnimationPlayer* Player : ActiveSequencePlayers)
	{
		if (IsValid(Player) && Player->GetSequence() == Animation && Player->IsPlaying())
		{
			return true;
		}
	}
	return false;
}

void UDreamWidgetAnimationComponent::StopAnimationsOf(UMovieSceneSequence* Animation)
{
	// Collected first: releasing edits the array this walks.
	TArray<UDreamWidgetAnimationPlayer*> Matching;
	for (UDreamWidgetAnimationPlayer* Player : ActiveSequencePlayers)
	{
		if (IsValid(Player) && Player->GetSequence() == Animation)
		{
			Matching.Add(Player);
		}
	}
	for (UDreamWidgetAnimationPlayer* Player : Matching)
	{
		ReleaseActiveSequencePlayer(Player);
	}
}

float UDreamWidgetAnimationComponent::PauseAnimationsOf(UMovieSceneSequence* Animation)
{
	float PausedTime = 0.0f;
	for (UDreamWidgetAnimationPlayer* Player : ActiveSequencePlayers)
	{
		if (IsValid(Player) && Player->GetSequence() == Animation)
		{
			FDreamUIAnimationHandle Handle;
			Handle.Player = Player;
			PausedTime = PauseAnimation(Handle);
		}
	}
	return PausedTime;
}

bool UDreamWidgetAnimationComponent::IsAnyAnimationPlaying() const
{
	if (SequencePlayer && SequencePlayer->IsPlaying())
	{
		return true;
	}
	for (UDreamWidgetAnimationPlayer* Player : ActiveSequencePlayers)
	{
		if (IsValid(Player) && Player->IsPlaying())
		{
			return true;
		}
	}
	return false;
}

void UDreamWidgetAnimationComponent::StopAllAnimations()
{
	if (SequencePlayer && SequencePlayer->IsPlaying())
	{
		SequencePlayer->Stop();
	}

	TArray<TObjectPtr<UDreamWidgetAnimationPlayer>> Players = MoveTemp(ActiveSequencePlayers);
	ActiveSequencePlayers.Reset();
	for (UDreamWidgetAnimationPlayer* Player : Players)
	{
		if (IsValid(Player))
		{
			Player->OnNativeFinished.Unbind();
			Player->Stop();
			Player->TearDown();
			NotifyInstanceFinished(Player);
		}
	}
}

void UDreamWidgetAnimationComponent::FlushAnimations()
{
	for (UDreamWidgetAnimationPlayer* Player : ActiveSequencePlayers)
	{
		if (IsValid(Player))
		{
			Player->FlushQueuedEvaluation();
		}
	}
}

// ---------------------------------------------------------------------------------- bookkeeping

void UDreamWidgetAnimationComponent::HandleActiveSequencePlayerFinished(UDreamWidgetAnimationPlayer* Player)
{
	// bPauseAtEnd intentionally leaves the handle alive so callers can resume or stop it.
	if (IsValid(Player) && !Player->IsPaused())
	{
		// Natural completion has already stopped and finalized the player. Calling Stop again
		// would queue a second final update through UMovieSceneSequencePlayer::StopInternal.
		ReleaseActiveSequencePlayer(Player, false);
	}
}

bool UDreamWidgetAnimationComponent::IsActiveSequencePlayer(const UDreamWidgetAnimationPlayer* Player) const
{
	return IsValid(Player) && ActiveSequencePlayers.Contains(Player);
}

void UDreamWidgetAnimationComponent::ReleaseActiveSequencePlayer(UDreamWidgetAnimationPlayer* Player, bool bStopPlayer)
{
	if (!IsValid(Player))
	{
		return;
	}

	Player->OnNativeFinished.Unbind();
	if (bStopPlayer)
	{
		Player->Stop();
	}
	Player->TearDown();
	// Out of the list before anyone hears about it, so a Finished listener asking whether the
	// instance still plays gets the answer it expects.
	ActiveSequencePlayers.RemoveSingleSwap(Player);
	NotifyInstanceFinished(Player);
}

void UDreamWidgetAnimationComponent::NotifyInstanceStarted(UDreamWidgetAnimationPlayer* Player)
{
	FDreamUIAnimationHandle Handle;
	Handle.Player = Player;
	UMovieSceneSequence* Animation = Player->GetSequence();

	OnInstanceStarted.Broadcast(Handle);
	OnAnimationStarted.Broadcast(Animation);
	ExecuteBoundAnimationEvents(Animation, EDreamUIAnimationEvent::Started);
	if (UDreamUserWidget* UserWidget = GetOwningUserWidget())
	{
		UserWidget->NotifyAnimationStarted(Animation);
	}
}

void UDreamWidgetAnimationComponent::NotifyInstanceFinished(UDreamWidgetAnimationPlayer* Player)
{
	FDreamUIAnimationHandle Handle;
	Handle.Player = Player;
	UMovieSceneSequence* Animation = Player->GetSequence();

	OnInstanceFinished.Broadcast(Handle);
	OnAnimationFinished.Broadcast(Animation);
	ExecuteBoundAnimationEvents(Animation, EDreamUIAnimationEvent::Finished);
	if (UDreamUserWidget* UserWidget = GetOwningUserWidget())
	{
		UserWidget->NotifyAnimationFinished(Animation);
	}
}

void UDreamWidgetAnimationComponent::ExecuteBoundAnimationEvents(UMovieSceneSequence* Animation, EDreamUIAnimationEvent Event)
{
	// A copy: a listener commonly unbinds itself from inside the call.
	const TArray<FDreamUIAnimationEventBinding> Callbacks = AnimationCallbacks;
	for (const FDreamUIAnimationEventBinding& Binding : Callbacks)
	{
		if (Binding.Animation == Animation && Binding.Event == Event)
		{
			Binding.Delegate.ExecuteIfBound();
		}
	}
}

void UDreamWidgetAnimationComponent::UnbindAnimationEvent(UMovieSceneSequence* Animation, EDreamUIAnimationEvent Event, const FDreamUIAnimationDynamicEvent* Delegate)
{
	AnimationCallbacks.RemoveAll([Animation, Event, Delegate](const FDreamUIAnimationEventBinding& Binding)
	{
		return Binding.Animation == Animation && Binding.Event == Event && (Delegate == nullptr || Binding.Delegate == *Delegate);
	});
}

void UDreamWidgetAnimationComponent::BindToAnimationEvent(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate, EDreamUIAnimationEvent AnimationEvent)
{
	if (!IsValid(Animation))
	{
		return;
	}
	FDreamUIAnimationEventBinding Binding;
	Binding.Animation = Animation;
	Binding.Delegate = Delegate;
	Binding.Event = AnimationEvent;
	AnimationCallbacks.Add(MoveTemp(Binding));
}

void UDreamWidgetAnimationComponent::BindToAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	BindToAnimationEvent(Animation, Delegate, EDreamUIAnimationEvent::Started);
}

void UDreamWidgetAnimationComponent::UnbindFromAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	UnbindAnimationEvent(Animation, EDreamUIAnimationEvent::Started, &Delegate);
}

void UDreamWidgetAnimationComponent::UnbindAllFromAnimationStarted(UMovieSceneSequence* Animation)
{
	UnbindAnimationEvent(Animation, EDreamUIAnimationEvent::Started, nullptr);
}

void UDreamWidgetAnimationComponent::BindToAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	BindToAnimationEvent(Animation, Delegate, EDreamUIAnimationEvent::Finished);
}

void UDreamWidgetAnimationComponent::UnbindFromAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate)
{
	UnbindAnimationEvent(Animation, EDreamUIAnimationEvent::Finished, &Delegate);
}

void UDreamWidgetAnimationComponent::UnbindAllFromAnimationFinished(UMovieSceneSequence* Animation)
{
	UnbindAnimationEvent(Animation, EDreamUIAnimationEvent::Finished, nullptr);
}

void UDreamWidgetAnimationComponent::BroadcastAnimationEvent(FName EventName)
{
	OnAnimationEvent.Broadcast(EventName);
	if (UDreamUserWidget* UserWidget = GetOwningUserWidget())
	{
		UserWidget->NotifyAnimationEvent(EventName);
	}
}

UDreamUserWidget* UDreamWidgetAnimationComponent::GetOwningUserWidget() const
{
	UDreamWidget* HostWidget = GetWidget();
	if (!IsValid(HostWidget))
	{
		return nullptr;
	}
	// The component may sit on the user widget itself, which its outer chain does not include.
	if (UDreamUserWidget* Self = Cast<UDreamUserWidget>(HostWidget))
	{
		return Self;
	}
	return HostWidget->GetTypedOuter<UDreamUserWidget>();
}

#if WITH_EDITOR
void UDreamWidgetAnimationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

}
void UDreamWidgetAnimationComponent::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
	FixEditorHelpers();
}
#include "UObject/ObjectSaveContext.h"
void UDreamWidgetAnimationComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	FixEditorHelpers();
}
void UDreamWidgetAnimationComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
}
void UDreamWidgetAnimationComponent::PostInitProperties()
{
	Super::PostInitProperties();
}
void UDreamWidgetAnimationComponent::PostLoad()
{
	Super::PostLoad();
}
/**
 * Re-derive every binding's editor-only widget path from the object it currently resolves to.
 *
 * That path is spelled out of widget display names, so a rename leaves it pointing at a widget that
 * no longer exists -- and it is the only thing "Try fix object reference" has to match on once the
 * direct pointer stops resolving. Rebuilding it here, while the pointer still works, is what makes a
 * later repair possible at all; leave it until the reference has already broken and there is nothing
 * to rebuild it from.
 */
void UDreamWidgetAnimationComponent::FixEditorHelpers()
{
	UDreamWidget* ContextWidget = GetWidget();
	if (!IsValid(ContextWidget))
	{
		return;
	}
	for (UDreamWidgetAnimation* Sequence : SequenceArray)
	{
		if (IsValid(Sequence))
		{
			Sequence->FixEditorHelpers(ContextWidget);
		}
	}
}

#endif

UDreamWidgetAnimation* UDreamWidgetAnimationComponent::GetSequenceByDisplayName(const FString& InName) const
{
	for (auto Item : SequenceArray)
	{
		if (IsValid(Item) && Item->GetDisplayNameString() == InName)
		{
			return Item;
		}
	}
	return nullptr;
}
UDreamWidgetAnimation* UDreamWidgetAnimationComponent::GetSequenceByIndex(int32 InIndex) const
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return nullptr;
	}
	return SequenceArray[InIndex];
}

void UDreamWidgetAnimationComponent::InitSequencePlayer()
{
	if (!SequencePlayer)
	{
		SequencePlayer = NewObject<UDreamWidgetAnimationPlayer>(this, "SequencePlayer");

		// Initialize this player for tick as soon as possible to ensure that a persistent
		// reference to the tick manager is maintained
		SequencePlayer->InitializeForTick(this);
	}
	if (SequenceArray.IsValidIndex(CurrentSequenceIndex))
	{
		DreamUI::EnsureMovieScenePropertyAccessorsRegistered();
		SequencePlayer->Initialize(SequenceArray[CurrentSequenceIndex], PlaybackSettings);
	}
}
void UDreamWidgetAnimationComponent::SetSequenceByIndex(int32 InIndex)
{
	CurrentSequenceIndex = InIndex;
	InitSequencePlayer();
}

void UDreamWidgetAnimationComponent::SetSequenceByDisplayName(const FString& InName)
{
	int FoundIndex = -1;
	FoundIndex = SequenceArray.IndexOfByPredicate([InName](const UDreamWidgetAnimation* Item) {
		return Item->GetDisplayNameString() == InName;
		});
	if (FoundIndex != INDEX_NONE)
	{
		CurrentSequenceIndex = FoundIndex;
		InitSequencePlayer();
	}
}

UDreamWidgetAnimation* UDreamWidgetAnimationComponent::AddNewAnimation()
{
	auto NewSequence = NewObject<UDreamWidgetAnimation>(this, NAME_None, RF_Public | RF_Transactional);
	auto MovieScene = NewSequence->GetMovieScene();
	SequenceArray.Add(NewSequence);
	return NewSequence;
}

bool UDreamWidgetAnimationComponent::DeleteAnimationByIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return false;
	}
	SequenceArray.RemoveAt(InIndex);
	CurrentSequenceIndex = SequenceArray.IsEmpty() ? 0 : FMath::Clamp(CurrentSequenceIndex, 0, SequenceArray.Num() - 1);
	return true;
}
UDreamWidgetAnimation* UDreamWidgetAnimationComponent::DuplicateAnimationByIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return nullptr;
	}
	auto SourceSequence = SequenceArray[InIndex];
	auto NewSequence = DuplicateObject(SourceSequence, this);
	NewSequence->SetDisplayNameString(NewSequence->GetName());
	{
		NewSequence->GetMovieScene()->SetTickResolutionDirectly(SourceSequence->GetMovieScene()->GetTickResolution());
		NewSequence->GetMovieScene()->SetPlaybackRange(SourceSequence->GetMovieScene()->GetPlaybackRange());
		NewSequence->GetMovieScene()->SetDisplayRate(SourceSequence->GetMovieScene()->GetDisplayRate());
	}
	SequenceArray.Insert(NewSequence, InIndex + 1);
	return NewSequence;
}
