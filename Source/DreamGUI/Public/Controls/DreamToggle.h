// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamToggle.generated.h"

class UDreamImage;
class UDreamText;
class UDreamWidget;
class UUIToggle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamToggleChangedEvent, bool, bIsOn);

/**
 * The three states a check box can show -- UMG's ECheckBoxState vocabulary in our own type, because
 * this header must not include UMG and because .dui, the designer and Blueprint all see a UENUM the
 * same way regardless.
 *
 * Undetermined is authorable, not clickable-into: authored (a mixed multi-select, a folder of
 * part-checked children), it stands until the user clicks, and the click lands as Checked -- the
 * same rule UMG's check box follows.
 */
UENUM(BlueprintType)
enum class EDreamCheckState : uint8
{
	Unchecked,
	Checked,
	Undetermined
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamCheckStateChangedEvent, EDreamCheckState, CheckedState);

/**
 * A toggle whose hierarchy is code, not an asset.
 *
 * The same four nodes BP_Toggle has -- a box, a tick inside it, a label beside it -- built in
 * NativeOnInitialized instead of instanced from a template. What that buys is that the control
 * cannot be half-built: BP_Button shipped for months with no UIButton on it and nothing said so,
 * and a class that always adds its own behaviour cannot have that happen to it.
 *
 * What it costs is the tree nobody can open. Everything an author would otherwise have reached into
 * the hierarchy to change has to be a property here, which is what FDreamToggleStyle is, and the
 * moment a project wants all its toggles to agree that stops being enough.
 *
 * It composes into .dui with no language change at all, because a class path is already a node tag:
 *
 *     Native.Toggle Mute {
 *         Style.TickChecked = #FF3355
 *         OnToggleChanged -> HandleMute
 *     }
 *
 * Deliberately label-less: a check box is one square and its mark, and the text beside it is the
 * consumer's layout. Pair it with a Text node in whatever row the screen wants.
 *
 * API-wise it speaks UMG's check box: CheckedState (Unchecked/Checked/Undetermined) with
 * GetCheckedState/SetCheckedState, IsChecked/SetIsChecked, and OnCheckStateChanged. bIsOn with
 * GetIsOn/SetIsOn and OnToggleChanged are the compatibility spelling of the two-state subset and
 * stay fully bindable; the two spellings mirror each other through every path, and when authored
 * values disagree CheckedState wins (see ReconcileCheckSpellings).
 *
 * The third state is the control's own. The behaviour underneath stays two-state and is parked at
 * unchecked while Undetermined stands; the tick swaps its check mark for a bar in the TickChecked
 * colour. A click leaves Undetermined by becoming checked.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Toggle")
class DREAMGUI_API UDreamToggle : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
	FDreamToggleStyle Style;

	/**
	 * The authored state, in UMG's spelling and with UMG's third value. A property rather than the
	 * getter/setter pair alone, because the pair alone is invisible: .dui writes properties, the
	 * designer lists properties, and a binding resolves a property -- so a knob that exists only as
	 * two UFUNCTIONs is a knob nothing outside C++ can turn.
	 *
	 * It is the authored value going in and a mirror of the behaviour's coming out;
	 * HandleValueChanged keeps it honest when the user is the one who changed it. Where it and
	 * bIsOn are authored to disagree, this spelling wins.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetCheckedState", BlueprintSetter = "SetCheckedState", Category = "Toggle")
	EDreamCheckState CheckedState = EDreamCheckState::Unchecked;

	/**
	 * The compatibility spelling: CheckedState as a bool. Checked mirrors true; Unchecked and
	 * Undetermined mirror false. Kept as a property because existing .dui two-way-binds it
	 * (`bIsOn <-> ...`) and a binding resolves a property; kept coherent with CheckedState through
	 * every path (authored push, user click, programmatic set). New code speaks CheckedState.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsOn", BlueprintSetter = "SetIsOn", Category = "Toggle")
	bool bIsOn = false;

	/**
	 * Fired by the toggle underneath, re-broadcast here so a consumer never has to reach into the
	 * parts. The compatibility spelling of OnCheckStateChanged: fires whenever the BOOL projection
	 * of the state changes (so Unchecked <-> Undetermined moves are silent here).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Toggle")
	FDreamToggleChangedEvent OnToggleChanged;

	/**
	 * UMG's spelling of the same moment, carrying the full state. Fires whenever CheckedState
	 * changes -- alongside OnToggleChanged when the bool projection moved too.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Toggle")
	FDreamCheckStateChangedEvent OnCheckStateChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Toggle")
	FDreamToggleChangedEvent OnValueChangedBP;


	/** The compatibility spelling of IsChecked(): the behaviour's bool once it exists, bIsOn before. */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	bool GetIsOn() const;

	/** The compatibility spelling of SetCheckedState(Checked/Unchecked). */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void SetIsOn(bool bInIsOn);

	/**
	 * The full state. The behaviour is the truth for the two states it can hold; Undetermined is
	 * the control's own and reads from here.
	 */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	EDreamCheckState GetCheckedState() const;

	/**
	 * Set any of the three states. Checked/Unchecked go through the behaviour with notify, the path
	 * a click takes; Undetermined parks the behaviour at unchecked without notify and lives on the
	 * control (see the class comment).
	 */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void SetCheckedState(EDreamCheckState InCheckedState);

	/** UMG's convenience: exactly GetCheckedState() == Checked. */
	UFUNCTION(BlueprintPure, Category = "Toggle")
	bool IsChecked() const;

	/** UMG's convenience spelling of SetIsOn. */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void SetIsChecked(bool bInIsChecked);

	virtual void ApplyStyle() override;

	/**
	 * The parts, in the shape the rest of the framework expects to find them.
	 *
	 * UPROPERTY rather than bare pointers on purpose: a part nothing reflects is a part the designer,
	 * the write-back and the bindings cannot see. Transient because they are rebuilt on every
	 * initialization and never belong to a saved package.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UDreamWidget> BoxNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UDreamWidget> TickNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Toggle")
	TObjectPtr<UUIToggle> ToggleBehaviour = nullptr;

protected:
	virtual void NativeOnInitialized() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void HandleValueChanged(bool bInIsOn);

	/**
	 * Raw property writes (an authored .dui value, a direct C++ member write) can leave the two
	 * spellings disagreeing; every setter keeps them coherent, so a disagreement is always a raw
	 * write. CheckedState wins wherever it can be told apart: any non-default value (Checked or
	 * Undetermined) overrides bIsOn. The one blind spot is bIsOn=true against Unchecked --
	 * indistinguishable from "only bIsOn was authored", which is the path existing .dui still
	 * takes -- and there the bool wins.
	 */
	void ReconcileCheckSpellings();

	/**
	 * The state's face: the glyph (check mark, or an em-dash bar while Undetermined) and where the
	 * checked transition's OFF colour aims (TickChecked while Undetermined, so the bar wears the
	 * checked colour despite the behaviour reading unchecked). Called wherever the state is
	 * applied, so no path can move the state and leave the glyph lying. bForceOffColour re-pushes
	 * the off colour even when it looks unchanged -- ApplyStyle needs that to land it through the
	 * immediate path after the value push (see there).
	 */
	void PushCheckStateVisuals(bool bForceOffColour = false);
};
