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
 * Resizes LexWidget to fit a specified aspect ratio.
 */
UCLASS(BlueprintType, DisplayName="LayoutSelf-AspectRatio")
class LGUI_API ULexLayoutSelfAspectRatio : public ULexLayoutSelf
{
	GENERATED_BODY()
private:

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
	virtual FVector2f GetLayoutPreferredSize() override;
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	ELexLayoutAspectRatioType GetAspectRatioType()const{return AspectRatioType;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	float GetAspectRatio()const{return AspectRatio;}
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	void SetAspectRatioType(const ELexLayoutAspectRatioType& Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	void SetAspectRatio(float Value);
};
