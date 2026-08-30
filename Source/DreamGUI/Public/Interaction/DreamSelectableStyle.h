// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/DreamUIImageBrush.h"
#include "DreamSelectableStyle.generated.h"

class USoundBase;
class UForceFeedbackEffect;

/** Reusable appearance shared by Button, Toggle, Slider and other selectable controls. */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamSelectableStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FColor NormalColor = FColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FColor HoveredColor = FColor(200, 200, 200, 255);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FColor PressedColor = FColor(150, 150, 150, 255);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FColor DisabledColor = FColor(150, 150, 150, 128);
	/**
	 * Give keyboard and gamepad focus a look of its own. Off, a focused control wears the Hovered
	 * visuals -- what every control authored before focus was a separate state already expects, and
	 * still the right answer for a UI that is only ever driven by a pointer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	bool bUseFocusedVisuals = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (EditCondition = "bUseFocusedVisuals"))
	FColor FocusedColor = FColor(220, 220, 255, 255);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush DisabledImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (EditCondition = "bUseFocusedVisuals"))
	FDreamUIImageBrush FocusedImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.0"))
	float AnimationDuration = 0.2f;

	/**
	 * Audio and rumble live on the style, not the instance: a click should sound like the OTHER
	 * clicks in this UI, which is a shared decision the same way its colors are. Navigation gets no
	 * slot of its own -- moving focus fires Enter on the target, so HoveredSound already plays once
	 * per move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> HoveredSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> PressedSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> ClickedSound;
	/** Rumble on click, for the gamepad the click came from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UForceFeedbackEffect> ClickedForceFeedback;
};
