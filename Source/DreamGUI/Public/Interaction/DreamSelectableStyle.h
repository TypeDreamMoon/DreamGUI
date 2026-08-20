// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/DreamUIImageBrush.h"
#include "DreamSelectableStyle.generated.h"

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FDreamUIImageBrush DisabledImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.0"))
	float AnimationDuration = 0.2f;
};
