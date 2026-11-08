// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDescriptor.h"
#include "Psd/DreamPsdEngineData.h"

/**
 * A layered image document as the reader leaves it: the header, the resources that matter, every layer record with
 * its pixels decoded, the blocks the importer maps typed, and the merged image. Nothing here is a UObject.
 *
 * Coordinates are document pixels, x right and y down, as the file writes them. Colours are encoded sRGB 0..1. Pixels
 * are 8 bits per channel whatever the document's depth: a 16- or 32-bit document is reduced as it is read (UI textures
 * are 8-bit, and the merged image a test compares with is too).
 */

enum class EDreamPsdColorMode : uint16
{
	Bitmap = 0,
	Grayscale = 1,
	Indexed = 2,
	RGB = 3,
	CMYK = 4,
	Multichannel = 7,
	Duotone = 8,
	Lab = 9,
};

/** One thing the reader or the importer has to say about a document. */
struct DREAMGUIPSD_API FDreamPsdMessage
{
	enum class ESeverity : uint8 { Info, Warning, Error };
	ESeverity Severity = ESeverity::Warning;
	FString Text;
	/** The layer it is about (its name), or empty for the document. */
	FString Layer;

	FString ToString() const;
};

struct DREAMGUIPSD_API FDreamPsdMessages
{
	TArray<FDreamPsdMessage> Items;

	void Info(const FString& InText, const FString& InLayer = FString());
	void Warn(const FString& InText, const FString& InLayer = FString());
	void Error(const FString& InText, const FString& InLayer = FString());
	bool HasErrors() const;
	int32 Count(FDreamPsdMessage::ESeverity InSeverity) const;
	FString ToString() const;
};

/** An 8-bit image: Width x Height, rows top down. */
struct DREAMGUIPSD_API FDreamPsdImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;

	bool IsEmpty() const { return Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height; }
	FColor At(int32 X, int32 Y) const { return Pixels[Y * Width + X]; }
	void Init(int32 InWidth, int32 InHeight, FColor InFill);
};

/** A one-channel 8-bit image (a mask). */
struct DREAMGUIPSD_API FDreamPsdGray
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint8> Pixels;

	bool IsEmpty() const { return Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height; }
	uint8 At(int32 X, int32 Y) const { return Pixels[Y * Width + X]; }
	void Init(int32 InWidth, int32 InHeight, uint8 InFill);
};

/** A raster layer mask, the user mask: `Rect` in document pixels, `DefaultColor` outside it. */
struct DREAMGUIPSD_API FDreamPsdLayerMask
{
	bool bPresent = false;
	FIntRect Rect;
	/** 0 or 255: the mask's value outside Rect. */
	uint8 DefaultColor = 0;
	bool bDisabled = false;
	bool bPositionRelative = false;
	/** "Invert layer mask when blending" (obsolete, still honoured). */
	bool bInvertOnBlend = false;
	/** The mask was rendered from other data -- a vector mask with feather or density -- rather than painted. */
	bool bFromRenderedData = false;
	/** 0..1; 1 is the painted mask as it is. The effective value is 1 - Density * (1 - mask). */
	float Density = 1.0f;
	/** Pixels of blur applied to the mask when it is drawn. */
	float Feather = 0.0f;
	/** Rect-sized, top down. */
	FDreamPsdGray Pixels;
};

/** One knot of a path: the control point before it, the anchor, the control point after it, in document pixels. */
struct DREAMGUIPSD_API FDreamPsdPathKnot
{
	FVector2D In = FVector2D::ZeroVector;
	FVector2D Anchor = FVector2D::ZeroVector;
	FVector2D Out = FVector2D::ZeroVector;
	bool bLinked = true;
};

/** How a subpath combines with what the subpaths before it made. */
enum class EDreamPsdPathOperation : int16
{
	/** Files older than the shape operations: the subpaths are filled together, even-odd. */
	Legacy = -1,
	Exclude = 0,
	Combine = 1,
	Subtract = 2,
	Intersect = 3,
};

struct DREAMGUIPSD_API FDreamPsdSubpath
{
	bool bClosed = true;
	EDreamPsdPathOperation Operation = EDreamPsdPathOperation::Combine;
	TArray<FDreamPsdPathKnot> Knots;
};

