// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Interaction/UISlider.h"
#include "DreamSlider.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamSliderValueChangedEvent, float, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamSliderCaptureEvent);

/**
 * A slider whose hierarchy is code, not an asset.
 *
 * The same five nodes both preset Blueprints carry -- a track, a fill area holding the fill, a
 * handle area holding the handle -- with one difference that is the point: BP_HorizontalSlider and
 * BP_VerticalSlider are two assets because an asset cannot branch on a property, and this class is
 * one control because code can. Direction re-anchors the parts; there is nothing else the two
 * presets disagreed about.
 *
 * The areas exist because UUISlider positions the fill and the handle INSIDE whatever their
 * parents are -- FillArea and HandleArea are how the track tells it where "0" and "1" live. The
 * handle area is inset by the handle's own size so the handle rides within the track's ends
 * instead of overhanging them.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Slider")
class DREAMGUI_API UDreamSlider : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider")
	FDreamSliderStyle Style;

	/**
	 * Which way it runs. One property instead of two Blueprint assets.
	 *
	 * BlueprintSetter, like every knob below it: nothing in this family re-derives a control from a
	 * property that changed -- that is the SynchronizeProperties tax UDreamUIControl documents -- so
	 * a runtime write straight onto the variable moved the number and left the parts where they
	 * were. Through the setter the re-push is not something a caller has to remember.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetDirection", BlueprintSetter = "SetDirection", Category = "Slider")
	EUISliderDirectionType Direction = EUISliderDirectionType::LeftToRight;

	/** Authored value in; mirror of the behaviour's out. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetValue", BlueprintSetter = "SetValue", Category = "Slider")
	float Value = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinValue", BlueprintSetter = "SetMinValue", Category = "Slider")
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxValue", BlueprintSetter = "SetMaxValue", Category = "Slider")
	float MaxValue = 1.0f;

	/**
	 * Whole numbers only -- the behaviour has always had this and the control never pushed it, so a
	 * slider authored as an integer picker answered 3.7215. The rule is applied where a drag becomes
	 * a value, so an authored fractional Value is snapped at the next push rather than refused.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWholeNumbers", BlueprintSetter = "SetWholeNumbers", Category = "Slider")
	bool bWholeNumbers = false;

	/**
	 * How far one gamepad or keyboard press moves the value, as a FRACTION of the range: a press is
	 * worth (MaxValue - MinValue) * this. Also the behaviour's, also never pushed before -- so every
	 * slider in the project stepped by the library's 0.1 whatever its .dui said.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetNavigationChangeInterval", BlueprintSetter = "SetNavigationChangeInterval", Category = "Slider", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NavigationChangeInterval = 0.1f;

	/**
	 * The quantum a MOUSE drag moves the value in, while bMouseUsesStep is on -- UMG's StepSize.
	 *
	 * Absolute, where NavigationChangeInterval above is a fraction, and each says which input it
	 * governs: re-pointing navigation at this number would silently re-scale every slider already
	 * authored against this plugin. See UUISlider::StepSize for the full argument.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStepSize", BlueprintSetter = "SetStepSize", Category = "Slider", meta = (ClampMin = "0.0"))
	float StepSize = 0.01f;

	/** Quantise a mouse drag to StepSize. Off, a drag is continuous. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMouseUsesStep", BlueprintSetter = "SetMouseUsesStep", Category = "Slider")
	bool bMouseUsesStep = false;

	/**
	 * A gamepad must capture this slider before its directions move the value -- UMG's
	 * RequiresControllerLock, on by default for the reason it is on there: a row of sliders a stick
	 * travels THROUGH is unusable if the first one swallows every left and right.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetRequiresControllerLock", BlueprintSetter = "SetRequiresControllerLock", Category = "Slider")
	bool bRequiresControllerLock = true;

	/**
	 * The four capture moments UMG's slider speaks, re-broadcast from the behaviour exactly as the
	 * value change is. Mouse begin/end are the press and release; controller begin/end are the lock
	 * being taken and given back. What they are FOR is the reason UMG has them: a consumer that
	 * writes the value into a setting, plays a sound or asks the server can hold off while the
	 * player is still dragging, and act once on the moment they let go.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderCaptureEvent OnMouseCaptureBegin;

	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderCaptureEvent OnMouseCaptureEnd;

	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderCaptureEvent OnControllerCaptureBegin;

	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderCaptureEvent OnControllerCaptureEnd;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderValueChangedEvent OnValueChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Slider")
	FDreamSliderValueChangedEvent OnValueChangedBP;


	UFUNCTION(BlueprintCallable, Category = "Slider")
	float GetValue() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetValue(float InValue);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	EUISliderDirectionType GetDirection() const;

	/** Re-anchors the parts: the direction decides which axis the track, fill and handle run along. */
	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetDirection(EUISliderDirectionType InDirection);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	float GetMinValue() const;

	/** Range before value, always: the value is clamped against the range, never the other way. */
	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetMinValue(float InMinValue);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	float GetMaxValue() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetMaxValue(float InMaxValue);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	bool GetWholeNumbers() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetWholeNumbers(bool bInWholeNumbers);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	float GetNavigationChangeInterval() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetNavigationChangeInterval(float InInterval);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	float GetStepSize() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetStepSize(float InStepSize);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	bool GetMouseUsesStep() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetMouseUsesStep(bool bInMouseUsesStep);

	UFUNCTION(BlueprintCallable, Category = "Slider")
	bool GetRequiresControllerLock() const;

	UFUNCTION(BlueprintCallable, Category = "Slider")
	void SetRequiresControllerLock(bool bInRequiresControllerLock);

	/** Whether a gamepad currently holds this slider -- read straight off the behaviour. */
	UFUNCTION(BlueprintPure, Category = "Slider")
	bool IsControllerCaptured() const;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UDreamWidget> TrackNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UDreamWidget> FillNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UDreamWidget> FillAreaNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UDreamWidget> HandleNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UDreamWidget> HandleAreaNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Slider")
	TObjectPtr<UUISlider> SliderBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleValueChanged(float InValue);
	void HandleMouseCaptureBegin();
	void HandleMouseCaptureEnd();
	void HandleControllerCaptureBegin();
	void HandleControllerCaptureEnd();
};
