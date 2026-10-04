// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextPaint.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Painted text on the GPU: a gradient a text's glyphs are filled with (FDreamTextPaint, CSS's `background-clip: text`), as
 * the renderer draws it on the gallery's render-target stage.
 *
 * Three questions, one test each. Does a page of painted text look as it did (a golden image: face, outline and overlay
 * paints, a radial, per-glyph boxes, small text from coverage glyphs)? Does the shader paint what FDreamGradient::Evaluate
 * -- the C++ twin of DreamUIPaint.ush -- says it paints, pixel for pixel, in every colour space? Measured along a thick
 * underline: a solid strip the face paint fills, with no glyph edge in the way. And does a text's outline fade with its
 * render opacity and its content tint, while the text colour's own alpha, which hollow text sets to 0, leaves it alone?
 */
namespace DreamTextPaintPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** A paint of the CSS gradient InCss, on; false when the CSS does not read. */
	bool MakePaint(const TCHAR* InCss, FDreamTextPaint& OutPaint, FString& OutError)
	{
		OutPaint = FDreamTextPaint();
		OutPaint.bEnabled = true;
		return FDreamGradient::ParseCss(InCss, OutPaint.Gradient, &OutError);
	}

	/** sRGB's decode, for measuring how much light a picture adds over its backdrop. */
	double LinearOfCode(uint8 InCode)
	{
		const double Encoded = static_cast<double>(InCode) / 255.0;
		return Encoded <= 0.04045 ? Encoded / 12.92 : FMath::Pow((Encoded + 0.055) / 1.055, 2.4);
	}

	/** A pixel rect of the stage: InSize centred at InPosition from the stage's centre, +Y up, clipped to the picture. */
	FIntRect StageRect(FIntPoint InStageSize, const FVector2D& InSize, const FVector2D& InPosition)
	{
		const int32 Left = FMath::RoundToInt32(InStageSize.X * 0.5 + InPosition.X - InSize.X * 0.5);
		const int32 Top = FMath::RoundToInt32(InStageSize.Y * 0.5 - InPosition.Y - InSize.Y * 0.5);
		FIntRect Rect(Left, Top, Left + FMath::RoundToInt32(InSize.X), Top + FMath::RoundToInt32(InSize.Y));
		Rect.Clip(FIntRect(0, 0, InStageSize.X, InStageSize.Y));
		return Rect;
	}

	/** The light a region adds over the backdrop, in linear light, summed: each pixel's largest channel difference. */
	double LinearMassOver(const TArray<FColor>& InPixels, FIntPoint InSize, const FIntRect& InRegion, const FColor& InBackdrop)
	{
		double Mass = 0.0;
		for (int32 Y = InRegion.Min.Y; Y < InRegion.Max.Y; ++Y)
		{
			for (int32 X = InRegion.Min.X; X < InRegion.Max.X; ++X)
			{
				const FColor& Pixel = InPixels[Y * InSize.X + X];
				Mass += FMath::Max3(
					FMath::Abs(LinearOfCode(Pixel.R) - LinearOfCode(InBackdrop.R)),
					FMath::Abs(LinearOfCode(Pixel.G) - LinearOfCode(InBackdrop.G)),
					FMath::Abs(LinearOfCode(Pixel.B) - LinearOfCode(InBackdrop.B)));
			}
		}
		return Mass;
	}

	/** The underline cases: one interpolation space each, the same three stops left to right. */
	struct FUnderlineCase
	{
		const TCHAR* Name;
		const TCHAR* Css;
		/** The text's centre above the stage's centre, +Y up. */
		double Y;
	};
	static const FUnderlineCase UnderlineCases[] =
	{
		{ TEXT("Srgb"), TEXT("linear-gradient(90deg, #FF2D55, #34C759, #0A84FF)"), 150.0 },
		{ TEXT("LinearLight"), TEXT("linear-gradient(90deg in srgb-linear, #FF2D55, #34C759, #0A84FF)"), 0.0 },
		{ TEXT("Oklab"), TEXT("linear-gradient(90deg in oklab, #FF2D55, #34C759, #0A84FF)"), -150.0 },
	};
	static const FIntPoint UnderlineStage(640, 480);
	static const FVector2D UnderlineBox(600.0, 150.0);
	static constexpr float UnderlineFontSize = 112.0f;
	/** A run of drawn pixels at least this long on one row is the underline: a glyph is narrower, and glyphs have gaps between them. */
	static constexpr int32 UnderlineMinRun = 400;
	/** How far a pixel is from the backdrop on its farthest channel before it counts as drawn. */
	static constexpr int32 DrawnThreshold = 24;
	/** Pixels at each end of the strip left out: its quad's ends are on fractional pixels. */
	static constexpr int32 StripEndMargin = 3;
	/** Codes a channel may be from the evaluator's colour: float arithmetic on both sides, and the target's 8-bit encode. */
	static constexpr int32 PaintTolerance = 3;

	/** The longest run of drawn pixels on row InY within [InLeft, InRight): its start and its end. */
	void LongestDrawnRun(const TArray<FColor>& InPixels, FIntPoint InSize, int32 InY, int32 InLeft, int32 InRight, const FColor& InBackdrop, int32& OutStart, int32& OutEnd)
	{
		OutStart = 0;
		OutEnd = 0;
		int32 RunStart = -1;
		for (int32 X = InLeft; X <= InRight; ++X)
		{
			bool bDrawn = false;
			if (X < InRight)
			{
				const FColor& Pixel = InPixels[InY * InSize.X + X];
				bDrawn = FMath::Max3(FMath::Abs(Pixel.R - InBackdrop.R), FMath::Abs(Pixel.G - InBackdrop.G), FMath::Abs(Pixel.B - InBackdrop.B)) > DrawnThreshold;
			}
			if (bDrawn && RunStart < 0)
			{
				RunStart = X;
			}
			else if (!bDrawn && RunStart >= 0)
			{
				if (X - RunStart > OutEnd - OutStart)
				{
					OutStart = RunStart;
					OutEnd = X;
				}
				RunStart = -1;
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryPaintedTextTest,
	"DreamGUI.RHI.Gallery.PaintedTextMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryPaintedTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintPixelTestLocal;
	// The paints together: a gold face top to bottom under a dark outline, a radial circle, a shimmer band added over a
	// solid face, a gradient outline round a white face, letters each painted in their own glyph box, and small text drawn
	// from coverage glyphs and painted all the same.
	FStageRef Stage = BeginStage(*this, FIntPoint(512, 320));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	FString Error;
	FDreamTextPaint Paint;
	if (UDreamText* Gold = Stage->AddText(TEXT("Gold"), TEXT("GOLD"), 56.0f, FVector2D(240.0, 72.0), FVector2D(-128.0, 110.0), FColor::White))
	{
		TestTrue(FString::Printf(TEXT("The gold gradient reads%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error),
			MakePaint(TEXT("linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)"), Paint, Error));
		Gold->SetFacePaint(Paint);
		FDreamTextStyle Style = Gold->GetTextStyle();
		Style.OutlineColor = FColor(58, 37, 0, 255);
		Style.OutlineWidth = 0.08f;
		Gold->SetTextStyle(Style);
	}
	if (UDreamText* Radial = Stage->AddText(TEXT("Radial"), TEXT("Radial"), 44.0f, FVector2D(240.0, 72.0), FVector2D(128.0, 110.0), FColor::White))
	{
		TestTrue(TEXT("The radial gradient reads"), MakePaint(TEXT("radial-gradient(circle at 50% 50%, #FFFFFF, #7FD1FF 45%, #1A4C9C)"), Paint, Error));
		Radial->SetFacePaint(Paint);
	}
	if (UDreamText* Shimmer = Stage->AddText(TEXT("Shimmer"), TEXT("Shimmer"), 44.0f, FVector2D(240.0, 64.0), FVector2D(-128.0, 30.0), FColor(150, 156, 176, 255)))
	{
		TestTrue(TEXT("The shimmer band reads"), MakePaint(TEXT("linear-gradient(105deg, transparent 40%, #FFFFFFD0 50%, transparent 60%)"), Paint, Error));
		Shimmer->SetOverlayPaint(Paint);
		Shimmer->SetOverlayBlend(EDreamTextOverlayBlend::Add);
	}
	if (UDreamText* Outlined = Stage->AddText(TEXT("Outlined"), TEXT("Outline"), 44.0f, FVector2D(240.0, 64.0), FVector2D(128.0, 30.0), FColor::White))
	{
		TestTrue(TEXT("The outline's gradient reads"), MakePaint(TEXT("linear-gradient(90deg, #FF2D55, #5856D6)"), Paint, Error));
		FDreamTextStyle Style = Outlined->GetTextStyle();
		Style.OutlineColor = FColor::White;
		Style.OutlineWidth = 0.12f;
		Outlined->SetTextStyle(Style);
		Outlined->SetOutlinePaint(Paint);
	}
	if (UDreamText* PerGlyph = Stage->AddText(TEXT("PerGlyph"), TEXT("ABCDEFG per glyph"), 32.0f, FVector2D(480.0, 48.0), FVector2D(0.0, -50.0), FColor::White))
	{
		TestTrue(TEXT("The per-glyph gradient reads"), MakePaint(TEXT("linear-gradient(180deg, #FFD60A, #FF375F)"), Paint, Error));
		PerGlyph->SetFacePaint(Paint);
		PerGlyph->SetPaintBoxes(EDreamTextPaintBox::Glyph, EDreamTextPaintBox::Glyph);
	}
	if (UDreamText* Small = Stage->AddText(TEXT("Small"), TEXT("small painted text at 14 px from coverage glyphs"), 14.0f, FVector2D(480.0, 24.0), FVector2D(0.0, -110.0), FColor::White))
	{
		TestTrue(TEXT("The small text's gradient reads"), MakePaint(TEXT("linear-gradient(90deg, #30D158, #64D2FF, #BF5AF2)"), Paint, Error));
		Small->SetFacePaint(Paint);
	}
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_PaintedText"), 4000, EMissingGolden::Fail);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintShaderAgainstEvaluatorTest,
	"DreamGUI.RHI.Paint.AThickUnderlineIsPaintedAsTheEvaluatorSaysInEveryColourSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamPaintShaderAgainstEvaluatorTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintPixelTestLocal;
	// The same three stops, left to right across each text's block, mixed in sRGB, in linear light and in Oklab, and each
	// text underlined at a size that makes the underline several pixels thick. The underline is a solid strip of the face
	// the face paint fills: on its middle row every pixel is the paint at that pixel's centre, opaque over the backdrop, so
	// what the shader drew is held to what FDreamGradient::Evaluate gives there. A 90-degree gradient's parameter is the
	// box's u alone, whatever the box's aspect, and the text block runs across the whole content box, the widget's rect.
	// The text's own colour is black, which a painted face does not use.
	FStageRef Stage = BeginStage(*this, UnderlineStage);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	TSharedRef<TArray<FDreamGradient>> Gradients = MakeShared<TArray<FDreamGradient>>();
	for (const FUnderlineCase& Case : UnderlineCases)
	{
		FDreamTextPaint Paint;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s: the gradient reads%s%s"), Case.Name, Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), MakePaint(Case.Css, Paint, Error)))
		{
			Stage->TearDown();
			return false;
		}
		Gradients->Add(Paint.Gradient);
		UDreamText* Text = Stage->AddText(Case.Name, TEXT("mmmmm"), UnderlineFontSize, UnderlineBox, FVector2D(0.0, Case.Y), FColor::Black);
		if (!TestNotNull(FString::Printf(TEXT("%s: a text on the stage"), Case.Name), Text))
		{
			Stage->TearDown();
			return false;
		}
		Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
		Text->SetUnderline(true);
		Text->SetFacePaint(Paint);
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 20000);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([this, Stage, Gradients]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The stage reads back"), Stage->ReadBack(Pixels, Size)) || !TestTrue(TEXT("...at its size"), Size == UnderlineStage))
		{
			return;
		}
		const FString CapturePath = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Paint_UnderlineAgainstEvaluator"));
		const FColor StageBackdrop = Stage->GetBackdrop();
		const int32 CaseCount = UE_ARRAY_COUNT(UnderlineCases);
		for (int32 CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
		{
			const FUnderlineCase& Case = UnderlineCases[CaseIndex];
			const FIntRect Box = StageRect(Size, UnderlineBox, FVector2D(0.0, Case.Y));
			// The underline's rows: those whose longest drawn run is long enough. Its middle row is held to the evaluator.
			int32 FirstRow = -1;
			int32 LastRow = -1;
			for (int32 Y = Box.Min.Y; Y < Box.Max.Y; ++Y)
			{
				int32 Start = 0;
				int32 End = 0;
				LongestDrawnRun(Pixels, Size, Y, Box.Min.X, Box.Max.X, StageBackdrop, Start, End);
				if (End - Start >= UnderlineMinRun)
				{
					FirstRow = FirstRow < 0 ? Y : FirstRow;
					LastRow = Y;
				}
			}
			if (!TestTrue(FString::Printf(TEXT("%s: the underline is drawn, a strip at least %d pixels long (picture: %s)"), Case.Name, UnderlineMinRun, *CapturePath), FirstRow >= 0))
			{
				continue;
			}
			const int32 Row = (FirstRow + LastRow) / 2;
			int32 Start = 0;
			int32 End = 0;
			LongestDrawnRun(Pixels, Size, Row, Box.Min.X, Box.Max.X, StageBackdrop, Start, End);
			const FDreamGradient& Gradient = (*Gradients)[CaseIndex];
			const float Aspect = static_cast<float>(UnderlineBox.X / UnderlineBox.Y);
			int32 Sampled = 0;
			int32 Off = 0;
			int32 Worst = 0;
			int32 WorstX = -1;
			FColor WorstDrawn = FColor::Black;
			FColor WorstExpected = FColor::Black;
			TArray<FString> Samples;
			for (int32 X = Start + StripEndMargin; X < End - StripEndMargin; ++X)
			{
				// u across the text block, which is the content box across: the widget's rect.
				const float U = static_cast<float>((static_cast<double>(X) + 0.5 - Box.Min.X) / UnderlineBox.X);
				const FColor Expected = Gradient.Evaluate(FVector2f(U, 0.5f), Aspect).ToFColor(true);
				const FColor& Drawn = Pixels[Row * Size.X + X];
				const int32 Delta = FMath::Max3(FMath::Abs(Drawn.R - Expected.R), FMath::Abs(Drawn.G - Expected.G), FMath::Abs(Drawn.B - Expected.B));
				++Sampled;
				if (Delta > PaintTolerance)
				{
					++Off;
				}
				if (Delta > Worst)
				{
					Worst = Delta;
					WorstX = X;
					WorstDrawn = Drawn;
					WorstExpected = Expected;
				}
				if ((X - Start) % 100 == 0)
				{
					Samples.Add(FString::Printf(TEXT("u %.2f: %s (%s)"), U, *FDreamPixelProbe::Describe(Drawn), *FDreamPixelProbe::Describe(Expected)));
				}
			}
			AddInfo(FString::Printf(TEXT("%s: the underline's rows %d to %d, row %d read from x %d to %d; drawn (evaluated): %s."),
				Case.Name, FirstRow, LastRow, Row, Start + StripEndMargin, End - StripEndMargin, *FString::Join(Samples, TEXT("; "))));
			TestTrue(FString::Printf(TEXT("%s: every pixel of the underline's middle row is within %d codes of the evaluator's colour (%d of %d off; the worst at x %d by %d: %s against %s)"),
				Case.Name, PaintTolerance, Off, Sampled, WorstX, Worst, *FDreamPixelProbe::Describe(WorstDrawn), *FDreamPixelProbe::Describe(WorstExpected)),
				Sampled > 0 && Off == 0);
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintFadedOutlineTest,
	"DreamGUI.RHI.Paint.AnOutlineFadesWithRenderOpacityAndContentTintButNotWithTheTextColoursAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamPaintFadedOutlineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintPixelTestLocal;
	// Three hollow texts -- a transparent face inside an opaque yellow outline, the way a hollow title is made -- side by
	// side on whole-pixel positions, so their glyphs fall on the pixel grid alike: one as it is, one at render opacity 0.5,
	// one under a parent whose content tint has alpha 0.5. The outline is an effect, and an effect fades with the widget's
	// opacity and tint as the face does; it does not fade with the text colour's alpha, or a hollow text would show nothing.
	// The stage blends in linear light, so half the opacity is half the light the outline adds over the backdrop.
	FStageRef Stage = BeginStage(*this, FIntPoint(512, 160));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	const FVector2D TextSize(150.0, 100.0);
	const FColor Hollow(255, 255, 255, 0);
	auto MakeHollow = [](UDreamText* InText)
	{
		if (InText == nullptr)
		{
			return;
		}
		FDreamTextStyle Style = InText->GetTextStyle();
		Style.OutlineColor = FColor(255, 210, 0, 255);
		Style.OutlineWidth = 0.12f;
		InText->SetTextStyle(Style);
	};
	UDreamText* Plain = Stage->AddText(TEXT("Plain"), TEXT("OUT"), 64.0f, TextSize, FVector2D(-160.0, 0.0), Hollow);
	MakeHollow(Plain);
	UDreamText* Faded = Stage->AddText(TEXT("Faded"), TEXT("OUT"), 64.0f, TextSize, FVector2D(0.0, 0.0), Hollow);
	MakeHollow(Faded);
	if (Faded != nullptr && Faded->GetWidget() != nullptr)
	{
		Faded->GetWidget()->SetRenderOpacity(0.5f);
	}
	// The tint is an ancestor's: a widget's own content tint is for its children.
	UDreamWidget* TintHolder = Stage->AddWidget(TEXT("TintHolder"), TextSize, FVector2D(160.0, 0.0));
	UDreamText* Tinted = nullptr;
	if (TintHolder != nullptr)
	{
		TintHolder->SetContentTint(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		if (UDreamWidget* TintedWidget = Stage->AddWidget(TEXT("Tinted"), TextSize, FVector2D::ZeroVector, TintHolder))
		{
			Tinted = TintedWidget->CreateNewVisual<UDreamText>();
			if (Tinted != nullptr)
			{
				// As the stage's AddText makes its texts.
				Tinted->SetText(FText::FromString(TEXT("OUT")));
				Tinted->SetFontSize(64.0f);
				Tinted->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
				Tinted->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
				Tinted->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
				Tinted->SetColor(Hollow);
				MakeHollow(Tinted);
			}
		}
	}
	if (!TestTrue(TEXT("Three hollow texts on the stage"), Plain != nullptr && Faded != nullptr && Tinted != nullptr))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 1000);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([this, Stage, TextSize]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The stage reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		const FString CapturePath = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Paint_FadedOutline"));
		const FColor StageBackdrop = Stage->GetBackdrop();
		const double PlainMass = LinearMassOver(Pixels, Size, StageRect(Size, TextSize, FVector2D(-160.0, 0.0)), StageBackdrop);
		const double FadedMass = LinearMassOver(Pixels, Size, StageRect(Size, TextSize, FVector2D(0.0, 0.0)), StageBackdrop);
		const double TintedMass = LinearMassOver(Pixels, Size, StageRect(Size, TextSize, FVector2D(160.0, 0.0)), StageBackdrop);
		AddInfo(FString::Printf(TEXT("The outlines add %.1f (as they are), %.1f (opacity 0.5) and %.1f (tint alpha 0.5) of linear light over the backdrop; picture: %s."),
			PlainMass, FadedMass, TintedMass, *CapturePath));
		if (!TestTrue(FString::Printf(TEXT("A hollow text shows its outline although its colour's alpha is 0 (%.1f of linear light)"), PlainMass), PlainMass > 50.0))
		{
			return;
		}
		const double FadedShare = FadedMass / PlainMass;
		const double TintedShare = TintedMass / PlainMass;
		TestTrue(FString::Printf(TEXT("At render opacity 0.5 the outline adds half the light (%.2f of it)"), FadedShare), FadedShare >= 0.42 && FadedShare <= 0.58);
		TestTrue(FString::Printf(TEXT("Under a content tint of alpha 0.5 the outline adds half the light (%.2f of it)"), TintedShare), TintedShare >= 0.42 && TintedShare <= 0.58);
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
