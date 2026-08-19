// Copyright 2025-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutSelfAspectRatio.generated.h"

class ULexWidget;

UENUM(BlueprintType)
enum class ELexLayoutAspectRatioType : uint8
{
	/**
	 * Unlock aspect ratio
	 */
	None,
	/**
	 * Width control height
	 */
	WidthControlHeight,
	/**
	 * Height control width
	 */
	HeightControlWidth,
	/**
	 * Sizes the rectangle such that it's fully contained within the parent rectangle.
	 */
	FitInParent,
	/**
	 * Sizes the rectangle such that the parent rectangle is fully contained within.
	 */
	EnvelopeParent,
};

/**
 * Everything an aspect-ratio pass decides, before any of it reaches the widget.
 *
 * Solving and applying are split because they are asked for by different callers. A parent panel
 * measuring a child wants only PreferredSize, and measuring must not move or resize the thing being
 * measured - GetLayoutPreferredSize used to run the whole write pass to produce that one number.
 * The apply flags carry the per-axis yields: whichever axes the parent layout already controls are
 * left alone, so the ratio simply does not apply on those axes rather than fighting for them.
 */
struct FLexAspectRatioSolution
{
	FVector2f PreferredSize = FVector2f::ZeroVector;

	bool bApplyWidth = false;
	float Width = 0.0f;
	bool bApplyHeight = false;
	float Height = 0.0f;

	bool bApplyPosition = false;
	FVector2D AnchoredPosition = FVector2D::ZeroVector;
	bool bApplySizeDelta = false;
	FVector2D SizeDelta = FVector2D::ZeroVector;

	/** Editor-only: the None mode derives the ratio back from the authored size. */
	bool bAdoptRatioFromWidget = false;
	float AdoptedRatio = 1.0f;
};

/**
 * Resizes LexWidget to fit a specified aspect ratio.
 */
UCLASS(BlueprintType, DisplayName="LayoutSelf-AspectRatio")
class LGUI_API ULexLayoutSelfAspectRatio : public ULexLayoutSelf
{
	GENERATED_BODY()
private:
	/** Pure: reads the widget and its parent, writes nothing. */
	FLexAspectRatioSolution Solve() const;


	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSelf", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutAspectRatioType AspectRatioType = ELexLayoutAspectRatioType::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSelf", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="AspectRatioType!=ELexLayoutAspectRatioType::None", ClampMin="0.0001"))
	float AspectRatio = 1.0f;

	bool bIsCalculatingSize = false;
	FVector2f CalculatedPreferred;
public:
	virtual void OnTransformChanged() override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void CalculateSize() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;
#endif
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)const override;
	virtual FVector2f GetLayoutPreferredSize() const override;
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	ELexLayoutAspectRatioType GetAspectRatioType()const{return AspectRatioType;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	float GetAspectRatio()const{return AspectRatio;}
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	void SetAspectRatioType(const ELexLayoutAspectRatioType& Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	void SetAspectRatio(float Value);
};
