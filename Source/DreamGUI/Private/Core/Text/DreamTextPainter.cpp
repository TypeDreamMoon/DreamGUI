// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Text/DreamTextPainter.h"
#include "Core/DreamUIGeometry.h"

namespace DreamTextPainterLocal
{
	/** Writes one axis-aligned quad: positions as (0, x, y) in the text's local plane, 0/3/2 + 0/1/3 winding. */
	struct FQuadWriter
	{
		TArray<FDreamUIOriginVertexData>& OriginVertices;
		TArray<FDreamUIMeshVertex>& Vertices;
		TArray<FDreamUIMeshIndex>& Triangles;
		int32 VertexCursor = 0;
		int32 IndexCursor = 0;

		FQuadWriter(FDreamUIGeometry& Geometry)
			: OriginVertices(Geometry.OriginVertices)
			, Vertices(Geometry.Vertices)
			, Triangles(Geometry.Triangles)
		{
		}

		void WriteQuad(float Left, float Right, float Bottom, float Top, const FDreamUICharData& Glyph,
			const FColor& Color, bool bWriteUV2, float UV2X)
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
				if (bWriteUV2)
				{
					Vertex.TextureCoordinate[2] = FVector2f(UV2X, 0);
				}
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
	};
}

void FDreamTextPainter::Paint(const FDreamTextDisplayList& DisplayList, const FDreamTextPaintParams& Params,
	FDreamUIGeometry& OutGeometry, TArray<FDreamUITextCharProperty>& OutCharProperties)
{
	using namespace DreamTextPainterLocal;

	OutCharProperties.Reset();

	// Size once. The geometry helper zeroes only memory it has not handed out before, which is the
	// convention the rest of the plugin relies on for the channels nobody writes (UV1.x, tangents).
	int32 TotalVertices = 0;
	int32 TotalIndices = 0;
	for (const auto& Item : DisplayList.Items)
	{
		if (!Item.bEmit)continue;
		int32 Quads = 1;
		if (Item.Style.bUnderline)Quads++;
		if (Item.Style.bStrikethrough)Quads++;
		TotalVertices += Quads * 4;
		TotalIndices += Quads * 6;
	}
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.OriginVertices, TotalVertices, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Vertices, TotalVertices, false);
	FDreamUIGeometry::DreamUIGeometrySetArrayNum(OutGeometry.Triangles, TotalIndices, false);

	FQuadWriter Writer(OutGeometry);
	for (const auto& Item : DisplayList.Items)
	{
		if (!Item.bEmit)continue;

		const int32 StartVertIndex = Writer.VertexCursor;
		const int32 StartTriangleIndex = Writer.IndexCursor;

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
		const float UV2X = Item.Style.Size * Params.FontScaleMultiplier;

		//glyph
		{
			const float OffsetX = Pen.X + Item.Glyph.XOffset;
			const float OffsetY = Pen.Y + Item.Glyph.YOffset;
			const int32 Start = Writer.VertexCursor;
			Writer.WriteQuad(OffsetX, OffsetX + Item.Glyph.Width, OffsetY - Item.Glyph.Height, OffsetY,
				Item.Glyph, Color, Params.bWriteFontScaleToUV2, UV2X);
			if (Item.Style.bItalic)
			{
				auto& OriginVertices = OutGeometry.OriginVertices;
				const float Vert01ItalicOffset = (Item.Glyph.Height - Item.Glyph.YOffset) * Params.ItalicSlope;
				OriginVertices[Start].Position.Y -= Vert01ItalicOffset;
				OriginVertices[Start + 1].Position.Y -= Vert01ItalicOffset;
				const float Vert23ItalicOffset = Item.Glyph.YOffset * Params.ItalicSlope;
				OriginVertices[Start + 2].Position.Y += Vert23ItalicOffset;
				OriginVertices[Start + 3].Position.Y += Vert23ItalicOffset;
			}
		}
		//underline
		if (Item.Style.bUnderline)
		{
			const float OffsetX = Pen.X;
			const float OffsetY = Pen.Y + Item.UnderlineGlyph.YOffset;
			Writer.WriteQuad(OffsetX, OffsetX + Item.AdvanceWithSpace, OffsetY - Item.UnderlineGlyph.Height, OffsetY,
				Item.UnderlineGlyph, Color, Params.bWriteFontScaleToUV2, UV2X);
		}
		//strikethrough
		if (Item.Style.bStrikethrough)
		{
			const float OffsetX = Pen.X;
			const float OffsetY = Pen.Y + Item.StrikethroughGlyph.YOffset;
			Writer.WriteQuad(OffsetX, OffsetX + Item.AdvanceWithSpace, OffsetY - Item.StrikethroughGlyph.Height, OffsetY,
				Item.StrikethroughGlyph, Color, Params.bWriteFontScaleToUV2, UV2X);
		}

		if (Item.bCountsAsVisible)
		{
			// A character is one entry however many glyphs the shaper gave it; its glyphs are
			// contiguous, so the entry just grows while the element index repeats.
			if (OutCharProperties.Num() > 0 && OutCharProperties.Last().CharIndex == Item.ElementIndex
				&& OutCharProperties.Last().StartVertIndex + OutCharProperties.Last().VertCount == StartVertIndex)
			{
				FDreamUITextCharProperty& Last = OutCharProperties.Last();
				Last.VertCount = Writer.VertexCursor - Last.StartVertIndex;
				Last.IndicesCount = Writer.IndexCursor - Last.StartTriangleIndex;
			}
			else
			{
				FDreamUITextCharProperty CharProperty;
				CharProperty.CharIndex = Item.ElementIndex;
				CharProperty.StartVertIndex = StartVertIndex;
				CharProperty.VertCount = Writer.VertexCursor - StartVertIndex;
				CharProperty.StartTriangleIndex = StartTriangleIndex;
				CharProperty.IndicesCount = Writer.IndexCursor - StartTriangleIndex;
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
