// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamTextPaint.generated.h"

class UDreamGradientAsset;
class UDreamUIRichTextCustomStyleData;

/*
 * PAINTS: a gradient a text's glyphs are filled with instead of a solid colour -- CSS's `background-clip: text` with
 * `color: transparent` -- on the face, on the outline, and as an overlay mixed onto the face (a moving shimmer band).
 *
 * "Paint" and not "fill", because Fill already means the lyric fill (FDreamTextFillSegment).
 *
 * How it reaches the pixels:
 *  - the painter writes each glyph quad's place inside its gradient box into the vertex's UV4 (FDreamUIMeshVertex), and
 *    which of the text's paints it uses into its quad code (DreamTextQuadCode::SlotStride);
 *  - the gradients themselves live in the world's paint rows (UDreamUIManagerWorldSubsystem::AcquirePaintGradientRow),
 *    one row per distinct gradient, shared by every text that paints with it; each painted text has a row of its own too,
 *    its text table (DreamPaintRows), which says which gradient row each of its slots paints with and holds what the text
 *    animates (phases, angle offset, centre offset, scale);
 *  - DreamUIPaint.ush evaluates them per pixel, exactly as FDreamGradient::Evaluate does on the CPU.
 */

/** What kind of gradient a paint is. Values are what a gradient row holds (DREAMUI_PAINT_TYPE_* in DreamUIText.ush). */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPaintType : uint8
{
	/** Nothing: the layer keeps its solid colour. */
	None,
	/** Along a line through the box's centre at Angle: CSS linear-gradient. */
	Linear,
	/** Outward from Center to the ending shape Shape and Size give: CSS radial-gradient. */
	Radial,
	/** Around Center, clockwise from Angle: CSS conic-gradient. */
	Conic,
	/** Outward from Center in a diamond, |x| / Rx + |y| / Ry, the radii as a Radial's, turned by Angle. Not in CSS. */
	Diamond,
	/**
	 * The first four stops' colours at the box's corners -- top left, top right, bottom left, bottom right -- mixed across
	 * it: TextMeshPro's four-corner gradient. Positions, Spread, Scale, Offset and the phase do not apply.
	 */
	Corners,
};

/** What a gradient does beyond its first and last stop. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPaintSpread : uint8
{
	/** The end colours carry on: CSS's gradients. */
	Pad,
	/** The span from the first stop to the last starts again: CSS's repeating-*-gradient. */
	Repeat,
	/** The span runs back and forth. */
	Reflect,
};

/** The colour space stops are mixed in, alpha premultiplied in every one of them. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPaintInterpolation : uint8
{
	/** The sRGB-encoded values, CSS's default: `in srgb`. */
	SRGB,
	/** Linear light: `in srgb-linear`. */
	Linear,
	/** Perceptually even: `in oklab`. */
	Oklab,
};

/** A Radial's (and a Diamond's) ending shape: CSS's `ellipse` or `circle`. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPaintRadialShape : uint8
{
	Ellipse,
	Circle,
};

/** How big a Radial's (and a Diamond's) ending shape is: CSS's size keywords, or Radius. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamPaintRadialSize : uint8
{
	/** Through the corner of the box farthest from the centre: CSS's default. */
	FarthestCorner,
	FarthestSide,
	ClosestCorner,
	ClosestSide,
	/** FDreamGradient::Radius. */
	Explicit,
};

/**
 * What a text's gradient is measured across on one axis -- the box a vertex's UV4 runs from 0 to 1 over. Each axis has
 * its own (FDreamTextStyle::PaintBoxHorizontal, PaintBoxVertical), as TextMeshPro maps its textures per axis.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTextPaintBox : uint8
{
	/** The text as a block: the content box's width by the lines' height, first line's top to last line's bottom -- CSS's background box of a block. */
	TextBlock,
	/** The content box: the widget's rect less Margin. */
	ContentBox,
	/** Each line on its own: across its runs horizontally, its line box vertically. */
	Line,
	/** Each glyph on its own: its advance box horizontally, its face's ascent to descent vertically. Letters of one size share their bands. */
	Glyph,
	/**
	 * A rich-text tag's run: its pieces on every line laid end to end horizontally (CSS's `box-decoration-break: slice`),
	 * its ascent to descent vertically. What a tag paint (`<gradient=Name>`) is always measured across.
	 */
	Run,
};

