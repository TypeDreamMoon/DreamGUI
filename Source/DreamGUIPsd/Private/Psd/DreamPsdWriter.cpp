// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdWriter.h"
#include "Psd/DreamPsdReader.h"
#include "Misc/Compression.h"

namespace DreamPsdWriterLocal
{
	struct FBytes
	{
		TArray<uint8> Data;

		void U8(uint8 V) { Data.Add(V); }
		void U16(uint16 V) { Data.Add(uint8(V >> 8)); Data.Add(uint8(V)); }
		void I16(int16 V) { U16((uint16)V); }
		void U32(uint32 V) { Data.Add(uint8(V >> 24)); Data.Add(uint8(V >> 16)); Data.Add(uint8(V >> 8)); Data.Add(uint8(V)); }
		void I32(int32 V) { U32((uint32)V); }
		void U64(uint64 V) { U32(uint32(V >> 32)); U32(uint32(V)); }
		void F32(float V) { uint32 B; FMemory::Memcpy(&B, &V, 4); U32(B); }
		void F64(double V) { uint64 B; FMemory::Memcpy(&B, &V, 8); U64(B); }
		void Length(uint64 V, bool bWide) { if (bWide) { U64(V); } else { U32((uint32)V); } }
		void Key(const FString& InKey)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				Data.Add(i < InKey.Len() ? uint8(InKey[i]) : uint8(' '));
			}
		}
		void Append(const TArray<uint8>& InBytes) { Data.Append(InBytes); }
		void Unicode(const FString& InText)
		{
			const FTCHARToUTF16 Units(*InText);
			U32(Units.Length());
			for (int32 i = 0; i < Units.Length(); ++i)
			{
				U16((uint16)Units.Get()[i]);
			}
		}
		void PadTo(int32 InMultiple, int32 InFrom)
		{
			while ((Data.Num() - InFrom) % InMultiple != 0)
			{
				U8(0);
			}
		}
	};

	/** A Pascal string padded so that it and its length byte make a multiple of four. */
	void PascalPadded(FBytes& Out, const FString& InText)
	{
		const int32 Length = FMath::Min(InText.Len(), 255);
		const int32 Start = Out.Data.Num();
		Out.U8((uint8)Length);
		for (int32 i = 0; i < Length; ++i)
		{
			Out.U8(InText[i] < 256 ? (uint8)InText[i] : (uint8)'?');
		}
		Out.PadTo(4, Start);
	}

	bool DreamPsdBlocksWide(const FString& InKey)
	{
		static const TSet<FString> Keys = {
			TEXT("LMsk"), TEXT("Lr16"), TEXT("Lr32"), TEXT("Layr"), TEXT("Mt16"), TEXT("Mt32"), TEXT("Mtrn"), TEXT("Alph"),
			TEXT("FMsk"), TEXT("lnk2"), TEXT("FEid"), TEXT("FXid"), TEXT("PxSD"),
		};
		return Keys.Contains(InKey);
	}

	void Block(FBytes& Out, const FString& InKey, const TArray<uint8>& InData, bool bInLarge, int32 InPad)
	{
		Out.Key(TEXT("8BIM"));
		Out.Key(InKey);
		TArray<uint8> Data = InData;
		while (Data.Num() % InPad != 0)
		{
			Data.Add(0);
		}
		Out.Length(Data.Num(), bInLarge && DreamPsdBlocksWide(InKey));
		Out.Append(Data);
	}

	/** A plane of 8-bit samples, at the document's depth, compressed. Row counts in front for run-length. */
	TArray<uint8> EncodePlane(const TArray<uint8>& InEight, int32 W, int32 H, int32 InDepth, uint16 InCompression, bool bInLarge)
	{
		const int32 SampleBytes = InDepth / 8;
		const int32 RowBytes = W * SampleBytes;
		TArray<uint8> Plane;
		Plane.SetNumUninitialized(RowBytes * H);
		for (int32 i = 0; i < W * H; ++i)
		{
			if (SampleBytes == 1)
			{
				Plane[i] = InEight[i];
			}
			else
			{
				const uint16 V = uint16(InEight[i]) * 257;
				Plane[i * 2] = uint8(V >> 8);
				Plane[i * 2 + 1] = uint8(V);
			}
		}
		FBytes Out;
		switch (InCompression)
		{
		case 1:
		{
			TArray<TArray<uint8>> Rows;
			for (int32 y = 0; y < H; ++y)
			{
				TArray<uint8> Row;
				DreamPsdPixels::PackBits(Plane.GetData() + y * RowBytes, RowBytes, Row);
				Rows.Add(MoveTemp(Row));
			}
			for (const TArray<uint8>& Row : Rows)
			{
				if (bInLarge) { Out.U32(Row.Num()); } else { Out.U16((uint16)Row.Num()); }
			}
			for (const TArray<uint8>& Row : Rows)
			{
				Out.Append(Row);
			}
			break;
		}
		case 2:
		case 3:
		{
			if (InCompression == 3)
			{
				for (int32 y = 0; y < H; ++y)
				{
					DreamPsdPixels::ApplyPrediction(Plane.GetData() + y * RowBytes, W, InDepth);
				}
			}
			int32 Size = FCompression::CompressMemoryBound(NAME_Zlib, Plane.Num());
			TArray<uint8> Compressed;
			Compressed.SetNumUninitialized(Size);
			FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), Size, Plane.GetData(), Plane.Num());
			Compressed.SetNum(Size);
			Out.Append(Compressed);
			break;
		}
		default:
			Out.Append(Plane);
			break;
		}
		return MoveTemp(Out.Data);
	}

	TArray<uint8> MakeVectorMask(const FDreamPsdWriteLayer& L, int32 InWidth, int32 InHeight)
	{
		FBytes B;
		B.U32(3);
		B.U32(L.bVectorInvert ? 1 : 0);
		// The path fill rule record, then the initial fill rule.
		B.U16(6);
		for (int32 i = 0; i < 24; ++i) { B.U8(0); }
		B.U16(8);
		B.U16(L.VectorPath.bStartsFilled ? 1 : 0);
		for (int32 i = 0; i < 22; ++i) { B.U8(0); }
		auto Point = [&B, InWidth, InHeight](const FVector2D& P)
		{
			B.I32((int32)FMath::RoundToInt64(P.Y / InHeight * 16777216.0));
			B.I32((int32)FMath::RoundToInt64(P.X / InWidth * 16777216.0));
		};
		for (const FDreamPsdSubpath& Sub : L.VectorPath.Subpaths)
		{
			B.U16(Sub.bClosed ? 0 : 3);
			B.U16((uint16)Sub.Knots.Num());
			B.I16((int16)Sub.Operation);
			B.U16(1);
			for (int32 i = 0; i < 18; ++i) { B.U8(0); }
			for (const FDreamPsdPathKnot& K : Sub.Knots)
			{
				B.U16(Sub.bClosed ? (K.bLinked ? 1 : 2) : (K.bLinked ? 4 : 5));
				Point(K.In);
				Point(K.Anchor);
				Point(K.Out);
			}
		}
		return MoveTemp(B.Data);
	}

	FDreamPsdDescriptor MakeUnitRect(const TCHAR* InClass, const FBox2D& InBox)
	{
		FDreamPsdDescriptor D;
		D.ClassId = InClass;
		D.Items.Emplace(TEXT("unitValueQuadVersion"), DreamPsdDescriptor::MakeInteger(1));
		D.Items.Emplace(TEXT("Top "), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InBox.Min.Y));
		D.Items.Emplace(TEXT("Left"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InBox.Min.X));
		D.Items.Emplace(TEXT("Btom"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InBox.Max.Y));
		D.Items.Emplace(TEXT("Rght"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InBox.Max.X));
		return D;
	}

	TArray<uint8> MakeLiveShape(const FDreamPsdLiveShape& InShape)
	{
		FDreamPsdDescriptor Origin;
		Origin.ClassId = TEXT("null");
		int32 Type = 1;
		switch (InShape.Kind)
		{
		case EDreamPsdLiveShapeKind::RoundedRectangle: Type = 2; break;
		case EDreamPsdLiveShapeKind::Line: Type = 4; break;
		case EDreamPsdLiveShapeKind::Ellipse: Type = 5; break;
		default: break;
		}
		Origin.Items.Emplace(TEXT("keyOriginType"), DreamPsdDescriptor::MakeInteger(Type));
		Origin.Items.Emplace(TEXT("keyOriginShapeBBox"), DreamPsdDescriptor::MakeDescriptor(MakeUnitRect(TEXT("unitRect"), InShape.Box)));
		if (InShape.Kind == EDreamPsdLiveShapeKind::RoundedRectangle)
		{
			FDreamPsdDescriptor Radii;
			Radii.ClassId = TEXT("radii");
			Radii.Items.Emplace(TEXT("unitValueQuadVersion"), DreamPsdDescriptor::MakeInteger(1));
			Radii.Items.Emplace(TEXT("topRight"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InShape.Radii.Y));
			Radii.Items.Emplace(TEXT("topLeft"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InShape.Radii.X));
			Radii.Items.Emplace(TEXT("bottomLeft"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InShape.Radii.W));
			Radii.Items.Emplace(TEXT("bottomRight"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InShape.Radii.Z));
			Origin.Items.Emplace(TEXT("keyOriginRRectRadii"), DreamPsdDescriptor::MakeDescriptor(Radii));
		}
		FDreamPsdDescriptor Transform;
		Transform.ClassId = TEXT("Trnf");
		for (const TCHAR* Key : { TEXT("xx"), TEXT("xy"), TEXT("yx"), TEXT("yy"), TEXT("tx"), TEXT("ty") })
		{
			const bool bDiagonal = FCString::Strcmp(Key, TEXT("xx")) == 0 || FCString::Strcmp(Key, TEXT("yy")) == 0;
			Transform.Items.Emplace(Key, DreamPsdDescriptor::MakeNumber(bDiagonal ? 1.0 : 0.0));
		}
		Origin.Items.Emplace(TEXT("Trnf"), DreamPsdDescriptor::MakeDescriptor(Transform));
		FDreamPsdDescriptor Root;
		Root.ClassId = TEXT("null");
		Root.Items.Emplace(TEXT("keyDescriptorList"), DreamPsdDescriptor::MakeList({ DreamPsdDescriptor::MakeDescriptor(Origin) }));
		FBytes B;
		B.U32(1);
		B.U32(16);
		DreamPsdDescriptor::Write(B.Data, Root);
		return MoveTemp(B.Data);
	}

	TArray<uint8> MakeStroke(const FDreamPsdStroke& InStroke)
	{
		FDreamPsdDescriptor Content;
		Content.ClassId = TEXT("solidColorLayer");
		Content.Items.Emplace(TEXT("Clr "), DreamPsdDescriptor::MakeDescriptor(DreamPsdDescriptor::MakeColor(InStroke.Color)));
		FDreamPsdDescriptor D;
		D.ClassId = TEXT("strokeStyle");
		D.Items.Emplace(TEXT("strokeStyleVersion"), DreamPsdDescriptor::MakeInteger(2));
		D.Items.Emplace(TEXT("strokeEnabled"), DreamPsdDescriptor::MakeBool(InStroke.bEnabled));
		D.Items.Emplace(TEXT("fillEnabled"), DreamPsdDescriptor::MakeBool(InStroke.bFillEnabled));
		D.Items.Emplace(TEXT("strokeStyleLineWidth"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), InStroke.Width));
		const TCHAR* Alignment = InStroke.Alignment == 0 ? TEXT("strokeStyleAlignInside") : (InStroke.Alignment == 2 ? TEXT("strokeStyleAlignOutside") : TEXT("strokeStyleAlignCenter"));
		D.Items.Emplace(TEXT("strokeStyleLineAlignment"), DreamPsdDescriptor::MakeEnum(TEXT("strokeStyleLineAlignment"), Alignment));
		D.Items.Emplace(TEXT("strokeStyleContent"), DreamPsdDescriptor::MakeDescriptor(Content));
		D.Items.Emplace(TEXT("strokeStyleOpacity"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Prc"), InStroke.Opacity * 100.0));
		FBytes B;
		B.U32(16);
		DreamPsdDescriptor::Write(B.Data, D);
		return MoveTemp(B.Data);
	}

	FDreamPsdDescriptor MakeGradientDescriptor(const FDreamPsdGradient& G)
	{
		FDreamPsdDescriptor D;
		D.ClassId = TEXT("Grdn");
		D.Items.Emplace(TEXT("Nm  "), DreamPsdDescriptor::MakeString(G.Name));
		D.Items.Emplace(TEXT("GrdF"), DreamPsdDescriptor::MakeEnum(TEXT("GrdF"), TEXT("CstS")));
		D.Items.Emplace(TEXT("Intr"), DreamPsdDescriptor::MakeNumber(G.Smoothness * 4096.0));
		TArray<FDreamPsdValue> Colours;
		for (const FDreamPsdColorStop& S : G.ColorStops)
		{
			FDreamPsdDescriptor Stop;
			Stop.ClassId = TEXT("Clrt");
			Stop.Items.Emplace(TEXT("Clr "), DreamPsdDescriptor::MakeDescriptor(DreamPsdDescriptor::MakeColor(S.Color)));
			Stop.Items.Emplace(TEXT("Type"), DreamPsdDescriptor::MakeEnum(TEXT("Clry"), TEXT("UsrS")));
			Stop.Items.Emplace(TEXT("Lctn"), DreamPsdDescriptor::MakeInteger(FMath::RoundToInt(S.Location * 4096.0f)));
			Stop.Items.Emplace(TEXT("Mdpn"), DreamPsdDescriptor::MakeInteger(FMath::RoundToInt(S.Midpoint * 100.0f)));
			Colours.Add(DreamPsdDescriptor::MakeDescriptor(Stop));
		}
		D.Items.Emplace(TEXT("Clrs"), DreamPsdDescriptor::MakeList(Colours));
		TArray<FDreamPsdValue> Alphas;
		const TArray<FDreamPsdAlphaStop> Stops = G.AlphaStops.Num() > 0 ? G.AlphaStops : TArray<FDreamPsdAlphaStop>{ { 0.0f, 0.5f, 1.0f }, { 1.0f, 0.5f, 1.0f } };
		for (const FDreamPsdAlphaStop& S : Stops)
		{
			FDreamPsdDescriptor Stop;
			Stop.ClassId = TEXT("TrnS");
			Stop.Items.Emplace(TEXT("Opct"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Prc"), S.Opacity * 100.0));
			Stop.Items.Emplace(TEXT("Lctn"), DreamPsdDescriptor::MakeInteger(FMath::RoundToInt(S.Location * 4096.0f)));
			Stop.Items.Emplace(TEXT("Mdpn"), DreamPsdDescriptor::MakeInteger(FMath::RoundToInt(S.Midpoint * 100.0f)));
			Alphas.Add(DreamPsdDescriptor::MakeDescriptor(Stop));
		}
		D.Items.Emplace(TEXT("Trns"), DreamPsdDescriptor::MakeList(Alphas));
		return D;
	}

	TArray<uint8> MakeFill(const FDreamPsdFill& InFill, FString& OutKey)
	{
		FDreamPsdDescriptor D;
		D.ClassId = TEXT("null");
		if (InFill.Kind == FDreamPsdFill::EKind::Gradient)
		{
			OutKey = TEXT("GdFl");
			const FDreamPsdGradientFill& G = InFill.Gradient;
			D.Items.Emplace(TEXT("Grad"), DreamPsdDescriptor::MakeDescriptor(MakeGradientDescriptor(G.Gradient)));
			D.Items.Emplace(TEXT("Type"), DreamPsdDescriptor::MakeEnum(TEXT("GrdT"), G.Style));
			D.Items.Emplace(TEXT("Angl"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Ang"), G.Angle));
			D.Items.Emplace(TEXT("Scl "), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Prc"), G.Scale * 100.0));
			D.Items.Emplace(TEXT("Rvrs"), DreamPsdDescriptor::MakeBool(G.bReverse));
			D.Items.Emplace(TEXT("Algn"), DreamPsdDescriptor::MakeBool(G.bAlignWithLayer));
		}
		else
		{
			OutKey = TEXT("SoCo");
			D.Items.Emplace(TEXT("Clr "), DreamPsdDescriptor::MakeDescriptor(DreamPsdDescriptor::MakeColor(InFill.Color)));
		}
		FBytes B;
		B.U32(16);
		DreamPsdDescriptor::Write(B.Data, D);
		return MoveTemp(B.Data);
	}

	FDreamPsdDescriptor MakeBounds(const TCHAR* InClass, const FBox2D& InBox)
	{
		FDreamPsdDescriptor D;
		D.ClassId = InClass;
		D.Items.Emplace(TEXT("Left"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pnt"), InBox.Min.X));
		D.Items.Emplace(TEXT("Top "), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pnt"), InBox.Min.Y));
		D.Items.Emplace(TEXT("Rght"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pnt"), InBox.Max.X));
		D.Items.Emplace(TEXT("Btom"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pnt"), InBox.Max.Y));
		return D;
	}

	TArray<uint8> MakeText(const FDreamPsdText& T)
	{
		FDreamPsdEngineValue Engine = DreamPsdWriter::MakeEngineData(T);
		TArray<uint8> EngineBytes;
		DreamPsdEngineData::Write(Engine, EngineBytes);
		FDreamPsdDescriptor D;
		D.ClassId = TEXT("TxLr");
		D.Items.Emplace(TEXT("Txt "), DreamPsdDescriptor::MakeString(T.Text.Replace(TEXT("\n"), TEXT("\r")) + TEXT("\r")));
		D.Items.Emplace(TEXT("textGridding"), DreamPsdDescriptor::MakeEnum(TEXT("textGridding"), TEXT("None")));
		D.Items.Emplace(TEXT("Ornt"), DreamPsdDescriptor::MakeEnum(TEXT("Ornt"), T.bVertical ? TEXT("Vrtc") : TEXT("Hrzn")));
		D.Items.Emplace(TEXT("AntA"), DreamPsdDescriptor::MakeEnum(TEXT("Annt"), T.AntiAlias.IsEmpty() ? FString(TEXT("antiAliasSharp")) : T.AntiAlias));
		const FBox2D Bounds = T.Bounds.bIsValid ? T.Bounds : FBox2D(FVector2D(0, -10), FVector2D(10, 2));
		D.Items.Emplace(TEXT("bounds"), DreamPsdDescriptor::MakeDescriptor(MakeBounds(TEXT("bounds"), Bounds)));
		D.Items.Emplace(TEXT("boundingBox"), DreamPsdDescriptor::MakeDescriptor(MakeBounds(TEXT("boundingBox"), Bounds)));
		D.Items.Emplace(TEXT("TextIndex"), DreamPsdDescriptor::MakeInteger(0));
		D.Items.Emplace(TEXT("EngineData"), DreamPsdDescriptor::MakeRaw(EngineBytes));
		FDreamPsdDescriptor Warp;
		Warp.ClassId = TEXT("warp");
		Warp.Items.Emplace(TEXT("warpStyle"), DreamPsdDescriptor::MakeEnum(TEXT("warpStyle"), T.Warp.Style));
		Warp.Items.Emplace(TEXT("warpValue"), DreamPsdDescriptor::MakeNumber(T.Warp.Value));
		Warp.Items.Emplace(TEXT("warpPerspective"), DreamPsdDescriptor::MakeNumber(T.Warp.Perspective));
		Warp.Items.Emplace(TEXT("warpPerspectiveOther"), DreamPsdDescriptor::MakeNumber(T.Warp.PerspectiveOther));
		Warp.Items.Emplace(TEXT("warpRotate"), DreamPsdDescriptor::MakeEnum(TEXT("Ornt"), T.Warp.Rotate));
		FBytes B;
		B.U16(1);
		for (double V : T.Transform)
		{
			B.F64(V);
		}
		B.U16(50);
		B.U32(16);
		DreamPsdDescriptor::Write(B.Data, D);
		B.U16(1);
		B.U32(16);
		DreamPsdDescriptor::Write(B.Data, Warp);
		for (int32 i = 0; i < 4; ++i)
		{
			B.I32(0);
		}
		return MoveTemp(B.Data);
	}

	void WriteLayerInfo(FBytes& Out, const FDreamPsdWriteDocument& Doc)
	{
		const bool bLarge = Doc.bLarge;
		Out.I16((int16)-Doc.Layers.Num());
		TArray<TArray<TArray<uint8>>> ChannelData;
		for (const FDreamPsdWriteLayer& L : Doc.Layers)
		{
			// The channels, encoded first: their lengths go in the record.
			TArray<TPair<int16, TArray<uint8>>> Channels;
			const int32 W = L.Rect.Width();
			const int32 H = L.Rect.Height();
			if (W > 0 && H > 0 && L.Pixels.Width == W && L.Pixels.Height == H)
			{
				for (int16 Id : { (int16)-1, (int16)0, (int16)1, (int16)2 })
				{
					TArray<uint8> Eight;
					Eight.SetNumUninitialized(W * H);
					for (int32 i = 0; i < W * H; ++i)
					{
						const FColor P = L.Pixels.Pixels[i];
						Eight[i] = Id == -1 ? P.A : (Id == 0 ? P.R : (Id == 1 ? P.G : P.B));
					}
					if (Doc.ColorMode == EDreamPsdColorMode::Grayscale && Id > 0)
					{
						continue;
					}
					Channels.Emplace(Id, EncodePlane(Eight, W, H, Doc.Depth, L.Compression, bLarge));
				}
			}
			else
			{
				for (int16 Id : { (int16)-1, (int16)0, (int16)1, (int16)2 })
				{
					if (Doc.ColorMode == EDreamPsdColorMode::Grayscale && Id > 0)
					{
						continue;
					}
					Channels.Emplace(Id, TArray<uint8>());
				}
			}
			if (L.bMask && !L.MaskPixels.IsEmpty())
			{
				Channels.Emplace((int16)-2, EncodePlane(L.MaskPixels.Pixels, L.MaskPixels.Width, L.MaskPixels.Height, Doc.Depth, L.Compression, bLarge));
			}
			Out.I32(L.Rect.Min.Y);
			Out.I32(L.Rect.Min.X);
			Out.I32(L.Rect.Max.Y);
			Out.I32(L.Rect.Max.X);
			Out.U16((uint16)Channels.Num());
			TArray<TArray<uint8>> Data;
			for (const TPair<int16, TArray<uint8>>& C : Channels)
			{
				Out.I16(C.Key);
				Out.Length(C.Value.Num() + 2, bLarge);
				TArray<uint8> WithCompression;
				WithCompression.Add(uint8(L.Compression >> 8));
				WithCompression.Add(uint8(L.Compression));
				WithCompression.Append(C.Value);
				Data.Add(MoveTemp(WithCompression));
			}
			ChannelData.Add(MoveTemp(Data));
			Out.Key(TEXT("8BIM"));
			Out.Key(L.BlendKey);
			Out.U8(L.Opacity);
			Out.U8(L.bClipping ? 1 : 0);
			Out.U8((L.bVisible ? 0 : 0x02) | 0x08);
			Out.U8(0);

			FBytes Extra;
			if (L.bMask)
			{
				FBytes Mask;
				Mask.I32(L.MaskRect.Min.Y);
				Mask.I32(L.MaskRect.Min.X);
				Mask.I32(L.MaskRect.Max.Y);
				Mask.I32(L.MaskRect.Max.X);
				Mask.U8(L.MaskDefault);
				const bool bParams = L.MaskDensity < 1.0f || L.MaskFeather > 0.0f;
				Mask.U8((L.bMaskDisabled ? 0x02 : 0) | (bParams ? 0x10 : 0));
				if (bParams)
				{
					Mask.U8((L.MaskDensity < 1.0f ? 0x01 : 0) | (L.MaskFeather > 0.0f ? 0x02 : 0));
					if (L.MaskDensity < 1.0f) { Mask.U8((uint8)FMath::RoundToInt(L.MaskDensity * 255.0f)); }
					if (L.MaskFeather > 0.0f) { Mask.F64(L.MaskFeather); }
				}
				while (Mask.Data.Num() < 20)
				{
					Mask.U8(0);
				}
				Extra.U32(Mask.Data.Num());
				Extra.Append(Mask.Data);
			}
			else
			{
				Extra.U32(0);
			}
			Extra.U32(0);
			PascalPadded(Extra, L.Name);
			{
				FBytes Name;
				Name.Unicode(L.Name);
				Block(Extra, TEXT("luni"), Name.Data, bLarge, 2);
			}
			if (L.LayerId >= 0)
			{
				FBytes Id;
				Id.I32(L.LayerId);
				Block(Extra, TEXT("lyid"), Id.Data, bLarge, 2);
			}
			if (L.SectionType != 0)
			{
				FBytes Section;
				Section.U32((uint32)L.SectionType);
				if (L.SectionType == 1 || L.SectionType == 2)
				{
					Section.Key(TEXT("8BIM"));
					Section.Key(L.BlendKey);
				}
				Block(Extra, TEXT("lsct"), Section.Data, bLarge, 2);
			}
			if (L.FillOpacity != 255)
			{
				FBytes Fill;
				Fill.U8(L.FillOpacity);
				Fill.U8(0); Fill.U8(0); Fill.U8(0);
				Block(Extra, TEXT("iOpa"), Fill.Data, bLarge, 2);
			}
			if (L.Fill.Kind != FDreamPsdFill::EKind::None)
			{
				FString Key;
				const TArray<uint8> Fill = MakeFill(L.Fill, Key);
				Block(Extra, Key, Fill, bLarge, 2);
			}
			if (L.bVectorMask)
			{
				Block(Extra, TEXT("vmsk"), MakeVectorMask(L, Doc.Width, Doc.Height), bLarge, 2);
			}
			if (L.LiveShape.Kind != EDreamPsdLiveShapeKind::None)
			{
				Block(Extra, TEXT("vogk"), MakeLiveShape(L.LiveShape), bLarge, 2);
			}
			if (L.Stroke.bPresent)
			{
				Block(Extra, TEXT("vstk"), MakeStroke(L.Stroke), bLarge, 2);
			}
			if (L.Text.IsValid())
			{
				Block(Extra, TEXT("TySh"), MakeText(*L.Text), bLarge, 2);
			}
			for (const FDreamPsdTaggedBlock& B : L.ExtraBlocks)
			{
				Block(Extra, B.Key, B.Data, bLarge, 2);
			}
			Out.U32(Extra.Data.Num());
			Out.Append(Extra.Data);
		}
		for (const TArray<TArray<uint8>>& Layer : ChannelData)
		{
			for (const TArray<uint8>& Channel : Layer)
			{
				Out.Append(Channel);
			}
		}
		while (Out.Data.Num() % 2 != 0)
		{
			Out.U8(0);
		}
	}
}

FDreamPsdEngineValue DreamPsdWriter::MakeEngineData(const FDreamPsdText& T)
{
	using V = FDreamPsdEngineValue;
	TArray<FString> Fonts;
	for (const FDreamPsdTextStyleRun& Run : T.StyleRuns)
	{
		Fonts.AddUnique(Run.FontName.IsEmpty() ? FString(TEXT("ArialMT")) : Run.FontName);
	}
	if (Fonts.Num() == 0)
	{
		Fonts.Add(TEXT("ArialMT"));
	}
	const FString Text = T.Text.Replace(TEXT("\n"), TEXT("\r")) + TEXT("\r");
	V Root = V::MakeDict();
	V& Engine = Root.Add(TEXT("EngineDict"), V::MakeDict());
	V& Editor = Engine.Add(TEXT("Editor"), V::MakeDict());
	Editor.Add(TEXT("Text"), V::MakeString(Text));

	V& Paragraph = Engine.Add(TEXT("ParagraphRun"), V::MakeDict());
	V& PArray = Paragraph.Add(TEXT("RunArray"), V::MakeArray());
	V& PLengths = Paragraph.Add(TEXT("RunLengthArray"), V::MakeArray());
	TArray<FDreamPsdTextParagraphRun> Paragraphs = T.ParagraphRuns;
	if (Paragraphs.Num() == 0)
	{
		Paragraphs.AddDefaulted();
	}
	for (int32 i = 0; i < Paragraphs.Num(); ++i)
	{
		V Run = V::MakeDict();
		V& Sheet = Run.Add(TEXT("ParagraphSheet"), V::MakeDict());
		Sheet.Add(TEXT("DefaultStyleSheet"), V::MakeNumber(0));
		V& Properties = Sheet.Add(TEXT("Properties"), V::MakeDict());
		Properties.Add(TEXT("Justification"), V::MakeNumber(Paragraphs[i].Justification));
		PArray.Push(Run);
		const int32 Length = i + 1 == Paragraphs.Num() ? Text.Len() - Paragraphs[i].Start : Paragraphs[i].Length;
		PLengths.Push(V::MakeNumber(Length));
	}

	V& Style = Engine.Add(TEXT("StyleRun"), V::MakeDict());
	V& SArray = Style.Add(TEXT("RunArray"), V::MakeArray());
	V& SLengths = Style.Add(TEXT("RunLengthArray"), V::MakeArray());
	TArray<FDreamPsdTextStyleRun> Runs = T.StyleRuns;
	if (Runs.Num() == 0)
	{
		Runs.AddDefaulted();
	}
	for (int32 i = 0; i < Runs.Num(); ++i)
	{
		const FDreamPsdTextStyleRun& R = Runs[i];
		V Run = V::MakeDict();
		V& Sheet = Run.Add(TEXT("StyleSheet"), V::MakeDict());
		V& Data = Sheet.Add(TEXT("StyleSheetData"), V::MakeDict());
		Data.Add(TEXT("Font"), V::MakeNumber(Fonts.IndexOfByKey(R.FontName.IsEmpty() ? FString(TEXT("ArialMT")) : R.FontName)));
		Data.Add(TEXT("FontSize"), V::MakeNumber(R.FontSize));
		Data.Add(TEXT("FauxBold"), V::MakeBool(R.bFauxBold));
		Data.Add(TEXT("FauxItalic"), V::MakeBool(R.bFauxItalic));
		Data.Add(TEXT("AutoLeading"), V::MakeBool(R.bAutoLeading));
		Data.Add(TEXT("Leading"), V::MakeNumber(R.Leading));
		Data.Add(TEXT("Tracking"), V::MakeNumber(R.Tracking));
		Data.Add(TEXT("Underline"), V::MakeBool(R.bUnderline));
		Data.Add(TEXT("Strikethrough"), V::MakeBool(R.bStrikethrough));
		V& Fill = Data.Add(TEXT("FillColor"), V::MakeDict());
		Fill.Add(TEXT("Type"), V::MakeNumber(1));
		V& Values = Fill.Add(TEXT("Values"), V::MakeArray());
		Values.Push(V::MakeNumber(R.Color.A));
		Values.Push(V::MakeNumber(R.Color.R));
		Values.Push(V::MakeNumber(R.Color.G));
		Values.Push(V::MakeNumber(R.Color.B));
		SArray.Push(Run);
		const int32 Length = i + 1 == Runs.Num() ? Text.Len() - R.Start : R.Length;
		SLengths.Push(V::MakeNumber(Length));
	}

	V& Rendered = Engine.Add(TEXT("Rendered"), V::MakeDict());
	V& Shapes = Rendered.Add(TEXT("Shapes"), V::MakeDict());
	V& Children = Shapes.Add(TEXT("Children"), V::MakeArray());
	V Shape = V::MakeDict();
	Shape.Add(TEXT("ShapeType"), V::MakeNumber(T.bBoxText ? 1 : 0));
	V& Cookie = Shape.Add(TEXT("Cookie"), V::MakeDict());
	// The cookie's key is the format's own name for the writing application.
	V& Writer = Cookie.Add(TEXT("Photoshop"), V::MakeDict());
	Writer.Add(TEXT("ShapeType"), V::MakeNumber(T.bBoxText ? 1 : 0));
	if (T.bBoxText)
	{
		V& Box = Writer.Add(TEXT("BoxBounds"), V::MakeArray());
		Box.Push(V::MakeNumber(T.BoxBounds.Min.X));
		Box.Push(V::MakeNumber(T.BoxBounds.Min.Y));
		Box.Push(V::MakeNumber(T.BoxBounds.Max.X));
		Box.Push(V::MakeNumber(T.BoxBounds.Max.Y));
	}
	Children.Push(Shape);

	V& Resources = Root.Add(TEXT("ResourceDict"), V::MakeDict());
	V& FontSet = Resources.Add(TEXT("FontSet"), V::MakeArray());
	for (const FString& Font : Fonts)
	{
		V Entry = V::MakeDict();
		Entry.Add(TEXT("Name"), V::MakeString(Font));
		Entry.Add(TEXT("Script"), V::MakeNumber(0));
		Entry.Add(TEXT("FontType"), V::MakeNumber(1));
		Entry.Add(TEXT("Synthetic"), V::MakeNumber(0));
		FontSet.Push(Entry);
	}
	V& Sheets = Resources.Add(TEXT("StyleSheetSet"), V::MakeArray());
	V Normal = V::MakeDict();
	Normal.Add(TEXT("Name"), V::MakeString(TEXT("Normal RGB")));
	V& NormalData = Normal.Add(TEXT("StyleSheetData"), V::MakeDict());
	NormalData.Add(TEXT("Font"), V::MakeNumber(0));
	NormalData.Add(TEXT("FontSize"), V::MakeNumber(12));
	Sheets.Push(Normal);
	Resources.Add(TEXT("TheNormalStyleSheet"), V::MakeNumber(0));
	Resources.Add(TEXT("TheNormalParagraphSheet"), V::MakeNumber(0));
	return Root;
}

FDreamPsdTaggedBlock DreamPsdWriter::MakeEffectsBlock(const FDreamPsdDescriptor& InEffects)
{
	DreamPsdWriterLocal::FBytes B;
	B.U32(0);
	B.U32(16);
	DreamPsdDescriptor::Write(B.Data, InEffects);
	return { TEXT("lfx2"), MoveTemp(B.Data) };
}

FDreamPsdTaggedBlock DreamPsdWriter::MakeDescriptorBlock(const FString& InKey, const FDreamPsdDescriptor& InDescriptor)
{
	DreamPsdWriterLocal::FBytes B;
	B.U32(16);
	DreamPsdDescriptor::Write(B.Data, InDescriptor);
	return { InKey, MoveTemp(B.Data) };
}

FDreamPsdTaggedBlock DreamPsdWriter::MakeLevelsBlock(int32 InBlack, int32 InWhite, float InGamma, int32 InOutBlack, int32 InOutWhite)
{
	DreamPsdWriterLocal::FBytes B;
	B.U16(2);
	for (int32 i = 0; i < 29; ++i)
	{
		const bool bComposite = i == 0;
		B.U16(bComposite ? InBlack : 0);
		B.U16(bComposite ? InWhite : 255);
		B.U16(bComposite ? InOutBlack : 0);
		B.U16(bComposite ? InOutWhite : 255);
		B.U16(bComposite ? (uint16)FMath::RoundToInt(InGamma * 100.0f) : 100);
	}
	B.U16(0);
	return { TEXT("levl"), MoveTemp(B.Data) };
}

FDreamPsdTaggedBlock DreamPsdWriter::MakeCurvesBlock(const TArray<FDreamPsdAdjustment::FCurve>& InCurves)
{
	DreamPsdWriterLocal::FBytes B;
	B.U8(0);
	B.U16(4);
	B.U32(InCurves.Num());
	for (const FDreamPsdAdjustment::FCurve& C : InCurves)
	{
		B.U16((uint16)C.Channel);
		B.U16((uint16)C.Points.Num());
		for (const FVector2f& P : C.Points)
		{
			B.U16((uint16)P.Y);
			B.U16((uint16)P.X);
		}
	}
	return { TEXT("curv"), MoveTemp(B.Data) };
}

FDreamPsdTaggedBlock DreamPsdWriter::MakeHueSaturationBlock(int32 InHue, int32 InSaturation, int32 InLightness, bool bInColorize)
{
	DreamPsdWriterLocal::FBytes B;
	B.U16(2);
	B.U8(bInColorize ? 1 : 0);
	B.U8(0);
	B.I16(bInColorize ? (int16)InHue : 0);
	B.I16(bInColorize ? (int16)InSaturation : 25);
	B.I16(bInColorize ? (int16)InLightness : 0);
	B.I16(bInColorize ? 0 : (int16)InHue);
	B.I16(bInColorize ? 0 : (int16)InSaturation);
	B.I16(bInColorize ? 0 : (int16)InLightness);
	for (int32 i = 0; i < 6; ++i)
	{
		for (int32 k = 0; k < 7; ++k)
		{
			B.I16(0);
		}
	}
	return { TEXT("hue2"), MoveTemp(B.Data) };
}

void DreamPsdWriter::Write(const FDreamPsdWriteDocument& Doc, TArray<uint8>& OutBytes)
{
	using namespace DreamPsdWriterLocal;
	FBytes Out;
	Out.Key(TEXT("8BPS"));
	Out.U16(Doc.bLarge ? 2 : 1);
	for (int32 i = 0; i < 6; ++i) { Out.U8(0); }
	const bool bGray = Doc.ColorMode == EDreamPsdColorMode::Grayscale;
	const int32 Planes = bGray ? 2 : 4;
	Out.U16((uint16)Planes);
	Out.U32(Doc.Height);
	Out.U32(Doc.Width);
	Out.U16((uint16)Doc.Depth);
	Out.U16((uint16)Doc.ColorMode);
	Out.U32(0);

	// Resources: resolution, global angle and altitude, version info.
	FBytes Resources;
	auto Resource = [&Resources](uint16 InId, const TArray<uint8>& InData)
	{
		Resources.Key(TEXT("8BIM"));
		Resources.U16(InId);
		Resources.U16(0);
		Resources.U32(InData.Num());
		Resources.Append(InData);
		if (InData.Num() % 2 == 1) { Resources.U8(0); }
	};
	{
		FBytes R;
		R.U32(72 << 16); R.U16(1); R.U16(2); R.U32(72 << 16); R.U16(1); R.U16(2);
		Resource(1005, R.Data);
	}
	{
		FBytes R;
		R.I32((int32)Doc.GlobalAngle);
		Resource(1037, R.Data);
	}
	{
		FBytes R;
		R.I32((int32)Doc.GlobalAltitude);
		Resource(1049, R.Data);
	}
	{
		FBytes R;
		R.U32(1); R.U8(1); R.Unicode(TEXT("DreamGUI")); R.Unicode(TEXT("DreamGUI")); R.U32(1);
		Resource(1057, R.Data);
	}
	Out.U32(Resources.Data.Num());
	Out.Append(Resources.Data);

	// Layers and masks.
	FBytes Info;
	WriteLayerInfo(Info, Doc);
	FBytes Section;
	if (Doc.Depth == 16)
	{
		Section.Length(0, Doc.bLarge);
		Section.U32(0);
		Block(Section, TEXT("Lr16"), Info.Data, Doc.bLarge, 4);
	}
	else
	{
		Section.Length(Info.Data.Num(), Doc.bLarge);
		Section.Append(Info.Data);
		Section.U32(0);
	}
	Out.Length(Section.Data.Num(), Doc.bLarge);
	Out.Append(Section.Data);

	// The merged image: matted against white where it is transparent, as the editor stores it.
	const int32 W = Doc.Width;
	const int32 H = Doc.Height;
	TArray<TArray<uint8>> Composite;
	Composite.SetNum(Planes);
	for (TArray<uint8>& P : Composite)
	{
		P.Init(255, W * H);
	}
	if (!Doc.Composite.IsEmpty())
	{
		for (int32 i = 0; i < W * H; ++i)
		{
			const FColor C = Doc.Composite.Pixels[i];
			const float A = C.A / 255.0f;
			auto Matte = [A](uint8 V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * A + 255.0f * (1.0f - A)), 0, 255); };
			if (bGray)
			{
				Composite[0][i] = Matte(C.R);
				Composite[1][i] = C.A;
			}
			else
			{
				Composite[0][i] = Matte(C.R);
				Composite[1][i] = Matte(C.G);
				Composite[2][i] = Matte(C.B);
				Composite[3][i] = C.A;
			}
		}
	}
	Out.U16(Doc.CompositeCompression);
	if (Doc.CompositeCompression == 1)
	{
		TArray<TArray<uint8>> Encoded;
		for (const TArray<uint8>& P : Composite)
		{
			Encoded.Add(EncodePlane(P, W, H, Doc.Depth, 1, Doc.bLarge));
		}
		// Every plane's row counts, then every plane's rows.
		const int32 CountBytes = H * (Doc.bLarge ? 4 : 2);
		for (const TArray<uint8>& E : Encoded)
		{
			Out.Data.Append(E.GetData(), CountBytes);
		}
		for (const TArray<uint8>& E : Encoded)
		{
			Out.Data.Append(E.GetData() + CountBytes, E.Num() - CountBytes);
		}
	}
	else
	{
		for (const TArray<uint8>& P : Composite)
		{
			Out.Append(EncodePlane(P, W, H, Doc.Depth, 0, Doc.bLarge));
		}
	}
	OutBytes = MoveTemp(Out.Data);
}
