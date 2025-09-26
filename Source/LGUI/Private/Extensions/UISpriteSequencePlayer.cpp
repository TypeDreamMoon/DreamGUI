// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSequencePlayer.h"

#include "LGUI.h"
#include "LTweenBPLibrary.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/Components/LexSprite.h"
#include "Core/Components/LexSpriteBase.h"

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
	if (!sprite.IsValid())
	{
		if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
		{
			sprite = Cast<ULexSprite>(Widget->GetVisual());
		}
	}
	if (!sprite.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s]Need UISprite component!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	if (spriteSequence.Num() <= 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s]SpriteSequence array is empty!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	return true;
}
float UUISpriteSequencePlayer::GetDuration()const
{
	return (float)(spriteSequence.Num()) / fps;
}
void UUISpriteSequencePlayer::PrepareForPlay()
{

}

void UUISpriteSequencePlayer::OnUpdateAnimation(int frameNumber)
{
	frameNumber = FMath::Clamp(frameNumber, 0, spriteSequence.Num() - 1);
	sprite->SetSprite(spriteSequence[frameNumber], snapSpriteSize);
}

void UUISpriteSequencePlayer::SetSpriteSequence(TArray<ULexUISpriteData_BaseObject*> value)
{
	spriteSequence = value;
}

void UUISpriteSequencePlayer::SetSnapSpriteSize(bool value)
{
	snapSpriteSize = value;
}
