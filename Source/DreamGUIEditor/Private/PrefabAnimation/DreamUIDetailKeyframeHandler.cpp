// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIDetailKeyframeHandler.h"
#include "ISequencer.h"
#include "DreamUIPrefabSequenceEditor.h"
#include "MovieScene.h"
#include "PrefabEditor/DreamUIPrefabEditor.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Core/Components/DreamWidget.h"

//declared in DreamUIPrefabSequenceEditorWidget.cpp; the comment there says why it is a bare prototype
bool DreamUIPrefabSequence_CanBindWidgetToSequencer(UDreamUIPrefabHelperObject* InPrefabHelper, const UDreamWidget* InWidget);


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
		//no animation selected leaves the sequencer itself null, so a live sequencer means keying is on
		return Sequencer->GetRootMovieSceneSequence() != nullptr;
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

		// A sub-prefab widget's binding does not survive a save, so the key button must not author
		// one; the picker in the sequencer refuses these widgets and this path has to match it.
		UDreamUIPrefabHelperObject* Helper = PrefabEditor.IsValid() ? PrefabEditor.Pin()->GetPrefabHelperObject() : nullptr;
		Objects.RemoveAll([Helper](UObject* Object)
		{
			const UDreamWidget* Widget = Cast<UDreamWidget>(Object);
			if (Widget == nullptr && Object != nullptr)
			{
				Widget = Object->GetTypedOuter<UDreamWidget>();
			}
			return Widget != nullptr && !DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper, Widget);
		});
		if (Objects.IsEmpty())
		{
			return;
		}

		FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

		Sequencer->KeyProperty(KeyPropertyParams);
	}
}
