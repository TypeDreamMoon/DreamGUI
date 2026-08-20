// Copyright 2019-Present LexLiu. All Rights Reserved.

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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetSize()const { return Size; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		float GetNavigationChangeInterval()const { return NavigationChangeInterval; }

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
		UDreamWidget* GetHandle()const { return Handle.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Scrollbar")
		EUIScrollbarDirectionType GetDirectionType()const { return DirectionType; }
	
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)override;
private:
	bool CheckHandle();
	void CalculateInputValue(UDreamPointerEventData* EventData);
	void ApplyValueToVisual();
	void SetValue(float InValue, bool FireEvent);
};
