// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/DreamLayerStyleTypes.h"
#include "DreamLayerStylePreset.generated.h"

/** A layer's blend mode and opacity, as an image editor's style keeps them when it includes the layer's blending options. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLayerStyleBlending
{
	GENERATED_BODY()

	/** The element's blend mode (its visual's BlendMode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending")
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	/** The widget's opacity (RenderOpacity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blending", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.0f;
};

/**
 * A layer style kept as an asset: an image editor's styles panel, for a project's shared looks -- a button's bevel, a title's
 * glow. A layer style that names it as its preset draws it, and follows every edit of it (OnStyleChanged).
 *
 * It can keep the layer's blending too (bIncludeBlending): applying the preset to a widget -- from the designer's preset
 * library -- then also sets the widget's blend mode and opacity, once. A layer style following the preset only ever draws
 * its effects.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Layer Style Preset"))
class DREAMGUI_API UDreamLayerStylePreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	const FDreamLayerEffects& GetStyle() const { return Style; }
	/** Replace the style, and tell every layer style drawing it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Layer Style")
	void SetStyle(const FDreamLayerEffects& InStyle);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	bool IncludesBlending() const { return bIncludeBlending; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layer Style")
	const FDreamLayerStyleBlending& GetBlending() const { return Blending; }
	/** Keep the layer's blending with the style, or stop keeping it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Layer Style")
	void SetBlending(bool bInInclude, const FDreamLayerStyleBlending& InBlending);

	DECLARE_EVENT(UDreamLayerStylePreset, FDreamLayerStylePresetChangedEvent);
	/** Broadcast on the game thread when the style changed: SetStyle, an edit in the details panel, an undo. */
	FDreamLayerStylePresetChangedEvent& OnStyleChanged() { return StyleChangedEvent; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layer Style")
	FDreamLayerEffects Style;
	/** Whether applying the preset also sets the widget's blend mode and opacity (Blending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blending", meta = (DisplayName = "Include Layer Blending Options"))
	bool bIncludeBlending = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blending", meta = (EditCondition = "bIncludeBlending"))
	FDreamLayerStyleBlending Blending;

private:
	FDreamLayerStylePresetChangedEvent StyleChangedEvent;
};
