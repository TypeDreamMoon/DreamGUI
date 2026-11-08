// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/DreamLayerStyleTypes.h"
#include "DreamLayerStylePreset.generated.h"

/**
 * A layer style kept as an asset: an image editor's styles panel, for a project's shared looks -- a button's bevel, a title's
 * glow. A layer style that names it as its preset draws it, and follows every edit of it (OnStyleChanged).
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

private:
	FDreamLayerStylePresetChangedEvent StyleChangedEvent;
};
