// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Text/DreamTextDisplayList.h"
#include "Core/Text/DreamTextPaint.h"

class FDreamUIGeometry;
/** UDreamGUISettings::SmallTextEffectFace's type, declared with the setting (DreamGUISettings.h); its first value, Field, is 0. */
enum class EDreamSmallTextEffectFace : uint8;

/**
 * What UV2.x of a glyph quad tells the shader. DreamUIShade.ush and DreamUIText.ush hold the same numbers: change one,
 * change both. For font marks 1-3 the shader first takes the paint slot off (GetSlot, StripSlot: a painted quad's code is
 * its code plus SlotStride times its slot), then decodes what is left, in this order:
 *  0. Below ColorSilhouetteThreshold (ColorSilhouette): a colour glyph a mesh modifier copied -- a shadow, an outline --
 *     drawn as its silhouette, the vertex RGB at the glyph's alpha times the vertex alpha.
 *  1. UV2.x < ColorThreshold: a colour glyph (premultiplied BGRA; vertex RGB ignored, vertex alpha kept). Below
 *     ColorEffectsThreshold (ColorEffects) it is the glyph's effects copy, which draws the underlay from the glyph's alpha
 *     in the style's underlay colour -- on a field font sampled at UV minus the underlay offset (the painter insets that
 *     copy by half the field's spread, which keeps every sample inside the glyph's padded cell), on a bitmap font where
 *     the copy is (the painter moved it by the shadow offset, as it does its plain glyphs' shadows); otherwise
 *     (ColorFace) its face. UV3.y holds the cell's texels per em (FDreamUICharData::ColorTexelsPerEm), which turns the
 *     offset in em into UV.
 *  2. Otherwise Layer = floor((UV2.x + 8) / 16) and DilateEm = UV2.x - 16 * Layer (DreamUIText_UnpackGlyphChannel).
 *     Layer 3 (CoverageBase + phase): a coverage glyph, the phase (0 to 3) being what DilateEm holds; UV3.y holds the
 *     contrast plus CoverageLinearTarget when the target blends in linear space. Layers 0, 1 and 2
 *     (DilateEm + FieldLayerStride * layer): a field glyph's face, effects, or both, as ever; UV3.y holds the glow boost.
 * UV2.y and UV3.x hold the lyric fill on every kind of quad. A bitmap font's plain glyphs write 0 and are not decoded.
 * Every code without its slot lies in [-SlotStride / 2, SlotStride / 2), which is what makes the slot's decode exact.
 *
 * Slots (FDreamTextPaints): 0 paints nothing -- every quad of a text with no paint, every colour glyph, every copy a bitmap
 * font's shadow or outline or a mesh modifier makes; 1 (TextSlot) the text's own paints; FirstTagSlot to MaxSlot its
 * rich-text tag paints. A painted quad's UV4 is where it sits in its slot's boxes (FDreamUIMeshVertex::UV4).
 */
namespace DreamTextQuadCode
{
	/** A painted quad adds SlotStride * its slot to its code. */
	constexpr float SlotStride = 128.0f;
	/** The text's own paints (FDreamTextStyle's FacePaint, OutlinePaint and OverlayPaint). */
	constexpr int32 TextSlot = 1;
	/** The first and the last slot of the tag paints (`<gradient=Name>`, a custom style that sets a paint). */
	constexpr int32 FirstTagSlot = 2;
	constexpr int32 MaxSlot = 9;
	/** Slots there are, 0 included: the size of a per-slot table. */
	constexpr int32 SlotCount = MaxSlot + 1;
	/** A colour glyph a mesh modifier copied, drawn as its silhouette; below ColorSilhouetteThreshold. Always slot 0. */
	constexpr float ColorSilhouette = -33.0f;
	constexpr float ColorSilhouetteThreshold = -24.5f;
	/** InCode with InSlot's paints. */
	inline float AddSlot(float InCode, int32 InSlot) { return InCode + SlotStride * (float)InSlot; }
	/** The slot of a quad's UV2.x: 0 for a quad that paints nothing. */
	inline int32 GetSlot(float InPacked) { return FMath::FloorToInt32((InPacked + 0.5f * SlotStride) / SlotStride); }
	/** A quad's UV2.x with its slot taken off: what every decode below reads. */
	inline float StripSlot(float InPacked) { return InPacked - SlotStride * (float)GetSlot(InPacked); }