struct DREAMGUIPSD_API FDreamPsdPath
{
	TArray<FDreamPsdSubpath> Subpaths;
	/** The fill starts with every pixel inside (the initial fill rule record). */
	bool bStartsFilled = false;

	bool IsEmpty() const { return Subpaths.Num() == 0; }
	/** The bounds of every anchor and control point. */
	FBox2D GetBounds() const;
};

/** A vector mask (or a shape layer's outline): 'vmsk' / 'vsms'. */
struct DREAMGUIPSD_API FDreamPsdVectorMask
{
	bool bPresent = false;
	bool bInvert = false;
	bool bNotLinked = false;
	bool bDisabled = false;
	float Density = 1.0f;
	float Feather = 0.0f;
	FDreamPsdPath Path;
};

/** What drew a live shape ('vogk'), when the tool left a description behind. */
enum class EDreamPsdLiveShapeKind : uint8
{
	None,
	Rectangle,
	RoundedRectangle,
	Line,
	Ellipse,
	Other,
};

struct DREAMGUIPSD_API FDreamPsdLiveShape
{
	EDreamPsdLiveShapeKind Kind = EDreamPsdLiveShapeKind::None;
	/** Document pixels. */
	FBox2D Box = FBox2D(ForceInit);
	/** Corner radii in pixels: top left, top right, bottom right, bottom left. */
	FVector4 Radii = FVector4(0, 0, 0, 0);
	/** The shape has been rotated or skewed since it was drawn (its transform is not a translation). */
	bool bTransformed = false;
};

/** A shape layer's stroke ('vstk'). */
struct DREAMGUIPSD_API FDreamPsdStroke
{
	bool bPresent = false;
	bool bEnabled = false;
	bool bFillEnabled = true;
	float Width = 0.0f;
	/** 0 inside, 1 centre, 2 outside. */
	int32 Alignment = 0;
	FLinearColor Color = FLinearColor::Black;
	float Opacity = 1.0f;
	FString BlendMode;
};

struct DREAMGUIPSD_API FDreamPsdColorStop
{
	/** 0..1 along the gradient. */
	float Location = 0.0f;
	/** 0..1 between this stop and the next: where the blend of the two is half way. */
	float Midpoint = 0.5f;
	FLinearColor Color = FLinearColor::Black;
};

struct DREAMGUIPSD_API FDreamPsdAlphaStop
{
	float Location = 0.0f;
	float Midpoint = 0.5f;
	float Opacity = 1.0f;
};

/** A gradient definition, from a descriptor ('Grdn') or from a gradient map's binary record. */
struct DREAMGUIPSD_API FDreamPsdGradient
{
	FString Name;
	/** A noise gradient (no stops); evaluated as its first and last colours. */
	bool bNoise = false;
	TArray<FDreamPsdColorStop> ColorStops;
	TArray<FDreamPsdAlphaStop> AlphaStops;
	/** Smoothness, 0..1 (4096 in the file). */
	float Smoothness = 1.0f;

	/** The colour (alpha included) at T in 0..1. */
	FLinearColor Evaluate(float T) const;
};

/** How a gradient fill or overlay lays its gradient out. */
struct DREAMGUIPSD_API FDreamPsdGradientFill
{
	FDreamPsdGradient Gradient;
	/** 'Lnr ', 'Rdl ', 'Angl', 'Rflc', 'Dmnd'. */
	FString Style = TEXT("Lnr ");
	/** Degrees, counter-clockwise from the right. */
	float Angle = 90.0f;
	/** 1 is 100 %. */
	float Scale = 1.0f;
	bool bReverse = false;
	bool bDither = false;
	bool bAlignWithLayer = true;
	/** Offset of the centre, in percent of the layer's size. */
	FVector2D Offset = FVector2D::ZeroVector;
};

struct DREAMGUIPSD_API FDreamPsdPatternFill
{
	FString PatternId;
	FString PatternName;
	float Scale = 1.0f;
	bool bAlignWithLayer = true;
	FVector2D Phase = FVector2D::ZeroVector;
};

/** A fill layer's content ('SoCo', 'GdFl', 'PtFl', or a shape's 'vscg'). */
struct DREAMGUIPSD_API FDreamPsdFill
{
	enum class EKind : uint8 { None, Solid, Gradient, Pattern };
	EKind Kind = EKind::None;
	FLinearColor Color = FLinearColor::Black;
	FDreamPsdGradientFill Gradient;
	FDreamPsdPatternFill Pattern;
	TSharedPtr<FDreamPsdDescriptor> Descriptor;
};

