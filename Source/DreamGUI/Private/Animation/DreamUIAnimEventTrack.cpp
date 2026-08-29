// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Evaluation/MovieScenePlayback.h"
#include "MovieSceneCommonHelpers.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneSection.h"

#define LOCTEXT_NAMESPACE "DreamUIAnimEventTrack"

UDreamUIAnimEventSection::UDreamUIAnimEventSection(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SetRange(TRange<FFrameNumber>::All());
	bRequiresTriggerHooks = true;
}

EMovieSceneChannelProxyType UDreamUIAnimEventSection::CacheChannelProxy()
{
#if WITH_EDITOR
	FMovieSceneChannelMetaData MetaData;
	MetaData.SetIdentifiers("Event", LOCTEXT("EventChannelName", "Event"));
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(EventChannel, MetaData, TMovieSceneExternalValue<FString>::Make());
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(EventChannel);
#endif
	return EMovieSceneChannelProxyType::Static;
}

void UDreamUIAnimEventSection::Trigger(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const
{
	TMovieSceneChannelData<const FString> ChannelData = EventChannel.GetData();
	if (!ensureMsgf(ChannelData.GetValues().IsValidIndex(Params.TriggerIndex),
		TEXT("Invalid trigger index specified: %d (Num triggers in channel = %d)"), Params.TriggerIndex, ChannelData.GetValues().Num()))
	{
		return;
	}
	// Real playback only: a scrub or a jump crossing the key is inspection, not the animation
	// happening. Same gate the engine's own trigger sections use.
	if (Params.Context.GetStatus() != EMovieScenePlayerStatus::Playing || Params.Context.IsSilent())
	{
		return;
	}
	const FString& EventName = ChannelData.GetValues()[Params.TriggerIndex];
	if (EventName.IsEmpty())
	{
		return;
	}

	// Embedded in a component: the section's outer chain ends at it. The asset form has no such
	// outer, so fall back to the playback context, which is the hosting widget.
	UDreamWidgetAnimationComponent* Component = GetTypedOuter<UDreamWidgetAnimationComponent>();
	if (Component == nullptr)
	{
		if (const UDreamWidget* ContextWidget = Cast<UDreamWidget>(SharedPlaybackState->GetPlaybackContext()))
		{
			Component = ContextWidget->GetComponent<UDreamWidgetAnimationComponent>();
		}
	}
	if (Component != nullptr)
	{
		Component->BroadcastAnimationEvent(FName(*EventName));
	}
}

UDreamUIAnimEventTrack::UDreamUIAnimEventTrack(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(41, 98, 255, 65);
#endif
}

void UDreamUIAnimEventTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}

bool UDreamUIAnimEventTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UDreamUIAnimEventSection::StaticClass();
}

UMovieSceneSection* UDreamUIAnimEventTrack::CreateNewSection()
{
	return NewObject<UDreamUIAnimEventSection>(this, NAME_None, RF_Transactional);
}

const TArray<UMovieSceneSection*>& UDreamUIAnimEventTrack::GetAllSections() const
{
	return reinterpret_cast<const TArray<UMovieSceneSection*>&>(Sections);
}

bool UDreamUIAnimEventTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}

bool UDreamUIAnimEventTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UDreamUIAnimEventTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UDreamUIAnimEventTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}

#if WITH_EDITORONLY_DATA
FText UDreamUIAnimEventTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Events");
}
#endif

#undef LOCTEXT_NAMESPACE
