// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSequencePlayer.h"

#include "LGUI.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/Components/LexSprite.h"
#include "Core/Components/LexSpriteBase.h"
#include "Core/Components/LexWidget.h"

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
		if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
		{
			Sprite = Cast<ULexSprite>(Widget->GetVisual());
		}
	}
	if (!Sprite.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Need UISprite component!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	if (SpriteSequence.Num() <= 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d SpriteSequence array is empty!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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

void UUISpriteSequencePlayer::SetSpriteSequence(TArray<ULexUISpriteData_BaseObject*> value)
{
	SpriteSequence = value;
}

void UUISpriteSequencePlayer::SetSnapSpriteSize(bool value)
{
	bSnapSpriteSize = value;
}
