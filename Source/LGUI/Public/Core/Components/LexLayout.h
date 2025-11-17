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

/**
 * LayoutContainer can handle children position
 */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutContainer : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
public:
	
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};

	//called by LexWidget during layout processing
	virtual void UpdateLayout(){}
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)const PURE_VIRTUAL(ULexLayout::GetLayoutControlAnchor, return FLexLayoutControlAnchorData(););
};

/**
 * LayoutSelf can handle self size
 */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutSelf : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LayoutSelf", Getter="GetIgnoreLayoutContainer", Setter="SetIgnoreLayoutContainer", meta = (AllowPrivateAccess = true))
	bool bIgnoreLayoutContainer = false;
public:
	static FName GetPropertyName_IgnoreLayout(){return GET_MEMBER_NAME_CHECKED(ULexLayoutSelf, bIgnoreLayoutContainer);}
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	/** Called by LexWidget to calculate size */
	virtual void UpdateLayout(){}

	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)const PURE_VIRTUAL(ULexLayoutSelf::GetLayoutControlAnchor, return FLexLayoutControlAnchorData(););

	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual bool GetIgnoreLayoutContainer()const{return bIgnoreLayoutContainer;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual void SetIgnoreLayoutContainer(bool Value);
};
