// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSheetTexturePlayer.h"

#include "DreamGUI.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"

#if WITH_EDITOR
void UUISpriteSheetTexturePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (!Texture.IsValid())
		{
			Texture = Cast<UDreamTexture>(GetWidget()->GetVisual());
		}
		if (Texture.IsValid())
		{
			if (!bPreviewInEditor)
			{
				Texture->SetUVRect(FVector4f(0, 0, 1, 1));
			}
		}
	}
}
#endif
bool UUISpriteSheetTexturePlayer::CanPlay()
{
	if (!Texture.IsValid())
	{
		Texture = Cast<UDreamTexture>(GetWidget()->GetVisual());
	}
	if (!Texture.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Need DreamTexture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	if (!IsValid(Texture->GetTexture()))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d DreamTexture must have valid texture!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return false;
	}
	if (WidthCount <= 0 || HeightCount <= 0)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d WidthCount & HeightCount must greater then 0!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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

/*
 * Both clamps take the LAST INDEX, not the count. They used to take the count, and only one of the
 * two was harmless: the column is already inside range from the modulo, so its clamp never bit,
 * while the row is FrameNumber / WidthCount and does reach HeightCount. That put the cell's V
 * origin at exactly 1.0 -- a full sheet below the last row, sampling nothing.
 *
 * Frame WidthCount * HeightCount is not an exotic input. The tick path wraps only when ElapsedTime
 * is STRICTLY past Duration, so a clock landing exactly on the duration asks for it, and SeekFrame
 * hands it over on request. Correcting the column at the same time is deliberate even though it
 * changes no behaviour today: the two lines are one idea, and leaving the wrong half in place is
 * how the next reader concludes the count was intentional.
 *
 * Texture is a weak pointer resolved once by CanPlay, and the visual it points at can be replaced
 * under a running animation, so it is re-checked here rather than trusted.
 */
void UUISpriteSheetTexturePlayer::OnUpdateAnimation(int FrameNumber)
{
	if (!Texture.IsValid() || WidthCount <= 0 || HeightCount <= 0)
	{
		return;
	}
	int verticalFrame = (int)(FrameNumber / WidthCount);
	int horizontalFrame = (int)(FrameNumber % WidthCount);
	verticalFrame = FMath::Clamp(verticalFrame, 0, HeightCount - 1);
	horizontalFrame = FMath::Clamp(horizontalFrame, 0, WidthCount - 1);
	Texture->SetUVRect(FVector4f(WidthUVInterval * horizontalFrame, HeightUVInterval * verticalFrame, WidthUVInterval, HeightUVInterval));
}

void UUISpriteSheetTexturePlayer::SetWidthCount(int value)
{
	WidthCount = value;
}
void UUISpriteSheetTexturePlayer::SetHeightCount(int value)
{
	HeightCount = value;
}
