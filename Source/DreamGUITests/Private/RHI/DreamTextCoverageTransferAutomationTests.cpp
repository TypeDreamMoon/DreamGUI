// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUITextData.h"
#include "DreamUICaptureLibrary.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"
#include "DreamTextParityCorpus.h"

/*
 * Small text's coverage transfer, on the GPU: what a coverage glyph's 8-bit coverage becomes on the target, against
 * the formula the shader is meant to follow (design_smalltext.txt, section 5: Skia's correcting LUT, worked out per
 * pixel, aware of whether the target blends in gamma or in linear space).
 *
 * The glyph is not rasterized: a ramp is put into the font's coverage cells with
 * UDreamUIFontData_FreeTypeRender::InjectCoverageGlyphForTesting, as the coverage glyph of 'H' at the size the text asks
 * for -- a solid column at each end and, between them, one column for every coverage from 0 to 255, the same in every
 * row and every subpixel phase. A text of that one 'H' then draws the ramp texel for pixel, wherever its pen lands,
 * black on white paper and white on black. The picture is searched for rows that are the ramp (an ink column, paper,
 * ..., ink, and the end), and each of the 256 steps is held to the formula:
 *  - on a render target (DreamGUI.RHI.SmallText...), which DreamGUI draws in linear space -- the gallery's stage, drawn
 *    with four samples into the renderer's multisampled target, which carries the stage target's sRGB flag and so stores
 *    the blend encoded, as the stage's own target would;
 *  - on the screen (DreamGUI.Pie.RHI.SmallText...), a play session's viewport, which blends in gamma space.
 * Both should come out the same: the correction stands in for a linear blend where the target blends encoded values.
 *
 * The font is a distance-field Roboto made for these tests alone, so the ramp never stands in for a real 'H' anywhere
 * else; the project's small-text switch is turned on for the length of the test and put back after.
 */
namespace DreamCoverageTransferTestLocal
{
	using namespace DreamGalleryStage;

	/** The ramp glyph: a solid column at each end, and between them a column for every coverage 0..255, every row alike. */
	static constexpr int32 RampSteps = 256;
	static constexpr int32 RampWidth = RampSteps + 2;
	static constexpr int32 RampHeight = 6;
	/** The text the ramp stands in for, and its size: well under the coverage limit, at a device scale of 1. */
	static constexpr uint32 RampCodepoint = 'H';
	static const TCHAR* const RampText = TEXT("H");
	static constexpr float RampFontSize = 16.0f;
	/** Raster sizes around the one the text asks for, in 26.6, the ramp goes in at: a device scale a hair off 1 rounds to a neighbour. */
	static constexpr int32 RampSizeSlack = 2;
	/** How far, in 8-bit steps, a channel may come out from the formula: the shader's arithmetic, the sRGB conversions, the rounding. */
	static constexpr double TransferTolerance = 4.0;
	/** How far a linear blend may land from the formula, in linear units: the blend's own 8-bit step and a half. */
	static constexpr double LinearBlendTolerance = 1.5 / 255.0;
	/** A pixel is the ink or the paper it is compared with when every colour channel is this close. */
	static constexpr uint8 SolidTolerance = 8;
	/** The two papers, one above the other, and the ramp text on each. */
	static constexpr double PaperWidth = 600.0;
	static constexpr double PaperHeight = 70.0;
	static constexpr double PaperOffset = 45.0;
	/**
	 * The ramp text's box, centred on the paper: the ramp and a margin, no wider. The play session draws into the level
	 * editor's viewport at whatever size the editor's layout gave it, which can be narrower than the paper; a box as wide
	 * as the paper would start the ramp off the left edge of such a viewport.
	 */
	static constexpr double TextWidth = RampWidth + 40.0;

	struct FRampPaper
	{
		const TCHAR* Name;
		FColor Ink;
		FColor Paper;
		/** The paper's centre above the stage's centre, +Y up. */
		double Y;
	};
	static const FRampPaper RampPapers[] =
	{
		{ TEXT("BlackOnWhite"), FColor(0, 0, 0, 255), FColor(255, 255, 255, 255), PaperOffset },
		{ TEXT("WhiteOnBlack"), FColor(255, 255, 255, 255), FColor(0, 0, 0, 255), -PaperOffset },
	};