/** How a text's overlay paint is mixed onto its face. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTextOverlayBlend : uint8
{
	/** Over the face by the overlay's alpha. */
	Normal,
	/** Added to the face, times the overlay's alpha: a highlight that never darkens. */
	Add,
};

/** One colour of a gradient, where it sits along it. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamGradientStop
{
	GENERATED_BODY()

	FDreamGradientStop() = default;
	FDreamGradientStop(float InPosition, const FColor& InColor) : Position(InPosition), Color(InColor) {}

	/**
	 * Where along the gradient, as a fraction of its length: 0 its start, 1 its end; beyond either too, as CSS lets a stop
	 * sit outside the box. Two stops at one place make a hard edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	float Position = 0.0f;
	/** sRGB-encoded with straight alpha, like every colour of a text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FColor Color = FColor::White;

	bool operator==(const FDreamGradientStop& Other) const { return Position == Other.Position && Color == Other.Color; }
	bool operator!=(const FDreamGradientStop& Other) const { return !(*this == Other); }
};

/**
 * What a text animates its paints by, on top of each gradient's own Angle, Center, Scale and Offset: what its text table
 * holds per slot (DreamPaintRows), and what FDreamGradient::Evaluate is told to reproduce the shader. Plain values.
 */
struct FDreamPaintAnimation
{
	/** Fractions of the gradient's length it is moved along its line: added to Offset. 1 moves it one whole gradient further. */
	float Phase = 0.0f;
	/** Degrees, added to Angle. */
	float AngleOffset = 0.0f;
	/** Fractions of the box, added to Center (and to a Linear's mid-point, the box's centre). */
	FVector2f CenterOffset = FVector2f::ZeroVector;
	/** Times Scale. */
	float ScaleMultiplier = 1.0f;
};

