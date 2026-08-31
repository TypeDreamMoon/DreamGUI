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

};
