// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "LexLayout.generated.h"

class ULexLayoutSlot;
/** Base class of UI element that can be renderred by LGUICanvas */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayout : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	virtual void OnUpdateLayout() {};

public:
	virtual void OnTransformChanged()override;
	
	void UpdateLayout();
	void MarkLayoutDirty(){bIsLayoutDirty = true;}
	
	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const PURE_VIRTUAL(ULexLayout::GetSlotClass, return nullptr;)

	virtual bool SupportShrinkToChildrenWidth(){return false;}
	virtual bool SupportShrinkToChildrenHeight(){return false;}
	virtual float GetShrinkToChildrenWidth(){return 0;}
	virtual float GetShrinkToChildrenHeight(){return 0;}
private:
	bool bIsLayoutDirty = true;
};

UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutSlot : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual bool GetLayoutControlWidth()const { return false; }
	virtual bool GetLayoutControlHeight()const { return false; }
	virtual bool GetLayoutControlHorizontalPosition()const { return false; }
	virtual bool GetLayoutControlVerticalPosition()const { return false; }
	
	virtual void CalculateTransformFromLayout(){};
};
