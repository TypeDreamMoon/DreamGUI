// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LexImageSequencePlayer.h"
#include "LTweenBPLibrary.h"
#include "Core/Components/LexTexture.h"
#include "PrefabSystem/LGUIPrefabManager.h"

ULexImageSequencePlayer::ULexImageSequencePlayer()
{
	PrimaryComponentTick.bCanEverTick = false;
}
void ULexImageSequencePlayer::BeginPlay()
{
	Super::BeginPlay();	
	if (!ULGUIPrefabWorldSubsystem::IsLGUIPrefabSystemProcessingActor(this->GetOwner()))
	{
		Awake_Implementation();
	}
}
void ULexImageSequencePlayer::Awake_Implementation()
{
	if (bPlayOnStart)
	{
		Play();
	}
}

void ULexImageSequencePlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	Stop();
}

void ULexImageSequencePlayer::OnRegister()
{
	Super::OnRegister();
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorPlayDelegateHandle = ULGUIPrefabManagerObject::RegisterEditorTickFunction([this](float deltaTime) {
			if (!bPreviewInEditor)return;
			if (!CanPlay())return;
			Duration = GetDuration();
			PrepareForPlay();
			UpdateAnimation(deltaTime);
			});
	}
#endif
}
void ULexImageSequencePlayer::OnUnregister()
{
	Super::OnUnregister();
#if WITH_EDITOR
	if (EditorPlayDelegateHandle.IsValid())
	{
		ULGUIPrefabManagerObject::UnregisterEditorTickFunction(EditorPlayDelegateHandle);
	}
#endif
}

void ULexImageSequencePlayer::Play()
{
	if (!CanPlay())return;
	if (!bIsPlaying)
	{
		bIsPlaying = true;
		ElapsedTime = 0.0f;
		Duration = GetDuration();
		PrepareForPlay();
		PlayTweener = ULTweenBPLibrary::UpdateCall(this, FLTweenUpdateDelegate::CreateUObject(this, &ULexImageSequencePlayer::UpdateAnimation));
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

void ULexImageSequencePlayer::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		ULTweenBPLibrary::KillIfIsTweening(this, PlayTweener.Get());
	}
}

void ULexImageSequencePlayer::SeekFrame(int frameNumber)
{
	ElapsedTime = frameNumber / Fps;
	if (CanPlay())
	{
		OnUpdateAnimation(frameNumber);
	}
}
void ULexImageSequencePlayer::SeekTime(float time)
{
	ElapsedTime = time;
	if (CanPlay())
	{
		OnUpdateAnimation(ElapsedTime * Fps);
	}
}

void ULexImageSequencePlayer::UpdateAnimation(float deltaTime)
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

void ULexImageSequencePlayer::SetFps(float value)
{
	Fps = value;
}
void ULexImageSequencePlayer::SetLoop(bool value)
{
	bLoop = value;
}
