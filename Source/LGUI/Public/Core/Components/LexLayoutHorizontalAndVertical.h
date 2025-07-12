// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutHorizontalAndVertical.generated.h"

class ULexWidget;

UENUM(BlueprintType)
enum class ELexLayoutDirection : uint8
{
	Horizontal,
	HorizontalReverse,
	Vertical,
	VerticalReverse,
};

UENUM(BlueprintType)
enum class ELexLayoutSpacingType : uint8
{
	/** Set space with fixed value */
	Fixed,
	/** Auto set space between children */
	Between,
	/** Auto set space between children, also set spacing on start & end child's edge */
	Around,
};
USTRUCT(BlueprintType)
struct FLexLayoutSpacing
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELexLayoutSpacingType Type = ELexLayoutSpacingType::Between;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 0;

	bool operator==(const FLexLayoutSpacing& Other) const
	{
		return Value == Other.Value && Type == Other.Type;
	}
};

UENUM(BlueprintType)
enum class ELexLayoutHorizontalAlignment : uint8
{
	Left,
	Center,
	Right,
};
UENUM(BlueprintType)
enum class ELexLayoutVerticalAlignment : uint8
{
	Top,
	Middle,
	Bottom,
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
	ELexLayoutHorizontalAlignment HorizontalAlignment = ELexLayoutHorizontalAlignment::Left;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutVerticalAlignment VerticalAlignment = ELexLayoutVerticalAlignment::Top;
	/** Control the spacing between child elements */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexLayoutSpacing Spacing;
protected:
	virtual void OnUpdateLayout() override;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual TSubclassOf<ULexLayoutSlot> GetSlotClass()const override;
	virtual bool SupportShrinkToChildrenWidth()override {return true;}
	virtual bool SupportShrinkToChildrenHeight()override {return true;}
	virtual float GetShrinkToChildrenWidth()override;
	virtual float GetShrinkToChildrenHeight()override;
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutDirection GetDirection()const { return Direction; }
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutHorizontalAlignment GetHorizontalAlignment()const { return HorizontalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "Layout")
	ELexLayoutVerticalAlignment GetVerticalAlignment()const { return VerticalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexLayoutSpacing GetSpacing()const { return Spacing; }

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetDirection(ELexLayoutDirection Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetVerticalAlignment(ELexLayoutVerticalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetSpacing(const FLexLayoutSpacing& Value);
};

UCLASS(BlueprintType, DisplayName="Vertical & Horizontal Slot")
class LGUI_API ULexLayoutHorizontalAndVerticalSlot : public ULexLayoutSlot
{
	GENERATED_BODY()
private:
	friend class FLexLayoutHorizontalAndVerticalSlotCustomization;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutHorizontalAlignment HorizontalAlignment = ELexLayoutHorizontalAlignment::Center;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexLayoutVerticalAlignment VerticalAlignment = ELexLayoutVerticalAlignment::Middle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	FVector2D PositionOffset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true))
	int32 Order = 0;
	/** Grow the size if there is extra space.
	 * Will be ignored if parent widget's size is ShrinkToChildren.
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true, UIMin=0))
	float Grow = 0;
	/** Shrink the size if there is no space but others need more space.
	 * Will be ignored if parent widget's size is ShrinkToChildren.
	 **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutSlot", Getter, Setter, meta = (AllowPrivateAccess = true, UIMin=0))
	float Shrink = 0;
	
	TWeakObjectPtr<ULexLayoutHorizontalAndVertical> CacheLayout;
public:
	virtual bool GetLayoutControlWidth() const override;
	virtual bool GetLayoutControlHeight() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	ULexLayoutHorizontalAndVertical* GetLayout();
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	ELexLayoutHorizontalAlignment GetHorizontalAlignment()const { return HorizontalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	ELexLayoutVerticalAlignment GetVerticalAlignment()const { return VerticalAlignment; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	const FVector2D& GetPositionOffset()const { return PositionOffset; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	int32 GetOrder()const { return Order; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetGrow()const { return Grow; }
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	float GetShrink()const { return Shrink; }
	
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetHorizontalAlignment(ELexLayoutHorizontalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetVerticalAlignment(ELexLayoutVerticalAlignment Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetPositionOffset(const FVector2D& Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetOrder(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetGrow(float Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutSlot")
	void SetShrink(float Value);
};
