// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIMaterialTrackEditor.h"

#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "PrefabSystem/PrefabAnimation/MovieSceneDreamUIMaterialTrack.h"


FDreamUIMaterialTrackEditor::FDreamUIMaterialTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMaterialTrackEditor( InSequencer )
{
}


TSharedRef<ISequencerTrackEditor> FDreamUIMaterialTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable( new FDreamUIMaterialTrackEditor( OwningSequencer ) );
}


bool FDreamUIMaterialTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneDreamUIMaterialTrack::StaticClass();
}


UMaterialInterface* FDreamUIMaterialTrackEditor::GetMaterialInterfaceForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack )
{
	for (TWeakObjectPtr<> WeakObjectPtr : GetSequencer()->FindObjectsInCurrentSequence(ObjectBinding))
	{
		auto Visual = Cast<UDreamVisualBatchMesh>( WeakObjectPtr.Get() );
		if (auto Text = Cast<UDreamText>(Visual))
		{
			return Text->GetOverrideMaterial();
		}
		if (auto Image = Cast<UDreamImage>(Visual))
		{
			return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
		}
	}
	return nullptr;
}