	/** The ramp's texels, BGRA rows top first: every byte of a texel, every phase, the same coverage. */
	TArray<uint8> MakeRampPixels()
	{
		TArray<uint8> Pixels;
		Pixels.SetNumZeroed(RampWidth * RampHeight * 4);
		for (int32 Row = 0; Row < RampHeight; ++Row)
		{
			for (int32 Column = 0; Column < RampWidth; ++Column)
			{
				const uint8 Value = static_cast<uint8>(Column == 0 || Column == RampWidth - 1 ? 255 : Column - 1);
				uint8* Texel = &Pixels[(Row * RampWidth + Column) * 4];
				Texel[0] = Value;
				Texel[1] = Value;
				Texel[2] = Value;
				Texel[3] = Value;
			}
		}
		return Pixels;
	}

	/** A distance-field Roboto of the test's own, so the ramp stands in for its 'H' and no other font's. */
	UDreamUIFontData_DistanceField* MakeRampFont(FString& OutError)
	{
		const FString File = DreamTextParity::ResolveFontPath(TEXT("$(EngineDir)/Content/Slate/Fonts/Roboto-Regular.ttf"));
		if (!FPaths::FileExists(File))
		{
			OutError = FString::Printf(TEXT("there is no %s"), *File);
			return nullptr;
		}
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(GetTransientPackage(), NAME_None, RF_Transient);
		Font->SetFontFilePath(File, false);
		Font->InitFont();
		return Font;
	}

	/** The ramp as the font's coverage glyph of 'H' at RampFontSize times InDeviceScale, and the sizes either side of it. */
	bool InjectRamp(UDreamUIFontData_FreeTypeRender* InFont, float InDeviceScale, FString& OutError)
	{
		FDreamUIGlyphKey Glyph;
		if (InFont == nullptr || !InFont->ResolveCodepoint(RampCodepoint, Glyph))
		{
			OutError = TEXT("the font has no glyph for 'H'");
			return false;
		}
		const TArray<uint8> Pixels = MakeRampPixels();
		const int32 Size26Dot6 = FMath::RoundToInt32(RampFontSize * InDeviceScale * 64.0f);
		for (int32 Size = Size26Dot6 - RampSizeSlack; Size <= Size26Dot6 + RampSizeSlack; ++Size)
		{
			// Left 0 and top the height: the box stands on the baseline row, from the pen's own column.
			if (!InFont->InjectCoverageGlyphForTesting(Glyph.FaceIndex, Glyph.GlyphIndex, Size, EDreamUICoverageGlyphFlags::None,
				RampWidth, RampHeight, 0, RampHeight, Pixels))
			{
				OutError = FString::Printf(TEXT("the font took no coverage glyph at %d/64 px (is its small-text coverage off?)"), Size);
				return false;
			}
		}
		return true;
	}

	/** The one-'H' text, left-aligned in its box so its pen starts at the box's left edge. */
	void ConfigureRampText(UDreamText* InText, UDreamUIFontData_BaseObject* InFont, FColor InInk)
	{
		InText->SetFont(InFont);
		InText->SetFontSize(RampFontSize);
		InText->SetColor(InInk);
		InText->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
		InText->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
		InText->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
		InText->SetText(FText::FromString(RampText));
	}

	/** sRGB's curve, an encoded value 0..1 to linear and back. */
	double LinearOfEncoded(double InEncoded)
	{
		return InEncoded <= 0.04045 ? InEncoded / 12.92 : FMath::Pow((InEncoded + 0.055) / 1.055, 2.4);
	}

	double EncodedOfLinear(double InLinear)
	{
		return InLinear <= 0.0031308 ? InLinear * 12.92 : 1.055 * FMath::Pow(InLinear, 1.0 / 2.4) - 0.055;
	}

