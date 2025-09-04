// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidget.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
#include "LexLayout.generated.h"


struct FLexLayoutControlAnchorData
{
	bool bCanControlHorizontalAnchoredPosition = false;
	bool bCanControlVerticalAnchoredPosition = false;
	bool bCanControlHorizontalSizeDelta = false;
	bool bCanControlVerticalSizeDelta = false;

	bool HaveRepeatedControl(const FLexLayoutControlAnchorData& Other)const
	{
		if (
			(bCanControlHorizontalAnchoredPosition && Other.bCanControlHorizontalAnchoredPosition)
			|| (bCanControlVerticalAnchoredPosition && Other.bCanControlVerticalAnchoredPosition)
			|| (bCanControlHorizontalSizeDelta && Other.bCanControlHorizontalSizeDelta)
			|| (bCanControlVerticalSizeDelta && Other.bCanControlVerticalSizeDelta)
			)
		{
			return true;
		}
		return false;
	}
	void Or(const FLexLayoutControlAnchorData& Other)
	{
		bCanControlHorizontalAnchoredPosition |= Other.bCanControlHorizontalAnchoredPosition;
		bCanControlVerticalAnchoredPosition |= Other.bCanControlVerticalAnchoredPosition;
		bCanControlHorizontalSizeDelta |= Other.bCanControlHorizontalSizeDelta;
		bCanControlVerticalSizeDelta |= Other.bCanControlVerticalSizeDelta;
	}
	bool AnyControl()const
	{
		return bCanControlHorizontalAnchoredPosition || bCanControlVerticalAnchoredPosition
		|| bCanControlHorizontalSizeDelta || bCanControlVerticalSizeDelta;
	}
	bool Conflict(const FLexLayoutControlAnchorData& Other)const
	{
		if (bCanControlHorizontalAnchoredPosition && bCanControlHorizontalAnchoredPosition == Other.bCanControlHorizontalAnchoredPosition)
			return true;
		if (bCanControlVerticalAnchoredPosition && bCanControlVerticalAnchoredPosition == Other.bCanControlVerticalAnchoredPosition)
			return true;
		if (bCanControlHorizontalSizeDelta && bCanControlHorizontalSizeDelta == Other.bCanControlHorizontalSizeDelta)
			return true;
		if (bCanControlVerticalSizeDelta && bCanControlVerticalSizeDelta == Other.bCanControlVerticalSizeDelta)
			return true;
		return false;
	}
};

class ULexLayoutSlot;

UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayout : public ULexWidgetSubObjectBehaviour
	, public ILGUIPrefabInterface
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
#endif
	virtual void OnUpdateLayout() {};
	virtual void OnPreSavePrefab_Implementation() override;
public:
	void MarkLayoutDirty();
	
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};

	//called by LexWidget during layout processing
	void UpdateLayout();
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)PURE_VIRTUAL(ULexLayout::GetLayoutControlAnchor, return FLexLayoutControlAnchorData(););
	
	virtual float GetMinWidth()const{return 0;}
	virtual float GetPreferredWidth()const{return 0;}
	virtual float GetFlexibleWidth()const{return -1;}
	virtual float GetMinHeight()const{return 0;}
	virtual float GetPreferredHeight()const{return 0;}
	virtual float GetFlexibleHeight()const{return -1;}
};

UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutSlot : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	ULexWidget* GetWidget() const;
	virtual bool GetLayoutControlHorizontalPosition()const { return false; }
	virtual bool GetLayoutControlVerticalPosition()const { return false; }
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};

	virtual bool GetIgnoreLayout()const{return false;}
	virtual float GetMinWidth()const{return 0;}
	virtual float GetPreferredWidth()const{return 0;}
	virtual float GetFlexibleWidth()const{return -1;}
	virtual float GetMinHeight()const{return -1;}
	virtual float GetPreferredHeight()const{return -1;}
	virtual float GetFlexibleHeight()const{return -1;}
private:
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;
};
