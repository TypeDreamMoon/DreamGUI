// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamRectBlock.h"
#include "DreamControlStyles.generated.h"

/**
 * Where a native control's look comes from.
 *
 * The default is the project sheet, because that is the point of having one: a code-built control
 * has no tree for anyone to open and recolour, so without a project-wide answer every instance is a
 * hand-tuned fork. Inline is the opt-out for the one toggle on one screen that really is special.
 *
 * This used to say that Inline was a deliberate opt-out and NOT a merge -- that a style is one
 * decision, and "sheet for most fields, instance for two" is how two sources of truth learn to
 * disagree. The middle ground is now a third answer rather than an accident, and the argument has
 * an answer: a merge is only ambiguous while nobody can see which side won. Every field carries a
 * tick beside it, the panel greys what is not in effect, and ProjectStyleSheetOverride is a state
 * an author chose rather than a state a struct drifted into. What made the old objection right was
 * that one toggle wanting a red face had to fork nine fields it did not care about, and forks are
 * the thing a sheet exists to prevent.
 */

UENUM(BlueprintType)
enum class EDreamUIStyleSource : uint8
{
	/** Resolve from the project's UDreamUIStyleSheet (StyleVariant picks a named entry). */
	ProjectStyleSheet,
	/** The sheet, with the fields this instance ticked written over it. */
	ProjectStyleSheetOverride,
	/** Use this instance's own Style property, whole. */
	Inline,
};

/**
 * Write the fields InOverrides has TICKED over OutBase, in place.
 *
 * Every style field carries a bOverride_<field> bit beside it -- UE's own idiom, the one
 * FPostProcessSettings uses, which renders as a checkbox next to the value and greys what is not in
 * effect. This is what reads them, by reflection, so there is exactly one implementation for all
 * eighteen structs and nothing to forget when a nineteenth arrives.
 *
 * The bits default to TRUE, which is what makes this change nothing that already exists: a sheet
 * variant with every bit ticked is the full fork it has always been, and untickng a field is the
 * new thing -- that field falls back to whatever it is being written over. A variant of a family
 * default is therefore inheritance without a Parent pointer, and inheritance without a cycle to
 * check for.
 *
 * Both pointers must be instances of InStruct. Nothing here allocates or reallocates; the copy is
 * the property's own CopySingleValue, so a brush, a margin or a colour is copied the way its type
 * says to.
 */
DREAMGUI_API void DreamUI_ApplyStyleOverrides(const UScriptStruct* InStruct, void* OutBase, const void* InOverrides);

/** Typed sugar for the above: merge InOverrides' ticked fields onto a copy of InBase. */
template<class TStyle>
TStyle DreamUI_MergeStyle(const TStyle& InBase, const TStyle& InOverrides)
{
	TStyle Result = InBase;
	DreamUI_ApplyStyleOverrides(TStyle::StaticStruct(), &Result, &InOverrides);
	return Result;
}

