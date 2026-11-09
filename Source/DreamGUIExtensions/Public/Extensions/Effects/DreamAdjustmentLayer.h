// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/DreamUIAdjustment.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamAdjustmentLayer.generated.h"

/** What an adjustment layer or a stylize effect works on. */
UENUM(BlueprintType)
enum class EDreamEffectTarget : uint8
{
	/** What is behind it, as far as its rect, its clips and its masks cover. */
	Backdrop,
	/** Its own widget's subtree, drawn as a group whose picture goes through the effect before it is put down. */
	Children,
};

/**
 * An adjustment layer: its operations, in order, on what is behind it -- the colours under its rect, as far as its clips
 * and their masks let it -- or on its own children. Brightness and contrast, levels, curves, exposure, hue and saturation,
 * vibrance, colour balance, black and white, a photo filter, a channel mixer, invert, posterize, threshold, a gradient map
 * and a lookup table; up to eight of them, mixed with what they were made from by a blend mode and an opacity.
 *
 * Drawn by DreamGUI's own renderer: screen-space, render-target and world-space (DreamUI renderer) canvases. On a canvas
 * the engine's renderer draws, nothing. The post process's Tint and output render target are not used.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUIEXTENSIONS_API UDreamAdjustmentLayer : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:
	UDreamAdjustmentLayer(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const TArray<FDreamAdjustment>& GetAdjustments()const { return Adjustments; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetAdjustments(const TArray<FDreamAdjustment>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetOpacity()const { return Opacity; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetOpacity(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamUIBlendMode GetEffectBlendMode()const { return EffectBlendMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetEffectBlendMode(EDreamUIBlendMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamEffectTarget GetTarget()const { return Target; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetTarget(EDreamEffectTarget Value);

	/** What the operations pack to, as the shader takes them: for a test, or a tool, to look at. */
	void PackAdjustments(FDreamUIAdjustPacked& OutPacked)const;

	virtual FDreamVisualPostProcessRenderProxyPtr GetRenderProxy()override;
	virtual bool HaveValidData()const override;
	virtual FDreamUIImageEffectRef GetChildrenEffect()const override;
	virtual void MarkAllDirty()override;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	virtual void OnEnabledChanged() override;
	virtual void SendRegionVertexDataToRenderProxy()override;

	/** The operations, in order: each works on what the one before made. The first eight enabled ones are drawn. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetAdjustments", Category = "DreamGUI|Adjustment")
	TArray<FDreamAdjustment> Adjustments;
	/** How much of what the operations made replaces what they were made from; times the visual's own alpha. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetOpacity", Category = "DreamGUI|Adjustment", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
	/** How what the operations made is blended with what they were made from: every mode, by the same formulas as an element's. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetEffectBlendMode", Category = "DreamGUI|Adjustment")
	EDreamUIBlendMode EffectBlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetTarget", Category = "DreamGUI|Adjustment")
	EDreamEffectTarget Target = EDreamEffectTarget::Backdrop;

private:
	/** The operations made into an effect again, and sent where it goes: the proxy, or the canvas's group. */
	void RefreshEffect();
	FDreamUIImageEffectRef Effect;
	float PackedAlpha = -1.0f;
};