struct DREAMGUIPSD_API FDreamPsdTextStyleRun
{
	int32 Start = 0;
	int32 Length = 0;
	/** The face's PostScript name, as the font set names it. */
	FString FontName;
	/** In the text's own units; multiply by the transform's scale for pixels. */
	float FontSize = 12.0f;
	FLinearColor Color = FLinearColor::Black;
	bool bFauxBold = false;
	bool bFauxItalic = false;
	bool bUnderline = false;
	bool bStrikethrough = false;
	bool bAutoLeading = true;
	float Leading = 0.0f;
	/** Thousandths of an em. */
	float Tracking = 0.0f;
	float BaselineShift = 0.0f;
	/** 0 normal, 1 small caps, 2 all caps. */
	int32 FontCaps = 0;
	float HorizontalScale = 1.0f;
	float VerticalScale = 1.0f;
};

struct DREAMGUIPSD_API FDreamPsdTextParagraphRun
{
	int32 Start = 0;
	int32 Length = 0;
	/** 0 left, 1 right, 2 centre, 3 justify last left, 4 justify last right, 5 justify last centre, 6 justify all. */
	int32 Justification = 0;
};

/** Text warp ('warp' of a type layer). */
struct DREAMGUIPSD_API FDreamPsdTextWarp
{
	/** 'warpNone', 'warpArc', 'warpArcLower', ... */
	FString Style = TEXT("warpNone");
	/** Bend, percent, -100..100. */
	float Value = 0.0f;
	/** Horizontal distortion, percent. */
	float Perspective = 0.0f;
	/** Vertical distortion, percent. */
	float PerspectiveOther = 0.0f;
	/** 'Hrzn' or 'Vrtc'. */
	FString Rotate = TEXT("Hrzn");

	bool IsNone() const { return Style.IsEmpty() || Style == TEXT("warpNone"); }
};

/** A type layer ('TySh'). */
struct DREAMGUIPSD_API FDreamPsdText
{
	/** The text, paragraphs separated by '\r' (the engine's own trailing '\r' removed). */
	FString Text;
	/** xx, xy, yx, yy, tx, ty: text space to document pixels. */
	double Transform[6] = { 1, 0, 0, 1, 0, 0 };
	/** Box (paragraph) text, as opposed to point text. */
	bool bBoxText = false;
	/** Box text: the box, in text space (before Transform). */
	FBox2D BoxBounds = FBox2D(ForceInit);
	/** The text's bounds from the descriptor, in text space. */
	FBox2D Bounds = FBox2D(ForceInit);
	/** Vertical text. */
	bool bVertical = false;
	/** 'Anno' none, 'antiAliasSharp', 'AnCr' crisp, 'AnSt' strong, 'AnSm' smooth... */
	FString AntiAlias;
	TArray<FDreamPsdTextStyleRun> StyleRuns;
	TArray<FDreamPsdTextParagraphRun> ParagraphRuns;
	FDreamPsdTextWarp Warp;
	TSharedPtr<FDreamPsdDescriptor> TextDescriptor;
	TSharedPtr<FDreamPsdDescriptor> WarpDescriptor;
	TSharedPtr<FDreamPsdEngineValue> EngineData;

	/** The scale the transform applies (its y axis length), which turns a font size into pixels. */
	double GetScale() const;
	/** The transform is a scale and a translation, nothing turned or skewed. */
	bool IsAxisAligned() const;
};

/** Which adjustment a layer is. */
enum class EDreamPsdAdjustmentKind : uint8
{
	None,
	BrightnessContrast,
	Levels,
	Curves,
	Exposure,
	Vibrance,
	HueSaturation,
	ColorBalance,
	BlackWhite,
	PhotoFilter,
	ChannelMixer,
	Invert,
	Posterize,
	Threshold,
	GradientMap,
	SelectiveColor,
	ColorLookup,
};

/**
 * An adjustment layer's settings, every kind in one struct (each kind reads its own fields). Numbers are kept in the
 * units the file writes, which are the units the editor shows: levels and curves in 0..255, hue in degrees,
 * saturation and lightness in -100..100, colour balance in -100..100, percentages as percent.
 */