	/** A field glyph writes DilateEm + FieldLayerStride * Layer: Layer 0 its face, 1 its effects, 2 both. DilateEm >= 0. */
	constexpr float FieldLayerStride = 16.0f;
	/** A coverage glyph writes CoverageBase + phase, the phase being 0 to 3: layer 3. */
	constexpr float CoverageBase = 48.0f;
	/** Added to a coverage glyph's UV3.y (its contrast, below 16) when the target blends in linear space. */
	constexpr float CoverageLinearTarget = 16.0f;
	/** A colour glyph's face copy, and its effects copy. */
	constexpr float ColorFace = -1.0f;
	constexpr float ColorEffects = -17.0f;
	/** UV2.x below this is a colour glyph; below ColorEffectsThreshold, its effects copy. */
	constexpr float ColorThreshold = -0.5f;
	constexpr float ColorEffectsThreshold = -8.5f;
}

/** What a paint found out about small-text coverage, written when FDreamTextCoverageParams::Report asks for it. */
struct FDreamTextCoverageReport
{
	/** Items drawn from coverage glyphs. */
	int32 CoverageItems = 0;
	/**
	 * Items under the threshold drawn from their field quad this time because their coverage glyph was still being made
	 * (or not drawn yet at all, when their field glyph is pending too): the text repaints when the font's
	 * OnCoverageGlyphsChanged says it landed.
	 */
	int32 PendingItems = 0;
};

/**
 * Small text from coverage glyphs: whether to, from which font, and where the device pixel grid lies. Filled by the
 * text component once its gate passed (UDreamText); off by default, which paints exactly as before.
 *
 * The grid: x and y are the painter's coordinates -- FDreamTextGlyphItem::Pen's, the text widget's local space, x right
 * and y UP (vertex positions are (0, x, y)). u = DeviceScale * x + SnapOrigin.X and v = DeviceScale * y + SnapOrigin.Y
 * are device pixels, u rightward and v upward, and whole values of each lie on pixel boundaries of the render target.
 * The component measures them through the matrices its root canvas is drawn with, onto the pixels of what the canvas
 * renders into (its render target, or the screen), from that target's left and top edges (a point there has u = 0,
 * v = 0; inside the target v is negative). Per item, with GlyphSize the item's:
 *  - drawn from coverage when it emits a glyph quad (Kind Glyph, bEmit), GlyphSize * DeviceScale <= MaxPixelSize, the
 *    glyph is not a colour glyph, and Font->GetCoverageGlyph(Glyph.FaceIndex, Glyph.GlyphIndex,
 *    round(GlyphSize * RasterScale * 64), the item's synthetic bold/italic) answers with a glyph that is not pending;
 *    otherwise from its field quad, as before. An item whose field glyph is still pending (it counts but does not emit)
 *    is drawn from coverage the same way once its coverage glyph is ready, its face alone. A coverage glyph is a face and
 *    nothing else: a text whose quads come with a bitmap font's shadow or outline copies is never drawn from coverage,
 *    and a text with effects only through the effects hybrid (EffectFace), each coverage item drawing its field quad's
 *    effects copy, moved by Delta below, under its coverage face;
 *  - the baseline row is v' = floor(v(Pen.Y) + 0.5); the column and phase are q = floor(4 * u(Pen.X) + 0.5),
 *    I = floor(q / 4), phase = q - 4 * I: the face is drawn as if the pen were at (q / 4, v'), Delta =
 *    ((q / 4 - u(Pen.X)), (v' - v(Pen.Y))) / DeviceScale away from where the field draws it;
 *  - the quad is [I + BitmapLeft, I + BitmapLeft + Width] x [v' + BitmapTop - Height, v' + BitmapTop] in (u, v), mapped
 *    back by x = (u - SnapOrigin.X) / DeviceScale and y = (v - SnapOrigin.Y) / DeviceScale, its UVs the glyph's texels
 *    exactly; no italic shear and no bold shift (both are in the raster); UV2.x = CoverageBase + phase,
 *    UV3.y = Contrast + CoverageLinearTarget * bLinearTarget.
 * While bEnabled, underline and strikethrough strips keep their solid texel and have their top and bottom snapped to
 * device rows, at least one pixel apart: the thickness rounded to whole rows, placed about the strip's own centre. Lines,
 * carets, visual runs and every item's place in the display list are the same either way: coverage replaces quads, never
 * positions the layout made.
 */
