// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamImageSequencePlayer.h"
#include "UISpriteSequencePlayer.generated.h"

class UDreamUISpriteData_BaseObject;

/** Play Sprite sequence, need UISprite component. */
UCLASS(ClassGroup = (DreamGUI), meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUISpriteSequencePlayer : public UDreamImageSequencePlayer
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	UPROPERTY(Transient)
		TWeakObjectPtr<class UDreamSpriteBase> Sprite;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<TObjectPtr<UDreamUISpriteData_BaseObject>> SpriteSequence;
	/** should also set size to Sprite-data? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bSnapSpriteSize = true;

	virtual bool CanPlay()override;
	virtual float GetDuration()const override;
	virtual void PrepareForPlay()override;
	virtual void OnUpdateAnimation(int FrameNumber)override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		const TArray<UDreamUISpriteData_BaseObject*>& GetSpriteSequence()const { return SpriteSequence; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetSnapSpriteSize()const { return bSnapSpriteSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSpriteSequence(TArray<UDreamUISpriteData_BaseObject*> value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetSnapSpriteSize(bool value);
};
