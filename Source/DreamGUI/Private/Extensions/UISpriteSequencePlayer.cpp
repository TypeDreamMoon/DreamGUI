// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSequencePlayer.h"

#include "DreamGUI.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/Components/DreamWidget.h"

#if WITH_EDITOR
void UUISpriteSequencePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{

	}
}
#endif
bool UUISpriteSequencePlayer::CanPlay()
{
	if (!Sprite.IsValid())
	{
		Sprite = Cast<UDreamSprite>(GetWidget()->GetVisual());
	}
	if (!Sprite.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Need UISprite component!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	if (SpriteSequence.Num() <= 0)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d SpriteSequence array is empty!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	return true;
}
float UUISpriteSequencePlayer::GetDuration()const
{
	return SpriteSequence.Num() / Fps;
}
void UUISpriteSequencePlayer::PrepareForPlay()
{

}

/*
 * Both guards here are load-bearing, and neither is the redundancy it looks like.
 *
 * The clamp does NOT protect an empty array. FMath::Clamp is Max(Min(X, Max), Min), so
 * Clamp(N, 0, -1) evaluates to 0 rather than -1 -- element zero of nothing. CanPlay refuses an
 * empty sequence at the door, but the door is only Play and Seek: the array can be emptied while
 * the tween is already ticking, and this function is what the tween calls.
 *
 * Sprite is a weak pointer to a visual that CanPlay resolved once. A visual can be replaced or
 * destroyed under a running animation -- CreateNewVisual swaps it outright -- so the pointer that
 * was valid at Play is not the pointer this frame necessarily has.
 */
void UUISpriteSequencePlayer::OnUpdateAnimation(int FrameNumber)
{
	if (SpriteSequence.Num() <= 0 || !Sprite.IsValid())
	{
		return;
	}
	FrameNumber = FMath::Clamp(FrameNumber, 0, SpriteSequence.Num() - 1);
	Sprite->SetSprite(SpriteSequence[FrameNumber], bSnapSpriteSize);
}

/*
 * Replacing the frames replaces the length, and Duration is captured once by Play -- so a running
 * animation handed a longer or shorter sequence would keep comparing its clock against the old
 * one, looping early or late for the rest of its life.
 *
 * Emptying it is the case that used to bite hardest: there is nothing left to draw, so the player
 * is stopped rather than left ticking over a sequence it cannot index. Stop on an already-stopped
 * player is a no-op, so this is safe on the configure-before-play path too, and it deliberately
 * does not go through CanPlay -- clearing a sequence is a legitimate way to end an animation, not
 * an authoring error worth a log line.
 */
void UUISpriteSequencePlayer::SetSpriteSequence(TArray<UDreamUISpriteData_BaseObject*> value)
{
	SpriteSequence = value;
	if (SpriteSequence.Num() <= 0)
	{
		Stop();
		return;
	}
	if (bIsPlaying)
	{
		Duration = GetDuration();
	}
}

void UUISpriteSequencePlayer::SetSnapSpriteSize(bool value)
{
	bSnapSpriteSize = value;
}
