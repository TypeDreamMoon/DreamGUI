// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutHorizontalAndVertical.generated.h"

class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutDirection :uint8
{
	Horizontal,
	Vertical,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutChildAlignment :uint8
{
	UpperLeft,
	UpperCenter,
	UpperRight,
	MiddleLeft,
	MiddleCenter,
	MiddleRight,
	LowerLeft,
	LowerCenter,
	LowerRight,
};

USTRUCT(BlueprintType)
struct FLexLayoutHorizontalAndVerticalSizeControl
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", DisplayName = "Width")
	bool bWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", DisplayName = "Height")
	bool bHeight = false;

	bool operator==(const FLexLayoutHorizontalAndVerticalSizeControl& Other) const
	{
		return bWidth == Other.bWidth && bHeight == Other.bHeight;
	}
	bool operator!=(const FLexLayoutHorizontalAndVerticalSizeControl& Other) const
	{
		return bWidth != Other.bWidth || bHeight != Other.bHeight;
	}
	bool operator[](int Axis) const
	{
		return Axis == 0 ? bWidth : (Axis == 1 ? bHeight : false);
	}
};

UCLASS(BlueprintType, DisplayName="Horizontal & Vertical")
class LGUI_API ULexLayoutHorizontalAndVertical : public ULexLayout
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutDirection Direction = ELexLayoutDirection::Horizontal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	float Spacing = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutChildAlignment ChildAlignment = ELexLayoutChildAlignment::UpperLeft;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter="GetReverseArrangement", Setter="SetReverseArrangement", meta = (AllowPrivateAccess = true))
	bool bReverseArrangement = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl ControlChildSize;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl UseChildScale;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl ChildForceExpand;
	
	virtual void OnUpdateLayout() override;

	void CalcAlongAxis(int Axis, bool bIsVertical);
	float GetStartOffset(int Axis, float RequiredSpaceWithoutPadding);
	float GetAlignmentOnAxis(int Axis);
	void SetLayoutInputForAxis(float TotalMin, float TotalPreferred, float TotalFlexible, int Axis);
	void SetChildrenAlongAxis(int Axis, bool isVertical);
	void GetChildSizes(ULexWidget* ChildWidget, int Axis, bool bControlSize, bool bChildForceExpand,
			float& OutMin, float& OutPreferred, float& OutFlexible);
	void SetChildAlongAxisWithScale(ULexWidget* ChildWidget, int Axis, float Pos, float Size, float ScaleFactor);
	void SetChildAlongAxisWithScale(ULexWidget* ChildWidget, int Axis, float Pos, float ScaleFactor);

	FVector2f TotalMinSize = FVector2f::Zero();
	FVector2f TotalPreferredSize = FVector2f::Zero();
	FVector2f TotalFlexibleSize = FVector2f::Zero();
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

	virtual void GetLayoutControlAnchor(ULexWidget* TargetWidget, FLGUICanLayoutControlAnchor& Result) override;
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
	ELexLayoutDirection GetDirection()const{return Direction;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FMargin GetPadding()const{return Padding;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	float GetSpacing()const{return Spacing;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutChildAlignment GetChildAlignment()const{return ChildAlignment;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool GetReverseArrangement()const{return bReverseArrangement;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexLayoutHorizontalAndVerticalSizeControl GetControlChildSize()const{return ControlChildSize;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexLayoutHorizontalAndVerticalSizeControl GetUseChildScale()const{return UseChildScale;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexLayoutHorizontalAndVerticalSizeControl GetChildForceExpand()const{return ChildForceExpand;}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetDirection(ELexLayoutDirection Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetPadding(FMargin Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetSpacing(float Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetChildAlignment(ELexLayoutChildAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetReverseArrangement(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetControlChildSize(FLexLayoutHorizontalAndVerticalSizeControl Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetUseChildScale(FLexLayoutHorizontalAndVerticalSizeControl Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetChildForceExpand(FLexLayoutHorizontalAndVerticalSizeControl Value);
};

UCLASS(BlueprintType, DisplayName="Horizontal & Vertical Slot")
class LGUI_API ULexLayoutHorizontalAndVerticalSlot : public ULexLayoutSlot
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
	virtual bool GetLayoutControlWidth() const override;
	virtual bool GetLayoutControlHeight() const override;
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
