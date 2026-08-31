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

	/** The background. State colours (BoxNormal/Hovered/Pressed) tint it, exactly as with no image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FDreamUIFaceBrush BoxBrush;

	/**
	 * The mark, per state, as images -- UMG's CheckedImage / UncheckedImage / UndeterminedImage.
	 * A state whose brush holds an image draws it (sized by the brush's ImageSize, else TickSize);
	 * a state whose brush is empty keeps the built-in glyph -- check mark, nothing, em-dash.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FDreamUIFaceBrush CheckedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
	FDreamUIFaceBrush UncheckedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle Style")
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

	/** Radial only: the ring's outer size. The bar shape takes its length from wherever it is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial")
	FVector2D RadialSize = FVector2D(64.0, 64.0);

	/** Radial only: how thick the ring is, as a fraction of half its size. 1 is a full pie. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RadialThickness = 0.25f;

	/** Radial only: where zero percent sits, in degrees clockwise from twelve o'clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress Bar Style|Radial")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	float Thickness = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FColor TrackColor = FColor(38, 42, 52, 255);

	/** The handle carries the pointer transition, the way the slider's does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FColor HandleNormal = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FColor HandleHovered = FColor(96, 105, 126, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FColor HandlePressed = FColor(120, 132, 158, 255);

	/** Half the thickness is a capsule, which is what a bar reads as with nobody styling it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FDreamUIFaceBrush TrackBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar Style")
	FDreamUIFaceBrush HandleBrush;
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamScrollBoxStyle
{
	GENERATED_BODY()

	/** Transparent by default: a scroll box is a viewport, not a panel -- the content brings its look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style")
	FColor Background = FColor(0, 0, 0, 0);

	/** Between the viewport's edge and the content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style")
	FMargin Padding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style")
	float CornerRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style")
	FDreamUIFaceBrush BackgroundBrush;

	/** The bars this box shows, when it shows them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Box Style")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor Background = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	float RowHeight = 30.0f;

	/** Between rows. Zero is the dense list UMG draws by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	float RowSpacing = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FMargin Padding = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor RowNormal = FColor(0, 0, 0, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor RowHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor RowSelected = FColor(0, 119, 255, 255);

	/** Every other row, when bAlternatingRowColors is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor RowAlternate = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FColor TextColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FDreamUIFaceBrush BackgroundBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FDreamUIFaceBrush RowBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List Style")
	FDreamScrollBarStyle Bar;
};

/** A tree: a list whose rows carry an indent and a twisty. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTreeViewStyle
{
	GENERATED_BODY()

	/** The rows and the viewport, shared whole with the list -- a tree IS a list that indents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	FDreamListStyle List;

	/** Per depth level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	float IndentPerLevel = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	FVector2D TwistySize = FVector2D(14.0, 14.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	FColor TwistyColor = FColor(140, 147, 166, 255);

	/** Empty keeps the built-in glyphs, the way the check box's state brushes do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	FDreamUIFaceBrush ExpandedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View Style")
	FDreamUIFaceBrush CollapsedBrush;
};

/** A tab view: a strip of tabs over a switcher of pages. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTabViewStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	float TabHeight = 34.0f;

	/** Between tabs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	float TabSpacing = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FMargin TabPadding = FMargin(14.0f, 4.0f, 14.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor TabNormal = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor TabHovered = FColor(60, 67, 82, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor TabPressed = FColor(38, 42, 52, 255);

	/** The tab whose page is showing. Its own colour, because selection is not a pointer state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor TabSelected = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor LabelColor = FColor(170, 178, 196, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor LabelSelectedColor = FColor(240, 244, 252, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	float FontSize = 15.0f;

	/** The line under the selected tab. Zero height turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	float IndicatorThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor IndicatorColor = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FColor PageBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FMargin PagePadding = FMargin(12.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FDreamUIFaceBrush TabBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tab View Style")
	FDreamUIFaceBrush PageBrush;
};

/** A dialog: a dimmer over the screen, a panel on it, a title, content and buttons. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamDialogStyle
{
	GENERATED_BODY()

	/** The screen behind. Alpha is the whole of it -- a dimmer nobody can see is a dimmer nobody wants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FColor DimmerColor = FColor(0, 0, 0, 160);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FVector2D PanelSize = FVector2D(420.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FColor PanelBackground = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FMargin PanelPadding = FMargin(20.0f, 16.0f, 20.0f, 16.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FColor TitleColor = FColor(240, 244, 252, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	float TitleFontSize = 19.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FColor MessageColor = FColor(198, 205, 220, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	float MessageFontSize = 15.0f;

	/** Between the title, the message and the button row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	float Spacing = 12.0f;

	/** Between the buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	float ButtonSpacing = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	float CornerRadius = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FDreamUIFaceBrush PanelBrush;

	/** The buttons are Native.Button instances; this is the style they wear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FDreamButtonStyle Button;

	/** The confirming button, when a dialog wants it to stand out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Style")
	FDreamButtonStyle PrimaryButton;
};

/** An expandable area: a header that toggles, and the content it hides. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamExpandableAreaStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	float HeaderHeight = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor HeaderNormal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor HeaderHovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor HeaderPressed = FColor(38, 42, 52, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FMargin HeaderPadding = FMargin(10.0f, 0.0f, 10.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor ContentBackground = FColor(44, 49, 60, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FMargin ContentPadding = FMargin(10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FVector2D ArrowSize = FVector2D(14.0, 14.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FColor ArrowColor = FColor(140, 147, 166, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FDreamUIFaceBrush HeaderBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FDreamUIFaceBrush ContentBrush;

	/** Empty keeps the built-in glyphs, as with the check box's state brushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FDreamUIFaceBrush ExpandedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area Style")
	FDreamUIFaceBrush CollapsedBrush;
};

/** A key binder: a button whose label is the bound key, and which listens when pressed. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamInputKeySelectorStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	float Height = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FColor Normal = FColor(52, 57, 70, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FColor Hovered = FColor(74, 81, 98, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FColor Pressed = FColor(38, 42, 52, 255);

	/** While it is listening. A different colour is the whole of the "press a key now" feedback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FColor Listening = FColor(0, 119, 255, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FColor LabelColor = FColor(230, 233, 240, 255);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	float FontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	FMargin ContentPadding = FMargin(12.0f, 4.0f, 12.0f, 4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
	float CornerRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Key Selector Style")
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
