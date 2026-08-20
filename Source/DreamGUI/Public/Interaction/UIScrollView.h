// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "UIScrollView.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIScrollViewValueChangedEvent, FVector2D, InVector2);

UENUM(BlueprintType)
enum class EDreamScrollCoordinateMode : uint8
{
	/** Backward-compatible raw component location used by legacy DreamGUI prefabs. */
	RelativeLocation,
	/** RectTransform-style offset that remains stable across anchors and pivots. */
	AnchoredPosition,
};

UCLASS(ClassGroup=(DreamGUI), Transient)
class DREAMGUI_API UUIScrollViewHelper :public UDreamUIBehaviour
{
	GENERATED_BODY()
private:
	virtual void Awake()override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	virtual void OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	friend class UUIScrollView;
	UPROPERTY(Transient)
		TWeakObjectPtr<class UUIScrollView> TargetComp;
};
//ScrollView
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIScrollView : public UDreamUIBehaviour, public IDreamPointerDragInterface, public IDreamPointerScrollInterface
{
	GENERATED_BODY()
	
protected:
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnUnregister() override;
	virtual void OnDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnEnable() override;
	virtual void OnTransformChanged() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	virtual void RecalculateRange();
protected:
	friend class UUIScrollViewHelper;
	/** Content can move inside it's parent area. */ 
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		TWeakObjectPtr<UDreamWidget> Content;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool Horizontal = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool Vertical = true;
	/** If allow Horizontal and Vertical both, then only allow one direction drag at the same time. */ 
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool OnlyOneDirection = true;
	/** Sensitivity when use mouse scroll wheel input */ 
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		float ScrollSensitivity = 1.0f;
	/** If greater than zero, mouse wheel input advances by this normalized progress instead of Slate units. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float WheelProgressStep = 0.0f;
	/** Coordinate contract used to move Content. AnchoredPosition is recommended for layout-managed content. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		EDreamScrollCoordinateMode CoordinateMode = EDreamScrollCoordinateMode::RelativeLocation;
	/** When Content size is smaller than Content's parent size, can we still scroll it? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool CanScrollInSmallSize = true;
	/** When Content size is smaller than Content's parent size, flip content's scroll direction and position. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool FlipDirectionInSmallSize = false;
	/** Determines how quickly the contents stop moving. A value of 0 means the movement will never slow down, larger value will stop the movement faster. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0"))
		float DecelerateRate = 0.135f;
	/** Limit Content inside Viewport's rect area, if out-of-range then move it back. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		bool RestrictRectArea = true;
	/** Decrease movement value when drag content out of range. A value of 0 means not allowed out of range. A value of 1 means no damp effect. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float OutOfRangeDamper = 0.5f;

	/** inherited events of this component can bubble up? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool AllowEventBubbleUp = false;

	/**
	 * Keep progress value when content position and size change.
	 * true- keep progress value and change content's position and size to fit progress.
	 * false- change progress value to fit content's position and size.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
		bool KeepProgress = false;
	//progress, 0--1, x for horizontal, y for vertical
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition="KeepProgress"))
		FVector2D Progress = FVector2D(0, 0);

	uint8 bAllowHorizontalScroll: 1, bAllowVerticalScroll: 1;
	uint8 bCanUpdateAfterDrag: 1;
	uint8 bRangeCalculated: 1;

	virtual void CalculateHorizontalRange();
	virtual void CalculateVerticalRange();
	bool CheckParameters();
	virtual bool CheckValidHit(UDreamWidget* InHitComp);
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> ContentParent = nullptr;//Content's parent
	UPROPERTY(Transient)TWeakObjectPtr<UUIScrollViewHelper> RangeHelper = nullptr;
	virtual void UpdateProgress(bool InFireEvent = true);
	FVector2D Velocity = FVector2D(0, 0);//drag speed
	FVector2D HorizontalRange;//horizontal scroll range, x--min, y--max
	FVector2D VerticalRange;//vertical scroll range, x--min, y--max
	FVector PrevPointerPosition;//prev frame pointer hit position in world

	void UpdateAfterDrag(float deltaTime);
	virtual void ApplyContentPositionWithProgress();
	FVector2D GetContentPosition() const;
	void SetContentPosition(const FVector2D& Value) const;
	void ReleaseRangeHelper();
	float GetSafeDeltaTime() const;

	FDreamUIMulticastDelegateVector2 OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-ScrollView", DisplayName="OnValueChanged")
	FUIScrollViewValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ScrollView")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Vector2);
public:
	FDreamUIMulticastDelegateVector2& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	
	//scroll range change(eg content or content's parent size change), use this to recalculate range
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void RectRangeChanged();
	
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;

	virtual bool OnPointerScroll_Implementation(UDreamPointerEventData* EventData)override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		UDreamWidget* GetContent()const { return Content.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetContent(UDreamWidget* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetHorizontal()const { return Horizontal; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetVertical()const { return Vertical; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetOnlyOneDirection()const { return OnlyOneDirection; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetScrollSensitivity()const { return ScrollSensitivity; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetWheelProgressStep()const { return WheelProgressStep; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		EDreamScrollCoordinateMode GetCoordinateMode()const { return CoordinateMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetCanScrollInSmallSize()const { return CanScrollInSmallSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetVelocity()const { return Velocity; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetDecelerateRate()const { return DecelerateRate; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		bool GetRestrictRectArea()const { return RestrictRectArea; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		float GetOutOfRangeDamper()const { return OutOfRangeDamper; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetScrollProgress()const { return Progress; }
	/** Get Content's position range in horizontal. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetHorizontalRange()const { return HorizontalRange; }
	/** Get Content's position range in vertical. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		FVector2D GetVerticalRange()const { return VerticalRange; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollSensitivity(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetWheelProgressStep(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetCoordinateMode(EDreamScrollCoordinateMode value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetKeepProgress(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetHorizontal(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetVertical(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetOnlyOneDirection(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetCanScrollInSmallSize(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetVelocity(const FVector2D& value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetDecelerateRate(float value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetRestrictRectArea(bool value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetOutOfRangeDamper(float value);

	/** Manually scroll it with delta value. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollDelta(FVector2D value);
	/** Manually scroll it with absolute value. The value will be applyed to Content's relative location. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollValue(FVector2D value);
	/** Manually scroll it with progress value (from 0 to 1). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void SetScrollProgress(FVector2D value);

	/**
	 * Try to scroll the scrollview so the child can sit at center. Will clamp it in valid range.
	 * @param InChild Target child actor.
	 * @param InEaseAnimation true-use tween animation to make smooth scroll, false-immediate set.
	 * @param InAnimationDuration Animation duration if InEaseAnimation = true.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ScrollView")
		void ScrollTo(UDreamWidget* InChild, bool InEaseAnimation = true, float InAnimationDuration = 0.5f);
};


