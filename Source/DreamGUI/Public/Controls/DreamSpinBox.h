// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamSpinBox.generated.h"

class UDreamWidget;
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
 * TODO(v2): drag-to-scrub on the field (UMG's mouse-drag value change). Deliberately out of scope
 * for v1 -- it wants capture/threshold plumbing from the drag interfaces and a scrub sensitivity
 * knob, none of which changes this class's shape.
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box")
	FDreamSpinBoxStyle Style;

	/** Authored value in; the control's own thereafter. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box")
	float Value = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box")
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box")
	float MaxValue = 100.0f;

	/** What one click of a step face adds or removes, before clamping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box")
	float StepSize = 1.0f;

	/** Fired when the clamped value actually changes, whichever road changed it. */
	UPROPERTY(BlueprintAssignable, Category = "Spin Box")
	FDreamSpinBoxValueChangedEvent OnValueChanged;

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

	virtual void ApplyStyle() override;

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
	virtual void NativeOnInitialized() override;

private:
	void HandleDecrementClicked();
	void HandleIncrementClicked();
	void HandleSubmitted(const FString& InText);

	/** The one road: clamp, assign, push without notify, broadcast only on an actual change. */
	void ApplyValueChange(float InValue);

	/** Value into the parts, eventless -- the field shows it, nobody is notified. */
	void PushValueToParts();

	/** The invariant spelling ("2.5", never "2,5"), shared with what the parser reads back. */
	FString FormatValue() const;
};
