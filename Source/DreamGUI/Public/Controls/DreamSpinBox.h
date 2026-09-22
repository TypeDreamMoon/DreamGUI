// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
//for EDreamUITextParagraphHorizontalAlign, which the value's Justification is spelled in
#include "Core/DreamUITextData.h"
//EVirtualKeyboardType and EVirtualKeyboardDismissAction, named rather than re-declared: the typed
//entry inside a spin box is the same text field, and it answers to the same two enums
#include "Components/SlateWrapperTypes.h"
#include "Widgets/Text/ISlateEditableTextWidget.h"
#include "DreamSpinBox.generated.h"

class UDreamWidget;
class UDreamUIFontData_BaseObject;
class UUIButton;
class UUITextInput;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamSpinBoxValueChangedEvent, float, Value);

/**
 * A spin box whose hierarchy is code, not an asset.
 *
 * USpinBox's pragmatic core: a number, a range, a step -- spelled as a row of three parts, [-] field
 * [+], because that row is buildable from what the library already has. The step faces carry
 * UIButton behaviours, the field carries UUITextInput hosting the value as culture-invariant text,
 * and the CONTROL owns the number: buttons and submitted text are two roads into one
 * clamp-assign-push-broadcast, so the field, the property and the event can never disagree about
 * what the value is.
 *
 * Every push into the parts is WithoutNotify -- writing authored state is not the user editing --
 * and OnValueChanged fires exactly when the clamped value actually changes.
 *
 * DRAG TO SCRUB is the other half of USpinBox, and it needed no capture plumbing of its own in the
 * end: UUITextInput already hands a drag UP when it is not being edited (its OnPointerBeginDrag
 * returns "bubble" unless bInputActive), so a drag that starts on an idle field arrives here as this
 * control's own NativeOnBeginDrag, and a drag inside a field the player is editing stays the text
 * selection's. That is exactly the rule SSpinBox follows -- click to type, drag to scrub -- arrived
 * at by asking who already owns the gesture rather than by inventing a mode.
 *
 * The scrub sweeps MinSliderValue..MaxSliderValue (which default to the hard range) across the
 * control's own width, bent by SliderExponent so a large range can still be fine near one end. It
 * counts from where the drag was recognised, not from the press: the travel that turned the press
 * into a drag moves nothing, as in SSpinBox, so the value does not leap by the drag threshold the
 * moment a scrub starts.
 *
 *     /Script/DreamGUI.DreamSpinBox Count {
 *         Value = 5
 *         MaxValue = 10
 *         OnValueChanged -> HandleCount
 *     }
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Spin Box")
class DREAMGUI_API UDreamSpinBox : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Spin Box")
	FDreamSpinBoxStyle Style;

	/** Authored value in; the control's own thereafter. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetValue", BlueprintSetter = "SetValue", Category = "Spin Box")
	float Value = 0.0f;

	/**
	 * Whether the value HAS a bottom -- UMG's bOverride_MinValue, and the half of a range this
	 * control could not say.
	 *
	 * Ticked (the default, and what every existing spin box does) the value is clamped into
	 * MinValue..MaxValue. Unticked, that end is open: a field that may hold any negative number is a
	 * thing a settings screen genuinely wants, and before this the only way to spell it was a very
	 * large number that still was not "no limit".
	 */
	UPROPERTY(EditAnywhere, Category = "Spin Box", meta = (InlineEditConditionToggle))
	bool bOverride_MinValue = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinValue", BlueprintSetter = "SetMinValue", Category = "Spin Box", meta = (EditCondition = "bOverride_MinValue"))
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Spin Box", meta = (InlineEditConditionToggle))
	bool bOverride_MaxValue = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxValue", BlueprintSetter = "SetMaxValue", Category = "Spin Box", meta = (EditCondition = "bOverride_MaxValue"))
	float MaxValue = 100.0f;

	/** What one click of a step face adds or removes, before clamping. UMG calls it Delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStepSize", BlueprintSetter = "SetStepSize", Category = "Spin Box")
	float StepSize = 1.0f;

	/**
	 * The range the DRAG sweeps, which is not always the range the value may take -- UMG's
	 * MinSliderValue / MaxSliderValue, and its override pair.
	 *
	 * Unticked (the default) the scrub sweeps MinValue..MaxValue. Ticked, it sweeps this instead,
	 * which is what makes an unbounded-ish field draggable at all: a value that may legally be
	 * anything from 0 to 1000000 cannot be scrubbed across 200 pixels, but the 0..100 a player
	 * actually wants can be, with the rest still reachable by typing.
	 */
	UPROPERTY(EditAnywhere, Category = "Spin Box", meta = (InlineEditConditionToggle))
	bool bOverride_MinSliderValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinSliderValue", BlueprintSetter = "SetMinSliderValue", Category = "Spin Box", meta = (EditCondition = "bOverride_MinSliderValue"))
	float MinSliderValue = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Spin Box", meta = (InlineEditConditionToggle))
	bool bOverride_MaxSliderValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxSliderValue", BlueprintSetter = "SetMaxSliderValue", Category = "Spin Box", meta = (EditCondition = "bOverride_MaxSliderValue"))
	float MaxSliderValue = 100.0f;

	/**
	 * How the drag's distance bends into the value -- UMG's SliderExponent. One is linear; larger
	 * numbers give the low end of the range more of the travel, which is what a range spanning
	 * several orders of magnitude needs to be usable at its bottom.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSliderExponent", BlueprintSetter = "SetSliderExponent", Category = "Spin Box", meta = (ClampMin = "0.01"))
	float SliderExponent = 1.0f;

	/** Whether dragging the field scrubs the value at all -- UMG's EnableSlider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetEnableSlider", BlueprintSetter = "SetEnableSlider", Category = "Spin Box")
	bool bEnableSlider = true;

	/**
	 * Every value, however it arrived, snapped to a multiple of StepSize -- UMG's AlwaysUsesDeltaSnap.
	 * Off (the default), only the step faces move in whole steps and a drag or a typed value is free.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAlwaysUsesDeltaSnap", BlueprintSetter = "SetAlwaysUsesDeltaSnap", Category = "Spin Box")
	bool bAlwaysUsesDeltaSnap = false;

	/**
	 * How the number is SPELLED -- UMG's MinFractionalDigits / MaxFractionalDigits.
	 *
	 * At least Min digits after the point and at most Max, trailing zeros past Min trimmed. Without
	 * them the field printed the shortest spelling that reads back exactly, so a currency field
	 * showed "2.5" where it meant "2.50" and a count field showed "3" beside "3.0000001".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinFractionalDigits", BlueprintSetter = "SetMinFractionalDigits", Category = "Spin Box", meta = (ClampMin = "0", ClampMax = "9"))
	int32 MinFractionalDigits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxFractionalDigits", BlueprintSetter = "SetMaxFractionalDigits", Category = "Spin Box", meta = (ClampMin = "0", ClampMax = "9"))
	int32 MaxFractionalDigits = 6;

	/** Stop editing the field when the value is committed -- UMG's ClearKeyboardFocusOnCommit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetClearKeyboardFocusOnCommit", BlueprintSetter = "SetClearKeyboardFocusOnCommit", Category = "Spin Box")
	bool bClearKeyboardFocusOnCommit = false;

	/**
	 * Select the whole number when the value is committed, ready to be typed over.
	 * TRUE here where UMG's is false: a spin box is filled in one entry after another and this is
	 * what it has always done.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectAllTextOnCommit", BlueprintSetter = "SetSelectAllTextOnCommit", Category = "Spin Box")
	bool bSelectAllTextOnCommit = true;

	/**
	 * How the number sits across the field -- UMG's Justification.
	 * Centre by default, which is where the built-in tree has always put it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetJustification", BlueprintSetter = "SetJustification", Category = "Spin Box")
	EDreamUITextParagraphHorizontalAlign Justification = EDreamUITextParagraphHorizontalAlign::Center;

	/**
	 * A floor under the width the control asks a content-sized parent for -- UMG's MinDesiredWidth.
	 * Zero means no opinion, which is every spin box that exists today.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinDesiredWidth", BlueprintSetter = "SetMinDesiredWidth", Category = "Spin Box", meta = (ClampMin = "0.0"))
	float MinDesiredWidth = 0.0f;

	/** Which virtual keyboard the typed-entry field summons. Default derives it from the field's type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetKeyboardType", BlueprintSetter = "SetKeyboardType", Category = "Spin Box", AdvancedDisplay)
	TEnumAsByte<EVirtualKeyboardType::Type> KeyboardType = EVirtualKeyboardType::Number;

	/** What dismissing that keyboard means -- pushed straight to the field's behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVirtualKeyboardDismissAction", BlueprintSetter = "SetVirtualKeyboardDismissAction", Category = "Spin Box", AdvancedDisplay)
	EVirtualKeyboardDismissAction VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextCommitOnDismiss;

	/** Fired when the clamped value actually changes, whichever road changed it. */
	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnValueChanged;

	/**
	 * Fired when the value is COMMITTED -- a typed entry submitted, a step face clicked, a scrub let
	 * go of -- as distinct from every intermediate value a drag passes through.
	 *
	 * The distinction USpinBox draws, and the one a consumer that writes to a setting, a server or an
	 * undo stack actually needs: OnValueChanged fires on every frame of a drag, this fires once.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnValueCommitted;

	/** The scrub began. Pairs with OnEndSliderMovement around a run of OnValueChanged. */
	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnBeginSliderMovement;

	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnEndSliderMovement;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnValueChangedBP;


	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetValue() const;

	/** Clamps into [MinValue, MaxValue]; broadcasts only when the clamped value differs. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetValue(float InValue);

	/** One step up, clamped -- what the [+] face does, callable without a pointer. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void Increment();

	/** One step down, clamped -- what the [-] face does, callable without a pointer. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void Decrement();

	/** The bottom of the range a DRAG sweeps: MinSliderValue when overridden, MinValue otherwise. */
	UFUNCTION(BlueprintPure, Category = "Spin Box")
	float GetSliderMinValue() const;

	UFUNCTION(BlueprintPure, Category = "Spin Box")
	float GetSliderMaxValue() const;

	/** Whether a scrub is in progress right now. */
	UFUNCTION(BlueprintPure, Category = "Spin Box")
	bool IsSliderMoving() const { return bSliderMoving; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	FDreamSpinBoxStyle GetStyle() const { return Style; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetStyle(const FDreamSpinBoxStyle& InStyle);

	/*
	 * The RANGE, in UMG's shape: a bound is a value plus whether there is one at all.
	 *
	 * Get returns the numeric limit rather than the stored number when the bound is off, because
	 * that is what "no minimum" means to every caller that then clamps with it -- including this
	 * control's own value road, which reads these two and nothing else.
	 */

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetMinValue() const;

	/** States a bottom, and clamps the current value into it. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMinValue(float InMinValue);

	/** Takes the bottom away: the value may then be any number down to float's own floor. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void ClearMinValue();

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetMaxValue() const;

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMaxValue(float InMaxValue);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void ClearMaxValue();

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetMinSliderValue() const;

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMinSliderValue(float InMinSliderValue);

	/** The scrub falls back to the VALUE range again, which is what an unset slider bound means. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void ClearMinSliderValue();

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetMaxSliderValue() const;

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMaxSliderValue(float InMaxSliderValue);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void ClearMaxSliderValue();

	/** Whether the scrub has two real ends to sweep between. An open range cannot be dragged. */
	UFUNCTION(BlueprintPure, Category = "Spin Box")
	bool HasFiniteSliderRange() const;

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetStepSize() const { return StepSize; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetStepSize(float InStepSize);

	/** UMG's name for StepSize. One implementation, two spellings, no second truth. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetDelta() const { return StepSize; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetDelta(float InDelta) { SetStepSize(InDelta); }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetSliderExponent() const { return SliderExponent; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetSliderExponent(float InSliderExponent);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	bool GetEnableSlider() const { return bEnableSlider; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetEnableSlider(bool bInEnableSlider);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	bool GetAlwaysUsesDeltaSnap() const { return bAlwaysUsesDeltaSnap; }

	/** Turning it on snaps the value the control is already holding, not just the next one. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetAlwaysUsesDeltaSnap(bool bInAlwaysUsesDeltaSnap);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	int32 GetMinFractionalDigits() const { return MinFractionalDigits; }

	/** Re-spells the number that is already showing: a digit count nobody applied is not a format. */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMinFractionalDigits(int32 InMinFractionalDigits);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	int32 GetMaxFractionalDigits() const { return MaxFractionalDigits; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMaxFractionalDigits(int32 InMaxFractionalDigits);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	bool GetClearKeyboardFocusOnCommit() const { return bClearKeyboardFocusOnCommit; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetClearKeyboardFocusOnCommit(bool bInClearKeyboardFocusOnCommit);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	bool GetSelectAllTextOnCommit() const { return bSelectAllTextOnCommit; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetSelectAllTextOnCommit(bool bInSelectAllTextOnCommit);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	EDreamUITextParagraphHorizontalAlign GetJustification() const { return Justification; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetJustification(EDreamUITextParagraphHorizontalAlign InJustification);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	float GetMinDesiredWidth() const { return MinDesiredWidth; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetMinDesiredWidth(float InMinDesiredWidth);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	TEnumAsByte<EVirtualKeyboardType::Type> GetKeyboardType() const { return KeyboardType; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> InKeyboardType);

	/**
	 * The typeface the number is drawn with -- UMG's Font, minus what FSlateFontInfo packs in beside
	 * it: the size is Style.FontSize and the outline belongs to the text style. Reads and writes
	 * Style.Font, so it obeys StyleSource like the rest of the look, and null means "leave the
	 * paragraph on the font it has", which is every spin box that exists today.
	 */
	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	UDreamUIFontData_BaseObject* GetFont() const { return Style.Font; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetFont(UDreamUIFontData_BaseObject* InFont);

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	EVirtualKeyboardDismissAction GetVirtualKeyboardDismissAction() const { return VirtualKeyboardDismissAction; }

	UFUNCTION(BlueprintCallable, Category = "Spin Box")
	void SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction InDismissAction);

	virtual void ApplyStyle() override;

	/**
	 * The scrub. Reaches this control because an idle UUITextInput lets a drag bubble -- see the
	 * class comment -- so no mode, no capture and no threshold of our own is involved.
	 */
	virtual bool NativeOnBeginDrag(UDreamPointerEventData* EventData) override;
	virtual bool NativeOnDrag(UDreamPointerEventData* EventData) override;
	virtual bool NativeOnEndDrag(UDreamPointerEventData* EventData) override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> DecrementNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> DecrementLabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> FieldNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> ClipNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> ValueTextNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> IncrementNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UDreamWidget> IncrementLabelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UUIButton> DecrementBehaviour = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UUIButton> IncrementBehaviour = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Spin Box")
	TObjectPtr<UUITextInput> InputBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleDecrementClicked();
	void HandleIncrementClicked();
	void HandleSubmitted(const FString& InText);

	/** The one road: clamp, assign, push without notify, broadcast only on an actual change. */
	void ApplyValueChange(float InValue);

	/**
	 * ApplyValueChange, plus the once-per-gesture half: OnValueCommitted, and the two knobs that act
	 * on the field afterwards. Every road a value ARRIVES by that is not an intermediate drag frame
	 * ends here, which is what makes "committed" a single well-defined moment.
	 */
	void CommitValue(float InValue);

	/** InValue snapped to a multiple of StepSize, measured from MinValue. Identity when off. */
	float SnapToStep(float InValue) const;

	/** The value as 0..1 across the SLIDER range, with SliderExponent undone. */
	float ValueToSliderFraction(float InValue) const;

	/** The inverse: a 0..1 fraction of the slider range, bent by SliderExponent, as a value. */
	float SliderFractionToValue(float InFraction) const;

	/** True while a drag is scrubbing. Transient: a scrub is a live gesture. */
	UPROPERTY(Transient)
	bool bSliderMoving = false;

	/** The slider fraction the scrub started from; every drag frame is an offset from it. */
	UPROPERTY(Transient)
	float SliderPressFraction = 0.0f;

	/**
	 * How far along the scrub axis the pointer had already come from the press when the drag was
	 * recognised, in the pressed widget's local units. Every drag frame measures from here rather than
	 * from the press: SSpinBox spends the travel that decides a press is a drag on deciding it, and
	 * moves the value only with what comes after.
	 */
	UPROPERTY(Transient)
	float ScrubStartTravel = 0.0f;

	/** Value into the parts, eventless -- the field shows it, nobody is notified. */
	void PushValueToParts();

	/** The floor under the control's width, and under what the value's paragraph asks a parent for. */
	void PushMinDesiredWidth();

	/** The invariant spelling ("2.5", never "2,5"), shared with what the parser reads back. */
	FString FormatValue() const;
};
