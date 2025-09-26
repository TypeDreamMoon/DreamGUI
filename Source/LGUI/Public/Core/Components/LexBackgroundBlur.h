// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisualPostProcess.h"
#include "LexBackgroundBlur.generated.h"

/** 
 * UI element that can add blur effect on background image, just like UMG's BackgroundBlur.
 * Use it in ScreenSpace or WorldSpace-LexUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer Sampling on OpenGL".
 */
UCLASS(ClassGroup = (LGUI), NotBlueprintable)
class LGUI_API ULexBackgroundBlur : public ULexVisualPostProcess
{
	GENERATED_BODY()

public:	
	ULexBackgroundBlur(const FObjectInitializer& ObjectInitializer);

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	/** Blur effect strength. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = 100.0f))
		float BlurStrength = 10.0f;
	/** Will alpha affect blur strength? If true, then 0 alpha means 0 blur strength, and 1 alpha means full blur strength. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool ApplyAlphaToBlur = true;
	/** No need to change this because default value can give you good result. */
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay) 
		int MaxDownSampleLevel = 6;
	/** Use strengthTexture's red channel to control blur strength, 0-no blur, 1-full blur. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (DisplayThumbnail = "false"))
		TObjectPtr<UTexture2D> StrengthTexture;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetBlurStrength() const { return BlurStrength; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		int GetMaxDownSampleLevel() const { return MaxDownSampleLevel; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool GetApplyAlphaToBlur()const { return ApplyAlphaToBlur; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		UTexture2D* GetStrengthTexture()const { return StrengthTexture; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetBlurStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetMaxDownSampleLevel(int Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetApplyAlphaToBlur(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetStrengthTexture(UTexture2D* Value);

	virtual TSharedPtr<FLexVisualPostProcessRenderProxy> GetRenderProxy()override;
	virtual void MarkAllDirty()override;
protected:
	float Inv_SampleLevelInterval = 1.0f;
	FORCEINLINE float GetBlurStrengthInternal();
protected:
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
	void SendStrengthTextureToRenderProxy();
};
