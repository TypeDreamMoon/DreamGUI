// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Utils/DreamUIUtils.h"
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

		/** UV2.x is the quad's code (DreamTextQuadCode), UV3.y what that kind of quad keeps there: see DreamTextQuadCode. */
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
	 * style's pending Multiply multiplies whatever that gives, and a tag colour override replaces it outright.
	 */
	static FColor ResolveItemColor(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params, const FDreamTextGlyphItem& Item)
	{
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
				}
			}
		}
		return Color;
	}

	/** One underline or strikethrough strip: a run of items on one line that draw the same stroke in the same colour. */
	struct FDecorationRun
	{
		/** The run's first item, which its fill channels and size are read from. */
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
	};
}

void FDreamTextPainter::Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
	FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties)
{
	using namespace DreamTextPainterLocal;

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
	// An index is a vertex ordinal, and FDreamUIMeshIndex is 16 bits wide unless the 32-bit buffer is
	// compiled in: past the limit every index wraps and the whole block draws as garbage. Stop adding
	// quads at the limit instead.
	const int32 MaxQuads = LEXUI_MAX_VERTEX_COUNT / 4;

	TArray<FColor> ItemColors;
	ItemColors.SetNumUninitialized(Items.Num());
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ItemIndex++)
	{
		ItemColors[ItemIndex] = ResolveItemColor(DisplayList, Params, Items[ItemIndex]);
	}

	// Small text from coverage glyphs (FDreamTextCoverageParams). A coverage glyph is a face and nothing else, so a text
	// whose quads come in several copies -- effects, a bitmap font's shadow or outline -- keeps its field quads; the text's
	// gate never lets one through.
	const bool bCoverage = Coverage.bEnabled && Coverage.Font != nullptr && Coverage.DeviceScale > 0.0f
		&& Coverage.RasterScale > 0.0f && PlainCopies.Total() == 1;
	const float CoverageCodeY = Coverage.Contrast + (Coverage.bLinearTarget ? DreamTextQuadCode::CoverageLinearTarget : 0.0f);
	TArray<EItemQuad> ItemQuads;
	ItemQuads.SetNumZeroed(Items.Num());
	TArray<FCoverageQuad> CoverageQuads;
	TArray<int32> ItemCoverageQuad;
	if (bCoverage)
	{
		ItemCoverageQuad.Init(INDEX_NONE, Items.Num());
	}

	// The coverage glyph on the device grid: the pen's baseline row rounded, its column and quarter-pixel phase from the
	// pen's x rounded to a quarter, the box from there in whole pixels, mapped back by 1/S. The UVs are the glyph's texels
	// exactly, so with the box on whole pixels every pixel centre samples one texel centre.
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
		return Quad;
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
		ItemCopies.Add(ItemQuads[ItemIndex] == EItemQuad::Color ? ColorCopies : PlainCopies, 1);
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
	struct FRunBounds { float MinX = FLT_MAX; float MaxX = -FLT_MAX; };
	const int32 SegmentCount = Params.FillSegments ? Params.FillSegments->Num() : 0;
	TArray<FRunBounds> SegmentBounds;
	SegmentBounds.SetNum(SegmentCount);
	TArray<FRunBounds> LineBounds;
	LineBounds.SetNum(DisplayList.Lines.Num());
	TArray<int32> ItemSegment;
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

	// Underlines and strikethroughs: one strip per run of items on a line that draw the same stroke in the same
	// colour, spaces included -- the layout clears the flag on the spaces that hang off a line's end and on anything a
	// clamp removed. A strip per glyph left a gap at every space and, under negative letter spacing, quads of negative
	// width that blended twice where they overlapped. Drawn per character instead, the strokes are in the glyph loop.
	TArray<FDecorationRun> Decorations;
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
			if (bOpen && Open.LineIndex == Item.LineIndex && FMath::IsNearlyEqual(Open.Top, Top, 0.01f)
				&& FMath::IsNearlyEqual(Open.Height, Glyph.Height, 0.01f) && Open.Color == ItemColors[ItemIndex]
				&& Open.Segment == ItemSegment[ItemIndex] && Open.DilateEm == DilateEm)
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
			Open.Color = ItemColors[ItemIndex];
			Open.Segment = ItemSegment[ItemIndex];
			Open.DilateEm = DilateEm;
			bOpen = true;
		}
		Flush();
	}
	// A strip is a plain quad in every copy; past the limit the last strips are left out.
	const int32 RoomForStrips = (MaxQuads - Blocks.Total()) / PlainCopies.Total();
	if (Decorations.Num() > RoomForStrips)
	{
		Decorations.SetNum(FMath::Max(0, RoomForStrips));
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

	// Every copy of one quad. They share the item's fill channels and its alpha, so a faded text fades whole; the
	// effect copy of a field may be grown further into the field than the face copy. A solid strip -- a stroke drawn
	// from one texel inside its glyph -- has no field around it to grow into: grown, it would only get thicker. Em is
	// what the quad's lengths in em are of: the glyph's own (GlyphEmOf), or the style's for a stroke.
	auto WriteCopies = [&](int32 ItemIndex, const FDreamUICharData& Glyph, float Em, float Left, float Right, float Bottom, float Top,
		const FColor& Color, float DilateEm, bool bItalic, float BaselineY)
	{
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Left, Right);
		const bool bSolidStrip = Glyph.MinUV == Glyph.MaxUV;
		auto WriteOne = [&](EGlyphLayer Layer, float ReachEm, const FVector2f& Offset, const FColor& CopyColor, int32& IndexCursor)
		{
			const FDreamUICharData Grown = bSolidStrip ? Glyph : GrowIntoField(Glyph, ReachEm, Em, Params);
			const float Grow = Grown.XOffset - Glyph.XOffset;// negative or zero: how far each edge moved out
			const int32 Start = Writer.VertexCursor;
			// Fonts without a field leave UV2.x at zero.
			const float UV2X = Params.bDistanceField
				? DilateEm + DreamTextQuadCode::FieldLayerStride * static_cast<float>(static_cast<int32>(Layer)) : 0.0f;
			const float CopyLeft = Left + Grow + Offset.X;
			const float CopyRight = Right - Grow + Offset.X;
			const float CopyBottom = Bottom + Grow + Offset.Y;
			const float CopyTop = Top - Grow + Offset.Y;
			Writer.WriteQuad(CopyLeft, CopyRight, CopyBottom, CopyTop, Grown, CopyColor, UV2X, Fill.GlowBoost, Fill, IndexCursor);
			if (bItalic)
			{
				// Shear about the baseline: an edge moves right by its height above the baseline times the slope.
				const float CopyBaseline = BaselineY + Offset.Y;
				Writer.ShearQuad(Start, (CopyBaseline - CopyBottom) * Params.ItalicSlope, (CopyTop - CopyBaseline) * Params.ItalicSlope);
			}
		};
		if (bBitmapShadow)
		{
			FColor ShadowColor = Params.BitmapShadowColor;
			ShadowColor.A = (uint8)FMath::Clamp(FMath::RoundToInt(ShadowColor.A * (Color.A / 255.0f)), 0, 255);
			// +Y is down in FDreamTextStyle's offset, and up in the text's own space.
			WriteOne(EGlyphLayer::Both, 0.0f, FVector2f(Params.BitmapShadowOffsetEm.X * Em, -Params.BitmapShadowOffsetEm.Y * Em), ShadowColor, ShadowIndexCursor);
		}
		if (bBitmapOutline)
		{
			FColor OutlineColor = Params.BitmapOutlineColor;
			OutlineColor.A = (uint8)FMath::Clamp(FMath::RoundToInt(OutlineColor.A * (Color.A / 255.0f)), 0, 255);
			const float Width = Params.BitmapOutlineWidthEm * Em;
			for (int32 Tap = 0; Tap < UE_ARRAY_COUNT(OutlineTaps); Tap++)
			{
				WriteOne(EGlyphLayer::Both, 0.0f, OutlineTaps[Tap] * Width, OutlineColor, OutlineIndexCursor);
			}
		}
		if (bSeparateEffectLayer)
		{
			WriteOne(EGlyphLayer::Effects, FMath::Max(Params.EffectReachEm, Params.FaceReachEm), FVector2f::ZeroVector, Color, EffectIndexCursor);
			WriteOne(EGlyphLayer::Face, Params.FaceReachEm, FVector2f::ZeroVector, Color, FaceIndexCursor);
		}
		else
		{
			WriteOne(EGlyphLayer::Both, FMath::Max(Params.EffectReachEm, Params.FaceReachEm), FVector2f::ZeroVector, Color, FaceIndexCursor);
		}
	};

	// A coverage glyph: one face quad on the device grid, in the item's colour. Bold and italic are in its raster, so it
	// is neither shifted nor sheared. UV3.y carries the contrast and whether the target blends in linear space.
	auto WriteCoverageQuad = [&](int32 ItemIndex, const FCoverageQuad& Quad)
	{
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Quad.Left, Quad.Right);
		Writer.WriteQuad(Quad.Left, Quad.Right, Quad.Bottom, Quad.Top, Quad.Texels, ItemColors[ItemIndex],
			DreamTextQuadCode::CoverageBase + static_cast<float>(Quad.Phase), CoverageCodeY, Fill, FaceIndexCursor);
	};

	// The shader draws a colour glyph's underlay from its alpha sampled at UV minus the style's offset, and keeps that offset
	// and the softness taps within half the field's spread, less a texel; the cell is padded by the whole spread
	// (FDreamGlyphColor). Inset by half the spread, the effects copy never samples past its own cell into a neighbour's.
	const float ColorUnderlayInsetEm = (Params.bDistanceField && Params.EmTexels > 0.0f) ? 0.5f * Params.FieldSpreadTexels / Params.EmTexels : 0.0f;
	// A colour glyph: the padded cell the layout placed, sampled as a colour bitmap -- not grown into a field it has not
	// got, not dilated or shifted for bold, but sheared for italic like any glyph. Its vertex RGB is white (an emoji keeps
	// its own colours; the shader ignores it) and its alpha the item's, so a fade or an alpha animation still reaches it.
	// UV3.y is the cell's texels per em, which the shader turns the underlay's offset in em into UV with.
	auto WriteColorCopies = [&](int32 ItemIndex)
	{
		const FDreamTextGlyphItem& Item = Items[ItemIndex];
		const FDreamUICharData& Glyph = Item.Glyph;
		const float Left = Item.Pen.X + Glyph.XOffset;
		const float Right = Left + Glyph.Width;
		const float Top = Item.Pen.Y + Glyph.YOffset;
		const float Bottom = Top - Glyph.Height;
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Left, Right);
		const FColor White(255, 255, 255, ItemColors[ItemIndex].A);
		auto WriteOne = [&](const FDreamUICharData& CopyGlyph, float CopyLeft, float CopyRight, float CopyBottom, float CopyTop,
			float CopyBaseline, float Code, const FQuadWriter::FFill& CopyFill, int32& IndexCursor)
		{
			const int32 Start = Writer.VertexCursor;
			Writer.WriteQuad(CopyLeft, CopyRight, CopyBottom, CopyTop, CopyGlyph, White, Code, Glyph.ColorTexelsPerEm, CopyFill, IndexCursor);
			if (Item.Style.bSyntheticItalic)
			{
				Writer.ShearQuad(Start, (CopyBaseline - CopyBottom) * Params.ItalicSlope, (CopyTop - CopyBaseline) * Params.ItalicSlope);
			}
		};
		if (ColorCopies.Shadow > 0)
		{
			// A bitmap font's drop shadow, moved by its offset as its plain glyphs' shadows are, and dimmed with its glyph: the
			// shader draws the glyph's alpha where the copy is, in the style's underlay colour. +Y is down in the offset, up
			// in the text's space.
			const float Em = GlyphEmOf(Item);
			const FVector2f Offset(Params.BitmapShadowOffsetEm.X * Em, -Params.BitmapShadowOffsetEm.Y * Em);
			WriteOne(Glyph, Left + Offset.X, Right + Offset.X, Bottom + Offset.Y, Top + Offset.Y, Item.Pen.Y + Offset.Y,
				DreamTextQuadCode::ColorEffects, Fill, ShadowIndexCursor);
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
				DreamTextQuadCode::ColorEffects, MakeFill(ItemIndex, Left + InsetX, Right - InsetX), EffectIndexCursor);
		}
		WriteOne(Glyph, Left, Right, Bottom, Top, Item.Pen.Y, DreamTextQuadCode::ColorFace, Fill, FaceIndexCursor);
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
		if (ItemQuad == EItemQuad::CoveragePending)
		{
			Report.PendingItems++;
		}

		if (DrawsQuad(ItemIndex))
		{
			const float DilateEm = DilateOf(Item);
			if (ItemQuad == EItemQuad::Coverage)
			{
				WriteCoverageQuad(ItemIndex, CoverageQuads[ItemCoverageQuad[ItemIndex]]);
				Report.CoverageItems++;
			}
			else if (ItemQuad == EItemQuad::Color)
			{
				WriteColorCopies(ItemIndex);
			}
			else
			{
				// Shader-side bold dilates the regular glyph by BoldDilateEm per side, in the glyph's own em. The layout
				// already gave the cluster twice that much extra advance; shifting the quad right by one side's worth keeps
				// the left bearing where it was and spends the whole extra advance on the right.
				const float GlyphEm = GlyphEmOf(Item);
				const float BoldShift = DilateEm * GlyphEm;
				const float OffsetX = Item.Pen.X + Item.Glyph.XOffset + BoldShift;
				const float OffsetY = Item.Pen.Y + Item.Glyph.YOffset;
				WriteCopies(ItemIndex, Item.Glyph, GlyphEm, OffsetX, OffsetX + Item.Glyph.Width, OffsetY - Item.Glyph.Height, OffsetY,
					ItemColors[ItemIndex], DilateEm, Item.Style.bSyntheticItalic, Item.Pen.Y);
			}
			// Strokes drawn per character: this glyph's own piece of each, under its pen box, right after it in the
			// vertex buffer so that whatever moves or fades the character takes its strokes along. What comes right
			// after it on its line with the stroke but no piece of its own -- a space, an emoji, a glyph still on the
			// rasterizer -- is under its piece, so a still text shows the same unbroken stroke as one drawn in runs.
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
				WriteCopies(ItemIndex, StrokeGlyph, Item.Style.Size, StrokeLeft, StrokeRight, StrokeBottom, StrokeTop,
					ItemColors[ItemIndex], DilateEm, false, Item.Pen.Y);
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
		float Bottom = Run.Top - Run.Height;
		float Top = Run.Top;
		if (bCoverage)
		{
			SnapStroke(Bottom, Top);
		}
		WriteCopies(Run.FirstItem, Run.Glyph, Items[Run.FirstItem].Style.Size, Run.Left, Run.Right, Bottom, Top,
			Run.Color, Run.DilateEm, false, Run.Top);
	}

	if (Coverage.Report != nullptr)
	{
		*Coverage.Report = Report;
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
