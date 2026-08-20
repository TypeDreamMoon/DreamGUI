// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamVisual.h"
#include "Core/DreamUIRender/DreamUIPostProcessVertex.h"
#include "DreamVisualPostProcess.generated.h"

class FDreamVisualPostProcessRenderProxy;
struct FDreamUIPostProcessVertex;

UENUM(BlueprintType)
enum class EDreamBackgroundBlurRenderType:uint8
{
	/** Render direct to screen */
	Screen,
	/** Output to a RenderTarget */
	RenderTarget,
};

/** How a post-process visual's Color is combined with the background it captured. */
UENUM(BlueprintType)
enum class EDreamPostProcessTintMode :uint8
{
	/** result = background * Color. Can only darken or colourise; white leaves it untouched. */
	Multiply,
	/** result = lerp(background, Color, Strength). Washes the background towards Color, and can lighten it. */
	Blend,
	/** result = background + Color * Strength. Glow-like brightening. */
	Additive,
};

/**
 * UI element that can do post-processing effect on screen space.
 * Only valid on DreamUIRenderer (ScreenSpaceUI or WorldSpace-DreamUIRenderer).
 */
UCLASS(Abstract, NotBlueprintable)
class DREAMGUI_API UDreamVisualPostProcess : public UDreamVisual
{
	GENERATED_BODY()

public:
	DECLARE_EVENT_OneParam(UDreamVisualPostProcess, FRenderTargetChangedEvent, UTextureRenderTarget2D*);
	UDreamVisualPostProcess(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	TSharedPtr<FDreamUIGeometry> Geometry = nullptr;
	virtual void UpdateGeometry()override final;

	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;
	virtual void OnTransformChanged(bool InPositionChanged, bool InScaleChanged) override;
	virtual void MarkAllDirty()override;

protected:
	friend class FDreamVisualPostProcessCustomization;
	/** Use maskTexture's red channel to mask out effect result. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (DisplayThumbnail = "false"))
	TObjectPtr<UTexture2D> MaskTexture;
	/** MaskTexture UV offset and scale info. Only get good result when MaskTextureType is Simple */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FVector4 MaskTextureUVRect = FVector4(0, 0, 1, 1);
	/**
	 * Use root canvas size instead of just this UI element rect area.
	 * For screen-space-overlay UI, this will act as full screen size.
	 * For world-space-DreamUI, this will use root canvas size.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bUseFullSize = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "!bUseFullSize"))
	EDreamBackgroundBlurRenderType RenderType = EDreamBackgroundBlurRenderType::Screen;
	/**
	 * Blur result will output to this RenderTarget.
	 * Will create one if not specified.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(EditCondition="RenderType==EDreamBackgroundBlurRenderType::RenderTarget&&!bUseFullSize"))
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget = nullptr;
	/**
	 * How this visual's Color tints the captured background.
	 * Only the RGB of Color is used — its alpha keeps whatever meaning the effect gives it (background blur
	 * reads alpha as blur strength), so TintStrength controls how strongly the tint applies.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tint")
	EDreamPostProcessTintMode TintMode = EDreamPostProcessTintMode::Multiply;
	/** 0 leaves the background untouched in every mode; 1 applies the tint fully. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tint", meta = (ClampMin = 0.0, ClampMax = 1.0))
	float TintStrength = 1.0f;
	FRenderTargetChangedEvent OnRenderTargetChanged;
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamPostProcessTintMode GetTintMode()const { return TintMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetTintStrength()const { return TintStrength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTintMode(EDreamPostProcessTintMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTintStrength(float Value);

	FRenderTargetChangedEvent& GetRenderTargetChangedEvent(){return OnRenderTargetChanged;}
	
	FDreamUIGeometry* GetGeometry()const { return Geometry.Get(); }
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UTexture2D* GetMaskTexture()const { return MaskTexture; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const FVector4& GetMaskTextureUVRect()const { return MaskTextureUVRect; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamBackgroundBlurRenderType GetRenderType()const { return RenderType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UTextureRenderTarget2D* GetOutputRenderTarget()const { return OutputRenderTarget; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetUseFullSize()const{return bUseFullSize;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMaskTexture(UTexture2D* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMaskTextureUVRect(const FVector4& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRenderType(EDreamBackgroundBlurRenderType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetUseFullSize(bool Value);
public:
	void MarkVertexPositionDirty();
	void MarkUVDirty();
public:
	virtual FDreamVisualPostProcessRenderProxy* GetRenderProxy()PURE_VIRTUAL(UUIPostProcessRenderable::GetRenderProxy, return 0;);
	virtual bool HaveValidData()const;

	virtual bool LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const override;
private:
	/** local vertex position changed */
	uint8 bLocalVertexPositionChanged : 1;
	/** vertex's uv change */
	uint8 bUVChanged : 1;
protected:
	FDreamVisualPostProcessRenderProxy* RenderProxy = nullptr;
	/** update ui geometry */
	virtual void OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged);
	/** update region vertex data */
	virtual void UpdateRegionVertex();
	void UpdateGeometryClipData(FDreamUIGeometry& InMesh, int InDataStartPosition);
	TArray<FDreamUIPostProcessCopyMeshRegionVertex> RenderScreenToMeshRegionVertexArray;
	TArray<FDreamUIPostProcessVertex> RenderMeshRegionToScreenVertexArray;

	virtual void SendRegionVertexDataToRenderProxy();
	void SendMaskTextureToRenderProxy();
	void SendRenderTargetToRenderProxy();

	void UpdateRenderTarget();
};