/**
 * A gradient, the way CSS spells one: what a paint paints with.
 *
 * THE REFERENCE EVALUATION. DreamUIPaint.ush and Evaluate compute the same thing, in float, from a vertex's UV4 = (u, v):
 * u 0 at the box's left edge and 1 at its right, v 0 at its TOP and 1 at its bottom (CSS's way up); A the box's width over
 * its height for a single box (TextBlock or ContentBox on both axes), 1 for every other box, whose angles are then measured
 * in the unit square (exact at 0, 90, 180 and 270 degrees); and the text's animation N (FDreamPaintAnimation). Lengths are
 * in units of the box's height: a point is p = ((u - cx) * A, v - cy) from a centre c.
 *  1. The gradient parameter t:
 *     - Linear: c = (0.5, 0.5) + N.CenterOffset. a = radians(Angle + N.AngleOffset), d = (sin a, -cos a),
 *       t = dot(p, d) / max(|A sin a| + |cos a|, 1e-6) + 0.5: CSS's gradient line, magic corners included.
 *     - Radial: c = Center + N.CenterOffset. The side distances sx = |cx| * A or |1 - cx| * A and sy = |cy| or |1 - cy|
 *       (distances, so a centre outside the box measures to its sides as CSS does), the smaller of each pair for Closest*,
 *       the larger for Farthest*. Ellipse: radii (sx, sy) for *Side and sqrt(2) * (sx, sy) for *Corner (CSS: the corner's
 *       ellipse keeps the side's aspect); Explicit (Radius.X * A, Radius.Y). Circle: the smaller (Closest*) or larger
 *       (Farthest*) of sx and sy for *Side, sqrt(sx^2 + sy^2) for *Corner, Radius.X * A for Explicit. t = length(p / R),
 *       each radius at least 1e-6.
 *     - Conic: c as Radial. t = frac((atan2(p.x, -p.y) in degrees - (Angle + N.AngleOffset)) / 360): 0 up, clockwise.
 *     - Diamond: c and the radii as Radial (Ellipse radii for both shapes); p turned by -(Angle + N.AngleOffset) degrees,
 *       with a = radians(Angle + N.AngleOffset) that is q = (p.x cos a + p.y sin a, -p.x sin a + p.y cos a) (v runs down,
 *       so a positive Angle turns the diamond clockwise); t = |q.x| / Rx + |q.y| / Ry.
 *     - Corners: no t; the colour is the first four stops mixed bilinearly at (saturate(u), saturate(v)), premultiplied in
 *       the Interpolation space, the last stop standing in for any of the four that is missing.
 *  frac(x) is x - floor(x) throughout, negative x included, as HLSL's frac is.
 *  2. Scale and move: S = Scale * N.ScaleMultiplier (at least 1e-4), o = Offset + N.Phase. Linear:
 *     t = (t - 0.5) / S + 0.5 - o. Radial, Conic, Diamond: t = t / S - o.
 *  3. Spread, over the span from the first stop's position a to the last's b (as Pad when b - a <= 0): Pad leaves t;
 *     Repeat t = a + frac((t - a) / (b - a)) * (b - a); Reflect s = (t - a) / (b - a), s = 1 - |frac(s / 2) * 2 - 1|,
 *     t = a + s * (b - a).
 *  4. Stops: t at or before the first position takes the first colour, at or after the last the last; otherwise the
 *     segment [p_i, p_i+1) holding t, mixed by (t - p_i) / max(p_i+1 - p_i, 1e-9). A hard edge (p_i == p_i+1) belongs
 *     to the later segment. Colours are mixed premultiplied, in the Interpolation space.
 *  5. Out: un-premultiplied (0 where alpha is 0), turned into linear light, and clamped at 0: straight linear RGB, alpha.
 * Stops as packed: positions in order, each at least the one before (CSS's fix-up); the first MaxStops only; colours
 * premultiplied and already in the Interpolation space (SRGB: the encoded values; Linear: decoded; Oklab: decoded, then
 * Oklab), so the shader mixes without converting.
 * As packed, too: an enum value out of its range reads as its first value (a Type out of range as None), a number that is
 * not finite as the field's default, a stop position that is not finite as the one before it (the first: 0). The sRGB
 * transfer is the exact one (DreamUIText_DecodeSrgb); Oklab is Ottosson's, through linear sRGB.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamGradient
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamPaintType Type = EDreamPaintType::Linear;
	/** The colours. The first MaxStops are used; positions out of order are taken as CSS takes them, each at least the one before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	TArray<FDreamGradientStop> Stops;
	/** Degrees, as CSS measures them: 0 points up, clockwise, 180 -- the default -- down. A Linear's direction, a Conic's start, a Diamond's turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (UIMin = "0", UIMax = "360"))
	float Angle = 180.0f;
	/** A Radial's, Conic's or Diamond's centre, as fractions of the box from its top left: CSS's `at x% y%`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FVector2f Center = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamPaintRadialShape Shape = EDreamPaintRadialShape::Ellipse;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamPaintRadialSize Size = EDreamPaintRadialSize::FarthestCorner;
	/** Size Explicit: the radii as fractions of the box's width and height; a circle's is X, of the width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FVector2f Radius = FVector2f(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamPaintSpread Spread = EDreamPaintSpread::Pad;
	/** How long the gradient is as a multiple of its natural length: about its middle for a Linear, about its start for the others. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0.0001", UIMin = "0.1", UIMax = "4"))
	float Scale = 1.0f;
	/** How far it is moved along its line, as a fraction of its length; a text's phase adds to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (UIMin = "-1", UIMax = "1"))
	float Offset = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	EDreamPaintInterpolation Interpolation = EDreamPaintInterpolation::SRGB;

	/** Stops a gradient row holds; the rest are not drawn. */
	static constexpr int32 MaxStops = 16;

	/** Whether it paints at all: a type other than None, and at least one stop. */
	bool IsPainting() const;

	/**
	 * The colour at InBoxUV (a vertex's UV4, see the struct), the C++ twin of DreamUIPaint.ush: straight alpha, RGB in
	 * linear light. InAspect is the box's width over its height for a single box, 1 otherwise. What the editor's preview
	 * strip, the vertex-colour fallback and the tests that hold the shader to it call. A gradient that does not paint
	 * (IsPainting) answers opaque white, what a colour is multiplied by to stay as it is -- the shader's "draws solid".
	 */
	FLinearColor Evaluate(const FVector2f& InBoxUV, float InAspect = 1.0f, const FDreamPaintAnimation& InAnimation = FDreamPaintAnimation()) const;

	/**
	 * Read a CSS gradient: linear-gradient, radial-gradient, conic-gradient and their repeating- forms, with angles in
	 * deg, turn, rad or grad, `to <side or corner>`, `at <position>` in percentages and keywords, the size and shape
	 * keywords, `from <angle>`, `in srgb | srgb-linear | oklab`, stops with percentage positions (one or two each), and
	 * the colours a rich text's <color> takes (names, #hex, rgb(), rgba(), transparent). Beyond CSS, what ToCss writes for
	 * what CSS cannot say: diamond-gradient and corners-gradient, reflecting- for Reflect, and the Scale and Offset. False,
	 * with OutGradient untouched and OutError saying where, for anything else.
	 *
	 * THE GRAMMAR, as read and as ToCss writes it. Keywords, function names and hex digits in any case; spaces where CSS
	 * allows them.
	 *   gradient := 'none' | [ 'repeating-' | 'reflecting-' ] kind '(' [ prelude ] [ ',' stop ]* ')'
	 *               (with no prelude the first stop follows the bracket)
	 *   kind     := 'linear-gradient' | 'radial-gradient' | 'conic-gradient' | 'diamond-gradient' | 'corners-gradient'
	 *               | 'none-gradient'
	 *   prelude  := items separated by spaces, in any order, each at most once. Every item is read in every kind, CSS's
	 *               own kinds included, so that a field the kind does not use still comes back (ToCss writes it then):
	 *     <angle> | 'to' side [ side ] | 'from' <angle>  Angle. deg, grad, rad or turn, or a bare 0. 'to top' is 0deg,
	 *                                                    'to right' 90deg; a corner, 'to top right', is 45deg (and 135,
	 *                                                    225, 315): the square's -- CSS turns it to the corner of the
	 *                                                    actual box, which a fixed Angle cannot.
	 *     'at' <position>                                Center: one, two or four of left, center, right, top, bottom and
	 *                                                    percentages, as CSS's <position>.
	 *     'circle' | 'ellipse'                           Shape.
	 *     'closest-side' | 'closest-corner' | 'farthest-side' | 'farthest-corner'   Size.
	 *     <percentage> [ <percentage> ]                  Size Explicit with Radius, of the width then of the height. A
	 *                                                    circle's one value is of the width (CSS has only lengths there).
	 *     'radius' <percentage> [ <percentage> ]         Radius alone, Size left as it is.
	 *     'in' ( 'srgb' | 'srgb-linear' | 'oklab' )      Interpolation.
	 *     'scale' <number>                               Scale.
	 *     'offset' ( <percentage> | <number> )           Offset, of the gradient's length (a number is a fraction).
	 *   stop     := <color> [ <percentage> [ <percentage> ] ]   two positions are two stops of the colour, CSS's hard band
	 *   <color>  := #rgb | #rgba | #rrggbb | #rrggbbaa | rgb() | rgba() -- commas or spaces and '/', numbers 0..255 or
	 *               percentages, alpha 0..1 or a percentage -- | 'transparent' | a CSS basic name, or orange, pink, brown,
	 *               gold, grey, cyan, magenta. CSS's values: green is #008000 (a rich text's <color=green> is #00FF00).
	 * The prefix sets Spread; reflecting- is DreamGUI's. Defaults are the struct's, but a conic gradient's Angle starts at 0
	 * (CSS's), every other kind's at 180. Stops with no position are placed as CSS places them -- the first at 0%, the last
	 * at 100%, those between spread evenly -- and written positions are kept as written, out of order too (Evaluate and
	 * PackRow fix them up as CSS does). Not read: lengths (px, em: no box is known here), midpoint hints, other colour
	 * functions, other colour spaces and hue methods.
	 */
	static bool ParseCss(const FString& InCss, FDreamGradient& OutGradient, FString* OutError = nullptr);
	/**
	 * This gradient as ParseCss reads it: plain CSS whenever CSS can say it. ParseCss(ToCss()) gives it back field for
	 * field, every finite gradient (a field that is not finite has no spelling). Positions CSS would place by itself are
	 * left out, colours are #RRGGBB or #RRGGBBAA, numbers the shortest that read back the same.
	 */
	FString ToCss() const;

	/**
	 * The gradient row it is drawn from (DreamPaintRows): exactly DreamPaintRows::RowWidth pixels of four floats, the
	 * layout DreamPaintRows names. What UDreamUIManagerWorldSubsystem::AcquirePaintGradientRow writes.
	 */
	void PackRow(TArray<FVector4f>& OutPixels) const;
	/** A hash of PackRow's pixels: gradients that draw the same share one row. */
	uint64 GetRowHash() const;

	bool operator==(const FDreamGradient& Other) const;
	bool operator!=(const FDreamGradient& Other) const { return !(*this == Other); }
};

