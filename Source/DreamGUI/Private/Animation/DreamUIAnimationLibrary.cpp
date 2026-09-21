// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Animation/DreamUIAnimationLibrary.h"
#include "Animation/DreamWidgetAnimationPlayer.h"

bool UDreamUIAnimationLibrary::IsAnimationHandleValid(const FDreamUIAnimationHandle& Handle)
{
	return Handle.IsValid();
}

UMovieSceneSequence* UDreamUIAnimationLibrary::GetAnimationFromHandle(const FDreamUIAnimationHandle& Handle)
{
	return Handle.GetAnimation();
}

bool UDreamUIAnimationLibrary::EqualAnimationHandles(const FDreamUIAnimationHandle& A, const FDreamUIAnimationHandle& B)
{
	return A.Player == B.Player;
}
