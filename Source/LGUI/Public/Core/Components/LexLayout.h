// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidget.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
#include "LexLayout.generated.h"


struct FLGUICanLayoutControlAnchor
{
	bool bCanControlHorizontalAnchoredPosition = false;
	bool bCanControlVerticalAnchoredPosition = false;
	bool bCanControlHorizontalSizeDelta = false;
	bool bCanControlVerticalSizeDelta = false;

	bool HaveRepeatedControl(const FLGUICanLayoutControlAnchor& Other)const
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
	void Or(const FLGUICanLayoutControlAnchor& Other)
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
};

class ULexLayoutSlot;
/** Base class of UI element that can be renderred by LGUICanvas */
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
	UPROPERTY(VisibleAnywhere, Category = "Layout")
	TMap<TObjectPtr<const ULexWidget>, TObjectPtr<ULexLayoutSlot>> Slots;
	void CleanupSlots();
	virtual void OnPreSavePrefab_Implementation() override;
public:
	void MarkLayoutDirty();
	const ULexWidget* GetWidgetBySlot(const ULexLayoutSlot* Slot);
	
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	
	void UpdateLayout();
	
	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const PURE_VIRTUAL(ULexLayout::GetSlotClass, return nullptr;)
	virtual ULexLayoutSlot* GetSlot(const ULexWidget* Child)const;
	virtual ULexLayoutSlot* GetOrCreateSlot(const ULexWidget* Child, TSubclassOf<ULexLayoutSlot> SlotClass);

	virtual void OnChildDetached(const ULexWidget* Child);

	virtual void GetLayoutControlAnchor(ULexWidget* Widget, FLGUICanLayoutControlAnchor& Result){}
	
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
	void MarkLayoutDirty();
	ULexWidget* GetWidget() const;
	virtual bool GetLayoutControlWidth()const { return false; }
	virtual bool GetLayoutControlHeight()const { return false; }
	virtual bool GetLayoutControlHorizontalPosition()const { return false; }
	virtual bool GetLayoutControlVerticalPosition()const { return false; }
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};

	virtual float GetMinWidth()const{return 0;}
	virtual float GetPreferredWidth()const{return 0;}
	virtual float GetFlexibleWidth()const{return -1;}
	virtual float GetMinHeight()const{return -1;}
	virtual float GetPreferredHeight()const{return -1;}
	virtual float GetFlexibleHeight()const{return -1;}
private:
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;
};
