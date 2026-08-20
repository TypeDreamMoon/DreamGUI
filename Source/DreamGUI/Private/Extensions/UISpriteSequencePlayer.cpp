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

void UUISpriteSequencePlayer::OnUpdateAnimation(int FrameNumber)
{
	FrameNumber = FMath::Clamp(FrameNumber, 0, SpriteSequence.Num() - 1);
	Sprite->SetSprite(SpriteSequence[FrameNumber], bSnapSpriteSize);
}

void UUISpriteSequencePlayer::SetSpriteSequence(TArray<UDreamUISpriteData_BaseObject*> value)
{
	SpriteSequence = value;
}

void UUISpriteSequencePlayer::SetSnapSpriteSize(bool value)
{
	bSnapSpriteSize = value;
}
