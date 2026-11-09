// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPsdImportCommandlet.h"
#include "Import/DreamPsdImporter.h"
#include "Psd/DreamPsdRaster.h"
#include "Psd/DreamPsdReader.h"
#include "Psd/DreamPsdWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamPsdCommandlet, Log, All);

UDreamPsdImportCommandlet::UDreamPsdImportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace DreamPsdImportCommandletLocal
{
	FDreamPsdWriteLayer Layer(const FString& InName, int32 InId, const FIntRect& InRect, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
	{
		FDreamPsdWriteLayer L;
		L.Name = InName;
		L.LayerId = InId;
		L.Rect = InRect;
		L.Pixels.Init(InRect.Width(), InRect.Height(), FColor(0, 0, 0, 0));
		for (int32 y = 0; y < InRect.Height(); ++y)
		{
			for (int32 x = 0; x < InRect.Width(); ++x)
			{
				L.Pixels.Pixels[y * InRect.Width() + x] = InColourAt(x, y);
			}
		}
		return L;
	}

	FDreamPsdPath RoundedRect(const FBox2D& B, double R)
	{
		const double K = 0.5522847498 * R;
		const double L = B.Min.X, T = B.Min.Y, Ri = B.Max.X, Bo = B.Max.Y;
		FDreamPsdSubpath S;
		auto Knot = [&S](FVector2D In, FVector2D A, FVector2D Out) { S.Knots.Add({ In, A, Out, true }); };
		Knot({ L + R - K, T }, { L + R, T }, { L + R, T });
		Knot({ Ri - R, T }, { Ri - R, T }, { Ri - R + K, T });
		Knot({ Ri, T + R - K }, { Ri, T + R }, { Ri, T + R });
		Knot({ Ri, Bo - R }, { Ri, Bo - R }, { Ri, Bo - R + K });
		Knot({ Ri - R + K, Bo }, { Ri - R, Bo }, { Ri - R, Bo });
		Knot({ L + R, Bo }, { L + R, Bo }, { L + R - K, Bo });
		Knot({ L, Bo - R + K }, { L, Bo - R }, { L, Bo - R });
		Knot({ L, T + R }, { L, T + R }, { L, T + R - K });
		FDreamPsdPath P;
		P.Subpaths.Add(S);
		return P;
	}

	/**
	 * An original sample document that shows what the importer does: a group fading as one, a rounded panel with a
	 * stroke, a text, a disc with stripes clipped to it, a nine-sliced button, a masked picture, a screen-blended glow,
	 * an adjustment layer and a drop shadow (read, listed, not imported yet), a hidden layer and a `!skip` one.
	 */
	TArray<uint8> MakeSample()
	{
		FDreamPsdWriteDocument Doc;
		Doc.Width = 512;
		Doc.Height = 320;
		auto Disc = [](int32 X, int32 Y, FVector2D C, double R, FColor Colour)
		{
			const double D = (FVector2D(X + 0.5, Y + 0.5) - C).Size();
			Colour.A = (uint8)FMath::RoundToInt(Colour.A * FMath::Clamp(R - D + 0.5, 0.0, 1.0));
			return Colour;
		};
		Doc.Layers.Add(Layer(TEXT("Background"), 1, FIntRect(0, 0, 512, 320), [](int32, int32 Y)
		{
			return FColor((uint8)(18 + Y / 20), (uint8)(22 + Y / 16), (uint8)(38 + Y / 8), 255);
		}));
		{
			FDreamPsdWriteLayer D = Layer(TEXT("</Layer group>"), 2, FIntRect(), [](int32, int32) { return FColor(); });
			D.SectionType = 3;
			Doc.Layers.Add(D);
		}
		{
			const FBox2D Box(FVector2D(32, 32), FVector2D(480, 288));
			const FDreamPsdPath Path = RoundedRect(Box, 16.0);
			const FIntRect Rect(32, 32, 480, 288);
			const FDreamPsdGray Coverage = DreamPsdRaster::FillPath(Path, Rect);
			FDreamPsdWriteLayer Panel = Layer(TEXT("Panel"), 3, Rect, [&Coverage](int32 X, int32 Y) { return FColor(40, 44, 60, Coverage.At(X, Y)); });
			Panel.bVectorMask = true;
			Panel.VectorPath = Path;
			Panel.LiveShape.Kind = EDreamPsdLiveShapeKind::RoundedRectangle;
			Panel.LiveShape.Box = Box;
			Panel.LiveShape.Radii = FVector4(16, 16, 16, 16);
			Panel.Fill.Kind = FDreamPsdFill::EKind::Solid;
			Panel.Fill.Color = FLinearColor(40 / 255.0f, 44 / 255.0f, 60 / 255.0f);
			Panel.Stroke.bPresent = true;
			Panel.Stroke.bEnabled = true;
			Panel.Stroke.Width = 2.0f;
			Panel.Stroke.Alignment = 0;
			Panel.Stroke.Color = FLinearColor(90 / 255.0f, 100 / 255.0f, 140 / 255.0f);
			Doc.Layers.Add(Panel);
		}
		{
			FDreamPsdWriteLayer Title = Layer(TEXT("Title"), 4, FIntRect(64, 52, 220, 84), [](int32 X, int32 Y) { return FColor(255, 255, 255, (X / 6) % 3 == 0 ? 0 : 230); });
			Title.Text = MakeShared<FDreamPsdText>();
			Title.Text->Text = TEXT("Settings");
			Title.Text->Transform[4] = 64.0;
			Title.Text->Transform[5] = 80.0;
			FDreamPsdTextStyleRun Run;
			Run.FontName = TEXT("Arial-BoldMT");
			Run.FontSize = 28.0f;
			Run.Color = FLinearColor::White;
			Run.Length = 8;
			Title.Text->StyleRuns.Add(Run);
			Title.Text->ParagraphRuns.Add(FDreamPsdTextParagraphRun());
			Doc.Layers.Add(Title);
		}
		Doc.Layers.Add(Layer(TEXT("Icon"), 5, FIntRect(64, 112, 112, 160), [&Disc](int32 X, int32 Y) { return Disc(X, Y, FVector2D(24, 24), 24.0, FColor(240, 140, 40, 255)); }));
		{
			FDreamPsdWriteLayer Shine = Layer(TEXT("Shine"), 6, FIntRect(60, 108, 116, 164), [](int32 X, int32 Y) { return ((X + Y) / 6) % 2 == 0 ? FColor(255, 255, 255, 120) : FColor(0, 0, 0, 0); });
			Shine.bClipping = true;
			Doc.Layers.Add(Shine);
		}
		{
			FDreamPsdWriteLayer Card = Layer(TEXT("Card"), 7, FIntRect(), [](int32, int32) { return FColor(); });
			Card.SectionType = 1;
			Card.Opacity = 230;
			Card.BlendKey = TEXT("norm");
			Doc.Layers.Add(Card);
		}
		{
			const FIntRect Rect(64, 200, 224, 248);
			const FDreamPsdGray Outer = DreamPsdRaster::FillPath(RoundedRect(FBox2D(FVector2D(64, 200), FVector2D(224, 248)), 8.0), Rect);
			const FDreamPsdGray Inner = DreamPsdRaster::FillPath(RoundedRect(FBox2D(FVector2D(66, 202), FVector2D(222, 246)), 6.0), Rect);
			Doc.Layers.Add(Layer(TEXT("Play @Button #9(8,8,8,8)"), 8, Rect, [&Outer, &Inner](int32 X, int32 Y)
			{
				const uint8 A = Outer.At(X, Y);
				return Inner.At(X, Y) > 128 ? FColor(70, 120, 230, A) : FColor(150, 190, 255, A);
			}));
		}
		{
			FDreamPsdWriteLayer Avatar = Layer(TEXT("Avatar"), 9, FIntRect(300, 110, 420, 230), [](int32 X, int32 Y) { return ((X / 12) + (Y / 12)) % 2 == 0 ? FColor(220, 200, 160, 255) : FColor(120, 90, 70, 255); });
			Avatar.bMask = true;
			Avatar.MaskRect = Avatar.Rect;
			Avatar.MaskPixels.Init(120, 120, 0);
			for (int32 y = 0; y < 120; ++y)
			{
				for (int32 x = 0; x < 120; ++x)
				{
					const double D = (FVector2D(x + 0.5, y + 0.5) - FVector2D(60, 60)).Size();
					Avatar.MaskPixels.Pixels[y * 120 + x] = (uint8)FMath::RoundToInt(255.0 * FMath::Clamp((60.0 - D) / 6.0, 0.0, 1.0));
				}
			}
			Doc.Layers.Add(Avatar);
		}
		{
			FDreamPsdWriteLayer Glow = Layer(TEXT("Glow"), 10, FIntRect(320, 40, 480, 120), [&Disc](int32 X, int32 Y) { return Disc(X, Y, FVector2D(80, 40), 38.0, FColor(255, 200, 120, 160)); });
			Glow.BlendKey = TEXT("scrn");
			Doc.Layers.Add(Glow);
		}
		{
			FDreamPsdWriteLayer Levels = Layer(TEXT("Warm levels"), 11, FIntRect(), [](int32, int32) { return FColor(); });
			Levels.ExtraBlocks.Add(DreamPsdWriter::MakeLevelsBlock(8, 240, 1.1f));
			Doc.Layers.Add(Levels);
		}
		{
			FDreamPsdWriteLayer Shadowed = Layer(TEXT("Badge"), 12, FIntRect(250, 200, 290, 240), [&Disc](int32 X, int32 Y) { return Disc(X, Y, FVector2D(20, 20), 18.0, FColor(90, 220, 140, 255)); });
			FDreamPsdDescriptor Shadow;
			Shadow.ClassId = TEXT("DrSh");
			Shadow.Items.Emplace(TEXT("enab"), DreamPsdDescriptor::MakeBool(true));
			Shadow.Items.Emplace(TEXT("Md  "), DreamPsdDescriptor::MakeEnum(TEXT("BlnM"), TEXT("Mltp")));
			Shadow.Items.Emplace(TEXT("Clr "), DreamPsdDescriptor::MakeDescriptor(DreamPsdDescriptor::MakeColor(FLinearColor::Black)));
			Shadow.Items.Emplace(TEXT("Opct"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Prc"), 60.0));
			Shadow.Items.Emplace(TEXT("Dstn"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), 4.0));
			Shadow.Items.Emplace(TEXT("blur"), DreamPsdDescriptor::MakeUnitFloat(TEXT("#Pxl"), 6.0));
			FDreamPsdDescriptor Effects;
			Effects.ClassId = TEXT("null");
			Effects.Items.Emplace(TEXT("masterFXSwitch"), DreamPsdDescriptor::MakeBool(true));
			Effects.Items.Emplace(TEXT("DrSh"), DreamPsdDescriptor::MakeDescriptor(Shadow));
			Shadowed.ExtraBlocks.Add(DreamPsdWriter::MakeEffectsBlock(Effects));
			Doc.Layers.Add(Shadowed);
		}
		{
			FDreamPsdWriteLayer Hidden = Layer(TEXT("Pressed state"), 13, FIntRect(64, 200, 224, 248), [](int32, int32) { return FColor(40, 80, 180, 255); });
			Hidden.bVisible = false;
			Doc.Layers.Add(Hidden);
		}
		{
			FDreamPsdWriteLayer Notes = Layer(TEXT("Notes !skip"), 14, FIntRect(380, 260, 500, 300), [](int32, int32) { return FColor(255, 255, 0, 255); });
			Notes.bVisible = false;
			Doc.Layers.Add(Notes);
		}
		// The merged image: the visible layers over each other in order, near enough for a thumbnail.
		Doc.Composite.Init(Doc.Width, Doc.Height, FColor(0, 0, 0, 0));
		for (const FDreamPsdWriteLayer& L : Doc.Layers)
		{
			if (!L.bVisible || L.Pixels.IsEmpty() || L.bClipping)
			{
				continue;
			}
			for (int32 y = 0; y < L.Rect.Height(); ++y)
			{
				for (int32 x = 0; x < L.Rect.Width(); ++x)
				{
					const FColor S = L.Pixels.At(x, y);
					const float A = S.A / 255.0f * (L.bMask ? L.MaskPixels.At(x, y) / 255.0f : 1.0f);
					FColor& D = Doc.Composite.Pixels[(L.Rect.Min.Y + y) * Doc.Width + L.Rect.Min.X + x];
					D.R = (uint8)FMath::RoundToInt(FMath::Lerp((float)D.R, (float)S.R, A));
					D.G = (uint8)FMath::RoundToInt(FMath::Lerp((float)D.G, (float)S.G, A));
					D.B = (uint8)FMath::RoundToInt(FMath::Lerp((float)D.B, (float)S.B, A));
					D.A = (uint8)FMath::RoundToInt(255.0f * (A + D.A / 255.0f * (1.0f - A)));
				}
			}
		}
		TArray<uint8> Bytes;
		DreamPsdWriter::Write(Doc, Bytes);
		return Bytes;
	}

	const TCHAR* KindName(EDreamPsdLayerKind InKind)
	{
		switch (InKind)
		{
		case EDreamPsdLayerKind::Pixel: return TEXT("pixels");
		case EDreamPsdLayerKind::GroupStart: return TEXT("group");
		case EDreamPsdLayerKind::GroupEnd: return TEXT("group end");
		case EDreamPsdLayerKind::Text: return TEXT("text");
		case EDreamPsdLayerKind::Shape: return TEXT("shape");
		case EDreamPsdLayerKind::Fill: return TEXT("fill");
		case EDreamPsdLayerKind::Adjustment: return TEXT("adjustment");
		default: return TEXT("?");
		}
	}

	FString CheckComposite(const FDreamPsdDocument& D);

	FString Dump(const FDreamPsdDocument& D)
	{
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("%s %d x %d, %d bits, mode %d, %d channels, %d layers, merged image %s"),
			D.IsLarge() ? TEXT("PSB") : TEXT("PSD"), D.Width, D.Height, D.Depth, (int32)D.ColorMode, D.ChannelCount, D.Layers.Num(),
			D.Composite.IsEmpty() ? TEXT("none") : TEXT("present")));
		for (const FDreamPsdLayer& L : D.Layers)
		{
			Lines.Add(FString::Printf(TEXT("  [%d] id %d '%s' %s rect (%d,%d)-(%d,%d) blend %s opacity %d fill %d%s%s%s%s%s%s"),
				L.RecordIndex, L.LayerId, *L.Name, KindName(L.Kind), L.Rect.Min.X, L.Rect.Min.Y, L.Rect.Max.X, L.Rect.Max.Y,
				*DreamPsdReader::DescribeBlendKey(L.BlendKey), L.Opacity, L.FillOpacity, L.bClipping ? TEXT(" clipped") : TEXT(""),
				L.bVisible ? TEXT("") : TEXT(" hidden"), L.Mask.bPresent ? TEXT(" mask") : TEXT(""), L.VectorMask.bPresent ? TEXT(" vector-mask") : TEXT(""),
				L.Effects.HasEnabled() ? TEXT(" effects") : TEXT(""), L.bSmartObject ? TEXT(" smart-object") : TEXT("")));
			Lines.Add(FString::Printf(TEXT("      blocks: %s"), *FString::Join(L.BlockKeys, TEXT(" "))));
			if (L.Text.IsValid())
			{
				Lines.Add(FString::Printf(TEXT("      text: \"%s\" runs %d box %d"), *L.Text->Text.Replace(TEXT("\r"), TEXT("\\r")), L.Text->StyleRuns.Num(), L.Text->bBoxText ? 1 : 0));
			}
			if (L.Effects.bPresent && L.Effects.Descriptor.IsValid())
			{
				Lines.Add(TEXT("      effects: ") + L.Effects.Descriptor->Dump(6));
			}
		}
		Lines.Add(CheckComposite(D));
		return FString::Join(Lines, TEXT("\n"));
	}

	/**
	 * When every visible layer is a plain pixel layer drawn normally (no groups, masks, clipping or effects), its layers
	 * put over each other as the editor does -- straight colour, encoded values -- must give the merged image the file
	 * carries: a check of the reader's pixels against the writer's own picture.
	 */
	FString CheckComposite(const FDreamPsdDocument& D)
	{
		if (D.Composite.IsEmpty())
		{
			return TEXT("composite check: the file has no merged image");
		}
		for (const FDreamPsdLayer& L : D.Layers)
		{
			if (!L.bVisible)
			{
				continue;
			}
			if (L.Kind != EDreamPsdLayerKind::Pixel || L.BlendKey != TEXT("norm") || L.bClipping || L.Mask.bPresent || L.VectorMask.bPresent || L.Effects.HasEnabled())
			{
				return FString::Printf(TEXT("composite check: skipped -- '%s' is not a plain normal pixel layer"), *L.Name);
			}
		}
		TArray<FLinearColor> Canvas;
		Canvas.Init(FLinearColor(0, 0, 0, 0), D.Width * D.Height);
		for (const FDreamPsdLayer& L : D.Layers)
		{
			if (!L.bVisible || !L.HasPixels())
			{
				continue;
			}
			const float Opacity = L.Opacity / 255.0f * L.FillOpacity / 255.0f;
			for (int32 y = 0; y < L.Rect.Height(); ++y)
			{
				const int32 DocY = L.Rect.Min.Y + y;
				for (int32 x = 0; x < L.Rect.Width() && DocY >= 0 && DocY < D.Height; ++x)
				{
					const int32 DocX = L.Rect.Min.X + x;
					if (DocX < 0 || DocX >= D.Width)
					{
						continue;
					}
					const FColor S = L.Pixels.At(x, y);
					const float A = S.A / 255.0f * Opacity;
					FLinearColor& B = Canvas[DocY * D.Width + DocX];
					const float Out = A + B.A * (1.0f - A);
					if (Out > 0.0f)
					{
						B.R = (S.R / 255.0f * A + B.R * B.A * (1.0f - A)) / Out;
						B.G = (S.G / 255.0f * A + B.G * B.A * (1.0f - A)) / Out;
						B.B = (S.B / 255.0f * A + B.B * B.A * (1.0f - A)) / Out;
					}
					B.A = Out;
				}
			}
		}
		int32 Over = 0, Worst = 0, Counted = 0;
		for (int32 i = 0; i < Canvas.Num(); ++i)
		{
			const FColor M = D.Composite.Pixels[i];
			const FLinearColor& C = Canvas[i];
			if (M.A < 8 && C.A < 8.0f / 255.0f)
			{
				continue;
			}
			++Counted;
			const int32 Diff = FMath::Max(FMath::Max3(FMath::Abs(FMath::RoundToInt(C.R * 255.0f) - M.R), FMath::Abs(FMath::RoundToInt(C.G * 255.0f) - M.G),
				FMath::Abs(FMath::RoundToInt(C.B * 255.0f) - M.B)), FMath::Abs(FMath::RoundToInt(C.A * 255.0f) - M.A));
			Worst = FMath::Max(Worst, Diff);
			Over += Diff > 2 ? 1 : 0;
		}
		return FString::Printf(TEXT("composite check: the visible layers over each other against the merged image -- %d of %d pixels differ by more than 2, the worst by %d"),
			Over, Counted, Worst);
	}
}