struct FDreamTextCoverageParams
{
	/** The text passed its gate. Off: every item paints from its field quad. */
	bool bEnabled = false;
	/** Where coverage glyphs come from: the text's font. Not null while bEnabled; valid for the paint only. */
	UDreamUIFontData_BaseObject* Font = nullptr;
	/**
	 * S: device pixels per local unit, as the canvas draws them -- in the usual case the canvas scale, times a render
	 * target's resolution scale, times the text's uniform scale relative to the root canvas widget.
	 */
	float DeviceScale = 1.0f;
	/** The scale glyphs are rasterized at, kept within 1% of DeviceScale by the component's hysteresis. */
	float RasterScale = 1.0f;
	/** Items above this many device pixels per em paint from the field (UDreamUIFontData_BaseObject::GetCoverageMaxPixelSize). */
	float MaxPixelSize = 0.0f;
	/** The device grid's origin in (u, v); see the struct. */
	FVector2f SnapOrigin = FVector2f::ZeroVector;
	/** Skia's text contrast, UDreamGUISettings::SmallTextContrast. */
	float Contrast = 1.0f;
	/** The canvas blends in linear space (a RenderTarget canvas: gamma 1); the shader then skips its gamma-correcting step. */
	bool bLinearTarget = false;
	/** Where the paint reports what it did; reset by the painter first. Null: no report. */
	FDreamTextCoverageReport* Report = nullptr;
	/**
	 * A text whose style has effects (outline, glow, underlay), and whose gate let it draw from coverage all the same: how
	 * a coverage item's face goes over its effects (UDreamGUISettings::SmallTextEffectFace, as the component read it).
	 * Field, the zero value, keeps every item of such a text on the field, as before. Otherwise a coverage item writes its
	 * field quad as the effects copy (grown as ever, its own block), moved by the coverage face's snap shift -- and by half
	 * the synthetic bold up -- so the two line up, then its coverage face, hinted or, for Unhinted, raster with
	 * EDreamUICoverageGlyphFlags::Unhinted; Auto is Unhinted for an item whose outline is visible but under 2 device pixels
	 * (0 < OutlineWidthEm * GlyphSize * DeviceScale < 2), Hinted otherwise -- a text with no outline included: its glow or
	 * underlay shows no offset.
	 */
	EDreamSmallTextEffectFace EffectFace{};
	/** The style's outline width in em, 0 with no visible outline: what Auto measures. */
	float OutlineWidthEm = 0.0f;
};

/**
 * One paint slot of a text, as the painter sees it: what its layers paint with and the boxes its quads' UV4 is measured in.
 * A slot with no layer is not used, and nothing is written with it.
 */
struct FDreamTextPaintSlot
{
	/** The gradients the slot's layers paint with, null for a layer it does not paint; valid for the paint only. Tag slots paint the face alone. */
	const FDreamGradient* Face = nullptr;
	const FDreamGradient* Outline = nullptr;
	const FDreamGradient* Overlay = nullptr;
	/** What UV4 runs from 0 to 1 across, across and down. Slot 1: the style's PaintBoxHorizontal and PaintBoxVertical; tag slots: Run on both. */
	EDreamTextPaintBox HorizontalBox = EDreamTextPaintBox::TextBlock;
	EDreamTextPaintBox VerticalBox = EDreamTextPaintBox::TextBlock;
	/** The box's width over its height when both boxes are single (TextBlock or ContentBox), else 1: what the text table holds for the slot. */
	float BoxAspect = 1.0f;

	bool IsUsed() const { return Face != nullptr || Outline != nullptr || Overlay != nullptr; }
};

/** A text's paints, as its component resolved them for one paint (UDreamText). All slots unused: the text paints nothing, and every vertex is what it was before paints existed. */
struct FDreamTextPaints
{
	/** Indexed by slot (DreamTextQuadCode); [0] is never used. */
	FDreamTextPaintSlot Slots[DreamTextQuadCode::SlotCount];
	/**
	 * Per entry of the display list's PaintNames: the slot its items' faces paint with, FirstTagSlot to MaxSlot, or 0 when
	 * the name resolved to nothing or no tag slot was left -- such an item is then drawn as an item with no tag paint.
	 * Null, or shorter than PaintNames, reads 0. Valid for the paint only.
	 */
	const TArray<uint8>* NameSlots = nullptr;
	/**
	 * The text's material does not shade through MF_DreamUI_Shade (DreamUIShadeMaterial::ShadeMarkerParameter): no quad
	 * is given a slot, and the face's gradient is evaluated at each vertex instead (FDreamGradient::Evaluate at its UV4,
	 * with FaceAnimation) and multiplied into its colour. The outline and the overlay are not drawn in this case.
	 */
	bool bVertexColorFallback = false;
	/** What the text animates its paints by -- what its text table holds -- for the vertex-colour fallback. */
	FDreamPaintAnimation FaceAnimation;

