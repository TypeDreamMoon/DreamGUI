// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamRadioButton.generated.h"

class UDreamWidget;
class UUIToggle;
class UUIToggleGroup;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRadioButtonChangedEvent, bool, bIsOn);

/**
 * A radio button whose hierarchy is code, not an asset.
 *
 * The toggle's anatomy with the toggle's own behaviour underneath -- a box, a mark centred in it, a
 * label beside it in a row -- differing in exactly two places, which is why it is a class and not a
 * style: the mark is an image dot sized by the style rather than a glyph, and the box's corner
 * radius defaults to half its size, which is the whole of what makes it read as a radio.
 *
 * What makes a radio a radio at runtime is the GROUP, and the group is not this control's: it is a
 * UUIToggleGroup behaviour living on a shared ancestor, and membership is handed over through
 * SetToggleGroup. Ungrouped, this control is honest about being a round toggle -- clicking it again
 * turns it off; a group with bAllowNoneSelected=false is what forbids that.
 *
 *     /Script/DreamGUI.DreamRadioButton OptionA {
 *         Label = "Option A"
 *         bIsOn = true
 *         OnToggleChanged -> HandleOptionA
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Radio Button")
class DREAMGUI_API UDreamRadioButton : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/** This instance's own look -- consulted only when StyleSource is Inline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::Inline"))
	FDreamRadioButtonStyle Style;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button")
	FText Label;

	/**
	 * Selected or not. A property rather than the getter/setter pair alone, because the pair alone
	 * is invisible: .dui writes properties, the designer lists properties, and a binding resolves a
	 * property. Authored value in, mirror of the behaviour's out -- HandleValueChanged keeps it
	 * honest when the user (or the group switching this one off) is the writer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button")
	bool bIsOn = false;

	/** Fired by the toggle underneath, re-broadcast here so a consumer never reaches into the parts. */
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FDreamRadioButtonChangedEvent OnToggleChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FDreamRadioButtonChangedEvent OnValueChangedBP;


	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	bool GetIsOn() const;

	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetIsOn(bool bInIsOn);

	/**
	 * Membership, passed straight through to the behaviour.
	 *
	 * A passthrough and nothing more on purpose: UUIToggle already owns joining, leaving and the
	 * join-time reconcile, and this control adds no second copy of that state. (The behaviour's
	 * bAutoFindToggleGroupInParent would make grouping declarative, but it has no setter today and
	 * is only read in Awake -- so wiring a group from code is this call, made after Initialize.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetToggleGroup(UUIToggleGroup* InGroup);

	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	UUIToggleGroup* GetToggleGroup() const;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Radio Button")
	TObjectPtr<UDreamWidget> BoxNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Radio Button")
	TObjectPtr<UDreamWidget> DotNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Radio Button")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Radio Button")
	TObjectPtr<UUIToggle> ToggleBehaviour = nullptr;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(bool bInIsOn);
};
