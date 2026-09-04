// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUITextData.h"
#include "DreamGUI.h"

namespace DreamTextPainterLocal
{
	/** UV2.x of an MTSDF glyph: DilateEm + 16 * Layer, matching DreamUIText_UnpackGlyphChannel. */
	enum class EGlyphLayer : int32 { Face = 0, Effects = 1, Both = 2 };
	static float PackGlyphChannel(EGlyphLayer Layer, float DilateEm)
	{
		return DilateEm + 16.0f * static_cast<float>(static_cast<int32>(Layer));
	}

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
		/** Effect quads index into the first block of the index buffer, face quads into the second. */
		int32 EffectIndexCursor = 0;
		int32 FaceIndexCursor = 0;

		FQuadWriter(FDreamUIGeometry& Geometry)
			: OriginVertices(Geometry.OriginVertices)
			, Vertices(Geometry.Vertices)
			, Triangles(Geometry.Triangles)
		{
		}

		/** Per-quad fill channels: UV2.y sweeps RunX0..RunX1 across the quad, UV3 = (progress, glow boost). */
		struct FFill
		{
			float RunX0 = 0.0f;
			float RunX1 = 1.0f;
			float Progress = 1.0f;
			float GlowBoost = 0.0f;
		};

		void WriteQuad(float Left, float Right, float Bottom, float Top, const FDreamUICharData& Glyph,
			const FColor& Color, float UV2X, const FFill& Fill, int32& IndexCursor)
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
				Vertex.TextureCoordinate[3] = FVector2f(Fill.Progress, Fill.GlowBoost);
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
}

