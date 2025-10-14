// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "LexImageSequencePlayer.h"
#include "UISpriteSequencePlayer.generated.h"

class ULexUISpriteData_BaseObject;

/** Play Sprite sequence, need UISprite component. */
UCLASS(ClassGroup = (LGUI), meta = (BlueprintSpawnableComponent))
class LGUI_API UUISpriteSequencePlayer : public ULexImageSequencePlayer
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	UPROPERTY(Transient)
		TWeakObjectPtr<class ULexSpriteBase> Sprite;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TArray<TObjectPtr<ULexUISpriteData_BaseObject>> SpriteSequence;
	/** should also set size to Sprite-data? */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bSnapSpriteSize = true;

	virtual bool CanPlay()override;
	virtual float GetDuration()const override;
	virtual void PrepareForPlay()override;
	virtual void OnUpdateAnimation(int FrameNumber)override;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<ULexUISpriteData_BaseObject*> GetSpriteSequence()const { return SpriteSequence; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool GetSnapSpriteSize()const { return bSnapSpriteSize; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSpriteSequence(TArray<ULexUISpriteData_BaseObject*> value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSnapSpriteSize(bool value);
};
