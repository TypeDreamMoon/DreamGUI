// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamToggle.h"
#include "Controls/DreamUIControl.h"
#include "DreamRadioButton.generated.h"

class UDreamWidget;
class UUIToggle;
class UUIToggleGroup;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRadioButtonChangedEvent, bool, bIsOn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRadioCheckStateChangedEvent, EDreamCheckState, CheckedState);

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
 *         bIsOn = true
 *         OnToggleChanged -> HandleOptionA
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Radio Button")
class DREAMGUI_API UDreamRadioButton : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Radio Button")
	FDreamRadioButtonStyle Style;

	UFUNCTION(BlueprintPure, Category = "Radio Button")
	FDreamRadioButtonStyle GetStyle() const { return Style; }

	/** This instance's whole look, replaced and pushed. See UDreamButton::SetStyle for the caveat. */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetStyle(const FDreamRadioButtonStyle& InStyle);

	/**
	 * Find a UUIToggleGroup on an ancestor at Awake and join it -- with a group behaviour on the
	 * shared parent, sibling radios exclude each other with nothing wired. Default on, because a
	 * radio that does not exclude is a round toggle; off, grouping goes through SetToggleGroup.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAutoGroupWithSiblings", BlueprintSetter = "SetAutoGroupWithSiblings", Category = "Radio Button")
	bool bAutoGroupWithSiblings = true;

	UFUNCTION(BlueprintPure, Category = "Radio Button")
	bool GetAutoGroupWithSiblings() const { return bAutoGroupWithSiblings; }

	/**
	 * Turn the automatic grouping on or off, and act on it now.
	 *
	 * The behaviour reads its own copy of this at Awake, so a radio created after begin play -- a
	 * list of options built from save data, which is the ordinary case -- would otherwise never
	 * search at all. Turning it ON therefore does the search the behaviour would have done, and only
	 * when this radio is in no group yet.
	 *
	 * Turning it OFF leaves any group already joined alone: leaving is SetToggleGroup(null), which
	 * is a separate thing to want and already has a spelling.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetAutoGroupWithSiblings(bool bInAutoGroupWithSiblings);

	/**
	 * Selected or not. A property rather than the getter/setter pair alone, because the pair alone
	 * is invisible: .dui writes properties, the designer lists properties, and a binding resolves a
	 * property. Authored value in, mirror of the behaviour's out -- HandleValueChanged keeps it
	 * honest when the user (or the group switching this one off) is the writer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsOn", BlueprintSetter = "SetIsOn", Category = "Radio Button")
	bool bIsOn = false;

	/**
	 * The full state, in UMG's check-box vocabulary -- the same EDreamCheckState UDreamToggle carries,
	 * and the same three answers.
	 *
	 * A radio WITH a third state is not a contradiction: a mixed multi-select ("these four objects
	 * disagree about which option they are on") is exactly the case Undetermined exists for, and it
	 * is as ordinary for a radio group as for a check box. The asymmetry with the toggle was the
	 * defect -- one of the pair could say "I do not know" and the other could not.
	 *
	 * Authorable, never clickable-into: the click lands as Checked, as it does on the toggle. The
	 * behaviour underneath stays two-state and is parked at unchecked while this stands, so the GROUP
	 * reads an undetermined radio as not-selected -- which is the honest answer to "is this the one".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetCheckedState", BlueprintSetter = "SetCheckedState", Category = "Radio Button")
	EDreamCheckState CheckedState = EDreamCheckState::Unchecked;

	/** Fired by the toggle underneath, re-broadcast here so a consumer never reaches into the parts. */
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FDreamRadioButtonChangedEvent OnToggleChanged;

	/**
	 * UMG's spelling of the same moment, carrying the full state. Fires whenever CheckedState changes
	 * -- alongside OnToggleChanged when the bool projection moved too.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FDreamRadioCheckStateChangedEvent OnCheckStateChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Radio Button")
	FDreamRadioButtonChangedEvent OnValueChangedBP;


	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	bool GetIsOn() const;

	/** The compatibility spelling of SetCheckedState(Checked/Unchecked), exactly as on the toggle. */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetIsOn(bool bInIsOn);

	/**
	 * The full state. The behaviour is the truth for the two states it can hold; Undetermined is the
	 * control's own and reads from here.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	EDreamCheckState GetCheckedState() const;

	/**
	 * Set any of the three. Checked/Unchecked go through the behaviour WITH notify -- the path a
	 * click takes, and the path the group hears -- while Undetermined parks the behaviour at
	 * unchecked without notify and lives on the control.
	 */
	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetCheckedState(EDreamCheckState InCheckedState);

	/** UMG's convenience: exactly GetCheckedState() == Checked. */
	UFUNCTION(BlueprintPure, Category = "Radio Button")
	bool IsChecked() const;

	UFUNCTION(BlueprintCallable, Category = "Radio Button")
	void SetIsChecked(bool bInIsChecked);

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
	TObjectPtr<UUIToggle> ToggleBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

#if WITH_EDITOR
	/** Mirror in the direction of the EDIT before the base re-applies; see ReconcileCheckSpellings. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void HandleValueChanged(bool bInIsOn);

	/**
	 * Raw property writes (an authored .dui value, a direct C++ member write) can leave the two
	 * spellings disagreeing; every setter keeps them coherent, so a disagreement is always a raw
	 * write. CheckedState wins wherever it can be told apart; the one blind spot is bIsOn=true
	 * against a still-default Unchecked, which is indistinguishable from "only bIsOn was authored"
	 * -- the path existing .dui takes -- and there the bool wins. Word for word the toggle's rule,
	 * because it is the same pair of spellings.
	 */
	void ReconcileCheckSpellings();

	/**
	 * The state's face. A radio has no glyph to swap, so the third state shows in the DOT's colour:
	 * while Undetermined stands, the checked transition's OFF colour is aimed at DotChecked, so the
	 * dot wears the chosen colour even though the behaviour beneath reads unchecked -- the toggle's
	 * em-dash rule, in the one vocabulary a dot has.
	 */
	void PushCheckStateVisuals(bool bForceOffColour = false);
};
