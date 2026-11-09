// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPsdBlocks.h"
#include "Psd/DreamPsdColor.h"
#include "Psd/DreamPsdReader.h"

namespace DreamPsdBlocksLocal
{
	/** Three 16-bit signed values in the order they are written (a constructor's arguments have no order). */
	FIntVector ReadHsl(FDreamPsdStream& S)
	{
		const int32 A = S.I16();
		const int32 B = S.I16();
		const int32 C = S.I16();
		return FIntVector(A, B, C);
	}

	/** A colour written as a colour space and four 16-bit components. */
	FLinearColor ReadBinaryColor(FDreamPsdStream& S)
	{
		const uint16 Space = S.U16();
		const uint16 C0 = S.U16();
		const uint16 C1 = S.U16();
		const uint16 C2 = S.U16();
		const uint16 C3 = S.U16();
		switch (Space)
		{
		case 0: return FLinearColor(C0 / 65535.0f, C1 / 65535.0f, C2 / 65535.0f, 1.0f);
		case 1: return DreamPsdColor::HsbToRgb(C0 / 65535.0f * 360.0f, C1 / 65535.0f, C2 / 65535.0f);
		case 2: return DreamPsdColor::CmykToRgb(1.0f - C0 / 65535.0f, 1.0f - C1 / 65535.0f, 1.0f - C2 / 65535.0f, 1.0f - C3 / 65535.0f);
		case 7: return DreamPsdColor::LabToRgb(C0 / 100.0f, (int16)C1 / 100.0f, (int16)C2 / 100.0f);
		case 8:
		{
			const float G = 1.0f - C0 / 10000.0f;
			return FLinearColor(G, G, G, 1.0f);
		}
		default: return FLinearColor::Black;
		}
	}

	FDreamPsdContour ReadContour(const FDreamPsdDescriptor* InContour)
	{
		FDreamPsdContour Contour;
		if (InContour == nullptr)
		{
			return Contour;
		}
		Contour.Name = InContour->GetString(TEXT("Nm  "));
		if (const TArray<FDreamPsdValue>* Curve = InContour->GetList(TEXT("Crv ")))
		{
			for (const FDreamPsdValue& V : *Curve)
			{
				if (V.Descriptor.IsValid())
				{
					FDreamPsdContour::FPoint& Point = Contour.Points.AddDefaulted_GetRef();
					Point.Input = (float)V.Descriptor->GetNumber(TEXT("Hrzn"));
					Point.Output = (float)V.Descriptor->GetNumber(TEXT("Vrtc"));
					Point.bCorner = !V.Descriptor->GetBool(TEXT("Cnty"), true);
				}
			}
		}
		return Contour;
	}

	void ReadEffect(const FDreamPsdDescriptor& D, EDreamPsdEffectKind InKind, const FDreamPsdDocument& InDoc, FDreamPsdEffect& E)
	{
		E.Kind = InKind;
		E.Descriptor = MakeShared<FDreamPsdDescriptor>(D);
		E.bEnabled = D.GetBool(TEXT("enab"), true);
		E.BlendMode = D.GetEnum(TEXT("Md  "), E.BlendMode);
		E.Opacity = (float)D.GetUnitFloat(TEXT("Opct"), 100.0) / 100.0f;
		D.GetColor(TEXT("Clr "), E.Color);
		E.bUseGlobalLight = D.GetBool(TEXT("uglg"), false);
		E.Angle = E.bUseGlobalLight ? InDoc.GlobalAngle : (float)D.GetUnitFloat(TEXT("lagl"), InDoc.GlobalAngle);
		E.Distance = (float)D.GetUnitFloat(TEXT("Dstn"), 0.0);
		E.Spread = (float)D.GetUnitFloat(TEXT("Ckmt"), 0.0) / 100.0f;
		E.Size = (float)D.GetUnitFloat(TEXT("blur"), 0.0);
		E.Noise = (float)D.GetUnitFloat(TEXT("Nose"), 0.0) / 100.0f;
		E.bAntiAlias = D.GetBool(TEXT("AntA"), false);
		// Satin's and a bevel's contour shapes the effect itself ('MpgS'); a bevel's 'TrnS' is its gloss.
		const bool bShapeContour = InKind == EDreamPsdEffectKind::Satin || InKind == EDreamPsdEffectKind::BevelEmboss;
		E.Contour = ReadContour(D.GetDescriptor(bShapeContour ? TEXT("MpgS") : TEXT("TrnS")));
		E.Range = (float)D.GetUnitFloat(TEXT("Inpr"), 50.0) / 100.0f;
		E.Jitter = (float)D.GetUnitFloat(TEXT("ShdN"), 0.0) / 100.0f;
		E.bLayerKnocksOutShadow = D.GetBool(TEXT("layerConceals"), true);
		E.Technique = D.GetEnum(TEXT("GlwT"), D.GetEnum(TEXT("bvlT")));
		E.GlowSource = D.GetEnum(TEXT("glwS"));
		E.bUseGradient = (InKind == EDreamPsdEffectKind::OuterGlow || InKind == EDreamPsdEffectKind::InnerGlow) && D.Has(TEXT("Grad"));

		if (InKind == EDreamPsdEffectKind::BevelEmboss)
		{
			E.BevelStyle = D.GetEnum(TEXT("bvlS"), E.BevelStyle);
			E.Depth = (float)D.GetUnitFloat(TEXT("srgR"), 100.0) / 100.0f;
			E.bDirectionUp = D.GetEnum(TEXT("bvlD"), TEXT("In  ")).TrimEnd() == TEXT("In");
			E.Soften = (float)D.GetUnitFloat(TEXT("Sftn"), 0.0);
			E.Altitude = E.bUseGlobalLight ? InDoc.GlobalAltitude : (float)D.GetUnitFloat(TEXT("Lald"), InDoc.GlobalAltitude);
			E.HighlightBlendMode = D.GetEnum(TEXT("hglM"), E.HighlightBlendMode);
			D.GetColor(TEXT("hglC"), E.HighlightColor);
			E.HighlightOpacity = (float)D.GetUnitFloat(TEXT("hglO"), 75.0) / 100.0f;
			E.ShadowBlendMode = D.GetEnum(TEXT("sdwM"), E.ShadowBlendMode);
			D.GetColor(TEXT("sdwC"), E.ShadowColor);
			E.ShadowOpacity = (float)D.GetUnitFloat(TEXT("sdwO"), 75.0) / 100.0f;
			E.GlossContour = ReadContour(D.GetDescriptor(TEXT("TrnS")));
			E.bGlossAntiAlias = D.GetBool(TEXT("antialiasGloss"), false);
			E.bUseShape = D.GetBool(TEXT("useShape"), false);
			E.bUseTexture = D.GetBool(TEXT("useTexture"), false);
		}
		if (InKind == EDreamPsdEffectKind::Satin)
		{
			E.bInvert = D.GetBool(TEXT("Invr"), false);
		}
		if (InKind == EDreamPsdEffectKind::GradientOverlay || D.Has(TEXT("Grad")))
		{
			DreamPsdBlocks::ReadGradientFill(D, E.Gradient);
		}
		if (InKind == EDreamPsdEffectKind::PatternOverlay || D.Has(TEXT("Ptrn")))
		{
			DreamPsdBlocks::ReadPatternFill(D, E.Pattern);
		}
		if (InKind == EDreamPsdEffectKind::Stroke)
		{
			E.StrokePosition = D.GetEnum(TEXT("Styl"), E.StrokePosition);
			E.FillType = D.GetEnum(TEXT("PntT"), E.FillType);
			E.Size = (float)D.GetUnitFloat(TEXT("Sz  "), 1.0);
			E.bOverprint = D.GetBool(TEXT("overprint"), false);
		}
	}