/** One layer of a text's paint: on or off, and what it paints with. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamTextPaint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	bool bEnabled = false;
	/** A shared gradient to paint with in place of Gradient; null paints Gradient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "bEnabled"))
	TObjectPtr<UDreamGradientAsset> Preset = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "bEnabled"))
	FDreamGradient Gradient;

	/** What it paints with: the preset's gradient when it has one, else Gradient. Whether it is enabled is not asked. */
	const FDreamGradient& GetEffectiveGradient() const;
	/** Enabled, and its effective gradient paints. */
	bool IsPainting() const;

	/**
	 * The gradient a tag paint's name stands for (FDreamTextItemStyle::PaintIndex, `<gradient=Name>`): InStyles' entry of
	 * that name when its paintType is Set -- its paint's effective gradient, whatever its bEnabled says -- else the project
	 * preset of that name (UDreamGUISettings::GradientPresets, read as CSS), else the name itself read as CSS. False when
	 * none of them gives a gradient that paints. Game thread; what a text asks when it paints a changed display list, each
	 * name once.
	 */
	static bool ResolveTagPaint(FName InName, const UDreamUIRichTextCustomStyleData* InStyles, FDreamGradient& OutGradient);

	bool operator==(const FDreamTextPaint& Other) const;
	bool operator!=(const FDreamTextPaint& Other) const { return !(*this == Other); }
};

