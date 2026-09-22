// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamRectBlock.h"
//for EDreamMenuPlacement, which FDreamMenuAnchorStyle carries. The enum belongs to the LAYOUT side
//(UDreamLayoutContainerMenuAnchor owns it and the placement arithmetic that reads it), and the
//control's style names that same enum rather than a second one meaning nearly the same thing.
#include "Core/Components/DreamPanelLayouts.h"
//for FDreamTextStyle, which FDreamRichTextStyle carries: outline, drop shadow and glow are one
//struct for the whole library, and a style that named its own would be a second copy of it.
#include "Core/DreamUITextData.h"
// FDreamUIStateFaces::BrushFor takes EUISelectableSelectionState by value, and an enum in a
// signature has to be a complete type -- a forward declaration compiles neither the switch nor the
// call. This header already sits under the controls, which all carry a selectable.
#include "Interaction/UISelectable.h"
#include "DreamControlStyles.generated.h"

//A style may name a sound to play; it never needs to know what one IS, and this header is included
//by every control in the family, so the sound module's header is not dragged along with it.
class USoundBase;
//Same bargain for a material: a style names one, it never asks one anything.
class UMaterialInterface;
//And for a typeface: a style names the font data asset, the text node is what loads it.
class UDreamUIFontData_BaseObject;

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
 * One face drawn five ways -- Slate's FButtonStyle shape, which is the thing a control style here
 * could not say.
 *
 * A control face is a procedural rect with ONE skin on it, and the five states were told apart by
 * colour alone: a project whose normal and pressed buttons are different DRAWINGS had to fork the
 * control. That is the gap this closes, and it closes it without a second state machine -- the
 * selectable already decides which of the five a control is in, and this is only what that decision
 * paints (see UDreamUIControl::UseStateFaces).
 *
 * EVERY FIELD IS OPTIONAL, and that is what makes it change nothing that already exists. A state
 * whose brush holds no image falls back to Normal's, and a Normal holding none falls back to the
 * control's own single brush -- so a style that says nothing here draws exactly what it drew before,
 * down to the pixel. The same is true of the other three groups: the foreground tint is gated on a
 * bit that ships off, the pressed padding on another, and the three sounds are null.
 *
 * Shared rather than per-control because a project's buttons, check boxes, dropdown faces and list
 * rows are the same decision made once. It is a FIELD of each of those styles rather than a base
 * struct: a USTRUCT hierarchy would not survive the override-bit merge, which reads one struct's
 * properties and writes the ticked ones onto another of the same type.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIStateFaces
{
	GENERATED_BODY()

	/** The resting skin. Empty means the control's own brush, which is what every style ships with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	FDreamUIFaceBrush Normal;

	/** Pointer resting on it. Empty falls back to Normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	FDreamUIFaceBrush Hovered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	FDreamUIFaceBrush Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	FDreamUIFaceBrush Disabled;

	/**
	 * Holding keyboard or gamepad focus, which is a different question from hovered -- focus survives
	 * the pointer leaving, and on a pad there is no pointer at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	FDreamUIFaceBrush Focused;

	/**
	 * Whether the five colours below are pushed at all.
	 *
	 * Off, and nothing is written to the foreground part -- which is the only default that leaves an
	 * existing control looking as it does, because "no opinion" has no spelling in an FColor: white
	 * is a real instruction to tint white, and a label authored amber would lose its colour to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (InlineEditConditionToggle))
	bool bTintForeground = false;

	/**
	 * What the control's FOREGROUND part -- its own label or glyph, never a host's content -- is
	 * tinted in each state. Slate's ForegroundColor, per state as FButtonStyle has it.
	 *
	 * Only controls that own a label nominate one (the dropdown's caption is the family's example);
	 * on a control whose text belongs to whoever filled its hole there is nothing here to write to,
	 * and reaching into a host's widgets to recolour them would be this control overruling the author.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bTintForeground"))
	FColor NormalForeground = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bTintForeground"))
	FColor HoveredForeground = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bTintForeground"))
	FColor PressedForeground = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bTintForeground"))
	FColor DisabledForeground = FColor(255, 255, 255, 128);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bTintForeground"))
	FColor FocusedForeground = FColor::White;

	/** Whether PressedPadding is used at all. Off, a pressed control keeps its resting padding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (InlineEditConditionToggle))
	bool bUsePressedPadding = false;

	/**
	 * What surrounds the content WHILE PRESSED -- Slate's PressedPadding, and the classic way a
	 * button looks pushed in: a top-heavy margin nudges the label down a pixel. The resting number is
	 * the control's own ContentPadding rather than a second copy here; see UDreamButton.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces", meta = (EditCondition = "bUsePressedPadding"))
	FMargin PressedPadding = FMargin(12.0f, 5.0f, 12.0f, 3.0f);

	/**
	 * Which brush a state actually draws with, with the fallback chain spelled once.
	 *
	 * An IMAGE is what makes a state's brush a statement. The other fields (tint, scale, nine-slice)
	 * describe how to draw an image, so a brush holding none says nothing and the question passes up
	 * the chain: this state, then the group's resting one, then InFallback -- the single brush every
	 * existing style already has, which is why an empty group changes nothing anywhere.
	 *
	 * On the STRUCT rather than on UDreamUIControl because a control paints one face and a list
	 * paints one per row: the rule has two callers with different shapes, and two copies of a
	 * fallback chain is how the row and the button learn to disagree about what an empty group means.
	 */
	const FDreamUIFaceBrush& BrushFor(EUISelectableSelectionState InState, const FDreamUIFaceBrush& InFallback) const
	{
		const FDreamUIFaceBrush* Chosen = nullptr;
		switch (InState)
		{
		case EUISelectableSelectionState::Hovered: Chosen = &Hovered; break;
		case EUISelectableSelectionState::Pressed: Chosen = &Pressed; break;
		case EUISelectableSelectionState::Disabled: Chosen = &Disabled; break;
		case EUISelectableSelectionState::Focused: Chosen = &Focused; break;
		default: Chosen = &Normal; break;
		}
		if (Chosen->Image == nullptr)
		{
			Chosen = &Normal;
		}
		return Chosen->Image != nullptr ? *Chosen : InFallback;
	}

	/**
	 * The three moments a control makes a noise, as a STYLE decision: a click should sound like the
	 * other clicks in this UI, which is the same argument the colours make.
	 *
	 * Pushed onto the selectable, which is what owns the playing (it knows the state it just entered
	 * and the widget to play from). A UDreamSelectableStyle asset on that selectable still wins,
	 * exactly as it does for the colours. Navigation gets no slot of its own -- a move fires Enter on
	 * the target, so HoveredSound already plays once per move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	TObjectPtr<USoundBase> HoveredSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	TObjectPtr<USoundBase> PressedSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Faces")
	TObjectPtr<USoundBase> ClickedSound = nullptr;
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

	/**
	 * The BOX's drawing per pointer state, where BoxBrush is one drawing tinted five ways. Nothing to
	 * do with the mark: the three brushes above answer "which of the three values is this", this one
	 * answers "is the pointer on it", and a check box shows both at once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (InlineEditConditionToggle))
	bool bOverride_StateFaces = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style", meta = (EditCondition = "bOverride_StateFaces"))
	FDreamUIStateFaces StateFaces;
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

	/**
	 * A different drawing per state, where FaceBrush is one drawing tinted five ways. Empty
	 * throughout, which is what keeps every existing button pixel-identical; FaceBrush is what an
	 * unstated state falls back to. ContentPadding is the resting padding its PressedPadding
	 * alternates with.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (InlineEditConditionToggle))
	bool bOverride_StateFaces = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style", meta = (EditCondition = "bOverride_StateFaces"))
	FDreamUIStateFaces StateFaces;
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

	/**
	 * The typeface, as the font data asset this library renders from -- the other half of what UMG
	 * packs into one FSlateFontInfo (the size is FontSize above, the outline is in the text style).
	 *
	 * NULL means "leave the paragraph on whatever font it already has", which is what every field
	 * that exists today is doing: the built-in tree never states one, so the text node keeps the
	 * project default. A style only ever ADDS a font here; it cannot take one away.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_Font = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_Font"))
	TObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_CornerRadius"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;

	/**
	 * The caret, and the highlight behind a selection. The one pair of colours a text field cannot
	 * do without and the only ones this sheet did not describe: the behaviour's own defaults were
	 * left to drive them, and its caret default sat four values away from Background above -- an
	 * invisible caret on the library's own theme. They follow the text, as a caret does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_CaretColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_CaretColor"))
	FColor CaretColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_SelectionColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_SelectionColor"))
	FColor SelectionColor = FColor(96, 140, 200, 128);

	/** How wide the caret is drawn, in UI units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_CaretWidth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_CaretWidth", ClampMin = "0.0"))
	float CaretWidth = 2.0f;

	/** Seconds between caret blinks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_CaretBlinkRate = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_CaretBlinkRate", ClampMin = "0.0"))
	float CaretBlinkRate = 0.5f;

	/**
	 * The words a rejected value is reported in -- what Slate's editable text box draws through its
	 * error-reporting widget, and the one piece of a text field's look that was missing entirely.
	 *
	 * Red by default because that is what an error means everywhere, and a project that disagrees has
	 * the same tick beside it as every other field here. The message itself is the CONTROL's
	 * (SetError): which value is wrong is content, not theme.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (InlineEditConditionToggle))
	bool bOverride_ErrorColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style", meta = (EditCondition = "bOverride_ErrorColor"))
	FColor ErrorColor = FColor(232, 96, 96, 255);
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

	/** An item that cannot be chosen. The face has FaceDisabled; the rows used to have nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ItemDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ItemDisabled"))
	FColor ItemDisabled = FColor(44, 47, 56, 255);

	/**
	 * An item the keyboard or the pad has landed on. The face already had FaceFocused; without the
	 * same answer for the rows, navigating INSIDE an open list showed nothing at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ItemFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ItemFocused"))
	FColor ItemFocused = FColor(96, 140, 200, 255);

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

	/**
	 * The FACE's drawing per state, where FaceBrush is one drawing tinted five ways, and the caption's
	 * colour per state when its foreground tint is ticked. Empty throughout, so an existing dropdown
	 * is unchanged: FaceBrush is the fallback, and TextColor stays the caption's colour while the
	 * tint bit is off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_StateFaces = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_StateFaces"))
	FDreamUIStateFaces StateFaces;

	/**
	 * Between the face's edge and the caption -- UMG's ContentPadding.
	 *
	 * The default IS what the control has always arranged, down to the number: ten on the left, and
	 * twenty-four on the right to clear the arrow glyph. Stating it here rather than leaving it in
	 * the realize call is what makes a wider face or a hidden arrow something a style can answer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (InlineEditConditionToggle))
	bool bOverride_ContentPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style", meta = (EditCondition = "bOverride_ContentPadding"))
	FMargin ContentPadding = FMargin(10.0f, 0.0f, 24.0f, 0.0f);
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

	/**
	 * Between the track's edge and the fill -- UMG's BorderPadding.
	 *
	 * Spent on the FILL, not on the track: the track still reaches both ends of what the bar spans,
	 * so the bar looks the same size and the fill sits inside it. Along the bar's axis the padding
	 * shortens the travel and moves the starting edge in; across it, the fill is thinner and stays
	 * centred between the two sides (an uneven pair shifts it, as it should).
	 *
	 * Zero is what this bar has always drawn, and stays the default for that reason. The Radial
	 * shape ignores it: a ring's fill sits exactly ON its track, and insetting it would make a
	 * second, smaller ring rather than a padded one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_BorderPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style", meta = (EditCondition = "bOverride_BorderPadding"))
	FMargin BorderPadding = FMargin(0.0f);
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

	/**
	 * How far the HANDLE is inset from the track, across the bar's axis -- Slate's
	 * FScrollBarStyle::Thickness beside its track, and the reason a real scroll bar reads as a pill
	 * running down a groove rather than as one solid slab. Zero (the default) is the handle filling
	 * the track, which is what this bar has always drawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_HandlePadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_HandlePadding", ClampMin = "0.0"))
	float HandlePadding = 0.0f;

	/**
	 * The margin around the WHOLE bar -- UMG's ScrollbarPadding, and a different measurement from
	 * HandlePadding above, which insets the handle inside its track.
	 *
	 * Spent on the bar's rect rather than on the track's, so the track still reaches both ends of
	 * what the bar spans and the handle never travels past it. A scroll box counts this into its
	 * gutter, because the gutter is everything the viewport gives up for the bar to sit there.
	 * Zero is what every bar in this library has drawn, and stays the default for that reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (InlineEditConditionToggle))
	bool bOverride_BarPadding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style", meta = (EditCondition = "bOverride_BarPadding"))
	FMargin BarPadding = FMargin(0.0f);

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

	/**
	 * A row that cannot be acted on. The rows used to push three pointer colours and leave this one
	 * to the behaviour's library default -- a flat grey belonging to no theme, and the one state a
	 * project sheet could not describe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowDisabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowDisabled"))
	FColor RowDisabled = FColor(44, 47, 56, 255);

	/**
	 * Focused by keyboard or gamepad, which is a different question from hovered: focus survives the
	 * pointer moving away, and on a pad there is no pointer at all. Pushing a colour here is what
	 * turns the selectable's focus visuals on -- they ship off, so before this a pad user watched
	 * the navigation land on a row that showed nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_RowFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_RowFocused"))
	FColor RowFocused = FColor(96, 140, 200, 255);

	/** How long a row's state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.2f;

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

	/**
	 * A different drawing per row STATE, where RowBrush is one drawing the row colours tint five
	 * ways. Empty throughout, which is what keeps every existing list pixel-identical; RowBrush is
	 * what an unstated state falls back to.
	 *
	 * The rows share one group rather than carrying one each, for the reason every other number in
	 * this struct is shared: a list's rows are the same row drawn many times, and a row that could
	 * be skinned individually would be a template, not a style.
	 *
	 * The foreground tint and the pressed padding in the group are not read here -- a row's label
	 * colour is TextColor above, and a row has no content padding of its own to alternate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (InlineEditConditionToggle))
	bool bOverride_StateFaces = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style", meta = (EditCondition = "bOverride_StateFaces"))
	FDreamUIStateFaces StateFaces;

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

	/** The typeface, as a font data asset. Null leaves the value's paragraph on the font it has. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (InlineEditConditionToggle))
	bool bOverride_Font = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style", meta = (EditCondition = "bOverride_Font"))
	TObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;

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
	 * Focused by keyboard or gamepad. A ring is the one control in the library a pad reaches FIRST,
	 * and pushing a colour here is what turns the selectable's focus visuals on -- they ship off, so
	 * stepping round the wheel with the stick used to move a highlight nothing drew.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_WedgeFocused = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_WedgeFocused"))
	FColor WedgeFocused = FColor(96, 140, 200, 245);

	/** How long a wedge's state change takes, in seconds. Zero snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Style|Colors", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.12f;

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

/**
 * A tile view: a list whose rows are a grid of tiles.
 *
 * Built the way FDreamTreeViewStyle is -- carrying a whole FDreamListStyle rather than restating its
 * fields -- because a tile view IS a list that wraps, and a project that styles its lists styles the
 * tiles' faces, hover and selection with them. What it adds is the one thing a row does not have: a
 * WIDTH of its own, because a tile is not as wide as the view.
 *
 * FDreamListStyle::RowHeight stays the tile's height, so there is exactly one place a tile's size is
 * written down; TileWidth is its partner and lives here.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTileViewStyle
{
	GENERATED_BODY()

	/** The tiles and the viewport, shared whole with the list. RowHeight is the tile's height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (InlineEditConditionToggle))
	bool bOverride_List = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (EditCondition = "bOverride_List"))
	FDreamListStyle List;

	/** A tile's width. Its height is FDreamListStyle::RowHeight -- one tile, one size, two fields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TileWidth = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (EditCondition = "bOverride_TileWidth", ClampMin = "1.0"))
	float TileWidth = 96.0f;

	/** Between tiles across a row. Down the column the gap is FDreamListStyle::RowSpacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (InlineEditConditionToggle))
	bool bOverride_TileSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View Style", meta = (EditCondition = "bOverride_TileSpacing", ClampMin = "0.0"))
	float TileSpacing = 4.0f;
};

/** Which silhouette a throbber draws. The progress bar's Bar/Radial split, for the same reason. */
UENUM(BlueprintType)
enum class EDreamThrobberShape : uint8
{
	/** A row of pieces, UMG's Throbber. */
	Linear,
	/** Pieces around a circle, UMG's CircularThrobber. */
	Circular,
};

/**
 * A throbber: N identical pieces, animating, saying only "something is happening".
 *
 * One control and one style for UMG's two, the call this library already made for the progress bar's
 * Bar and Radial and the slider's Direction: the pieces, the period and the colours are identical,
 * and the only thing that differs is where the pieces are put. The palette still offers two rows.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamThrobberStyle
{
	GENERATED_BODY()

	/** How many pieces. UMG ships three. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_NumberOfPieces = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_NumberOfPieces", ClampMin = "1", ClampMax = "32"))
	int32 NumberOfPieces = 3;

	/** One piece's size. Square by default, which is what a dot is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_PieceSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_PieceSize"))
	FVector2D PieceSize = FVector2D(10.0, 10.0);

	/** Between pieces, in the Linear shape. The circular one spreads them over its Radius instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_PieceSpacing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_PieceSpacing", ClampMin = "0.0"))
	float PieceSpacing = 6.0f;

	/** Circular only: how far out the pieces ride, measured from the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_Radius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_Radius", ClampMin = "1.0"))
	float Radius = 16.0f;

	/** How long one full cycle takes, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_Period = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_Period", ClampMin = "0.01"))
	float Period = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_PieceColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_PieceColor"))
	FColor PieceColor = FColor(230, 233, 240, 255);

	/** How faint a piece gets at the bottom of its cycle. One keeps every piece solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_MinOpacity = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_MinOpacity", ClampMin = "0.0", ClampMax = "1.0"))
	float MinOpacity = 0.15f;

	/** A piece's corner rounding. Half the piece size is a dot; zero is a square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_CornerRadius", ClampMin = "0.0"))
	float CornerRadius = 5.0f;

	/** A piece's face. Empty is the plain rounded rect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (InlineEditConditionToggle))
	bool bOverride_PieceBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throbber Style", meta = (EditCondition = "bOverride_PieceBrush"))
	FDreamUIFaceBrush PieceBrush;
};

/**
 * A border: a brush, a padding and one child.
 *
 * UMG's Border is a panel with a look, and this library already offers that shape as a PANEL entry
 * (an overlay carrying an image). This is the CONTROL spelling of it -- the one that reads the
 * project sheet, so every bordered box in a project restyles at once -- and it is the reason a
 * control class exists beside the panel: a panel's brush is authored per instance, and a control's
 * is authored once for the whole project.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamBorderStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_Background = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_Background"))
	FColor Background = FColor(44, 49, 60, 255);

	/** Between the border's edge and whatever is inside it -- UMG's Padding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_Padding = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_Padding"))
	FMargin Padding = FMargin(8.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_CornerRadius", ClampMin = "0.0"))
	float CornerRadius = 5.0f;

	/** The border's own line. Zero draws no outline, which is UMG's default border. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_BorderThickness = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_BorderThickness", ClampMin = "0.0"))
	float BorderThickness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_BorderColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_BorderColor"))
	FColor BorderColor = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;
};

/**
 * A rich text block: one paragraph that reads markup.
 *
 * Only the text's own look, because that is all a rich text block is -- no face, no padding, no
 * states. What makes it RICH is the two data assets the markup resolves against, and those are the
 * control's properties rather than the style's: they are content (which tag means which sprite),
 * not theme.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamRichTextStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextColor = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (EditCondition = "bOverride_TextColor"))
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (InlineEditConditionToggle))
	bool bOverride_FontSize = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (EditCondition = "bOverride_FontSize"))
	float FontSize = 15.0f;

	/** The DEFAULT typeface, as a font data asset -- markup may still switch it per run. Null leaves
	 *  the paragraph on the font it has, which is what every existing block uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (InlineEditConditionToggle))
	bool bOverride_Font = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (EditCondition = "bOverride_Font"))
	TObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;

	/**
	 * Outline, drop shadow and glow -- what UMG spells as the shadow half of an FTextBlockStyle, and
	 * what this library has always drawn from FDreamTextStyle's underlay.
	 *
	 * Here rather than on the control because it is theme: a project decides that its prose carries a
	 * shadow, not each paragraph. Default-constructed means every effect switched off (each one's
	 * colour ships with alpha zero), which is what a rich text block looked like before this existed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (InlineEditConditionToggle))
	bool bOverride_TextStyle = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (EditCondition = "bOverride_TextStyle"))
	FDreamTextStyle TextStyle;

	/**
	 * A material to draw the glyphs with instead of the built-in one -- UMG's default font material.
	 * Null keeps the library's own text material, which is what every existing block uses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (InlineEditConditionToggle))
	bool bOverride_OverrideMaterial = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rich Text Style", meta = (EditCondition = "bOverride_OverrideMaterial"))
	TObjectPtr<UMaterialInterface> OverrideMaterial = nullptr;
};

/**
 * A menu anchor: the popup's look, and how it is placed against whatever opened it.
 *
 * Where UMG's MenuAnchor keeps Placement as a widget property, it lives here because it is the same
 * kind of decision as a corner radius -- a project wants every menu to open the same way -- and
 * because the control still exposes it per instance through the style's override bits.
 *
 * The placement ENUM is not declared here: it is EDreamMenuPlacement, which belongs to
 * UDreamLayoutContainerMenuAnchor along with the arithmetic that reads it. One enum and one set of
 * placement maths for the layout panel and the control, so a menu opened by either lands in the same
 * place -- and so there is nothing to keep in step when a placement is added.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamMenuAnchorStyle
{
	GENERATED_BODY()

	/** Where the menu opens relative to the widget that anchors it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_Placement = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_Placement"))
	EDreamMenuPlacement Placement = EDreamMenuPlacement::BelowAnchor;

	/** Between the anchor's edge and the menu's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_Offset = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_Offset"))
	float Offset = 2.0f;

	/** The menu's own face, behind whatever the menu class draws. Transparent leaves it to the menu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_Background = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_Background"))
	FColor Background = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_CornerRadius = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_CornerRadius", ClampMin = "0.0"))
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_BackgroundBrush = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_BackgroundBrush"))
	FDreamUIFaceBrush BackgroundBrush;

	/** How long the menu takes to fade in and out. Zero snaps, and so does a control with no world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (InlineEditConditionToggle))
	bool bOverride_TransitionDuration = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor Style", meta = (EditCondition = "bOverride_TransitionDuration", ClampMin = "0.0"))
	float TransitionDuration = 0.12f;
};
