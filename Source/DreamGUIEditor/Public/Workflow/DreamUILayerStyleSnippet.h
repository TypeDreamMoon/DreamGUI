// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamWidgetTree.h"
#include "UObject/StrongObjectPtr.h"

class UDreamLayerStyle;
class UDreamLayerStylePreset;
class UDreamVisual;
class UDreamWidget;
class UDreamWidgetTree;

/** Which parts of a widget's look a copied layer style, a pasted one or a style preset carries. */
enum class EDreamUIStyleParts : uint8
{
	None = 0,
	/** The layer style: its effects, and the preset it follows when it follows one (UDreamLayerStyle). */
	Style = 1 << 0,
	/** The element's blend mode (its visual's BlendMode) and the widget's opacity (RenderOpacity). */
	Blending = 1 << 1,
	/** The widget's clip, its corner radius and margin, its clip mask, and clip-to-layer-below. */
	Mask = 1 << 2,
	All = Style | Blending | Mask,
};
ENUM_CLASS_FLAGS(EDreamUIStyleParts);

/**
 * A widget's layer look as lines of `.dui`: what the layer style clipboard holds, what a style text preset is
 * (UDreamGUISettings::StylePresets), and what a preset dragged from the preset library carries.
 *
 * The lines are the ones a node's block takes, so they can be pasted into a `.dui` as they are:
 *
 *     // DreamGUI layer style
 *     BlendMode = Multiply
 *     RenderOpacity = 0.8
 *     Clipping = ClipToBounds
 *     ClipMask.Shape = Ellipse
 *     + DreamLayerStyle {
 *         Style.DropShadows = ["(Angle=120,Distance=6,Size=10,Opacity=0.6)"]
 *         Style.BevelEmboss.bEnabled = true
 *     }
 *
 * WHAT IT IS READ WITH. The builder that compiles a `.dui`: the lines are put into a node of their own and built, and the
 * values are read off the objects that node built (GetWidget, GetVisual, GetLayerStyle). So a snippet means exactly what
 * the same lines mean in a file -- enum spellings, colours, asset paths, arrays of structs -- with no second reader to drift
 * from it. Writing goes the other way through the write-back's own printer (FDreamUITextWriteBack::PrintValueLiteral), and
 * only what differs from the class's defaults is written; Clipping is always written with the mask, so that pasting a mask
 * also says whether it clips.
 *
 * Which parts a snippet carries is read off its lines: the `+` line of a layer style (Style), BlendMode or RenderOpacity
 * (Blending), and Clipping, ClippingCornerRadius, ClippingMargin, ClipMask or bClipToBelow (Mask). A part the lines do not
 * name is not touched by a paste. Other lines are built and ignored.
 */
struct DREAMGUIEDITOR_API FDreamUILayerStyleSnippet
{
	/** The first line of every snippet written here: a comment, so a `.dui` it is pasted into still reads it. */
	static const TCHAR* HeaderLine;

	/** The lines. */
	const FString& GetText() const { return Text; }
	/** The parts the lines carry. */
	EDreamUIStyleParts GetParts() const { return Parts; }
	bool HasPart(EDreamUIStyleParts InPart) const { return EnumHasAnyFlags(Parts, InPart); }
	bool IsEmpty() const { return Parts == EDreamUIStyleParts::None; }

	/**
	 * Read InText. False, with the reason, when it does not build or names none of the parts: a clipboard holding anything
	 * else -- a widget copied from the hierarchy, a line of text -- is no layer style.
	 */
	static bool Parse(const FString& InText, FDreamUILayerStyleSnippet& OutSnippet, FText* OutError = nullptr);

	/**
	 * InWidget's look as lines: the parts of InParts it has. A widget with no layer style gives no Style part, and a widget
	 * with nothing of InParts gives an empty snippet. InWidget may be a template or a preview widget; the values are its.
	 */
	static FDreamUILayerStyleSnippet FromWidget(const UDreamWidget* InWidget, EDreamUIStyleParts InParts);

	/**
	 * A style as lines: the effects, the preset they follow when InPreset is given, and the blending when InBlendMode is
	 * set. What a style preset becomes when it is applied from the preset library or saved as text.
	 */
	static FDreamUILayerStyleSnippet FromStyle(const FDreamLayerEffects& InStyle, const UDreamLayerStylePreset* InPreset = nullptr,
		TOptional<EDreamUIBlendMode> InBlendMode = TOptional<EDreamUIBlendMode>(), float InOpacity = 1.0f);

	/** The layer style the lines built: its effects (GetStyle) and its preset. Null without a Style part. */
	const UDreamLayerStyle* GetLayerStyle() const;
	/** The widget the lines built: its opacity and its clip. Null when the lines did not build. */
	const UDreamWidget* GetWidget() const;
	/** The element the lines built, whose BlendMode is the blending part's. */
	const UDreamVisual* GetVisual() const;
	/** The blending part's blend mode and opacity. False without one. */
	bool GetBlending(EDreamUIBlendMode& OutBlendMode, float& OutOpacity) const;

	/** The properties each part is, by head: what a paste copies, and what it reports to the write-back as touched. */
	static TConstArrayView<FName> GetMaskPropertyNames();
	static TConstArrayView<FName> GetStylePropertyNames();
	static FName GetBlendModePropertyName();
	static FName GetOpacityPropertyName();

private:
	FString Text;
	EDreamUIStyleParts Parts = EDreamUIStyleParts::None;
	/** The tree the lines built, kept for the values on it. One node; a layer style on it when there is a Style part. */
	TStrongObjectPtr<UDreamWidgetTree> Built;
};
