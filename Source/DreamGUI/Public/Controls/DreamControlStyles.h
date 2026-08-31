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

	/** Of the box. The faces are procedural rects now, which is where the UMG feel mostly lives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FDreamUIFaceBrush BoxBrush;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button Style")
	FDreamUIFaceBrush FaceBrush;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FDreamUIFaceBrush FillBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slider Style")
	FDreamUIFaceBrush HandleBrush;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input Style")
	FDreamUIFaceBrush BackgroundBrush;
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
	float FontSize = 15.0f;

	/** Face and list share it; the items inside the list stay square against its edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FDreamUIFaceBrush FaceBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FDreamUIFaceBrush ListBrush;

	/** The rows, template and duplicates alike; a row at rest also shows the list background colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dropdown Style")
	FDreamUIFaceBrush ItemBrush;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	float Height = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	FColor TrackColor = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	FColor FillColor = FColor(0, 119, 255, 255);

	/** Half the default height: a capsule, the silhouette UMG's bar fakes with a rounded brush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	float CornerRadius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style")
	FDreamUIFaceBrush FillBrush;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FVector2D BoxSize = FVector2D(26.0, 26.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FVector2D DotSize = FVector2D(12.0, 12.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FColor BoxNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FColor BoxHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FColor BoxPressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FColor DotChecked = FColor(0, 119, 255, 255);

	/** Transparent, not absent: the dot exists either way, unchecked just does not show it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FColor DotUnchecked = FColor(0, 119, 255, 0);

	/**
	 * Half of BoxSize by default -- that is the whole of what makes a radio round. Kept a knob
	 * rather than derived, so a project that squares its radios is one edit, not a subclass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	float CornerRadius = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FDreamUIFaceBrush BoxBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radio Button Style")
	FDreamUIFaceBrush DotBrush;
};

/** A spin box: a numeric field between a decrement face and an increment face. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamSpinBoxStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	float Height = 34.0f;

	/** The two step buttons share one colour set; each still carries its own selectable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor ButtonNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor ButtonHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor ButtonPressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor FieldBackground = FColor(38, 42, 52, 255);

	/**
	 * While hovered. The field's behaviour is a selectable and WILL tint the background with its
	 * transition colours -- left unset those default to white, which is exactly how the first build
	 * of the text input shipped as a white bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor FieldBackgroundHovered = FColor(48, 53, 66, 255);

	/** The value and the step glyphs alike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	float ButtonWidth = 26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	float CornerRadius = 5.0f;

	/** Both step faces; each keeps its own selectable, they only share the look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FDreamUIFaceBrush ButtonBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spin Box Style")
	FDreamUIFaceBrush FieldBrush;
};
