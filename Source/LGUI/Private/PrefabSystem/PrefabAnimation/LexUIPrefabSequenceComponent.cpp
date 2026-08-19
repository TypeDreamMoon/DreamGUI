// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequencePlayer.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"

#include "MovieSceneSequencePlaybackSettings.h"

ULexUIPrefabSequenceComponent::ULexUIPrefabSequenceComponent()
{
}

bool FLexUIAnimationHandle::IsValid() const
{
	return ::IsValid(Player.Get());
}

void ULexUIPrefabSequenceComponent::Awake()
{
	Super::Awake();
	InitSequencePlayer();

	if (PlaybackSettings.bAutoPlay && SequencePlayer && SequencePlayer->IsValid())
	{
		SequencePlayer->Play();
	}
}

void ULexUIPrefabSequenceComponent::OnDestroy()
{
	Super::OnDestroy();
	StopAllAnimations();

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer->TearDown();
	}
}

FLexUIAnimationHandle ULexUIPrefabSequenceComponent::PlayAnimationByDisplayName(
	const FString& Name,
	float StartAtTime,
	int32 NumLoopsToPlay,
	ELexUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	FLexUIAnimationHandle Handle;
	ULexUIPrefabSequence* Sequence = GetSequenceByDisplayName(Name);
	if (!IsValid(Sequence))
	{
		UE_LOG(LGUI, Warning, TEXT("Animation '%s' was not found on '%s'."), *Name, *GetPathName());
		return Handle;
	}

	FMovieSceneSequencePlaybackSettings Settings = PlaybackSettings;
	Settings.bAutoPlay = false;
	Settings.bRandomStartTime = false;
	Settings.StartTime = PlayMode == ELexUIAnimationPlayMode::Forward ? FMath::Max(0.0f, StartAtTime) : 0.0f;
	Settings.PlayRate = FMath::Max(FMath::Abs(PlaybackSpeed), UE_SMALL_NUMBER);
	Settings.LoopCount.Value = NumLoopsToPlay == 0 ? -1 : FMath::Max(0, NumLoopsToPlay - 1);
	Settings.FinishCompletionStateOverride = bRestoreState
		? EMovieSceneCompletionModeOverride::ForceRestoreState
		: EMovieSceneCompletionModeOverride::ForceKeepState;

	ULexUIPrefabSequencePlayer* Player = NewObject<ULexUIPrefabSequencePlayer>(this);
	Player->SetPlaybackClient(this);
	Player->InitializeForTick(this);
	Player->Initialize(Sequence, Settings);
	Player->OnNativeFinished.BindUObject(this, &ULexUIPrefabSequenceComponent::HandleActiveSequencePlayerFinished, Player);
	ActiveSequencePlayers.Add(Player);

	if (PlayMode == ELexUIAnimationPlayMode::Reverse)
	{
		if (StartAtTime > 0.0f)
		{
			const double ReverseStartTime = FMath::Max(0.0, Player->GetEndTime().AsSeconds() - StartAtTime);
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(ReverseStartTime, EUpdatePositionMethod::Jump));
		}
		Player->PlayReverse();
	}
	else
	{
		Player->Play();
	}

	Handle.Player = Player;
	return Handle;
}

void ULexUIPrefabSequenceComponent::PauseAnimation(FLexUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPlaying())
	{
		Handle.Player->Pause();
	}
}

void ULexUIPrefabSequenceComponent::StopAnimation(FLexUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		ReleaseActiveSequencePlayer(Handle.Player);
	}
}

void ULexUIPrefabSequenceComponent::ReverseAnimation(FLexUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->ChangePlaybackDirection();
	}
}

bool ULexUIPrefabSequenceComponent::IsAnimationPlaying(FLexUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPlaying();
}

void ULexUIPrefabSequenceComponent::StopAllAnimations()
{
	if (SequencePlayer && SequencePlayer->IsPlaying())
	{
		SequencePlayer->Stop();
	}

	TArray<TObjectPtr<ULexUIPrefabSequencePlayer>> Players = MoveTemp(ActiveSequencePlayers);
	ActiveSequencePlayers.Reset();
	for (ULexUIPrefabSequencePlayer* Player : Players)
	{
		if (IsValid(Player))
		{
			Player->OnNativeFinished.Unbind();
			Player->Stop();
			Player->TearDown();
		}
	}
}

