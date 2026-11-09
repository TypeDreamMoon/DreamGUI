// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Designer/DreamDesignerHandles.h"

/*
 * The viewport handles of a selection's effects (Designer/DreamDesignerHandles.h): kinds their properties are tagged with,
 * meta=(DreamGUI_Handle="Kind"), which the editor module registers. Each tagged property's provider is named Kind.Property.
 *  - MaskShape, on a widget's ClipMask: its clip and the mask on it -- the clip rect's sides (its margins), the rect's
 *    corners, a polygon's roundness, a star's or a ring's inner size, and the feather;
 *  - GlassShape, on a glass's CornerRadius: its corners, its edge's width and where its highlight's light comes from;
 *  - Shadow, on a layer style's Style and on a rect block's OuterShadowAngle: the style's shadows, or the rect block's own
 *    two, dragged where they fall -- angle and distance together; a style's shadow on the global light turns the global
 *    light, and every effect that uses it turns with it;
 * Each drag is one undo step and writes the .dui when the button comes up, as every provider's does. A corner, or a side's
 * feather, moves all four with it (as the widget's uniform corner setting says, for a clip); Alt moves it alone. An effect
 * that is off has no handles.
 *
 * Each handle's number is here, for the tests and for anything that drives the handles.
 */
namespace DreamDesignerEffectHandles
{
	DREAMGUIEDITOR_API extern const FName MaskKindName;
	DREAMGUIEDITOR_API FDreamDesignerHandleKindFactory MakeMaskKindFactory();

	namespace MaskIds
	{
		/** The clip rect's sides, moving ClippingMargin: left, top, right, bottom. */
		constexpr int32 FirstSide = 0;
		/** A rect mask's corners, ClippingCornerRadius in its order: right-bottom, right-top, left-top, left-bottom. */
		constexpr int32 FirstCorner = 10;
		/** A polygon's roundness. */
		constexpr int32 Roundness = 20;
		/** A star's inner corners, a ring's hole: InnerRatio. */
		constexpr int32 InnerRatio = 21;
		/** The feather: a shape's one, or a rect's sides -- left, top, right, bottom. */
		constexpr int32 FirstFeather = 30;
	}

	DREAMGUIEDITOR_API extern const FName GlassKindName;
	DREAMGUIEDITOR_API FDreamDesignerHandleKindFactory MakeGlassKindFactory();

	namespace GlassIds
	{
		/** Its corners, CornerRadius in its order: right-bottom, right-top, left-top, left-bottom. */
		constexpr int32 FirstCorner = 10;
		constexpr int32 EdgeWidth = 20;
		/** Where the highlight's light comes from: LightAngle. */
		constexpr int32 LightAngle = 30;
	}

	DREAMGUIEDITOR_API extern const FName ShadowKindName;
	DREAMGUIEDITOR_API FDreamDesignerHandleKindFactory MakeShadowKindFactory();

	namespace ShadowIds
	{
		/** A layer style's drop shadows and inner shadows, by their place in its lists. */
		constexpr int32 FirstDropShadow = 100;
		constexpr int32 FirstInnerShadow = 200;
		/** A rect block's own two. */
		constexpr int32 RectBlockOuterShadow = 300;
		constexpr int32 RectBlockInnerShadow = 301;
	}
}