struct DREAMGUIPSD_API FDreamPsdAdjustment
{
	EDreamPsdAdjustmentKind Kind = EDreamPsdAdjustmentKind::None;
	/** The block's key: 'levl', 'hue2', 'CgEd'... */
	FString Key;

	// BrightnessContrast ('brit' or 'CgEd')
	float Brightness = 0.0f;
	float Contrast = 0.0f;
	bool bLegacyBrightness = false;

	// Levels: [0] composite, then the channels in order.
	struct FLevels
	{
		int32 InputBlack = 0;
		int32 InputWhite = 255;
		int32 OutputBlack = 0;
		int32 OutputWhite = 255;
		float Gamma = 1.0f;
	};
	TArray<FLevels> Levels;

	// Curves: Channel 0 is the composite, 1.. the channels. Points are (input, output) in 0..255.
	struct FCurve
	{
		int32 Channel = 0;
		TArray<FVector2f> Points;
	};
	TArray<FCurve> Curves;

	// Exposure
	float Exposure = 0.0f;
	float Offset = 0.0f;
	float Gamma = 1.0f;

	// Vibrance
	float Vibrance = 0.0f;
	float Saturation = 0.0f;

	// HueSaturation
	bool bColorize = false;
	FIntVector ColorizeHsl = FIntVector::ZeroValue;
	FIntVector MasterHsl = FIntVector::ZeroValue;
	struct FHueRange
	{
		/** Begin of the ramp, begin, end, end of the ramp, in degrees. */
		int32 Range[4] = { 0, 0, 0, 0 };
		FIntVector Hsl = FIntVector::ZeroValue;
	};
	TArray<FHueRange> HueRanges;

	// ColorBalance: (cyan-red, magenta-green, yellow-blue) per range.
	FIntVector Shadows = FIntVector::ZeroValue;
	FIntVector Midtones = FIntVector::ZeroValue;
	FIntVector Highlights = FIntVector::ZeroValue;
	bool bPreserveLuminosity = true;

	// BlackWhite: reds, yellows, greens, cyans, blues, magentas, percent.
	float BlackWhiteWeights[6] = { 40, 60, 40, 60, 20, 80 };
	bool bTint = false;
	FLinearColor TintColor = FLinearColor::White;

	// PhotoFilter
	FLinearColor FilterColor = FLinearColor::White;
	float Density = 25.0f;

	// ChannelMixer: four output rows (red, green, blue, gray) of red, green, blue, constant, percent.
	bool bMonochrome = false;
	float Mixer[4][4] = { { 100, 0, 0, 0 }, { 0, 100, 0, 0 }, { 0, 0, 100, 0 }, { 0, 0, 0, 0 } };

	// Posterize and Threshold
	int32 Level = 0;

	// GradientMap
	FDreamPsdGradient Gradient;
	bool bReverse = false;
	bool bDither = false;

	/** The descriptor the block was written as, when it was one. */
	TSharedPtr<FDreamPsdDescriptor> Descriptor;
	/** The block's bytes, for a kind read only in part. */
	TArray<uint8> Raw;
};

/** Which layer effect. */
enum class EDreamPsdEffectKind : uint8
{
	DropShadow,
	InnerShadow,
	OuterGlow,
	InnerGlow,
	BevelEmboss,
	Satin,
	ColorOverlay,
	GradientOverlay,
	PatternOverlay,
	Stroke,
};

/** One layer effect, every kind in one struct. Pixels for sizes and distances, degrees for angles, 0..1 for opacity and percentages. */
struct DREAMGUIPSD_API FDreamPsdEffect
{
	EDreamPsdEffectKind Kind = EDreamPsdEffectKind::DropShadow;
	bool bEnabled = true;
	/** The descriptor's blend mode id ('Nrml', 'Mltp', 'linearDodge'...). */
	FString BlendMode = TEXT("Nrml");
	float Opacity = 1.0f;
	FLinearColor Color = FLinearColor::Black;
	/** The light's angle, degrees, counter-clockwise from the right; the global angle already applied when bUseGlobalLight. */
	float Angle = 120.0f;
	bool bUseGlobalLight = true;
	float Distance = 0.0f;
	/** Spread (shadows, glows) or choke (inner shadow, inner glow), 0..1. */
	float Spread = 0.0f;
	/** Blur size. */
	float Size = 0.0f;
	float Noise = 0.0f;
	bool bAntiAlias = false;
	FString ContourName;
	/** The contour curve as (input, output) points in 0..255; empty for linear. */
	TArray<FVector2f> Contour;
	/** Glows: range and jitter, 0..1. */
	float Range = 0.5f;
	float Jitter = 0.0f;
	bool bLayerKnocksOutShadow = true;
	/** Glows: 'SfBL' softer or 'PrBL' precise. Bevel: 'SfBL' smooth, 'PrBL' chisel hard, 'Slmt' chisel soft. */
	FString Technique;
	/** Inner glow: 'SrcC' centre or 'SrcE' edge. */
	FString GlowSource;
	/** Glows: a gradient instead of a colour. */
	bool bUseGradient = false;