int32 UDreamPsdImportCommandlet::Main(const FString& InParams)
{
	FString SampleFile;
	if (FParse::Value(*InParams, TEXT("WriteSample="), SampleFile))
	{
		SampleFile = FPaths::ConvertRelativePathToFull(SampleFile);
		const bool bWritten = FFileHelper::SaveArrayToFile(DreamPsdImportCommandletLocal::MakeSample(), *SampleFile);
		UE_LOG(LogDreamPsdCommandlet, Display, TEXT("%s the sample document %s"), bWritten ? TEXT("Wrote") : TEXT("Could not write"), *SampleFile);
		return bWritten ? 0 : 1;
	}
	FDreamPsdImportRequest Request;
	FParse::Value(*InParams, TEXT("Psd="), Request.SourceFile);
	if (Request.SourceFile.IsEmpty())
	{
		UE_LOG(LogDreamPsdCommandlet, Error, TEXT("-Psd=<file> or -WriteSample=<file> is required"));
		return 1;
	}
	Request.SourceFile = FPaths::ConvertRelativePathToFull(Request.SourceFile);
	if (FParse::Param(*InParams, TEXT("Dump")))
	{
		FDreamPsdDocument Document;
		FDreamPsdMessages Messages;
		const bool bRead = DreamPsdReader::ReadFile(Request.SourceFile, Document, Messages);
		UE_LOG(LogDreamPsdCommandlet, Display, TEXT("%s\n%s"), *DreamPsdImportCommandletLocal::Dump(Document), *Messages.ToString());
		return bRead ? 0 : 1;
	}
	FParse::Value(*InParams, TEXT("Folder="), Request.AssetFolder);
	FParse::Value(*InParams, TEXT("Name="), Request.Name);
	FParse::Value(*InParams, TEXT("Dui="), Request.DuiFile);
	Request.bCreateBlueprint = !FParse::Param(*InParams, TEXT("NoBlueprint"));
	Request.bSave = !FParse::Param(*InParams, TEXT("NoSave"));
	Request.bDryRun = FParse::Param(*InParams, TEXT("DryRun"));
	FDreamPsdImportReport Report;
	const bool bOk = DreamPsdImporter::Import(Request, Report);
	const FString Text = Report.ToString();
	UE_LOG(LogDreamPsdCommandlet, Display, TEXT("%s"), *Text);
	FString ReportFile;
	if (FParse::Value(*InParams, TEXT("Report="), ReportFile))
	{
		FFileHelper::SaveStringToFile(Text + TEXT("\n\n") + Report.DuiText, *ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	return bOk ? 0 : 1;
}