void FDreamTextPainter::Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
	FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties)
{
	using namespace DreamTextPainterLocal;

	OutCharProperties.Reset();

	// Size once. The geometry helper zeroes only memory it has not handed out before, which is the
	// convention the rest of the plugin relies on for the channels nobody writes (UV1.x, tangents).
	// With a separate effect layer every quad is written twice: the effect copy indexes into the first
	// half of the index buffer, the face copy into the second, so all effects draw before any face.
	const bool bSeparateEffectLayer = Params.bDistanceField && Params.bSeparateEffectLayer;
	// An index is a vertex ordinal, and FDreamUIMeshIndex is 16 bits wide unless the 32-bit buffer is
	// compiled in: past the limit every index wraps and the whole block draws as garbage. Stop adding
	// glyphs at the limit instead, keeping each item's quads together so the cursors stay in step.
	const int32 QuadsPerGlyph = bSeparateEffectLayer ? 2 : 1;
	const int32 MaxFaceQuads = (LEXUI_MAX_VERTEX_COUNT / 4) / QuadsPerGlyph;
	int32 FaceQuads = 0;
	int32 EmitItemCount = DisplayList.Items.Num();
	for (int32 ItemIndex = 0; ItemIndex < DisplayList.Items.Num(); ItemIndex++)
	{
		const auto& Item = DisplayList.Items[ItemIndex];
		if (!Item.bEmit)continue;
		int32 ItemQuads = 1;
		if (Item.Style.bUnderline)ItemQuads++;
		if (Item.Style.bStrikethrough)ItemQuads++;
		if (FaceQuads + ItemQuads > MaxFaceQuads)
		{
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
		FaceQuads += ItemQuads;
	}
	const int32 TotalQuads = FaceQuads * QuadsPerGlyph;
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.OriginVertices, TotalQuads * 4, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Vertices, TotalQuads * 4, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Triangles, TotalQuads * 6, false);

	// Fill runs. A glyph belongs to the first segment covering its character, else to its line;
	// each run's horizontal extent comes from the glyph quads in it, so UV2.y spans exactly the ink.
	struct FRunBounds { float MinX = FLT_MAX; float MaxX = -FLT_MAX; };
	const int32 SegmentCount = Params.FillSegments ? Params.FillSegments->Num() : 0;
	TArray<FRunBounds> SegmentBounds;
	SegmentBounds.SetNum(SegmentCount);
	TArray<FRunBounds> LineBounds;
	LineBounds.SetNum(DisplayList.Lines.Num());
	TArray<int32> ItemSegment;
	ItemSegment.SetNumUninitialized(DisplayList.Items.Num());
	for (int32 ItemIndex = 0; ItemIndex < DisplayList.Items.Num(); ItemIndex++)
	{
		const auto& Item = DisplayList.Items[ItemIndex];
		ItemSegment[ItemIndex] = INDEX_NONE;
		if (!Item.bEmit)continue;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; SegmentIndex++)
		{
			const auto& Segment = (*Params.FillSegments)[SegmentIndex];
			if (Item.ElementIndex >= Segment.StartCharIndex && Item.ElementIndex <= Segment.EndCharIndex)
			{
				ItemSegment[ItemIndex] = SegmentIndex;
				break;
			}
		}
		const float GlyphLeft = Item.Pen.X + Item.Glyph.XOffset;
		const float GlyphRight = GlyphLeft + Item.Glyph.Width;
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
		const auto& Item = DisplayList.Items[ItemIndex];
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

	FQuadWriter Writer(OutGeometry);
	Writer.FaceIndexCursor = bSeparateEffectLayer ? FaceQuads * 6 : 0;

	// One glyph quad, as the face copy and, when the effects draw separately, the effect copy first.
	// Both copies share the run channels; the effect copy may be grown further into the field.
	auto WriteGlyphQuads = [&](int32 ItemIndex, const FDreamUICharData& Glyph, float Left, float Right, float Bottom, float Top,
		const FColor& Color, float DilateEm, bool bItalic, float BaselineY, float LegacyUV2X)
	{
		const auto& Item = DisplayList.Items[ItemIndex];
		const FQuadWriter::FFill Fill = MakeFill(ItemIndex, Left, Right);
		const float Size = Item.Style.Size;
		auto WriteOne = [&](EGlyphLayer Layer, float ReachEm, int32& IndexCursor)
		{
			const FDreamUICharData Grown = GrowIntoField(Glyph, ReachEm, Size, Params);
			const float Grow = Grown.XOffset - Glyph.XOffset;// negative or zero: how far each edge moved out
			const int32 Start = Writer.VertexCursor;
			const float UV2X = Params.bDistanceField ? PackGlyphChannel(Layer, DilateEm) : LegacyUV2X;
			Writer.WriteQuad(Left + Grow, Right - Grow, Bottom + Grow, Top - Grow, Grown, Color, UV2X, Fill, IndexCursor);
			if (bItalic)
			{
				// Shear about the baseline: an edge moves right by its height above the baseline times the slope.
				Writer.ShearQuad(Start, (BaselineY - (Bottom + Grow)) * Params.ItalicSlope, ((Top - Grow) - BaselineY) * Params.ItalicSlope);
			}
		};
		if (bSeparateEffectLayer)
		{
			WriteOne(EGlyphLayer::Effects, FMath::Max(Params.EffectReachEm, Params.FaceReachEm), Writer.EffectIndexCursor);
			WriteOne(EGlyphLayer::Face, Params.FaceReachEm, Writer.FaceIndexCursor);
		}
		else
		{
			WriteOne(EGlyphLayer::Both, FMath::Max(Params.EffectReachEm, Params.FaceReachEm), Writer.FaceIndexCursor);
		}
	};

	for (int32 ItemIndex = 0; ItemIndex < EmitItemCount; ItemIndex++)
	{
		const auto& Item = DisplayList.Items[ItemIndex];
		if (!Item.bEmit)continue;

		const int32 StartVertIndex = Writer.VertexCursor;
		const int32 StartFaceIndex = Writer.FaceIndexCursor;

		FVector2f Pen = Item.Pen;
		if (Item.Style.SupOrSub == 1)
		{
			Pen.Y += Item.Style.Size * 0.5f;
		}
		else if (Item.Style.SupOrSub == 2)
		{
			Pen.Y -= Item.Style.Size * 0.5f;
		}
		const FColor Color = Item.Style.bHasColor ? Item.Style.Color : Params.BaseColor;
		// Fonts without a multi-channel field leave UV2.x at zero.
		const float LegacyUV2X = 0.0f;
		// Shader-side bold dilates the regular glyph by BoldDilateEm per side. The layout already gave
		// the glyph twice that much extra advance; shifting the quad right by one side's worth keeps
		// the left bearing where it was and spends the whole extra advance on the right.
		const bool bShaderBold = Item.Style.bBold && Params.bDistanceField && Params.BoldDilateEm > 0.0f;
		const float DilateEm = bShaderBold ? Params.BoldDilateEm : 0.0f;
		const float BoldShift = bShaderBold ? Params.BoldDilateEm * Item.Style.Size : 0.0f;

		//glyph
		{
			const float OffsetX = Pen.X + Item.Glyph.XOffset + BoldShift;
			const float OffsetY = Pen.Y + Item.Glyph.YOffset;
			WriteGlyphQuads(ItemIndex, Item.Glyph, OffsetX, OffsetX + Item.Glyph.Width, OffsetY - Item.Glyph.Height, OffsetY,
				Color, DilateEm, Item.Style.bItalic, Pen.Y, LegacyUV2X);
		}
		//underline
		if (Item.Style.bUnderline)
		{
			const float OffsetX = Pen.X;
			const float OffsetY = Pen.Y + Item.UnderlineGlyph.YOffset;
			WriteGlyphQuads(ItemIndex, Item.UnderlineGlyph, OffsetX, OffsetX + Item.AdvanceWithSpace, OffsetY - Item.UnderlineGlyph.Height, OffsetY,
				Color, DilateEm, false, Pen.Y, LegacyUV2X);
		}
		//strikethrough
		if (Item.Style.bStrikethrough)
		{
			const float OffsetX = Pen.X;
			const float OffsetY = Pen.Y + Item.StrikethroughGlyph.YOffset;
			WriteGlyphQuads(ItemIndex, Item.StrikethroughGlyph, OffsetX, OffsetX + Item.AdvanceWithSpace, OffsetY - Item.StrikethroughGlyph.Height, OffsetY,
				Color, DilateEm, false, Pen.Y, LegacyUV2X);
		}

		if (Item.bCountsAsVisible)
		{
			// A character is one entry however many glyphs the shaper gave it; its glyphs are
			// contiguous, so the entry just grows while the element index repeats. The vertex range
			// covers both layers' quads; the triangle range is the face layer's.
			if (OutCharProperties.Num() > 0 && OutCharProperties.Last().CharIndex == Item.ElementIndex
				&& OutCharProperties.Last().StartVertIndex + OutCharProperties.Last().VertCount == StartVertIndex)
			{
				FDreamUITextCharProperty& Last = OutCharProperties.Last();
				Last.VertCount = Writer.VertexCursor - Last.StartVertIndex;
				Last.IndicesCount = Writer.FaceIndexCursor - Last.StartTriangleIndex;
			}
			else
			{
				FDreamUITextCharProperty CharProperty;
				CharProperty.CharIndex = Item.ElementIndex;
				CharProperty.StartVertIndex = StartVertIndex;
				CharProperty.VertCount = Writer.VertexCursor - StartVertIndex;
				CharProperty.StartTriangleIndex = StartFaceIndex;
				CharProperty.IndicesCount = Writer.FaceIndexCursor - StartFaceIndex;
				OutCharProperties.Add(CharProperty);
			}
		}
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