	/**
	 * A colour's luma as Skia takes the text colour's, and as DreamUIText_ShadeCoverage does: its sRGB-encoded channels
	 * weighted as SkComputeLuminance weighs them, 54, 183 and 19 in 256ths -- not the encoding of its linear luminance,
	 * which reads a saturated colour as a much lighter source. Black and white come out alike either way.
	 */
	double EncodedLuma(const FColor& InColour)
	{
		return (54.0 * InColour.R + 183.0 * InColour.G + 19.0 * InColour.B) / (256.0 * 255.0);
	}

	/**
	 * The 8-bit value one channel of a pixel of coverage InCoverage comes out at, ink over paper, by design_smalltext.txt
	 * section 5 (Skia's SkTMaskGamma_build_correcting_lut, worked out per pixel), step for step as
	 * DreamUIText_ShadeCoverage (Shaders/Private/DreamUIText.ush) works it out, its clamps included:
	 *  1. srcL is the ink's encoded luma, dstL = 1 - srcL Skia's guess at what the ink is drawn over;
	 *  2. contrast: a += (1 - a) * C * lin(dstL) * a, clamped to 0..1;
	 *  3. on a target that blends encoded values, the coverage is corrected so the blend lands where a linear one would:
	 *     a = (enc(lin(srcL) a + lin(dstL) (1 - a)) - dstL) / (srcL - dstL), clamped to 0..1, unless srcL and dstL are
	 *     within 1/256;
	 *  4. on a target that blends linear values, step 3 is skipped: the blend is already linear.
	 * Then the target's own blend of the ink over the paper with that coverage, read back encoded. Both targets keep what they
	 * blend encoded, to 8 bits or more: the sRGB render target encodes its linear blend as it stores it, and the screen stores
	 * encoded values as it blends them. A target that kept a linear blend in 8 bits would read back coarser near black.
	 */
	double PredictChannel(int32 InCoverage, uint8 InInk, uint8 InPaper, double InInkLuma, double InContrast, bool bInLinearTarget)
	{
		double Alpha = static_cast<double>(InCoverage) / 255.0;
		const double SourceLuma = InInkLuma;
		const double GuessedLuma = 1.0 - SourceLuma;
		Alpha = FMath::Clamp(Alpha + (1.0 - Alpha) * (InContrast * LinearOfEncoded(GuessedLuma)) * Alpha, 0.0, 1.0);
		const double Ink = static_cast<double>(InInk) / 255.0;
		const double Paper = static_cast<double>(InPaper) / 255.0;
		if (bInLinearTarget)
		{
			return 255.0 * EncodedOfLinear(LinearOfEncoded(Ink) * Alpha + LinearOfEncoded(Paper) * (1.0 - Alpha));
		}
		if (FMath::Abs(SourceLuma - GuessedLuma) >= 1.0 / 256.0)
		{
			Alpha = FMath::Clamp((EncodedOfLinear(LinearOfEncoded(SourceLuma) * Alpha + LinearOfEncoded(GuessedLuma) * (1.0 - Alpha)) - GuessedLuma) / (SourceLuma - GuessedLuma), 0.0, 1.0);
		}
		return 255.0 * (Ink * Alpha + Paper * (1.0 - Alpha));
	}

	/** Where a ramp was found in a picture, and what its 256 steps came out at on the red channel, averaged over its rows. */
	struct FRampReading
	{
		FIntPoint At = FIntPoint(-1, -1);
		int32 Rows = 0;
		TArray<double> Steps;
	};

	bool IsSolid(const FColor& InPixel, const FColor& InColour)
	{
		return FDreamPixelProbe::IsNear(FColor(InPixel.R, InPixel.G, InPixel.B, 255), FColor(InColour.R, InColour.G, InColour.B, 255), SolidTolerance);
	}

