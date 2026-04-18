// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutContainerFlexBox.generated.h"

class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxDirectionType :uint8
{
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
	 */
	Stretch,
};

/**
 * This layout act like html/css3-flex layout
 */
UCLASS(BlueprintType, DisplayName="LayoutContainer-FlexBox")
class LGUI_API ULexLayoutContainerFlexBox : public ULexLayoutContainer
{
	GENERATED_BODY()
private:
	/**
	 * Specifies how items are placed in the container, by setting the direction of the container's primary axis.
	 * Direction defines the primary axis, if Direction is Horizontal or HorizontalReversed then secondary axis would be vertical.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxDirectionType Direction = ELexLayoutFlexBoxDirectionType::Horizontal;
	/**
	 * Controls whether the container is single-line or multi-line, and the direction of the secondary-axis, which determines the direction new lines are stacked in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxWrapType Wrap = ELexLayoutFlexBoxWrapType::NoWrap;
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

	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget)const override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void GetLayoutProperties(FVector2f& OutMin, FVector2f& OutMax, FVector2f& OutPreferred) override;

	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxDirectionType GetDirection()const{return Direction;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxWrapType GetWrap()const{return Wrap;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxPrimaryAxisAlignment GetPrimaryAlignment()const{return PrimaryAlignment;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxSecondaryAxisAlignment GetSecondaryAlignment()const{return SecondaryAlignment;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxSecondaryAxisLineAlignment GetSecondaryLineAlignment()const{return SecondaryLineAlignment;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetWidthGap()const{return WidthGap;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetHeightGap()const{return HeightGap;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FMargin GetPadding()const{return Padding;}
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetDirection(ELexLayoutFlexBoxDirectionType Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetWrap(ELexLayoutFlexBoxWrapType Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetPrimaryAlignment(ELexLayoutFlexBoxPrimaryAxisAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetSecondaryAlignment(ELexLayoutFlexBoxSecondaryAxisAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetSecondaryLineAlignment(ELexLayoutFlexBoxSecondaryAxisLineAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetWidthGap(float Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetHeightGap(float Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetPadding(FMargin Value);
};
