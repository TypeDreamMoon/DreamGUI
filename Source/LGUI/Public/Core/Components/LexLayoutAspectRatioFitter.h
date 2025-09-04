// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutAspectRatioFitter.generated.h"

class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutAspectRatioFitterMode :uint8
{
	// The aspect ratio is not enforced
	None,
	// Changes the height of the rectangle to match the aspect ratio.
	WidthControlsHeight,
	// Changes the width of the rectangle to match the aspect ratio.
	HeightControlsWidth,
	// Sizes the rectangle such that it's fully contained within the parent rectangle.
	FitInParent,
	// Sizes the rectangle such that the parent rectangle is fully contained within.
	EnvelopeParent
};

UCLASS(BlueprintType, DisplayName="Aspect Ratio Fitter")
class LGUI_API ULexLayoutAspectRatioFitter : public ULexLayout
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutAspectRatioFitterMode AspectMode = ELexLayoutAspectRatioFitterMode::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	float AspectRatio;
	
	void UpdateSize();
	virtual void OnUpdateLayout() override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutAspectRatioFitterMode GetAspectMode()const{return AspectMode;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetAspectRatio()const{return AspectRatio;}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAspectMode(ELexLayoutAspectRatioFitterMode Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAspectRatio(float Value);
};
