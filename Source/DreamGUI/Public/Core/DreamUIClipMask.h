// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Templates/Function.h"
#include "UObject/ObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "DreamUIClipMask.generated.h"

class UTexture;

/** The shape a clip mask cuts: the clip rect itself (with the widget's clipping corner radius), or one inscribed in it. */
UENUM(BlueprintType)
enum class EDreamClipMaskShape : uint8
{
	/** The clip rect, rounded by the widget's Clipping Corner Radius. */
	Rect,
	/** The ellipse that touches the clip rect's four sides. */
	Ellipse,
	/** A regular polygon fitted to the clip rect, a corner straight up. */
	Polygon,
	/** A star fitted to the clip rect, a point straight up. */
	Star,
	/** An ellipse with an elliptical hole in its middle. */
	Ring,
};

/** What the mask's value comes from inside its shape. */
UENUM(BlueprintType)
enum class EDreamClipMaskSource : uint8
{
	/** The shape alone. */
	None,
	/** A channel of a texture, laid over the clip rect. */
	Texture,
	/** A pattern computed for each pixel: gradients, stripes, checks, noise. */
	Procedural,
};

UENUM(BlueprintType)
enum class EDreamClipMaskChannel : uint8
{
	Alpha,
	Red,
	Green,
	Blue,
	/** Rec. 709 luminance of the colour as the texture is sampled. */
	Luminance,
};

/** How a mask texture is laid over the clip rect. */
UENUM(BlueprintType)
enum class EDreamClipMaskFill : uint8
{
	/** Stretched to the clip rect. */
	Stretch,
	/** As large as fits whole, its aspect kept; nothing outside it. */
	Fit,
	/** As small as covers the rect, its aspect kept. */
	Fill,
	/** At its own size in pixels, repeated. */
	Tile,
};

/** A procedural mask's pattern. The gradients run 0 to 1 across the clip; the others repeat Frequency times across it. */
UENUM(BlueprintType)
enum class EDreamClipMaskPattern : uint8
{
	/** 0 at the left edge to 1 at the right, turned by Rotation. */
	Linear,
	/** 0 at the centre to 1 at the middle of each side. */
	Radial,
	/** 0 to 1 clockwise from twelve o'clock: a clock wipe. */
	Angular,
	/** 0 at the centre to 1 at the middle of each side, in a diamond. */
	Diamond,
	/** 0 to 1 across each of Frequency stripes. */
	Blinds,
	/** 0 and 1 in a checkerboard Frequency squares across. */
	Checker,
	/** Smooth value noise, Frequency cells across, Seed choosing which. */
	Noise,
};

/** What a pointer hit inside a masked clip asks of the mask. */
UENUM(BlueprintType)
enum class EDreamClipMaskHitTest : uint8
{
	/** The clip rect, as without a mask: cheap, and blind to the mask. */
	Bounds,
	/** The mask: a point hits where the mask is at least one half. A texture is read only if it can be (a Texture2D whose pixels the CPU has); otherwise it counts as white. */
	Mask,
};

