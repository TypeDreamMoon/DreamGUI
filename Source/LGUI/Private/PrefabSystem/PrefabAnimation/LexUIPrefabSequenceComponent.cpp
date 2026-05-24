// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequencePlayer.h"
#include "LGUI.h"
#include "PrefabSystem/LexUIPrefabManager.h"

ULexUIPrefabSequenceComponent::ULexUIPrefabSequenceComponent()
{
}

void ULexUIPrefabSequenceComponent::BeginPlay()
{
	Super::BeginPlay();
	InitSequencePlayer();

	if (PlaybackSettings.bAutoPlay)
	{
		SequencePlayer->Play();
	}
}

void ULexUIPrefabSequenceComponent::EndPlay()
{
	Super::EndPlay();

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer->TearDown();
	}
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
void ULexUIPrefabSequenceComponent::FixEditorHelpers()
{
	// for (auto& Sequence : SequenceArray)
	// {
	// 	if (Sequence->IsObjectReferencesGood(GetOwner()) && !Sequence->IsEditorHelpersGood(this->GetOwner()))
	// 	{
	// 		Sequence->FixEditorHelpers(this->GetOwner());
	// 	}
	// }
}

#endif

ULexUIPrefabSequence* ULexUIPrefabSequenceComponent::GetSequenceByDisplayName(const FString& InName) const
{
	for (auto Item : SequenceArray)
	{
		if (Item->GetDisplayNameString() == InName)
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
	if (auto CurrentSequence = GetCurrentSequence())
	{
		SequencePlayer->Initialize(CurrentSequence, PlaybackSettings);
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
	if (auto SequenceItem = SequenceArray[InIndex])
	{
		SequenceItem->ConditionalBeginDestroy();
	}
	SequenceArray.RemoveAt(InIndex);
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
