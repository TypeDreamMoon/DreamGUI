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
				EnforceFrameRate();
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

/*
 * Fps is a divisor, a multiplier and the only source of Duration, and every one of those breaks at
 * zero in a way nothing reports. GetDuration becomes an infinity (or, on an empty sequence, a NaN
 * from zero over zero); UpdateAnimation wraps when ElapsedTime > Duration and stops when it is not
 * looping, and NEITHER comparison is ever true against those -- so a non-looping animation with no
 * frame rate never ends and never says why. SeekFrame divides by it directly, so a zero there
 * writes a NaN into ElapsedTime that every subsequent += preserves forever.
 *
 * A negative rate is the same defect with the sign flipped: the duration goes negative and the wrap
 * fires on every tick. This design has no notion of playing backwards -- ElapsedTime only ever
 * accumulates -- so a negative rate is not a feature being refused, it is a value with no meaning.
 *
 * The floor is a small positive number rather than 1: a rate below one frame a second is a
 * legitimate slow crawl, and only the zero itself has to be excluded. It is enforced where the
 * value is USED rather than only in the setter, because the property is EditAnywhere and assets
 * authored before the ClampMin metadata existed can still carry a zero in from disk.
 */
void UDreamImageSequencePlayer::EnforceFrameRate()
{
	Fps = FMath::Max(Fps, UE_KINDA_SMALL_NUMBER);
}

void UDreamImageSequencePlayer::Play()
{
	EnforceFrameRate();
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
			PlayTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
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

/*
 * Seeking draws a frame, and drawing a frame needs the same derived state Play computes before its
 * first draw: the length, and whatever PrepareForPlay works out for the subclass. The sheet player
 * turns its two grid counts into a UV cell size there and nowhere else, so a seek on a player that
 * had never played used to read two floats nothing had written and hand the visual a cell with no
 * size -- silently, and only on the path a designer uses to scrub.
 *
 * PrepareForPlay is only reachable once CanPlay has agreed, because the preparation is what CanPlay
 * guards: the sheet player divides by its cell counts the instant it is allowed to.
 */
void UDreamImageSequencePlayer::SeekFrame(int frameNumber)
{
	EnforceFrameRate();
	ElapsedTime = frameNumber / Fps;
	if (CanPlay())
	{
		Duration = GetDuration();
		PrepareForPlay();
		OnUpdateAnimation(frameNumber);
	}
}
void UDreamImageSequencePlayer::SeekTime(float time)
{
	EnforceFrameRate();
	ElapsedTime = time;
	if (CanPlay())
	{
		Duration = GetDuration();
		PrepareForPlay();
		OnUpdateAnimation(ElapsedTime * Fps);
	}
}

void UDreamImageSequencePlayer::UpdateAnimation(float deltaTime)
{
	EnforceFrameRate();
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
	Fps = FMath::Max(value, UE_KINDA_SMALL_NUMBER);
}
void UDreamImageSequencePlayer::SetLoop(bool value)
{
	bLoop = value;
}
