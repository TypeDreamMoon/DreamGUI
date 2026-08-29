// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

struct FAssetData;

/**
 * Offers "DreamUI Animation" on widget and presenter bindings: pick a UDreamUISequence asset and it
 * plays as a subsequence whose root re-targets to that binding's object.
 */
class FDreamUISequenceTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);

	FDreamUISequenceTrackEditor(TSharedRef<ISequencer> InSequencer);

	//~ ISequencerTrackEditor interface
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;

private:
	void AddAssetSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void HandleAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings);
	void AddWidgetPickerSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);
	void HandleWidgetPicked(TWeakObjectPtr<class UDreamWidget> InWidget);
};
