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

enum class ELexLayoutUpdateType:uint8
{
	/**
	 * From leaf to root, calculate determinate size
	 */
	FirstPass_RootToLeaf,
	SecondPass_LeafToRoot,
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

	virtual void GetLayoutProperties(FVector2f& OutPreferred)PURE_VIRTUAL(ULexLayout::GetLayoutProperties, );

protected:
	void ApplyWidgetWidth(ULexWidget* InWidget, const float& InWidth);
	void ApplyWidgetHeight(ULexWidget* InWidget, const float& InHeight);
	void ApplyWidgetAnchoredPosition(ULexWidget* InWidget, const FVector2D& InAnchoredPosition);
	void ApplyWidgetSizeDelta(ULexWidget* InWidget, const FVector2D& InSizedDelta);
};


UENUM(BlueprintType, Category = LGUI)
enum class ELexLayoutAnimationType :uint8
{
	/** Immediately change position and size */
	Immediately,
	/** Change position and size with ease animation */
	EaseAnimation,
	/** Use implemented LexLayoutAnimationCustom object to do the transition */
	Custom,
};

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutAnimationCustom : public UObject
{
	GENERATED_BODY()
public:
	ULexLayoutAnimationCustom();

	virtual void BeginSetupAnimations();

	virtual void ApplyAnchoredPositionAnimation(const FVector2D& Value, ULexWidget* Target);
	virtual void ApplyRotatorAnimation(const FQuat& Value, ULexWidget* Target);
	virtual void ApplyWidthAnimation(float Value, ULexWidget* Target);
	virtual void ApplyHeightAnimation(float Value, ULexWidget* Target);
	virtual void ApplySizeDeltaAnimation(const FVector2D& Value, ULexWidget* Target);

	virtual void EndSetupAnimations();
protected:
	/** use this to tell if the class is compiled from blueprint, only blueprint can execute ReceiveXXX. */
	bool bCanExecuteBlueprintEvent = false;
	/** Called before setup any animation when trying to calculate layout. Use this to initialize, eg cancel previous animations. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "BeginSetupAnimations"), Category = "LGUI")
		void ReceiveBeginSetupAnimations();
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ApplyAnchoredPositionAnimation"), Category = "LGUI")
		void ReceiveApplyAnchoredPositionAnimation(const FVector2D& Value, ULexWidget* Target);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ApplyRotatorAnimation"), Category = "LGUI")
		void ReceiveApplyRotatorAnimation(const FQuat& Value, ULexWidget* Target);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ApplyWidthAnimation"), Category = "LGUI")
		void ReceiveApplyWidthAnimation(float Value, ULexWidget* Target);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ApplyHeightAnimation"), Category = "LGUI")
		void ReceiveApplyHeightAnimation(float Value, ULexWidget* Target);
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ApplySizeDeltaAnimation"), Category = "LGUI")
		void ReceiveApplySizeDeltaAnimation(const FVector2D& Value, ULexWidget* Target);
	/** Called after setup all animations when finish calculate layout. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "EndSetupAnimations"), Category = "LGUI")
		void ReceiveEndSetupAnimations();
};

UCLASS(Abstract)
class LGUI_API ULexLayoutAnimation : public ULexLayout
{
	GENERATED_BODY()

protected:

	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexLayoutAnimationType AnimationType = ELexLayoutAnimationType::Immediately;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (EditCondition = "AnimationType==ELexLayoutAnimationType::EaseAnimation"))
		float AnimationDuration = 0.3f;
	/** Will fallback to Immediately if object is not valid */
	UPROPERTY(EditAnywhere, Instanced, Category = "LGUI", meta = (EditCondition = "AnimationType==ELexLayoutAnimationType::Custom"))
		TObjectPtr<ULexLayoutAnimationCustom> CustomAnimation;
	UPROPERTY(Transient)
		TArray<TObjectPtr<class ULTweener>> TweenerArray;

	bool bIsAnimationPlaying = false;
	bool bShouldRebuildLayoutAfterAnimation = false;
	/** Called before setup any animation when trying to calculate layout. Use this to initialize, eg cancel previous animations. */
	virtual void BeginSetupAnimations();
	virtual void ApplyAnchoredPositionWithAnimation(ELexLayoutAnimationType AnimationType, FVector2D Value, ULexWidget* Target);
	virtual void ApplyRotationWithAnimation(ELexLayoutAnimationType AnimationType, const FQuat& Value, ULexWidget* Target);
	virtual void ApplyWidthWithAnimation(ELexLayoutAnimationType AnimationType, float Value, ULexWidget* Target);
	virtual void ApplyHeightWithAnimation(ELexLayoutAnimationType AnimationType, float Value, ULexWidget* Target);
	virtual void ApplySizeDeltaWithAnimation(ELexLayoutAnimationType AnimationType, FVector2D Value, ULexWidget* Target);
	/** Called after setup all animations when finish calculate layout. */
	virtual void EndSetupAnimations();
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ELexLayoutAnimationType GetAnimationType()const { return AnimationType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetAnimationDuration()const { return AnimationDuration; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexLayoutAnimationCustom* GetCustomAnimation()const { return CustomAnimation; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetAnimationType(ELexLayoutAnimationType Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetAnimationDuration(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetCustomAnimation(ULexLayoutAnimationCustom* Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void CancelAllAnimations(bool callComplete = false);
};

/**
 * LayoutContainer can handle children position
 */
UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexLayoutContainer : public ULexLayoutAnimation
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostReinitProperties()override;

	//called by LexWidget during layout processing
	virtual void UpdateLayout(ELexLayoutUpdateType UpdateType){}
};

enum class ELexLayoutSelfSizeFitType : uint8
{
	None,
	FitParent,
	FitChildren,
};

/**
 * LayoutSelf can handle self size.
 * This base class just provide IgnoreLayout.
 */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, DisplayName="LayoutSelf-IgnoreLayout")
class LGUI_API ULexLayoutSelf : public ULexLayoutAnimation
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

	virtual ELexLayoutSelfSizeFitType GetWidthFitType()const{return ELexLayoutSelfSizeFitType::None;}
	virtual ELexLayoutSelfSizeFitType GetHeightFitType()const{return ELexLayoutSelfSizeFitType::None;}
	//called by LexWidget during layout processing
	virtual void CalculateSize(ELexLayoutUpdateType UpdateType
		, TOptional<float>& OutPreferredWidth, TOptional<float>& OutPreferredHeight
		, TOptional<float>& OutStretchedWidth, TOptional<float>& OutStretchedHeight){}
	
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget) const override{return FLexLayoutControlAnchorData();}

	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual bool GetIgnoreLayoutContainer()const{return bIgnoreLayoutContainer;}
	UFUNCTION(BlueprintCallable, Category = "LayoutSelf")
	virtual void SetIgnoreLayoutContainer(bool Value);
};
