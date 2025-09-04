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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexLayoutHorizontalAndVerticalSizeControl SizeFitToChildren;
	
	virtual void OnUpdateLayout() override;

	void CalcAlongAxis(int Axis, bool bIsVertical);
	float GetStartOffset(int Axis, float RequiredSpaceWithoutPadding);
	float GetAlignmentOnAxis(int Axis);
	void SetLayoutInputForAxis(float TotalMin, float TotalPreferred, float TotalFlexible, int Axis);
	void SetChildrenAlongAxis(int Axis, bool isVertical);
	void SetSelfAlongAxis(int Axis);
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

	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget) override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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
	FLexLayoutHorizontalAndVerticalSizeControl GetSizeFitToChildren()const{return SizeFitToChildren;}

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
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetSizeFitToChildren(FLexLayoutHorizontalAndVerticalSizeControl Value);
};

UINTERFACE(Blueprintable, MinimalAPI)
class ULexLayoutHorizontalAndVerticalSlotInterface : public UInterface
{
	GENERATED_BODY()
};

class ILexLayoutHorizontalAndVerticalSlotInterface
{
	GENERATED_BODY()
public:
	virtual bool GetIgnoreLayout()const = 0;
	
	virtual float GetMinWidth()const = 0;
	virtual float GetMinHeight()const = 0;
	virtual float GetPreferredWidth()const = 0;
	virtual float GetPreferredHeight()const = 0;
	virtual float GetFlexibleWidth()const = 0;
	virtual float GetFlexibleHeight()const = 0;
};
