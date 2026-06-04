// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIMaterialTrackEditor.h"

#include "Core/Components/LexImage.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "PrefabSystem/PrefabAnimation/MovieSceneLexUIMaterialTrack.h"


FLexUIMaterialTrackEditor::FLexUIMaterialTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMaterialTrackEditor( InSequencer )
{
}


TSharedRef<ISequencerTrackEditor> FLexUIMaterialTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable( new FLexUIMaterialTrackEditor( OwningSequencer ) );
}


bool FLexUIMaterialTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneLexUIMaterialTrack::StaticClass();
}


UMaterialInterface* FLexUIMaterialTrackEditor::GetMaterialInterfaceForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
{
	for (TWeakObjectPtr<> WeakObjectPtr : GetSequencer()->FindObjectsInCurrentSequence(ObjectBinding))
	{
		auto Visual = Cast<ULexVisualBatchMesh>( WeakObjectPtr.Get() );
		if (auto Text = Cast<ULexText>(Visual))
		{
			return Text->GetOverrideMaterial();
		}
		if (auto Image = Cast<ULexImage>(Visual))
		{
			return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
		}
	}
	return nullptr;
}
