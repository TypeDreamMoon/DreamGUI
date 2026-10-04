// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamGUISettings.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Utils/DreamUIUtils.h"
#include "CoreGlobals.h"
#include "Misc/ScopeExit.h"
#include "DreamGUI.h"

namespace DreamTextPainterLocal
{
	/** What a field glyph's quad draws: UV2.x is DilateEm + DreamTextQuadCode::FieldLayerStride * Layer. */
	enum class EGlyphLayer : int32 { Face = 0, Effects = 1, Both = 2 };

	/**
	 * Grows a glyph quad into the atlas's distance field so an effect that reaches ReachEm outside the
	 * glyph's edge has room to be drawn. The layout's quads sit QuadMarginTexels into the spread; the
	 * growth stops at the spread, which is where the field stops being true. The shader clamps its
	 * reaches to the same limit, so whatever the field cannot hold is dropped rather than cut.
	 */
	static FDreamUICharData GrowIntoField(const FDreamUICharData& Glyph, float ReachEm, float Size, const FDreamTextPaintParams& Params)
	{
		if (!Params.bDistanceField || Params.EmTexels <= 0.0f || ReachEm <= 0.0f)return Glyph;
		// 1.5 texels: the anti-aliasing band plus the bilinear footprint, same margin the shader keeps.
		const float NeedTexels = ReachEm * Params.EmTexels + 1.5f;
		const float AvailableTexels = FMath::Max(Params.FieldSpreadTexels - Params.QuadMarginTexels, 0.0f);
		const float GrowTexels = FMath::Clamp(NeedTexels - Params.QuadMarginTexels, 0.0f, AvailableTexels);
		if (GrowTexels <= 0.0f)return Glyph;
		const float GrowPx = GrowTexels * Size / Params.EmTexels;
		const float GrowUV = GrowTexels * Params.TexelToUV;
		FDreamUICharData Grown = Glyph;
		Grown.Width += GrowPx + GrowPx;
		Grown.Height += GrowPx + GrowPx;
		Grown.XOffset -= GrowPx;
		Grown.YOffset += GrowPx;
		Grown.MinUV -= FVector2f(GrowUV, GrowUV);
		Grown.MaxUV += FVector2f(GrowUV, GrowUV);
		return Grown;
	}

	/** Writes one axis-aligned quad: positions as (0, x, y) in the text's local plane, 0/3/2 + 0/1/3 winding. */
	struct FQuadWriter
	{
		TArray<FDreamUIOriginVertexData>& OriginVertices;
		TArray<FDreamUIMeshVertex>& Vertices;
		TArray<FDreamUIMeshIndex>& Triangles;
		int32 VertexCursor = 0;

		FQuadWriter(FDreamUIGeometry& Geometry)
			: OriginVertices(Geometry.OriginVertices)
			, Vertices(Geometry.Vertices)
			, Triangles(Geometry.Triangles)
		{
		}

		/** Per-quad fill channels: UV2.y sweeps RunX0..RunX1 across the quad, UV3.x is the progress; a field glyph's UV3.y is the glow boost. */
		struct FFill
		{
			float RunX0 = 0.0f;
			float RunX1 = 1.0f;
			float Progress = 1.0f;
			float GlowBoost = 0.0f;
		};

		/**
		 * UV2.x is the quad's code (DreamTextQuadCode), UV3.y what that kind of quad keeps there: see DreamTextQuadCode. UV4 is
		 * (0, 0), a quad that paints nothing, until the quad is given its paint slot -- written here every time, because the
		 * geometry's memory may be the last paint's and still hold its UV4.
		 */
		void WriteQuad(float Left, float Right, float Bottom, float Top, const FDreamUICharData& Glyph,
			const FColor& Color, float UV2X, float UV3Y, const FFill& Fill, int32& IndexCursor)
		{
			const int32 Start = VertexCursor;
			OriginVertices[Start].Position = FVector3f(0, Left, Bottom);
			OriginVertices[Start + 1].Position = FVector3f(0, Right, Bottom);
			OriginVertices[Start + 2].Position = FVector3f(0, Left, Top);
			OriginVertices[Start + 3].Position = FVector3f(0, Right, Top);

			Vertices[Start].TextureCoordinate[0] = Glyph.GetUV0();
			Vertices[Start + 1].TextureCoordinate[0] = Glyph.GetUV1();
			Vertices[Start + 2].TextureCoordinate[0] = Glyph.GetUV2();
			Vertices[Start + 3].TextureCoordinate[0] = Glyph.GetUV3();
			for (int32 i = 0; i < 4; i++)
			{
				auto& Vertex = Vertices[Start + i];
				Vertex.TextureCoordinate[1].Y = Glyph.SliceIndex;
				// Vertices 0 and 2 are the quad's left edge, 1 and 3 its right.
				const float RunX = (i == 0 || i == 2) ? Fill.RunX0 : Fill.RunX1;
				Vertex.TextureCoordinate[2] = FVector2f(UV2X, RunX);
				Vertex.TextureCoordinate[3] = FVector2f(Fill.Progress, UV3Y);
				Vertex.Color = Color;
				Vertex.UV4 = FVector2f::ZeroVector;
			}

			Triangles[IndexCursor] = Start;
			Triangles[IndexCursor + 1] = Start + 3;
			Triangles[IndexCursor + 2] = Start + 2;
			Triangles[IndexCursor + 3] = Start;
			Triangles[IndexCursor + 4] = Start + 1;
			Triangles[IndexCursor + 5] = Start + 3;

			VertexCursor += 4;
			IndexCursor += 6;
		}

		/** Leans the quad written at Start: its top edge right, its bottom edge left, like an italic glyph. */
		void ShearQuad(int32 Start, float BottomOffset, float TopOffset)
		{
			OriginVertices[Start].Position.Y -= BottomOffset;
			OriginVertices[Start + 1].Position.Y -= BottomOffset;
			OriginVertices[Start + 2].Position.Y += TopOffset;
			OriginVertices[Start + 3].Position.Y += TopOffset;
		}
	};

	/** Render opacity on a colour the author wrote: a tag's, a style's, a hovered link's. */
	static FColor FadeAuthoredColor(FColor Color, float Opacity)
	{
		Color.A = (uint8)FMath::Clamp(FMath::RoundToInt(Color.A * Opacity), 0, 255);
		return Color;
	}

	/**
	 * What an item is painted with. A <color> tag's colour keeps the alpha the author wrote; the hierarchy's fade is
	 * applied here, at paint time, so changing it never invalidates the layout (BaseColor carries it already). A custom
	 * style's pending Multiply multiplies whatever that gives, and a tag colour override replaces it outright --
	 * bOutRecoloured says one did, which makes the item solid whatever paints the text has (a hovered link).
	 */
	static FColor ResolveItemColor(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params, const FDreamTextGlyphItem& Item,
		bool& bOutRecoloured)
	{
		bOutRecoloured = false;
		FColor Color = Params.BaseColor;
		if (Item.Style.bHasColor)
		{
			Color = FadeAuthoredColor(Item.Style.Color, Params.RichTextTagOpacity);
		}
		if (Item.Style.bHasMultiplyColor)
		{
			Color = FDreamUIUtils::MultiplyColor(Color, Item.Style.MultiplyColor);
		}
		if (Params.TagColorOverrides != nullptr)
		{
			// Matched by element, not by visible character, so the spaces between a link's words -- and the
			// underline under them -- change colour with the words.
			for (const TPair<int32, FColor>& Override : *Params.TagColorOverrides)
			{
				if (!DisplayList.CustomTagElementRanges.IsValidIndex(Override.Key))continue;
				const FIntPoint& Range = DisplayList.CustomTagElementRanges[Override.Key];
				if (Item.ElementIndex >= Range.X && Item.ElementIndex <= Range.Y)
				{
					Color = FadeAuthoredColor(Override.Value, Params.RichTextTagOpacity);
					bOutRecoloured = true;
				}
			}
		}
		return Color;
	}

	/**
	 * The slot an item paints with (FDreamTextPaintParams::Paints), a colour glyph's exception aside: none for a hovered link
	 * and for an item a custom style took every paint off; its tag paint's slot when the name resolved to one; none for an
	 * item with a solid colour of its own (a <color>, a custom style's Replace), which, being innermost, wins over the text's
	 * paints; else the text's own slot, when it paints the face or the overlay.
	 */
	static int32 ResolveItemSlot(const FDreamTextPaints& Paints, const FDreamTextGlyphItem& Item, bool bRecoloured)
	{
		if (bRecoloured || Item.Style.bPaintRemoved)
		{
			return 0;
		}
		if (Item.Style.PaintIndex != INDEX_NONE && Paints.NameSlots != nullptr && Paints.NameSlots->IsValidIndex(Item.Style.PaintIndex))
		{
			const int32 NameSlot = (*Paints.NameSlots)[Item.Style.PaintIndex];
			if (NameSlot >= DreamTextQuadCode::FirstTagSlot && NameSlot <= DreamTextQuadCode::MaxSlot && Paints.Slots[NameSlot].IsUsed())
			{
				return NameSlot;
			}
		}
		if (Item.Style.bHasColor)
		{
			return 0;
		}
		const FDreamTextPaintSlot& OwnSlot = Paints.Slots[DreamTextQuadCode::TextSlot];
		return (OwnSlot.Face != nullptr || OwnSlot.Overlay != nullptr) ? DreamTextQuadCode::TextSlot : 0;
	}

