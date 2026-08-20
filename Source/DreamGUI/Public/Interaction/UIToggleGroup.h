// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamUIEventDelegate.h"
#include "UIToggleGroup.generated.h"

class UUIToggle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUIToggleGroupValueChangedEvent, int32, Index);

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUIToggleGroup : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UUIToggleGroup();
protected:
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI-ToggleGroup", AdvancedDisplay) TWeakObjectPtr<UUIToggle> LastSelect = nullptr;
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI-ToggleGroup", AdvancedDisplay) TArray<TWeakObjectPtr<UUIToggle>> ToggleCollection;
	void SortToggleCollection();
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ToggleGroup")
		bool bAllowNoneSelected = true;
	
	FDreamUIMulticastDelegateInt32 OnValueChangedCPP;
	/* Called when selection change of this toggle group. Parameter is selected toggle's index, or -1 if none selected. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Toggle", DisplayName="OnValueChanged")
	FUIToggleGroupValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-ToggleGroup")
		FDreamUIEventDelegate OnValueChanged;
public:
	FDreamUIMulticastDelegateInt32& GetOnValueChangedEvent(){return OnValueChangedCPP;}
	
	void AddToggleComponent(UUIToggle* InComp);
	void RemoveToggleComponent(UUIToggle* InComp);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		void SetSelection(UUIToggle* Target);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		void ClearSelection();
	/** Return current selected toggle item. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		UUIToggle* GetSelectedItem()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		bool GetAllowNoneSelected()const { return bAllowNoneSelected; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		void SetAllowNoneSelected(bool InBool) { bAllowNoneSelected = InBool; }
	/** return toggle's index in this group. return -1 if not belong to this group. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		int32 GetToggleIndex(const UUIToggle* InComp)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-ToggleGroup")
		UUIToggle* GetToggleByIndex(int32 InIndex)const;
};
