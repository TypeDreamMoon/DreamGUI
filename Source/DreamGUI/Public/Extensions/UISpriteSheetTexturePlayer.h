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

	float WidthUVInterval, HeightUVInterval;
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