/**
 * An optional skin for a control part's procedural-rect face.
 *
 * Empty means the face stays the plain rounded rect -- the built-in look. Set a texture (or an
 * atlas sprite) and the face draws it inside the same silhouette: corner radius, borders and the
 * selectable's tint all keep working, because the skin is the rect's BODY texture, not a different
 * visual. Deliberately not the image brush: a control face never stops being a rect.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIFaceBrush
{
	GENERATED_BODY()

	/**
	 * One slot for the image, the Slate-brush shape: a plain texture or an atlas sprite, whichever
	 * is dropped in. Two typed slots with a "sprite wins" rule was a worse panel for the same data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush", meta = (DisplayThumbnail = "true", DisplayName = "Image",
		AllowedClasses = "/Script/Engine.Texture,/Script/DreamGUI.DreamUISpriteData_BaseObject"))
	TObjectPtr<UObject> Image = nullptr;

	/**
	 * Multiplied over the image -- Slate's tint. A separate channel from the style's state colours:
	 * those ride the visual's colour via the selectable's transition, this rides the rect's BODY
	 * colour, and the two multiply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush")
	FColor Tint = FColor::White;

	/** Stretch / FitIn / Envelop -- the rect's three, matching UMG's Stretch / ScaleToFit / ScaleToFill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush")
	EDreamRectBlockTextureScaleMode ScaleMode = EDreamRectBlockTextureScaleMode::Stretch;

	/**
	 * Image / Box / Border -- Slate's DrawAs, and the last thing this brush could not say.
	 *
	 * Box and Border are nine-slice: Margin's edges keep their own pixel size and only the middle
	 * stretches, so a skin drawn with a bevel or a stitched border survives being put on a button of
	 * a different size. Border is Box without its middle, for a frame around something else.
	 *
	 * Only for a plain TEXTURE. An atlas sprite's UV span is a sub-rect of its atlas, so slicing
	 * within it would read the neighbouring sprite; a sprite draws as Image whatever this says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush")
	EDreamRectBlockTextureDrawMode DrawMode = EDreamRectBlockTextureDrawMode::Image;

	/**
	 * The nine-slice edges, in TEXTURE pixels -- Slate's Margin, and the number the artist has: a
	 * border eight pixels wide in the file stays eight pixels wide on screen at any rect size.
	 * Ignored unless DrawMode slices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush",
		meta = (EditCondition = "DrawMode != EDreamRectBlockTextureDrawMode::Image", ClampMin = "0.0"))
	FMargin Margin = FMargin(0.0f);

	/**
	 * The image's own drawn size -- Slate's ImageSize. Zero means no opinion: the part keeps the
	 * size its style gives it. Non-zero wins over the style's size field on the parts that carry
	 * one (the toggle's box and mark, the radio's box and dot, the slider's handle).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Brush")
	FVector2D ImageSize = FVector2D::ZeroVector;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxSize"))
	FVector2D BoxSize = FVector2D(26.0, 26.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_TickSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_TickSize"))
	FVector2D TickSize = FVector2D(14.0, 14.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxNormal"))
	FColor BoxNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxHovered"))
	FColor BoxHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxPressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxPressed"))
	FColor BoxPressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxDisabled"))
	FColor BoxDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxFocused"))
	FColor BoxFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_TickChecked = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_TickChecked"))
	FColor TickChecked = FColor(0, 119, 255, 255);

	/** Transparent, not absent: the tick exists either way, unchecked just does not show it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_TickUnchecked = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_TickUnchecked"))
	FColor TickUnchecked = FColor(0, 119, 255, 0);

	/** Of the box. The faces are procedural rects now, which is where the UMG feel mostly lives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	/** The background. State colours (BoxNormal/Hovered/Pressed) tint it, exactly as with no image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_BoxBrush"))
	FDreamUIFaceBrush BoxBrush;

	/**
	 * The mark, per state, as images -- UMG's CheckedImage / UncheckedImage / UndeterminedImage.
	 * A state whose brush holds an image draws it (sized by the brush's ImageSize, else TickSize);
	 * a state whose brush is empty keeps the built-in glyph -- check mark, nothing, em-dash.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_CheckedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_CheckedBrush"))
	FDreamUIFaceBrush CheckedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_UncheckedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_UncheckedBrush"))
	FDreamUIFaceBrush UncheckedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_UndeterminedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_UndeterminedBrush"))
	FDreamUIFaceBrush UndeterminedBrush;
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

	/**
	 * The button's minimum height, not its height: a row of buttons lines up at this number, and one
	 * holding something taller than it grows instead of clipping. It reaches the control as the size
	 * box's MinDesiredSize; see UDreamButton.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 38.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Normal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Normal"))
	FColor Normal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Hovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Hovered"))
	FColor Hovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Pressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Pressed"))
	FColor Pressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Disabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Disabled"))
	FColor Disabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_Focused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_Focused"))
	FColor Focused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	/**
	 * Between the face's edge and what is in the hole -- UMG's ContentPadding, and half of the
	 * button's own size: the face is a size box, and this is the padding it measures with.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_ContentPadding"))
	FMargin ContentPadding = FMargin(12.0f, 4.0f, 12.0f, 4.0f);

	// No LabelColor and no FontSize. A button draws no text of its own -- what is on one is whatever
	// the host puts in its hole -- so a text look here would be a knob with nothing to write it onto,
	// which is worse than no knob at all: it reads as "this is how my button's label looks" and does
	// nothing. Whoever supplies the label styles it, and where that is a control rather than an
	// author, that control's own style says so (FDreamDialogStyle::ButtonLabelColor).

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_FaceBrush"))
	FDreamUIFaceBrush FaceBrush;
};

/** A slider: a track, the filled part of it, and the handle riding it. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamSliderStyle
{
	GENERATED_BODY()

	/** Across the slider's axis; length comes from wherever the control is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackThickness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_TrackThickness"))
	float TrackThickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_TrackColor"))
	FColor TrackColor = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_FillColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_FillColor"))
	FColor FillColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleSize"))
	FVector2D HandleSize = FVector2D(18.0, 18.0);

	/** The handle carries the pointer transition, the way the toggle's box does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleNormal"))
	FColor HandleNormal = FColor(255, 255, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleHovered"))
	FColor HandleHovered = FColor(200, 212, 236, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandlePressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandlePressed"))
	FColor HandlePressed = FColor(154, 176, 216, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleDisabled"))
	FColor HandleDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleFocused"))
	FColor HandleFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_TrackBrush"))
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_FillBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_FillBrush"))
	FDreamUIFaceBrush FillBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style", meta = (EditCondition = "bOverride_HandleBrush"))
	FDreamUIFaceBrush HandleBrush;
};

/** A text field: the box, the text in it, and the placeholder shown while there is none. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTextInputStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_Background = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_Background"))
	FColor Background = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_BackgroundDisabled"))
	FColor BackgroundDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_BackgroundFocused"))
	FColor BackgroundFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	/**
	 * While hovered. The field's behaviour is a selectable and WILL tint the background with its
	 * transition colours -- left unset those default to white, which is exactly how the first build
	 * of this control shipped as a white bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_BackgroundHovered"))
	FColor BackgroundHovered = FColor(48, 53, 66, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_TextColor"))
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_PlaceholderColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_PlaceholderColor"))
	FColor PlaceholderColor = FColor(140, 147, 166, 255);

	/** Between the box edge and the text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_Padding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_Padding"))
	FMargin Padding = FMargin(8.0f, 4.0f, 8.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;
};

/** A dropdown: a button-shaped face, and the list it opens. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamDropdownStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FaceNormal"))
	FColor FaceNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FaceHovered"))
	FColor FaceHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FacePressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FacePressed"))
	FColor FacePressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FaceDisabled"))
	FColor FaceDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FaceFocused"))
	FColor FaceFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_TextColor"))
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ArrowColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ArrowColor"))
	FColor ArrowColor = FColor(140, 147, 166, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ListBackground = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ListBackground"))
	FColor ListBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ItemHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ItemHeight"))
	float ItemHeight = 30.0f;

	/** An item's face while hovered; at rest it shows the list background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ItemHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ItemHovered"))
	FColor ItemHovered = FColor(74, 81, 98, 255);

	/** The mark on the selected item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_CheckColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_CheckColor"))
	FColor CheckColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	/** Face and list share it; the items inside the list stay square against its edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_FaceBrush"))
	FDreamUIFaceBrush FaceBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ListBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ListBrush"))
	FDreamUIFaceBrush ListBrush;

	/** The rows, template and duplicates alike; a row at rest also shows the list background colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ItemBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ItemBrush"))
	FDreamUIFaceBrush ItemBrush;
};

/**
 * Which silhouette a progress bar draws.
 *
 * One control, two shapes, for the reason the slider has one Direction rather than two Blueprint
 * presets: the parts, the events and the percent are identical, and the only thing that differs is
 * how the fill is drawn. Radial rides the rect's own RadialFill -- no second visual, no mask.
 */