/**
 * The project's gradient presets (UDreamGUISettings::GradientPresets) and tag names read as CSS, each string parsed once:
 * what FDreamTextPaint::ResolveTagPaint asks after a custom style's entry. A lookup reads the settings' map as it is now
 * and keeps parses by the CSS string itself, so a preset added or edited -- in the project settings, by the editor's
 * "save as preset", by a config reload -- is found as it is at its very next lookup. Defined with the settings
 * (DreamGUISettings.cpp). Game thread: the parse cache is locked, but the settings' map is read as it stands.
 */
namespace DreamGradientPresets
{
	/** The project preset InName, read as CSS. False when there is no preset of that name, or its CSS does not read. */
	DREAMGUI_API bool FindPreset(FName InName, FDreamGradient& OutGradient);
	/** FDreamGradient::ParseCss of InCss, through the same cache. OutGradient is untouched when false. */
	DREAMGUI_API bool ParseCssCached(const FString& InCss, FDreamGradient& OutGradient);
	/**
	 * Broadcast on the game thread when the presets were edited in the editor (UDeveloperSettings::OnSettingChanged for
	 * GradientPresets, which the project settings and the gradient picker's "save as preset" both raise), once anything has
	 * looked a preset up: a text that resolved a tag name to a preset resolves it again to show the edit. A config reload
	 * at run time raises nothing; the next lookup finds the new string all the same.
	 */
	DREAMGUI_API FSimpleMulticastDelegate& OnPresetsChanged();
}

