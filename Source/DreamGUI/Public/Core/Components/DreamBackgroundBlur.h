// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamVisualPostProcess.h"
#include "DreamBackgroundBlur.generated.h"

/** 
 * UI element that can add blur effect on background image, just like UMG's BackgroundBlur.
 * Use it in ScreenSpace or WorldSpace-DreamUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer Sampling on OpenGL".
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUI_API UDreamBackgroundBlur : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:	
	UDreamBackgroundBlur(const FObjectInitializer& ObjectInitializer);

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	/** Blur effect strength. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = 0.0, ClampMax = 1.0f))
		float BlurStrength = 0.1f;
	/** Will alpha affect blur strength? If true, then 0 alpha means 0 blur strength, and 1 alpha means full blur strength. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool ApplyAlphaToBlur = true;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay, meta = (ClampMin = 0, UIMin = 0, UIMax = 8))
		int MaxDownSampleLevel = 7;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetBlurStrength() const { return BlurStrength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	int GetMaxDownSampleLevel() const { return MaxDownSampleLevel; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetApplyAlphaToBlur()const { return ApplyAlphaToBlur; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetBlurStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMaxDownSampleLevel(int Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetApplyAlphaToBlur(bool Value);

	virtual FDreamVisualPostProcessRenderProxy* GetRenderProxy()override;
	virtual void MarkAllDirty()override;
private:
	FORCEINLINE float GetBlurStrengthInternal();
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
};
