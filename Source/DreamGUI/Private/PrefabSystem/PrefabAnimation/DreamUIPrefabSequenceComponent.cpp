// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequencePlayer.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"

#include "MovieSceneSequencePlaybackSettings.h"

UDreamUIPrefabSequenceComponent::UDreamUIPrefabSequenceComponent()
{
}

bool FDreamUIAnimationHandle::IsValid() const
{
	return ::IsValid(Player.Get());
}

void UDreamUIPrefabSequenceComponent::Awake()
{
	Super::Awake();
	InitSequencePlayer();

	if (PlaybackSettings.bAutoPlay && SequencePlayer && SequencePlayer->IsValid())
	{
		SequencePlayer->Play();
	}
}

void UDreamUIPrefabSequenceComponent::OnDestroy()
{
	Super::OnDestroy();
	StopAllAnimations();

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer->TearDown();
	}
}

FDreamUIAnimationHandle UDreamUIPrefabSequenceComponent::PlayAnimationByDisplayName(
	const FString& Name,
	float StartAtTime,
	int32 NumLoopsToPlay,
	EDreamUIAnimationPlayMode PlayMode,
	float PlaybackSpeed,
	bool bRestoreState)
{
	FDreamUIAnimationHandle Handle;
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
		return Handle;
	}

	FMovieSceneSequencePlaybackSettings Settings = PlaybackSettings;
	Settings.bAutoPlay = false;
	Settings.bRandomStartTime = false;
	Settings.StartTime = PlayMode == EDreamUIAnimationPlayMode::Forward ? FMath::Max(0.0f, StartAtTime) : 0.0f;
	Settings.PlayRate = FMath::Max(FMath::Abs(PlaybackSpeed), UE_SMALL_NUMBER);
	Settings.LoopCount.Value = NumLoopsToPlay == 0 ? -1 : FMath::Max(0, NumLoopsToPlay - 1);
	Settings.FinishCompletionStateOverride = bRestoreState
		? EMovieSceneCompletionModeOverride::ForceRestoreState
		: EMovieSceneCompletionModeOverride::ForceKeepState;

	UDreamUIPrefabSequencePlayer* Player = NewObject<UDreamUIPrefabSequencePlayer>(this);
	Player->SetPlaybackClient(this);
	Player->InitializeForTick(this);
	Player->Initialize(Sequence, Settings);
	Player->OnNativeFinished.BindUObject(this, &UDreamUIPrefabSequenceComponent::HandleActiveSequencePlayerFinished, Player);
	ActiveSequencePlayers.Add(Player);

	if (PlayMode == EDreamUIAnimationPlayMode::Reverse)
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

void UDreamUIPrefabSequenceComponent::PauseAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPlaying())
	{
		Handle.Player->Pause();
	}
}

void UDreamUIPrefabSequenceComponent::StopAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		ReleaseActiveSequencePlayer(Handle.Player);
	}
}

void UDreamUIPrefabSequenceComponent::ReverseAnimation(FDreamUIAnimationHandle Handle)
{
	if (IsActiveSequencePlayer(Handle.Player))
	{
		Handle.Player->ChangePlaybackDirection();
	}
}

bool UDreamUIPrefabSequenceComponent::IsAnimationPlaying(FDreamUIAnimationHandle Handle) const
{
	return IsActiveSequencePlayer(Handle.Player) && Handle.Player->IsPlaying();
}

void UDreamUIPrefabSequenceComponent::StopAllAnimations()
{
	if (SequencePlayer && SequencePlayer->IsPlaying())
	{
		SequencePlayer->Stop();
	}

	TArray<TObjectPtr<UDreamUIPrefabSequencePlayer>> Players = MoveTemp(ActiveSequencePlayers);
	ActiveSequencePlayers.Reset();
	for (UDreamUIPrefabSequencePlayer* Player : Players)
	{
		if (IsValid(Player))
		{
			Player->OnNativeFinished.Unbind();
			Player->Stop();
			Player->TearDown();
		}
	}
}

