// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexVisualPostProcess.h"
#include "LexFrameCapture.generated.h"

UENUM(BlueprintType)
enum class ELexFrameCaptureMode : uint8
{
	/** Capture one frame every time we call DoCapture, and return new RenderTarget. */
	OneShot,
	/** Continuous capture frame after we call DoCapture until we call StopCapture. */
	Continuous,
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FLexFrameCapture_OnFrameReady_DynamicDelegate, UTextureRenderTarget2D*, CapturedFrame);
DECLARE_DELEGATE_OneParam(FLexFrameCapture_OnFrameReady_Delegate, UTextureRenderTarget2D*);

/**
 * UI element that can capture background image for further use.
 * Use it in ScreenSpace or WorldSpace-LexUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer Sampling on OpenGL".
 */
UCLASS(ClassGroup = (LGUI), NotBlueprintable)
class LGUI_API ULexFrameCapture : public ULexVisualPostProcess
{
	GENERATED_BODY()

public:	
	ULexFrameCapture(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	void OnUpdate(float DeltaTime);
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	/** Capture full screen or just this UI's rect area. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
	bool bCaptureFullScreen = true;
	UPROPERTY(VisibleAnywhere, Transient, Category = "LGUI")
	TObjectPtr<UTextureRenderTarget2D> CapturedFrame;

	DECLARE_MULTICAST_DELEGATE_OneParam(FLexFrameCapture_OnFrameReady_MulticastDelegate, UTextureRenderTarget2D*);
	FLexFrameCapture_OnFrameReady_MulticastDelegate OnFrameReady;
	bool bIsFrameReady = false;
	friend class FUIFrameCaptureRenderProxy;
	enum class ECaptureMode : uint8
	{
		None, OneShot, Continuous,
	};
	ECaptureMode CaptureMode = ECaptureMode::None;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	UTextureRenderTarget2D* GetCapturedFrame()const { return CapturedFrame; }
	/**
	 * Do a one shot capture, register the delegate to get result.
	 * @param InDelegate Called when capture finish.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void DoOneFrameCapture(const FLexFrameCapture_OnFrameReady_DynamicDelegate& InDelegate);
	void DoOneFrameCapture(const FLexFrameCapture_OnFrameReady_Delegate& InDelegate);
	void DoOneFrameCapture(const TFunction<void(UTextureRenderTarget2D*)>& InFunction);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void StartContinuousCapture();
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void StopContinuousCapture();

	virtual TSharedPtr<FLexVisualPostProcessRenderProxy> GetRenderProxy()override;
	virtual void MarkAllDirty()override;
protected:
	void MarkOneFrameCapture();
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendCaptureDataToRenderProxy();
	void UpdateRenderTarget();
};
