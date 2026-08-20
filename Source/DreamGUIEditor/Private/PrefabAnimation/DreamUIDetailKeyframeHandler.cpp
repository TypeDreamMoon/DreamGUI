// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIDetailKeyframeHandler.h"
#include "ISequencer.h"
#include "DreamUIPrefabSequenceEditor.h"
#include "MovieScene.h"
#include "Animation/WidgetAnimation.h"
#include "PrefabEditor/DreamUIPrefabEditor.h"


FDreamUIDetailKeyframeHandler::FDreamUIDetailKeyframeHandler(TSharedPtr<FDreamUIPrefabEditor> InSequenceEditor)
	: PrefabEditor( InSequenceEditor )
{}

bool FDreamUIDetailKeyframeHandler::IsPropertyKeyable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (auto Sequencer = GetSequencer())
	{
		return Sequencer->CanKeyProperty(FCanKeyPropertyParams(InObjectClass, InPropertyHandle));
	}
	return false;
}

bool FDreamUIDetailKeyframeHandler::IsPropertyKeyingEnabled() const
{
	if (auto Sequencer = GetSequencer())
	{
		UMovieSceneSequence* Sequence = Sequencer->GetRootMovieSceneSequence();
		return Sequence != nullptr && Sequence != UWidgetAnimation::GetNullAnimation();
	}
	return false;
}

bool FDreamUIDetailKeyframeHandler::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
{
	if (auto Sequencer = GetSequencer())
	{
		return Sequencer->GetPropertyKeyedStatus(PropertyHandle) != EPropertyKeyedStatus::NotKeyed;
	}
	return false;
}

EPropertyKeyedStatus FDreamUIDetailKeyframeHandler::GetPropertyKeyedStatus(const IPropertyHandle& PropertyHandle) const
{
	if (auto Sequencer = GetSequencer())
	{
		return Sequencer->GetPropertyKeyedStatus(PropertyHandle);
	}
	return EPropertyKeyedStatus::NotKeyed;
}

TSharedPtr<ISequencer> FDreamUIDetailKeyframeHandler::GetSequencer() const
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

void FDreamUIDetailKeyframeHandler::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	if (auto Sequencer = GetSequencer())
	{
		TArray<UObject*> Objects;
		KeyedPropertyHandle.GetOuterObjects( Objects );

		FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

		Sequencer->KeyProperty(KeyPropertyParams);
	}
}