void UDreamUIPrefabSequenceComponent::HandleActiveSequencePlayerFinished(UDreamUIPrefabSequencePlayer* Player)
{
	// bPauseAtEnd intentionally leaves the handle alive so callers can resume or stop it.
	if (IsValid(Player) && !Player->IsPaused())
	{
		// Natural completion has already stopped and finalized the player. Calling Stop again
		// would queue a second final update through UMovieSceneSequencePlayer::StopInternal.
		ReleaseActiveSequencePlayer(Player, false);
	}
}

bool UDreamUIPrefabSequenceComponent::IsActiveSequencePlayer(const UDreamUIPrefabSequencePlayer* Player) const
{
	return IsValid(Player) && ActiveSequencePlayers.Contains(Player);
}

void UDreamUIPrefabSequenceComponent::ReleaseActiveSequencePlayer(UDreamUIPrefabSequencePlayer* Player, bool bStopPlayer)
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
void UDreamUIPrefabSequenceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

}
void UDreamUIPrefabSequenceComponent::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	Super::PreDuplicate(DupParams);
	FixEditorHelpers();
}
#include "UObject/ObjectSaveContext.h"
void UDreamUIPrefabSequenceComponent::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
	FixEditorHelpers();
}
void UDreamUIPrefabSequenceComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
}
void UDreamUIPrefabSequenceComponent::PostInitProperties()
{
	Super::PostInitProperties();
}
void UDreamUIPrefabSequenceComponent::PostLoad()
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
void UDreamUIPrefabSequenceComponent::FixEditorHelpers()
{
	UDreamWidget* ContextWidget = GetWidget();
	if (!IsValid(ContextWidget))
	{
		return;
	}
	for (UDreamUIPrefabSequence* Sequence : SequenceArray)
	{
		if (IsValid(Sequence))
		{
			Sequence->FixEditorHelpers(ContextWidget);
		}
	}
}

#endif

UDreamUIPrefabSequence* UDreamUIPrefabSequenceComponent::GetSequenceByDisplayName(const FString& InName) const
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
UDreamUIPrefabSequence* UDreamUIPrefabSequenceComponent::GetSequenceByIndex(int32 InIndex) const
{
	if (InIndex < 0 || InIndex >= SequenceArray.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Index out of range! Index: %d, ArrayNum: %d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InIndex, SequenceArray.Num());
		return nullptr;
	}
	return SequenceArray[InIndex];
}

void UDreamUIPrefabSequenceComponent::InitSequencePlayer()
{
	if (!SequencePlayer)
	{
		SequencePlayer = NewObject<UDreamUIPrefabSequencePlayer>(this, "SequencePlayer");
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
void UDreamUIPrefabSequenceComponent::SetSequenceByIndex(int32 InIndex)
{
	CurrentSequenceIndex = InIndex;
	InitSequencePlayer();
}

void UDreamUIPrefabSequenceComponent::SetSequenceByDisplayName(const FString& InName)
{
	int FoundIndex = -1;
	FoundIndex = SequenceArray.IndexOfByPredicate([InName](const UDreamUIPrefabSequence* Item) {
		return Item->GetDisplayNameString() == InName;
		});
	if (FoundIndex != INDEX_NONE)
	{
		CurrentSequenceIndex = FoundIndex;
		InitSequencePlayer();
	}
}

UDreamUIPrefabSequence* UDreamUIPrefabSequenceComponent::AddNewAnimation()
{
	auto NewSequence = NewObject<UDreamUIPrefabSequence>(this, NAME_None, RF_Public | RF_Transactional);
	auto MovieScene = NewSequence->GetMovieScene();
	SequenceArray.Add(NewSequence);
	return NewSequence;
}

bool UDreamUIPrefabSequenceComponent::DeleteAnimationByIndex(int32 InIndex)
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
UDreamUIPrefabSequence* UDreamUIPrefabSequenceComponent::DuplicateAnimationByIndex(int32 InIndex)
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
