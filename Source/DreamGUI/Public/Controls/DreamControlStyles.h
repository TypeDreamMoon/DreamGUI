// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamControlStyles.generated.h"

/**
 * Where a native control's look comes from.
 *
 * The default is the project sheet, because that is the point of having one: a code-built control
 * has no tree for anyone to open and recolour, so without a project-wide answer every instance is a
 * hand-tuned fork. Inline is the opt-out for the one toggle on one screen that really is special --
 * and it is a deliberate opt-out, not a merge: a style is one decision, and "sheet for most fields,
 * instance for two" is how two sources of truth learn to disagree.
 */
UENUM(BlueprintType)
enum class EDreamUIStyleSource : uint8
{
	/** Resolve from the project's UDreamUIStyleSheet (StyleVariant picks a named entry). */
	ProjectStyleSheet,
	/** Use this instance's own Style property, whole. */
	Inline,
};

/**
 * What a toggle looks like, separated from what it is.
 *
 * This is the FButtonStyle shape, and it exists for the reason Slate's does: the control assembles
 * itself, so every appearance decision has to be a knob or it is a fork. The box and the tick have
 * separate colour sets because they carry separate transitions -- the pointer one tints the box,
 * the checked one tints the tick, and one visual cannot hold both.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamToggleStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FVector2D BoxSize = FVector2D(26.0, 26.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FVector2D TickSize = FVector2D(14.0, 14.0);

	/** Between the box and the label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	float Spacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor BoxPressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor TickChecked = FColor(0, 119, 255, 255);

	/** Transparent, not absent: the tick exists either way, unchecked just does not show it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor TickUnchecked = FColor(0, 119, 255, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	float FontSize = 15.0f;

	/** Of the box. The faces are procedural rects now, which is where the UMG feel mostly lives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	float CornerRadius = 5.0f;
};

/**
 * A button: one face, three pointer states, a label.
 *
 * The face colours are absolute -- the brush stays white and the selectable writes these onto it --
 * rather than the gallery's habit of a coloured brush multiplied by near-white transition tints.
 * Absolute colours are the ones a style sheet can actually reason about; a product of two sources
 * is a look nobody can name.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamButtonStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	float Height = 38.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FColor Normal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FColor Hovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FColor Pressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FColor LabelColor = FColor(230, 233, 240, 255);

	/** Between the face's edge and the label -- UMG's ContentPadding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FMargin ContentPadding = FMargin(12.0f, 4.0f, 12.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	float CornerRadius = 5.0f;
};

/** A slider: a track, the filled part of it, and the handle riding it. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamSliderStyle
{
	GENERATED_BODY()

	/** Across the slider's axis; length comes from wherever the control is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	float TrackThickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FColor TrackColor = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FColor FillColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FVector2D HandleSize = FVector2D(18.0, 18.0);

	/** The handle carries the pointer transition, the way the toggle's box does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FColor HandleNormal = FColor(255, 255, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FColor HandleHovered = FColor(200, 212, 236, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FColor HandlePressed = FColor(154, 176, 216, 255);
};

/** A text field: the box, the text in it, and the placeholder shown while there is none. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTextInputStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FColor Background = FColor(38, 42, 52, 255);

	/**
	 * While hovered. The field's behaviour is a selectable and WILL tint the background with its
	 * transition colours -- left unset those default to white, which is exactly how the first build
	 * of this control shipped as a white bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FColor BackgroundHovered = FColor(48, 53, 66, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FColor PlaceholderColor = FColor(140, 147, 166, 255);

	/** Between the box edge and the text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FMargin Padding = FMargin(8.0f, 4.0f, 8.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	float CornerRadius = 5.0f;
};

/** A dropdown: a button-shaped face, and the list it opens. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamDropdownStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor FaceNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor FaceHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor FacePressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor ArrowColor = FColor(140, 147, 166, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor ListBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float ItemHeight = 30.0f;

	/** An item's face while hovered; at rest it shows the list background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor ItemHovered = FColor(74, 81, 98, 255);

	/** The mark on the selected item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FColor CheckColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float MaxListHeight = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float FontSize = 15.0f;

	/** Face and list share it; the items inside the list stay square against its edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float CornerRadius = 5.0f;
};
