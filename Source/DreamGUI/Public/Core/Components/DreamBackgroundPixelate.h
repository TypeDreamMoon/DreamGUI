// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "DreamVisualPostProcess.h"
#include "DreamBackgroundPixelate.generated.h"

/** 
 * UI element that can make the background look pixelated
 * Use it in ScreenSpace or WorldSpace-DreamUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer Sampling on OpenGL".
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUI_API UDreamBackgroundPixelate : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:	
	UDreamBackgroundPixelate(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	/** Pixelate effect strength. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = 100.0f))
		float PixelateStrength = 10.0f;
	/** Will alpha affect pixelate strength? If true, then 0 alpha means 0 pixelate strength, and 1 alpha means full pixelate strength. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool ApplyAlphaToStrength = true;
public:
	UFUNCTION(BlueprintCallable, Category="DreamGUI")
		float GetPixelateStrength() const { return PixelateStrength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetApplyAlphaToStrength()const { return ApplyAlphaToStrength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetPixelateStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetApplyAlphaToStrength(bool Value);

	virtual FDreamVisualPostProcessRenderProxy* GetRenderProxy()override;
	virtual void MarkAllDirty()override;
protected:
	FORCEINLINE float GetStrengthInternal();
protected:
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
};