/**
 * A mask on a clipping widget's clip: a shape inscribed in the clip rect, feathered, multiplied by a texture's channel or
 * a procedural pattern mapped through a threshold, and optionally inverted. It clips the widget's whole subtree, on every
 * path that draws it (the built-in shader, the UI materials, world space, render layers and perspective scopes), because
 * it is evaluated in the clip's own frame -- the frame the clip rect is.
 *
 * A mask at its defaults is no mask: the clip is the plain rect it always was, costing nothing more (IsActive).
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamClipMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask")
	EDreamClipMaskShape Shape = EDreamClipMaskShape::Rect;
	/** A polygon's sides, a star's points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "2", ClampMax = "64", UIMin = "3", UIMax = "16", EditCondition = "Shape==EDreamClipMaskShape::Polygon||Shape==EDreamClipMaskShape::Star", EditConditionHides))
	int32 Sides = 6;
	/** A polygon's corners rounded by this many pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "0.0", EditCondition = "Shape==EDreamClipMaskShape::Polygon", EditConditionHides))
	float Roundness = 0.0f;
	/** A star's inner corners, or a ring's hole, as a share of the outer size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Shape==EDreamClipMaskShape::Star||Shape==EDreamClipMaskShape::Ring", EditConditionHides))
	float InnerRatio = 0.5f;
	/**
	 * Pixels over which the mask fades in from its edge. A Rect fades each side in over its own width, measured inwards;
	 * every other shape uses Left for the whole outline, centred on it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask")
	FMargin Feather = FMargin(0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask")
	EDreamClipMaskSource Source = EDreamClipMaskSource::None;
	/**
	 * The mask texture. Two texture masks at most take effect along one chain of clips (the outermost two); a third is
	 * ignored, with a warning. A draw call binds one texture per slot, so content under different mask textures is drawn
	 * in different draw calls.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source==EDreamClipMaskSource::Texture", EditConditionHides))
	TObjectPtr<UTexture> Texture = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source==EDreamClipMaskSource::Texture", EditConditionHides))
	EDreamClipMaskChannel Channel = EDreamClipMaskChannel::Alpha;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source==EDreamClipMaskSource::Texture", EditConditionHides))
	EDreamClipMaskFill Fill = EDreamClipMaskFill::Stretch;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source==EDreamClipMaskSource::Procedural", EditConditionHides))
	EDreamClipMaskPattern Pattern = EDreamClipMaskPattern::Linear;
	/** Stripes, squares or noise cells across the clip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "0.0", EditCondition = "Source==EDreamClipMaskSource::Procedural&&(Pattern==EDreamClipMaskPattern::Blinds||Pattern==EDreamClipMaskPattern::Checker||Pattern==EDreamClipMaskPattern::Noise)", EditConditionHides))
	float Frequency = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "0", EditCondition = "Source==EDreamClipMaskSource::Procedural&&Pattern==EDreamClipMaskPattern::Noise", EditConditionHides))
	int32 Seed = 0;
	/** Degrees clockwise the texture or pattern is turned about the clip's centre: a linear gradient's angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (UIMin = "-360.0", UIMax = "360.0", EditCondition = "Source!=EDreamClipMaskSource::None", EditConditionHides))
	float Rotation = 0.0f;
	/** The texture or pattern's scale: 2 shows it twice as large. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source!=EDreamClipMaskSource::None", EditConditionHides))
	FVector2D Scale = FVector2D(1.0, 1.0);
	/** Moves the texture or pattern, in its own coordinates (1 is its whole width or height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (EditCondition = "Source!=EDreamClipMaskSource::None", EditConditionHides))
	FVector2D Offset = FVector2D(0.0, 0.0);
	/**
	 * The source value v becomes saturate((v - Threshold + Softness) / (2 * Softness)): at 0.5 and 0.5, v as it is. A
	 * small Softness turns it into a cut at Threshold, and driving Threshold makes wipes and dissolves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (UIMin = "0.0", UIMax = "1.0", EditCondition = "Source!=EDreamClipMaskSource::None", EditConditionHides))
	float Threshold = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask", meta = (ClampMin = "0.0", UIMax = "0.5", EditCondition = "Source!=EDreamClipMaskSource::None", EditConditionHides))
	float Softness = 0.5f;
	/** Shows what the mask hides and hides what it shows -- outside the clip rect included: a cut-out, such as a spotlight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask")
	bool bInvert = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clip Mask")
	EDreamClipMaskHitTest HitTest = EDreamClipMaskHitTest::Bounds;

	/** Anything but the plain clip rect: a mask at its defaults costs the clip nothing. */
	bool IsActive() const;
	/** The texture this mask reads, when it is active and reads one; null otherwise. */
	UTexture* GetActiveTexture() const;

	bool operator==(const FDreamClipMask& Other) const;
	bool operator!=(const FDreamClipMask& Other) const { return !(*this == Other); }
};

/**
 * The layout of a row of the clip data texture, each constant paired with a #define of DreamUIClip.ush that a test holds it
 * to. A level is BaseTexels texels -- the canvas-to-clipper matrix, whose last column is (half width, half height, mask
 * flag, is valid), and the corner radius -- and MaskTexels more when its mask flag is set.
 */
