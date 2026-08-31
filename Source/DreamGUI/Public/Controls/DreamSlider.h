// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Interaction/UISlider.h"
#include "DreamSlider.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamSliderValueChangedEvent, float, Value);

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

	/** Which way it runs. One property instead of two Blueprint assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider")
	EUISliderDirectionType Direction = EUISliderDirectionType::LeftToRight;

	/** Authored value in; mirror of the behaviour's out. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider")
	float Value = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider")
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider")
	float MaxValue = 1.0f;

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
	virtual void NativeOnInitialized() override;

private:
	void HandleValueChanged(float InValue);
};
