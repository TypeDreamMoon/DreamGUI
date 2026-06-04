// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIDetailKeyframeHandler.h"
#include "ISequencer.h"
#include "LexUIPrefabSequenceEditor.h"
#include "MovieScene.h"
#include "Animation/WidgetAnimation.h"
#include "PrefabEditor/LexUIPrefabEditor.h"


FLexUIDetailKeyframeHandler::FLexUIDetailKeyframeHandler(TSharedPtr<FLexUIPrefabEditor> InSequenceEditor)
	: PrefabEditor( InSequenceEditor )
{}

bool FLexUIDetailKeyframeHandler::IsPropertyKeyable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (auto Sequencer = GetSequencer())
	{
		return Sequencer->CanKeyProperty(FCanKeyPropertyParams(InObjectClass, InPropertyHandle));
	}
	return false;
}

bool FLexUIDetailKeyframeHandler::IsPropertyKeyingEnabled() const
{
	if (auto Sequencer = GetSequencer())
	{
		UMovieSceneSequence* Sequence = Sequencer->GetRootMovieSceneSequence();
		return Sequence != nullptr && Sequence != UWidgetAnimation::GetNullAnimation();
	}
	return false;
}

bool FLexUIDetailKeyframeHandler::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
{
	if (auto Sequencer = GetSequencer())
	{
		if (Sequencer->GetFocusedMovieSceneSequence())
		{
			constexpr bool bCreateHandleIfMissing = false;
			FGuid ObjectHandle = Sequencer->GetHandleToObject(ParentObject, bCreateHandleIfMissing);
			if (ObjectHandle.IsValid()) 
			{
				UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
				FProperty* Property = PropertyHandle.GetProperty();
				TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
				PropertyPath->AddProperty(FPropertyInfo(Property));
				FName PropertyName(*PropertyPath->ToString(TEXT(".")));
				TSubclassOf<UMovieSceneTrack> TrackClass; //use empty @todo find way to get the UMovieSceneTrack from the Property type.
				return MovieScene->FindTrack(TrackClass, ObjectHandle, PropertyName) != nullptr;
			}
		}
	}
	return false;
}

EPropertyKeyedStatus FLexUIDetailKeyframeHandler::GetPropertyKeyedStatus(const IPropertyHandle& PropertyHandle) const
{
	if (auto Sequencer = GetSequencer())
	{
		return Sequencer->GetPropertyKeyedStatus(PropertyHandle);
	}
	return EPropertyKeyedStatus::NotKeyed;
}

TSharedPtr<ISequencer> FLexUIDetailKeyframeHandler::GetSequencer() const
{
	if (PrefabEditor.IsValid())
	{
		auto SequencerEditor = PrefabEditor.Pin()->GetSequencerEditor();
		if (SequencerEditor.IsValid())
		{
			return SequencerEditor->GetSequencer();
		}
	}
	return TSharedPtr<ISequencer>(nullptr);
}

void FLexUIDetailKeyframeHandler::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	if (auto Sequencer = GetSequencer())
	{
		TArray<UObject*> Objects;
		KeyedPropertyHandle.GetOuterObjects( Objects );

		FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

		Sequencer->KeyProperty(KeyPropertyParams);
	}
}
