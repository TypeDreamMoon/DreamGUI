// Copyright Epic Games, Inc. All Rights Reserved.

#include "LGUIMaterialTrackEditor.h"

#include "Core/Components/LexImage.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "PrefabAnimation/MovieSceneLGUIMaterialTrack.h"


FLGUIMaterialTrackEditor::FLGUIMaterialTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMaterialTrackEditor( InSequencer )
{
}


TSharedRef<ISequencerTrackEditor> FLGUIMaterialTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable( new FLGUIMaterialTrackEditor( OwningSequencer ) );
}


bool FLGUIMaterialTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneLGUIMaterialTrack::StaticClass();
}


UMaterialInterface* FLGUIMaterialTrackEditor::GetMaterialInterfaceForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
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
