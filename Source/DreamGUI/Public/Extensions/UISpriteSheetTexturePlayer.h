// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamImageSequencePlayer.h"
#include "UISpriteSheetTexturePlayer.generated.h"

class UDreamUISpriteData;

/** Play spritesheet texture, need UITexture component. */
UCLASS(ClassGroup = (DreamGUI), meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUISpriteSheetTexturePlayer : public UDreamImageSequencePlayer
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	UPROPERTY(Transient)
		TWeakObjectPtr<class UDreamTexture> Texture;
	/** Sprite element count of horizontal direction in texture. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		int WidthCount = 8;
	/** Sprite element count of vertical direction in texture. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		int HeightCount = 8;

	/**
	 * The size of one cell in UV space, derived from the two counts by PrepareForPlay and read by
	 * OnUpdateAnimation. Initialised because these are the only members of the class outside the
	 * reflection system, so nothing else zeroes them: a read that reached them before the first
	 * PrepareForPlay used to hand the visual whatever the allocation happened to contain. The base
	 * class now prepares on every entry point that draws, so the initialiser is a floor rather than
	 * a value anything is expected to see.
	 */
	float WidthUVInterval = 0.0f, HeightUVInterval = 0.0f;
	virtual bool CanPlay()override;
	virtual float GetDuration()const override;
	virtual void PrepareForPlay()override;
	virtual void OnUpdateAnimation(int FrameNumber)override;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetWidthCount()const { return WidthCount; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetHeightCount()const { return HeightCount; }
	/** Will take effect on next cycle. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetWidthCount(int value);
	/** Will take effect on next cycle. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetHeightCount(int value);
};
