// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutHorizontalAndVertical.h"
#include "LexLayoutFlexBox.generated.h"

class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutFlexBoxDirection :uint8
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
enum class ELexLayoutFlexBoxAlignment :uint8
{
	Start,
	Center,
	End,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
};

UCLASS(BlueprintType, DisplayName="FlexBox")
class LGUI_API ULexLayoutFlexBox : public ULexLayout
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxDirection Direction = ELexLayoutFlexBoxDirection::Horizontal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxWrapType Warp = ELexLayoutFlexBoxWrapType::NoWrap;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxAlignment PrimaryAlignment = ELexLayoutFlexBoxAlignment::Start;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	ELexLayoutFlexBoxAlignment SecondaryAlignment = ELexLayoutFlexBoxAlignment::Start;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl ControlChildSize;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl SizeFitToChildren;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	float WidthGap = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	float HeightGap = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = true, UIMin=0))
	FMargin Padding;
	
	virtual void OnUpdateLayout() override;

	struct FLineData
	{
		FLineData()
		{
			TotalMin = FVector2f::Zero();
			TotalPreferred = FVector2f::Zero();
			TotalFlexible = FVector2f::Zero();
		}
		TArray<ULexWidget*> Children;
		FVector2f TotalMin;
		FVector2f TotalPreferred;
		FVector2f TotalFlexible;
	};
	TArray<FLineData> LineDataArray;
	
	FVector2f TotalMinSize;
	FVector2f TotalPreferredSize;
	FVector2f TotalFlexibleSize;
	UPROPERTY(Transient)TArray<ULexWidget*> Children;
	float GetTotalMinSize(int axis)const
	{
		return TotalMinSize[axis];
	}
	float GetTotalPreferredSize(int axis)const
	{
		return TotalPreferredSize[axis];
	}
	float GetTotalFlexibleSize(int axis)const
	{
		return TotalFlexibleSize[axis];
	}
	void SetChildPositionAndSize(ULexWidget* Child, FVector2f Pos, FVector2f Size, bool ReverseX, bool ReverseY);

	virtual void GetLayoutControlAnchor(ULexWidget* TargetWidget, FLexLayoutControlAnchorData& Result) override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const override;

	virtual float GetMinWidth()const override{return GetTotalMinSize(0);}
	virtual float GetPreferredWidth()const override{return GetTotalPreferredSize(0);}
	virtual float GetFlexibleWidth()const override{return GetTotalFlexibleSize(0);}
	virtual float GetMinHeight()const override{return GetTotalMinSize(1);}
	virtual float GetPreferredHeight()const override{return GetTotalPreferredSize(1);}
	virtual float GetFlexibleHeight()const override{return GetTotalFlexibleSize(1);}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutFlexBoxDirection GetDirection()const{return Direction;}
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetDirection(ELexLayoutFlexBoxDirection Value);
};

UCLASS(BlueprintType, DisplayName="FlexBox Slot")
class LGUI_API ULexLayoutFlexBoxSlot : public ULexLayoutSlot
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalSlotCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter="GetIgnoreLayout", Setter="SetIgnoreLayout", meta = (AllowPrivateAccess = true))
	bool bIgnoreLayout = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float MinWidth = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float MinHeight = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float PreferredWidth = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float PreferredHeight = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float FlexibleWidth = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	float FlexibleHeight = -1;

public:
	virtual void OnTransformChanged() override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	bool GetIgnoreLayout()const{return bIgnoreLayout;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetIgnoreLayout(bool Value);
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetMinWidth()const override{return MinWidth;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetMinHeight()const override{return MinHeight;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetPreferredWidth()const override{return PreferredWidth;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetPreferredHeight()const override{return PreferredHeight;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetFlexibleWidth()const override{return FlexibleWidth;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual float GetFlexibleHeight()const override{return FlexibleHeight;}

	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetMinWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetMinHeight(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetPreferredWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetPreferredHeight(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetFlexibleWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetFlexibleHeight(float Value);
};