namespace DreamUIClipLayout
{
	inline constexpr int32 Depth = 16;
	inline constexpr int32 BaseTexels = 5;
	inline constexpr int32 MaskTexels = 4;
	inline constexpr int32 BytesPerTexel = 16;
	/** A row holds the deepest chain with every level masked. */
	inline constexpr int32 RowTexels = Depth * (BaseTexels + MaskTexels);
	inline constexpr int32 RowBytes = RowTexels * BytesPerTexel;
	/**
	 * What a chain with no mask on any level uploads: the row as it was before masks, byte for byte. Its levels are where
	 * they always were, and so is the zero that ends it.
	 */
	inline constexpr int32 UnmaskedRowBytes = Depth * BaseTexels * BytesPerTexel;

	inline constexpr int32 KindShift = 0;
	inline constexpr int32 ShapeShift = 2;
	inline constexpr int32 SourceShift = 5;
	inline constexpr int32 ChannelShift = 7;
	inline constexpr int32 SlotShift = 10;
	inline constexpr int32 PatternShift = 11;
	inline constexpr int32 InvertShift = 14;
	inline constexpr int32 AddressShift = 15;
	inline constexpr int32 NoCentreShift = 17;

	inline constexpr int32 KindMask = 0;
	inline constexpr int32 KindUnderlayImage = 1;
	inline constexpr int32 KindUnderlayShape = 2;

	inline constexpr int32 AddressBounded = 0;
	inline constexpr int32 AddressRepeat = 1;
	inline constexpr int32 AddressRaw = 2;

	/** How many mask textures a draw binds: the chain's outermost texture masks take them in turn. */
	inline constexpr int32 TextureSlots = 2;
}

/**
 * The mask textures an element's clips read, by slot (FDreamUIClipData::GetMaskTextures): part of a draw call's identity,
 * since a draw binds one texture per slot. An element that reads nothing from a slot draws the same whatever is bound
 * there, so it batches with any draw call that slot is concerned.
 */
struct FDreamUIMaskTextures
{
	TWeakObjectPtr<UTexture> Slots[DreamUIClipLayout::TextureSlots];

	/** Compared as the keys they are, never resolved: the batching compares them on its worker. */
	bool IsSameAs(const FDreamUIMaskTextures& Other) const
	{
		for (int32 Slot = 0; Slot < DreamUIClipLayout::TextureSlots; ++Slot)
		{
			if (!Slots[Slot].HasSameIndexAndSerialNumber(Other.Slots[Slot]))
			{
				return false;
			}
		}
		return true;
	}
	/** Whether a draw call binding these can draw an element reading InElement: no slot both read differently. */
	bool CanDraw(const FDreamUIMaskTextures& InElement) const
	{
		for (int32 Slot = 0; Slot < DreamUIClipLayout::TextureSlots; ++Slot)
		{
			if (!InElement.Slots[Slot].IsExplicitlyNull() && !Slots[Slot].IsExplicitlyNull()
				&& !Slots[Slot].HasSameIndexAndSerialNumber(InElement.Slots[Slot]))
			{
				return false;
			}
		}
		return true;
	}
	/** A draw call taking InElement in binds what it reads too. */
	void Take(const FDreamUIMaskTextures& InElement)
	{
		for (int32 Slot = 0; Slot < DreamUIClipLayout::TextureSlots; ++Slot)
		{
			if (!InElement.Slots[Slot].IsExplicitlyNull())
			{
				Slots[Slot] = InElement.Slots[Slot];
			}
		}
	}
};

/**
 * What a widget that clips to the layer below it (UDreamWidget::bClipToBelow) is clipped by: the drawn shape of the
 * element below, in that element's frame. An image is its quad -- simple or nine-sliced, the texture's coordinates at the
 * slice lines -- and a rect block its body's rounded rect.
 */