	/** What one item paints with, worked out once a paint. */
	struct FItemPaint
	{
		/** Its own colour, as ever (ResolveItemColor): what a quad that paints nothing is written in. */
		FColor Color = FColor::White;
		/**
		 * What its glyph's face and its strokes are written in: PaintBaseColor, times a custom style's pending Multiply, when
		 * Slot paints the face -- the gradient takes the text's own colour's place -- else Color.
		 */
		FColor SlotColor = FColor::White;
		/** The slot of its strokes, and of its glyph's face -- unless that is a colour glyph, which keeps its own colours (FaceSlot 0). */
		uint8 Slot = 0;
		uint8 FaceSlot = 0;
		/** The slot of its effects copies: the text's own when it paints the outline and no custom style took that off the item. */
		uint8 EffectsSlot = 0;
	};

	/** The extent of a run of the lyric fill across: the quads in it. */
	struct FRunBounds
	{
		float MinX = FLT_MAX;
		float MaxX = -FLT_MAX;
	};

	/** One underline or strikethrough strip: a run of items on one line that draw the same stroke in the same colour and paint. */
	struct FDecorationRun
	{
		/** The run's first item, which its fill channels, its size and -- for its paint's boxes -- its line and its tag run are read from. */
		int32 FirstItem = 0;
		int32 LineIndex = 0;
		float Left = 0.0f;
		float Right = 0.0f;
		float Top = 0.0f;
		float Height = 0.0f;
		FDreamUICharData Glyph;
		FColor Color = FColor::White;
		int32 Segment = INDEX_NONE;
		float DilateEm = 0.0f;
		/** The slots of its face and effects copies (FItemPaint), and under a tag's Run box the piece of the run it stays inside. */
		uint8 Slot = 0;
		uint8 EffectsSlot = 0;
		int32 Fragment = INDEX_NONE;
		/** One character's piece, under a Glyph box: nothing joins it. */
		bool bAlone = false;
	};

	/** How many copies of one quad go into each block of the index buffer, the blocks drawn back to front. */
	struct FQuadCopies
	{
		int32 Shadow = 0;
		int32 Outline = 0;
		int32 Effects = 0;
		int32 Face = 0;

		int32 Total() const { return Shadow + Outline + Effects + Face; }
		void Add(const FQuadCopies& Other, int32 Times)
		{
			Shadow += Other.Shadow * Times;
			Outline += Other.Outline * Times;
			Effects += Other.Effects * Times;
			Face += Other.Face * Times;
		}
	};

	/** Where an item's glyph quad comes from. */
	enum class EItemQuad : uint8
	{
		/** The layout's quad, when the item emits one: a field or bitmap glyph, as ever. */
		Layout,
		/** A colour glyph's padded cell (FDreamUICharData::bColor). */
		Color,
		/** A coverage glyph on the device pixel grid (FDreamTextCoverageParams). */
		Coverage,
		/** Small enough for coverage, but its coverage glyph is still being made: the layout's quad this time, if it has one. */
		CoveragePending,
	};

	/** A coverage glyph placed on the device pixel grid, mapped back into the text's local space. */
	struct FCoverageQuad
	{
		float Left = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;
		float Top = 0.0f;
		/** The glyph's texels: only MinUV, MaxUV and SliceIndex are read. */
		FDreamUICharData Texels;
		int32 Phase = 0;
		/**
		 * Delta, the face's snap shift: how far landing on the grid moved it from the pen, in local units -- the pen drawn at
		 * (q / 4, Row) in device pixels instead of at (U, V). The effects hybrid moves the field's effects copy by it.
		 */
		FVector2f Shift = FVector2f::ZeroVector;
	};

	/**
	 * A painted quad's boxes for one item (FDreamTextPaintSlot's HorizontalBox and VerticalBox): UV4 = ((Offset + x - Left) /
	 * Width, (Top - y) / Height) in the text's local space, x right and y up -- u from the box's left edge, v from its top,
	 * as CSS measures a background. A box under 1e-6 across or down gives 0 on that axis.
	 */
	struct FPaintBoxes
	{
		float Left = 0.0f;
		/** A Run box's pieces laid end to end: how far into the run this piece's left edge is. */
		float Offset = 0.0f;
		float Width = 0.0f;
		float Top = 0.0f;
		float Height = 0.0f;

		FVector2f At(float X, float Y) const
		{
			return FVector2f(Width >= 1e-6f ? (Offset + X - Left) / Width : 0.0f, Height >= 1e-6f ? (Top - Y) / Height : 0.0f);
		}
	};

	/** A quad as the layout places it, before any copy is made of it: what every copy of a glyph or a stroke starts from. */
	struct FQuadSource
	{
		/** The item it belongs to: its fill channels, and the item its paint's boxes are measured for. */
		int32 ItemIndex = 0;
		const FDreamUICharData* Glyph = nullptr;
		/** What its lengths in em are of: the glyph's own size, or the style's for a stroke. */
		float Em = 0.0f;
		float Left = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;
		float Top = 0.0f;
		/** Its synthetic bold as a dilation of the field, in em per side. */
		float DilateEm = 0.0f;
		bool bItalic = false;
		/** What an italic copy is sheared about. */
		float BaselineY = 0.0f;
	};

	/**
	 * A paint's working arrays, kept from one paint to the next: a burst of thousands of paints in one frame -- every label of
	 * a wall that starts to turn at once -- then allocates nothing once they have grown. Texts paint on the game thread, which
	 * keeps one set; a paint anywhere else, or one started while another is under way, uses a set of its own.
	 */
	struct FPaintScratch
	{
		TArray<FItemPaint> ItemPaints;
		TArray<EItemQuad> ItemQuads;
		TArray<FCoverageQuad> CoverageQuads;
		TArray<int32> ItemCoverageQuad;
		TArray<FRunBounds> SegmentBounds;
		TArray<FRunBounds> LineBounds;
		TArray<int32> ItemSegment;
		TArray<FDecorationRun> Decorations;
		/** A Run box measured on the text's own paints: per line, its piece of the whole text laid end to end. */
		TArray<FDreamTextPaintFragment> LineRunPieces;
		bool bInUse = false;
	};

	/** Items a kept set of arrays stays sized for: one text longer than this lets them go when it is painted. */
	constexpr int32 PaintScratchKeptItems = 16384;

	/** The game thread's set while nobody uses it, else InLocal. */
	static FPaintScratch& AcquirePaintScratch(FPaintScratch& InLocal)
	{
		if (IsInGameThread())
		{
			static FPaintScratch GameThreadScratch;
			if (!GameThreadScratch.bInUse)
			{
				GameThreadScratch.bInUse = true;
				return GameThreadScratch;
			}
		}
		return InLocal;
	}

	static void ReleasePaintScratch(FPaintScratch& InScratch)
	{
		if (!InScratch.bInUse)return;
		InScratch.bInUse = false;
		if (InScratch.ItemPaints.Max() > PaintScratchKeptItems || InScratch.Decorations.Max() > PaintScratchKeptItems
			|| InScratch.LineBounds.Max() > PaintScratchKeptItems)
		{
			InScratch = FPaintScratch();
		}
	}
}

