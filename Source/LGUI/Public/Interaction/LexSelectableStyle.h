// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/LexUIImageBrush.h"
#include "LexSelectableStyle.generated.h"

/** Reusable appearance shared by Button, Toggle, Slider and other selectable controls. */
UCLASS(BlueprintType)
class LGUI_API ULexSelectableStyle : public UDataAsset
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
	FLexUIImageBrush NormalImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLexUIImageBrush HoveredImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLexUIImageBrush PressedImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLexUIImageBrush DisabledImageBrush;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.0"))
	float AnimationDuration = 0.2f;
};
