// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexLayout.h"
#include "UIStandardControls.generated.h"

class ULexWidget;

UENUM(BlueprintType)
enum class EUIProgressBarFillType : uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIProgressChangedEvent, float, Percent);

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIProgressBar : public ULexUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Percent = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ProgressBar")
	TWeakObjectPtr<ULexWidget> FillWidget;
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
	void SetFillWidget(ULexWidget* Value) { FillWidget = Value; ApplyProgress(); }
	UFUNCTION(BlueprintPure, Category = "ProgressBar")
	ULexWidget* GetFillWidget()const { return FillWidget.Get(); }
	UFUNCTION(BlueprintCallable, Category = "ProgressBar")
	void SetIsMarquee(bool Value);
};

/** Desired-space widget equivalent to UMG Spacer. */
UCLASS(BlueprintType, DisplayName = "LayoutSelf-Spacer")
class LGUI_API ULexLayoutSelfSpacer : public ULexLayoutSelf
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacer", meta = (ClampMin = "0.0"))
	FVector2D Size = FVector2D(32.0, 32.0);
	virtual void CalculateSize() override;
	virtual FVector2f GetLayoutPreferredSize() override { return FVector2f(Size); }
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget) const override;
};
