// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIDetailKeyframeHandler.h"
#include "ISequencer.h"
#include "SDreamWidgetAnimationEditor.h"
#include "MovieScene.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/Components/DreamWidget.h"

//declared in DreamWidgetAnimationEditorWidget.cpp; the comment there says why it is a bare prototype
bool DreamWidgetAnimation_CanBindWidgetToSequencer(const UDreamWidget* InWidget);


FDreamUIDetailKeyframeHandler::FDreamUIDetailKeyframeHandler(TSharedPtr<FDreamWidgetBlueprintEditor> InSequenceEditor)
	: DesignerEditor( InSequenceEditor )
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
	if (DesignerEditor.IsValid())
	{
		auto SequencerEditor = DesignerEditor.Pin()->GetSequencerEditor();
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
		Objects.RemoveAll([](UObject* Object)
		{
			const UDreamWidget* Widget = Cast<UDreamWidget>(Object);
			if (Widget == nullptr && Object != nullptr)
			{
				Widget = Object->GetTypedOuter<UDreamWidget>();
			}
			return Widget != nullptr && !DreamWidgetAnimation_CanBindWidgetToSequencer(Widget);
		});
		if (Objects.IsEmpty())
		{
			return;
		}

		FKeyPropertyParams KeyPropertyParams(Objects, KeyedPropertyHandle, ESequencerKeyMode::ManualKeyForced);

		Sequencer->KeyProperty(KeyPropertyParams);
	}
}
