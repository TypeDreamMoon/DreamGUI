// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "UIStandardControls.generated.h"

class UDreamWidget;

UENUM(BlueprintType)
enum class EUIProgressBarFillType : uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIProgressChangedEvent, float, Percent);

/**
 * SUPERSEDED by UDreamProgressBar (`Native.ProgressBar`). Kept for the assets that use it.
 *
 * The difference is not features, it is who owns the parts. This is a component you drop on a
 * widget whose track and fill you then build and wire yourself; the control builds both, reads
 * its look from the project style sheet, and offers a second SHAPE -- Radial, which is also
 * where a throbber comes from -- that a fill-widget pointer cannot express.
 *
 * UDreamProgressBar deliberately does not compose this class. A behaviour earns its place by
 * owning interaction or per-frame work, and a plain bar has neither; the control writing its own
 * fill is smaller than handing a part to a component whose other features it then has to hide.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIProgressBar : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Percent = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar")
	TWeakObjectPtr<UDreamWidget> FillWidget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar")
	EUIProgressBarFillType FillType = EUIProgressBarFillType::LeftToRight;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar")
	bool bIsMarquee = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar", meta = (EditCondition = "bIsMarquee", ClampMin = "0.01", ClampMax = "1.0"))
	float MarqueeWidth = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar", meta = (EditCondition = "bIsMarquee", ClampMin = "0.0"))
	float MarqueeSpeed = 0.75f;
	UPROPERTY(BlueprintAssignable, Category = "ProgressBar")
	FUIProgressChangedEvent OnPercentChanged;
	float MarqueeOffset = 0.0f;
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	void ApplyProgress();

public:
	UFUNCTION(BlueprintCallable, Category = "ProgressBar")
	void SetPercent(float Value);
	UFUNCTION(BlueprintPure, Category = "ProgressBar")
	float GetPercent()const { return Percent; }
	UFUNCTION(BlueprintCallable, Category = "ProgressBar")
	void SetFillWidget(UDreamWidget* Value) { FillWidget = Value; ApplyProgress(); }
	UFUNCTION(BlueprintPure, Category = "ProgressBar")
	UDreamWidget* GetFillWidget()const { return FillWidget.Get(); }
	UFUNCTION(BlueprintCallable, Category = "ProgressBar")
	void SetFillType(EUIProgressBarFillType Value);
	UFUNCTION(BlueprintPure, Category = "ProgressBar")
	EUIProgressBarFillType GetFillType()const { return FillType; }
	UFUNCTION(BlueprintCallable, Category = "ProgressBar")
	void SetIsMarquee(bool Value);
};

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
