// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamLayout.h"
#include "DreamLayoutSelfSpacer.generated.h"

class UDreamWidget;

/** Desired-space widget equivalent to UMG Spacer. */
UCLASS(BlueprintType, DisplayName = "LayoutSelf-Spacer")
class DREAMGUI_API UDreamLayoutSelfSpacer : public UDreamLayoutSelf
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacer", meta = (ClampMin = "0.0"))
	FVector2D Size = FVector2D(32.0, 32.0);
	virtual void CalculateSize() override;
	virtual FVector2f GetLayoutPreferredSize() const override { return FVector2f(Size); }
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* Widget) const override;
};