void FDreamTextPainter::Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
	FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties)
{
	using namespace DreamTextPainterLocal;

	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::TextPaints, 1);
	FPaintScratch LocalScratch;
	FPaintScratch& Scratch = AcquirePaintScratch(LocalScratch);
	ON_SCOPE_EXIT
	{
		ReleasePaintScratch(Scratch);
	};

	OutCharProperties.Reset();
	const TArray<FDreamTextGlyphItem>& Items = DisplayList.Items;
	const FDreamTextCoverageParams& Coverage = Params.Coverage;
	if (Coverage.Report != nullptr)
	{
		*Coverage.Report = FDreamTextCoverageReport();
	}

	const bool bSeparateEffectLayer = Params.bDistanceField && Params.bSeparateEffectLayer;
	// A bitmap font's shadow and outline are more copies of the same quads, drawn under the face: one
	// for the shadow, eight around the glyph for the outline.
	const bool bBitmapShadow = !Params.bDistanceField && Params.BitmapShadowColor.A > 0
		&& !Params.BitmapShadowOffsetEm.IsNearlyZero();
	const bool bBitmapOutline = !Params.bDistanceField && Params.BitmapOutlineColor.A > 0
		&& Params.BitmapOutlineWidthEm > 0.0f;
	// Eight taps: the four sides and the four diagonals, which is what makes a round-ish outline out of
	// offset copies rather than a plus sign.
	static const FVector2f OutlineTaps[8] =
	{
		FVector2f(1.0f, 0.0f), FVector2f(-1.0f, 0.0f), FVector2f(0.0f, 1.0f), FVector2f(0.0f, -1.0f),
		FVector2f(0.7071f, 0.7071f), FVector2f(-0.7071f, 0.7071f), FVector2f(0.7071f, -0.7071f), FVector2f(-0.7071f, -0.7071f),
	};
	const int32 ShadowCopies = bBitmapShadow ? 1 : 0;
	const int32 OutlineCopies = bBitmapOutline ? UE_ARRAY_COUNT(OutlineTaps) : 0;
	// Every quad is written as several copies: an effect copy and a face copy for a field with effects, a shadow,
	// eight outline taps and a face for a bitmap font with both, the face alone otherwise. A quad's copies are
	// neighbours in the vertex buffer -- so a character's vertex range still covers all of them, which is what
	// TextAnimation moves -- while each kind of copy has its own block of the index buffer, back to front: every
	// shadow draws before any outline and every outline before any face, as Slate draws them in whole-run passes. Per
	// glyph, the next glyph's outline used to land on top of this glyph's face wherever the two overlapped.
	FQuadCopies PlainCopies;
	PlainCopies.Shadow = ShadowCopies;
	PlainCopies.Outline = OutlineCopies;
	PlainCopies.Effects = bSeparateEffectLayer ? 1 : 0;
	PlainCopies.Face = 1;
	// A colour glyph has no field, so no outline or glow: its face, and under it one copy for the underlay when the text
	// draws one -- the style's underlay on a field font, the drop shadow on a bitmap font -- made of the glyph's alpha.
	FQuadCopies ColorCopies;
	ColorCopies.Shadow = ShadowCopies;
	ColorCopies.Effects = (Params.bDistanceField && Params.bHasUnderlay) ? 1 : 0;
	ColorCopies.Face = 1;
	// A coverage glyph standing in for a field glyph still pending: its face alone, there being no field quad to draw the
	// effects from yet.
	FQuadCopies FaceOnlyCopies;
	FaceOnlyCopies.Face = 1;
	// An index is a vertex ordinal, and FDreamUIMeshIndex is 16 bits wide unless the 32-bit buffer is
	// compiled in: past the limit every index wraps and the whole block draws as garbage. Stop adding
	// quads at the limit instead.
	const int32 MaxQuads = LEXUI_MAX_VERTEX_COUNT / 4;

	// The effects -- a field's effects copy, a colour glyph's underlay copy, a bitmap font's shadow and outline copies -- fade
	// with the render opacity and the content tint (EffectOpacity), never with the colour of the text or of a tag: a text
	// with a clear face still draws its outline. Whatever animates the characters fades them through their vertices still.
	auto FadeEffectAlpha = [&Params](uint8 InAlpha)
	{
		return (uint8)FMath::Clamp(FMath::RoundToInt(InAlpha * Params.EffectOpacity), 0, 255);
	};
	const uint8 EffectAlpha = FadeEffectAlpha(255);

	// Paints (FDreamTextPaintParams::Paints): per item, the slots its quads take and the colour its face is written in. With
	// no paint every slot is 0 and every colour the item's own, which is what a text wrote before paints existed.
	const FDreamTextPaints& Paints = Params.Paints;
	const bool bPaints = Paints.HasAny();
	const uint8 OutlinePaintSlot = (uint8)((bPaints && Paints.Slots[DreamTextQuadCode::TextSlot].Outline != nullptr) ? DreamTextQuadCode::TextSlot : 0);
	TArray<FItemPaint>& ItemPaints = Scratch.ItemPaints;
	ItemPaints.Reset();
	ItemPaints.SetNumUninitialized(Items.Num());
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ItemIndex++)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		FItemPaint& ItemPaint = ItemPaints[ItemIndex];
		bool bRecoloured = false;
		ItemPaint.Color = ResolveItemColor(DisplayList, Params, Item, bRecoloured);
		const int32 Slot = bPaints ? ResolveItemSlot(Paints, Item, bRecoloured) : 0;
		ItemPaint.Slot = (uint8)Slot;
		ItemPaint.FaceSlot = (uint8)(Item.Glyph.bColor ? 0 : Slot);
		ItemPaint.EffectsSlot = Item.Style.bPaintRemoved ? (uint8)0 : OutlinePaintSlot;
		ItemPaint.SlotColor = ItemPaint.Color;
		if (Slot != 0 && Paints.Slots[Slot].Face != nullptr)
		{
			ItemPaint.SlotColor = Item.Style.bHasMultiplyColor
				? FDreamUIUtils::MultiplyColor(Params.PaintBaseColor, Item.Style.MultiplyColor) : Params.PaintBaseColor;
		}
	}

	// Small text from coverage glyphs (FDreamTextCoverageParams). A coverage glyph is a face and nothing else: a text whose
	// quads come with a bitmap font's shadow or outline copies keeps its field quads, and a text with effects draws from
	// coverage only through the effects hybrid (EffectFace), each coverage item's field effects copy under its coverage face.
	const bool bEffectsHybrid = PlainCopies.Effects > 0 && Coverage.EffectFace != EDreamSmallTextEffectFace::Field;
	const bool bCoverage = Coverage.bEnabled && Coverage.Font != nullptr && Coverage.DeviceScale > 0.0f
		&& Coverage.RasterScale > 0.0f && PlainCopies.Shadow == 0 && PlainCopies.Outline == 0
		&& (PlainCopies.Effects == 0 || bEffectsHybrid);
	const float CoverageCodeY = Coverage.Contrast + (Coverage.bLinearTarget ? DreamTextQuadCode::CoverageLinearTarget : 0.0f);
	TArray<EItemQuad>& ItemQuads = Scratch.ItemQuads;
	ItemQuads.Reset();
	ItemQuads.SetNumZeroed(Items.Num());
	TArray<FCoverageQuad>& CoverageQuads = Scratch.CoverageQuads;
	CoverageQuads.Reset();
	TArray<int32>& ItemCoverageQuad = Scratch.ItemCoverageQuad;
	ItemCoverageQuad.Reset();
	if (bCoverage)
	{
		ItemCoverageQuad.SetNumUninitialized(Items.Num());
		for (int32& CoverageQuadIndex : ItemCoverageQuad)
		{
			CoverageQuadIndex = INDEX_NONE;
		}
	}

	// The coverage glyph on the device grid: the pen's baseline row rounded, its column and quarter-pixel phase from the
	// pen's x rounded to a quarter, the box from there in whole pixels, mapped back by 1/S. The UVs are the glyph's texels
	// exactly, so with the box on whole pixels every pixel centre samples one texel centre. Its Shift is how far that moved
	// the pen, which the effects hybrid moves the field's effects copy by.
	auto PlaceCoverageQuad = [&Coverage](const FDreamTextGlyphItem& Item, const FDreamUICoverageGlyph& Glyph)
	{
		const double Scale = Coverage.DeviceScale;
		const double U = Scale * Item.Pen.X + Coverage.SnapOrigin.X;
		const double V = Scale * Item.Pen.Y + Coverage.SnapOrigin.Y;
		const int32 Row = FMath::FloorToInt32(V + 0.5);
		const int32 Quarter = FMath::FloorToInt32(4.0 * U + 0.5);
		FCoverageQuad Quad;
		Quad.Phase = Quarter & 3;
		const int32 Column = (Quarter - Quad.Phase) / 4;
		const double Left = (double)Column + Glyph.BitmapLeft;
		const double Top = (double)Row + Glyph.BitmapTop;
		Quad.Left = (float)((Left - Coverage.SnapOrigin.X) / Scale);
		Quad.Right = (float)((Left + Glyph.Width - Coverage.SnapOrigin.X) / Scale);
		Quad.Top = (float)((Top - Coverage.SnapOrigin.Y) / Scale);
		Quad.Bottom = (float)((Top - Glyph.Height - Coverage.SnapOrigin.Y) / Scale);
		Quad.Texels.MinUV = Glyph.MinUV;
		Quad.Texels.MaxUV = Glyph.MaxUV;
		Quad.Texels.SliceIndex = Glyph.SliceIndex;
		Quad.Shift = FVector2f((float)((0.25 * Quarter - U) / Scale), (float)(((double)Row - V) / Scale));
		return Quad;
	};

	// The effects hybrid's face (FDreamTextCoverageParams::EffectFace): unhinted where a hinted face would stand visibly off
	// the field's outline -- an outline under 2 device pixels, for Auto -- or always, as the setting says. A text with a glow
	// or an underlay and no outline keeps the crisper hinted face under Auto: nothing it draws shows the offset.
	auto WantsUnhintedFace = [&Coverage](const FDreamTextGlyphItem& Item)
	{
		switch (Coverage.EffectFace)
		{
		case EDreamSmallTextEffectFace::Unhinted:
			return true;
		case EDreamSmallTextEffectFace::Auto:
		{
			const float OutlinePixels = Coverage.OutlineWidthEm * Item.GlyphSize * Coverage.DeviceScale;
			return OutlinePixels > 0.0f && OutlinePixels < 2.0f;
		}
		default:
			return false;
		}
	};

	// Where an item's quad comes from. Small text asks the font for a coverage glyph for an item that has a quad, and for one
	// whose field glyph is still on the rasterizer's worker: coverage can draw that before the field glyph lands.
	auto PlanItem = [&](int32 ItemIndex)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		if (Item.Glyph.bColor)
		{
			return Item.bEmit ? EItemQuad::Color : EItemQuad::Layout;
		}
		if (!bCoverage || Item.Kind != EDreamTextItemKind::Glyph)return EItemQuad::Layout;
		const bool bFieldPending = !Item.bEmit && Item.bCountsAsVisible && Item.Glyph.bPending;
		if (!Item.bEmit && !bFieldPending)return EItemQuad::Layout;
		if (Item.GlyphSize <= 0.0f || Item.GlyphSize * Coverage.DeviceScale > Coverage.MaxPixelSize)return EItemQuad::Layout;
		EDreamUICoverageGlyphFlags Flags = EDreamUICoverageGlyphFlags::None;
		if (Item.Style.bSyntheticBold)
		{
			Flags |= EDreamUICoverageGlyphFlags::SyntheticBold;
		}
		if (Item.Style.bSyntheticItalic)
		{
			Flags |= EDreamUICoverageGlyphFlags::SyntheticItalic;
		}
		if (bEffectsHybrid && WantsUnhintedFace(Item))
		{
			Flags |= EDreamUICoverageGlyphFlags::Unhinted;
		}
		FDreamUICoverageGlyph Glyph;
		if (!Coverage.Font->GetCoverageGlyph(Item.Glyph.FaceIndex, Item.Glyph.GlyphIndex,
			FMath::RoundToInt32(Item.GlyphSize * Coverage.RasterScale * 64.0f), Flags, Glyph))
		{
			return EItemQuad::Layout;
		}
		if (Glyph.bPending)return EItemQuad::CoveragePending;
		ItemCoverageQuad[ItemIndex] = CoverageQuads.Add(PlaceCoverageQuad(Item, Glyph));
		return EItemQuad::Coverage;
	};
	// The item has a quad to write: the layout emits one, or a coverage glyph stands in for a field glyph still pending.
	auto DrawsQuad = [&](int32 ItemIndex)
	{
		return Items[ItemIndex].bEmit || ItemQuads[ItemIndex] == EItemQuad::Coverage;
	};

	// Strokes drawn per character, when the characters are animated one by one: the underline and strikethrough pieces
	// of a drawn glyph, written right after it so they fall inside its character's vertex range.
	const bool bStrokesPerCharacter = Params.bStrokesPerCharacter;
	auto CharacterStrokes = [&](int32 ItemIndex)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		if (!bStrokesPerCharacter || !DrawsQuad(ItemIndex) || Item.Kind == EDreamTextItemKind::Image)return 0;
		return (Item.Style.bUnderline ? 1 : 0) + (Item.Style.bStrikethrough ? 1 : 0);
	};

	FQuadCopies Blocks;
	int32 EmitItemCount = Items.Num();
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ItemIndex++)
	{
		ItemQuads[ItemIndex] = PlanItem(ItemIndex);
		if (!DrawsQuad(ItemIndex))continue;
		FQuadCopies ItemCopies;
		if (ItemQuads[ItemIndex] == EItemQuad::Color)
		{
			ItemCopies.Add(ColorCopies, 1);
		}
		else if (ItemQuads[ItemIndex] == EItemQuad::Coverage && !Items[ItemIndex].bEmit)
		{
			ItemCopies.Add(FaceOnlyCopies, 1);
		}
		else
		{
			// A field or bitmap glyph's copies; a coverage glyph's under the effects hybrid are the same two, its field
			// effects copy and its face.
			ItemCopies.Add(PlainCopies, 1);
		}
		ItemCopies.Add(PlainCopies, CharacterStrokes(ItemIndex));
		if (Blocks.Total() + ItemCopies.Total() > MaxQuads)
		{
			// Nothing from here on is drawn: this item measures as the layout made it, like the ones after it.
			ItemQuads[ItemIndex] = EItemQuad::Layout;
			EmitItemCount = ItemIndex;
			static bool bLoggedIndexLimit = false;
			if (!bLoggedIndexLimit)
			{
				bLoggedIndexLimit = true;
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d A text needs more quads than the mesh index type can address (%d vertices); the rest of it is not drawn. (reported once)")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, LEXUI_MAX_VERTEX_COUNT);
			}
			break;
		}
		Blocks.Add(ItemCopies, 1);
	}

	// Fill runs. A glyph belongs to the first segment covering its character, else to its line;
	// each run's horizontal extent comes from the glyph quads in it, so UV2.y spans exactly the ink.
	// Every item gets its segment, spaces included, since an underline spans them.
	const int32 SegmentCount = Params.FillSegments ? Params.FillSegments->Num() : 0;
	TArray<FRunBounds>& SegmentBounds = Scratch.SegmentBounds;
	SegmentBounds.Reset();
	SegmentBounds.SetNum(SegmentCount);
	TArray<FRunBounds>& LineBounds = Scratch.LineBounds;
	LineBounds.Reset();
	LineBounds.SetNum(DisplayList.Lines.Num());
	TArray<int32>& ItemSegment = Scratch.ItemSegment;
	ItemSegment.Reset();
	ItemSegment.SetNumUninitialized(Items.Num());
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ItemIndex++)
	{
		const auto& Item = Items[ItemIndex];
		ItemSegment[ItemIndex] = INDEX_NONE;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; SegmentIndex++)
		{
			const auto& Segment = (*Params.FillSegments)[SegmentIndex];
			if (Item.ElementIndex >= Segment.StartCharIndex && Item.ElementIndex <= Segment.EndCharIndex)
			{
				ItemSegment[ItemIndex] = SegmentIndex;
				break;
			}
		}
		if (!DrawsQuad(ItemIndex))continue;
		// Measured on the layout's quads, so a run sweeps the same way whether its glyphs come from the field or from
		// coverage. A coverage glyph standing in for a field glyph still pending has no layout quad: it measures its own.
		const FCoverageQuad* OwnQuad = !Item.bEmit ? &CoverageQuads[ItemCoverageQuad[ItemIndex]] : nullptr;
		const float GlyphLeft = OwnQuad ? OwnQuad->Left : Item.Pen.X + Item.Glyph.XOffset;
		const float GlyphRight = OwnQuad ? OwnQuad->Right : GlyphLeft + Item.Glyph.Width;
		FRunBounds* Bounds = ItemSegment[ItemIndex] != INDEX_NONE ? &SegmentBounds[ItemSegment[ItemIndex]]
			: (LineBounds.IsValidIndex(Item.LineIndex) ? &LineBounds[Item.LineIndex] : nullptr);
		if (Bounds)
		{
			Bounds->MinX = FMath::Min(Bounds->MinX, GlyphLeft);
			Bounds->MaxX = FMath::Max(Bounds->MaxX, GlyphRight);
		}
	}
	auto MakeFill = [&](int32 ItemIndex, float Left, float Right)
	{
		const auto& Item = Items[ItemIndex];
		FQuadWriter::FFill Fill;
		const FRunBounds* Bounds = nullptr;
		if (ItemSegment[ItemIndex] != INDEX_NONE)
		{
			const auto& Segment = (*Params.FillSegments)[ItemSegment[ItemIndex]];
			Fill.Progress = Segment.Progress;
			Fill.GlowBoost = Segment.GlowBoost;
			Bounds = &SegmentBounds[ItemSegment[ItemIndex]];
		}
		else
		{
			Fill.Progress = Params.FillProgress;
			Fill.GlowBoost = Params.GlowBoost;
			Bounds = LineBounds.IsValidIndex(Item.LineIndex) ? &LineBounds[Item.LineIndex] : nullptr;
		}
		if (Bounds && Bounds->MaxX > Bounds->MinX)
		{
			const float InvWidth = 1.0f / (Bounds->MaxX - Bounds->MinX);
			Fill.RunX0 = (Left - Bounds->MinX) * InvWidth;
			Fill.RunX1 = (Right - Bounds->MinX) * InvWidth;
		}
		return Fill;
	};
	auto DilateOf = [&Params](const FDreamTextGlyphItem& Item)
	{
		// Shader-side bold dilates the regular glyph by BoldDilateEm per side; a real bold face needs none.
		return (Item.Style.bSyntheticBold && Params.bDistanceField && Params.BoldDilateEm > 0.0f) ? Params.BoldDilateEm : 0.0f;
	};
	// The em a glyph's own lengths are in: its size as drawn, which a fallback face's scale is part of (GlyphSize). The field
	// it samples is that size, so the shader's dilation and effects are in that em, and so is its quad's growth into the field
	// and a bitmap font's shadow and outline around it. A stroke stays in the style's em.
	auto GlyphEmOf = [](const FDreamTextGlyphItem& Item)
	{
		return Item.GlyphSize > 0.0f ? Item.GlyphSize : Item.Style.Size;
	};

	// Per slot, for its strokes: cut into a piece per character when it measures a Glyph box, so every piece has its own
	// character's box; kept inside one tag run's piece when a tag slot measures a Run box. And whether any slot measures a
	// Run box at all.
	bool bSlotStrokesPerCharacter[DreamTextQuadCode::SlotCount] = {};
	bool bSlotStrokesPerRun[DreamTextQuadCode::SlotCount] = {};
	bool bAnyRunBox = false;
	if (bPaints)
	{
		for (int32 SlotIndex = DreamTextQuadCode::TextSlot; SlotIndex < DreamTextQuadCode::SlotCount; SlotIndex++)
		{
			const FDreamTextPaintSlot& PaintSlot = Paints.Slots[SlotIndex];
			if (!PaintSlot.IsUsed())continue;
			bSlotStrokesPerCharacter[SlotIndex] = PaintSlot.HorizontalBox == EDreamTextPaintBox::Glyph || PaintSlot.VerticalBox == EDreamTextPaintBox::Glyph;
			const bool bRunBox = PaintSlot.HorizontalBox == EDreamTextPaintBox::Run || PaintSlot.VerticalBox == EDreamTextPaintBox::Run;
			bSlotStrokesPerRun[SlotIndex] = bRunBox && SlotIndex >= DreamTextQuadCode::FirstTagSlot;
			bAnyRunBox |= bRunBox;
		}
	}
	// A Run box for an item no tag run covers -- the text's own paints measured across Runs: the whole text as one run, its
	// lines laid end to end in line order (CSS's slice, as for an inline element holding all of it), each line's piece its
	// line box.
	TArray<FDreamTextPaintFragment>& LineRunPieces = Scratch.LineRunPieces;
	LineRunPieces.Reset();
	if (bAnyRunBox)
	{
		float WholeRunWidth = 0.0f;
		for (int32 LineIndex = 0; LineIndex < DisplayList.LineBoxes.Num(); LineIndex++)
		{
			FDreamTextPaintFragment& Piece = LineRunPieces.AddDefaulted_GetRef();
			Piece.LineIndex = LineIndex;
			Piece.Box = DisplayList.LineBoxes[LineIndex];
			Piece.RunOffset = WholeRunWidth;
			WholeRunWidth += FMath::Max(Piece.Box.GetWidth(), 0.0f);
		}
		for (FDreamTextPaintFragment& Piece : LineRunPieces)
		{
			Piece.RunWidth = WholeRunWidth;
		}
	}

	// Underlines and strikethroughs: one strip per run of items on a line that draw the same stroke in the same
	// colour, spaces included -- the layout clears the flag on the spaces that hang off a line's end and on anything a
	// clamp removed. A strip per glyph left a gap at every space and, under negative letter spacing, quads of negative
	// width that blended twice where they overlapped. Drawn per character instead, the strokes are in the glyph loop.
	// A painted strip is measured in one box, so a run is also cut where the paint's slot changes, where its tag run does
	// under a Run box, and at every character under a Glyph box.
	TArray<FDecorationRun>& Decorations = Scratch.Decorations;
	Decorations.Reset();
	for (int32 Kind = 0; Kind < 2 && !bStrokesPerCharacter; Kind++)
	{
		const bool bStrikethrough = Kind == 1;
		FDecorationRun Open;
		bool bOpen = false;
		auto Flush = [&Decorations, &Open, &bOpen]()
		{
			if (bOpen && Open.Right > Open.Left)
			{
				Decorations.Add(Open);
			}
			bOpen = false;
		};
		for (int32 ItemIndex = 0; ItemIndex < EmitItemCount; ItemIndex++)
		{
			const auto& Item = Items[ItemIndex];
			const bool bDecorated = (bStrikethrough ? Item.Style.bStrikethrough : Item.Style.bUnderline)
				&& Item.Kind != EDreamTextItemKind::Image;
			if (!bDecorated)
			{
				Flush();
				continue;
			}
			const FDreamUICharData& Glyph = bStrikethrough ? Item.StrikethroughGlyph : Item.UnderlineGlyph;
			const float Top = Item.Pen.Y + Glyph.YOffset;
			const float EdgeA = Item.Pen.X + Item.DecorationOffset;
			const float EdgeB = EdgeA + Item.AdvanceWithSpace;
			const float DilateEm = DilateOf(Item);
			const FItemPaint& ItemPaint = ItemPaints[ItemIndex];
			const bool bAlone = bSlotStrokesPerCharacter[ItemPaint.Slot];
			const int32 Fragment = bSlotStrokesPerRun[ItemPaint.Slot] ? Item.PaintFragment : INDEX_NONE;
			if (bOpen && !bAlone && !Open.bAlone && Open.LineIndex == Item.LineIndex && FMath::IsNearlyEqual(Open.Top, Top, 0.01f)
				&& FMath::IsNearlyEqual(Open.Height, Glyph.Height, 0.01f) && Open.Color == ItemPaint.SlotColor
				&& Open.Segment == ItemSegment[ItemIndex] && Open.DilateEm == DilateEm && Open.Slot == ItemPaint.Slot
				&& Open.EffectsSlot == ItemPaint.EffectsSlot && Open.Fragment == Fragment)
			{
				Open.Left = FMath::Min(Open.Left, FMath::Min(EdgeA, EdgeB));
				Open.Right = FMath::Max(Open.Right, FMath::Max(EdgeA, EdgeB));
				continue;
			}
			Flush();
			Open = FDecorationRun();
			Open.FirstItem = ItemIndex;
			Open.LineIndex = Item.LineIndex;
			Open.Left = FMath::Min(EdgeA, EdgeB);
			Open.Right = FMath::Max(EdgeA, EdgeB);
			Open.Top = Top;
			Open.Height = Glyph.Height;
			Open.Glyph = Glyph;
			Open.Color = ItemPaint.SlotColor;
			Open.Segment = ItemSegment[ItemIndex];
			Open.DilateEm = DilateEm;
			Open.Slot = ItemPaint.Slot;
			Open.EffectsSlot = ItemPaint.EffectsSlot;
			Open.Fragment = Fragment;
			Open.bAlone = bAlone;
			bOpen = true;
		}
		Flush();
	}
	// A strip is a plain quad in every copy; past the limit the last strips are left out.
	const int32 RoomForStrips = (MaxQuads - Blocks.Total()) / PlainCopies.Total();
	if (Decorations.Num() > RoomForStrips)
	{
		Decorations.SetNum(FMath::Max(0, RoomForStrips), EAllowShrinking::No);
	}
	Blocks.Add(PlainCopies, Decorations.Num());

	// Size once. The geometry helper zeroes only memory it has not handed out before, which is the
	// convention the rest of the plugin relies on for the channels nobody writes (UV1.x, tangents).
	const int32 TotalQuads = Blocks.Total();
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.OriginVertices, TotalQuads * 4, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Vertices, TotalQuads * 4, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Triangles, TotalQuads * 6, false);

	FQuadWriter Writer(OutGeometry);
	// The index buffer's blocks, back to front: shadows, outline taps, effects, faces.
	int32 ShadowIndexCursor = 0;
	int32 OutlineIndexCursor = Blocks.Shadow * 6;
	int32 EffectIndexCursor = (Blocks.Shadow + Blocks.Outline) * 6;
	int32 FaceIndexCursor = (Blocks.Shadow + Blocks.Outline + Blocks.Effects) * 6;

	// The boxes a slot's paints are measured in, for one item (FDreamTextPaintSlot, the display list's boxes): the text
	// block, the content box, the item's line, its own glyph box (its pen box across, its ascent to its descent down), or its
	// run -- a tag run's piece on the item's line, the run's pieces laid end to end; the whole text as one run for the text's
	// own paints. A box the display list does not have is no box: 0 on that axis.
	auto LineBoxOf = [&DisplayList](const FDreamTextGlyphItem& BoxItem) -> const FDreamTextBox*
	{
		return DisplayList.LineBoxes.IsValidIndex(BoxItem.LineIndex) ? &DisplayList.LineBoxes[BoxItem.LineIndex] : nullptr;
	};
	auto RunPieceOf = [&DisplayList, &LineRunPieces](int32 InSlot, const FDreamTextGlyphItem& BoxItem) -> const FDreamTextPaintFragment*
	{
		if (InSlot >= DreamTextQuadCode::FirstTagSlot && DisplayList.PaintFragments.IsValidIndex(BoxItem.PaintFragment))
		{
			return &DisplayList.PaintFragments[BoxItem.PaintFragment];
		}
		return LineRunPieces.IsValidIndex(BoxItem.LineIndex) ? &LineRunPieces[BoxItem.LineIndex] : nullptr;
	};
	auto MakePaintBoxes = [&](int32 InSlot, int32 InBoxItem)
	{
		const FDreamTextPaintSlot& PaintSlot = Paints.Slots[InSlot];
		const FDreamTextGlyphItem& BoxItem = Items[InBoxItem];
		FPaintBoxes Boxes;
		switch (PaintSlot.HorizontalBox)
		{
		case EDreamTextPaintBox::ContentBox:
		{
			Boxes.Left = DisplayList.ContentBox.Left;
			Boxes.Width = DisplayList.ContentBox.GetWidth();
			break;
		}
		case EDreamTextPaintBox::Line:
		{
			if (const FDreamTextBox* LineBox = LineBoxOf(BoxItem))
			{
				Boxes.Left = LineBox->Left;
				Boxes.Width = LineBox->GetWidth();
			}
			break;
		}
		case EDreamTextPaintBox::Glyph:
		{
			const float PenEdge = BoxItem.Pen.X + BoxItem.DecorationOffset;
			Boxes.Left = FMath::Min(PenEdge, PenEdge + BoxItem.AdvanceWithSpace);
			Boxes.Width = FMath::Abs(BoxItem.AdvanceWithSpace);
			break;
		}
		case EDreamTextPaintBox::Run:
		{
			if (const FDreamTextPaintFragment* Piece = RunPieceOf(InSlot, BoxItem))
			{
				Boxes.Left = Piece->Box.Left;
				Boxes.Offset = Piece->RunOffset;
				Boxes.Width = Piece->RunWidth;
			}
			break;
		}
		default:
		{
			Boxes.Left = DisplayList.TextBlockBox.Left;
			Boxes.Width = DisplayList.TextBlockBox.GetWidth();
			break;
		}
		}
		switch (PaintSlot.VerticalBox)
		{
		case EDreamTextPaintBox::ContentBox:
		{
			Boxes.Top = DisplayList.ContentBox.Top;
			Boxes.Height = DisplayList.ContentBox.GetHeight();
			break;
		}
		case EDreamTextPaintBox::Line:
		{
			if (const FDreamTextBox* LineBox = LineBoxOf(BoxItem))
			{
				Boxes.Top = LineBox->Top;
				Boxes.Height = LineBox->GetHeight();
			}
			break;
		}
		case EDreamTextPaintBox::Glyph:
		{
			Boxes.Top = BoxItem.Pen.Y + BoxItem.Ascent;
			Boxes.Height = BoxItem.Ascent + BoxItem.Descent;
			break;
		}
		case EDreamTextPaintBox::Run:
		{
			if (const FDreamTextPaintFragment* Piece = RunPieceOf(InSlot, BoxItem))
			{
				Boxes.Top = Piece->Box.Top;
				Boxes.Height = Piece->Box.GetHeight();
			}
			break;
		}
		default:
		{
			Boxes.Top = DisplayList.TextBlockBox.Top;
			Boxes.Height = DisplayList.TextBlockBox.GetHeight();
			break;
		}
		}
		return Boxes;
	};

	// The quad just written at Start, its corners final -- sheared, snapped -- given its slot's paints, measured for the item
	// at BoxItem. Slot 0 leaves it as written: UV4 (0, 0), its code as it is. Otherwise each vertex's UV4 is its place in the
	// slot's boxes and the code carries the slot (DreamTextQuadCode::AddSlot) -- or, under the vertex-colour fallback, a
	// face takes the gradient into its vertex colours there, the shader's product of the two in linear light, and stays slot
	// 0; nothing else is painted then.
	auto PaintQuad = [&](int32 Start, int32 InSlot, int32 InBoxItem, bool bInFace)
	{
		if (InSlot == 0)return;
		const FDreamTextPaintSlot& PaintSlot = Paints.Slots[InSlot];
		if (Paints.bVertexColorFallback && (!bInFace || PaintSlot.Face == nullptr))return;
		const FPaintBoxes Boxes = MakePaintBoxes(InSlot, InBoxItem);
		for (int32 Corner = Start; Corner < Start + 4; Corner++)
		{
			const FVector3f& Position = Writer.OriginVertices[Corner].Position;
			const FVector2f BoxUV = Boxes.At(Position.Y, Position.Z);
			FDreamUIMeshVertex& Vertex = Writer.Vertices[Corner];
			if (Paints.bVertexColorFallback)
			{
				const FLinearColor Gradient = PaintSlot.Face->Evaluate(BoxUV, PaintSlot.BoxAspect, Paints.FaceAnimation);
				Vertex.Color = (FLinearColor(Vertex.Color) * Gradient).ToFColor(true);
			}
			else
			{
				Vertex.UV4 = BoxUV;
				Vertex.TextureCoordinate[2].X = DreamTextQuadCode::AddSlot(Vertex.TextureCoordinate[2].X, InSlot);
			}
		}
	};

	// One copy of a quad: grown into the field by ReachEm unless it is a solid strip -- a stroke drawn from one texel inside
	// its glyph has no field around it to grow into; grown, it would only get thicker -- moved by Offset, raised by Lift,
	// and, when italic, sheared about its baseline moved by Offset but not raised. Returns where its vertices start.
	auto WriteCopy = [&](const FQuadSource& Source, EGlyphLayer Layer, float ReachEm, const FVector2f& Offset, float Lift,
		const FColor& CopyColor, const FQuadWriter::FFill& Fill, int32& IndexCursor)
	{
		const FDreamUICharData& SourceGlyph = *Source.Glyph;
		const bool bSolidStrip = SourceGlyph.MinUV == SourceGlyph.MaxUV;
		const FDreamUICharData Grown = bSolidStrip ? SourceGlyph : GrowIntoField(SourceGlyph, ReachEm, Source.Em, Params);
		const float Grow = Grown.XOffset - SourceGlyph.XOffset;// negative or zero: how far each edge moved out
		const int32 Start = Writer.VertexCursor;
		// Fonts without a field leave UV2.x at zero.
		const float UV2X = Params.bDistanceField
			? Source.DilateEm + DreamTextQuadCode::FieldLayerStride * static_cast<float>(static_cast<int32>(Layer)) : 0.0f;
		const float CopyLeft = Source.Left + Grow + Offset.X;
		const float CopyRight = Source.Right - Grow + Offset.X;
		const float CopyBottom = Source.Bottom + Grow + Offset.Y + Lift;
		const float CopyTop = Source.Top - Grow + Offset.Y + Lift;
		Writer.WriteQuad(CopyLeft, CopyRight, CopyBottom, CopyTop, Grown, CopyColor, UV2X, Fill.GlowBoost, Fill, IndexCursor);
		if (Source.bItalic)
		{
			// Shear about the baseline: an edge moves right by its height above the baseline times the slope.
			const float CopyBaseline = Source.BaselineY + Offset.Y;
			Writer.ShearQuad(Start, (CopyBaseline - CopyBottom) * Params.ItalicSlope, (CopyTop - CopyBaseline) * Params.ItalicSlope);
		}
		return Start;
	};

	// Every copy of one quad. They share the item's fill channels, measured on the quad as the layout placed it, and the
	// effect copy of a field may be grown further into the field than the face copy. The face copy is in Color and paints
	// with FaceSlot. The effects fade with EffectOpacity instead of the item's alpha: a field's effects copy paints with
	// EffectsSlot, a bitmap font's shadow and outline copies -- in their own colours, at their own alpha times that -- with
	// nothing.
	auto WriteCopies = [&](const FQuadSource& Source, const FColor& Color, int32 FaceSlot, int32 EffectsSlot)
	{
		const FQuadWriter::FFill Fill = MakeFill(Source.ItemIndex, Source.Left, Source.Right);
		if (bBitmapShadow)
		{
			FColor ShadowColor = Params.BitmapShadowColor;
			ShadowColor.A = FadeEffectAlpha(ShadowColor.A);
			// +Y is down in FDreamTextStyle's offset, and up in the text's own space.
			const FVector2f ShadowOffset(Params.BitmapShadowOffsetEm.X * Source.Em, -Params.BitmapShadowOffsetEm.Y * Source.Em);
			WriteCopy(Source, EGlyphLayer::Both, 0.0f, ShadowOffset, 0.0f, ShadowColor, Fill, ShadowIndexCursor);
		}
		if (bBitmapOutline)
		{
			FColor OutlineColor = Params.BitmapOutlineColor;
			OutlineColor.A = FadeEffectAlpha(OutlineColor.A);
			const float OutlineWidth = Params.BitmapOutlineWidthEm * Source.Em;
			for (int32 Tap = 0; Tap < UE_ARRAY_COUNT(OutlineTaps); Tap++)
			{
				WriteCopy(Source, EGlyphLayer::Both, 0.0f, OutlineTaps[Tap] * OutlineWidth, 0.0f, OutlineColor, Fill, OutlineIndexCursor);
			}
		}
		const float BothReachEm = FMath::Max(Params.EffectReachEm, Params.FaceReachEm);
		if (bSeparateEffectLayer)
		{
			FColor EffectsColor = Color;
			EffectsColor.A = EffectAlpha;
			const int32 EffectsStart = WriteCopy(Source, EGlyphLayer::Effects, BothReachEm, FVector2f::ZeroVector, 0.0f, EffectsColor, Fill, EffectIndexCursor);
			PaintQuad(EffectsStart, EffectsSlot, Source.ItemIndex, false);
			const int32 FaceStart = WriteCopy(Source, EGlyphLayer::Face, Params.FaceReachEm, FVector2f::ZeroVector, 0.0f, Color, Fill, FaceIndexCursor);
			PaintQuad(FaceStart, FaceSlot, Source.ItemIndex, true);
		}
		else
		{
			const int32 FaceStart = WriteCopy(Source, EGlyphLayer::Both, BothReachEm, FVector2f::ZeroVector, 0.0f, Color, Fill, FaceIndexCursor);
			PaintQuad(FaceStart, FaceSlot, Source.ItemIndex, true);
		}
	};

	// An item's glyph quad as the layout placed it. Shader-side bold dilates the regular glyph by BoldDilateEm per side, in
	// the glyph's own em. The layout already gave the cluster twice that much extra advance; shifting the quad right by one
	// side's worth keeps the left bearing where it was and spends the whole extra advance on the right.
	auto MakeGlyphSource = [&](int32 ItemIndex)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		FQuadSource Source;
		Source.ItemIndex = ItemIndex;
		Source.Glyph = &Item.Glyph;
		Source.Em = GlyphEmOf(Item);
		Source.DilateEm = DilateOf(Item);
		Source.Left = Item.Pen.X + Item.Glyph.XOffset + Source.DilateEm * Source.Em;
		Source.Right = Source.Left + Item.Glyph.Width;
		Source.Top = Item.Pen.Y + Item.Glyph.YOffset;
		Source.Bottom = Source.Top - Item.Glyph.Height;
		Source.bItalic = Item.Style.bSyntheticItalic;
		Source.BaselineY = Item.Pen.Y;
		return Source;
	};

	// A coverage glyph: one face quad on the device grid, in the item's colour or its paint's. Bold and italic are in its
	// raster, so it is neither shifted nor sheared, and it is measured in its paint's boxes at its snapped corners. UV3.y
	// carries the contrast and whether the target blends in linear space.
	auto WriteCoverageQuad = [&](int32 ItemIndex, const FCoverageQuad& Quad)
	{
		const FItemPaint& CoveragePaint = ItemPaints[ItemIndex];
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Quad.Left, Quad.Right);
		const int32 Start = Writer.VertexCursor;
		Writer.WriteQuad(Quad.Left, Quad.Right, Quad.Bottom, Quad.Top, Quad.Texels, CoveragePaint.SlotColor,
			DreamTextQuadCode::CoverageBase + static_cast<float>(Quad.Phase), CoverageCodeY, Fill, FaceIndexCursor);
		PaintQuad(Start, CoveragePaint.FaceSlot, ItemIndex, true);
	};

	// The effects hybrid (FDreamTextCoverageParams::EffectFace): a coverage item's effects from the field, in the effects block
	// under its coverage face -- the layout's field quad, grown and dilated as a field item's effects copy is, moved by the
	// face's snap shift so the two line up; for synthetic bold also raised by half the bold, since FreeType grows a bold
	// coverage glyph right and up where the field dilates it both ways (the right half is the field's bold shift already).
	// The raise comes before the italic shear, as the bold does in the raster.
	auto WriteCoverageEffects = [&](int32 ItemIndex, const FCoverageQuad& Quad)
	{
		const FQuadSource Source = MakeGlyphSource(ItemIndex);
		const FItemPaint& EffectsPaint = ItemPaints[ItemIndex];
		FColor EffectsColor = EffectsPaint.SlotColor;
		EffectsColor.A = EffectAlpha;
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Source.Left + Quad.Shift.X, Source.Right + Quad.Shift.X);
		const int32 Start = WriteCopy(Source, EGlyphLayer::Effects, FMath::Max(Params.EffectReachEm, Params.FaceReachEm), Quad.Shift,
			Source.DilateEm * Source.Em, EffectsColor, Fill, EffectIndexCursor);
		PaintQuad(Start, EffectsPaint.EffectsSlot, ItemIndex, false);
	};

	// The shader draws a colour glyph's underlay from its alpha sampled at UV minus the style's offset, and keeps that offset
	// and the softness taps within half the field's spread, less a texel; the cell is padded by the whole spread
	// (FDreamGlyphColor). Inset by half the spread, the effects copy never samples past its own cell into a neighbour's.
	const float ColorUnderlayInsetEm = (Params.bDistanceField && Params.EmTexels > 0.0f) ? 0.5f * Params.FieldSpreadTexels / Params.EmTexels : 0.0f;
	// A colour glyph: the padded cell the layout placed, sampled as a colour bitmap -- not grown into a field it has not
	// got, not dilated or shifted for bold, but sheared for italic like any glyph. Its vertex RGB is white (an emoji keeps
	// its own colours; the shader ignores it) and its alpha the item's, so a fade or an alpha animation still reaches it;
	// its underlay copy fades as every effect does. It paints nothing: every copy is slot 0. UV3.y is the cell's texels per
	// em, which the shader turns the underlay's offset in em into UV with.
	auto WriteColorCopies = [&](int32 ItemIndex)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		const FDreamUICharData& Glyph = Item.Glyph;
		const float Left = Item.Pen.X + Glyph.XOffset;
		const float Right = Left + Glyph.Width;
		const float Top = Item.Pen.Y + Glyph.YOffset;
		const float Bottom = Top - Glyph.Height;
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Left, Right);
		const FColor White(255, 255, 255, ItemPaints[ItemIndex].Color.A);
		const FColor EffectsWhite(255, 255, 255, EffectAlpha);
		auto WriteOne = [&](const FDreamUICharData& CopyGlyph, float CopyLeft, float CopyRight, float CopyBottom, float CopyTop,
			float CopyBaseline, float Code, const FColor& CopyColor, const FQuadWriter::FFill& CopyFill, int32& IndexCursor)
		{
			const int32 Start = Writer.VertexCursor;
			Writer.WriteQuad(CopyLeft, CopyRight, CopyBottom, CopyTop, CopyGlyph, CopyColor, Code, Glyph.ColorTexelsPerEm, CopyFill, IndexCursor);
			if (Item.Style.bSyntheticItalic)
			{
				Writer.ShearQuad(Start, (CopyBaseline - CopyBottom) * Params.ItalicSlope, (CopyTop - CopyBaseline) * Params.ItalicSlope);
			}
		};
		if (ColorCopies.Shadow > 0)
		{
			// A bitmap font's drop shadow, moved by its offset as its plain glyphs' shadows are: the shader draws the glyph's
			// alpha where the copy is, in the style's underlay colour. +Y is down in the offset, up in the text's space.
			const float Em = GlyphEmOf(Item);
			const FVector2f Offset(Params.BitmapShadowOffsetEm.X * Em, -Params.BitmapShadowOffsetEm.Y * Em);
			WriteOne(Glyph, Left + Offset.X, Right + Offset.X, Bottom + Offset.Y, Top + Offset.Y, Item.Pen.Y + Offset.Y,
				DreamTextQuadCode::ColorEffects, EffectsWhite, Fill, ShadowIndexCursor);
		}
		if (ColorCopies.Effects > 0)
		{
			// A field font's underlay, which the shader moves by the style's offset. Its fill is measured on its own edges,
			// so the run's lit edge falls where the face's does.
			FDreamUICharData Inset = Glyph;
			float InsetX = 0.0f;
			float InsetY = 0.0f;
			const float InsetTexels = ColorUnderlayInsetEm * Glyph.ColorTexelsPerEm;
			const float CellTexelsX = Params.TexelToUV > 0.0f ? (Glyph.MaxUV.X - Glyph.MinUV.X) / Params.TexelToUV : 0.0f;
			const float CellTexelsY = Params.TexelToUV > 0.0f ? (Glyph.MaxUV.Y - Glyph.MinUV.Y) / Params.TexelToUV : 0.0f;
			if (InsetTexels > 0.0f && CellTexelsX > 2.0f * InsetTexels && CellTexelsY > 2.0f * InsetTexels)
			{
				InsetX = Glyph.Width * InsetTexels / CellTexelsX;
				InsetY = Glyph.Height * InsetTexels / CellTexelsY;
				const float InsetUV = InsetTexels * Params.TexelToUV;
				Inset.MinUV += FVector2f(InsetUV, InsetUV);
				Inset.MaxUV -= FVector2f(InsetUV, InsetUV);
			}
			WriteOne(Inset, Left + InsetX, Right - InsetX, Bottom + InsetY, Top - InsetY, Item.Pen.Y,
				DreamTextQuadCode::ColorEffects, EffectsWhite, MakeFill(ItemIndex, Left + InsetX, Right - InsetX), EffectIndexCursor);
		}
		WriteOne(Glyph, Left, Right, Bottom, Top, Item.Pen.Y, DreamTextQuadCode::ColorFace, White, Fill, FaceIndexCursor);
	};

	// While coverage is on, a stroke's top and bottom go to device rows, as the glyphs' baselines do: its thickness rounded
	// to whole rows, at least one, about its own centre, so strokes of one thickness stay that thickness wherever they fall.
	auto SnapStroke = [&Coverage](float& Bottom, float& Top)
	{
		const double Scale = Coverage.DeviceScale;
		const double V0 = Scale * Bottom + Coverage.SnapOrigin.Y;
		const double V1 = Scale * Top + Coverage.SnapOrigin.Y;
		const int32 Rows = FMath::Max(1, FMath::RoundToInt32(V1 - V0));
		const int32 FirstRow = FMath::FloorToInt32(0.5 * (V0 + V1) - 0.5 * Rows + 0.5);
		Bottom = (float)(((double)FirstRow - Coverage.SnapOrigin.Y) / Scale);
		Top = (float)(((double)(FirstRow + Rows) - Coverage.SnapOrigin.Y) / Scale);
	};

	FDreamTextCoverageReport Report;
	for (int32 ItemIndex = 0; ItemIndex < EmitItemCount; ItemIndex++)
	{
		const auto& Item = Items[ItemIndex];
		// An item that counts but does not emit is a character whose glyph has not landed yet: it takes
		// its place in OutCharProperties with an empty vertex range so the numbering does not move when
		// the rasterizer's worker finishes -- unless a coverage glyph draws it meanwhile.
		if (!Item.bEmit && !Item.bCountsAsVisible)continue;

		const int32 StartVertIndex = Writer.VertexCursor;
		const int32 StartFaceIndex = FaceIndexCursor;
		const EItemQuad ItemQuad = ItemQuads[ItemIndex];
		const FItemPaint& ItemPaint = ItemPaints[ItemIndex];
		if (ItemQuad == EItemQuad::CoveragePending)
		{
			Report.PendingItems++;
		}

		if (DrawsQuad(ItemIndex))
		{
			const float DilateEm = DilateOf(Item);
			if (ItemQuad == EItemQuad::Coverage)
			{
				const FCoverageQuad& CoverageQuad = CoverageQuads[ItemCoverageQuad[ItemIndex]];
				// Under the effects hybrid the field's effects go first -- unless the coverage glyph stands in for a field
				// glyph still pending, which has no field quad to draw them from yet.
				if (bSeparateEffectLayer && Item.bEmit)
				{
					WriteCoverageEffects(ItemIndex, CoverageQuad);
				}
				WriteCoverageQuad(ItemIndex, CoverageQuad);
				Report.CoverageItems++;
			}
			else if (ItemQuad == EItemQuad::Color)
			{
				WriteColorCopies(ItemIndex);
			}
			else
			{
				WriteCopies(MakeGlyphSource(ItemIndex), ItemPaint.SlotColor, ItemPaint.FaceSlot, ItemPaint.EffectsSlot);
			}
			// Strokes drawn per character: this glyph's own piece of each, under its pen box, right after it in the
			// vertex buffer so that whatever moves or fades the character takes its strokes along. What comes right
			// after it on its line with the stroke but no piece of its own -- a space, an emoji, a glyph still on the
			// rasterizer -- is under its piece, so a still text shows the same unbroken stroke as one drawn in runs.
			// A piece paints as its glyph's strokes do, a colour glyph's too.
			for (int32 Kind = 0; Kind < 2 && CharacterStrokes(ItemIndex) > 0; Kind++)
			{
				const bool bStrikethrough = Kind == 1;
				auto DrawsStroke = [bStrikethrough](const FDreamTextGlyphItem& Other)
				{
					return bStrikethrough ? Other.Style.bStrikethrough : Other.Style.bUnderline;
				};
				if (!DrawsStroke(Item))continue;
				const FDreamUICharData& StrokeGlyph = bStrikethrough ? Item.StrikethroughGlyph : Item.UnderlineGlyph;
				float StrokeTop = Item.Pen.Y + StrokeGlyph.YOffset;
				float StrokeBottom = StrokeTop - StrokeGlyph.Height;
				const float EdgeA = Item.Pen.X + Item.DecorationOffset;
				const float EdgeB = EdgeA + Item.AdvanceWithSpace;
				const float StrokeLeft = FMath::Min(EdgeA, EdgeB);
				float StrokeRight = FMath::Max(EdgeA, EdgeB);
				for (int32 NextIndex = ItemIndex + 1; NextIndex < EmitItemCount; NextIndex++)
				{
					const FDreamTextGlyphItem& Next = Items[NextIndex];
					if (Next.LineIndex != Item.LineIndex || !DrawsStroke(Next) || CharacterStrokes(NextIndex) > 0)break;
					const float NextEdge = Next.Pen.X + Next.DecorationOffset;
					StrokeRight = FMath::Max(StrokeRight, FMath::Max(NextEdge, NextEdge + Next.AdvanceWithSpace));
				}
				if (bCoverage)
				{
					SnapStroke(StrokeBottom, StrokeTop);
				}
				FQuadSource Stroke;
				Stroke.ItemIndex = ItemIndex;
				Stroke.Glyph = &StrokeGlyph;
				Stroke.Em = Item.Style.Size;
				Stroke.Left = StrokeLeft;
				Stroke.Right = StrokeRight;
				Stroke.Bottom = StrokeBottom;
				Stroke.Top = StrokeTop;
				Stroke.DilateEm = DilateEm;
				Stroke.BaselineY = Item.Pen.Y;
				WriteCopies(Stroke, ItemPaint.SlotColor, ItemPaint.Slot, ItemPaint.EffectsSlot);
			}
		}

		if (Item.bCountsAsVisible)
		{
			// A character is one entry however many glyphs the shaper gave it; its glyphs are
			// contiguous, so the entry just grows while the element index repeats. The vertex range
			// covers every copy of its quads; the triangle range is the face block's.
			if (OutCharProperties.Num() > 0 && OutCharProperties.Last().CharIndex == Item.ElementIndex
				&& OutCharProperties.Last().StartVertIndex + OutCharProperties.Last().VertCount == StartVertIndex)
			{
				FDreamUITextCharProperty& Last = OutCharProperties.Last();
				Last.VertCount = Writer.VertexCursor - Last.StartVertIndex;
				Last.IndicesCount = FaceIndexCursor - Last.StartTriangleIndex;
			}
			else
			{
				FDreamUITextCharProperty CharProperty;
				CharProperty.CharIndex = Item.ElementIndex;
				CharProperty.StartVertIndex = StartVertIndex;
				CharProperty.VertCount = Writer.VertexCursor - StartVertIndex;
				CharProperty.StartTriangleIndex = StartFaceIndex;
				CharProperty.IndicesCount = FaceIndexCursor - StartFaceIndex;
				OutCharProperties.Add(CharProperty);
			}
		}
	}

	// The strips come after every glyph and belong to no character: a stroke spans many of them.
	for (const FDecorationRun& Run : Decorations)
	{
		FQuadSource Strip;
		Strip.ItemIndex = Run.FirstItem;
		Strip.Glyph = &Run.Glyph;
		Strip.Em = Items[Run.FirstItem].Style.Size;
		Strip.Left = Run.Left;
		Strip.Right = Run.Right;
		Strip.Bottom = Run.Top - Run.Height;
		Strip.Top = Run.Top;
		if (bCoverage)
		{
			SnapStroke(Strip.Bottom, Strip.Top);
		}
		Strip.DilateEm = Run.DilateEm;
		Strip.BaselineY = Run.Top;
		WriteCopies(Strip, Run.Color, Run.Slot, Run.EffectsSlot);
	}

	if (Coverage.Report != nullptr)
	{
		*Coverage.Report = Report;
	}
	if (Report.CoverageItems > 0)
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::CoverageItemsDrawn, Report.CoverageItems);
	}

	if (Params.bRequireNormalAndTangent)
	{
		for (auto& Vertex : OutGeometry.OriginVertices)
		{
			Vertex.Normal = FVector3f(-1, 0, 0);
			Vertex.Tangent = FVector3f(0, 1, 0);
		}
	}
}