	FDreamPsdTextStyleRun ReadStyle(const FDreamPsdEngineValue* InDefaults, const FDreamPsdEngineValue* InRun, const TArray<FString>& InFonts)
	{
		auto Number = [InDefaults, InRun](const TCHAR* InKey, double InDefault) -> double
		{
			if (InRun != nullptr && InRun->Find(InKey) != nullptr)
			{
				return InRun->GetNumber(InKey, InDefault);
			}
			return InDefaults != nullptr ? InDefaults->GetNumber(InKey, InDefault) : InDefault;
		};
		auto Value = [InDefaults, InRun](const TCHAR* InKey) -> const FDreamPsdEngineValue*
		{
			const FDreamPsdEngineValue* V = InRun != nullptr ? InRun->Find(InKey) : nullptr;
			return V != nullptr ? V : (InDefaults != nullptr ? InDefaults->Find(InKey) : nullptr);
		};
		FDreamPsdTextStyleRun Style;
		const int32 FontIndex = (int32)Number(TEXT("Font"), 0);
		Style.FontName = InFonts.IsValidIndex(FontIndex) ? InFonts[FontIndex] : FString();
		Style.FontSize = (float)Number(TEXT("FontSize"), 12.0);
		Style.bFauxBold = Number(TEXT("FauxBold"), 0) != 0;
		Style.bFauxItalic = Number(TEXT("FauxItalic"), 0) != 0;
		Style.bUnderline = Number(TEXT("Underline"), 0) != 0;
		Style.bStrikethrough = Number(TEXT("Strikethrough"), 0) != 0;
		Style.bAutoLeading = Number(TEXT("AutoLeading"), 1) != 0;
		Style.Leading = (float)Number(TEXT("Leading"), 0.0);
		Style.Tracking = (float)Number(TEXT("Tracking"), 0.0);
		Style.BaselineShift = (float)Number(TEXT("BaselineShift"), 0.0);
		Style.FontCaps = (int32)Number(TEXT("FontCaps"), 0);
		Style.HorizontalScale = (float)Number(TEXT("HorizontalScale"), 1.0);
		Style.VerticalScale = (float)Number(TEXT("VerticalScale"), 1.0);
		if (const FDreamPsdEngineValue* Fill = Value(TEXT("FillColor")))
		{
			const FDreamPsdEngineValue* Values = Fill->Find(TEXT("Values"));
			if (Values != nullptr && Values->Items.Num() >= 4)
			{
				// ARGB, 0..1.
				Style.Color = FLinearColor((float)Values->Items[1].Number, (float)Values->Items[2].Number, (float)Values->Items[3].Number, (float)Values->Items[0].Number);
			}
		}
		return Style;
	}

	FBox2D ReadBounds(const FDreamPsdDescriptor* D)
	{
		FBox2D Box(ForceInit);
		if (D != nullptr)
		{
			Box += FVector2D(D->GetUnitFloat(TEXT("Left")), D->GetUnitFloat(TEXT("Top ")));
			Box += FVector2D(D->GetUnitFloat(TEXT("Rght")), D->GetUnitFloat(TEXT("Btom")));
		}
		return Box;
	}

	void ReadText(FDreamPsdStream& S, FDreamPsdLayer& L, FDreamPsdMessages& M)
	{
		TSharedPtr<FDreamPsdText> Text = MakeShared<FDreamPsdText>();
		S.U16();
		for (int32 i = 0; i < 6; ++i)
		{
			Text->Transform[i] = S.F64();
		}
		S.U16();
		FString Error;
		Text->TextDescriptor = MakeShared<FDreamPsdDescriptor>();
		if (!DreamPsdDescriptor::ReadVersioned(S, *Text->TextDescriptor, Error))
		{
			M.Warn(FString::Printf(TEXT("its text does not read (%s); it is imported as pixels"), *Error), L.Name);
			return;
		}
		S.U16();
		Text->WarpDescriptor = MakeShared<FDreamPsdDescriptor>();
		if (DreamPsdDescriptor::ReadVersioned(S, *Text->WarpDescriptor, Error))
		{
			Text->Warp.Style = Text->WarpDescriptor->GetEnum(TEXT("warpStyle"), Text->Warp.Style);
			Text->Warp.Value = (float)Text->WarpDescriptor->GetNumber(TEXT("warpValue"));
			Text->Warp.Perspective = (float)Text->WarpDescriptor->GetNumber(TEXT("warpPerspective"));
			Text->Warp.PerspectiveOther = (float)Text->WarpDescriptor->GetNumber(TEXT("warpPerspectiveOther"));
			Text->Warp.Rotate = Text->WarpDescriptor->GetEnum(TEXT("warpRotate"), Text->Warp.Rotate);
		}
		else
		{
			Text->WarpDescriptor.Reset();
		}
		const FDreamPsdDescriptor& D = *Text->TextDescriptor;
		Text->Text = D.GetString(TEXT("Txt "));
		Text->Bounds = ReadBounds(D.GetDescriptor(TEXT("bounds")));
		Text->bVertical = D.GetEnum(TEXT("Ornt")) == TEXT("Vrtc");
		Text->AntiAlias = D.GetEnum(TEXT("AntA"));
		DreamPsdBlocks::ReadTextEngine(*Text, M, L.Name);
		L.Text = Text;
	}

