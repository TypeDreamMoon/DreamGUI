// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Event/Interface/LGUIPointerClickInterface.h"
#include "UISelectableComponent.h"
#include "Event/LGUIEventDelegate.h"
#include "Event/LGUIDelegateDeclaration.h"
#include "LGUIDelegateHandleWrapper.h"
#include "UIToggleComponent.generated.h"


DECLARE_DYNAMIC_DELEGATE_OneParam(FUIToggleValueChangedDelegate, bool, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIToggleValueChangedEvent, bool, Value);

UCLASS(ClassGroup = LGUI, Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIToggleComponent : public UUISelectableComponent, public ILGUIPointerClickInterface
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
	ELexUISelectableTransitionType ToggleTransition = ELexUISelectableTransitionType::Color;
	UPROPERTY(EditAnywhere, Category="LGUI-Toggle", Instanced, meta = (EditCondition = "Transition==ELexUISelectableSelectionState::Custom"))
	TObjectPtr<class UUISelectableTransitionComponent> CustomToggleTransition = nullptr;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI-Toggle")
		FName OnTransitionName = TEXT("On");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LGUI-Toggle")
		FName OffTransitionName = TEXT("Off");
#pragma endregion
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
		bool bIsOn = true;
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	TWeakObjectPtr<class UUIToggleGroupComponent> ToggleGroup = nullptr;

	FLGUIMulticastBoolDelegate OnValueChangedCPP;
	UPROPERTY(EditAnywhere, Category = "LGUI-Toggle")
	FLGUIEventDelegate OnValueChanged = FLGUIEventDelegate(ELGUIEventDelegateParameterType::Bool);

	void SetValue(bool Value, bool SendCallback);
	void ApplyValueToUI(bool ImmediateSet);
	virtual bool OnPointerClick_Implementation(ULGUIPointerEventData* eventData)override;
public:
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnValueChanged")
	FUIToggleValueChangedEvent OnValueChangedBP;

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

	FDelegateHandle RegisterToggleEvent(const FLGUIBoolDelegate& InDelegate);
	FDelegateHandle RegisterToggleEvent(const TFunction<void(bool)>& InFunction);
	void UnregisterToggleEvent(const FDelegateHandle& InHandle);

	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
		FLGUIDelegateHandleWrapper RegisterToggleEvent(const FUIToggleValueChangedDelegate& InDelegate);
	UFUNCTION(BlueprintCallable, Category = "LGUI-Toggle")
		void UnregisterToggleEvent(const FLGUIDelegateHandleWrapper& InDelegateHandle);
};
