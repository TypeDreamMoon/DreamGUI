// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutFlexBoxContainer.generated.h"

class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxDirectionType :uint8
{
	None,
	Horizontal,
	HorizontalReverse,
	Vertical,
	VerticalReverse,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxWrapType :uint8
{
	NoWrap,
	Wrap,
	WrapReverse,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxPrimaryAxisAlignment :uint8
{
	Start,
	Center,
	End,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxSecondaryAxisAlignment :uint8
{
	Start,
	Center,
	End,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
	/**
	* Aligns a container's lines within the container when there is extra space in the secondary-axis, similar to how 'PrimaryAlignment' aligns individual items within the primary-axis.
	*/
	Stretch,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxSecondaryAxisLineAlignment :uint8
{
	Start,
	Center,
	End,
	/**
	 * Expand size to fill all area.
	 * Stretch only works when ControlChildSize is checked.
	 */
	Stretch,
};

/**
 * This layout act like html/css3-flex layout
 */
UCLASS(BlueprintType, DisplayName="FlexBox Container")
class LGUI_API ULexLayoutFlexBoxContainer : public ULexLayoutContainer
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalCustomization;
	/**
	 * Specifies how items are placed in the container, by setting the direction of the container's primary axis.
	 * Direction defines the primary axis, if Direction is Horizontal or HorizontalReversed then secondary axis would be vertical.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxDirectionType Direction = ELexLayoutFlexBoxDirectionType::None;
	/**
	 * Controls whether the container is single-line or multi-line, and the direction of the secondary-axis, which determines the direction new lines are stacked in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxWrapType Warp = ELexLayoutFlexBoxWrapType::NoWrap;
	/**
	 * Know as justify-content. Aligns items along the primary axis of the current line of the container.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxPrimaryAxisAlignment PrimaryAlignment = ELexLayoutFlexBoxPrimaryAxisAlignment::Start;
	/**
	 * Know as align-content. Aligns a container's lines within the container when there is extra space in the secondary-axis, similar to how 'PrimaryAlignment' aligns individual items within the primary-axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxSecondaryAxisAlignment SecondaryAlignment = ELexLayoutFlexBoxSecondaryAxisAlignment::Start;
	/**
	 * Know as align-items. Aligns items along the secondary-axis of the current line of the container.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxSecondaryAxisLineAlignment SecondaryLineAlignment = ELexLayoutFlexBoxSecondaryAxisLineAlignment::Start;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	float WidthGap = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	float HeightGap = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	FMargin Padding;
	
	virtual void UpdateLayout() override;

	struct FLineData
	{
		FLineData()
		{
			TotalMin = FVector2f::Zero();
			TotalMax = FVector2f::Zero();
			TotalPreferred = FVector2f::Zero();
			TotalGrow = FVector2f::Zero();
			TotalShrink = FVector2f::Zero();
		}
		TArray<ULexWidget*> Children;
		FVector2f TotalMin;
		FVector2f TotalMax;
		FVector2f TotalPreferred;
		FVector2f TotalGrow;
		FVector2f TotalShrink;
	};
	TArray<FLineData> LineDataArray;
	UPROPERTY(Transient)TArray<ULexWidget*> Children;
	FVector2f TotalMinSize;
	FVector2f TotalMaxSize;
	FVector2f TotalPreferredSize;
	float GetTotalMinSize(int Axis);
	float GetTotalMaxSize(int Axis);
	float GetTotalPreferredSize(int Axis);
	void SetChildPositionAndSize(ULexWidget* Child, FVector2f Pos, FVector2f AreaSize, int PrimaryAxis, int SecondaryAxis, float SecondaryPreferred, bool ReverseX, bool ReverseY);

	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget)const override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;

	float GetMinWidthPixelValue() {return GetTotalMinSize(0);}
	float GetMaxWidthPixelValue() {return GetTotalMaxSize(0);}
	float GetPreferredWidthPixelValue(){return GetTotalPreferredSize(0);}
	float GetPreferredHeightPixelValue(){return GetTotalPreferredSize(1);}
	float GetMinHeightPixelValue() {return GetTotalMinSize(1);}
	float GetMaxHeightPixelValue(){return GetTotalMaxSize(1);}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxDirectionType GetDirection()const{return Direction;}
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetDirection(ELexLayoutFlexBoxDirectionType Value);
};
