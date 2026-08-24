// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUISequenceFactory.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "MovieScene.h"

UDreamUISequenceFactory::UDreamUISequenceFactory()
{
	bCreateNew = true;
	bEditAfterNew = false;
	SupportedClass = UDreamUISequence::StaticClass();
}

UObject* UDreamUISequenceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDreamUISequence* Sequence = NewObject<UDreamUISequence>(InParent, Class, Name, Flags | RF_Transactional);
	// A default two-second range and the root binding, so the asset is usable straight away.
	const FFrameRate TickResolution = Sequence->GetMovieScene()->GetTickResolution();
	Sequence->GetMovieScene()->SetPlaybackRange(0, (2.0 * TickResolution).FrameNumber.Value);
	Sequence->EnsureRootBinding();
	return Sequence;
}

UClass* FAssetTypeActions_DreamUISequence::GetSupportedClass() const
{
	return UDreamUISequence::StaticClass();
}
