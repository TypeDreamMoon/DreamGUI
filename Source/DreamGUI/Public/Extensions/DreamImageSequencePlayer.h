// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "Core/DreamUIBehaviour.h"
#include "DreamImageSequencePlayer.generated.h"

UCLASS(Abstract)
class DREAMGUI_API UDreamImageSequencePlayer : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamImageSequencePlayer();
protected:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bPreviewInEditor = true;
#endif
	/** fps: Frame per second */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		float Fps = 24;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bLoop = true;
	/** Autoplay when BeginPlay. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bPlayOnStart = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bAffectByGamePause = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bAffectByTimeDilation = false;
	bool bIsPlaying = false;

	virtual void Awake()override;
	virtual void OnDestroy()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
#if WITH_EDITOR
	FDelegateHandle EditorPlayDelegateHandle;
#endif
	TWeakObjectPtr<class UDreamTweener> PlayTweener;
	void UpdateAnimation(float deltaTime);
	virtual bool CanPlay() { return true; }
	virtual void PrepareForPlay() {};
	virtual void OnUpdateAnimation(int FrameNumber)PURE_VIRTUAL(UDreamUIImageSequencePlayer::OnUpdateAnimation, );
	float ElapsedTime = 0.0f;
	float Duration = 1.0f;
	bool bIsPaused = false;
public:
	/** Play the animation sequence. If is paused then resume play */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Play();
	/** Stop the animation. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Stop();
	/** Pause the animation, call Play to resume. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Pause() { bIsPaused = true; }
	/** Seek to desired frame and play. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SeekFrame(int frameNumber);
	/** Seek to desired time and play. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SeekTime(float time);
	/** Is the animation playing? */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetIsPlaying()const { return bIsPlaying && !bIsPaused; }
	/** Get the animation time length */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		virtual float GetDuration()const PURE_VIRTUAL(UDreamGUIImageSequencePlayer::GetDuration, return 0.0f;);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetFps()const { return Fps; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetLoop()const { return bLoop; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetFps(float value);
	/** Will take effect on nexe cycle. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetLoop(bool value);
};