/**
 * The layout of the world's paint rows (UDreamUIManagerWorldSubsystem::GetPaintRowsTexture): one RGBA32F texture, rows of
 * RowWidth pixels, every pixel four plain float values -- no bit patterns, so no flush-to-zero can touch them. The shader
 * side is the DREAMUI_PAINT_* block of DreamUIText.ush: change one, change both.
 *
 * A TEXT TABLE row is a painted text's own. For each slot s, 1 to DreamTextQuadCode::MaxSlot (1 the text's own paints,
 * 2 and up its rich-text tag paints):
 *   pixel TextSlotRowsPixel + s:       (face gradient row, outline gradient row, overlay gradient row, overlay blend)
 *   pixel TextSlotAnimationPixel + s:  (face phase, outline phase, overlay phase, box aspect A)
 *   pixel TextSlotTransformPixel + s:  (angle offset in degrees, centre offset x, centre offset y, scale multiplier)
 * A row number NoRow (-1) is a layer the slot does not paint; the overlay blend is an EDreamTextOverlayBlend's value. Pixel
 * TextHeaderPixel holds (the highest slot in use, 0, 0, 0); the rest of the row is 0. A slot the text does not use reads
 * NoRow throughout. A text's widget record links to its table through bits 0..15 of its marks pixel: the row + 1, 0 for none
 * (RecordRowLinkMask).
 *
 * A GRADIENT row is one FDreamGradient (FDreamGradient::PackRow):
 *   pixel GradientHeaderPixel:   (type, spread, interpolation, stop count)   -- the enums' values
 *   pixel GradientShapePixel:    (angle in degrees, scale, offset, radial shape)
 *   pixel GradientCenterPixel:   (centre x, centre y, radius x, radius y)
 *   pixel GradientSizePixel:     (radial size, 0, 0, 0)
 *   pixels GradientPositionsPixel to +3: the stops' positions, four to a pixel (stop i: pixel + i / 4, component i % 4)
 *   pixels GradientColorsPixel to +15: the stops' colours (stop i: pixel + i), premultiplied, in the interpolation space
 *   the rest: 0.
 */
namespace DreamPaintRows
{
	constexpr int32 RowWidth = 32;
	constexpr int32 BytesPerPixel = 16;
	constexpr int32 RowBytes = RowWidth * BytesPerPixel;
	/** What a slot's row number holds for a layer it does not paint. */
	constexpr float NoRow = -1.0f;

	constexpr int32 TextHeaderPixel = 0;
	constexpr int32 TextSlotRowsPixel = 0;
	constexpr int32 TextSlotAnimationPixel = 9;
	constexpr int32 TextSlotTransformPixel = 18;

	constexpr int32 GradientHeaderPixel = 0;
	constexpr int32 GradientShapePixel = 1;
	constexpr int32 GradientCenterPixel = 2;
	constexpr int32 GradientSizePixel = 3;
	constexpr int32 GradientPositionsPixel = 4;
	constexpr int32 GradientColorsPixel = 8;

	/** Bits of a widget record's marks pixel that hold its text table's row + 1. */
	constexpr uint32 RecordRowLinkMask = 0xFFFFu;
	/** Text tables a world can link from its records: the link is 16 bits, 0 being none. */
	constexpr int32 MaxTextTableRows = 0xFFFF;

	/** The text-table pixels of slot InSlot (1 to DreamTextQuadCode::MaxSlot). */
	constexpr int32 GetSlotRowsPixel(int32 InSlot) { return TextSlotRowsPixel + InSlot; }
	constexpr int32 GetSlotAnimationPixel(int32 InSlot) { return TextSlotAnimationPixel + InSlot; }
	constexpr int32 GetSlotTransformPixel(int32 InSlot) { return TextSlotTransformPixel + InSlot; }
}

/** Names a material that shades through MF_DreamUI_Shade (DShader/MF_DreamUI_Shade.dsf) carries, as the canvas and the text read them. */
namespace DreamUIShadeMaterial
{
	/** Texture parameter: the world's paint rows, which every material section of a canvas is given. */
	inline const FName PaintDataTextureParameter(TEXT("DreamUI_PaintDataTexture"));
	/**
	 * Scalar parameter, 1 by default, that only MF_DreamUI_Shade declares: a material whose
	 * GetScalarParameterDefaultValue finds it above 0.5 shades through DreamUIShade.ush, which alone knows coverage glyphs,
	 * colour glyphs and paints. Read the same way in a cooked game.
	 */
	inline const FName ShadeMarkerParameter(TEXT("DreamUI_ShadeMarker"));
}