	void ReadVectorMask(FDreamPsdStream& S, FDreamPsdLayer& L, const FDreamPsdDocument& Doc)
	{
		FDreamPsdVectorMask& V = L.VectorMask;
		S.U32();
		const uint32 Flags = S.U32();
		V.bPresent = true;
		V.bInvert = (Flags & 1) != 0;
		V.bNotLinked = (Flags & 2) != 0;
		V.bDisabled = (Flags & 4) != 0;
		auto ReadPoint = [&S, &Doc]()
		{
			const double Y = S.I32() / 16777216.0 * Doc.Height;
			const double X = S.I32() / 16777216.0 * Doc.Width;
			return FVector2D(X, Y);
		};
		while (S.Remaining() >= 26)
		{
			const uint16 Selector = S.U16();
			switch (Selector)
			{
			case 0:
			case 3:
			{
				FDreamPsdSubpath Sub;
				Sub.bClosed = Selector == 0;
				S.U16();
				const int16 Operation = S.I16();
				Sub.Operation = Operation >= 0 && Operation <= 3 ? (EDreamPsdPathOperation)Operation : EDreamPsdPathOperation::Legacy;
				S.Skip(20);
				V.Path.Subpaths.Add(MoveTemp(Sub));
				break;
			}
			case 1:
			case 2:
			case 4:
			case 5:
			{
				FDreamPsdPathKnot Knot;
				Knot.In = ReadPoint();
				Knot.Anchor = ReadPoint();
				Knot.Out = ReadPoint();
				Knot.bLinked = Selector == 1 || Selector == 4;
				if (V.Path.Subpaths.Num() == 0)
				{
					V.Path.Subpaths.AddDefaulted();
					V.Path.Subpaths.Last().bClosed = Selector <= 2;
				}
				V.Path.Subpaths.Last().Knots.Add(Knot);
				break;
			}
			case 8:
				V.Path.bStartsFilled = S.U16() == 1;
				S.Skip(22);
				break;
			default:
				S.Skip(24);
				break;
			}
		}
	}

	void ReadLiveShape(FDreamPsdStream& S, FDreamPsdLayer& L, FDreamPsdMessages& M)
	{
		S.U32();
		FDreamPsdDescriptor D;
		FString Error;
		if (!DreamPsdDescriptor::ReadVersioned(S, D, Error))
		{
			M.Warn(FString::Printf(TEXT("its live shape description does not read (%s)"), *Error), L.Name);
			return;
		}
		const TArray<FDreamPsdValue>* List = D.GetList(TEXT("keyDescriptorList"));
		if (List == nullptr || List->Num() == 0 || !(*List)[0].Descriptor.IsValid())
		{
			return;
		}
		FDreamPsdLiveShape& Shape = L.LiveShape;
		if (List->Num() > 1)
		{
			Shape.Kind = EDreamPsdLiveShapeKind::Other;
			return;
		}
		const FDreamPsdDescriptor& Origin = *(*List)[0].Descriptor;
		switch ((int32)Origin.GetNumber(TEXT("keyOriginType"), 0))
		{
		case 1: Shape.Kind = EDreamPsdLiveShapeKind::Rectangle; break;
		case 2: Shape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle; break;
		case 4: Shape.Kind = EDreamPsdLiveShapeKind::Line; break;
		case 5: Shape.Kind = EDreamPsdLiveShapeKind::Ellipse; break;
		default: Shape.Kind = EDreamPsdLiveShapeKind::Other; break;
		}
		Shape.Box = ReadBounds(Origin.GetDescriptor(TEXT("keyOriginShapeBBox")));
		if (const FDreamPsdDescriptor* Radii = Origin.GetDescriptor(TEXT("keyOriginRRectRadii")))
		{
			Shape.Radii = FVector4(Radii->GetUnitFloat(TEXT("topLeft")), Radii->GetUnitFloat(TEXT("topRight")),
				Radii->GetUnitFloat(TEXT("bottomRight")), Radii->GetUnitFloat(TEXT("bottomLeft")));
		}
		if (const FDreamPsdDescriptor* Transform = Origin.GetDescriptor(TEXT("Trnf")))
		{
			const double XY = Transform->GetNumber(TEXT("xy"));
			const double YX = Transform->GetNumber(TEXT("yx"));
			const double XX = Transform->GetNumber(TEXT("xx"), 1.0);
			const double YY = Transform->GetNumber(TEXT("yy"), 1.0);
			Shape.bTransformed = FMath::Abs(XY) > 1e-6 || FMath::Abs(YX) > 1e-6 || XX <= 0.0 || YY <= 0.0;
		}
	}

	void ReadStroke(FDreamPsdStream& S, FDreamPsdLayer& L, FDreamPsdMessages& M)
	{
		FDreamPsdDescriptor D;
		FString Error;
		if (!DreamPsdDescriptor::ReadVersioned(S, D, Error))
		{
			M.Warn(FString::Printf(TEXT("its stroke does not read (%s)"), *Error), L.Name);
			return;
		}
		FDreamPsdStroke& Stroke = L.Stroke;
		Stroke.bPresent = true;
		Stroke.bEnabled = D.GetBool(TEXT("strokeEnabled"), false);
		Stroke.bFillEnabled = D.GetBool(TEXT("fillEnabled"), true);
		Stroke.Width = (float)D.GetUnitFloat(TEXT("strokeStyleLineWidth"), 1.0);
		const FString Alignment = D.GetEnum(TEXT("strokeStyleLineAlignment"));
		Stroke.Alignment = Alignment == TEXT("strokeStyleAlignInside") ? 0 : (Alignment == TEXT("strokeStyleAlignOutside") ? 2 : 1);
		Stroke.Opacity = (float)D.GetUnitFloat(TEXT("strokeStyleOpacity"), 100.0) / 100.0f;
		Stroke.BlendMode = D.GetEnum(TEXT("strokeStyleBlendMode"), TEXT("Nrml"));
		if (const FDreamPsdDescriptor* Content = D.GetDescriptor(TEXT("strokeStyleContent")))
		{
			Content->GetColor(TEXT("Clr "), Stroke.Color);
		}
	}

	bool ReadFill(const FString& InKey, FDreamPsdStream& S, FDreamPsdLayer& L, FDreamPsdMessages& M)
	{
		FDreamPsdDescriptor D;
		FString Error;
		if (!DreamPsdDescriptor::ReadVersioned(S, D, Error))
		{
			M.Warn(FString::Printf(TEXT("its fill ('%s') does not read (%s)"), *InKey, *Error), L.Name);
			return false;
		}
		FDreamPsdFill& Fill = L.Fill;
		Fill.Descriptor = MakeShared<FDreamPsdDescriptor>(D);
		if (InKey == TEXT("SoCo"))
		{
			Fill.Kind = FDreamPsdFill::EKind::Solid;
			D.GetColor(TEXT("Clr "), Fill.Color);
		}
		else if (InKey == TEXT("GdFl"))
		{
			Fill.Kind = FDreamPsdFill::EKind::Gradient;
			DreamPsdBlocks::ReadGradientFill(D, Fill.Gradient);
		}
		else if (InKey == TEXT("PtFl"))
		{
			Fill.Kind = FDreamPsdFill::EKind::Pattern;
			DreamPsdBlocks::ReadPatternFill(D, Fill.Pattern);
		}
		return true;
	}

