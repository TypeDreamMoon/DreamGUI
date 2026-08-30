// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/DreamPointerClickInterface.h"
#include "UISelectable.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "UIToggle.generated.h"


class UUIToggle;

UCLASS(ClassGroup = (DreamUI), Abstract, Blueprintable)
class DREAMGUI_API UUIToggleTransition :public UUITransition
{
	GENERATED_BODY()
public:

	UFUNCTION()
	UUIToggle* GetToggleComponent()const;
protected:
	UPROPERTY(Transient, BlueprintReadOnly, Getter=GetToggleComponent, Category = "DreamGUI-Transition", DisplayName=UIToggle)
	mutable TObjectPtr<UUIToggle> UIToggleComp;

	/** 
	 * Called when UISelectableComponent's transition state = normal.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "ToggleOn"))
		void ReceiveToggleOn(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI-Transition", meta = (DisplayName = "ToggleOff"))
		void ReceiveToggleOff(bool InImmediateSet);
public:
	/**
	 * Called when UISelectableComponent's transition state = normal.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnNormal();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void ToggleOn(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * Default will call blueprint implemented function. If you dont want that, just not use Super::OnHighlighted();
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	virtual void ToggleOff(bool InImmediateSet);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIToggleValueChangedEvent, bool, Value);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIToggle : public UUISelectable, public IDreamPointerClickInterface
{
	GENERATED_BODY()

	UUIToggle();
protected:
	virtual void Awake() override;
	virtual void Start() override;
	virtual void OnDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	friend class FUIToggleCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	TWeakObjectPtr<UDreamVisual> ToggleTransitionTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Toggle")
	EUISelectableTransitionType ToggleTransitionType = EUISelectableTransitionType::Color;
	UPROPERTY(EditAnywhere, Category="DreamGUI-Toggle", meta = (EditCondition = "ToggleTransitionType==EUISelectableTransitionType::Custom"))
	TWeakObjectPtr<UUIToggleTransition> CustomToggleTransition = nullptr;
#pragma region Transition
	UPROPERTY(Transient) TObjectPtr<class UDreamTweener> ToggleTransitionTweener = nullptr;

	/** Appearance when this is checked */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	FColor OnColor;
	/** Appearance when this is unchecked */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	FColor OffColor;
	
	/** Appearance when this is checked */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	FDreamUIImageBrush OnImageBrush;
	/** Appearance when this is unchecked */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	FDreamUIImageBrush OffImageBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Toggle")
		float ToggleDuration = 0.2f;

#pragma endregion
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
		bool bIsOn = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	TWeakObjectPtr<class UUIToggleGroup> ToggleGroup = nullptr;
	/** When Awake, if ToggleGroup is not set, enable this will find toggle group in parent component and up hierarchy. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	bool bAutoFindToggleGroupInParent = false;

	FDreamUIMulticastDelegateBool OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Toggle", DisplayName="OnValueChanged")
	FUIToggleValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Toggle")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Bool);

	void SetValue(bool Value, bool SendCallback);
	void ApplyValueToVisual(bool ImmediateSet);
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData)override;
public:
	FDreamUIMulticastDelegateBool& GetOnValueChangedEvent(){ return OnValueChangedCPP;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	UUIToggleGroup* GetToggleGroup()const { return ToggleGroup.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	void SetToggleGroup(UUIToggleGroup* InGroupComp);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	bool GetValue()const { return bIsOn; }
	/** Set IsChecked value and send callback event */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	virtual void SetValue(bool Value);
	/** Set IsChecked value and NOT send callback event */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	void SetValueWithoutNotify(bool Value);
	/**
	 * Aliases in the spelling the binding system derives from the property: bIsOn's setter is
	 * SetIsOn by the naming rule, and until these existed a toggle could not be bound at all --
	 * FindDreamWidgetSetterFor found nothing and DUI5005 blamed the author.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	void SetIsOn(bool Value) { SetValue(Value); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
	void SetIsOnWithoutNotify(bool Value) { SetValueWithoutNotify(Value); }
	/**
	 * If this toggle added to a ToggleGroup, then return index in group. Return -1 if not add to ToggleGroup.
	 * Index is sorted by flatten-hierarchy-index, from RootComponent(UIItem).
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Toggle")
		virtual int32 GetIndexInGroup()const;
};