	// BevelEmboss
	/** 'OtrB' outer, 'InrB' inner, 'Embs' emboss, 'PlEb' pillow, 'strokeEmboss'. */
	FString BevelStyle = TEXT("InrB");
	/** 0..10 (the file's 1..1000 percent / 100). */
	float Depth = 1.0f;
	/** True for up. */
	bool bDirectionUp = true;
	float Soften = 0.0f;
	float Altitude = 30.0f;
	FString HighlightBlendMode = TEXT("Scrn");
	FLinearColor HighlightColor = FLinearColor::White;
	float HighlightOpacity = 0.75f;
	FString ShadowBlendMode = TEXT("Mltp");
	FLinearColor ShadowColor = FLinearColor::Black;
	float ShadowOpacity = 0.75f;
	bool bUseShape = false;
	bool bUseTexture = false;

	// Satin
	bool bInvert = false;

	// Overlays and the stroke's fill
	/** Stroke: 'SClr', 'GrFl' or 'Ptrn'. */
	FString FillType = TEXT("SClr");
	FDreamPsdGradientFill Gradient;
	FDreamPsdPatternFill Pattern;

	// Stroke
	/** 'OutF', 'InsF' or 'CtrF'. */
	FString StrokePosition = TEXT("OutF");

	TSharedPtr<FDreamPsdDescriptor> Descriptor;
};

/** A layer's effects ('lfx2', or the several-of-a-kind 'lmfx'). */
struct DREAMGUIPSD_API FDreamPsdLayerEffects
{
	bool bPresent = false;
	/** "Layer effects on" as a whole. */
	bool bMasterSwitch = true;
	/** The effects' scale, 1 is 100 %. */
	float Scale = 1.0f;
	TArray<FDreamPsdEffect> Effects;
	TSharedPtr<FDreamPsdDescriptor> Descriptor;

	bool HasEnabled() const;
};

/** What a layer record turned out to be. */
enum class EDreamPsdLayerKind : uint8
{
	/** Pixels. Smart objects are pixels too (their rendered pixels), with bSmartObject set. */
	Pixel,
	/** The record that opens a group: the group's own record, written after its children. */
	GroupStart,
	/** The bounding divider at the bottom of a group, written before its children. */
	GroupEnd,
	Text,
	/** A fill with a vector mask: a shape layer. */
	Shape,
	/** A fill layer with no vector mask. */
	Fill,
	Adjustment,
};

/** A tagged block the reader keeps without interpreting. */
struct DREAMGUIPSD_API FDreamPsdTaggedBlock
{
	FString Key;
	TArray<uint8> Data;
};

struct DREAMGUIPSD_API FDreamPsdChannelInfo
{
	int16 Id = 0;
	uint64 Length = 0;
	uint16 Compression = 0;
};

struct DREAMGUIPSD_API FDreamPsdLayer
{
	/** Its index in the file's records, bottom first. */
	int32 RecordIndex = INDEX_NONE;
	/** 'luni', or the record's Pascal name. */
	FString Name;
	/** 'lyid', or -1 when the file has none. */
	int32 LayerId = -1;
	EDreamPsdLayerKind Kind = EDreamPsdLayerKind::Pixel;
	/** Document pixels. */
	FIntRect Rect;
	TArray<FDreamPsdChannelInfo> Channels;
	/** The blend mode key ('norm', 'mul ', 'pass'...). A group's is its section divider's when it has one. */
	FString BlendKey = TEXT("norm");
	/** 0..255. */
	uint8 Opacity = 255;
	/** 'iOpa', 0..255. */
	uint8 FillOpacity = 255;
	/** Clipped to the layer below. */
	bool bClipping = false;
	bool bVisible = true;
	bool bTransparencyProtected = false;
	bool bPixelDataIrrelevant = false;
	/** 'lsct' type: 0 other, 1 open group, 2 closed group, 3 divider. */
	int32 SectionType = 0;
	/** 'clbl': blend clipped layers as a group. */
	bool bBlendClippedElements = true;
	/** 'infx': blend interior effects as a group. */
	bool bBlendInteriorElements = false;
	/** 'knko'. */
	uint8 Knockout = 0;
	/** 'lclr': the colour tag. */
	int32 ColorTag = 0;
	bool bSmartObject = false;
	/** 'artb': an artboard, Rect being its rectangle. */
	bool bArtboard = false;
	FIntRect ArtboardRect;