	TSharedPtr<FDreamPsdAdjustment> ReadAdjustment(const FString& InKey, FDreamPsdStream& S, FDreamPsdLayer& L, FDreamPsdMessages& M)
	{
		TSharedPtr<FDreamPsdAdjustment> A = MakeShared<FDreamPsdAdjustment>();
		A->Key = InKey;
		S.Bytes(A->Raw, S.Remaining());
		FDreamPsdStream R(A->Raw.GetData(), A->Raw.Num());
		FString Error;
		auto Descriptor = [&R, &A, &Error]() -> bool
		{
			A->Descriptor = MakeShared<FDreamPsdDescriptor>();
			return DreamPsdDescriptor::ReadVersioned(R, *A->Descriptor, Error);
		};
		if (InKey == TEXT("brit"))
		{
			A->Kind = EDreamPsdAdjustmentKind::BrightnessContrast;
			A->Brightness = R.I16();
			A->Contrast = R.I16();
			A->bLegacyBrightness = true;
		}
		else if (InKey == TEXT("CgEd"))
		{
			A->Kind = EDreamPsdAdjustmentKind::BrightnessContrast;
			if (!Descriptor())
			{
				M.Warn(FString::Printf(TEXT("its brightness/contrast does not read (%s)"), *Error), L.Name);
				return nullptr;
			}
			A->Brightness = (float)A->Descriptor->GetNumber(TEXT("Brgh"));
			A->Contrast = (float)A->Descriptor->GetNumber(TEXT("Cntr"));
			A->bLegacyBrightness = A->Descriptor->GetBool(TEXT("useLegacy"), false);
		}
		else if (InKey == TEXT("levl"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Levels;
			R.U16();
			for (int32 i = 0; i < 29 && R.Remaining() >= 10; ++i)
			{
				FDreamPsdAdjustment::FLevels Lv;
				Lv.InputBlack = R.U16();
				Lv.InputWhite = R.U16();
				Lv.OutputBlack = R.U16();
				Lv.OutputWhite = R.U16();
				Lv.Gamma = R.U16() / 100.0f;
				if (i < 4)
				{
					A->Levels.Add(Lv);
				}
			}
		}
		else if (InKey == TEXT("curv"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Curves;
			R.U8();
			const uint16 Version = R.U16();
			auto ReadPoints = [&R](FDreamPsdAdjustment::FCurve& C)
			{
				const uint16 Count = R.U16();
				for (uint16 p = 0; p < Count && R.Remaining() >= 4; ++p)
				{
					const float Output = R.U16();
					const float Input = R.U16();
					C.Points.Emplace(Input, Output);
				}
			};
			if (Version == 1)
			{
				const uint32 Bitmap = R.U32();
				for (int32 Bit = 0; Bit < 32; ++Bit)
				{
					if ((Bitmap & (1u << Bit)) != 0)
					{
						FDreamPsdAdjustment::FCurve C;
						C.Channel = Bit;
						ReadPoints(C);
						A->Curves.Add(MoveTemp(C));
					}
				}
			}
			else
			{
				const uint32 Count = R.U32();
				for (uint32 i = 0; i < Count && R.Remaining() >= 4; ++i)
				{
					FDreamPsdAdjustment::FCurve C;
					C.Channel = R.U16();
					ReadPoints(C);
					A->Curves.Add(MoveTemp(C));
				}
			}
			// The newer curves follow, after a signature of their own, and are the ones the editor shows.
			if (R.Remaining() >= 14 && R.Key4() == TEXT("8BIM") && R.Key4() == TEXT("Crv "))
			{
				R.U16();
				const uint32 Count = R.U32();
				TArray<FDreamPsdAdjustment::FCurve> Newer;
				for (uint32 i = 0; i < Count && R.Remaining() >= 4; ++i)
				{
					FDreamPsdAdjustment::FCurve C;
					C.Channel = R.U16();
					ReadPoints(C);
					Newer.Add(MoveTemp(C));
				}
				if (Newer.Num() > 0)
				{
					A->Curves = MoveTemp(Newer);
				}
			}
		}
		else if (InKey == TEXT("expA"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Exposure;
			R.U16();
			A->Exposure = R.F32();
			A->Offset = R.F32();
			A->Gamma = R.F32();
		}
		else if (InKey == TEXT("vibA"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Vibrance;
			if (!Descriptor())
			{
				M.Warn(FString::Printf(TEXT("its vibrance does not read (%s)"), *Error), L.Name);
				return nullptr;
			}
			A->Vibrance = (float)A->Descriptor->GetNumber(TEXT("vibrance"));
			A->Saturation = (float)A->Descriptor->GetNumber(TEXT("Strt"));
		}
		else if (InKey == TEXT("hue2") || InKey == TEXT("hue "))
		{
			A->Kind = EDreamPsdAdjustmentKind::HueSaturation;
			R.U16();
			A->bColorize = R.U8() != 0;
			R.U8();
			A->ColorizeHsl = ReadHsl(R);
			A->MasterHsl = ReadHsl(R);
			for (int32 i = 0; i < 6 && R.Remaining() >= 14; ++i)
			{
				FDreamPsdAdjustment::FHueRange Range;
				for (int32 k = 0; k < 4; ++k)
				{
					Range.Range[k] = R.I16();
				}
				Range.Hsl = ReadHsl(R);
				A->HueRanges.Add(Range);
			}
		}
		else if (InKey == TEXT("blnc"))
		{
			A->Kind = EDreamPsdAdjustmentKind::ColorBalance;
			A->Shadows = ReadHsl(R);
			A->Midtones = ReadHsl(R);
			A->Highlights = ReadHsl(R);
			A->bPreserveLuminosity = R.U8() != 0;
		}
		else if (InKey == TEXT("blwh"))
		{
			A->Kind = EDreamPsdAdjustmentKind::BlackWhite;
			if (!Descriptor())
			{
				M.Warn(FString::Printf(TEXT("its black and white does not read (%s)"), *Error), L.Name);
				return nullptr;
			}
			const TCHAR* Keys[6] = { TEXT("Rd  "), TEXT("Yllw"), TEXT("Grn "), TEXT("Cyn "), TEXT("Bl  "), TEXT("Mgnt") };
			for (int32 i = 0; i < 6; ++i)
			{
				A->BlackWhiteWeights[i] = (float)A->Descriptor->GetNumber(Keys[i], A->BlackWhiteWeights[i]);
			}
			A->bTint = A->Descriptor->GetBool(TEXT("useTint"), false);
			A->Descriptor->GetColor(TEXT("tintColor"), A->TintColor);
		}
		else if (InKey == TEXT("phfl"))
		{
			A->Kind = EDreamPsdAdjustmentKind::PhotoFilter;
			const uint16 Version = R.U16();
			if (Version == 3)
			{
				// XYZ, fixed point with sixteen fraction bits, D50.
				const float X = R.I32() / 65536.0f / 100.0f;
				const float Y = R.I32() / 65536.0f / 100.0f;
				const float Z = R.I32() / 65536.0f / 100.0f;
				const float Rl = 3.1338561f * X - 1.6168667f * Y - 0.4906146f * Z;
				const float Gl = -0.9787684f * X + 1.9161415f * Y + 0.0334540f * Z;
				const float Bl = 0.0719453f * X - 0.2289914f * Y + 1.4052427f * Z;
				A->FilterColor = FLinearColor(DreamPsdColor::LinearToEncoded(Rl), DreamPsdColor::LinearToEncoded(Gl), DreamPsdColor::LinearToEncoded(Bl), 1.0f);
			}
			else
			{
				A->FilterColor = ReadBinaryColor(R);
			}
			A->Density = (float)R.U32();
			A->bPreserveLuminosity = R.U8() != 0;
		}
		else if (InKey == TEXT("mixr"))
		{
			A->Kind = EDreamPsdAdjustmentKind::ChannelMixer;
			R.U16();
			A->bMonochrome = R.U16() != 0;
			for (int32 Row = 0; Row < 4 && R.Remaining() >= 10; ++Row)
			{
				const int16 Red = R.I16();
				const int16 Green = R.I16();
				const int16 Blue = R.I16();
				R.I16();
				const int16 Constant = R.I16();
				A->Mixer[Row][0] = Red;
				A->Mixer[Row][1] = Green;
				A->Mixer[Row][2] = Blue;
				A->Mixer[Row][3] = Constant;
			}
		}
		else if (InKey == TEXT("nvrt"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Invert;
		}
		else if (InKey == TEXT("post"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Posterize;
			A->Level = R.U16();
		}
		else if (InKey == TEXT("thrs"))
		{
			A->Kind = EDreamPsdAdjustmentKind::Threshold;
			A->Level = R.U16();
		}
		else if (InKey == TEXT("grdm"))
		{
			A->Kind = EDreamPsdAdjustmentKind::GradientMap;
			R.U16();
			A->bReverse = R.U8() != 0;
			A->bDither = R.U8() != 0;
			A->Gradient.Name = R.Unicode();
			const uint16 Stops = R.U16();
			for (uint16 i = 0; i < Stops && R.Remaining() >= 20; ++i)
			{
				FDreamPsdColorStop Stop;
				Stop.Location = R.U32() / 4096.0f;
				Stop.Midpoint = R.U32() / 100.0f;
				R.U16();
				Stop.Color = ReadBinaryColor(R);
				A->Gradient.ColorStops.Add(Stop);
			}
			const uint16 Alphas = R.U16();
			for (uint16 i = 0; i < Alphas && R.Remaining() >= 10; ++i)
			{
				FDreamPsdAlphaStop Stop;
				Stop.Location = R.U32() / 4096.0f;
				Stop.Midpoint = R.U32() / 100.0f;
				Stop.Opacity = FMath::Clamp(R.U16() / 255.0f, 0.0f, 1.0f);
				A->Gradient.AlphaStops.Add(Stop);
			}
			R.U16();
			A->Gradient.Smoothness = FMath::Clamp(R.U16() / 4096.0f, 0.0f, 1.0f);
		}
		else if (InKey == TEXT("selc"))
		{
			A->Kind = EDreamPsdAdjustmentKind::SelectiveColor;
		}
		else if (InKey == TEXT("clrL"))
		{
			A->Kind = EDreamPsdAdjustmentKind::ColorLookup;
		}
		if (R.bOverrun)
		{
			M.Warn(FString::Printf(TEXT("its adjustment ('%s') is shorter than its kind is written"), *InKey), L.Name);
		}
		return A;
	}

	bool IsAdjustmentKey(const FString& InKey)
	{
		static const TSet<FString> Keys = {
			TEXT("brit"), TEXT("CgEd"), TEXT("levl"), TEXT("curv"), TEXT("expA"), TEXT("vibA"), TEXT("hue2"), TEXT("hue "),
			TEXT("blnc"), TEXT("blwh"), TEXT("phfl"), TEXT("mixr"), TEXT("nvrt"), TEXT("post"), TEXT("thrs"), TEXT("grdm"),
			TEXT("selc"), TEXT("clrL"),
		};
		return Keys.Contains(InKey);
	}

	void ReadPatternImage(FDreamPsdStream& S, FDreamPsdPattern& P, int32 InMode, FDreamPsdMessages& M)
	{
		// A virtual memory array list: a rectangle, then one array per channel and two more (the masks).
		S.U32();
		const uint32 Length = S.U32();
		FDreamPsdStream V = S.Sub(Length);
		const int32 Top = V.I32();
		const int32 Left = V.I32();
		const int32 Bottom = V.I32();
		const int32 Right = V.I32();
		const int32 W = Right - Left;
		const int32 H = Bottom - Top;
		const uint32 Channels = V.U32();
		if (W <= 0 || H <= 0 || W > 16384 || H > 16384)
		{
			M.Warn(FString::Printf(TEXT("pattern '%s' has a size of %d x %d; it is skipped"), *P.Name, W, H));
			return;
		}
		TArray<TArray<uint8>> Planes;
		for (uint32 c = 0; c < Channels + 2 && !V.AtEnd(); ++c)
		{
			const uint32 Written = V.U32();
			if (Written == 0)
			{
				continue;
			}
			const uint32 ArrayLength = V.U32();
			if (ArrayLength == 0)
			{
				continue;
			}
			FDreamPsdStream A = V.Sub(ArrayLength);
			const uint32 Depth = A.U32();
			A.Skip(16);
			A.U16();
			const uint8 Compression = A.U8();
			TArray<uint8> Raw;
			FString Error;
			if (!DreamPsdPixels::DecodePlane(A.Data + A.Pos, A.Remaining(), Compression, W, H, Depth == 0 ? 8 : (int32)Depth, false, Raw, Error))
			{
				M.Warn(FString::Printf(TEXT("pattern '%s', array %u: %s"), *P.Name, c, *Error));
			}
			TArray<uint8> Eight;
			DreamPsdPixels::ToEightBit(Raw, Depth == 0 ? 8 : (int32)Depth, W, H, Eight);
			Planes.Add(MoveTemp(Eight));
		}
		const int32 ColourPlanes = InMode == (int32)EDreamPsdColorMode::RGB ? 3 : 1;
		if (Planes.Num() < ColourPlanes)
		{
			M.Warn(FString::Printf(TEXT("pattern '%s' has %d planes; it is skipped"), *P.Name, Planes.Num()));
			return;
		}
		P.Image.Init(W, H, FColor::White);
		for (int32 i = 0; i < W * H; ++i)
		{
			const uint8 R = Planes[0][i];
			const uint8 G = ColourPlanes == 3 ? Planes[1][i] : R;
			const uint8 B = ColourPlanes == 3 ? Planes[2][i] : R;
			const uint8 Alpha = Planes.Num() > ColourPlanes ? Planes.Last()[i] : 255;
			P.Image.Pixels[i] = FColor(R, G, B, Alpha);
		}
	}
}

bool DreamPsdBlocks::HasWideLength(const FString& InKey)
{
	static const TSet<FString> Keys = {
		TEXT("LMsk"), TEXT("Lr16"), TEXT("Lr32"), TEXT("Layr"), TEXT("Mt16"), TEXT("Mt32"), TEXT("Mtrn"), TEXT("Alph"),
		TEXT("FMsk"), TEXT("lnk2"), TEXT("FEid"), TEXT("FXid"), TEXT("PxSD"),
	};
	return Keys.Contains(InKey);
}

void DreamPsdBlocks::ReadGradient(const FDreamPsdDescriptor& D, FDreamPsdGradient& G)
{
	G.Name = D.GetString(TEXT("Nm  "));
	G.bNoise = D.GetEnum(TEXT("GrdF")) == TEXT("ClNs");
	G.Smoothness = FMath::Clamp((float)D.GetNumber(TEXT("Intr"), 4096.0) / 4096.0f, 0.0f, 1.0f);
	if (const TArray<FDreamPsdValue>* Colours = D.GetList(TEXT("Clrs")))
	{
		for (const FDreamPsdValue& V : *Colours)
		{
			if (!V.Descriptor.IsValid())
			{
				continue;
			}
			FDreamPsdColorStop Stop;
			Stop.Location = (float)V.Descriptor->GetNumber(TEXT("Lctn")) / 4096.0f;
			Stop.Midpoint = (float)V.Descriptor->GetNumber(TEXT("Mdpn"), 50.0) / 100.0f;
			const FString Type = V.Descriptor->GetEnum(TEXT("Type"));
			if (!V.Descriptor->GetColor(TEXT("Clr "), Stop.Color))
			{
				// The foreground and background colours are not in the file: black and white, the editor's defaults.
				Stop.Color = Type == TEXT("BckC") ? FLinearColor::White : FLinearColor::Black;
			}
			G.ColorStops.Add(Stop);
		}
	}
	if (const TArray<FDreamPsdValue>* Alphas = D.GetList(TEXT("Trns")))
	{
		for (const FDreamPsdValue& V : *Alphas)
		{
			if (!V.Descriptor.IsValid())
			{
				continue;
			}
			FDreamPsdAlphaStop Stop;
			Stop.Location = (float)V.Descriptor->GetNumber(TEXT("Lctn")) / 4096.0f;
			Stop.Midpoint = (float)V.Descriptor->GetNumber(TEXT("Mdpn"), 50.0) / 100.0f;
			Stop.Opacity = (float)V.Descriptor->GetUnitFloat(TEXT("Opct"), 100.0) / 100.0f;
			G.AlphaStops.Add(Stop);
		}
	}
	if (G.bNoise || G.ColorStops.Num() == 0)
	{
		G.ColorStops = { { 0.0f, 0.5f, FLinearColor::Black }, { 1.0f, 0.5f, FLinearColor::White } };
	}
	G.ColorStops.StableSort([](const FDreamPsdColorStop& A, const FDreamPsdColorStop& B) { return A.Location < B.Location; });
	G.AlphaStops.StableSort([](const FDreamPsdAlphaStop& A, const FDreamPsdAlphaStop& B) { return A.Location < B.Location; });
}

void DreamPsdBlocks::ReadGradientFill(const FDreamPsdDescriptor& D, FDreamPsdGradientFill& F)
{
	if (const FDreamPsdDescriptor* Gradient = D.GetDescriptor(TEXT("Grad")))
	{
		ReadGradient(*Gradient, F.Gradient);
	}
	F.Style = D.GetEnum(TEXT("Type"), F.Style);
	F.Angle = (float)D.GetUnitFloat(TEXT("Angl"), F.Angle);
	F.Scale = (float)D.GetUnitFloat(TEXT("Scl "), 100.0) / 100.0f;
	F.bReverse = D.GetBool(TEXT("Rvrs"), false);
	F.bDither = D.GetBool(TEXT("Dthr"), false);
	F.bAlignWithLayer = D.GetBool(TEXT("Algn"), true);
	if (const FDreamPsdDescriptor* Offset = D.GetDescriptor(TEXT("Ofst")))
	{
		F.Offset = FVector2D(Offset->GetUnitFloat(TEXT("Hrzn")), Offset->GetUnitFloat(TEXT("Vrtc")));
	}
}

void DreamPsdBlocks::ReadPatternFill(const FDreamPsdDescriptor& D, FDreamPsdPatternFill& F)
{
	if (const FDreamPsdDescriptor* Pattern = D.GetDescriptor(TEXT("Ptrn")))
	{
		F.PatternName = Pattern->GetString(TEXT("Nm  "));
		F.PatternId = Pattern->GetString(TEXT("Idnt"));
	}
	F.Scale = (float)D.GetUnitFloat(TEXT("Scl "), 100.0) / 100.0f;
	F.bAlignWithLayer = D.GetBool(TEXT("Algn"), true);
	if (const FDreamPsdDescriptor* Phase = D.GetDescriptor(TEXT("phase")))
	{
		F.Phase = FVector2D(Phase->GetNumber(TEXT("Hrzn")), Phase->GetNumber(TEXT("Vrtc")));
	}
}

void DreamPsdBlocks::ReadEffects(const FDreamPsdDescriptor& D, const FDreamPsdDocument& InDocument, FDreamPsdLayerEffects& Out)
{
	Out = FDreamPsdLayerEffects();
	Out.bPresent = true;
	Out.Descriptor = MakeShared<FDreamPsdDescriptor>(D);
	Out.bMasterSwitch = D.GetBool(TEXT("masterFXSwitch"), true);
	Out.Scale = (float)D.GetUnitFloat(TEXT("Scl "), 100.0) / 100.0f;
	struct FKey { const TCHAR* Single; const TCHAR* Multi; EDreamPsdEffectKind Kind; };
	// In the order the editor draws them, bottom first.
	static const FKey Keys[] = {
		{ TEXT("DrSh"), TEXT("dropShadowMulti"), EDreamPsdEffectKind::DropShadow },
		{ TEXT("OrGl"), nullptr, EDreamPsdEffectKind::OuterGlow },
		{ TEXT("ebbl"), nullptr, EDreamPsdEffectKind::BevelEmboss },
		{ TEXT("ChFX"), nullptr, EDreamPsdEffectKind::Satin },
		{ TEXT("IrSh"), TEXT("innerShadowMulti"), EDreamPsdEffectKind::InnerShadow },
		{ TEXT("IrGl"), nullptr, EDreamPsdEffectKind::InnerGlow },
		{ TEXT("patternFill"), nullptr, EDreamPsdEffectKind::PatternOverlay },
		{ TEXT("GrFl"), TEXT("gradientFillMulti"), EDreamPsdEffectKind::GradientOverlay },
		{ TEXT("SoFi"), TEXT("solidFillMulti"), EDreamPsdEffectKind::ColorOverlay },
		{ TEXT("FrFX"), TEXT("frameFXMulti"), EDreamPsdEffectKind::Stroke },
	};
	for (const FKey& Key : Keys)
	{
		TArray<const FDreamPsdDescriptor*> Found;
		if (Key.Multi != nullptr)
		{
			if (const TArray<FDreamPsdValue>* List = D.GetList(Key.Multi))
			{
				for (const FDreamPsdValue& V : *List)
				{
					if (V.Descriptor.IsValid())
					{
						Found.Add(V.Descriptor.Get());
					}
				}
			}
		}
		if (Found.Num() == 0)
		{
			if (const FDreamPsdDescriptor* Single = D.GetDescriptor(Key.Single))
			{
				Found.Add(Single);
			}
		}
		for (const FDreamPsdDescriptor* Effect : Found)
		{
			if (Effect->Has(TEXT("present")) && !Effect->GetBool(TEXT("present")))
			{
				continue;
			}
			FDreamPsdEffect E;
			DreamPsdBlocksLocal::ReadEffect(*Effect, Key.Kind, InDocument, E);
			Out.Effects.Add(MoveTemp(E));
		}
	}
}

void DreamPsdBlocks::ReadTextEngine(FDreamPsdText& T, FDreamPsdMessages& M, const FString& InLayer)
{
	if (!T.TextDescriptor.IsValid())
	{
		return;
	}
	const FDreamPsdValue* Raw = T.TextDescriptor->Find(TEXT("EngineData"));
	if (Raw == nullptr || Raw->Type != EDreamPsdValueType::RawData)
	{
		M.Warn(TEXT("its text has no engine data: the text is read, its styles are the defaults"), InLayer);
		T.StyleRuns.Add(FDreamPsdTextStyleRun());
		T.StyleRuns.Last().Length = T.Text.Len();
		return;
	}
	TSharedPtr<FDreamPsdEngineValue> Root = MakeShared<FDreamPsdEngineValue>();
	FString Error;
	if (!DreamPsdEngineData::Parse(Raw->Raw, *Root, Error))
	{
		M.Warn(FString::Printf(TEXT("its text's engine data does not read (%s): its styles are the defaults"), *Error), InLayer);
		T.StyleRuns.Add(FDreamPsdTextStyleRun());
		T.StyleRuns.Last().Length = T.Text.Len();
		return;
	}
	T.EngineData = Root;
	const FDreamPsdEngineValue* Engine = Root->Find(TEXT("EngineDict"));
	const FDreamPsdEngineValue* Resources = Root->Find(TEXT("ResourceDict"));
	if (Resources == nullptr)
	{
		Resources = Root->Find(TEXT("DocumentResources"));
	}
	if (Engine != nullptr)
	{
		if (const FDreamPsdEngineValue* Editor = Engine->Find(TEXT("Editor")))
		{
			const FDreamPsdEngineValue* Text = Editor->Find(TEXT("Text"));
			if (Text != nullptr && Text->Type == EDreamPsdEngineType::String)
			{
				T.Text = Text->String;
			}
		}
	}
	// The engine ends every text with a paragraph break of its own.
	while (T.Text.EndsWith(TEXT("\r")))
	{
		T.Text.LeftChopInline(1);
	}
	T.Text.ReplaceInline(TEXT("\x03"), TEXT("\r"));

	TArray<FString> Fonts;
	const FDreamPsdEngineValue* NormalStyle = nullptr;
	const FDreamPsdEngineValue* NormalParagraph = nullptr;
	if (Resources != nullptr)
	{
		if (const FDreamPsdEngineValue* FontSet = Resources->Find(TEXT("FontSet")))
		{
			for (const FDreamPsdEngineValue& Font : FontSet->Items)
			{
				Fonts.Add(Font.GetString(TEXT("Name")));
			}
		}
		const FDreamPsdEngineValue* Sheets = Resources->Find(TEXT("StyleSheetSet"));
		const int32 Normal = (int32)Resources->GetNumber(TEXT("TheNormalStyleSheet"), 0);
		if (Sheets != nullptr && Sheets->Items.IsValidIndex(Normal))
		{
			NormalStyle = Sheets->Items[Normal].Find(TEXT("StyleSheetData"));
		}
		const FDreamPsdEngineValue* Paragraphs = Resources->Find(TEXT("ParagraphSheetSet"));
		const int32 NormalP = (int32)Resources->GetNumber(TEXT("TheNormalParagraphSheet"), 0);
		if (Paragraphs != nullptr && Paragraphs->Items.IsValidIndex(NormalP))
		{
			NormalParagraph = Paragraphs->Items[NormalP].Find(TEXT("Properties"));
		}
	}
	const int32 TextLength = T.Text.Len();
	if (Engine != nullptr)
	{
		const FDreamPsdEngineValue* Runs = Engine->FindPath(TEXT("StyleRun/RunArray"));
		const FDreamPsdEngineValue* Lengths = Engine->FindPath(TEXT("StyleRun/RunLengthArray"));
		int32 Start = 0;
		if (Runs != nullptr)
		{
			for (int32 i = 0; i < Runs->Items.Num(); ++i)
			{
				const FDreamPsdEngineValue* Data = Runs->Items[i].FindPath(TEXT("StyleSheet/StyleSheetData"));
				FDreamPsdTextStyleRun Style = DreamPsdBlocksLocal::ReadStyle(NormalStyle, Data, Fonts);
				Style.Start = Start;
				const int32 Length = Lengths != nullptr && Lengths->Items.IsValidIndex(i) ? (int32)Lengths->Items[i].Number : TextLength - Start;
				Style.Length = FMath::Clamp(Length, 0, FMath::Max(0, TextLength - Start));
				Start += Length;
				if (Style.Length > 0 || T.StyleRuns.Num() == 0)
				{
					T.StyleRuns.Add(Style);
				}
			}
		}
		const FDreamPsdEngineValue* PRuns = Engine->FindPath(TEXT("ParagraphRun/RunArray"));
		const FDreamPsdEngineValue* PLengths = Engine->FindPath(TEXT("ParagraphRun/RunLengthArray"));
		Start = 0;
		if (PRuns != nullptr)
		{
			for (int32 i = 0; i < PRuns->Items.Num(); ++i)
			{
				const FDreamPsdEngineValue* Properties = PRuns->Items[i].FindPath(TEXT("ParagraphSheet/Properties"));
				FDreamPsdTextParagraphRun Paragraph;
				const FDreamPsdEngineValue* Justification = Properties != nullptr ? Properties->Find(TEXT("Justification")) : nullptr;
				Paragraph.Justification = Justification != nullptr ? (int32)Justification->Number
					: (NormalParagraph != nullptr ? (int32)NormalParagraph->GetNumber(TEXT("Justification"), 0) : 0);
				Paragraph.Start = Start;
				const int32 Length = PLengths != nullptr && PLengths->Items.IsValidIndex(i) ? (int32)PLengths->Items[i].Number : TextLength - Start;
				Paragraph.Length = FMath::Clamp(Length, 0, FMath::Max(0, TextLength - Start));
				Start += Length;
				T.ParagraphRuns.Add(Paragraph);
			}
		}
		if (const FDreamPsdEngineValue* Shape = Engine->FindPath(TEXT("Rendered/Shapes/Children/0")))
		{
			T.bBoxText = (int32)Shape->GetNumber(TEXT("ShapeType"), 0) == 1;
			const FDreamPsdEngineValue* Box = Shape->FindPath(TEXT("Cookie/Photoshop/BoxBounds"));
			if (T.bBoxText && Box != nullptr && Box->Items.Num() >= 4)
			{
				T.BoxBounds = FBox2D(FVector2D(Box->Items[0].Number, Box->Items[1].Number), FVector2D(Box->Items[2].Number, Box->Items[3].Number));
			}
		}
	}
	if (T.StyleRuns.Num() == 0)
	{
		FDreamPsdTextStyleRun Style = DreamPsdBlocksLocal::ReadStyle(NormalStyle, nullptr, Fonts);
		Style.Length = TextLength;
		T.StyleRuns.Add(Style);
	}
	if (T.ParagraphRuns.Num() == 0)
	{
		FDreamPsdTextParagraphRun Paragraph;
		Paragraph.Length = TextLength;
		Paragraph.Justification = NormalParagraph != nullptr ? (int32)NormalParagraph->GetNumber(TEXT("Justification"), 0) : 0;
		T.ParagraphRuns.Add(Paragraph);
	}
}

void DreamPsdBlocks::ParsePatterns(FDreamPsdStream& S, FDreamPsdDocument& Doc, FDreamPsdMessages& M)
{
	while (S.Remaining() >= 4)
	{
		const uint32 Length = S.U32();
		if (Length == 0 || Length > (uint64)S.Remaining())
		{
			return;
		}
		FDreamPsdStream P = S.Sub(Length);
		if (Length % 4 != 0)
		{
			S.Skip(FMath::Min<int64>(4 - Length % 4, S.Remaining()));
		}
		FDreamPsdPattern Pattern;
		P.U32();
		const uint32 Mode = P.U32();
		P.I16();
		P.I16();
		Pattern.Name = P.Unicode();
		Pattern.Id = P.Pascal(1);
		if (Mode == (uint32)EDreamPsdColorMode::Indexed)
		{
			P.Skip(768);
		}
		DreamPsdBlocksLocal::ReadPatternImage(P, Pattern, (int32)Mode, M);
		if (!Pattern.Image.IsEmpty())
		{
			Doc.Patterns.Add(MoveTemp(Pattern));
		}
	}
}

void DreamPsdBlocks::ParseLayerBlock(const FString& InKey, FDreamPsdStream& S, FDreamPsdLayer& L, const FDreamPsdDocument& Doc, FDreamPsdMessages& M)
{
	FString Error;
	if (InKey == TEXT("luni"))
	{
		const FString Name = S.Unicode();
		if (!Name.IsEmpty())
		{
			L.Name = Name;
		}
	}
	else if (InKey == TEXT("lsct") || InKey == TEXT("lsdk"))
	{
		L.SectionType = (int32)S.U32();
		if (S.Remaining() >= 8)
		{
			S.Key4();
			const FString Blend = S.Key4();
			if (!Blend.IsEmpty() && (L.SectionType == 1 || L.SectionType == 2))
			{
				L.BlendKey = Blend;
			}
		}
	}
	else if (InKey == TEXT("lyid"))
	{
		L.LayerId = S.I32();
	}
	else if (InKey == TEXT("iOpa"))
	{
		L.FillOpacity = S.U8();
	}
	else if (InKey == TEXT("clbl"))
	{
		L.bBlendClippedElements = S.U8() != 0;
	}
	else if (InKey == TEXT("infx"))
	{
		L.bBlendInteriorElements = S.U8() != 0;
	}
	else if (InKey == TEXT("knko"))
	{
		L.Knockout = S.U8();
	}
	else if (InKey == TEXT("lclr"))
	{
		L.ColorTag = S.U16();
	}
	else if (InKey == TEXT("TySh"))
	{
		DreamPsdBlocksLocal::ReadText(S, L, M);
	}
	else if (InKey == TEXT("SoCo") || InKey == TEXT("GdFl") || InKey == TEXT("PtFl"))
	{
		DreamPsdBlocksLocal::ReadFill(InKey, S, L, M);
	}
	else if (InKey == TEXT("vscg"))
	{
		const FString Kind = S.Key4();
		if (L.Fill.Kind == FDreamPsdFill::EKind::None)
		{
			DreamPsdBlocksLocal::ReadFill(Kind, S, L, M);
		}
	}
	else if (InKey == TEXT("vmsk") || InKey == TEXT("vsms"))
	{
		DreamPsdBlocksLocal::ReadVectorMask(S, L, Doc);
	}
	else if (InKey == TEXT("vogk"))
	{
		DreamPsdBlocksLocal::ReadLiveShape(S, L, M);
	}
	else if (InKey == TEXT("vstk"))
	{
		DreamPsdBlocksLocal::ReadStroke(S, L, M);
	}
	else if (InKey == TEXT("lfx2") || InKey == TEXT("lmfx"))
	{
		S.U32();
		FDreamPsdDescriptor D;
		if (DreamPsdDescriptor::ReadVersioned(S, D, Error))
		{
			// 'lmfx' holds every effect of a kind used more than once; 'lfx2' the first of each. The fuller one wins.
			if (InKey == TEXT("lmfx") || !L.Effects.bPresent)
			{
				ReadEffects(D, Doc, L.Effects);
			}
		}
		else
		{
			M.Warn(FString::Printf(TEXT("its layer effects do not read (%s)"), *Error), L.Name);
		}
	}
	else if (InKey == TEXT("lrFX"))
	{
		if (!L.Effects.bPresent)
		{
			M.Warn(TEXT("it has layer effects only in the oldest form ('lrFX'), which is not read; save the file again to write them in the current one"), L.Name);
		}
	}
	else if (DreamPsdBlocksLocal::IsAdjustmentKey(InKey))
	{
		TSharedPtr<FDreamPsdAdjustment> Adjustment = DreamPsdBlocksLocal::ReadAdjustment(InKey, S, L, M);
		// 'CgEd' is the current form of 'brit'; a layer may carry both.
		if (Adjustment.IsValid() && (!L.Adjustment.IsValid() || InKey == TEXT("CgEd")))
		{
			L.Adjustment = Adjustment;
		}
	}
	else if (InKey == TEXT("SoLd") || InKey == TEXT("SoLE") || InKey == TEXT("PlLd"))
	{
		L.bSmartObject = true;
	}
	else if (InKey == TEXT("artb") || InKey == TEXT("artd") || InKey == TEXT("abdd"))
	{
		FDreamPsdDescriptor D;
		if (DreamPsdDescriptor::ReadVersioned(S, D, Error))
		{
			if (const FDreamPsdDescriptor* Rect = D.GetDescriptor(TEXT("artboardRect")))
			{
				L.bArtboard = true;
				L.ArtboardRect = FIntRect(FMath::RoundToInt(Rect->GetNumber(TEXT("Left"))), FMath::RoundToInt(Rect->GetNumber(TEXT("Top "))),
					FMath::RoundToInt(Rect->GetNumber(TEXT("Rght"))), FMath::RoundToInt(Rect->GetNumber(TEXT("Btom"))));
			}
		}
	}
	else
	{
		FDreamPsdTaggedBlock Block;
		Block.Key = InKey;
		S.Bytes(Block.Data, S.Remaining());
		L.OtherBlocks.Add(MoveTemp(Block));
	}
}
