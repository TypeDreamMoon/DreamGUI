// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "DreamWidget.h"
#include "DreamWidgetSubObjectBehaviour.h"
#include "DreamLayout.generated.h"


struct FDreamLayoutControlAnchorData
{
	bool bCanControlHorizontalPosition = false;
	bool bCanControlVerticalPosition = false;
	bool bCanControlHorizontalSize = false;
	bool bCanControlVerticalSize = false;

	bool HaveRepeatedControl(const FDreamLayoutControlAnchorData& Other)const
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
	void Or(const FDreamLayoutControlAnchorData& Other)
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
	bool Conflict(const FDreamLayoutControlAnchorData& Other)const
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

/** Read-only snapshot used by editor layout diagnostics. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLayoutDebugInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	TObjectPtr<UDreamWidget> Widget = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FVector2D DesiredSize = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FVector2D ArrangedPosition = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FVector2D ArrangedSize = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FVector2D AuthoredSize = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FVector2D ContentBounds = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FString Algorithm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FString SlotRule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FString PositionOwner;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FString SizeOwner;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout Debug")
	FString Clipping;
};

/**
 * Class label for the snapshot above. The diagnostics are editor-facing but the code that fills them
 * lives in the runtime module, and UClass::GetDisplayNameText only exists with the editor, so outside
 * it the raw class name stands in.
 */
DREAMGUI_API FString DreamLayoutDebugClassLabel(const UClass* InClass);

UCLASS(Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamLayout : public UDreamWidgetSubObjectBehaviour
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	virtual void OnTransformChanged() {}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) {}
	
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* Widget)const PURE_VIRTUAL(UDreamLayout::GetLayoutControlAnchor, return FDreamLayoutControlAnchorData(););

	/** Zero means this layout does not contribute an intrinsic desired size. */
	virtual FVector2f GetLayoutPreferredSize() const { return FVector2f::ZeroVector; }
	virtual void MarkLayoutDirty();
protected:
	bool bIsLayoutDirty = false;
};

USTRUCT(BlueprintType)
struct FLayoutAnimationSnapshotData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TObjectPtr<UDreamWidget> Widget = nullptr;
	UPROPERTY(EditAnywhere)
	FVector2D Position = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector2D Size = FVector2D::ZeroVector;
};

UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamLayoutAnimation : public UObject
{
	GENERATED_BODY()
public:
	UDreamLayoutAnimation();

	virtual void OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamLayoutContainer* GetLayoutContainer()const;
private:
	UPROPERTY(Transient, BlueprintReadOnly, Category = DreamGUI, Getter=GetLayoutContainer, meta = (AllowPrivateAccess = true), DisplayName=LayoutContainer)
	mutable TObjectPtr<UDreamLayoutContainer> OwnerLayoutContainer;
	bool bCanExecuteBlueprintEvent;
protected:
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnApplyLayoutResults"), Category = "LayoutContainer")
	void ReceiveOnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<UDreamTweener*>& ResultTweenerArray);
};

UCLASS(BlueprintType)
class DREAMGUI_API UDreamLayoutAnimation_CommonTween : public UDreamLayoutAnimation
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	float Duration = 0.3f;
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	EDreamTweenEase Ease = EDreamTweenEase::OutCubic;
	UPROPERTY(EditAnywhere, Category = "LayoutContainer", meta = (EditCondition = "Ease==EDreamTweenEase::CurveFloat"))
	FRuntimeFloatCurve EaseCurve;
public:
	virtual void OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray) override;
};

UCLASS(BlueprintType)
class DREAMGUI_API UDreamLayoutAnimation_SlideIn : public UDreamLayoutAnimation
{
	GENERATED_BODY()
private:
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	float Duration = 0.3f;
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	EDreamTweenEase Ease = EDreamTweenEase::OutCubic;
	UPROPERTY(EditAnywhere, Category = "LayoutContainer", meta = (EditCondition = "Ease==EDreamTweenEase::CurveFloat"))
	FRuntimeFloatCurve EaseCurve;

	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	float OpacityOffset = 0;
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	FVector2D PositionOffset = FVector2D(100, 0);
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	FVector2D SizeOffset = FVector2D(0, 0);
public:
	virtual void OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray) override;
};

/**
 * LayoutContainer can handle children position
 */
UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamLayoutContainer : public UDreamLayout
{
	GENERATED_BODY()
public:
	UDreamLayoutContainer();
	virtual bool GetLayoutDebugInfo(const UDreamWidget* TargetWidget, FDreamLayoutDebugInfo& OutInfo) const;
protected:
	UPROPERTY(EditAnywhere, Category = "LayoutContainer")
	bool bUseAnimation = false;
	UPROPERTY(EditAnywhere, Instanced, Category = "LayoutContainer", meta = (EditCondition = "bUseAnimation"))
	TObjectPtr<UDreamLayoutAnimation> AnimationHandler;

	//position and size snapshot before layout calculation
	TArray<FLayoutAnimationSnapshotData> LayoutAnimSnapshotDataArray;
	TArray<TWeakObjectPtr<UDreamTweener>> LayoutAnimTweenerArray;

	/** Scratch list of participating children. Each container fills this itself, on its own filter rules. */
	UPROPERTY(Transient)TArray<UDreamWidget*> Children;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostReinitProperties()override;

	virtual void OnRegister() override;
	/** INDEX_NONE means this container does not impose a child-count limit. */
	virtual int32 GetMaxChildren() const { return INDEX_NONE; }
	/**
	 * Behaviours the owning widget must carry for this container to match the UMG panel it mirrors
	 * (a Size Box needs the ContentWidget, for example). UDreamWidget::SyncRequiredBehavioursForLayoutContainer
	 * adds the missing ones whenever the container is assigned and removes the ones only the previous
	 * container asked for, so this is the single place such a dependency is declared.
	 */
	virtual void GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const {}

	virtual void SnapshotLayout();
	virtual void ApplyLayoutResult();

	//called by DreamWidget during layout processing
	virtual void CalculateLayout(){}

	UFUNCTION(BlueprintCallable, Category = "LayoutContainer")
	bool GetUseAnimation()const{return bUseAnimation;}
	UFUNCTION(BlueprintCallable, Category = "LayoutContainer")
	void SetUseAnimation(bool Value){bUseAnimation = Value;}

	UFUNCTION(BlueprintCallable, Category = "LayoutContainer", meta=(DeterminesOutputType="LayoutClass"))
	void SetLayoutAnimation(UDreamLayoutAnimation* Value);
	UFUNCTION(BlueprintCallable, Category = "LayoutContainer", meta=(DeterminesOutputType="LayoutClass"))
	UDreamLayoutAnimation* CreateNewLayoutAnimation(TSubclassOf<UDreamLayoutAnimation> Class);
	template<class T>
	T* CreateNewLayoutAnimation()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamLayoutAnimation>::Value, "'T' template parameter to CreateNewLayoutAnimation must be derived from UDreamLayoutAnimation");
		return (T*)CreateNewLayoutAnimation(T::StaticClass());
	}
};

/**
 * LayoutSelf can handle self size.
 * This base class just provide IgnoreLayout.
 */
UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamLayoutSelf : public UDreamLayout
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostReinitProperties()override;

	//called by DreamWidget during layout processing
	virtual void CalculateSize(){}
	
	virtual FVector2f GetLayoutFinalSize();
	
	virtual FDreamLayoutControlAnchorData GetLayoutControlAnchor(const UDreamWidget* Widget) const override{return FDreamLayoutControlAnchorData();}
};
