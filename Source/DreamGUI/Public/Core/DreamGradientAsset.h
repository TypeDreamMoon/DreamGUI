// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamGradientAsset.generated.h"

/**
 * A gradient kept as an asset, for a project's shared looks: gold titles, a rarity's colours, a shimmer band. A text's
 * paint names one as its Preset (FDreamTextPaint) and paints with its gradient; a rich-text tag names one through a
 * custom style's entry. Editing it repaints nothing: every text painting with it keeps its gradient row, which is written
 * again (OnGradientChanged).
 *
 * Gradients a project would rather keep as text live in the project settings instead (UDreamGUISettings::GradientPresets).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Gradient"))
class DREAMGUI_API UDreamGradientAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Paint")
	const FDreamGradient& GetGradient() const { return Gradient; }
	/** Replace the gradient, and tell every text painting with it (OnGradientChanged). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Paint")
	void SetGradient(const FDreamGradient& InGradient);

	DECLARE_EVENT(UDreamGradientAsset, FDreamGradientAssetChangedEvent);
	/** Broadcast on the game thread when the gradient changed: SetGradient, an edit in the details panel, an undo. */
	FDreamGradientAssetChangedEvent& OnGradientChanged() { return GradientChangedEvent; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gradient")
	FDreamGradient Gradient;

private:
	FDreamGradientAssetChangedEvent GradientChangedEvent;
};
