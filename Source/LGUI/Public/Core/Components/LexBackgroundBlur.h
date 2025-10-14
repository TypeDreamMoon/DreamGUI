// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisualPostProcess.h"
#include "LexBackgroundBlur.generated.h"

UENUM(BlueprintType)
enum class ELexBackgroundBlurRenderType:uint8
{
	/** Render direct to screen */
	Screen,
	/** Output to a RenderTarget */
	RenderTarget,
};

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

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	/** Blur effect strength. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = 100.0f))
		float BlurStrength = 10.0f;
	/** Will alpha affect blur strength? If true, then 0 alpha means 0 blur strength, and 1 alpha means full blur strength. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool ApplyAlphaToBlur = true;
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexBackgroundBlurRenderType RenderType = ELexBackgroundBlurRenderType::Screen;
	/**
	 * Blur result will output to this RenderTarget.
	 * Will create one if not specified.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta=(EditCondition="RenderType==ELexBackgroundBlurRenderType::RenderTarget"))
	UTextureRenderTarget2D* OutputRenderTarget = nullptr;
	
	/** No need to change this because default value can give you good result. */
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay)
		int MaxDownSampleLevel = 6;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	float GetBlurStrength() const { return BlurStrength; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetMaxDownSampleLevel() const { return MaxDownSampleLevel; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetApplyAlphaToBlur()const { return ApplyAlphaToBlur; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexBackgroundBlurRenderType GetRenderType()const { return RenderType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	UTextureRenderTarget2D* GetOutputRenderTarget()const { return OutputRenderTarget; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetBlurStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetMaxDownSampleLevel(int Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetApplyAlphaToBlur(bool Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRenderType(ELexBackgroundBlurRenderType Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetOutputRenderTarget(UTextureRenderTarget2D* Value);

	virtual TSharedPtr<FLexVisualPostProcessRenderProxy> GetRenderProxy()override;
	virtual void MarkAllDirty()override;
private:
	float Inv_SampleLevelInterval = 1.0f;
	FORCEINLINE float GetBlurStrengthInternal();
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
	void UpdateRenderTarget();
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void OnTransformChanged() override;
};