	bool HasAny() const
	{
		for (const FDreamTextPaintSlot& Slot : Slots)
		{
			if (Slot.IsUsed())
			{
				return true;
			}
		}
		return false;
	}
};

/** What the painter needs beyond the display list: the font's quad conventions and the canvas's. */
struct FDreamTextPaintParams
{
	/** tan(italic angle): how far the top edge of an italic quad leans right. */
	float ItalicSlope = 0.0f;
	/** The canvas asks for normals and tangents (SDF fonts do, for the tilt-aware smoothing). */
	bool bRequireNormalAndTangent = false;
	/** Colour of every glyph that did not get one from a <color> tag. Carries the render opacity already. */
	FColor BaseColor = FColor::White;
	/**
	 * Render opacity for glyphs that did get a colour from a <color> tag: the tag's alpha is what the
	 * author wrote, this is the hierarchy's fade. Keeping it here rather than baking it into the parsed
	 * colour is what lets a fade repaint without re-laying out.
	 */
	float RichTextTagOpacity = 1.0f;
	/**
	 * Lyric-style fill. Glyphs inside a segment carry that segment's progress and glow boost and
	 * their position across it (UV2.y 0..1, UV3.x progress, UV3.y glow boost); glyphs outside any
	 * segment are one run per line with FillProgress / GlowBoost.
	 */
	const TArray<struct FDreamTextFillSegment>* FillSegments = nullptr;
	float FillProgress = 1.0f;
	float GlowBoost = 0.0f;

	/**
	 * A drop shadow and an outline for a font that is NOT a distance field. A bitmap atlas holds the
	 * face and nothing else, so there is no field to dilate and no way to re-rasterize with an outline
	 * -- what there is, and what UMG's own ShadowOffset does, is to draw the glyphs again, offset, in
	 * another colour. The outline is that eight times around the glyph, which is the standard stand-in
	 * for a real one. Offsets and widths are in em, like every other length in FDreamTextStyle, and are
	 * ignored entirely when bDistanceField is set (the shader draws the real thing there). Every shadow
	 * is drawn before any outline and every outline before any face, as Slate draws them in whole-run
	 * passes, so one glyph's outline never lands on its neighbour's face. The copies are in these colours
	 * at their own alpha times EffectOpacity, whatever the glyph's alpha is, and they paint nothing.
	 */
	FColor BitmapShadowColor = FColor(0, 0, 0, 0);
	FVector2f BitmapShadowOffsetEm = FVector2f::ZeroVector;
	FColor BitmapOutlineColor = FColor(0, 0, 0, 0);
	float BitmapOutlineWidthEm = 0.0f;

	/** Colours that replace a rich-text tag's glyph colour at paint time (a hovered or pressed link): pairs of an index into DisplayList.CustomTags and the colour. Applied without laying out again. */
	const TArray<TPair<int32, FColor>>* TagColorOverrides = nullptr;

	/**
	 * Draw each character's underline and strikethrough as a piece of its own, inside that character's vertex range,
	 * so whatever animates the characters one by one moves, fades and reveals the strokes with them; a piece reaches
	 * across what follows its glyph with no piece of its own, a space or an emoji. Otherwise a stroke is one strip per
	 * run of matching decoration, across the spaces between words, as a browser draws it.
	 */
	bool bStrokesPerCharacter = false;

