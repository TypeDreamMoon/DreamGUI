// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/DreamPointerDragInterface.h"
#include "UISelectable.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "UISlider.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUISliderValueChangedEvent, float, Value);

class UDreamWidget;

UENUM(BlueprintType, Category = DreamGUI)
enum class EUISliderDirectionType:uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUISlider : public UUISelectable, public IDreamPointerDragInterface
{
	GENERATED_BODY()
	
protected:	
	virtual void Awake() override;
	virtual void Start() override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;
	/**
	 * The fill and the handle are placed in ABSOLUTE numbers read off their areas, so an area that
	 * was re-arranged is news this component has to hear. See the definition for why the two parts
	 * themselves are deliberately ignored here.
	 */
	virtual void OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		float Value = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		float MinValue = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		float MaxValue = 1;
	/** clamp to integer value */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		bool WholeNumbers = false;
	/** "Fill" can fill inside it's parent */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		TWeakObjectPtr<UDreamWidget> Fill;
	/** Handle can move inside it's parent */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		TWeakObjectPtr<UDreamWidget> Handle;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		EUISliderDirectionType DirectionType;
	/** When use navigation input to change the slider value, each press will change value as (MaxValue - MinValue) * NavigationChangeInterval. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider", meta=(ClampMin = "0.0", ClampMax = "1.0"))
		float NavigationChangeInterval = 0.1f;
	/**
	 * The quantum a MOUSE drag moves the value in, while MouseUsesStep is on -- UMG's StepSize.
	 *
	 * Deliberately NOT the keyboard's: this library already states the navigation step as
	 * NavigationChangeInterval, a FRACTION of the range, and every slider authored against this
	 * plugin means that number when it says "one press". Re-pointing navigation at StepSize would
	 * silently re-scale every one of them, so the two live side by side and each says which input it
	 * governs. StepSize is an ABSOLUTE value step; NavigationChangeInterval is a fraction.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider", meta=(ClampMin = "0.0"))
		float StepSize = 0.01f;
	/** Quantise a mouse drag to StepSize. Off, a drag is continuous -- which is this library's default. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		bool MouseUsesStep = false;
	/**
	 * A gamepad must CAPTURE this slider before its directions move the value -- UMG's
	 * RequiresControllerLock, and on for the same reason it is on there: a row of sliders the stick
	 * moves THROUGH is unusable if the first slider swallows every left and right.
	 *
	 * The capture is taken and released by the navigation TRIGGER (the key a focused control is
	 * activated with), which arrives here as a pointer down carrying InputType Navigation. While
	 * captured the directions change the value; while not, they fall through to the navigation
	 * search and move focus to the next control.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		bool RequiresControllerLock = true;

	/** Whether the gamepad currently holds this slider. Transient: a capture is a live gesture. */
	UPROPERTY(Transient)
		bool bControllerCaptured = false;

	/**
	 * Whether the pointer press being handled began a mouse capture -- SSlider's HasMouseCapture. Set
	 * by a press on an unlocked slider and cleared by the release that ends it, which is what keeps
	 * OnMouseCaptureBegin and OnMouseCaptureEnd in pairs: a press on a locked slider begins nothing,
	 * so its release ends nothing, and a lock that lands mid-drag still ends the capture already begun.
	 */
	UPROPERTY(Transient)
		bool bMouseCaptured = false;

	/**
	 * Shows its value and refuses to be moved -- UMG's Locked.
	 *
	 * A different thing from not being interactable, which is why it is a flag of its own: a
	 * disabled slider is drawn in its Disabled colours and says "not now", a locked one looks
	 * completely ordinary and says "this is what it is". A volume bar during a cutscene is the
	 * second thing, not the first.
	 *
	 * It gates INPUT only. SetValue keeps working, because a read-only slider that game code could
	 * not write would have nothing to show.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
		bool bLocked = false;

	/**
	 * The four capture moments UMG's slider speaks, in C++ only -- the control re-broadcasts them to
	 * Blueprint. Mouse begin/end are the press and the release; controller begin/end are the lock
	 * being taken and given back. What a consumer needs them for is the same thing UMG does: pause
	 * the game's own reaction to the value while the player is still dragging.
	 */
	FSimpleMulticastDelegate OnMouseCaptureBeginCPP;
	FSimpleMulticastDelegate OnMouseCaptureEndCPP;
	FSimpleMulticastDelegate OnControllerCaptureBeginCPP;
	FSimpleMulticastDelegate OnControllerCaptureEndCPP;

	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> FillArea;
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> HandleArea;

