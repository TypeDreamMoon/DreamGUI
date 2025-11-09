// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutHorizontalAndVertical.h"
#include "LexLayoutCommonSlot.generated.h"

class ULexWidget;

// UENUM(BlueprintType)
// enum class ELexLayoutSlotSizeType : uint8
// {
// 	/** Default value from widget's visual or layout */
// 	Auto,
// 	/** Fixed pixel value */
// 	Fixed,
// 	/** Percentage relative it's parent size */
// 	Percent,
// };
//
// UENUM(BlueprintType)
// enum class ELexLayoutSlotAspectRatioType : uint8
// {
// 	None,
// 	WidthControlHeight,
// 	HeightControlWidth,
// };

UCLASS(BlueprintType, DisplayName="Common Slot")
class LGUI_API ULexLayoutCommonSlot : public ULexLayoutSlot, public ILexLayoutHorizontalAndVerticalSlotInterface
{
	GENERATED_BODY()
private:
	friend class FLexLayoutCommonSlotCustomization;
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
	virtual void PostInitProperties() override;
#endif
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	virtual bool GetIgnoreLayout()const override{return bIgnoreLayout;}
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