void ULexUIPrefabSequenceComponent::HandleActiveSequencePlayerFinished(ULexUIPrefabSequencePlayer* Player)
{
	// bPauseAtEnd intentionally leaves the handle alive so callers can resume or stop it.
	if (IsValid(Player) && !Player->IsPaused())
	{
		// Natural completion has already stopped and finalized the player. Calling Stop again
		// would queue a second final update through UMovieSceneSequencePlayer::StopInternal.
		ReleaseActiveSequencePlayer(Player, false);
	}
}

bool ULexUIPrefabSequenceComponent::IsActiveSequencePlayer(const ULexUIPrefabSequencePlayer* Player) const
{
	return IsValid(Player) && ActiveSequencePlayers.Contains(Player);
}

void ULexUIPrefabSequenceComponent::ReleaseActiveSequencePlayer(ULexUIPrefabSequencePlayer* Player, bool bStopPlayer)
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
	ActiveSequencePlayers.RemoveSingleSwap(Player);
}

#if WITH_EDITOR
void ULexUIPrefabSequenceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

}
void ULexUIPrefabSequenceComponent::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
	FixEditorHelpers();
}
#include "UObject/ObjectSaveContext.h"
void ULexUIPrefabSequenceComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	FixEditorHelpers();
}
void ULexUIPrefabSequenceComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
}
void ULexUIPrefabSequenceComponent::PostInitProperties()
{
	Super::PostInitProperties();
}
void ULexUIPrefabSequenceComponent::PostLoad()
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
void ULexUIPrefabSequenceComponent::FixEditorHelpers()
{
	ULexWidget* ContextWidget = GetWidget();
	if (!IsValid(ContextWidget))
	{
		return;
	}
	for (ULexUIPrefabSequence* Sequence : SequenceArray)
	{
		if (IsValid(Sequence))
		{
			Sequence->FixEditorHelpers(ContextWidget);
		}
	}
}

#endif

ULexUIPrefabSequence* ULexUIPrefabSequenceComponent::GetSequenceByDisplayName(const FString& InName) const
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
ULexUIPrefabSequence* ULexUIPrefabSequenceComponent::GetSequenceByIndex(int32 InIndex) const
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return nullptr;
	}
	return SequenceArray[InIndex];
}

void ULexUIPrefabSequenceComponent::InitSequencePlayer()
{
	if (!SequencePlayer)
	{
		SequencePlayer = NewObject<ULexUIPrefabSequencePlayer>(this, "SequencePlayer");
		SequencePlayer->SetPlaybackClient(this);

		// Initialize this player for tick as soon as possible to ensure that a persistent
		// reference to the tick manager is maintained
		SequencePlayer->InitializeForTick(this);
	}
	if (SequenceArray.IsValidIndex(CurrentSequenceIndex))
	{
		SequencePlayer->Initialize(SequenceArray[CurrentSequenceIndex], PlaybackSettings);
	}
}
void ULexUIPrefabSequenceComponent::SetSequenceByIndex(int32 InIndex)
{
	CurrentSequenceIndex = InIndex;
	InitSequencePlayer();
}

void ULexUIPrefabSequenceComponent::SetSequenceByDisplayName(const FString& InName)
{
	int FoundIndex = -1;
	FoundIndex = SequenceArray.IndexOfByPredicate([InName](const ULexUIPrefabSequence* Item) {
		return Item->GetDisplayNameString() == InName;
		});
	if (FoundIndex != INDEX_NONE)
	{
		CurrentSequenceIndex = FoundIndex;
		InitSequencePlayer();
	}
}

ULexUIPrefabSequence* ULexUIPrefabSequenceComponent::AddNewAnimation()
{
	auto NewSequence = NewObject<ULexUIPrefabSequence>(this, NAME_None, RF_Public | RF_Transactional);
	auto MovieScene = NewSequence->GetMovieScene();
	SequenceArray.Add(NewSequence);
	return NewSequence;
}

bool ULexUIPrefabSequenceComponent::DeleteAnimationByIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return false;
	}
	SequenceArray.RemoveAt(InIndex);
	CurrentSequenceIndex = SequenceArray.IsEmpty() ? 0 : FMath::Clamp(CurrentSequenceIndex, 0, SequenceArray.Num() - 1);
	return true;
}
ULexUIPrefabSequence* ULexUIPrefabSequenceComponent::DuplicateAnimationByIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
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
