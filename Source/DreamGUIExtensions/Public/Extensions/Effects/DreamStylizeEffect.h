// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/DreamUIStylize.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "DreamStylizeEffect.generated.h"

/**
 * A stylize effect: its layers, in order, on what is behind it or on its own children -- a CRT screen, a glitch, chromatic
 * aberration, a halftone screen, ordered dithering, film grain, a vignette, posterization. Up to eight layers, mixed with
 * what they were made from by a blend mode and an opacity. What moves moves with the world's time.
 *
 * Drawn by DreamGUI's own renderer, as the adjustment layer is. The pixel sort stays the visual of its own it always was.
 */
UCLASS(ClassGroup = (DreamGUI), NotBlueprintable)
class DREAMGUIEXTENSIONS_API UDreamStylizeEffect : public UDreamVisualPostProcess
{
	GENERATED_BODY()

public:
	UDreamStylizeEffect(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const TArray<FDreamStylizeLayer>& GetLayers()const { return Layers; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetLayers(const TArray<FDreamStylizeLayer>& Value);
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

	/**
	 * Every glitch layer flares: InIntensity more intensity at once, fading to none over InDuration seconds of the world's
	 * time -- a hit taken, a signal lost.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void TriggerBurst(float InDuration = 0.4f, float InIntensity = 1.5f);

	/** What the layers pack to, as the shader takes them. */
	void PackLayers(FDreamUIStylizePacked& OutPacked)const;

	virtual FDreamVisualPostProcessRenderProxyPtr GetRenderProxy()override;
	virtual bool HaveValidData()const override;
	virtual FDreamUIImageEffectRef GetChildrenEffect()const override;
	virtual void MarkAllDirty()override;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	virtual void SendRegionVertexDataToRenderProxy()override;

	/** The layers, in order: each works on what the one before made. The first eight enabled ones are drawn. */
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetLayers", Category = "DreamGUI|Stylize")
	TArray<FDreamStylizeLayer> Layers;
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetOpacity", Category = "DreamGUI|Stylize", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetEffectBlendMode", Category = "DreamGUI|Stylize")
	EDreamUIBlendMode EffectBlendMode = EDreamUIBlendMode::Alpha;
	UPROPERTY(EditAnywhere, BlueprintSetter = "SetTarget", Category = "DreamGUI|Stylize")
	EDreamEffectTarget Target = EDreamEffectTarget::Backdrop;

private:
	void RefreshEffect();
	FDreamUIImageEffectRef Effect;
	float PackedAlpha = -1.0f;
	/** The burst TriggerBurst started: when, in the world's seconds, how long, how strong. */
	float BurstStart = -1.0f;
	float BurstDuration = 0.0f;
	float BurstIntensity = 0.0f;
};