struct FDreamClipUnderlay
{
	enum class EKind : uint8
	{
		/** Not an element this can follow: text, a group, a tiled or filled image, a custom mesh. Nothing is clipped. */
		None,
		Image,
		Shape,
	};
	EKind Kind = EKind::None;
	/** The drawn quad's centre in the element's local space (its pivot at the origin), and its half size. */
	FVector2f Center = FVector2f::ZeroVector;
	FVector2f HalfSize = FVector2f::ZeroVector;
	/** A shape's corner radius, in UDreamWidget::ClippingCornerRadius's order. */
	FVector4f CornerRadius = FVector4f::Zero();
	/** An image's texture, its borders in pixels (left, right, bottom, top), and its u and v at the four lines across and up. */
	TWeakObjectPtr<UTexture> Texture;
	FVector4f Border = FVector4f::Zero();
	FVector4f U = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
	FVector4f V = FVector4f(1.0f, 1.0f, 0.0f, 0.0f);
	bool bFillCentre = true;
	/** The element's own opacity: the clipped content shows through it as it does. */
	float Opacity = 1.0f;
	/** Why it is None, for the warning. */
	FString Unsupported;
	/** None only because the element has not drawn yet: nothing to warn about. */
	bool bPending = false;
};

/** One level of a clip row, as the shader reads it. */
struct FDreamUIClipLevel
{
	FVector2f HalfSize = FVector2f::ZeroVector;
	/** As uploaded: flipped into DreamUIClip_sdRoundRect's order. */
	FVector4f CornerRadius = FVector4f::Zero();
	bool bMasked = false;
	FVector4f Mask[DreamUIClipLayout::MaskTexels];
};

/**
 * DreamUIClip.ush on the CPU: every function there has its twin here, step for step, so that a hit test answers what the
 * picture shows. Where the shader measures its antialiasing with fwidth, these take a width; 0 is a hard edge, whose
 * boundary is where the shader's ramp crosses one half.
 */
struct DREAMGUI_API FDreamUIClipMaskMath
{
	static float SdRoundRect(FVector2f P, FVector2f B, FVector4f R);
	static float SdEllipse(FVector2f P, FVector2f B);
	static float SdStar(FVector2f Q, float Points, float R, float Inner);
	static float ShapeDistance(int32 Shape, FVector2f P, FVector2f B, float ParamA, float ParamB);
	static uint32 Hash(uint32 X, uint32 Y, uint32 Seed);
	static float Pattern(int32 InPattern, FVector2f UV, float Frequency, uint32 Seed);
	static float Remap(float Value, float Threshold, float Softness);
	static float SliceAxis(float T, float Size, float Low, float High, const FVector4f& U);
	static float Edge(float Distance, float Width);
	/**
	 * A masked level's alpha at P (the clip's frame, +x right, +y up), its texture -- if it reads one -- answered by
	 * InSample, which returns false where it cannot read: the texel then counts as white, as DreamUI_CalculateClipData
	 * has it.
	 */
	static float EvaluateMaskedLevel(const FDreamUIClipLevel& Level, FVector2f P, float AAWidth, TFunctionRef<bool(FVector2f UV, FLinearColor& OutTexel)> InSample);
	/** A level masked or not. */
	static float EvaluateLevel(const FDreamUIClipLevel& Level, FVector2f P, float AAWidth, TFunctionRef<bool(FVector2f UV, FLinearColor& OutTexel)> InSample);

	/**
	 * A widget's mask as its level's four texels. TextureSlot is the slot its texture reads from, or INDEX_NONE when it
	 * has none (no texture, or a third texture along the chain), in which case the texture is left out. False, and nothing
	 * written, for a mask at its defaults.
	 */
	static bool PackMask(const FDreamClipMask& Mask, FVector2f HalfSize, int32 TextureSlot, FVector4f OutMask[DreamUIClipLayout::MaskTexels]);
	/** An underlay as its level's four texels; its texture as PackMask's. */
	static void PackUnderlay(const FDreamClipUnderlay& Underlay, int32 TextureSlot, FVector4f OutMask[DreamUIClipLayout::MaskTexels]);
	/** Where a mask texture is read in the texel; the texture's surface is how a Fit, Fill or Tile is sized. */
	static FVector2f TextureSize(const UTexture* InTexture);
};
