// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LTweener.h"
#include "LexLayoutAnchor.generated.h"

class ULexLayoutSlot;
UCLASS(BlueprintType)
class LGUI_API ULexLayoutAnchor : public ULexLayout
{
	GENERATED_BODY()

public:	
	ULexLayoutAnchor();

private:
	virtual void BeginPlay()override{};
	virtual void EndPlay()override{};
	virtual void OnRegister()override{};
	virtual void OnUnregister()override{};

	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const;

#if WITH_EDITORONLY_DATA
	/** This is nothing but just a place-holder to display Layout in editor */
	UPROPERTY(VisibleAnywhere, Category = "Layout")
	FString PlaceHolder;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	virtual void OnUpdateLayout()override;
};

UCLASS(BlueprintType)
class LGUI_API ULexLayoutAnchorSlot : public ULexLayoutSlot
{
	GENERATED_BODY()

public:	
	ULexLayoutAnchorSlot();

private:
	UPROPERTY(EditAnywhere, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	FVector2D AnchorMin = FVector2D(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	FVector2D AnchorMax = FVector2D(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	FVector2D AnchoredPosition = FVector2D(0, 0);
	UPROPERTY(EditAnywhere, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	FVector2D SizeDelta = FVector2D(100, 100);

	mutable float CacheWidth = 0, CacheHeight = 0, CacheAnchorLeft = 0, CacheAnchorRight = 0, CacheAnchorTop = 0, CacheAnchorBottom = 0;
	mutable uint8 bWidthCached : 1, bHeightCached : 1, bAnchorLeftCached : 1, bAnchorRightCached : 1, bAnchorTopCached : 1, bAnchorBottomCached : 1;
	FVector2f PrevScale2D = FVector2f::One();
	
	uint8 bCanSetAnchorFromTransform : 1;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;

	bool IsHorizontalStretched()const { return AnchorMin.X != AnchorMax.X; }
	bool IsVerticalStretched()const { return AnchorMin.Y != AnchorMax.Y; }

	virtual void OnParentTransformChanged()override;
	virtual void OnParentDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)override;

	virtual bool GetLayoutControlWidth() const override{return true;}
	virtual bool GetLayoutControlHeight() const override{return true;}
	virtual bool GetLayoutControlHorizontalPosition() const override{return true;}
	virtual bool GetLayoutControlVerticalPosition() const override{return true;}

	virtual void CalculateTransformFromLayout() override;
	
	void CalculateAnchorFromTransform();
	void CalculateTransformFromAnchor();
	void CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged);

public:
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetWidth() const;
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetHeight() const;
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FVector2D GetAnchorMin() const { return AnchorMin; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FVector2D GetAnchorMax() const { return AnchorMax; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FVector2D GetAnchoredPosition() const { return AnchoredPosition; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	FVector2D GetSizeDelta() const { return SizeDelta; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetHorizontalAnchoredPosition() const { return AnchoredPosition.X; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetVerticalAnchoredPosition() const { return AnchoredPosition.Y; }

	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetAnchorLeft()const;
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetAnchorTop()const;
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetAnchorRight()const;
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetAnchorBottom()const;
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorMin(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorMax(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation);

	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);

	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchoredPosition(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetVerticalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetSizeDelta(FVector2D Value);

	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorLeft(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorTop(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorRight(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetAnchorBottom(float Value);


	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* HorizontalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* VerticalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* AnchoredPositionTo(FVector2D endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* AnchorLeftTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* AnchorRightTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* AnchorTopTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween-LayoutSlot")
	ULTweener* AnchorBottomTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
};
