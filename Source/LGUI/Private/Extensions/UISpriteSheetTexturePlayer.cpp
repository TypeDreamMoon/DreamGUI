// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "LTweenBPLibrary.h"
#include "LGUI/Public/Core/Components/UITexture.h"

#if WITH_EDITOR
void UUISpriteSheetTexturePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		if (!texture.IsValid())
		{
			if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
			{
				texture = Cast<UUITexture>(Widget->GetVisual());
			}
		}
		if (texture.IsValid())
		{
			if (!previewInEditor)
			{
				texture->SetUVRect(FVector4(0, 0, 1, 1));
			}
		}
	}
}
#endif
bool UUISpriteSheetTexturePlayer::CanPlay()
{
	if (!texture.IsValid())
	{
		if (auto Widget = GetOwner()->FindComponentByClass<ULexWidget>())
		{
			texture = Cast<UUITexture>(Widget->GetVisual());
		}
	}
	if (!texture.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s]Need UITexture component!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	if (!IsValid(texture->GetTexture()))
	{
		UE_LOG(LGUI, Error, TEXT("[%s]UITexture component must have valid texture!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	if (widthCount <= 0 || heightCount <= 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s]WidthCount & HeightCount must greater then 0!"), ANSI_TO_TCHAR(__FUNCTION__));
		return false;
	}
	return true;
}
float UUISpriteSheetTexturePlayer::GetDuration()const
{
	return (float)(widthCount * heightCount) / fps;
}
void UUISpriteSheetTexturePlayer::PrepareForPlay()
{
	widthUVInterval = 1.0f / widthCount;
	heightUVInterval = 1.0f / heightCount;
}

void UUISpriteSheetTexturePlayer::OnUpdateAnimation(int frameNumber)
{
	int verticalFrame = (int)(frameNumber / widthCount);
	int horizontalFrame = (int)(frameNumber % widthCount);
	verticalFrame = FMath::Clamp(verticalFrame, 0, heightCount);
	horizontalFrame = FMath::Clamp(horizontalFrame, 0, widthCount);
	texture->SetUVRect(FVector4(widthUVInterval * horizontalFrame, heightUVInterval * verticalFrame, widthUVInterval, heightUVInterval));
}

void UUISpriteSheetTexturePlayer::SetWidthCount(int value)
{
	widthCount = value;
}
void UUISpriteSheetTexturePlayer::SetHeightCount(int value)
{
	heightCount = value;
}
