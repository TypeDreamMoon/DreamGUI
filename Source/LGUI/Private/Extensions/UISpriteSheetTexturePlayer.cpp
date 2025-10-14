// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSheetTexturePlayer.h"

#include "LGUI.h"
#include "LTweenBPLibrary.h"
#include "Core/Components/LexTexture.h"

#if WITH_EDITOR
void UUISpriteSheetTexturePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (!Texture.IsValid())
		{
			if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
			{
				Texture = Cast<ULexTexture>(Widget->GetVisual());
			}
		}
		if (Texture.IsValid())
		{
			if (!bPreviewInEditor)
			{
				Texture->SetUVRect(FVector4(0, 0, 1, 1));
			}
		}
	}
}
#endif
bool UUISpriteSheetTexturePlayer::CanPlay()
{
	if (!Texture.IsValid())
	{
		if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
		{
			Texture = Cast<ULexTexture>(Widget->GetVisual());
		}
	}
	if (!Texture.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s]Need UITexture component!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	if (!IsValid(Texture->GetTexture()))
	{
		UE_LOG(LGUI, Error, TEXT("[%s]UITexture component must have valid texture!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	if (WidthCount <= 0 || HeightCount <= 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s]WidthCount & HeightCount must greater then 0!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	return true;
}
float UUISpriteSheetTexturePlayer::GetDuration()const
{
	return (float)(WidthCount * HeightCount) / Fps;
}
void UUISpriteSheetTexturePlayer::PrepareForPlay()
{
	WidthUVInterval = 1.0f / WidthCount;
	HeightUVInterval = 1.0f / HeightCount;
}

void UUISpriteSheetTexturePlayer::OnUpdateAnimation(int FrameNumber)
{
	int verticalFrame = (int)(FrameNumber / WidthCount);
	int horizontalFrame = (int)(FrameNumber % WidthCount);
	verticalFrame = FMath::Clamp(verticalFrame, 0, HeightCount);
	horizontalFrame = FMath::Clamp(horizontalFrame, 0, WidthCount);
	Texture->SetUVRect(FVector4(WidthUVInterval * horizontalFrame, HeightUVInterval * verticalFrame, WidthUVInterval, HeightUVInterval));
}

void UUISpriteSheetTexturePlayer::SetWidthCount(int value)
{
	WidthCount = value;
}
void UUISpriteSheetTexturePlayer::SetHeightCount(int value)
{
	HeightCount = value;
}
