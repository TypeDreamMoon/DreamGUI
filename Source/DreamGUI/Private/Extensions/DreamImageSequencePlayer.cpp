// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamImageSequencePlayer.h"
#include "DreamTweenBPLibrary.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamTexture.h"

UDreamImageSequencePlayer::UDreamImageSequencePlayer()
{
}
void UDreamImageSequencePlayer::Awake()
{
	Super::Awake();	
	if (bPlayOnStart)
	{
		Play();
	}
}

void UDreamImageSequencePlayer::OnDestroy()
{
	Super::OnDestroy();
	Stop();
}

void UDreamImageSequencePlayer::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(true))
		{
			EditorPlayDelegateHandle = DreamUIManagerObject->GetEditorTickDelegate().AddWeakLambda(this, [this](float deltaTime) {
				if (!bPreviewInEditor)return;
				if (!CanPlay())return;
				Duration = GetDuration();
				PrepareForPlay();
				UpdateAnimation(deltaTime);
				});
		}
	}
#endif
}
void UDreamImageSequencePlayer::OnUnregister()
{
	Super::OnUnregister();
#if WITH_EDITOR
	if (EditorPlayDelegateHandle.IsValid())
	{
		if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(false))
		{
			DreamUIManagerObject->GetEditorTickDelegate().Remove(EditorPlayDelegateHandle);
		}
	}
#endif
}

void UDreamImageSequencePlayer::Play()
{
	if (!CanPlay())return;
	if (!bIsPlaying)
	{
		bIsPlaying = true;
		ElapsedTime = 0.0f;
		Duration = GetDuration();
		PrepareForPlay();
		PlayTweener = UDreamTweenBPLibrary::UpdateCall(this, FDreamTweenUpdateDelegate::CreateUObject(this, &UDreamImageSequencePlayer::UpdateAnimation));
		if (PlayTweener.IsValid())
		{
			PlayTweener->SetAffectByGamePause(bAffectByGamePause);
		}
		UpdateAnimation(0);
	}
	if (bIsPaused)
	{
		bIsPaused = false;
	}
}

void UDreamImageSequencePlayer::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		UDreamTweenBPLibrary::KillIfIsTweening(this, PlayTweener.Get());
	}
}

void UDreamImageSequencePlayer::SeekFrame(int frameNumber)
{
	ElapsedTime = frameNumber / Fps;
	if (CanPlay())
	{
		OnUpdateAnimation(frameNumber);
	}
}
void UDreamImageSequencePlayer::SeekTime(float time)
{
	ElapsedTime = time;
	if (CanPlay())
	{
		OnUpdateAnimation(ElapsedTime * Fps);
	}
}

void UDreamImageSequencePlayer::UpdateAnimation(float deltaTime)
{
	if (bIsPaused)return;
	ElapsedTime += deltaTime;
	if (ElapsedTime > Duration)
	{
		if (bLoop)
		{
			ElapsedTime -= Duration;
		}
		else
		{
			Stop();
			return;
		}
	}
	int frameNumber = (int)(ElapsedTime * Fps);
	OnUpdateAnimation(frameNumber);
}

void UDreamImageSequencePlayer::SetFps(float value)
{
	Fps = value;
}
void UDreamImageSequencePlayer::SetLoop(bool value)
{
	bLoop = value;
}
