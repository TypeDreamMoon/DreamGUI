// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidget.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "LexLayout.generated.h"


struct FLexLayoutControlAnchorData
{
	bool bCanControlHorizontalPosition = false;
	bool bCanControlVerticalPosition = false;
	bool bCanControlHorizontalSize = false;
	bool bCanControlVerticalSize = false;

	bool HaveRepeatedControl(const FLexLayoutControlAnchorData& Other)const
	{
		if (
			(bCanControlHorizontalPosition && Other.bCanControlHorizontalPosition)
			|| (bCanControlVerticalPosition && Other.bCanControlVerticalPosition)
			|| (bCanControlHorizontalSize && Other.bCanControlHorizontalSize)
			|| (bCanControlVerticalSize && Other.bCanControlVerticalSize)
			)
		{
			return true;
		}
		return false;
	}
	void Or(const FLexLayoutControlAnchorData& Other)
	{
		bCanControlHorizontalPosition |= Other.bCanControlHorizontalPosition;
		bCanControlVerticalPosition |= Other.bCanControlVerticalPosition;
		bCanControlHorizontalSize |= Other.bCanControlHorizontalSize;
		bCanControlVerticalSize |= Other.bCanControlVerticalSize;
	}
	bool AnyControl()const
	{
		return bCanControlHorizontalPosition || bCanControlVerticalPosition
		|| bCanControlHorizontalSize || bCanControlVerticalSize;
	}
	bool Conflict(const FLexLayoutControlAnchorData& Other)const
	{
		if (bCanControlHorizontalPosition && bCanControlHorizontalPosition == Other.bCanControlHorizontalPosition)
			return true;
		if (bCanControlVerticalPosition && bCanControlVerticalPosition == Other.bCanControlVerticalPosition)
			return true;
		if (bCanControlHorizontalSize && bCanControlHorizontalSize == Other.bCanControlHorizontalSize)
			return true;
		if (bCanControlVerticalSize && bCanControlVerticalSize == Other.bCanControlVerticalSize)
			return true;
		return false;
	}
};

UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayout : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	virtual void OnTransformChanged() {}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) {}
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)const PURE_VIRTUAL(ULexLayout::GetLayoutControlAnchor, return FLexLayoutControlAnchorData(););

	virtual FVector2f GetLayoutPreferredSize()PURE_VIRTUAL(ULexLayout::GetLayoutProperties, return FVector2f::ZeroVector;);
	virtual void MarkLayoutDirty();
	bool IsCalculatingLayout()const{return LayoutCalculateCount > 0;}
	constexpr static int32 MaxLayoutCalculateCount = 2;
protected:
	int LayoutCalculateCount = 0;
};

/**
 * LayoutContainer can handle children position
 */
UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutContainer : public ULexLayout
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostReinitProperties()override;

	//called by LexWidget during layout processing
	virtual void CalculateLayout(){}
};

/**
 * LayoutSelf can handle self size.
 * This base class just provide IgnoreLayout.
 */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, DisplayName="LayoutSelf-IgnoreLayout")
class LGUI_API ULexLayoutSelf : public ULexLayout
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSelf", Getter="GetIgnoreLayoutContainer", Setter="SetIgnoreLayoutContainer", meta = (AllowPrivateAccess = true))
	bool bIgnoreLayoutContainer = false;
public:
	static FName GetPropertyName_IgnoreLayout(){return GET_MEMBER_NAME_CHECKED(ULexLayoutSelf, bIgnoreLayoutContainer);}
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostReinitProperties()override;

	//called by LexWidget during layout processing
	virtual void CalculateSize(){}
	
	virtual FVector2f GetLayoutFinalSize();
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget) const override{return FLexLayoutControlAnchorData();}

	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual bool GetIgnoreLayoutContainer()const{return bIgnoreLayoutContainer;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual void SetIgnoreLayoutContainer(bool Value);
};