	/**
	 * Distance-field fonts (either kind). UV2.x carries DilateEm + 16 * Layer per glyph (DreamTextQuadCode); quads grow
	 * into the field as far as the face / the effects reach; bold is a dilation of the regular glyph when BoldDilateEm is
	 * set. A colour glyph (FDreamUICharData::bColor) in any font is none of this: its quad is its padded cell as it is.
	 */
	bool bDistanceField = false;
	/**
	 * Draw the effects (underlay, glow, outline) of every glyph in a first set of quads and the faces
	 * in a second, so a glyph's glow never lands on top of its neighbour's face. Doubles the quads, so
	 * only for texts whose style has effects.
	 */
	bool bSeparateEffectLayer = false;
	/** Synthetic bold as a dilation of the face, in em per side; 0 when the atlas bakes bold. */
	float BoldDilateEm = 0.0f;
	/** How far beyond the glyph's edge the face and the effects reach, in em (see FDreamTextStyle). */
	float FaceReachEm = 0.0f;
	float EffectReachEm = 0.0f;
	/** Field geometry for sizing the quads: texels per em, the spread, what the quads already include, a texel in UV. */
	float EmTexels = 0.0f;
	float FieldSpreadTexels = 0.0f;
	float QuadMarginTexels = 0.0f;
	float TexelToUV = 0.0f;
	/**
	 * Distance-field fonts: the text style draws an underlay (its UnderlayColor has alpha). A colour glyph gets an effects
	 * copy (DreamTextQuadCode::ColorEffects) only then; its outline and glow are never drawn. Bitmap fonts say the same with
	 * BitmapShadowColor.
	 */
	bool bHasUnderlay = false;

	/** Small text from coverage glyphs. */
	FDreamTextCoverageParams Coverage;

	/**
	 * Gradients (FDreamTextPaints). Per item, the face's slot is: 0 for a colour glyph, for an item a tag colour override
	 * recolours (a hovered link), and for one whose style says bPaintRemoved; the name's slot for an item with a tag paint
	 * (FDreamTextItemStyle::PaintIndex) whose name resolved; else 0 for an item with a solid colour of its own (bHasColor);
	 * else slot 1 when it paints the face or the overlay. An item's effects copy is slot 1 when it paints the outline, else
	 * 0 -- and 0 for an item whose style says bPaintRemoved, which takes every paint off it, the outline's too. Underline
	 * and strikethrough strips take their item's slot -- the slot its face would have, a colour glyph's too (whose face
	 * itself stays slot 0), so a gradient's stroke runs on under an emoji -- and a strip is split where the slot changes.
	 */
	FDreamTextPaints Paints;
	/**
	 * What a face quad whose slot paints its face is coloured instead of the item's colour: white tinted by the content tint
	 * the text's ancestors lay over it, at the render opacity -- the gradient takes the place of the text's own Color, as
	 * `color: transparent` does. A custom style's pending Multiply still multiplies it, and TextAnimation's colour and
	 * alpha still reach it through the vertex colour.
	 */
	FColor PaintBaseColor = FColor::White;
	/**
	 * The alpha every effects copy (a field glyph's effects layer, a colour glyph's underlay copy) is written with, 0..1:
	 * the render opacity times the content tint's alpha, and never the text colour's alpha -- so a hollow text (a clear face
	 * with an outline) still draws its outline, and a faded text fades its outline, glow and underlay with its face. The
	 * shader multiplies the effects by it.
	 */
	float EffectOpacity = 1.0f;
};

/**
 * Turns a display list into quads. The only place in the plugin that knows what a glyph's vertices
 * look like: the glyph quad, the italic shear, the underline and strikethrough strips, which UV
 * channel carries what, which paint slot a quad takes and where it sits in its paint's boxes (UV4).
 * Runs from a cached display list, so it is cheap enough to run every time the geometry is rebuilt;
 * its working arrays are kept between paints on the game thread, so a burst of paints allocates
 * nothing once they have grown.
 */
class DREAMGUI_API FDreamTextPainter
{
public:
	/**
	 * Appends nothing: the geometry is sized to exactly what the display list emits. OutCharProperties
	 * lists the emitted glyphs in order, which is the contract TextAnimation and pixel snapping read; a
	 * character's vertex range covers every copy of its quad, its triangle range the face copy. An
	 * underline or strikethrough is one strip per run of identical decoration and paint, written after all
	 * the glyphs and belonging to no character -- a piece per character under a Glyph box -- or, with
	 * bStrokesPerCharacter, one piece per glyph inside its character's range. Every vertex is written,
	 * UV4 included, (0, 0) on a quad that paints nothing. Counts the paint, and the items drawn from
	 * coverage, in DreamUIRenderStats (TextPaints, CoverageItemsDrawn).
	 */
	static void Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
		FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties);
};
