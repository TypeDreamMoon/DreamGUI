// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LexPointerClickInterface.h"
#include "UISelectableComponent.h"
#include "Event/LexUIEventDelegate.h"
#include "Event/LexDelegateDeclaration.h"
#include "UIToggleComponent.generated.h"


class UUIToggleComponent;

UCLASS(ClassGroup = (LexUI), Abstract, Blueprintable)
class LGUI_API UUIToggleTransition :public UUITransitionComponent
{
	GENERATED_BODY()
public:

	UFUNCTION()
	UUIToggleComponent* GetToggleComponent()const;
protected:
	UPROPERTY(Transient, BlueprintReadOnly, Getter=GetToggleComponent, Category = "LGUI-Transition", DisplayName=UIToggle)
	mutable TObjectPtr<UUIToggleComponent> UIToggleComp;

	/** 
	 * Called when UISelectableComponent's transition state = normal.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "ToggleOn"))
		void ReceiveToggleOn(bool InImmediateSet);
	/**
	 * Called when UISelectableComponent's transition state = highlighted.
	 * @param InImmediateSet	set properties immediately or use tween animation. InImmediateSet is true when set initialize state.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI-Transition", meta = (DisplayName = "ToggleOff"))
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

UCLASS(ClassGroup = LGUI, Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIToggleComponent : public UUISelectableComponent, public ILexPointerClickInterface
{
	GENERATED_BODY()

	UUIToggleComponent();
protected:
	virtual void Awake() override;
	virtual void Start() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	friend class FUIToggleCustomization;
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	TWeakObjectPtr<ULexVisual> ToggleTransitionTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI-Toggle")
	EUISelectableTransitionType ToggleTransitionType = EUISelectableTransitionType::Color;
	UPROPERTY(EditAnywhere, Category="LGUI-Toggle", meta = (EditCondition = "ToggleTransitionType==EUISelectableTransitionType::Custom"))
	TWeakObjectPtr<UUIToggleTransition> CustomToggleTransition = nullptr;
	bool CheckTarget();
#pragma region Transition
	UPROPERTY(Transient) TObjectPtr<class ULTweener> ToggleTransitionTweener = nullptr;

	/** Appearance when this is checked */
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FColor OnColor;
	/** Appearance when this is unchecked */
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FColor OffColor;
	
	/** Appearance when this is checked */
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FLexUIImageBrush OnImageBrush;
	/** Appearance when this is unchecked */
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FLexUIImageBrush OffImageBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI-Toggle")
		float ToggleDuration = 0.2f;

#pragma endregion
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
		bool bIsOn = true;
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	TWeakObjectPtr<class UUIToggleGroupComponent> ToggleGroup = nullptr;

	FLexUIMulticastDelegateBool OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnValueChanged")
	FUIToggleValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FLexUIEventDelegate OnValueChanged = FLexUIEventDelegate(ELexUIEventDelegateParameterType::Bool);

	void SetValue(bool Value, bool SendCallback);
	void ApplyValueToUI(bool ImmediateSet);
	virtual bool OnPointerClick_Implementation(ULexPointerEventData* EventData)override;
public:
	FLexUIMulticastDelegateBool& GetOnValueChangedEvent(){ return OnValueChangedCPP;}

	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
	UUIToggleGroupComponent* GetToggleGroup()const { return ToggleGroup.Get(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
	void SetToggleGroup(UUIToggleGroupComponent* InGroupComp);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
	bool GetValue()const { return bIsOn; }
	/** Set IsChecked value and send callback event */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
	virtual void SetValue(bool Value);
	/** Set IsChecked value and NOT send callback event */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
	void SetValueWithoutNotify(bool Value);
	/**
	 * If this toggle added to a ToggleGroup, then return index in group. Return -1 if not add to ToggleGroup.
	 * Index is sorted by flatten-hierarchy-index, from RootComponent(UIItem).
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
		virtual int32 GetIndexInGroup()const;
};
