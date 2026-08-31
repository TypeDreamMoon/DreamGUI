// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Event/Interface/DreamPointerDragInterface.h"
#include "UISelectable.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "UIScrollbar.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIScrollbarValueChangedEvent, float, Value);

class UDreamWidget;

UENUM(BlueprintType, Category = DreamGUI)
enum class EUIScrollbarDirectionType:uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

/**
 * A handle riding a track: a position from 0 to 1, and how much of the track the handle covers.
 *
 * THE HANDLE'S RECT IS ABSOLUTE, NOT A RATIO ANCHOR
 * -------------------------------------------------
 * This component used to place its handle by writing RATIO anchors on it, and that is the shape
 * this library has paid for five separate times: an anchor setter resolves the parent's span AT
 * WRITE TIME, so on every frame that is not a full layout pass it reads a stretched track's zero
 * SizeDelta rather than its arranged length, and the handle spends those frames collapsed onto the
 * track's start. The progress bar's fill flickered as a dot for the same reason; the dropdown's
 * list opened at zero width for the same reason.
 *
 * So the handle's rect is now absolute numbers off the handle area's resolved size, with the anchor
 * collapsed to a POINT on the area's start edge and the pivot on that same edge -- there is no span
 * left to resolve, so there is nothing to be stale about. UDreamScrollBar used to re-do exactly
 * this from outside, after the fact, through an event whose only job was to undo the base class's
 * work; that event and the UUIScrollbar subclass it lived on are both gone.
 *
 * THE MINIMUM LENGTH BELONGS HERE
 * -------------------------------
 * A handle that would draw shorter than MinHandleSize is drawn at MinHandleSize -- and the DRAG
 * SCALE is derived from the same number, which is the reason this lives in the component that owns
 * both. A handle drawn longer than the fraction it drags with would run at the wrong rate for the
 * whole of a long list, and every version of this that kept the floor at the control layer had to
 * read the control's own rect to compute it, which is precisely the measurement that is unreliable
 * before the first arrange.
 *
 * A bar with no scroll view is a value control in its own right. Point one at a UUIScrollView (see
 * UDreamScrollBar) and both numbers become the view's: progress in, progress out.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIScrollbar : public UUISelectable, public IDreamPointerDragInterface
{
	GENERATED_BODY()

public:
	UUIScrollbar();

	virtual void Awake() override;
	virtual void Start() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	virtual void OnEnable()override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	virtual void OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Value = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Size = 0;
	/** Handle can move inside it's parent */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar")
		TWeakObjectPtr<UDreamWidget> Handle;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar")
		EUIScrollbarDirectionType DirectionType;
	/** When use navigation input to change the scroll value, each press will change value as NavigationChangeInterval. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float NavigationChangeInterval = 0.1f;
	/**
	 * Floor on the handle's drawn length, in local units. Zero leaves Size alone, which is what a
	 * bar authored as a plain value control wants; anything positive is the "you can still grab it"
	 * minimum every real scroll bar has.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar", meta = (ClampMin = "0.0"))
		float MinHandleSize = 0.0f;

	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> HandleArea;

	FDreamUIMulticastDelegateFloat OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Scrollbar", DisplayName="OnValueChanged")
	FUIScrollbarValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Scrollbar")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Double);

	float PressValue = 0;
public:
	FDreamUIMulticastDelegateFloat& GetOnValueChangedEvent(){return OnValueChangedCPP;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetValue()const { return Value; }
	/** The AUTHORED fraction. What the handle actually covers is GetEffectiveSize. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetSize()const { return Size; }
	/** Size, raised to whatever MinHandleSize asks for on the current track. What the handle draws. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetEffectiveSize()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetNavigationChangeInterval()const { return NavigationChangeInterval; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetMinHandleSize()const { return MinHandleSize; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
	void SetValue(float InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
	void SetValueWithoutNotify(float InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void SetSize(float InSize);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void SetValueAndSize(float InValue, float InSize, bool FireEvent = true);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetNavigationChangeInterval(float InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void SetMinHandleSize(float InValue);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		UDreamWidget* GetHandle()const { return Handle.Get(); }
	/**
	 * The widget the value moves, and (through its PARENT) the area the value is measured against.
	 *
	 * Public because a control that builds its own hierarchy has no details panel to be filled in
	 * from -- which is the entire reason UDreamScrollBar used to carry a UUIScrollbar subclass whose
	 * only job was to reach these two fields from inside the class.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void SetHandle(UDreamWidget* InHandle);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		EUIScrollbarDirectionType GetDirectionType()const { return DirectionType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void SetDirectionType(EUIScrollbarDirectionType InDirection);
	/** True for LeftToRight and RightToLeft. Everything axis-dependent in here asks this. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Scrollbar")
		bool IsHorizontal()const;
	/** The track's length along the bar's axis, or zero while the area is not resolvable yet. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Scrollbar")
		float GetHandleAreaLength()const;

	/** Re-place the handle from the value and size held right now. Called for you on every change. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		void ApplyValueToVisual();

	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)override;
private:
	bool CheckHandle();
	void CalculateInputValue(UDreamPointerEventData* EventData);
	void SetValue(float InValue, bool FireEvent);
	/** True while the handle's zero end is the track's FAR end -- RightToLeft and TopToBottom. */
	bool IsReversed()const;
	/** How far the handle can travel: the area's length less the handle's own drawn length. */
	float GetHandleTravel()const;
	/** The pointer's position along the bar's axis, in the handle area's space. */
	float ProjectPointerOntoAxis(const FVector& InWorldPoint)const;
};
