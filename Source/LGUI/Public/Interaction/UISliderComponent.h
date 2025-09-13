// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerDragInterface.h"
#include "UISelectableComponent.h"
#include "Event/LGUIEventDelegate.h"
#include "Event/LexDelegateDeclaration.h"
#include "LGUIDelegateHandleWrapper.h"
#include "UISliderComponent.generated.h"


class ULexLayoutAnchorSlot;
DECLARE_DYNAMIC_DELEGATE_OneParam(FUISliderValueChangedDelegate, float, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUISliderValueChangedEvent, float, Value);

class ALexWidgetActor;
class ULexWidget;

UENUM(BlueprintType, Category = LGUI)
enum class UISliderDirectionType:uint8
{
	LeftToRight,
	RightToLeft,
	BottomToTop,
	TopToBottom,
};

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUISliderComponent : public UUISelectableComponent, public ILexPointerDragInterface
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

	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		float Value = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		float MinValue = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		float MaxValue = 1;
	/** clamp to integer value */
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		bool WholeNumbers = false;
	/** "Fill" can fill inside it's parent */
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		TWeakObjectPtr<ALexWidgetActor> FillActor;
	/** Handle can move inside it's parent */
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		TWeakObjectPtr<ALexWidgetActor> HandleActor;
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		UISliderDirectionType DirectionType;
	/** When use navigation input to change the slider value, each press will change value as (MaxValue - MinValue) * NavigationChangeInterval. */
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider", meta=(ClampMin = "0.0", ClampMax = "1.0"))
		float NavigationChangeInterval = 0.1f;

	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> Fill;
	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> FillArea;
	
	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> Handle;
	UPROPERTY(Transient)TWeakObjectPtr<ULexWidget> HandleArea;

	FLGUIMulticastFloatDelegate OnValueChangedCPP;
	UPROPERTY(EditAnywhere, Category = "LGUI-Slider")
		FLGUIEventDelegate OnValueChanged = FLGUIEventDelegate(ELGUIEventDelegateParameterType::Double);
	
public:
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnValueChanged")
	FUISliderValueChangedEvent OnValueChangedBP;
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		float GetValue()const { return Value; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		float GetMinValue()const { return MinValue; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		float GetMaxValue()const { return MaxValue; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		bool GetWholeNumber()const { return WholeNumbers; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		ALexWidgetActor* GetFillActor()const { return FillActor.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		ALexWidgetActor* GetHandleActor()const { return HandleActor.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		UISliderDirectionType GetDirectionType()const { return DirectionType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		float GetNavigationChangeInterval()const { return NavigationChangeInterval; }

	/**
	 * @param	InValue				New value set for Value
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
	void SetValue(float InValue);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
	void SetValueWithoutNotify(float InValue);
	/** 
	 * @param	InMinValue			New value set for MinValue
	 * @param	KeepRelativeValue	Keep percentage value, eg: if origin value is 0.25 from 0.0 to 1.0, then it will be 25.0 from 0.0 to 100.0, or be -7.5 from -10.0 to 0.0
	 * @param	FireEvent			Should execute callback event?
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
	void SetMinValue(float InMinValue, bool KeepRelativeValue, bool FireEvent = true);
	/**
	 * @param	InMaxValue			New value set for MaxValue
	 * @param	KeepRelativeValue	Keep percentage value, eg: if origin value is 0.25 from 0.0 to 1.0, then it will be 25.0 from 0.0 to 100.0, or be -7.5 from -10.0 to 0.0
	 * @param	FireEvent			Should execute callback event?
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
	void SetMaxValue(float InMaxValue, bool KeepRelativeValue, bool FireEvent = true);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
	void SetNavigationChangeInterval(float InValue);

	FDelegateHandle RegisterSlideEvent(const FLGUIFloatDelegate& InDelegate);
	FDelegateHandle RegisterSlideEvent(const TFunction<void(float)>& InFunction);
	void UnregisterSlideEvent(const FDelegateHandle& InHandle);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		FLGUIDelegateHandleWrapper RegisterSlideEvent(const FUISliderValueChangedDelegate& InDelegate);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Slider")
		void UnregisterSlideEvent(const FLGUIDelegateHandleWrapper& InDelegateHandle);
public:
	virtual bool OnPointerDown_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerUp_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerBeginDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnPointerEndDrag_Implementation(ULexPointerEventData* eventData)override;
	virtual bool OnNavigate_Implementation(ELexUINavigationDirection direction, TScriptInterface<ILexNavigationInterface>& result)override;
private:
	bool CheckFill();
	bool CheckHandle();
	void CalculateInputValue(ULexPointerEventData* eventData);
	void SetValue(float InValue, bool FireEvent);
	void ApplyValueToUI();
#if WITH_EDITOR
public:
	/** This function is only for update from LGUI2 to LGUI3 */
	void ForUpgrade2to3_ApplyValueToUI() { ApplyValueToUI(); }
#endif
};
