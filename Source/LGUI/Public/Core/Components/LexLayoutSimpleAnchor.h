// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutFlexBox.h"
#include "Core/LexWidgetTypes.h"
#include "LexLayoutSimpleAnchor.generated.h"

class ULexWidget;

UCLASS(BlueprintType, DisplayName="Simple Anchor")
class LGUI_API ULexLayoutSimpleAnchor : public ULexLayout
{
	GENERATED_BODY()

protected:
	virtual void OnUpdateLayout() override;
public:
	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const override;
	virtual bool SupportShrinkToChildrenWidth() override{return false;}
	virtual bool SupportShrinkToChildrenHeight() override{return false;}
};

UCLASS(BlueprintType, DisplayName="Simple Anchor Slot")
class LGUI_API ULexLayoutSimpleAnchorSlot : public ULexLayoutSlot
{
	GENERATED_BODY()
private:
	friend class FLexLayoutSimpleAnchorSlotCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutHorizontalAlignment HorizontalAlignment = ELexLayoutHorizontalAlignment::Center;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true, Horizontal))
	FLexWidgetOffset HorizontalOffset = FLexWidgetOffset(ELexWidgetOffsetType::Fixed, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutVerticalAlignment VerticalAlignment = ELexLayoutVerticalAlignment::Middle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true, Vertical))
	FLexWidgetOffset VerticalOffset = FLexWidgetOffset(ELexWidgetOffsetType::Fixed, 0, 0);
public:
	void UpdateChildLayout();
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	ELexLayoutHorizontalAlignment GetHorizontalAlignment()const { return HorizontalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	ELexLayoutVerticalAlignment GetVerticalAlignment()const { return VerticalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FLexWidgetOffset GetHorizontalOffset()const { return HorizontalOffset; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FLexWidgetOffset GetVerticalOffset()const { return VerticalOffset; }
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetVerticalAlignment(ELexLayoutVerticalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalOffset(FLexWidgetOffset Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetVerticalOffset(FLexWidgetOffset Value);
};