	/**
	 * Every row of the picture that is the ramp in this ink over this paper -- an ink pixel, paper (coverage 0), the
	 * steps, ink at the far end and not past it -- read step by step. A row the quad's edge cut through, or a quad not
	 * drawn texel for pixel, has no clean ink at its ends and is not taken.
	 */
	FRampReading ReadRamp(const TArray<FColor>& InPixels, FIntPoint InSize, const FColor& InInk, const FColor& InPaper)
	{
		FRampReading Reading;
		Reading.Steps.Init(0.0, RampSteps);
		if (InSize.X < RampWidth || InPixels.Num() < InSize.X * InSize.Y)
		{
			return Reading;
		}
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			const FColor* Row = &InPixels[Y * InSize.X];
			for (int32 X = 0; X + RampWidth <= InSize.X; ++X)
			{
				const bool bEndsThere = X + RampWidth == InSize.X || !IsSolid(Row[X + RampWidth], InInk);
				if (!IsSolid(Row[X], InInk) || !IsSolid(Row[X + 1], InPaper) || !IsSolid(Row[X + RampWidth - 1], InInk) || !bEndsThere)
				{
					continue;
				}
				if (Reading.Rows == 0)
				{
					Reading.At = FIntPoint(X, Y);
				}
				for (int32 Step = 0; Step < RampSteps; ++Step)
				{
					Reading.Steps[Step] += static_cast<double>(Row[X + 1 + Step].R);
				}
				++Reading.Rows;
				break;
			}
		}
		if (Reading.Rows > 0)
		{
			for (double& Step : Reading.Steps)
			{
				Step /= static_cast<double>(Reading.Rows);
			}
		}
		return Reading;
	}

	/** Finds the ramp of one paper in the picture and holds every step of it to the formula. */
	void CheckRamp(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize, const FRampPaper& InPaper, bool bInLinearTarget,
		const FString& InTarget, const FString& InCapturePath)
	{
		const FRampReading Reading = ReadRamp(InPixels, InSize, InPaper.Ink, InPaper.Paper);
		if (!InTest.TestTrue(FString::Printf(TEXT("%s, %s: the ramp is in the picture, drawn texel for pixel (the text drew its 'H' from the injected coverage glyph; picture: %s)"),
			*InTarget, InPaper.Name, *InCapturePath), Reading.Rows > 0))
		{
			return;
		}
		const double Contrast = UDreamGUISettings::Get()->SmallTextContrast;
		const double InkLuma = EncodedLuma(InPaper.Ink);
		double Worst = 0.0;
		int32 WorstStep = 0;
		double Sum = 0.0;
		TArray<FString> Samples;
		for (int32 Step = 0; Step < RampSteps; ++Step)
		{
			const double Expected = PredictChannel(Step, InPaper.Ink.R, InPaper.Paper.R, InkLuma, Contrast, bInLinearTarget);
			double Error = FMath::Abs(Reading.Steps[Step] - Expected);
			// A target that blends linear values blends them at about 8 bits before it encodes the result: one linear step
			// near black is a dozen encoded ones (the sRGB curve is steepest there), so a step within that blend's own
			// precision of the formula is right however many encoded steps it reads apart.
			if (bInLinearTarget && FMath::Abs(LinearOfEncoded(Reading.Steps[Step] / 255.0) - LinearOfEncoded(Expected / 255.0)) <= LinearBlendTolerance)
			{
				Error = FMath::Min(Error, TransferTolerance);
			}
			Sum += Error;
			if (Error > Worst)
			{
				Worst = Error;
				WorstStep = Step;
			}
			if (Step % 32 == 0 || Step == RampSteps - 1)
			{
				Samples.Add(FString::Printf(TEXT("%d: %.1f (%.1f)"), Step, Reading.Steps[Step], Expected));
			}
		}
		InTest.AddInfo(FString::Printf(TEXT("%s, %s: the ramp at (%d, %d), %d row(s); coverage: drawn (formula, contrast %.2f, %s) -- %s."),
			*InTarget, InPaper.Name, Reading.At.X, Reading.At.Y, Reading.Rows, Contrast, bInLinearTarget ? TEXT("linear blend") : TEXT("gamma blend"),
			*FString::Join(Samples, TEXT(", "))));
		InTest.TestTrue(FString::Printf(TEXT("%s, %s: every coverage from 0 to 255 comes out within %.0f of the transfer function (the worst, coverage %d, by %.1f: %.1f against %.1f; the mean error %.2f)"),
			*InTarget, InPaper.Name, TransferTolerance, WorstStep, Worst, Reading.Steps[WorstStep],
			PredictChannel(WorstStep, InPaper.Ink.R, InPaper.Paper.R, InkLuma, Contrast, bInLinearTarget), Sum / RampSteps),
			Worst <= TransferTolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTransferRenderTargetTest,
	"DreamGUI.RHI.SmallText.ACoverageRampOnARenderTargetBlendsAsTheTransferFunctionSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * The ramp, black on white and white on black, on the gallery's render-target stage: a RenderTarget canvas, which
 * DreamGUI draws with gamma 1 into an sRGB target, so the blend is linear and the shader skips its correction. Drawn with
 * four samples, as the test host draws everything: into the renderer's multisampled target, which takes the stage
 * target's sRGB flag, so the blend is stored encoded there as it would be in the stage's own target, and the resolve
 * averages decoded samples and encodes the result. Each of the 256 steps must come out within TransferTolerance of the
 * formula, the tolerance the single-sampled path is held to.
 */
bool FDreamCoverageTransferRenderTargetTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoverageTransferTestLocal;
	FStageRef Stage = BeginStage(*this, FIntPoint(640, 200));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	Stage->UseSmallTextCoverage(true);
	// Four samples, stated rather than left to the host's settings. The renderer's multisampled target used to have the
	// stage's pixel format without its sRGB flag, which kept the linear blend in 8 bits until the resolve encoded it -- one
	// step near black was then 13 encoded ones, and black on white at coverage 244 read 0, not 6 -- so this test drew
	// single-sampled. With the flag carried over, the multisampled path stores what the single-sampled one does.
	Stage->UseMultisampling(4);
	// One target pixel per canvas unit, as the ramp goes in for (a device scale of 1): no canvas scaler, and the target at
	// the canvas's own resolution. A coverage glyph's device scale includes both, so they are stated here, not assumed.
	if (UDreamCanvas* Canvas = Stage->GetCanvas())
	{
		Canvas->SetScaleMode(EDreamCanvasScaleMode::ConstantPixelSize);
		Canvas->SetRenderTargetResolutionScale(1.0f);
	}
	FString Error;
	UDreamUIFontData_DistanceField* Font = MakeRampFont(Error);
	if (!TestNotNull(FString::Printf(TEXT("A distance-field font to put the ramp in%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), Font))
	{
		Stage->TearDown();
		return false;
	}
	Stage->KeepAlive(Font);
	if (!TestTrue(FString::Printf(TEXT("The ramp goes into the font's coverage cells%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), InjectRamp(Font, 1.0f, Error)))
	{
		Stage->TearDown();
		return false;
	}
	for (const FRampPaper& Paper : RampPapers)
	{
		Stage->AddBlock(Paper.Name, FVector2D(PaperWidth, PaperHeight), FVector2D(0.0, Paper.Y), Paper.Paper);
		UDreamWidget* Widget = Stage->AddWidget(Paper.Name, FVector2D(TextWidth, PaperHeight), FVector2D(0.0, Paper.Y));
		if (UDreamText* Text = Widget->CreateNewVisual<UDreamText>())
		{
			ConfigureRampText(Text, Font, Paper.Ink);
		}
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 1);
	EnqueueFramesUntilSettled(Stage);
	// A text joins coverage once its device scale has held still for a few frames; let that happen and settle again.
	EnqueueFrames(Stage, 6);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([this, Stage]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The stage reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		const FString CapturePath = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("SmallText_TransferOnARenderTarget"));
		for (const FRampPaper& Paper : RampPapers)
		{
			CheckRamp(*this, Pixels, Size, Paper, true, TEXT("A render target"), CapturePath);
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTransferScreenTest,
	"DreamGUI.Pie.RHI.SmallText.ACoverageRampOnTheScreenBlendsAsTheTransferFunctionSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * The ramp, black on white and white on black, on the screen: a play session's screen-space canvas, drawn into the main
 * viewport with the display gamma, where the blend is of encoded values and the shader corrects the coverage for it.
 * Each of the 256 steps must come out within TransferTolerance of the formula. A PIE test (DreamGUI.Pie.), because a
 * screen-space canvas draws into a game viewport, and a play session replaces the editor's level the pixel tests use.
 */
bool FDreamCoverageTransferScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamCoverageTransferTestLocal;
	struct FState
	{
		TStrongObjectPtr<UDreamUIFontData_DistanceField> Font;
		bool bSavedSmallTextCoverage = true;
		bool bSavedBuiltInShader = true;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	State->bSavedSmallTextCoverage = Settings->bSmallTextCoverage;
	Settings->bSmallTextCoverage = true;
	// The built-in shader, as on the render-target stage: the transfer function under test is DreamUIText_ShadeCoverage's.
	UDreamUISettings* UISettings = GetMutableDefault<UDreamUISettings>();
	State->bSavedBuiltInShader = UISettings->bUseBuiltInUIShader;
	UISettings->bUseBuiltInUIShader = true;
	FString Error;
	State->Font.Reset(MakeRampFont(Error));
	const bool bInjected = State->Font.IsValid() && InjectRamp(State->Font.Get(), 1.0f, Error);
	if (!TestTrue(FString::Printf(TEXT("A distance-field font with the ramp in its coverage cells%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), bInjected))
	{
		Settings->bSmallTextCoverage = State->bSavedSmallTextCoverage;
		UISettings->bUseBuiltInUIShader = State->bSavedBuiltInShader;
		return false;
	}

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([State](FDreamDriverPieRig& InRig)
	{
		// One viewport pixel per canvas unit, as the ramp went in for (a device scale of 1): no canvas scaler, and the
		// screen-space canvas drawn at the viewport's full resolution. A coverage glyph's device scale includes both, so
		// they are stated here, not assumed.
		if (UDreamCanvas* Canvas = InRig.RootCanvas())
		{
			Canvas->SetScaleMode(EDreamCanvasScaleMode::ConstantPixelSize);
			Canvas->SetScreenSpaceRenderScale(1.0f);
		}
		for (const FRampPaper& Paper : RampPapers)
		{
			UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), FString::Printf(TEXT("%sPaper"), Paper.Name), nullptr,
				FVector2D(PaperWidth, PaperHeight), FVector2D(0.0, Paper.Y));
			if (UDreamTexture* Visual = Block != nullptr ? Cast<UDreamTexture>(Block->GetVisual()) : nullptr)
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(Paper.Paper);
			}
			UDreamWidget* Label = InRig.MakeWidgetWithVisual(UDreamText::StaticClass(), Paper.Name, nullptr,
				FVector2D(TextWidth, PaperHeight), FVector2D(0.0, Paper.Y));
			if (UDreamText* Text = Label != nullptr ? Cast<UDreamText>(Label->GetVisual()) : nullptr)
			{
				ConfigureRampText(Text, State->Font.Get(), Paper.Ink);
			}
		}
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	// The canvas batches on a worker, the scene render trails the tick, and a text joins coverage once its device scale
	// has held still for a few frames: a dozen covers all three.
	Steps.WaitFrames(12);
	Steps.Then([this](FDreamDriverContext& InContext)
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		FViewport* Viewport = UDreamUICaptureLibrary::FindViewportOf(InContext.World);
		if (!TestTrue(TEXT("The play session's viewport reads back"), UDreamUICaptureLibrary::ReadViewportPixels(Viewport, Pixels, Size)))
		{
			return;
		}
		const FString CapturePath = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("SmallText_TransferOnTheScreen"));
		for (const FRampPaper& Paper : RampPapers)
		{
			CheckRamp(*this, Pixels, Size, Paper, false, TEXT("The screen"), CapturePath);
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	EnqueueDo([State]()
	{
		GetMutableDefault<UDreamGUISettings>()->bSmallTextCoverage = State->bSavedSmallTextCoverage;
		GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = State->bSavedBuiltInShader;
		State->Font.Reset();
	});
	return true;
}

#endif
