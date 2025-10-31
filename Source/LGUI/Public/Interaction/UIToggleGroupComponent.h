// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Core/LexUIBehaviour.h"
#include "Event/LexDelegateDeclaration.h"
#include "Event/LexUIEventDelegate.h"
#include "UIToggleGroupComponent.generated.h"

class UUIToggleComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIToggleGroupValueChangedEvent, int32, Index);

UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API UUIToggleGroupComponent : public ULexUIBehaviour
{
	GENERATED_BODY()
public:
	UUIToggleGroupComponent();
protected:
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI-ToggleGroup", AdvancedDisplay) TWeakObjectPtr<UUIToggleComponent> LastSelect = nullptr;
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI-ToggleGroup", AdvancedDisplay) TArray<TWeakObjectPtr<UUIToggleComponent>> ToggleCollection;
	bool bNeedToSortToggleCollection = false;
	void SortToggleCollection();
	UPROPERTY(EditAnywhere, Category = "LGUI-ToggleGroup")
		bool bAllowNoneSelected = true;
	
	FLexUIMulticastDelegateInt32 OnValueChangedCPP;
	/* Called when selection change of this toggle group. Parameter is selected toggle's index, or -1 if none selected. */
	UPROPERTY(BlueprintAssignable, Category = "LGUI-Toggle", DisplayName="OnValueChanged")
	FUIToggleGroupValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "LGUI-ToggleGroup")
		FLexUIEventDelegate OnValueChanged;
public:
	FLexUIMulticastDelegateInt32& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	
	void AddToggleComponent(UUIToggleComponent* InComp);
	void RemoveToggleComponent(UUIToggleComponent* InComp);

	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		void SetSelection(UUIToggleComponent* Target);
	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		void ClearSelection();
	/** Return current selected toggle item. */
	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		UUIToggleComponent* GetSelectedItem()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		bool GetAllowNoneSelected()const { return bAllowNoneSelected; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		void SetAllowNoneSelected(bool InBool) { bAllowNoneSelected = InBool; }
	/** return toggle's index in this group. return -1 if not belong to this group. */
	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		int32 GetToggleIndex(const UUIToggleComponent* InComp)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-ToggleGroup")
		UUIToggleComponent* GetToggleByIndex(int32 InIndex)const;
};