	FDreamUIMulticastDelegateFloat OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Slider", DisplayName="OnValueChanged")
	FUISliderValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Slider")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Double);
	
public:
	FDreamUIMulticastDelegateFloat& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		float GetValue()const { return Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		float GetMinValue()const { return MinValue; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		float GetMaxValue()const { return MaxValue; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		bool GetWholeNumber()const { return WholeNumbers; }

	/** See bLocked: input refused, look unchanged, SetValue unaffected. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Slider")
		bool IsLocked()const { return bLocked; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetLocked(bool InValue) { bLocked = InValue; }
	/**
	 * Settable, like the parts and the direction beside it and for the same reason: the property is
	 * EditAnywhere, so the designer and .dui have always reached it by reflection while no caller
	 * could -- and a slider assembled in code (every UDreamSlider) is exactly such a caller.
	 *
	 * Turning it ON snaps the value it is holding, because a whole-number slider showing 2.5 is a
	 * control disagreeing with its own rule.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetWholeNumbers(bool InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		UDreamWidget* GetFill()const { return Fill.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		UDreamWidget* GetHandle()const { return Handle.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		EUISliderDirectionType GetDirectionType()const { return DirectionType; }
	/**
	 * The parts and the direction, settable from code. All three properties are EditAnywhere, so the
	 * designer and .dui have always reached them by reflection while no caller could -- the same hole
	 * UUIToggle's transition target had. Each resets the cached area and re-applies, the way
	 * PostEditChangeProperty already does for an edit.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetFill(UDreamWidget* InFill);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetHandle(UDreamWidget* InHandle);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetDirectionType(EUISliderDirectionType InDirection);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		float GetNavigationChangeInterval()const { return NavigationChangeInterval; }

	/**
	 * @param	InValue				New value set for Value
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetValue(float InValue);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetValueWithoutNotify(float InValue);
	/** 
	 * @param	InMinValue			New value set for MinValue
	 * @param	KeepRelativeValue	Keep percentage value, eg: if origin value is 0.25 from 0.0 to 1.0, then it will be 25.0 from 0.0 to 100.0, or be -7.5 from -10.0 to 0.0
	 * @param	FireEvent			Should execute callback event?
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetMinValue(float InMinValue, bool KeepRelativeValue, bool FireEvent = true);
	/**
	 * @param	InMaxValue			New value set for MaxValue
	 * @param	KeepRelativeValue	Keep percentage value, eg: if origin value is 0.25 from 0.0 to 1.0, then it will be 25.0 from 0.0 to 100.0, or be -7.5 from -10.0 to 0.0
	 * @param	FireEvent			Should execute callback event?
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetMaxValue(float InMaxValue, bool KeepRelativeValue, bool FireEvent = true);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
	void SetNavigationChangeInterval(float InValue);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		float GetStepSize()const { return StepSize; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetStepSize(float InValue) { StepSize = FMath::Max(0.0f, InValue); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		bool GetMouseUsesStep()const { return MouseUsesStep; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetMouseUsesStep(bool InValue) { MouseUsesStep = InValue; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		bool GetRequiresControllerLock()const { return RequiresControllerLock; }
	/** Turning it OFF releases a capture that is standing, so the lock cannot outlive its own rule. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetRequiresControllerLock(bool InValue);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		bool IsControllerCaptured()const { return bControllerCaptured; }
	/** The one writer of the capture flag, so the flag and the two events can never disagree. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Slider")
		void SetControllerCaptured(bool InValue);

	FSimpleMulticastDelegate& GetOnMouseCaptureBeginEvent(){ return OnMouseCaptureBeginCPP; }
	FSimpleMulticastDelegate& GetOnMouseCaptureEndEvent(){ return OnMouseCaptureEndCPP; }
	FSimpleMulticastDelegate& GetOnControllerCaptureBeginEvent(){ return OnControllerCaptureBeginCPP; }
	FSimpleMulticastDelegate& GetOnControllerCaptureEndEvent(){ return OnControllerCaptureEndCPP; }

	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)override;
private:
	bool CheckFill();
	bool CheckHandle();
	void CalculateInputValue(UDreamPointerEventData* EventData);
	void SetValue(float InValue, bool FireEvent);
	void ApplyValueToVisual();

	/** LeftToRight or RightToLeft: which axis the value travels along. */
	bool IsHorizontal() const;
	/** RightToLeft or TopToBottom: the two that put the ZERO end at the far edge. */
	bool IsReversed() const;
	/**
	 * The value as 0..1, clamped, and ZERO when the range is empty.
	 *
	 * The one reader of (MaxValue - MinValue), because that division was unguarded in three places
	 * and MinValue == MaxValue is an ordinary authored state (a range left at its defaults, a slider
	 * deliberately locked): 0/0 is NaN, FMath::Clamp answers NaN with NaN -- both of its comparisons
	 * are false -- and the NaN reached an anchor, where nothing downstream can recover from it.
	 */
	float GetValue01() const;
};