UENUM(BlueprintType)
enum class EDreamProgressShape : uint8
{
	/** A horizontal bar; Percent is the fill's width. */
	Bar,
	/** A ring; Percent is the swept angle. */
	Radial,
};

/**
 * A progress bar: a track and the filled part of it.
 *
 * No handle colours and no pointer states, because there is no behaviour: what fraction is filled
 * is the control's one property, and the fill's geometry is the control's own to drive.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamProgressBarStyle
{
	GENERATED_BODY()

	/** The control's own height; length comes from wherever it is placed, like the slider's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_TrackColor"))
	FColor TrackColor = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_FillColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_FillColor"))
	FColor FillColor = FColor(0, 119, 255, 255);

	/** Half the default height: a capsule, the silhouette UMG's bar fakes with a rounded brush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_TrackBrush"))
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_FillBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_FillBrush"))
	FDreamUIFaceBrush FillBrush;

	/** Radial only: the ring's outer size. The bar shape takes its length from wherever it is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (InlineEditConditionToggle))
	bool bOverride_RadialSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (EditCondition = "bOverride_RadialSize"))
	FVector2D RadialSize = FVector2D(64.0, 64.0);

	/** Radial only: how thick the ring is, as a fraction of half its size. 1 is a full pie. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (InlineEditConditionToggle))
	bool bOverride_RadialThickness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (EditCondition = "bOverride_RadialThickness", ClampMin = "0.0", ClampMax = "1.0"))
	float RadialThickness = 0.25f;

	/** Radial only: where zero percent sits, in degrees clockwise from twelve o'clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (InlineEditConditionToggle))
	bool bOverride_RadialStartAngle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (EditCondition = "bOverride_RadialStartAngle"))
	float RadialStartAngle = 0.0f;
};

/**
 * A scroll box: a clipped viewport, the content that slides inside it, and the bars beside it.
 *
 * The bar's own look is FDreamScrollBarStyle -- the same struct the standalone scroll bar uses, so
 * a project styles its bars once and both wear it.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamScrollBarStyle
{
	GENERATED_BODY()

	/** Across the bar's axis. Its length comes from whatever it is scrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_Thickness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_Thickness"))
	float Thickness = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_TrackColor"))
	FColor TrackColor = FColor(38, 42, 52, 255);

	/** The handle carries the pointer transition, the way the slider's does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandleNormal"))
	FColor HandleNormal = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandleHovered"))
	FColor HandleHovered = FColor(96, 105, 126, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandlePressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandlePressed"))
	FColor HandlePressed = FColor(120, 132, 158, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandleDisabled"))
	FColor HandleDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandleFocused"))
	FColor HandleFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	/** Half the thickness is a capsule, which is what a bar reads as with nobody styling it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_TrackBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_TrackBrush"))
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandleBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandleBrush"))
	FDreamUIFaceBrush HandleBrush;
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamScrollBoxStyle
{
	GENERATED_BODY()

	/** Transparent by default: a scroll box is a viewport, not a panel -- the content brings its look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_Background = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (EditCondition = "bOverride_Background"))
	FColor Background = FColor(0, 0, 0, 0);

	/** Between the viewport's edge and the content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_Padding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (EditCondition = "bOverride_Padding"))
	FMargin Padding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;

	/** The bars this box shows, when it shows them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_Bar = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style", meta = (EditCondition = "bOverride_Bar"))
	FDreamScrollBarStyle Bar;
};

/**
 * A list: rows built from a source, in a scrolling viewport.
 *
 * Row colours are the list's, not the row's: a row is whatever the item template makes it, and the
 * selection and hover states have to read the same across every template a project writes.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamListStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_Background = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_Background"))
	FColor Background = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowHeight"))
	float RowHeight = 30.0f;

	/** Between rows. Zero is the dense list UMG draws by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowSpacing"))
	float RowSpacing = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_Padding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_Padding"))
	FMargin Padding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowNormal"))
	FColor RowNormal = FColor(0, 0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowHovered"))
	FColor RowHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowSelected = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowSelected"))
	FColor RowSelected = FColor(0, 119, 255, 255);

	/** Every other row, when bAlternatingRowColors is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowAlternate = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowAlternate"))
	FColor RowAlternate = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_TextColor"))
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowBrush"))
	FDreamUIFaceBrush RowBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_Bar = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_Bar"))
	FDreamScrollBarStyle Bar;
};

/** A tree: a list whose rows carry an indent and a twisty. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTreeViewStyle
{
	GENERATED_BODY()

	/** The rows and the viewport, shared whole with the list -- a tree IS a list that indents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_List = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_List"))
	FDreamListStyle List;

	/** Per depth level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_IndentPerLevel = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_IndentPerLevel"))
	float IndentPerLevel = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TwistySize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_TwistySize"))
	FVector2D TwistySize = FVector2D(14.0, 14.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TwistyColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_TwistyColor"))
	FColor TwistyColor = FColor(140, 147, 166, 255);

	/** Empty keeps the built-in glyphs, the way the check box's state brushes do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_ExpandedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_ExpandedBrush"))
	FDreamUIFaceBrush ExpandedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (InlineEditConditionToggle))
	bool bOverride_CollapsedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style", meta = (EditCondition = "bOverride_CollapsedBrush"))
	FDreamUIFaceBrush CollapsedBrush;
};

/** A tab view: a strip of tabs over a switcher of pages. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTabViewStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabHeight"))
	float TabHeight = 34.0f;

	/** Between tabs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabSpacing"))
	float TabSpacing = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabPadding"))
	FMargin TabPadding = FMargin(14.0f, 4.0f, 14.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabNormal"))
	FColor TabNormal = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabHovered"))
	FColor TabHovered = FColor(60, 67, 82, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabPressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabPressed"))
	FColor TabPressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabDisabled"))
	FColor TabDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabFocused"))
	FColor TabFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	/** The tab whose page is showing. Its own colour, because selection is not a pointer state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabSelected = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabSelected"))
	FColor TabSelected = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_LabelColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_LabelColor"))
	FColor LabelColor = FColor(170, 178, 196, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_LabelSelectedColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_LabelSelectedColor"))
	FColor LabelSelectedColor = FColor(240, 244, 252, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	/** The line under the selected tab. Zero height turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_IndicatorThickness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_IndicatorThickness"))
	float IndicatorThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_IndicatorColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_IndicatorColor"))
	FColor IndicatorColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_PageBackground = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_PageBackground"))
	FColor PageBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_PagePadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_PagePadding"))
	FMargin PagePadding = FMargin(12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TabBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_TabBrush"))
	FDreamUIFaceBrush TabBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (InlineEditConditionToggle))
	bool bOverride_PageBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style", meta = (EditCondition = "bOverride_PageBrush"))
	FDreamUIFaceBrush PageBrush;
};

/** A dialog: a dimmer over the screen, a panel on it, a title, content and buttons. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamDialogStyle
{
	GENERATED_BODY()

	/** The screen behind. Alpha is the whole of it -- a dimmer nobody can see is a dimmer nobody wants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_DimmerColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_DimmerColor"))
	FColor DimmerColor = FColor(0, 0, 0, 160);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_PanelSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_PanelSize"))
	FVector2D PanelSize = FVector2D(420.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_PanelBackground = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_PanelBackground"))
	FColor PanelBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_PanelPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_PanelPadding"))
	FMargin PanelPadding = FMargin(20.0f, 16.0f, 20.0f, 16.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_TitleColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_TitleColor"))
	FColor TitleColor = FColor(240, 244, 252, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_TitleFontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_TitleFontSize"))
	float TitleFontSize = 19.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_MessageColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_MessageColor"))
	FColor MessageColor = FColor(198, 205, 220, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_MessageFontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_MessageFontSize"))
	float MessageFontSize = 15.0f;

	/** Between the title, the message and the button row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_Spacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_Spacing"))
	float Spacing = 12.0f;

	/** Between the buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_ButtonSpacing"))
	float ButtonSpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_PanelBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_PanelBrush"))
	FDreamUIFaceBrush PanelBrush;

	/** The buttons are Native.Button instances; this is the style they wear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_Button = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_Button"))
	FDreamButtonStyle Button;

	/** The confirming button, when a dialog wants it to stand out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_PrimaryButton = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_PrimaryButton"))
	FDreamButtonStyle PrimaryButton;

	/**
	 * The wording on those buttons -- which is the DIALOG's to describe, because the dialog is what
	 * puts it there.
	 *
	 * A button draws no text of its own; UDreamDialog builds one UDreamText per entry in Buttons and
	 * hangs it in that button's content hole, exactly as a .dui author would nest one. These two are
	 * that text's look, and they sit beside TitleColor/MessageColor for the same reason: every other
	 * string this dialog draws is described right here.
	 *
	 * One pair, not one per button kind. Plain and primary differ in their FACE (two whole button
	 * styles above); nothing has yet wanted them to differ in their lettering, and a second pair can
	 * be added the day something does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonLabelColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_ButtonLabelColor"))
	FColor ButtonLabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonLabelFontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style", meta = (EditCondition = "bOverride_ButtonLabelFontSize"))
	float ButtonLabelFontSize = 15.0f;
};

/** An expandable area: a header that toggles, and the content it hides. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamExpandableAreaStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderHeight"))
	float HeaderHeight = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderNormal"))
	FColor HeaderNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderHovered"))
	FColor HeaderHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderPressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderPressed"))
	FColor HeaderPressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderDisabled"))
	FColor HeaderDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderFocused"))
	FColor HeaderFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_LabelColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_LabelColor"))
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderPadding"))
	FMargin HeaderPadding = FMargin(10.0f, 0.0f, 10.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentBackground = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ContentBackground"))
	FColor ContentBackground = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ContentPadding"))
	FMargin ContentPadding = FMargin(10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ArrowSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ArrowSize"))
	FVector2D ArrowSize = FVector2D(14.0, 14.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ArrowColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ArrowColor"))
	FColor ArrowColor = FColor(140, 147, 166, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_HeaderBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_HeaderBrush"))
	FDreamUIFaceBrush HeaderBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ContentBrush"))
	FDreamUIFaceBrush ContentBrush;

	/** Empty keeps the built-in glyphs, as with the check box's state brushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_ExpandedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_ExpandedBrush"))
	FDreamUIFaceBrush ExpandedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (InlineEditConditionToggle))
	bool bOverride_CollapsedBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style", meta = (EditCondition = "bOverride_CollapsedBrush"))
	FDreamUIFaceBrush CollapsedBrush;
};

/** A key binder: a button whose label is the bound key, and which listens when pressed. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamInputKeySelectorStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Normal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Normal"))
	FColor Normal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Hovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Hovered"))
	FColor Hovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Pressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Pressed"))
	FColor Pressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Disabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Disabled"))
	FColor Disabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Focused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Focused"))
	FColor Focused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	/** While it is listening. A different colour is the whole of the "press a key now" feedback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_Listening = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_Listening"))
	FColor Listening = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_LabelColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_LabelColor"))
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_ContentPadding"))
	FMargin ContentPadding = FMargin(12.0f, 4.0f, 12.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (InlineEditConditionToggle))
	bool bOverride_FaceBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style", meta = (EditCondition = "bOverride_FaceBrush"))
	FDreamUIFaceBrush FaceBrush;
};

/**
 * A radio button: the toggle's anatomy -- a box, a mark inside it, a label beside it -- with the
 * mark an image dot rather than a glyph, and the corner radius defaulted to HALF the box so the
 * face reads as a radio with nobody styling anything.
 *
 * The box and the dot carry separate colour sets for the same reason the toggle's box and tick do:
 * the pointer transition and the checked transition are two writers, and pointed at one visual they
 * overwrite each other.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamRadioButtonStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxSize"))
	FVector2D BoxSize = FVector2D(26.0, 26.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_DotSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_DotSize"))
	FVector2D DotSize = FVector2D(12.0, 12.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxNormal"))
	FColor BoxNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxHovered"))
	FColor BoxHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxPressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxPressed"))
	FColor BoxPressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxDisabled"))
	FColor BoxDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxFocused"))
	FColor BoxFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_DotChecked = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_DotChecked"))
	FColor DotChecked = FColor(0, 119, 255, 255);

	/** Transparent, not absent: the dot exists either way, unchecked just does not show it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_DotUnchecked = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_DotUnchecked"))
	FColor DotUnchecked = FColor(0, 119, 255, 0);

	/**
	 * Half of BoxSize by default -- that is the whole of what makes a radio round. Kept a knob
	 * rather than derived, so a project that squares its radios is one edit, not a subclass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_BoxBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_BoxBrush"))
	FDreamUIFaceBrush BoxBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_DotBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style", meta = (EditCondition = "bOverride_DotBrush"))
	FDreamUIFaceBrush DotBrush;
};

/** A spin box: a numeric field between a decrement face and an increment face. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamSpinBoxStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_Height = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_Height"))
	float Height = 34.0f;

	/** The two step buttons share one colour set; each still carries its own selectable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonNormal"))
	FColor ButtonNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonHovered"))
	FColor ButtonHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonPressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonPressed"))
	FColor ButtonPressed = FColor(38, 42, 52, 255);

	/**
	 * Not interactable. Every control pushed three pointer colours and left this one to the
	 * behaviour's library default, a flat grey that belongs to no theme -- so a disabled control was
	 * the one state a project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonDisabled"))
	FColor ButtonDisabled = FColor(60, 63, 72, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user saw the
	 * navigation land on nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonFocused"))
	FColor ButtonFocused = FColor(96, 140, 200, 255);

	/** How long a state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_FieldBackground = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_FieldBackground"))
	FColor FieldBackground = FColor(38, 42, 52, 255);

	/**
	 * While hovered. The field's behaviour is a selectable and WILL tint the background with its
	 * transition colours -- left unset those default to white, which is exactly how the first build
	 * of the text input shipped as a white bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_FieldBackgroundHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_FieldBackgroundHovered"))
	FColor FieldBackgroundHovered = FColor(48, 53, 66, 255);

	/** The value and the step glyphs alike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_TextColor"))
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonWidth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonWidth"))
	float ButtonWidth = 26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	/** Both step faces; each keeps its own selectable, they only share the look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_ButtonBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_ButtonBrush"))
	FDreamUIFaceBrush ButtonBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_FieldBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_FieldBrush"))
	FDreamUIFaceBrush FieldBrush;
};


/**
 * A ring menu: wedges around a hub, and the geometry that decides where they sit.
 *
 * Bigger than the other style structs in this file, and it has to be: a ring menu has no natural
 * size the way a button has a height. Where the ring begins and ends, how far round it runs, how
 * wide the gaps are and where the labels ride are all APPEARANCE -- a project themes its wheels
 * once, and a designer who wants a half-ring on the left edge of the screen gets there without a
 * subclass. What the menu CONTAINS, and how it answers a pointer, stays on the control.
 *
 * Every radius is in local units, measured from the ring's centre.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamRingMenuStyle
{
	GENERATED_BODY()

	/** The wedges' far edge. The control sizes itself to twice this, square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_OuterRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_OuterRadius", ClampMin = "1.0"))
	float OuterRadius = 200.0f;

	/**
	 * The hole. Also the hub's radius, and (unless DeadZoneRadius overrides it) the radius inside
	 * which the pointer picks nothing -- one number for one edge, because three knobs describing
	 * the same circle is three chances for it to stop being a circle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_InnerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_InnerRadius", ClampMin = "0.0"))
	float InnerRadius = 80.0f;

	/** Where the first item begins, in degrees clockwise from twelve o'clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_StartAngle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_StartAngle"))
	float StartAngle = 0.0f;

	/**
	 * How far round the items are spread from there. 360 is a full wheel; 180 is the half-ring a
	 * menu hanging off a screen edge wants, and it is why this is a knob at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_SweepAngle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_SweepAngle", ClampMin = "1.0", ClampMax = "360.0"))
	float SweepAngle = 360.0f;

	/**
	 * Taken out of each wedge, half at each end, so neighbours do not touch.
	 *
	 * Drawn only. The HIT sector keeps the full slice, which is not a shortcut: with the gap in the
	 * hit shape too, dragging across one would exit a wedge and enter nothing, and the highlight
	 * would blink off between every pair of items.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_ItemGapAngle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_ItemGapAngle", ClampMin = "0.0"))
	float ItemGapAngle = 2.0f;

	/** How much further out the highlighted wedge reaches. Zero for a wheel that only recolours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_HighlightGrowth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_HighlightGrowth", ClampMin = "0.0"))
	float HighlightGrowth = 12.0f;

	/**
	 * Where an item's icon and label ride, as a radius. Zero means midway between the two edges,
	 * which is right for almost every ring and stays right when the radii change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_ContentRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_ContentRadius", ClampMin = "0.0"))
	float ContentRadius = 0.0f;

	/** How wide an item's icon-and-label box is, which is also what a long label wraps at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (InlineEditConditionToggle))
	bool bOverride_ContentWidth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Geometry", meta = (EditCondition = "bOverride_ContentWidth", ClampMin = "1.0"))
	float ContentWidth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgeNormal = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgeNormal"))
	FColor WedgeNormal = FColor(52, 57, 70, 235);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgeHovered = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgeHovered"))
	FColor WedgeHovered = FColor(74, 81, 98, 245);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgePressed = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgePressed"))
	FColor WedgePressed = FColor(38, 42, 52, 255);

	/** The committed item's wedge. Separate from Hovered because the two say different things. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgeSelected = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgeSelected"))
	FColor WedgeSelected = FColor(0, 119, 255, 255);

	/**
	 * An item whose bEnabled is false. Its selectable is switched off, so the pointer transition
	 * never reaches it and this is the only colour it ever wears.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgeDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgeDisabled"))
	FColor WedgeDisabled = FColor(40, 43, 52, 140);

	/**
	 * The unbroken ring behind the wedges, filling the gaps between them. Transparent to switch it
	 * off; it is a separate node and costs nothing when it draws nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_BackdropColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_BackdropColor"))
	FColor BackdropColor = FColor(24, 26, 33, 190);

	/** The disc inside InnerRadius. Transparent leaves the middle of the screen visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_HubColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_HubColor"))
	FColor HubColor = FColor(30, 33, 41, 235);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_LabelColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_LabelColor"))
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_LabelSelectedColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_LabelSelectedColor"))
	FColor LabelSelectedColor = FColor(255, 255, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_LabelDisabledColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_LabelDisabledColor"))
	FColor LabelDisabledColor = FColor(120, 126, 140, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	/** The hub's caption -- the highlighted item's name, in the common arrangement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_HubTextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_HubTextColor"))
	FColor HubTextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_HubFontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_HubFontSize"))
	float HubFontSize = 18.0f;

	/** An item's icon, when it has one. A brush stating its own ImageSize wins over this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_IconSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_IconSize"))
	FVector2D IconSize = FVector2D(40.0, 40.0);

	/** Between the icon and the label under it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_IconSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_IconSpacing"))
	float IconSpacing = 4.0f;

	/**
	 * The box a label is drawn in, which decides where the icon above it ends up.
	 *
	 * Zero derives it as 2.6 line heights, so a label that wraps to two lines still sits centred
	 * where a one-line label sat -- the text is vertically centred in this box, and measuring the
	 * real thing is not available where this is decided (a rebuild runs with no layout pass behind
	 * it, and a headless test has no text layout at all).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (InlineEditConditionToggle))
	bool bOverride_LabelHeight = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Text", meta = (EditCondition = "bOverride_LabelHeight", ClampMin = "0.0"))
	float LabelHeight = 0.0f;

	/**
	 * How long Open and Close take. Zero snaps -- and so does a control with no world, because the
	 * tween manager is a world subsystem and hands back null without one (which is every headless
	 * test, and is why the end state is written first and the tween only animates toward it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Open", meta = (InlineEditConditionToggle))
	bool bOverride_OpenDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Open", meta = (EditCondition = "bOverride_OpenDuration", ClampMin = "0.0"))
	float OpenDuration = 0.12f;

	/** What the ring scales up FROM while opening, and back down to while closing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Open", meta = (InlineEditConditionToggle))
	bool bOverride_OpenScaleFrom = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Open", meta = (EditCondition = "bOverride_OpenScaleFrom", ClampMin = "0.01"))
	float OpenScaleFrom = 0.86f;
};