	FDreamPsdLayerMask Mask;
	FDreamPsdVectorMask VectorMask;
	FDreamPsdLiveShape LiveShape;
	FDreamPsdStroke Stroke;
	FDreamPsdFill Fill;
	TSharedPtr<FDreamPsdText> Text;
	TSharedPtr<FDreamPsdAdjustment> Adjustment;
	FDreamPsdLayerEffects Effects;

	/** Rect-sized RGBA, the alpha channel (-1) in A; opaque when the layer has none. Empty when the layer has no pixels. */
	FDreamPsdImage Pixels;

	/** Every key of every tagged block, in order, the typed ones included. */
	TArray<FString> BlockKeys;
	/** The blocks no part of the reader interprets. */
	TArray<FDreamPsdTaggedBlock> OtherBlocks;

	bool IsGroupStart() const { return Kind == EDreamPsdLayerKind::GroupStart; }
	bool IsGroupEnd() const { return Kind == EDreamPsdLayerKind::GroupEnd; }
	bool HasPixels() const { return !Pixels.IsEmpty(); }
};

/** A pattern, from the document's pattern blocks ('Patt', 'Pat2', 'Pat3'). */
struct DREAMGUIPSD_API FDreamPsdPattern
{
	FString Name;
	FString Id;
	FDreamPsdImage Image;
};

/** The layers as a tree: a group's children in drawing order (bottom first), as the records are written. */
struct DREAMGUIPSD_API FDreamPsdLayerNode
{
	/** Into FDreamPsdDocument::Layers; the group's own record for a group, INDEX_NONE for the root. */
	int32 LayerIndex = INDEX_NONE;
	TArray<FDreamPsdLayerNode> Children;
	bool IsGroup() const { return LayerIndex == INDEX_NONE || Children.Num() > 0 || bGroup; }
	bool bGroup = false;
};

struct DREAMGUIPSD_API FDreamPsdDocument
{
	/** 1 PSD, 2 PSB. */
	int32 Version = 1;
	int32 ChannelCount = 0;
	int32 Width = 0;
	int32 Height = 0;
	/** 1, 8, 16 or 32 bits per channel. */
	int32 Depth = 8;
	EDreamPsdColorMode ColorMode = EDreamPsdColorMode::RGB;
	/** Indexed documents: 256 colours. */
	TArray<FColor> ColorTable;

	/** Pixels per inch (resource 1005); 72 when the file has none. */
	double ResolutionX = 72.0;
	double ResolutionY = 72.0;
	/** Resources 1037 and 1049: the angle and altitude effects using the global light take. */
	float GlobalAngle = 120.0f;
	float GlobalAltitude = 30.0f;
	/** Resource 1057: the file has a real merged image ("maximize compatibility"). */
	bool bHasRealMergedData = true;
	/** The ids of every image resource, in order. */
	TArray<uint16> ResourceIds;

	/** Bottom first, as the file writes them. Groups are a GroupEnd record, the children, then the GroupStart record. */
	TArray<FDreamPsdLayer> Layers;
	/** The layer count was negative: the merged image's first alpha channel is its transparency. */
	bool bMergedHasTransparency = false;

	/** The merged image: Width x Height. Empty when the file stored none. */
	FDreamPsdImage Composite;

	TArray<FDreamPsdPattern> Patterns;

	bool IsLarge() const { return Version == 2; }
	/** The layer tree (built from the section records). */
	FDreamPsdLayerNode BuildTree(FDreamPsdMessages* OutMessages = nullptr) const;
	const FDreamPsdPattern* FindPattern(const FString& InId) const;
};
