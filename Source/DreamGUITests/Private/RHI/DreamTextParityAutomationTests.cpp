// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Brushes/SlateColorBrush.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Text/ILayoutBlock.h"
#include "Framework/Text/IRun.h"
#include "Framework/Text/ITextDecorator.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/TextLayout.h"
#include "HAL/FileManager.h"
#include "Layout/Clipping.h"
#include "Layout/Margin.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "RenderDeferredCleanup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SLeafWidget.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAnchorData.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUITextData.h"
#include "Core/Text/DreamTextDisplayList.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUICaptureLibrary.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"
#include "DreamTextParityCorpus.h"

/*
 * Text parity: every case of Resources/TextParity/corpus.json drawn by DreamGUI, by Slate and by Chrome, and the three
 * set side by side, in pictures and in numbers. DreamGUI.RHI.TextParity.<case id> is one case.
 *
 * DreamGUI draws on the gallery's render-target stage, with distance-field fonts (outline multi-channel) made from the
 * same font files the case names, the first file the font and the rest its fallback entries (with the cultures, ranges
 * and scale the fonts table gives them). The stage is the case's canvas times its scale in pixels, its root laid out at
 * the canvas's own size, so the canvas scale is the case's. Its numbers come from the text's display list: line starts
 * and the caret per UTF-16 offset from the caret lines, the first baseline from the pen of the first line's glyphs, all
 * of it turned from the text's local space (origin at its top-left pivot, +Y up) into canvas pixels from the top-left.
 *
 * Slate draws the same text through an FSlateTextLayout -- the layout STextBlock and SRichTextBlock wrap -- painted by a
 * small leaf widget, so that the picture and the numbers come from one layout object. The font is an
 * FStandaloneCompositeFont over the same files: the first file is the default typeface (with its true bold and italic
 * faces where the key has them), and each fallback file is a sub-typeface over exactly the code points of this case it
 * is the first file to draw, at its scale. Sizes are points at 96 DPI, the pixel size times 0.75; the case's scale is
 * the DPI scale Slate draws at. FWidgetRenderer draws it twice, a few frames apart, because Slate rasterises a glyph the
 * first time it paints it. Rich text is read with DreamGUI's markup rules and handed to Slate as one style per run; a
 * superscript or subscript keeps only its smaller size there, since Slate cannot raise a run off the baseline. Where
 * Slate has no equivalent of what a case is about -- justification, a tab size, faces chosen by the text's language --
 * Slate does not draw it and its panel is hatched: n/a.
 *
 * Chrome is read from Resources/TextParity/Chrome/<case>.png and .json, which Tools/TextParity/Make-ChromeReference.ps1
 * writes. Without them the case says so and compares DreamGUI and Slate with nothing. A case whose "reference" is
 * "slate" (a middle ellipsis, which Chrome cannot draw) is measured against Slate instead and has no Chrome reference.
 *
 * A fill case -- a "fill" gradient over the whole text, or a rich text's <gradient=...> run -- is drawn twice on each
 * side: painted, and as a solid mask in the ink colour with nothing painted, outlined or shadowed (Chrome's
 * <case>.mask.png). Slate has no gradient fill and is n/a. On the pixels both masks cover fully the two painted pictures'
 * colours are compared: the mean and the 95th percentile of each pixel's largest channel difference, and the largest
 * difference of the average colour in 32 bands along the gradient's axis. Reported only.
 *
 * Everything goes to Saved/DreamGUITextParity: <case>_dream.png, <case>_slate.png, <case>_compare.png (DreamGUI | Slate
 * | Chrome), <case>_mask.png for the corner cases, <case>_dream_mask.png for the fill cases, cases/<case>.json, and
 * report.json and report.md over every case on disk. Asserted, unless the case is flagged reportOnly: where it is
 * flagged "breaks", DreamGUI starts its lines where the reference does, and every target it names (ink, carets,
 * crispness) holds. The rest is reported, to be tightened into assertions once the numbers have been looked at.
 */
namespace DreamTextParityTestLocal
{
	using namespace DreamGalleryStage;
	using namespace DreamTextParity;

	/** Grey between the three pictures of a comparison, and the panel of a picture that is missing. */
	static const FColor SeparatorColour(128, 128, 128, 255);
	static const FColor MissingPanelColour(214, 214, 214, 255);
	/** The stripes over a panel whose engine has no equivalent of what the case shows. */
	static const FColor NotApplicableStripeColour(176, 176, 176, 255);
	static constexpr int32 SeparatorWidth = 4;
	/** A pixel is ink when it is this far from the paper, as a fraction of the full range on its farthest channel. */
	static constexpr double InkThreshold = 0.25;
	/** And solid ink, for the corner masks, past this. */
	static constexpr double MaskThreshold = 0.5;
	/** Crispness: a pixel at or below EdgePaper is paper and at or above EdgeInk is ink; a pixel between is part of an edge. */
	static constexpr double EdgePaper = 0.1;
	static constexpr double EdgeInk = 0.9;
	/** Crispness: grey is strictly between these, of what is strictly above the first. */
	static constexpr double GreyLow = 0.15;
	static constexpr double GreyHigh = 0.85;
	/**
	 * Edge width per stroke: a run of a row or a column above EdgePaper is a stroke once its peak reaches StrokePeak, and
	 * its edges are its pixels below StrokeEdgeHigh of the peak and above StrokeEdgeLow of it (and above EdgePaper).
	 */
	static constexpr double StrokePeak = 0.5;
	static constexpr double StrokeEdgeHigh = 0.9;
	static constexpr double StrokeEdgeLow = 0.1;
	/** A fill case's colours are compared on the pixels both masks cover at least this much. */
	static constexpr double FillCovered = 0.99;
	/** And their average colours in this many bands along the gradient's axis. */
	static constexpr int32 FillBands = 32;

	/** A caret, in canvas pixels from the top-left. */
	struct FCaretSample
	{
		double X = 0.0;
		double Y = 0.0;
	};

	/** What one engine drew for a case: positions in canvas (CSS) pixels from the top-left, the picture in device pixels. */
	struct FEngineResult
	{
		FString Engine;
		bool bPicture = false;
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool bNumbers = false;
		/** Why a picture or numbers are missing, or what is only approximated. */
		TArray<FString> Notes;
		/** Why this engine has no equivalent of what the case shows; empty when it has one. */
		FString NotApplicable;
		TArray<int32> LineStarts;
		TMap<int32, FCaretSample> Carets;
		TOptional<double> Baseline;
		TOptional<double> Pitch;
		FString Version;
	};

	struct FInk
	{
		bool bAny = false;
		FIntRect Bounds = FIntRect(0, 0, 0, 0);
		/** The sum of the coverage, sRGB-encoded as the pixels are: what the reports before linear ink compared. */
		double Mass = 0.0;
		/** The sum of the coverage in linear light: each pixel and the paper decoded before they are differenced. */
		double LinearMass = 0.0;
	};

	/** How crisp a picture's edges are; see MeasureCrispness. */
	struct FCrispness
	{
		bool bAny = false;
		/** Paper-to-ink transitions found reading the rows (vertical edges) and the columns (horizontal edges). */
		int32 RowTransitions = 0;
		int32 ColumnTransitions = 0;
		/** Mean pixels per transition strictly between paper and ink, along the rows and along the columns. */
		double EdgeWidthRows = 0.0;
		double EdgeWidthColumns = 0.0;
		/** #(GreyLow < c < GreyHigh) / #(c > GreyLow). */
		double GreyFraction = 0.0;
		/** The sum of |grad c| over the picture, over the ink mass. */
		double GradientPerInk = 0.0;
		/** Edges of strokes found reading the rows and the columns (two per stroke), and their mean width in pixels. */
		int32 RowStrokeEdges = 0;
		int32 ColumnStrokeEdges = 0;
		double StrokeEdgeRows = 0.0;
		double StrokeEdgeColumns = 0.0;
	};

	/** A picture of a fill case's solid mask. */
	struct FMaskPicture
	{
		bool bPicture = false;
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
	};

	/** How a fill case's colours stand against Chrome's, on the pixels both masks cover fully; see MeasureFill. */
	struct FFillMeasure
	{
		bool bMeasured = false;
		/** Why nothing was measured. */
		FString Why;
		/** The pixels both masks cover fully. */
		int32 Pixels = 0;
		/** Each pixel's largest channel difference between the two painted pictures, in 8-bit codes: mean and 95th percentile. */
		double MeanDifference = 0.0;
		int32 P95Difference = 0;
		/** The largest channel difference of the two average colours of a band along the axis, and which band (0 first). */
		double BandDifference = 0.0;
		int32 WorstBand = INDEX_NONE;
		/** The axis the bands are cut along: a linear gradient's angle, else horizontal. */
		FString Axis;
	};

	/** A leaf widget that paints one FSlateTextLayout in the box it was made for. */
	class SDreamParityText : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDreamParityText) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const FTextBlockStyle& InDefaultStyle, const FVector2D& InBox)
		{
			Box = InBox;
			Layout = FSlateTextLayout::Create(this, InDefaultStyle);
		}

		FSlateTextLayout& GetLayout() const
		{
			return *Layout;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return Box;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			// The DPI scale the renderer draws at, as STextBlock hands it to its layout.
			Layout->SetScale(AllottedGeometry.Scale);
			Layout->SetVisibleRegion(Box, FVector2D::ZeroVector);
			Layout->UpdateIfNeeded();
			return Layout->OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
		}

	private:
		TSharedPtr<FSlateTextLayout> Layout;
		FVector2D Box = FVector2D::ZeroVector;
	};

	/**
	 * Every composite font a case has made, kept for the length of the process. Slate caches what it knows of a
	 * composite font by its address, so a font freed and another allocated where it was would be drawn from the first
	 * one's cache. Slate's own font cache keeps the composite fonts it makes for as long as it runs, for the same reason.
	 */
	TArray<TSharedPtr<FStandaloneCompositeFont>>& KeptSlateFonts()
	{
		static TArray<TSharedPtr<FStandaloneCompositeFont>> Fonts;
		return Fonts;
	}

	/** Everything one case carries from step to step. */
	struct FCaseRun
	{
		FCorpus Corpus;
		FCase Case;
		FFontKey FontKey;
		FString PlainText;
		FEngineResult Dream;
		FEngineResult Slate;
		FEngineResult Chrome;
		/** A case's fill as FDreamGradient::ParseCss reads it; unset when the case has no fill. */
		TOptional<FDreamGradient> FillGradient;
		/** The text's style before the case's outline, shadow and fill were put on it: what a fill case's mask is drawn with. */
		FDreamTextStyle PlainStyle;
		/** A fill case's solid masks: DreamGUI's text drawn again with nothing painted, and Chrome's <case>.mask.png. */
		FMaskPicture DreamMask;
		FMaskPicture ChromeMask;
		TWeakObjectPtr<UDreamText> DreamText;
		TSharedPtr<FStandaloneCompositeFont> SlateFont;
		TSharedPtr<FSlateStyleSet> SlateStyles;
		TSharedPtr<FSlateColorBrush> SlatePaper;
		TSharedPtr<SDreamParityText> SlateText;
		TSharedPtr<SWidget> SlateRoot;
		FWidgetRenderer* SlateRenderer = nullptr;
		TStrongObjectPtr<UTextureRenderTarget2D> SlateTarget;

		FCaseRun()
		{
			Dream.Engine = TEXT("DreamGUI");
			Slate.Engine = TEXT("Slate");
			Chrome.Engine = TEXT("Chrome");
		}

		~FCaseRun()
		{
			ReleaseSlate();
		}

		void ReleaseSlate()
		{
			if (SlateRenderer != nullptr)
			{
				// The renderer's resources are the render thread's until a frame has passed.
				BeginCleanup(SlateRenderer);
				SlateRenderer = nullptr;
			}
			SlateRoot.Reset();
			SlateText.Reset();
			SlateTarget.Reset();
		}
	};

	EDreamUITextParagraphHorizontalAlign ToDreamAlign(const FString& InAlign)
	{
		if (InAlign.Equals(TEXT("center"), ESearchCase::IgnoreCase))
		{
			return EDreamUITextParagraphHorizontalAlign::Center;
		}
		if (InAlign.Equals(TEXT("end"), ESearchCase::IgnoreCase) || InAlign.Equals(TEXT("right"), ESearchCase::IgnoreCase))
		{
			return EDreamUITextParagraphHorizontalAlign::Right;
		}
		if (InAlign.Equals(TEXT("justify"), ESearchCase::IgnoreCase))
		{
			return EDreamUITextParagraphHorizontalAlign::Justify;
		}
		return EDreamUITextParagraphHorizontalAlign::Left;
	}

	EDreamTextJustify ToDreamTextJustify(const FString& InJustify)
	{
		if (InJustify.Equals(TEXT("inter-word"), ESearchCase::IgnoreCase)) { return EDreamTextJustify::InterWord; }
		if (InJustify.Equals(TEXT("inter-character"), ESearchCase::IgnoreCase)) { return EDreamTextJustify::InterCharacter; }
		if (InJustify.Equals(TEXT("none"), ESearchCase::IgnoreCase)) { return EDreamTextJustify::None; }
		return EDreamTextJustify::Auto;
	}

	EDreamTextLastLineAlign ToDreamLastLineAlign(const FString& InAlignLast)
	{
		if (InAlignLast.Equals(TEXT("start"), ESearchCase::IgnoreCase)) { return EDreamTextLastLineAlign::Start; }
		if (InAlignLast.Equals(TEXT("center"), ESearchCase::IgnoreCase)) { return EDreamTextLastLineAlign::Center; }
		if (InAlignLast.Equals(TEXT("end"), ESearchCase::IgnoreCase)) { return EDreamTextLastLineAlign::End; }
		if (InAlignLast.Equals(TEXT("justify"), ESearchCase::IgnoreCase)) { return EDreamTextLastLineAlign::Justify; }
		return EDreamTextLastLineAlign::Auto;
	}

	EDreamTextFlowDirection ToDreamDirection(const FString& InDir)
	{
		if (InDir.Equals(TEXT("rtl"), ESearchCase::IgnoreCase))
		{
			return EDreamTextFlowDirection::RightToLeft;
		}
		if (InDir.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			return EDreamTextFlowDirection::Auto;
		}
		return EDreamTextFlowDirection::LeftToRight;
	}

	ETextJustify::Type ToSlateJustify(const FString& InAlign)
	{
		if (InAlign.Equals(TEXT("center"), ESearchCase::IgnoreCase)) { return ETextJustify::Center; }
		if (InAlign.Equals(TEXT("end"), ESearchCase::IgnoreCase)) { return ETextJustify::Right; }
		if (InAlign.Equals(TEXT("left"), ESearchCase::IgnoreCase)) { return ETextJustify::InvariantLeft; }
		if (InAlign.Equals(TEXT("right"), ESearchCase::IgnoreCase)) { return ETextJustify::InvariantRight; }
		return ETextJustify::Left;
	}

	ETextFlowDirection ToSlateDirection(const FString& InDir)
	{
		if (InDir.Equals(TEXT("rtl"), ESearchCase::IgnoreCase)) { return ETextFlowDirection::RightToLeft; }
		if (InDir.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) { return ETextFlowDirection::Auto; }
		return ETextFlowDirection::LeftToRight;
	}

	bool IsEllipsisOverflow(const FCase& InCase)
	{
		return InCase.Overflow.Equals(TEXT("ellipsis"), ESearchCase::IgnoreCase) || InCase.Overflow.Equals(TEXT("middleEllipsis"), ESearchCase::IgnoreCase);
	}

	/**
	 * Why Slate has no equivalent of what the case shows, or empty when it has one. Slate does not justify, has no tab
	 * size (a tab is a fixed width of its own), chooses a composite font's sub-fonts by the game's culture rather than
	 * by a text's language, and cannot fill text with a gradient, so a case about any of those would compare Slate
	 * against something it cannot be asked for.
	 */
	FString GetSlateNotApplicable(const FCase& InCase, const FFontKey& InKey)
	{
		if (InCase.HasFill())
		{
			return TEXT("Slate has no gradient fill for text");
		}
		if (InCase.Align.Equals(TEXT("justify"), ESearchCase::IgnoreCase))
		{
			return TEXT("Slate has no justified alignment");
		}
		if (InCase.bTabSizeSet)
		{
			return TEXT("Slate has no tab size");
		}
		if (InKey.HasLanguageFaces() || (InCase.bRich && InCase.Text.Contains(TEXT("<lang="), ESearchCase::IgnoreCase)))
		{
			return TEXT("Slate chooses fallback faces by the game's culture, not by a text's language");
		}
		return FString();
	}

	/** How far a pixel is from the paper, 0..1, on its farthest channel. */
	double Coverage(const FColor& InPixel, const FColor& InPaper)
	{
		const int32 Delta = FMath::Max3(FMath::Abs(static_cast<int32>(InPixel.R) - InPaper.R), FMath::Abs(static_cast<int32>(InPixel.G) - InPaper.G),
			FMath::Abs(static_cast<int32>(InPixel.B) - InPaper.B));
		return static_cast<double>(Delta) / 255.0;
	}

	/** An 8-bit sRGB-encoded value in linear light, the exact transfer. */
	double DecodeSrgbCode(uint8 InCode)
	{
		static const TArray<double> Decoded = []()
		{
			TArray<double> Values;
			Values.SetNumUninitialized(256);
			for (int32 Code = 0; Code < 256; ++Code)
			{
				const double Encoded = static_cast<double>(Code) / 255.0;
				Values[Code] = Encoded <= 0.04045 ? Encoded / 12.92 : FMath::Pow((Encoded + 0.055) / 1.055, 2.4);
			}
			return Values;
		}();
		return Decoded[InCode];
	}

	/** How far a pixel is from the paper in linear light, 0..1, on its farthest channel: both decoded, then differenced. */
	double LinearCoverage(const FColor& InPixel, const FColor& InPaper)
	{
		return FMath::Max3(FMath::Abs(DecodeSrgbCode(InPixel.R) - DecodeSrgbCode(InPaper.R)), FMath::Abs(DecodeSrgbCode(InPixel.G) - DecodeSrgbCode(InPaper.G)),
			FMath::Abs(DecodeSrgbCode(InPixel.B) - DecodeSrgbCode(InPaper.B)));
	}

	FInk MeasureInk(const FEngineResult& InResult, const FColor& InPaper)
	{
		FInk Ink;
		if (!InResult.bPicture)
		{
			return Ink;
		}
		for (int32 Y = 0; Y < InResult.Size.Y; ++Y)
		{
			for (int32 X = 0; X < InResult.Size.X; ++X)
			{
				const FColor& Pixel = InResult.Pixels[Y * InResult.Size.X + X];
				const double Amount = Coverage(Pixel, InPaper);
				Ink.Mass += Amount;
				Ink.LinearMass += LinearCoverage(Pixel, InPaper);
				if (Amount > InkThreshold)
				{
					if (!Ink.bAny)
					{
						Ink.Bounds = FIntRect(X, Y, X + 1, Y + 1);
						Ink.bAny = true;
					}
					else
					{
						Ink.Bounds.Include(FIntPoint(X, Y));
						Ink.Bounds.Include(FIntPoint(X + 1, Y + 1));
					}
				}
			}
		}
		return Ink;
	}

	/**
	 * The strokes of one line of coverage values, a row or a column (InCount values from InStart, InStride apart): every
	 * run of values above EdgePaper whose peak p reaches StrokePeak, and the pixels of its two edges -- from where the run
	 * starts to where it first reaches StrokeEdgeHigh * p, and from where it last does to where it ends -- that are above
	 * max(StrokeEdgeLow * p, EdgePaper). Two edges a stroke, however thin: a one-pixel line that peaks at 0.83 counts as
	 * well as a stem that reaches full ink.
	 */
	void AddStrokeEdges(const TArray<double>& InValues, int32 InStart, int32 InStride, int32 InCount, int32& InOutEdges, double& InOutEdgePixels)
	{
		int32 Step = 0;
		while (Step < InCount)
		{
			if (InValues[InStart + Step * InStride] <= EdgePaper)
			{
				++Step;
				continue;
			}
			const int32 RunStart = Step;
			double Peak = 0.0;
			while (Step < InCount && InValues[InStart + Step * InStride] > EdgePaper)
			{
				Peak = FMath::Max(Peak, InValues[InStart + Step * InStride]);
				++Step;
			}
			if (Peak < StrokePeak)
			{
				continue;
			}
			const double High = StrokeEdgeHigh * Peak;
			const double Low = FMath::Max(StrokeEdgeLow * Peak, EdgePaper);
			int32 EdgePixels = 0;
			for (int32 Along = RunStart; Along < Step; ++Along)
			{
				const double Value = InValues[InStart + Along * InStride];
				if (Value >= High)
				{
					break;
				}
				EdgePixels += Value > Low ? 1 : 0;
			}
			for (int32 Along = Step - 1; Along >= RunStart; --Along)
			{
				const double Value = InValues[InStart + Along * InStride];
				if (Value >= High)
				{
					break;
				}
				EdgePixels += Value > Low ? 1 : 0;
			}
			InOutEdges += 2;
			InOutEdgePixels += EdgePixels;
		}
	}

	/**
	 * How sharp the edges of a picture are, the measures small text is judged by:
	 *  - edge width per stroke (AddStrokeEdges), reading each row and separately each column: the mean width of a
	 *    stroke's edges. What the targets hold DreamGUI to (strokeEdge);
	 *  - edge width per transition, the measure before it, reported alongside for one round: reading each row, and
	 *    separately each column, the pixels strictly between paper (c <= 0.1) and ink (c >= 0.9) wherever the line goes
	 *    from one to the other; their mean per transition. A run that leaves paper and comes back to it without reaching
	 *    ink is not a transition, so a thin stroke that peaks below 0.9 is not counted at all;
	 *  - grey fraction: of the pixels with any ink (c > 0.15), the share that is neither paper nor ink (c < 0.85);
	 *  - gradient: the sum over the picture of |grad c| (forward differences) over the ink mass, higher for sharper edges.
	 */
	FCrispness MeasureCrispness(const FEngineResult& InResult, const FColor& InPaper)
	{
		FCrispness Crisp;
		if (!InResult.bPicture || InResult.Size.X <= 1 || InResult.Size.Y <= 1)
		{
			return Crisp;
		}
		const int32 Width = InResult.Size.X;
		const int32 Height = InResult.Size.Y;
		TArray<double> Values;
		Values.SetNumUninitialized(Width * Height);
		double InkMass = 0.0;
		int32 Inked = 0;
		int32 Grey = 0;
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			const double Value = Coverage(InResult.Pixels[Index], InPaper);
			Values[Index] = Value;
			InkMass += Value;
			if (Value > GreyLow)
			{
				++Inked;
				Grey += Value < GreyHigh ? 1 : 0;
			}
		}
		// One line of samples, a row or a column: the edge pixels of every paper-to-ink and ink-to-paper transition on it.
		auto WalkLine = [&Values](int32 InStart, int32 InStride, int32 InCount, int32& InOutTransitions, double& InOutEdgePixels)
		{
			int32 State = -1;
			int32 Between = 0;
			for (int32 Step = 0; Step < InCount; ++Step)
			{
				const double Value = Values[InStart + Step * InStride];
				const int32 Solid = Value <= EdgePaper ? 0 : (Value >= EdgeInk ? 1 : -1);
				if (Solid < 0)
				{
					++Between;
					continue;
				}
				if (State >= 0 && Solid != State)
				{
					++InOutTransitions;
					InOutEdgePixels += Between;
				}
				State = Solid;
				Between = 0;
			}
		};
		double RowEdgePixels = 0.0;
		double ColumnEdgePixels = 0.0;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			WalkLine(Y * Width, 1, Width, Crisp.RowTransitions, RowEdgePixels);
		}
		for (int32 X = 0; X < Width; ++X)
		{
			WalkLine(X, Width, Height, Crisp.ColumnTransitions, ColumnEdgePixels);
		}
		double RowStrokePixels = 0.0;
		double ColumnStrokePixels = 0.0;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			AddStrokeEdges(Values, Y * Width, 1, Width, Crisp.RowStrokeEdges, RowStrokePixels);
		}
		for (int32 X = 0; X < Width; ++X)
		{
			AddStrokeEdges(Values, X, Width, Height, Crisp.ColumnStrokeEdges, ColumnStrokePixels);
		}
		Crisp.StrokeEdgeRows = Crisp.RowStrokeEdges > 0 ? RowStrokePixels / Crisp.RowStrokeEdges : 0.0;
		Crisp.StrokeEdgeColumns = Crisp.ColumnStrokeEdges > 0 ? ColumnStrokePixels / Crisp.ColumnStrokeEdges : 0.0;
		double Gradient = 0.0;
		for (int32 Y = 0; Y + 1 < Height; ++Y)
		{
			for (int32 X = 0; X + 1 < Width; ++X)
			{
				const double Here = Values[Y * Width + X];
				const double Dx = Values[Y * Width + X + 1] - Here;
				const double Dy = Values[(Y + 1) * Width + X] - Here;
				Gradient += FMath::Sqrt(Dx * Dx + Dy * Dy);
			}
		}
		Crisp.bAny = Inked > 0;
		Crisp.EdgeWidthRows = Crisp.RowTransitions > 0 ? RowEdgePixels / Crisp.RowTransitions : 0.0;
		Crisp.EdgeWidthColumns = Crisp.ColumnTransitions > 0 ? ColumnEdgePixels / Crisp.ColumnTransitions : 0.0;
		Crisp.GreyFraction = Inked > 0 ? static_cast<double>(Grey) / Inked : 0.0;
		Crisp.GradientPerInk = InkMass > 0.0 ? Gradient / InkMass : 0.0;
		return Crisp;
	}

	/** Pixels solid in one picture and not in the other, over the part both cover. */
	int32 CountMaskDifference(const FEngineResult& InA, const FEngineResult& InB, const FColor& InPaper)
	{
		if (!InA.bPicture || !InB.bPicture)
		{
			return -1;
		}
		const int32 Width = FMath::Min(InA.Size.X, InB.Size.X);
		const int32 Height = FMath::Min(InA.Size.Y, InB.Size.Y);
		int32 Different = 0;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const bool bA = Coverage(InA.Pixels[Y * InA.Size.X + X], InPaper) > MaskThreshold;
				const bool bB = Coverage(InB.Pixels[Y * InB.Size.X + X], InPaper) > MaskThreshold;
				Different += bA != bB ? 1 : 0;
			}
		}
		return Different;
	}

	/** White where neither is solid, black where both are, blue where only DreamGUI is, red where only Chrome is. */
	void MakeMaskPicture(const FEngineResult& InDream, const FEngineResult& InChrome, const FColor& InPaper, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		OutSize = FIntPoint(FMath::Min(InDream.Size.X, InChrome.Size.X), FMath::Min(InDream.Size.Y, InChrome.Size.Y));
		OutPixels.Init(FColor::White, OutSize.X * OutSize.Y);
		for (int32 Y = 0; Y < OutSize.Y; ++Y)
		{
			for (int32 X = 0; X < OutSize.X; ++X)
			{
				const bool bDream = Coverage(InDream.Pixels[Y * InDream.Size.X + X], InPaper) > MaskThreshold;
				const bool bChrome = Coverage(InChrome.Pixels[Y * InChrome.Size.X + X], InPaper) > MaskThreshold;
				FColor& Pixel = OutPixels[Y * OutSize.X + X];
				if (bDream && bChrome)
				{
					Pixel = FColor::Black;
				}
				else if (bDream)
				{
					Pixel = FColor(40, 90, 255, 255);
				}
				else if (bChrome)
				{
					Pixel = FColor(230, 40, 40, 255);
				}
			}
		}
	}

	/**
	 * A fill case's colours against Chrome's, on the pixels that both masks -- DreamGUI's and Chrome's solid picture of
	 * the same text -- cover at least FillCovered: each such pixel's largest channel difference between the two painted
	 * pictures, its mean and 95th percentile; and the pixels in FillBands bands of equal width along the gradient's axis
	 * (a linear fill's angle; horizontal for any other kind and for a rich text's runs), the largest channel difference
	 * between the two average colours of a band. Pixel noise along the glyphs' edges averages out in a band; a gradient
	 * placed, turned or mixed differently does not.
	 */
	FFillMeasure MeasureFill(const FCaseRun& InRun)
	{
		FFillMeasure Measure;
		const FIntPoint Expected = InRun.Case.GetDeviceCanvas();
		const FColor Paper = InRun.Case.GetPaper();
		if (!InRun.Dream.bPicture || !InRun.DreamMask.bPicture)
		{
			Measure.Why = TEXT("DreamGUI has no picture of the fill or of its mask");
			return Measure;
		}
		if (!InRun.Chrome.bPicture || !InRun.ChromeMask.bPicture)
		{
			Measure.Why = TEXT("Chrome has no picture of the fill or of its mask");
			return Measure;
		}
		if (InRun.Dream.Size != Expected || InRun.DreamMask.Size != Expected || InRun.Chrome.Size != Expected || InRun.ChromeMask.Size != Expected)
		{
			Measure.Why = FString::Printf(TEXT("the four pictures are not all the case's %d x %d"), Expected.X, Expected.Y);
			return Measure;
		}
		FVector2D Axis(1.0, 0.0);
		Measure.Axis = TEXT("horizontal");
		if (InRun.FillGradient.IsSet() && InRun.FillGradient->Type == EDreamPaintType::Linear)
		{
			// CSS's angle, 0 up and clockwise, in the picture's space (+Y down): what the gradient's line points along.
			const double Radians = FMath::DegreesToRadians(static_cast<double>(InRun.FillGradient->Angle));
			Axis = FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians));
			Measure.Axis = FString::Printf(TEXT("%gdeg"), InRun.FillGradient->Angle);
		}
		const int32 Width = Expected.X;
		TArray<int32> Covered;
		TArray<int32> Histogram;
		Histogram.Init(0, 256);
		double DifferenceSum = 0.0;
		double AlongMin = TNumericLimits<double>::Max();
		double AlongMax = TNumericLimits<double>::Lowest();
		auto AlongAxis = [&Axis, Width](int32 InIndex)
		{
			return (static_cast<double>(InIndex % Width) + 0.5) * Axis.X + (static_cast<double>(InIndex / Width) + 0.5) * Axis.Y;
		};
		for (int32 Index = 0; Index < Expected.X * Expected.Y; ++Index)
		{
			if (Coverage(InRun.DreamMask.Pixels[Index], Paper) < FillCovered || Coverage(InRun.ChromeMask.Pixels[Index], Paper) < FillCovered)
			{
				continue;
			}
			const FColor& DreamPixel = InRun.Dream.Pixels[Index];
			const FColor& ChromePixel = InRun.Chrome.Pixels[Index];
			const int32 Difference = FMath::Max3(FMath::Abs(static_cast<int32>(DreamPixel.R) - ChromePixel.R), FMath::Abs(static_cast<int32>(DreamPixel.G) - ChromePixel.G),
				FMath::Abs(static_cast<int32>(DreamPixel.B) - ChromePixel.B));
			++Histogram[Difference];
			DifferenceSum += Difference;
			Covered.Add(Index);
			const double Along = AlongAxis(Index);
			AlongMin = FMath::Min(AlongMin, Along);
			AlongMax = FMath::Max(AlongMax, Along);
		}
		if (Covered.Num() == 0)
		{
			Measure.Why = TEXT("no pixel is fully covered in both masks");
			return Measure;
		}
		Measure.bMeasured = true;
		Measure.Pixels = Covered.Num();
		Measure.MeanDifference = DifferenceSum / Covered.Num();
		const int32 Rank = FMath::Max(1, FMath::CeilToInt32(0.95 * Covered.Num()));
		int32 Counted = 0;
		for (int32 Difference = 0; Difference < Histogram.Num(); ++Difference)
		{
			Counted += Histogram[Difference];
			if (Counted >= Rank)
			{
				Measure.P95Difference = Difference;
				break;
			}
		}
		struct FFillBand
		{
			double DreamSum[3] = { 0.0, 0.0, 0.0 };
			double ChromeSum[3] = { 0.0, 0.0, 0.0 };
			int32 Count = 0;
		};
		TArray<FFillBand> Bands;
		Bands.SetNum(FillBands);
		const double Span = AlongMax - AlongMin;
		for (const int32 Index : Covered)
		{
			const int32 BandIndex = Span > 0.0 ? FMath::Clamp(FMath::FloorToInt32((AlongAxis(Index) - AlongMin) / Span * FillBands), 0, FillBands - 1) : 0;
			FFillBand& Band = Bands[BandIndex];
			const FColor& DreamPixel = InRun.Dream.Pixels[Index];
			const FColor& ChromePixel = InRun.Chrome.Pixels[Index];
			Band.DreamSum[0] += DreamPixel.R;
			Band.DreamSum[1] += DreamPixel.G;
			Band.DreamSum[2] += DreamPixel.B;
			Band.ChromeSum[0] += ChromePixel.R;
			Band.ChromeSum[1] += ChromePixel.G;
			Band.ChromeSum[2] += ChromePixel.B;
			++Band.Count;
		}
		for (int32 BandIndex = 0; BandIndex < Bands.Num(); ++BandIndex)
		{
			const FFillBand& Band = Bands[BandIndex];
			if (Band.Count == 0)
			{
				continue;
			}
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				const double Apart = FMath::Abs(Band.DreamSum[Channel] - Band.ChromeSum[Channel]) / Band.Count;
				if (Apart > Measure.BandDifference || Measure.WorstBand == INDEX_NONE)
				{
					Measure.BandDifference = Apart;
					Measure.WorstBand = BandIndex;
				}
			}
		}
		return Measure;
	}

	/** DreamGUI | Slate | Chrome, each in a panel the size of the device canvas, grey between them; n/a panels hatched. */
	void ComposeComparison(const FCaseRun& InRun, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		const FIntPoint Panel = InRun.Case.GetDeviceCanvas();
		OutSize = FIntPoint(Panel.X * 3 + SeparatorWidth * 2, Panel.Y);
		OutPixels.Init(SeparatorColour, OutSize.X * OutSize.Y);
		const FEngineResult* Results[] = { &InRun.Dream, &InRun.Slate, &InRun.Chrome };
		for (int32 PanelIndex = 0; PanelIndex < 3; ++PanelIndex)
		{
			const FEngineResult& Result = *Results[PanelIndex];
			const bool bNotApplicable = !Result.NotApplicable.IsEmpty();
			const int32 Left = PanelIndex * (Panel.X + SeparatorWidth);
			for (int32 Y = 0; Y < Panel.Y; ++Y)
			{
				for (int32 X = 0; X < Panel.X; ++X)
				{
					const bool bInside = Result.bPicture && X < Result.Size.X && Y < Result.Size.Y;
					FColor Pixel = MissingPanelColour;
					if (bInside)
					{
						Pixel = Result.Pixels[Y * Result.Size.X + X];
					}
					else if (bNotApplicable && ((X + Y) / 6) % 2 == 0)
					{
						Pixel = NotApplicableStripeColour;
					}
					OutPixels[Y * OutSize.X + Left + X] = Pixel;
				}
			}
		}
	}

	/**
	 * The DreamGUI text for a case, its box's top-left at (padding, padding) of the canvas. A fill is the text's FacePaint,
	 * measured across the text as a block on both axes (the paint boxes' default), CSS's background box of the paragraph.
	 */
	UDreamText* AddDreamText(FGalleryStage& InStage, FCaseRun& InOutRun, UDreamUIFontData_BaseObject* InFont)
	{
		const FCase& Case = InOutRun.Case;
		const int32 Padding = InOutRun.Corpus.Padding;
		FVector2D Box = Case.GetBox(Padding);
		const bool bClamp = Case.Overflow.Equals(TEXT("clamp"), ESearchCase::IgnoreCase) && Case.MaxLines > 0;
		if (bClamp)
		{
			// As many lines as the clamp keeps: DreamGUI cuts at the box, not at a line count.
			const float Pitch = InFont->GetLineHeight(Case.Size) * (Case.LineHeight > 0.0f ? Case.LineHeight : 1.0f);
			Box.Y = FMath::Max(1.0, static_cast<double>(Pitch) * Case.MaxLines + 1.0);
		}
		UDreamWidget* Widget = InStage.AddWidget(TEXT("ParityText"), Box, FVector2D::ZeroVector);
		FDreamUIAnchorData Anchor = Widget->GetAnchorData();
		Anchor.AnchorMin = FVector2D(0.0, 1.0);
		Anchor.AnchorMax = FVector2D(0.0, 1.0);
		Anchor.Pivot = FVector2D(0.0, 1.0);
		Anchor.AnchoredPosition = FVector2D(static_cast<double>(Padding), -static_cast<double>(Padding));
		Anchor.SizeDelta = Box;
		Widget->SetAnchorData(Anchor);

		UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
		if (Text == nullptr)
		{
			return nullptr;
		}
		Text->SetFont(InFont);
		Text->SetFontSize(Case.Size);
		Text->SetColor(Case.GetInk());
		Text->SetRichText(Case.bRich);
		// The case's language, as Chrome's page has it in its lang attribute: it picks the fallback faces meant for it and
		// the forms HarfBuzz draws (locl).
		Text->SetLanguage(Case.Lang);
		Text->SetParagraphHorizontalAlignment(ToDreamAlign(Case.Align));
		Text->SetTextJustify(ToDreamTextJustify(Case.TextJustify));
		Text->SetLastLineAlign(ToDreamLastLineAlign(Case.TextAlignLast));
		Text->SetTabSize(Case.TabSize);
		Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Top);
		Text->SetFlowDirection(ToDreamDirection(Case.Dir));
		Text->SetFontSpace(FVector2D(Case.LetterSpacing, 0.0));
		Text->SetLineHeightPercentage(Case.LineHeight > 0.0f ? Case.LineHeight : 1.0f);
		Text->SetWrappingPolicy(Case.Wrap.Equals(TEXT("normal"), ESearchCase::IgnoreCase) ? ETextWrappingPolicy::DefaultWrapping : ETextWrappingPolicy::AllowPerCharacterWrapping);
		if (Case.Overflow.Equals(TEXT("ellipsis"), ESearchCase::IgnoreCase))
		{
			Text->SetOverflowType(EDreamUITextOverflowType::Ellipsis);
		}
		else if (Case.Overflow.Equals(TEXT("middleEllipsis"), ESearchCase::IgnoreCase))
		{
			Text->SetOverflowType(EDreamUITextOverflowType::MiddleEllipsis);
		}
		else if (bClamp)
		{
			Text->SetOverflowType(EDreamUITextOverflowType::Ellipsis);
			Text->SetAutoWrapText(true);
		}
		else
		{
			Text->SetOverflowType(Case.Width > 0.0f ? EDreamUITextOverflowType::VerticalOverflow : EDreamUITextOverflowType::HorizontalOverflow);
		}
		if (Case.Transform.Equals(TEXT("uppercase"), ESearchCase::IgnoreCase))
		{
			Text->SetTextTransform(EDreamUITextTransformPolicy::ToUpper);
		}
		if (Case.SmallTextRaster.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Text->SetSmallTextRaster(EDreamTextSmallTextRaster::Off);
		}
		InOutRun.PlainStyle = Text->GetTextStyle();
		if (Case.OutlineEm > 0.0f || Case.bShadow || InOutRun.FillGradient.IsSet())
		{
			FDreamTextStyle Style = Text->GetTextStyle();
			if (Case.OutlineEm > 0.0f)
			{
				Style.OutlineColor = Case.OutlineColor;
				Style.OutlineWidth = Case.OutlineEm;
			}
			if (Case.bShadow)
			{
				// A hard shadow, as Chrome's text-shadow with no blur: the underlay's offset is in em with +Y down, as the case's.
				Style.UnderlayColor = Case.ShadowColor;
				Style.UnderlayOffset = Case.ShadowOffsetEm;
				Style.UnderlaySoftness = 0.0f;
				Style.UnderlayDilate = 0.0f;
			}
			if (InOutRun.FillGradient.IsSet())
			{
				// Chrome's color: transparent with background-clip: text: the face is the gradient, the text's colour unused.
				Style.FacePaint.bEnabled = true;
				Style.FacePaint.Gradient = InOutRun.FillGradient.GetValue();
			}
			Text->SetTextStyle(Style);
		}
		Text->SetText(FText::FromString(Case.Text));
		return Text;
	}

	/**
	 * Turns a fill case's text into its mask: the style it had before the case's outline, shadow and fill, in the ink
	 * colour, a rich text's <gradient=...> tags taken out -- the same glyphs, solid, as Chrome's mask page draws them.
	 */
	void ShowDreamMask(FCaseRun& InOutRun)
	{
		UDreamText* Text = InOutRun.DreamText.Get();
		if (Text == nullptr)
		{
			InOutRun.Dream.Notes.Add(TEXT("the text is gone, so there is no mask"));
			return;
		}
		Text->SetTextStyle(InOutRun.PlainStyle);
		Text->SetColor(InOutRun.Case.GetInk());
		if (InOutRun.Case.bRich)
		{
			Text->SetText(FText::FromString(GetMaskText(InOutRun.Case)));
		}
	}

	/** DreamGUI's numbers, read off the text's display list once the stage has settled. */
	void ReadDreamNumbers(FCaseRun& InOutRun)
	{
		FEngineResult& Result = InOutRun.Dream;
		const UDreamText* Text = InOutRun.DreamText.Get();
		if (Text == nullptr)
		{
			Result.Notes.Add(TEXT("the text is gone"));
			return;
		}
		const FDreamTextDisplayList& List = Text->GetCacheTextGeometryData().GetDisplayList();
		const double Padding = static_cast<double>(InOutRun.Corpus.Padding);
		// A rich text's carets are offsets into its markup; the rest of the report counts UTF-16 units of the text with
		// the markup taken out, which is what Chrome measures. The parse that takes the markup out says where in the
		// markup each unit came from, and a caret maps to the first unit read at or after its offset.
		const bool bRich = InOutRun.Case.bRich;
		TArray<int32> SourceOffsets;
		if (bRich)
		{
			TArray<FRichRun> Runs;
			FString Plain;
			ParseRichText(InOutRun.Case.Text, InOutRun.Case.Size, Runs, Plain, &SourceOffsets);
		}
		auto ToOffset = [bRich, &SourceOffsets](int32 InCharIndex)
		{
			if (!bRich)
			{
				return InCharIndex;
			}
			for (int32 Unit = 0; Unit < SourceOffsets.Num(); ++Unit)
			{
				if (SourceOffsets[Unit] >= InCharIndex)
				{
					return Unit;
				}
			}
			return SourceOffsets.Num();
		};
		TArray<double> LineCentres;
		for (const FDreamUITextLineProperty& Line : List.Lines)
		{
			int32 LineStart = MAX_int32;
			double CentreSum = 0.0;
			int32 CentreCount = 0;
			for (const FDreamUITextCaretProperty& Caret : Line.CaretPropertyList)
			{
				CentreSum += Caret.CaretPosition.Y;
				++CentreCount;
				if (Caret.CharIndex < 0)
				{
					continue;
				}
				const int32 Offset = ToOffset(Caret.CharIndex);
				LineStart = FMath::Min(LineStart, Offset);
				if (!Result.Carets.Contains(Offset))
				{
					FCaretSample Sample;
					Sample.X = Padding + Caret.CaretPosition.X;
					Sample.Y = Padding - Caret.CaretPosition.Y;
					Result.Carets.Add(Offset, Sample);
				}
			}
			if (LineStart != MAX_int32)
			{
				Result.LineStarts.Add(LineStart);
			}
			if (CentreCount > 0)
			{
				LineCentres.Add(Padding - CentreSum / CentreCount);
			}
		}
		if (LineCentres.Num() >= 2)
		{
			Result.Pitch = (LineCentres.Last() - LineCentres[0]) / static_cast<double>(LineCentres.Num() - 1);
		}
		TArray<double> Pens;
		for (const FDreamTextGlyphItem& Item : List.Items)
		{
			if (Item.LineIndex == 0 && Item.Kind == EDreamTextItemKind::Glyph)
			{
				Pens.Add(Item.Pen.Y);
			}
		}
		if (Pens.Num() > 0)
		{
			// The median: a combining mark's pen carries its own vertical offset, the base glyphs' do not.
			Pens.Sort();
			Result.Baseline = Padding - Pens[Pens.Num() / 2];
		}
		Result.bNumbers = Result.LineStarts.Num() > 0;
	}

	/**
	 * Slate's composite font for a case: the key's primary as the default typeface, each fallback over its share of the
	 * text -- the code points it is the first face to draw in the order a text in the case's language tries them -- at
	 * its scale (Slate's ScalingFactor, which scales the glyphs and not the line, unlike CSS's size-adjust).
	 */
	TSharedPtr<FStandaloneCompositeFont> MakeSlateFont(const FFontKey& InKey, const FString& InPlainText, const FString& InLanguage)
	{
		TSharedPtr<FStandaloneCompositeFont> Font = MakeShared<FStandaloneCompositeFont>();
		Font->DefaultTypeface.AppendFont(TEXT("Regular"), InKey.Faces[0].File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		if (!InKey.Bold.IsEmpty())
		{
			Font->DefaultTypeface.AppendFont(TEXT("Bold"), InKey.Bold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		if (!InKey.Italic.IsEmpty())
		{
			Font->DefaultTypeface.AppendFont(TEXT("Italic"), InKey.Italic, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		if (!InKey.BoldItalic.IsEmpty())
		{
			Font->DefaultTypeface.AppendFont(TEXT("BoldItalic"), InKey.BoldItalic, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		TArray<uint32> Codepoints;
		TArray<int32> Offsets;
		DecodeCodepoints(InPlainText, Codepoints, Offsets);
		TArray<TArray<uint32>> PerFace;
		PerFace.SetNum(InKey.Faces.Num());
		for (const uint32 Codepoint : Codepoints)
		{
			const int32 Face = FindCoveringFace(InKey, Codepoint, InLanguage);
			if (Face > 0)
			{
				PerFace[Face].AddUnique(Codepoint);
			}
		}
		for (int32 Face = 1; Face < InKey.Faces.Num(); ++Face)
		{
			TArray<uint32>& Assigned = PerFace[Face];
			if (Assigned.Num() == 0)
			{
				continue;
			}
			Assigned.Sort();
			FCompositeSubFont& SubFont = Font->SubTypefaces.AddDefaulted_GetRef();
			SubFont.Typeface.AppendFont(TEXT("Regular"), InKey.Faces[Face].File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			SubFont.ScalingFactor = InKey.Faces[Face].Scale;
			int32 RangeStart = static_cast<int32>(Assigned[0]);
			int32 RangeEnd = RangeStart;
			for (int32 Index = 1; Index <= Assigned.Num(); ++Index)
			{
				const int32 Next = Index < Assigned.Num() ? static_cast<int32>(Assigned[Index]) : -1;
				if (Next == RangeEnd + 1)
				{
					RangeEnd = Next;
					continue;
				}
				SubFont.CharacterRanges.Add(FInt32Range::Inclusive(RangeStart, RangeEnd));
				RangeStart = Next;
				RangeEnd = Next;
			}
		}
		KeptSlateFonts().Add(Font);
		return Font;
	}

	FTextBlockStyle MakeSlateStyle(const FCaseRun& InRun, float InSizePx, bool bInBold, bool bInItalic, bool bInUnderline, bool bInStrike,
		const TOptional<FColor>& InColour)
	{
		const FCase& Case = InRun.Case;
		const FFontKey& Key = InRun.FontKey;
		FName Typeface(TEXT("Regular"));
		if (bInBold && bInItalic && !Key.BoldItalic.IsEmpty())
		{
			Typeface = FName(TEXT("BoldItalic"));
		}
		else if (bInBold && !Key.Bold.IsEmpty())
		{
			Typeface = FName(TEXT("Bold"));
		}
		else if (bInItalic && !Key.Italic.IsEmpty())
		{
			Typeface = FName(TEXT("Italic"));
		}
		FSlateFontInfo FontInfo(InRun.SlateFont, InSizePx * 0.75f, Typeface);
		// Thousandths of the run's size in points: Slate's shaper adds LetterSpacing * Size / 1000 to each advance.
		FontInfo.LetterSpacing = InSizePx > 0.0f ? FMath::RoundToInt32(Case.LetterSpacing * 1000.0f / (InSizePx * 0.75f)) : 0;
		if (Case.OutlineEm > 0.0f)
		{
			FontInfo.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt32(Case.OutlineEm * InSizePx));
			FontInfo.OutlineSettings.OutlineColor = FLinearColor(Case.OutlineColor);
			FontInfo.OutlineSettings.bMiteredCorners = true;
		}
		FTextBlockStyle Style;
		Style.SetFont(FontInfo);
		Style.SetColorAndOpacity(FSlateColor(FLinearColor(InColour.IsSet() ? InColour.GetValue() : Case.GetInk())));
		if (Case.bShadow)
		{
			// In layout units, which the case's em make at the run's own size.
			Style.SetShadowOffset(FVector2D(Case.ShadowOffsetEm.X * InSizePx, Case.ShadowOffsetEm.Y * InSizePx));
			Style.SetShadowColorAndOpacity(FLinearColor(Case.ShadowColor));
		}
		// The application's style, which Slate's text blocks draw their lines with. FCoreStyle::GetCoreStyle() has no
		// instance in the editor, whose core style is Starship's, so it would dereference null here.
		const FSlateBrush* LineBrush = FAppStyle::Get().GetBrush(TEXT("DefaultTextUnderline"));
		if (bInUnderline && LineBrush != nullptr)
		{
			Style.SetUnderlineBrush(*LineBrush);
		}
		if (bInStrike && LineBrush != nullptr)
		{
			Style.SetStrikeBrush(*LineBrush);
		}
		return Style;
	}

	FString EscapeForSlateMarkup(const FString& InText)
	{
		FString Escaped;
		Escaped.Reserve(InText.Len());
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('&'): Escaped += TEXT("&amp;"); break;
			case TEXT('<'): Escaped += TEXT("&lt;"); break;
			case TEXT('>'): Escaped += TEXT("&gt;"); break;
			case TEXT('"'): Escaped += TEXT("&quot;"); break;
			default: Escaped.AppendChar(Char); break;
			}
		}
		return Escaped;
	}

	/** Lays the case out in Slate and puts the widget that paints it on paper of the case's colour. */
	void BuildSlate(FCaseRun& InOutRun)
	{
		const FCase& Case = InOutRun.Case;
		const int32 Padding = InOutRun.Corpus.Padding;
		const FVector2D Box = Case.GetBox(Padding);
		InOutRun.SlateFont = MakeSlateFont(InOutRun.FontKey, InOutRun.PlainText, Case.Lang);
		const FTextBlockStyle DefaultStyle = MakeSlateStyle(InOutRun, Case.Size, false, false, false, false, TOptional<FColor>());

		TSharedRef<SDreamParityText> TextWidget = SNew(SDreamParityText, DefaultStyle, Box);
		FSlateTextLayout& Layout = TextWidget->GetLayout();
		Layout.SetScale(static_cast<float>(Case.GetDeviceScale()));
		// An elided line is one line, as Chrome's white-space: pre and DreamGUI's no-wrap make it; Slate wraps at whatever
		// width it is given first and only then elides.
		Layout.SetWrappingWidth(Case.Width > 0.0f && !IsEllipsisOverflow(Case) ? Case.Width : 0.0f);
		Layout.SetWrappingPolicy(Case.Wrap.Equals(TEXT("normal"), ESearchCase::IgnoreCase) ? ETextWrappingPolicy::DefaultWrapping : ETextWrappingPolicy::AllowPerCharacterWrapping);
		Layout.SetLineHeightPercentage(Case.LineHeight > 0.0f ? Case.LineHeight : 1.0f);
		Layout.SetJustification(ToSlateJustify(Case.Align));
		Layout.SetTextFlowDirection(ToSlateDirection(Case.Dir));
		if (Case.Overflow.Equals(TEXT("ellipsis"), ESearchCase::IgnoreCase))
		{
			Layout.SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		}
		else if (Case.Overflow.Equals(TEXT("middleEllipsis"), ESearchCase::IgnoreCase))
		{
			Layout.SetTextOverflowPolicy(ETextOverflowPolicy::MiddleEllipsis);
		}
		else if (Case.Overflow.Equals(TEXT("clamp"), ESearchCase::IgnoreCase))
		{
			Layout.SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);
			InOutRun.Slate.Notes.Add(TEXT("the clamp is Slate's multi-line ellipsis over the whole box, not a line count"));
		}
		if (!Case.Overflow.IsEmpty())
		{
			// Slate elides against the paint's culling rect: a widget that does not clip has the whole canvas for one.
			TextWidget->SetClipping(EWidgetClipping::ClipToBounds);
		}
		Layout.SetVisibleRegion(Box, FVector2D::ZeroVector);

		if (Case.bRich)
		{
			TArray<FRichRun> Runs;
			FString Plain;
			ParseRichText(Case.Text, Case.Size, Runs, Plain);
			InOutRun.SlateStyles = MakeShared<FSlateStyleSet>(FName(*FString::Printf(TEXT("DreamTextParity_%s"), *Case.Id)));
			FString Markup;
			bool bSupOrSub = false;
			for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
			{
				const FRichRun& Run = Runs[RunIndex];
				const FRichStyle& RunStyle = Run.Style;
				// A superscript or subscript keeps only its smaller size: Slate has no way to raise or lower a run.
				const float Size = RunStyle.SupOrSub != 0 ? RunStyle.Size / 1.2f : RunStyle.Size;
				bSupOrSub |= RunStyle.SupOrSub != 0;
				TOptional<FColor> Colour;
				if (RunStyle.bHasColor)
				{
					Colour = RunStyle.Color;
				}
				else if (RunStyle.bLink)
				{
					Colour = FColor(6, 69, 173, 255);
				}
				const FName StyleName(*FString::Printf(TEXT("s%d"), RunIndex));
				InOutRun.SlateStyles->Set(StyleName, MakeSlateStyle(InOutRun, Size, RunStyle.bBold, RunStyle.bItalic,
					RunStyle.bUnderline || RunStyle.bLink, RunStyle.bStrikethrough, Colour));
				Markup += FString::Printf(TEXT("<%s>%s</>"), *StyleName.ToString(), *EscapeForSlateMarkup(Run.Text));
			}
			if (bSupOrSub)
			{
				InOutRun.Slate.Notes.Add(TEXT("superscript and subscript are drawn smaller on the baseline"));
			}
			TArray<TSharedRef<ITextDecorator>> Decorators;
			TSharedRef<FRichTextLayoutMarshaller> Marshaller = FRichTextLayoutMarshaller::Create(Decorators, InOutRun.SlateStyles.Get());
			Marshaller->SetText(Markup, Layout);
		}
		else
		{
			TSharedRef<FPlainTextLayoutMarshaller> Marshaller = FPlainTextLayoutMarshaller::Create();
			// Upper-cased here rather than through Slate's transform policy: the policy assumes the case mapping keeps the
			// length (TextLayout.cpp ensures it), and "straße" becomes one letter longer.
			const bool bUpper = Case.Transform.Equals(TEXT("uppercase"), ESearchCase::IgnoreCase);
			Marshaller->SetText(bUpper ? FText::FromString(Case.Text).ToUpper().ToString() : Case.Text, Layout);
		}
		Layout.UpdateIfNeeded();

		InOutRun.SlatePaper = MakeShared<FSlateColorBrush>(FLinearColor(Case.GetPaper()));
		InOutRun.SlateText = TextWidget;
		InOutRun.SlateRoot = SNew(SBorder)
			.BorderImage(InOutRun.SlatePaper.Get())
			.Padding(FMargin(static_cast<float>(Padding), static_cast<float>(Padding), 0.0f, 0.0f))
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			[
				TextWidget
			];
	}

	/*
	 * Slate draws here the way it draws into a window: its shader applies the display gamma
	 * (FWidgetRenderer(true)), and the target stores what the shader wrote as it is -- an 8-bit target
	 * created with bForceLinearGamma, which UTextureRenderTarget2D::IsSRGB reads as "not sRGB". The bytes
	 * read back are then the colours the case authored, and glyph edges blend in gamma space, as they do
	 * on screen. FWidgetRenderer::CreateTargetFor(..., true) is not that target: it sets SRGB to false but
	 * hands InitCustomFormat bForceLinearGamma = false, and a target with an override format takes its
	 * sRGB-ness from that flag alone, so the hardware encoded the shader's already encoded colours a second
	 * time and every colour between black and white came out lighter.
	 *
	 * The target is the device canvas, and the widget is drawn at the case's scale: Slate lays it out in canvas units,
	 * the way a DPI scale has it.
	 */
	void DrawSlate(FCaseRun& InOutRun)
	{
		if (!InOutRun.SlateRoot.IsValid())
		{
			return;
		}
		const FIntPoint Device = InOutRun.Case.GetDeviceCanvas();
		const FVector2D DrawSize(Device);
		if (InOutRun.SlateRenderer == nullptr)
		{
			InOutRun.SlateRenderer = new FWidgetRenderer(true, true);
		}
		if (!InOutRun.SlateTarget.IsValid())
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->Filter = TF_Nearest;
			Target->ClearColor = FLinearColor::Transparent;
			Target->InitCustomFormat(static_cast<uint32>(Device.X), static_cast<uint32>(Device.Y), EPixelFormat::PF_B8G8R8A8, true);
			Target->UpdateResourceImmediate(true);
			InOutRun.SlateTarget.Reset(Target);
		}
		if (InOutRun.SlateTarget.IsValid())
		{
			InOutRun.SlateRenderer->DrawWidget(InOutRun.SlateTarget.Get(), InOutRun.SlateRoot.ToSharedRef(),
				static_cast<float>(InOutRun.Case.GetDeviceScale()), DrawSize, 0.0f);
		}
	}

	/** Slate's numbers, from the same layout its picture was painted from, turned from its scaled units into canvas pixels. */
	void ReadSlateNumbers(FCaseRun& InOutRun)
	{
		FEngineResult& Result = InOutRun.Slate;
		if (!InOutRun.SlateText.IsValid())
		{
			return;
		}
		const FSlateTextLayout& Layout = InOutRun.SlateText->GetLayout();
		const double Padding = static_cast<double>(InOutRun.Corpus.Padding);
		const float Scale = Layout.GetScale();
		const double InverseScale = Scale > 0.0f ? 1.0 / static_cast<double>(Scale) : 1.0;
		const TArray<FTextLayout::FLineModel>& Models = Layout.GetLineModels();
		TArray<int32> ModelStarts;
		int32 Accumulated = 0;
		for (const FTextLayout::FLineModel& Model : Models)
		{
			ModelStarts.Add(Accumulated);
			Accumulated += Model.Text->Len() + 1;
		}
		const TArray<FTextLayout::FLineView>& Views = Layout.GetLineViews();
		for (const FTextLayout::FLineView& View : Views)
		{
			if (ModelStarts.IsValidIndex(View.ModelIndex))
			{
				Result.LineStarts.Add(ModelStarts[View.ModelIndex] + View.Range.BeginIndex);
			}
		}
		for (int32 ModelIndex = 0; ModelIndex < Models.Num(); ++ModelIndex)
		{
			const int32 Length = Models[ModelIndex].Text->Len();
			for (int32 Local = 0; Local <= Length; ++Local)
			{
				const FVector2D Location = Layout.GetLocationAt(FTextLocation(ModelIndex, Local), false);
				FCaretSample Sample;
				Sample.X = Padding + Location.X * InverseScale;
				Sample.Y = Padding + Location.Y * InverseScale;
				Result.Carets.Add(ModelStarts[ModelIndex] + Local, Sample);
			}
		}
		if (Views.Num() > 0)
		{
			int32 AboveBaseline = 0;
			for (const TSharedRef<ILayoutBlock>& Block : Views[0].Blocks)
			{
				const TSharedRef<IRun> Run = Block->GetRun();
				AboveBaseline = FMath::Max(AboveBaseline, static_cast<int32>(Run->GetMaxHeight(Scale)) + static_cast<int32>(Run->GetBaseLine(Scale)));
			}
			Result.Baseline = Padding + (Views[0].Offset.Y + AboveBaseline) * InverseScale;
			Result.Pitch = (Views.Num() >= 2 ? Views[1].Offset.Y - Views[0].Offset.Y : Views[0].Size.Y) * InverseScale;
		}
		if (InOutRun.Case.Transform.Equals(TEXT("uppercase"), ESearchCase::IgnoreCase))
		{
			Result.Notes.Add(TEXT("Slate is given the upper-cased text, so its offsets count the upper-cased text"));
		}
		if (InOutRun.PlainText.Contains(TEXT("\t")))
		{
			Result.Notes.Add(TEXT("Slate's tab is a width of its own: it has no tab stops"));
		}
		Result.bNumbers = Result.LineStarts.Num() > 0;
	}

	/** Chrome's picture and numbers, when the reference script has made them. */
	void ReadChrome(FCaseRun& InOutRun)
	{
		FEngineResult& Result = InOutRun.Chrome;
		if (InOutRun.Case.IsHeldToSlate())
		{
			Result.NotApplicable = TEXT("the case is held to Slate: Chrome cannot draw it");
			Result.Notes.Add(FString::Printf(TEXT("n/a: %s"), *Result.NotApplicable));
			return;
		}
		const FString Directory = GetChromeReferenceDirectory();
		const FString PngPath = FPaths::Combine(Directory, InOutRun.Case.Id + TEXT(".png"));
		const FString JsonPath = FPaths::Combine(Directory, InOutRun.Case.Id + TEXT(".json"));
		if (!FPaths::FileExists(PngPath) || !FPaths::FileExists(JsonPath))
		{
			Result.Notes.Add(FString::Printf(TEXT("no reference at %s; Tools/TextParity/Make-ChromeReference.ps1 makes it"), *JsonPath));
			return;
		}
		Result.bPicture = FDreamPixelProbe::LoadPng(PngPath, Result.Pixels, Result.Size);
		if (!Result.bPicture)
		{
			Result.Notes.Add(FString::Printf(TEXT("%s could not be read"), *PngPath));
		}
		else if (Result.Size != InOutRun.Case.GetDeviceCanvas())
		{
			Result.Notes.Add(FString::Printf(TEXT("the reference picture is %d x %d, not the case's %d x %d: it was made from another corpus"),
				Result.Size.X, Result.Size.Y, InOutRun.Case.GetDeviceCanvas().X, InOutRun.Case.GetDeviceCanvas().Y));
		}
		if (InOutRun.Case.HasFill())
		{
			// The same text solid, in the ink colour: which pixels Chrome's face covers fully.
			const FString MaskPath = FPaths::Combine(Directory, InOutRun.Case.Id + TEXT(".mask.png"));
			FMaskPicture& Mask = InOutRun.ChromeMask;
			Mask.bPicture = FPaths::FileExists(MaskPath) && FDreamPixelProbe::LoadPng(MaskPath, Mask.Pixels, Mask.Size);
			if (!Mask.bPicture)
			{
				Result.Notes.Add(FString::Printf(TEXT("no mask reference at %s; Tools/TextParity/Make-ChromeReference.ps1 makes it"), *MaskPath));
			}
		}
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *JsonPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			Result.Notes.Add(FString::Printf(TEXT("%s could not be read"), *JsonPath));
			return;
		}
		Root->TryGetStringField(TEXT("chrome"), Result.Version);
		bool bFontsLoaded = true;
		if (Root->TryGetBoolField(TEXT("fontsLoaded"), bFontsLoaded) && !bFontsLoaded)
		{
			Result.Notes.Add(TEXT("Chrome reported a font that did not load: its picture used another"));
		}
		const TArray<TSharedPtr<FJsonValue>>* LineStarts = nullptr;
		if (Root->TryGetArrayField(TEXT("lineStarts"), LineStarts) && LineStarts != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *LineStarts)
			{
				if (Value.IsValid())
				{
					Result.LineStarts.Add(FMath::RoundToInt32(Value->AsNumber()));
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Carets = nullptr;
		if (Root->TryGetArrayField(TEXT("carets"), Carets) && Carets != nullptr)
		{
			for (int32 Offset = 0; Offset < Carets->Num(); ++Offset)
			{
				const TSharedPtr<FJsonValue>& Value = (*Carets)[Offset];
				const TArray<TSharedPtr<FJsonValue>>* Entry = nullptr;
				if (Value.IsValid() && Value->TryGetArray(Entry) && Entry != nullptr && Entry->Num() >= 2)
				{
					FCaretSample Sample;
					Sample.X = (*Entry)[0]->AsNumber();
					Sample.Y = (*Entry)[1]->AsNumber();
					Result.Carets.Add(Offset, Sample);
				}
			}
		}
		double Number = 0.0;
		if (Root->TryGetNumberField(TEXT("baseline"), Number))
		{
			Result.Baseline = Number;
		}
		if (Root->TryGetNumberField(TEXT("linePitch"), Number))
		{
			Result.Pitch = Number;
		}
		Result.bNumbers = Result.LineStarts.Num() > 0;
	}

	TSharedPtr<FJsonValue> IntArrayValue(const TArray<int32>& InValues)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const int32 Value : InValues)
		{
			Values.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return MakeShared<FJsonValueArray>(Values);
	}

	FString IntListText(const TArray<int32>& InValues)
	{
		TArray<FString> Parts;
		for (const int32 Value : InValues)
		{
			Parts.Add(FString::FromInt(Value));
		}
		return FString::Join(Parts, TEXT(" "));
	}

	/** How one engine's numbers stand against the reference's (Chrome's, or Slate's for a case held to Slate). */
	struct FDeviation
	{
		bool bComparable = false;
		bool bLineStartsEqual = false;
		int32 CaretsCompared = 0;
		double CaretMax = 0.0;
		double CaretMean = 0.0;
		TOptional<double> Baseline;
		TOptional<double> Pitch;
		bool bInk = false;
		FIntRect InkBounds = FIntRect(0, 0, 0, 0);
		TOptional<double> InkMass;
		int32 MaskDifference = -1;
		bool bCrisp = false;
		/** Edge widths as differences in pixels; grey fraction and gradient as relative differences. */
		double EdgeWidthRows = 0.0;
		double EdgeWidthColumns = 0.0;
		TOptional<double> GreyFraction;
		TOptional<double> GradientPerInk;
		/** Edge widths per stroke as differences in pixels, when both pictures have strokes on both axes. */
		bool bStrokeEdge = false;
		double StrokeEdgeRows = 0.0;
		double StrokeEdgeColumns = 0.0;
		/** Ink in linear light, a relative difference. */
		TOptional<double> InkLinear;
		/**
		 * DreamGUI's grey fraction against Slate's, whatever the reference, a relative difference: both are plain grayscale
		 * coverage, where Chrome's DirectWrite masks are filtered across. Dark text on light paper only. Set by FinishCase.
		 */
		TOptional<double> GreyVsSlate;
	};

	FDeviation Compare(const FEngineResult& InEngine, const FEngineResult& InReference, const FInk& InEngineInk, const FInk& InReferenceInk,
		const FCrispness& InEngineCrisp, const FCrispness& InReferenceCrisp, bool bInMask, const FColor& InPaper)
	{
		FDeviation Deviation;
		if (InEngine.bNumbers && InReference.bNumbers)
		{
			Deviation.bComparable = true;
			Deviation.bLineStartsEqual = InEngine.LineStarts == InReference.LineStarts;
			double Sum = 0.0;
			for (const TPair<int32, FCaretSample>& Caret : InEngine.Carets)
			{
				if (const FCaretSample* Reference = InReference.Carets.Find(Caret.Key))
				{
					const double Delta = FMath::Abs(Caret.Value.X - Reference->X);
					Deviation.CaretMax = FMath::Max(Deviation.CaretMax, Delta);
					Sum += Delta;
					++Deviation.CaretsCompared;
				}
			}
			Deviation.CaretMean = Deviation.CaretsCompared > 0 ? Sum / Deviation.CaretsCompared : 0.0;
			if (InEngine.Baseline.IsSet() && InReference.Baseline.IsSet())
			{
				Deviation.Baseline = InEngine.Baseline.GetValue() - InReference.Baseline.GetValue();
			}
			if (InEngine.Pitch.IsSet() && InReference.Pitch.IsSet())
			{
				Deviation.Pitch = InEngine.Pitch.GetValue() - InReference.Pitch.GetValue();
			}
		}
		if (InEngineInk.bAny && InReferenceInk.bAny)
		{
			Deviation.bInk = true;
			Deviation.InkBounds = FIntRect(InEngineInk.Bounds.Min - InReferenceInk.Bounds.Min, InEngineInk.Bounds.Max - InReferenceInk.Bounds.Max);
			if (InReferenceInk.Mass > 0.0)
			{
				Deviation.InkMass = (InEngineInk.Mass - InReferenceInk.Mass) / InReferenceInk.Mass;
			}
			if (InReferenceInk.LinearMass > 0.0)
			{
				Deviation.InkLinear = (InEngineInk.LinearMass - InReferenceInk.LinearMass) / InReferenceInk.LinearMass;
			}
		}
		if (InEngineCrisp.bAny && InReferenceCrisp.bAny)
		{
			Deviation.bCrisp = true;
			Deviation.EdgeWidthRows = InEngineCrisp.EdgeWidthRows - InReferenceCrisp.EdgeWidthRows;
			Deviation.EdgeWidthColumns = InEngineCrisp.EdgeWidthColumns - InReferenceCrisp.EdgeWidthColumns;
			if (InReferenceCrisp.GreyFraction > 0.0)
			{
				Deviation.GreyFraction = (InEngineCrisp.GreyFraction - InReferenceCrisp.GreyFraction) / InReferenceCrisp.GreyFraction;
			}
			if (InReferenceCrisp.GradientPerInk > 0.0)
			{
				Deviation.GradientPerInk = (InEngineCrisp.GradientPerInk - InReferenceCrisp.GradientPerInk) / InReferenceCrisp.GradientPerInk;
			}
			if (InEngineCrisp.RowStrokeEdges > 0 && InEngineCrisp.ColumnStrokeEdges > 0 && InReferenceCrisp.RowStrokeEdges > 0 && InReferenceCrisp.ColumnStrokeEdges > 0)
			{
				Deviation.bStrokeEdge = true;
				Deviation.StrokeEdgeRows = InEngineCrisp.StrokeEdgeRows - InReferenceCrisp.StrokeEdgeRows;
				Deviation.StrokeEdgeColumns = InEngineCrisp.StrokeEdgeColumns - InReferenceCrisp.StrokeEdgeColumns;
			}
		}
		if (bInMask)
		{
			Deviation.MaskDifference = CountMaskDifference(InEngine, InReference, InPaper);
		}
		return Deviation;
	}

	/** One target of a case held against DreamGUI's deviation from the reference. */
	struct FTargetResult
	{
		FString Name;
		double Limit = 0.0;
		/** Whether there was anything to measure: no reference, no ink or no carets leave a target unmeasured. */
		bool bMeasured = false;
		bool bMet = false;
		/** The measure and its limit, as the report shows them. */
		FString Text;
	};

	/**
	 * The case's targets against DreamGUI's deviation: inkMass (relative, either way), inkBounds (pixels, every edge),
	 * caretMax (pixels), baseline and pitch (pixels, either way), edgeWidth (pixels wider than the reference's, rows and
	 * columns alike) and greyFraction (relative, either way) -- the measures before this round's, which small text is no
	 * longer held to -- and strokeEdge (edge width per stroke, pixels wider than the reference's, rows and columns alike),
	 * inkLinear (ink in linear light, relative, either way) and greyVsSlate (the grey fraction against Slate's, relative,
	 * either way; dark text on light paper only). A name it does not know is reported as not measured.
	 */
	void EvaluateTargets(const FCase& InCase, const FDeviation& InDeviation, TArray<FTargetResult>& OutResults)
	{
		OutResults.Reset();
		for (const TPair<FString, double>& Target : InCase.Targets)
		{
			FTargetResult& Result = OutResults.AddDefaulted_GetRef();
			Result.Name = Target.Key;
			Result.Limit = Target.Value;
			const FString& Name = Target.Key;
			const double Limit = Target.Value;
			if (Name == TEXT("inkMass"))
			{
				if (InDeviation.InkMass.IsSet())
				{
					const double Value = InDeviation.InkMass.GetValue();
					Result.bMeasured = true;
					Result.bMet = FMath::Abs(Value) <= Limit;
					Result.Text = FString::Printf(TEXT("ink %+.0f%% (within %.0f%%)"), Value * 100.0, Limit * 100.0);
				}
			}
			else if (Name == TEXT("inkBounds"))
			{
				if (InDeviation.bInk)
				{
					const FIntRect& Bounds = InDeviation.InkBounds;
					const int32 Worst = FMath::Max(FMath::Max(FMath::Abs(Bounds.Min.X), FMath::Abs(Bounds.Min.Y)), FMath::Max(FMath::Abs(Bounds.Max.X), FMath::Abs(Bounds.Max.Y)));
					Result.bMeasured = true;
					Result.bMet = Worst <= Limit;
					Result.Text = FString::Printf(TEXT("ink bounds off by %d px (within %.0f)"), Worst, Limit);
				}
			}
			else if (Name == TEXT("caretMax"))
			{
				if (InDeviation.bComparable && InDeviation.CaretsCompared > 0)
				{
					Result.bMeasured = true;
					Result.bMet = InDeviation.CaretMax <= Limit;
					Result.Text = FString::Printf(TEXT("carets off by %.1f px (within %.1f)"), InDeviation.CaretMax, Limit);
				}
			}
			else if (Name == TEXT("baseline") || Name == TEXT("pitch"))
			{
				const TOptional<double>& Value = Name == TEXT("baseline") ? InDeviation.Baseline : InDeviation.Pitch;
				if (Value.IsSet())
				{
					Result.bMeasured = true;
					Result.bMet = FMath::Abs(Value.GetValue()) <= Limit;
					Result.Text = FString::Printf(TEXT("%s %+.1f px (within %.1f)"), *Name, Value.GetValue(), Limit);
				}
			}
			else if (Name == TEXT("edgeWidth"))
			{
				if (InDeviation.bCrisp)
				{
					const double Wider = FMath::Max(InDeviation.EdgeWidthRows, InDeviation.EdgeWidthColumns);
					Result.bMeasured = true;
					Result.bMet = Wider <= Limit;
					Result.Text = FString::Printf(TEXT("edges %+.2f / %+.2f px (rows / columns; at most +%.2f)"), InDeviation.EdgeWidthRows, InDeviation.EdgeWidthColumns, Limit);
				}
			}
			else if (Name == TEXT("greyFraction"))
			{
				if (InDeviation.GreyFraction.IsSet())
				{
					const double Value = InDeviation.GreyFraction.GetValue();
					Result.bMeasured = true;
					Result.bMet = FMath::Abs(Value) <= Limit;
					Result.Text = FString::Printf(TEXT("grey %+.0f%% (within %.0f%%)"), Value * 100.0, Limit * 100.0);
				}
			}
			else if (Name == TEXT("strokeEdge"))
			{
				if (InDeviation.bStrokeEdge)
				{
					const double Wider = FMath::Max(InDeviation.StrokeEdgeRows, InDeviation.StrokeEdgeColumns);
					Result.bMeasured = true;
					Result.bMet = Wider <= Limit;
					Result.Text = FString::Printf(TEXT("stroke edges %+.2f / %+.2f px (rows / columns; at most +%.2f)"),
						InDeviation.StrokeEdgeRows, InDeviation.StrokeEdgeColumns, Limit);
				}
			}
			else if (Name == TEXT("inkLinear"))
			{
				if (InDeviation.InkLinear.IsSet())
				{
					const double Value = InDeviation.InkLinear.GetValue();
					Result.bMeasured = true;
					Result.bMet = FMath::Abs(Value) <= Limit;
					Result.Text = FString::Printf(TEXT("linear ink %+.1f%% (within %.0f%%)"), Value * 100.0, Limit * 100.0);
				}
			}
			else if (Name == TEXT("greyVsSlate"))
			{
				if (InDeviation.GreyVsSlate.IsSet())
				{
					const double Value = InDeviation.GreyVsSlate.GetValue();
					Result.bMeasured = true;
					Result.bMet = FMath::Abs(Value) <= Limit;
					Result.Text = FString::Printf(TEXT("grey %+.0f%% of Slate's (within %.0f%%)"), Value * 100.0, Limit * 100.0);
				}
				else if (InCase.bInverse)
				{
					Result.Text = TEXT("greyVsSlate: measured on dark text on light paper only");
				}
			}
			else
			{
				Result.Text = FString::Printf(TEXT("%s: not a measure the test knows"), *Name);
				continue;
			}
			if (!Result.bMeasured && Result.Text.IsEmpty())
			{
				Result.Text = FString::Printf(TEXT("%s: nothing to measure"), *Name);
			}
		}
	}

	TSharedRef<FJsonObject> EngineJson(const FEngineResult& InResult, const FInk& InInk, const FCrispness& InCrisp)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("picture"), InResult.bPicture);
		Object->SetBoolField(TEXT("numbers"), InResult.bNumbers);
		if (!InResult.NotApplicable.IsEmpty())
		{
			Object->SetStringField(TEXT("notApplicable"), InResult.NotApplicable);
		}
		TArray<TSharedPtr<FJsonValue>> Notes;
		for (const FString& Note : InResult.Notes)
		{
			Notes.Add(MakeShared<FJsonValueString>(Note));
		}
		Object->SetArrayField(TEXT("notes"), Notes);
		if (!InResult.Version.IsEmpty())
		{
			Object->SetStringField(TEXT("version"), InResult.Version);
		}
		if (InResult.bNumbers)
		{
			Object->SetField(TEXT("lineStarts"), IntArrayValue(InResult.LineStarts));
			Object->SetNumberField(TEXT("carets"), InResult.Carets.Num());
			if (InResult.Baseline.IsSet())
			{
				Object->SetNumberField(TEXT("baseline"), InResult.Baseline.GetValue());
			}
			if (InResult.Pitch.IsSet())
			{
				Object->SetNumberField(TEXT("pitch"), InResult.Pitch.GetValue());
			}
		}
		if (InInk.bAny)
		{
			TSharedRef<FJsonObject> Ink = MakeShared<FJsonObject>();
			Ink->SetField(TEXT("bounds"), IntArrayValue({ InInk.Bounds.Min.X, InInk.Bounds.Min.Y, InInk.Bounds.Max.X, InInk.Bounds.Max.Y }));
			Ink->SetNumberField(TEXT("mass"), InInk.Mass);
			Ink->SetNumberField(TEXT("linearMass"), InInk.LinearMass);
			Object->SetObjectField(TEXT("ink"), Ink);
		}
		if (InCrisp.bAny)
		{
			TSharedRef<FJsonObject> Crisp = MakeShared<FJsonObject>();
			Crisp->SetNumberField(TEXT("strokeEdgeRows"), InCrisp.StrokeEdgeRows);
			Crisp->SetNumberField(TEXT("strokeEdgeColumns"), InCrisp.StrokeEdgeColumns);
			Crisp->SetNumberField(TEXT("rowStrokeEdges"), InCrisp.RowStrokeEdges);
			Crisp->SetNumberField(TEXT("columnStrokeEdges"), InCrisp.ColumnStrokeEdges);
			Crisp->SetNumberField(TEXT("edgeWidthRows"), InCrisp.EdgeWidthRows);
			Crisp->SetNumberField(TEXT("edgeWidthColumns"), InCrisp.EdgeWidthColumns);
			Crisp->SetNumberField(TEXT("rowTransitions"), InCrisp.RowTransitions);
			Crisp->SetNumberField(TEXT("columnTransitions"), InCrisp.ColumnTransitions);
			Crisp->SetNumberField(TEXT("greyFraction"), InCrisp.GreyFraction);
			Crisp->SetNumberField(TEXT("gradientPerInk"), InCrisp.GradientPerInk);
			Object->SetObjectField(TEXT("crispness"), Crisp);
		}
		return Object;
	}

	TSharedRef<FJsonObject> DeviationJson(const FDeviation& InDeviation)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("comparable"), InDeviation.bComparable);
		if (InDeviation.bComparable)
		{
			Object->SetBoolField(TEXT("lineStartsEqual"), InDeviation.bLineStartsEqual);
			Object->SetNumberField(TEXT("caretsCompared"), InDeviation.CaretsCompared);
			Object->SetNumberField(TEXT("caretMax"), InDeviation.CaretMax);
			Object->SetNumberField(TEXT("caretMean"), InDeviation.CaretMean);
			if (InDeviation.Baseline.IsSet())
			{
				Object->SetNumberField(TEXT("baseline"), InDeviation.Baseline.GetValue());
			}
			if (InDeviation.Pitch.IsSet())
			{
				Object->SetNumberField(TEXT("pitch"), InDeviation.Pitch.GetValue());
			}
		}
		if (InDeviation.bInk)
		{
			Object->SetField(TEXT("inkBounds"), IntArrayValue({ InDeviation.InkBounds.Min.X, InDeviation.InkBounds.Min.Y, InDeviation.InkBounds.Max.X, InDeviation.InkBounds.Max.Y }));
			if (InDeviation.InkMass.IsSet())
			{
				Object->SetNumberField(TEXT("inkMass"), InDeviation.InkMass.GetValue());
			}
		}
		if (InDeviation.bCrisp)
		{
			Object->SetNumberField(TEXT("edgeWidthRows"), InDeviation.EdgeWidthRows);
			Object->SetNumberField(TEXT("edgeWidthColumns"), InDeviation.EdgeWidthColumns);
			if (InDeviation.GreyFraction.IsSet())
			{
				Object->SetNumberField(TEXT("greyFraction"), InDeviation.GreyFraction.GetValue());
			}
			if (InDeviation.GradientPerInk.IsSet())
			{
				Object->SetNumberField(TEXT("gradientPerInk"), InDeviation.GradientPerInk.GetValue());
			}
		}
		if (InDeviation.bStrokeEdge)
		{
			Object->SetNumberField(TEXT("strokeEdgeRows"), InDeviation.StrokeEdgeRows);
			Object->SetNumberField(TEXT("strokeEdgeColumns"), InDeviation.StrokeEdgeColumns);
		}
		if (InDeviation.InkLinear.IsSet())
		{
			Object->SetNumberField(TEXT("inkLinear"), InDeviation.InkLinear.GetValue());
		}
		if (InDeviation.GreyVsSlate.IsSet())
		{
			Object->SetNumberField(TEXT("greyVsSlate"), InDeviation.GreyVsSlate.GetValue());
		}
		if (InDeviation.MaskDifference >= 0)
		{
			Object->SetNumberField(TEXT("maskDifference"), InDeviation.MaskDifference);
		}
		return Object;
	}

	TSharedRef<FJsonObject> FillJson(const FCase& InCase, const FFillMeasure& InMeasure, const FString& InDreamMaskPath)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("css"), InCase.Fill);
		Object->SetBoolField(TEXT("richRuns"), InCase.Fill.IsEmpty());
		Object->SetBoolField(TEXT("measured"), InMeasure.bMeasured);
		if (!InMeasure.bMeasured)
		{
			Object->SetStringField(TEXT("why"), InMeasure.Why);
		}
		else
		{
			Object->SetNumberField(TEXT("pixels"), InMeasure.Pixels);
			Object->SetNumberField(TEXT("meanDifference"), InMeasure.MeanDifference);
			Object->SetNumberField(TEXT("p95Difference"), InMeasure.P95Difference);
			Object->SetNumberField(TEXT("bandDifference"), InMeasure.BandDifference);
			Object->SetNumberField(TEXT("worstBand"), InMeasure.WorstBand);
			Object->SetNumberField(TEXT("bands"), FillBands);
			Object->SetStringField(TEXT("axis"), InMeasure.Axis);
		}
		Object->SetStringField(TEXT("dreamMask"), InDreamMaskPath);
		return Object;
	}

	bool WriteJson(const TSharedRef<FJsonObject>& InObject, const FString& InPath)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		if (!FJsonSerializer::Serialize(InObject, Writer))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(Text, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString Signed(const TOptional<double>& InValue)
	{
		return InValue.IsSet() ? FString::Printf(TEXT("%+.1f"), InValue.GetValue()) : FString(TEXT("-"));
	}

	/** One row of report.md for a case's JSON. */
	FString ReportRow(const TSharedPtr<FJsonObject>& InCase)
	{
		auto Field = [](const TSharedPtr<FJsonObject>& InObject, const TCHAR* InName) -> TSharedPtr<FJsonObject>
		{
			const TSharedPtr<FJsonObject>* Found = nullptr;
			return InObject.IsValid() && InObject->TryGetObjectField(InName, Found) && Found != nullptr ? *Found : TSharedPtr<FJsonObject>();
		};
		auto Number = [](const TSharedPtr<FJsonObject>& InObject, const TCHAR* InName) -> TOptional<double>
		{
			double Value = 0.0;
			return InObject.IsValid() && InObject->TryGetNumberField(InName, Value) ? TOptional<double>(Value) : TOptional<double>();
		};
		auto Lines = [](const TSharedPtr<FJsonObject>& InObject) -> FString
		{
			FString NotApplicable;
			if (InObject.IsValid() && InObject->TryGetStringField(TEXT("notApplicable"), NotApplicable))
			{
				return TEXT("n/a");
			}
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!InObject.IsValid() || !InObject->TryGetArrayField(TEXT("lineStarts"), Values) || Values == nullptr)
			{
				return TEXT("-");
			}
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Parts.Add(FString::FromInt(FMath::RoundToInt32(Value->AsNumber())));
			}
			return Parts.Num() > 1 ? FString::Join(Parts, TEXT(" ")) : FString(TEXT("0"));
		};
		const TSharedPtr<FJsonObject> Engines = Field(InCase, TEXT("engines"));
		const TSharedPtr<FJsonObject> Dream = Field(Engines, TEXT("DreamGUI"));
		const TSharedPtr<FJsonObject> Slate = Field(Engines, TEXT("Slate"));
		const TSharedPtr<FJsonObject> Chrome = Field(Engines, TEXT("Chrome"));
		FString ReferenceName = TEXT("Chrome");
		InCase->TryGetStringField(TEXT("reference"), ReferenceName);
		const bool bHeldToSlate = ReferenceName == TEXT("Slate");
		// Cases written before the reference could be Slate kept their deviations under vsChrome.
		TSharedPtr<FJsonObject> Against = Field(InCase, TEXT("vsReference"));
		if (!Against.IsValid())
		{
			Against = Field(InCase, TEXT("vsChrome"));
		}
		const TSharedPtr<FJsonObject> DreamDelta = Field(Against, TEXT("DreamGUI"));
		const TSharedPtr<FJsonObject> SlateDelta = Field(Against, TEXT("Slate"));
		const bool bSlateNotApplicable = Slate.IsValid() && Slate->HasField(TEXT("notApplicable"));
		auto Caret = [&Number](const TSharedPtr<FJsonObject>& InDelta) -> FString
		{
			const TOptional<double> Max = Number(InDelta, TEXT("caretMax"));
			const TOptional<double> Mean = Number(InDelta, TEXT("caretMean"));
			return Max.IsSet() && Mean.IsSet() ? FString::Printf(TEXT("%.1f / %.1f"), Max.GetValue(), Mean.GetValue()) : FString(TEXT("-"));
		};
		auto Percent = [&Number](const TSharedPtr<FJsonObject>& InDelta, const TCHAR* InName) -> FString
		{
			const TOptional<double> Value = Number(InDelta, InName);
			return Value.IsSet() ? FString::Printf(TEXT("%+.0f%%"), Value.GetValue() * 100.0) : FString(TEXT("-"));
		};
		auto Mask = [&Number](const TSharedPtr<FJsonObject>& InDelta) -> FString
		{
			const TOptional<double> Value = Number(InDelta, TEXT("maskDifference"));
			return Value.IsSet() ? FString::Printf(TEXT("%.0f"), Value.GetValue()) : FString(TEXT("-"));
		};
		// An engine's own crispness: rows and columns of edge width per transition (0) and per stroke (3), its grey share
		// (1) and its gradient per ink (2).
		auto Crisp = [&Field, &Number](const TSharedPtr<FJsonObject>& InEngine, int32 InWhich) -> FString
		{
			const TSharedPtr<FJsonObject> Values = Field(InEngine, TEXT("crispness"));
			if (!Values.IsValid())
			{
				return TEXT("-");
			}
			if (InWhich == 0)
			{
				return FString::Printf(TEXT("%.2f:%.2f"), Number(Values, TEXT("edgeWidthRows")).Get(0.0), Number(Values, TEXT("edgeWidthColumns")).Get(0.0));
			}
			if (InWhich == 1)
			{
				return FString::Printf(TEXT("%.0f%%"), Number(Values, TEXT("greyFraction")).Get(0.0) * 100.0);
			}
			if (InWhich == 3)
			{
				const TOptional<double> Rows = Number(Values, TEXT("strokeEdgeRows"));
				const TOptional<double> Columns = Number(Values, TEXT("strokeEdgeColumns"));
				return Rows.IsSet() && Columns.IsSet() ? FString::Printf(TEXT("%.2f:%.2f"), Rows.GetValue(), Columns.GetValue()) : FString(TEXT("-"));
			}
			return FString::Printf(TEXT("%.2f"), Number(Values, TEXT("gradientPerInk")).Get(0.0));
		};
		const TSharedPtr<FJsonObject> Reference = bHeldToSlate ? Slate : Chrome;
		const FString SlateCaret = bHeldToSlate ? FString(TEXT("ref")) : (bSlateNotApplicable ? FString(TEXT("n/a")) : Caret(SlateDelta));
		auto SlatePair = [bHeldToSlate, bSlateNotApplicable](const FString& InDream, const FString& InSlate) -> FString
		{
			return FString::Printf(TEXT("%s / %s"), *InDream, bHeldToSlate ? TEXT("ref") : (bSlateNotApplicable ? TEXT("n/a") : *InSlate));
		};
		// Targets: how many held, the ones that did not, and whether the case asserts them.
		FString Targets = TEXT("-");
		const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
		if (InCase->TryGetArrayField(TEXT("targets"), TargetValues) && TargetValues != nullptr && TargetValues->Num() > 0)
		{
			int32 Met = 0;
			int32 Measured = 0;
			TArray<FString> Missed;
			for (const TSharedPtr<FJsonValue>& Value : *TargetValues)
			{
				const TSharedPtr<FJsonObject> Target = Value.IsValid() ? Value->AsObject() : nullptr;
				bool bMeasured = false;
				bool bMet = false;
				FString Text;
				if (!Target.IsValid() || !Target->TryGetBoolField(TEXT("measured"), bMeasured) || !bMeasured)
				{
					continue;
				}
				++Measured;
				Target->TryGetBoolField(TEXT("met"), bMet);
				Target->TryGetStringField(TEXT("text"), Text);
				if (bMet)
				{
					++Met;
				}
				else
				{
					Missed.Add(Text);
				}
			}
			bool bReportOnly = false;
			InCase->TryGetBoolField(TEXT("reportOnly"), bReportOnly);
			Targets = Measured == 0 ? FString::Printf(TEXT("not measured (%d)"), TargetValues->Num())
				: (Missed.Num() == 0 ? FString::Printf(TEXT("%d/%d"), Met, Measured)
					: FString::Printf(TEXT("**%d/%d**: %s"), Met, Measured, *FString::Join(Missed, TEXT("; "))));
			if (bReportOnly)
			{
				Targets += TEXT(" (report only)");
			}
		}
		bool bEqual = false;
		const bool bHasEqual = DreamDelta.IsValid() && DreamDelta->TryGetBoolField(TEXT("lineStartsEqual"), bEqual);
		FString Id;
		InCase->TryGetStringField(TEXT("id"), Id);
		return FString::Printf(TEXT("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s / %s / %s | %s | %s | %s | %s / %s / %s | %s / %s / %s | %s / %s / %s | %s |"),
			*Id, bHeldToSlate ? TEXT("S") : TEXT("C"), *Lines(Dream), *Lines(Slate), *Lines(Reference),
			bHasEqual ? (bEqual ? TEXT("yes") : TEXT("**no**")) : TEXT("-"),
			*Caret(DreamDelta), *SlateCaret,
			*SlatePair(Signed(Number(DreamDelta, TEXT("baseline"))), Signed(Number(SlateDelta, TEXT("baseline")))),
			*SlatePair(Signed(Number(DreamDelta, TEXT("pitch"))), Signed(Number(SlateDelta, TEXT("pitch")))),
			*SlatePair(Percent(DreamDelta, TEXT("inkLinear")), Percent(SlateDelta, TEXT("inkLinear"))),
			*Crisp(Dream, 3), *Crisp(Slate, 3), *Crisp(Reference, 3),
			*Percent(DreamDelta, TEXT("greyVsSlate")),
			*SlatePair(Percent(DreamDelta, TEXT("inkMass")), Percent(SlateDelta, TEXT("inkMass"))),
			*SlatePair(Mask(DreamDelta), Mask(SlateDelta)),
			*Crisp(Dream, 0), *Crisp(Slate, 0), *Crisp(Reference, 0),
			*Crisp(Dream, 1), *Crisp(Slate, 1), *Crisp(Reference, 1),
			*Crisp(Dream, 2), *Crisp(Slate, 2), *Crisp(Reference, 2),
			*Targets);
	}

	/** One row of report.md's gradient fill table for a case's JSON; empty for a case with no fill. */
	FString FillReportRow(const TSharedPtr<FJsonObject>& InCase)
	{
		const TSharedPtr<FJsonObject>* FillField = nullptr;
		if (!InCase.IsValid() || !InCase->TryGetObjectField(TEXT("fill"), FillField) || FillField == nullptr || !FillField->IsValid())
		{
			return FString();
		}
		const TSharedPtr<FJsonObject>& FillObject = *FillField;
		FString Id;
		InCase->TryGetStringField(TEXT("id"), Id);
		FString Css;
		FillObject->TryGetStringField(TEXT("css"), Css);
		const FString Paint = Css.IsEmpty() ? FString(TEXT("`<gradient>` runs")) : FString::Printf(TEXT("`%s`"), *Css);
		bool bMeasured = false;
		if (!FillObject->TryGetBoolField(TEXT("measured"), bMeasured) || !bMeasured)
		{
			FString Why;
			FillObject->TryGetStringField(TEXT("why"), Why);
			return FString::Printf(TEXT("| %s | %s | - | - | - | - | - | %s |"), *Id, *Paint, Why.IsEmpty() ? TEXT("not measured") : *Why);
		}
		double Pixels = 0.0;
		double Mean = 0.0;
		double Percentile95 = 0.0;
		double BandApart = 0.0;
		double WorstBand = 0.0;
		double Bands = 0.0;
		FString Axis;
		FillObject->TryGetNumberField(TEXT("pixels"), Pixels);
		FillObject->TryGetNumberField(TEXT("meanDifference"), Mean);
		FillObject->TryGetNumberField(TEXT("p95Difference"), Percentile95);
		FillObject->TryGetNumberField(TEXT("bandDifference"), BandApart);
		FillObject->TryGetNumberField(TEXT("worstBand"), WorstBand);
		FillObject->TryGetNumberField(TEXT("bands"), Bands);
		FillObject->TryGetStringField(TEXT("axis"), Axis);
		return FString::Printf(TEXT("| %s | %s | %.0f | %.1f | %.0f | %.1f (band %.0f of %.0f) | %s | - |"), *Id, *Paint, Pixels, Mean, Percentile95,
			BandApart, WorstBand + 1.0, Bands, *Axis);
	}

	/** report.json and report.md over every case written so far, so that a run of a few cases still reports the rest. */
	void WriteReport()
	{
		const FString CasesDirectory = FPaths::Combine(GetOutputDirectory(), TEXT("cases"));
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(CasesDirectory, TEXT("*.json")), true, false);
		Files.Sort();
		TArray<TSharedPtr<FJsonValue>> Cases;
		TArray<FString> Rows;
		TArray<FString> FillRows;
		FString ChromeVersion;
		for (const FString& File : Files)
		{
			FString Text;
			TSharedPtr<FJsonObject> Case;
			if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(CasesDirectory, File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Case) || !Case.IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Engines = nullptr;
			const TSharedPtr<FJsonObject>* Chrome = nullptr;
			FString Version;
			if (ChromeVersion.IsEmpty() && Case->TryGetObjectField(TEXT("engines"), Engines) && Engines != nullptr
				&& (*Engines)->TryGetObjectField(TEXT("Chrome"), Chrome) && Chrome != nullptr && (*Chrome)->TryGetStringField(TEXT("version"), Version))
			{
				ChromeVersion = Version;
			}
			Rows.Add(ReportRow(Case));
			const FString FillRow = FillReportRow(Case);
			if (!FillRow.IsEmpty())
			{
				FillRows.Add(FillRow);
			}
			Cases.Add(MakeShared<FJsonValueObject>(Case));
		}
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("written"), FDateTime::UtcNow().ToIso8601());
		Report->SetStringField(TEXT("chrome"), ChromeVersion);
		Report->SetArrayField(TEXT("cases"), Cases);
		WriteJson(Report, FPaths::Combine(GetOutputDirectory(), TEXT("report.json")));

		FString Markdown;
		Markdown += TEXT("# Text parity: DreamGUI, Slate and Chrome\n\n");
		Markdown += FString::Printf(TEXT("Written %s from %d case(s) in `cases/`. Chrome: %s.\n\n"), *FDateTime::UtcNow().ToIso8601(), Rows.Num(),
			ChromeVersion.IsEmpty() ? TEXT("no reference") : *ChromeVersion);
		Markdown += TEXT("Positions are canvas (CSS) pixels, pictures device pixels. Ref is what a case is measured against: C Chrome, S Slate ");
		Markdown += TEXT("(Chrome cannot draw the case). Lines are the UTF-16 offsets where lines start. Caret is the largest and the mean ");
		Markdown += TEXT("distance in x from the reference's caret over the offsets both have. Baseline and pitch are signed differences from ");
		Markdown += TEXT("the reference. Ink lin is the difference in the sum of coverage in linear light (pixel and paper decoded, then ");
		Markdown += TEXT("differenced). Stroke edge is the mean width in pixels of a stroke's edges -- a run of a row or a column that peaks ");
		Markdown += TEXT("at p >= 0.5, its pixels between 0.1 p and 0.9 p before it first reaches 0.9 p and after it last does -- reading rows ");
		Markdown += TEXT(": columns. Grey D vs S is DreamGUI's grey share against Slate's, dark on light only. The columns marked old are ");
		Markdown += TEXT("the measures before those, reported alongside for one round: ink summed in sRGB-encoded coverage, the mean width ");
		Markdown += TEXT("of an edge between paper (0.1) and ink (0.9), which misses a stroke that never reaches 0.9, and the grey share ");
		Markdown += TEXT("against the reference. Mask counts the pixels solid in one picture and not in the reference's (corner cases); ");
		Markdown += TEXT("grad is the sum of the gradient over the ink. Each is for DreamGUI / Slate / the reference where three are given. ");
		Markdown += TEXT("Targets are the case's limits on DreamGUI, met of measured. D is DreamGUI and S is Slate; n/a: Slate has no ");
		Markdown += TEXT("equivalent of the case. `<case>_compare.png` shows DreamGUI | Slate | Chrome.\n\n");
		Markdown += TEXT("| Case | Ref | Lines D | Lines S | Lines Ref | D breaks = Ref | Caret D max/mean | Caret S | Baseline D / S | Pitch D / S | Ink lin D / S | Stroke edge D / S / Ref | Grey D vs S | Ink D / S (old) | Mask D / S | Edge D / S / Ref (old) | Grey D / S / Ref (old) | Grad D / S / Ref | Targets |\n");
		Markdown += TEXT("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
		for (const FString& Row : Rows)
		{
			Markdown += Row + TEXT("\n");
		}
		if (FillRows.Num() > 0)
		{
			Markdown += TEXT("\n## Gradient fills\n\n");
			Markdown += TEXT("Reported only. Each fill case is drawn twice on each side, painted and as a solid mask; on the pixels both ");
			Markdown += TEXT("masks cover fully (Pixels), Mean and 95th percentile are those of each pixel's largest channel ");
			Markdown += TEXT("difference between DreamGUI's and Chrome's painted pictures, in 8-bit codes, and Worst band the largest channel ");
			Markdown += TEXT("difference of the two average colours of a band, the pixels cut into bands of equal width along the axis (a ");
			Markdown += TEXT("linear fill's angle, else horizontal). `<case>_dream_mask.png` is DreamGUI's mask, Chrome's is `<case>.mask.png` ");
			Markdown += TEXT("beside its picture.\n\n");
			Markdown += TEXT("| Case | Paint | Pixels | Mean | 95th percentile | Worst band | Axis | Not measured |\n");
			Markdown += TEXT("|---|---|---|---|---|---|---|---|\n");
			for (const FString& Row : FillRows)
			{
				Markdown += Row + TEXT("\n");
			}
		}
		FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(GetOutputDirectory(), TEXT("report.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/**
	 * Writes the case's pictures and numbers, adds it to the report, and -- unless the case is reportOnly -- holds
	 * DreamGUI's line breaks to the reference's and checks the case's targets.
	 */
	void FinishCase(FAutomationTestBase& InTest, FCaseRun& InOutRun)
	{
		const FCase& Case = InOutRun.Case;
		const FColor Paper = Case.GetPaper();
		const FString Directory = GetOutputDirectory();
		IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory, TEXT("cases")), true);

		ReadChrome(InOutRun);
		const bool bHeldToSlate = Case.IsHeldToSlate();
		const FEngineResult& Reference = bHeldToSlate ? InOutRun.Slate : InOutRun.Chrome;
		const FInk DreamInk = MeasureInk(InOutRun.Dream, Paper);
		const FInk SlateInk = MeasureInk(InOutRun.Slate, Paper);
		const FInk ChromeInk = MeasureInk(InOutRun.Chrome, Paper);
		const FCrispness DreamCrisp = MeasureCrispness(InOutRun.Dream, Paper);
		const FCrispness SlateCrisp = MeasureCrispness(InOutRun.Slate, Paper);
		const FCrispness ChromeCrisp = MeasureCrispness(InOutRun.Chrome, Paper);
		const FInk& ReferenceInk = bHeldToSlate ? SlateInk : ChromeInk;
		const FCrispness& ReferenceCrisp = bHeldToSlate ? SlateCrisp : ChromeCrisp;
		const bool bCorners = Case.HasFlag(TEXT("corners"));
		FDeviation DreamDeviation = Compare(InOutRun.Dream, Reference, DreamInk, ReferenceInk, DreamCrisp, ReferenceCrisp, bCorners, Paper);
		// The grey share against Slate's whatever the reference: for dark text on light paper both are plain grayscale
		// coverage, where Chrome's DirectWrite masks are filtered across; light on dark is left unmeasured.
		if (!Case.bInverse && InOutRun.Slate.bPicture && DreamCrisp.bAny && SlateCrisp.bAny && SlateCrisp.GreyFraction > 0.0)
		{
			DreamDeviation.GreyVsSlate = (DreamCrisp.GreyFraction - SlateCrisp.GreyFraction) / SlateCrisp.GreyFraction;
		}
		const bool bSlateCompared = !bHeldToSlate && InOutRun.Slate.NotApplicable.IsEmpty();
		const FDeviation SlateDeviation = bSlateCompared
			? Compare(InOutRun.Slate, Reference, SlateInk, ReferenceInk, SlateCrisp, ReferenceCrisp, bCorners, Paper) : FDeviation();
		TArray<FTargetResult> Targets;
		EvaluateTargets(Case, DreamDeviation, Targets);
		const bool bFill = Case.HasFill();
		const FFillMeasure FillResult = bFill ? MeasureFill(InOutRun) : FFillMeasure();

		const FString DreamPath = FPaths::Combine(Directory, Case.Id + TEXT("_dream.png"));
		const FString SlatePath = FPaths::Combine(Directory, Case.Id + TEXT("_slate.png"));
		const FString ComparePath = FPaths::Combine(Directory, Case.Id + TEXT("_compare.png"));
		const FString DreamMaskPath = FPaths::Combine(Directory, Case.Id + TEXT("_dream_mask.png"));
		if (InOutRun.Dream.bPicture)
		{
			UDreamUICaptureLibrary::SavePixelsToPng(InOutRun.Dream.Pixels, InOutRun.Dream.Size, DreamPath);
		}
		if (bFill && InOutRun.DreamMask.bPicture)
		{
			UDreamUICaptureLibrary::SavePixelsToPng(InOutRun.DreamMask.Pixels, InOutRun.DreamMask.Size, DreamMaskPath);
		}
		if (InOutRun.Slate.bPicture)
		{
			UDreamUICaptureLibrary::SavePixelsToPng(InOutRun.Slate.Pixels, InOutRun.Slate.Size, SlatePath);
		}
		TArray<FColor> Composite;
		FIntPoint CompositeSize = FIntPoint::ZeroValue;
		ComposeComparison(InOutRun, Composite, CompositeSize);
		UDreamUICaptureLibrary::SavePixelsToPng(Composite, CompositeSize, ComparePath);
		if (bCorners && InOutRun.Dream.bPicture && Reference.bPicture)
		{
			TArray<FColor> Mask;
			FIntPoint MaskSize = FIntPoint::ZeroValue;
			MakeMaskPicture(InOutRun.Dream, Reference, Paper, Mask, MaskSize);
			UDreamUICaptureLibrary::SavePixelsToPng(Mask, MaskSize, FPaths::Combine(Directory, Case.Id + TEXT("_mask.png")));
		}

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Case.Id);
		Entry->SetStringField(TEXT("base"), Case.BaseId);
		Entry->SetStringField(TEXT("written"), FDateTime::UtcNow().ToIso8601());
		Entry->SetStringField(TEXT("font"), Case.Font);
		Entry->SetNumberField(TEXT("size"), Case.Size);
		Entry->SetNumberField(TEXT("width"), Case.Width);
		Entry->SetNumberField(TEXT("scale"), Case.GetDeviceScale());
		Entry->SetField(TEXT("canvas"), IntArrayValue({ Case.Canvas.X, Case.Canvas.Y }));
		Entry->SetStringField(TEXT("reference"), bHeldToSlate ? TEXT("Slate") : TEXT("Chrome"));
		Entry->SetBoolField(TEXT("reportOnly"), Case.IsReportOnly());
		if (bFill)
		{
			Entry->SetObjectField(TEXT("fill"), FillJson(Case, FillResult, DreamMaskPath));
		}
		TArray<TSharedPtr<FJsonValue>> Flags;
		for (const FString& Flag : Case.Flags)
		{
			Flags.Add(MakeShared<FJsonValueString>(Flag));
		}
		Entry->SetArrayField(TEXT("flags"), Flags);
		TSharedRef<FJsonObject> Engines = MakeShared<FJsonObject>();
		Engines->SetObjectField(TEXT("DreamGUI"), EngineJson(InOutRun.Dream, DreamInk, DreamCrisp));
		Engines->SetObjectField(TEXT("Slate"), EngineJson(InOutRun.Slate, SlateInk, SlateCrisp));
		Engines->SetObjectField(TEXT("Chrome"), EngineJson(InOutRun.Chrome, ChromeInk, ChromeCrisp));
		Entry->SetObjectField(TEXT("engines"), Engines);
		TSharedRef<FJsonObject> Against = MakeShared<FJsonObject>();
		Against->SetObjectField(TEXT("DreamGUI"), DeviationJson(DreamDeviation));
		if (bSlateCompared)
		{
			Against->SetObjectField(TEXT("Slate"), DeviationJson(SlateDeviation));
		}
		Entry->SetObjectField(TEXT("vsReference"), Against);
		TArray<TSharedPtr<FJsonValue>> TargetValues;
		for (const FTargetResult& Target : Targets)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Target.Name);
			Object->SetNumberField(TEXT("limit"), Target.Limit);
			Object->SetBoolField(TEXT("measured"), Target.bMeasured);
			Object->SetBoolField(TEXT("met"), Target.bMet);
			Object->SetStringField(TEXT("text"), Target.Text);
			TargetValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Entry->SetArrayField(TEXT("targets"), TargetValues);
		TSharedRef<FJsonObject> Pictures = MakeShared<FJsonObject>();
		Pictures->SetStringField(TEXT("dream"), DreamPath);
		Pictures->SetStringField(TEXT("slate"), SlatePath);
		Pictures->SetStringField(TEXT("compare"), ComparePath);
		Entry->SetObjectField(TEXT("pictures"), Pictures);
		WriteJson(Entry, FPaths::Combine(Directory, TEXT("cases"), Case.Id + TEXT(".json")));
		WriteReport();

		for (const FEngineResult* Result : { &InOutRun.Dream, &InOutRun.Slate, &InOutRun.Chrome })
		{
			for (const FString& Note : Result->Notes)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s, %s: %s."), *Case.Id, *Result->Engine, *Note));
			}
		}
		InTest.AddInfo(FString::Printf(TEXT("%s: lines start at DreamGUI [%s], Slate [%s], %s [%s]; caret x off %s by at most %.1f px (DreamGUI)%s; %s"),
			*Case.Id, *IntListText(InOutRun.Dream.LineStarts), *IntListText(InOutRun.Slate.LineStarts), *Reference.Engine,
			*IntListText(Reference.LineStarts), *Reference.Engine, DreamDeviation.CaretMax,
			bSlateCompared ? *FString::Printf(TEXT(" and %.1f px (Slate)"), SlateDeviation.CaretMax) : TEXT(""), *ComparePath));
		if (DreamCrisp.bAny && ReferenceCrisp.bAny)
		{
			InTest.AddInfo(FString::Printf(TEXT("%s: stroke edges %.2f / %.2f px (rows / columns, %d / %d edges) against %s's %.2f / %.2f (%d / %d); linear ink %s; grey %s."),
				*Case.Id, DreamCrisp.StrokeEdgeRows, DreamCrisp.StrokeEdgeColumns, DreamCrisp.RowStrokeEdges, DreamCrisp.ColumnStrokeEdges, *Reference.Engine,
				ReferenceCrisp.StrokeEdgeRows, ReferenceCrisp.StrokeEdgeColumns, ReferenceCrisp.RowStrokeEdges, ReferenceCrisp.ColumnStrokeEdges,
				DreamDeviation.InkLinear.IsSet() ? *FString::Printf(TEXT("%+.1f%% of %s's"), DreamDeviation.InkLinear.GetValue() * 100.0, *Reference.Engine) : TEXT("not measured"),
				DreamDeviation.GreyVsSlate.IsSet() ? *FString::Printf(TEXT("%.0f%% against Slate's %.0f%%"), DreamCrisp.GreyFraction * 100.0, SlateCrisp.GreyFraction * 100.0)
					: TEXT("not measured against Slate")));
			InTest.AddInfo(FString::Printf(TEXT("%s (the measures before, for one round): edges %.2f / %.2f px (rows / columns) against %s's %.2f / %.2f, grey %.0f%% against %.0f%%, gradient per ink %.2f against %.2f."),
				*Case.Id, DreamCrisp.EdgeWidthRows, DreamCrisp.EdgeWidthColumns, *Reference.Engine, ReferenceCrisp.EdgeWidthRows, ReferenceCrisp.EdgeWidthColumns,
				DreamCrisp.GreyFraction * 100.0, ReferenceCrisp.GreyFraction * 100.0, DreamCrisp.GradientPerInk, ReferenceCrisp.GradientPerInk));
		}
		if (bFill)
		{
			if (FillResult.bMeasured)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s (report only): over the %d pixel(s) both masks cover fully, the fill is %.1f codes from Chrome's on average and %d at the 95th percentile; the average colours of the %d bands along the %s axis differ by at most %.1f codes (band %d)."),
					*Case.Id, FillResult.Pixels, FillResult.MeanDifference, FillResult.P95Difference, FillBands, *FillResult.Axis, FillResult.BandDifference,
					FillResult.WorstBand + 1));
			}
			else
			{
				InTest.AddInfo(FString::Printf(TEXT("%s: the fill is not measured: %s."), *Case.Id, *FillResult.Why));
			}
		}
		if (bCorners && DreamDeviation.MaskDifference >= 0)
		{
			InTest.AddInfo(FString::Printf(TEXT("%s: %d pixel(s) solid in DreamGUI's picture and not in %s's or the other way%s."),
				*Case.Id, DreamDeviation.MaskDifference, *Reference.Engine,
				bSlateCompared ? *FString::Printf(TEXT(", %d in Slate's"), SlateDeviation.MaskDifference) : TEXT("")));
		}

		const bool bReportOnly = Case.IsReportOnly();
		if (Case.HasFlag(TEXT("breaks")))
		{
			if (!Reference.bNumbers)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s: the line breaks are not checked, there is no %s reference%s."), *Case.Id, *Reference.Engine,
					bHeldToSlate ? TEXT("") : TEXT(". Run Tools/TextParity/Make-ChromeReference.ps1 to make one")));
			}
			else if (bReportOnly)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s (report only): DreamGUI starts its lines at [%s], %s at [%s]."), *Case.Id,
					*IntListText(InOutRun.Dream.LineStarts), *Reference.Engine, *IntListText(Reference.LineStarts)));
			}
			else
			{
				InTest.TestTrue(FString::Printf(TEXT("%s: DreamGUI starts its lines where %s does (DreamGUI [%s], %s [%s])"),
					*Case.Id, *Reference.Engine, *IntListText(InOutRun.Dream.LineStarts), *Reference.Engine, *IntListText(Reference.LineStarts)),
					InOutRun.Dream.bNumbers && InOutRun.Dream.LineStarts == Reference.LineStarts);
			}
		}
		for (const FTargetResult& Target : Targets)
		{
			if (!Target.bMeasured)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s: target %s not checked: %s."), *Case.Id, *Target.Name, *Target.Text));
			}
			else if (bReportOnly)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s (report only): %s, %s."), *Case.Id, *Target.Text, Target.bMet ? TEXT("met") : TEXT("missed")));
			}
			else
			{
				InTest.TestTrue(FString::Printf(TEXT("%s: DreamGUI meets its %s target against %s: %s"), *Case.Id, *Target.Name, *Reference.Engine, *Target.Text),
					Target.bMet);
			}
		}
	}

	/** Takes a case's earlier results out of the report, for a case that is not drawn this time. */
	void ForgetCase(const FString& InCaseId)
	{
		IFileManager::Get().Delete(*FPaths::Combine(GetOutputDirectory(), TEXT("cases"), InCaseId + TEXT(".json")), false, false, true);
		WriteReport();
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamTextParityTest,
	"DreamGUI.RHI.TextParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

void FDreamTextParityTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace DreamTextParityTestLocal;
	FCorpus Corpus;
	FString Error;
	if (!LoadCorpus(Corpus, Error))
	{
		return;
	}
	for (const FCase& Case : Corpus.Cases)
	{
		OutBeautifiedNames.Add(Case.Id);
		OutTestCommands.Add(Case.Id);
	}
}

/*
 * One corpus case drawn by DreamGUI, Slate and Chrome (when the reference exists): the pictures and the numbers are
 * written and reported; unless the case is reportOnly, for a case flagged "breaks" DreamGUI's line starts must equal the
 * reference's, and every target the case names must hold. A missing Chrome reference is reported, never a failure; a
 * case whose font key is optional and missing a file on this machine is skipped. A fill case is drawn a second time as
 * its solid mask, and fails only when its fill does not read as a gradient.
 */
bool FDreamTextParityTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextParityTestLocal;
	TSharedRef<FCaseRun> Run = MakeShared<FCaseRun>();
	FString Error;
	const bool bLoaded = LoadCorpus(Run->Corpus, Error);
	if (!TestTrue(FString::Printf(TEXT("The corpus loads%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), bLoaded))
	{
		return false;
	}
	const FCase* Found = Run->Corpus.FindCase(Parameters);
	if (!TestNotNull(FString::Printf(TEXT("The corpus has the case %s"), *Parameters), Found))
	{
		return false;
	}
	Run->Case = *Found;
	Run->PlainText = GetPlainText(Run->Case);
	if (!TestTrue(FString::Printf(TEXT("%s: the font key %s is in the fonts table, and every face of it reads"), *Run->Case.Id, *Run->Case.Font),
		Run->Corpus.FindFontKey(Run->Case.Font, Run->FontKey)))
	{
		return false;
	}
	const TArray<FString> MissingFiles = Run->FontKey.GetMissingFiles();
	if (Run->FontKey.bOptional && MissingFiles.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("%s is skipped: the font key %s names %s, which this machine does not have."),
			*Run->Case.Id, *Run->Case.Font, *FString::Join(MissingFiles, TEXT(", "))));
		ForgetCase(Run->Case.Id);
		return true;
	}
	FString FontError;
	UDreamUIFontData_FreeTypeRender* Font = GetDreamFont(Run->FontKey, FontError);
	if (!FontError.IsEmpty())
	{
		AddWarning(FString::Printf(TEXT("%s: %s."), *Run->Case.Id, *FontError));
	}
	if (!TestNotNull(FString::Printf(TEXT("%s: DreamGUI has a font for %s"), *Run->Case.Id, *Run->Case.Font), Font))
	{
		return false;
	}
	Run->Slate.NotApplicable = GetSlateNotApplicable(Run->Case, Run->FontKey);
	if (Run->Case.IsHeldToSlate() && !Run->Slate.NotApplicable.IsEmpty())
	{
		AddError(FString::Printf(TEXT("%s is held to Slate, which cannot draw it: %s."), *Run->Case.Id, *Run->Slate.NotApplicable));
		return false;
	}
	if (!Run->Case.Fill.IsEmpty())
	{
		// The string Chrome's page paints with, read the way a .dui file's or a property's CSS is.
		FDreamGradient FillGradient;
		FString FillError;
		if (!FDreamGradient::ParseCss(Run->Case.Fill, FillGradient, &FillError))
		{
			AddError(FString::Printf(TEXT("%s: the fill \"%s\" does not read as a gradient: %s."), *Run->Case.Id, *Run->Case.Fill, *FillError));
			return false;
		}
		Run->FillGradient = FillGradient;
	}

	// The stage is the case's canvas in device pixels; at a scale other than 1 its root is laid out at the canvas's own
	// size, so the canvas scale is the case's and every number stays in canvas pixels.
	const FIntPoint DeviceCanvas = Run->Case.GetDeviceCanvas();
	FStageRef Stage = BeginStage(*this, DeviceCanvas, Run->Case.GetPaper());
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	if (DeviceCanvas != Run->Case.Canvas)
	{
		Stage->UseLayoutSize(FVector2D(Run->Case.Canvas));
	}
	// Outlines are drawn by the built-in shader.
	Stage->UseBuiltInShader(true);
	Stage->KeepAlive(Font);
	UDreamText* Text = AddDreamText(*Stage, *Run, Font);
	if (!TestNotNull(FString::Printf(TEXT("%s: DreamGUI has a text on the stage"), *Run->Case.Id), Text))
	{
		Stage->TearDown();
		return false;
	}
	Run->DreamText = Text;

	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 1);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([Stage, Run]()
	{
		Run->Dream.bPicture = Stage->ReadBack(Run->Dream.Pixels, Run->Dream.Size);
		if (!Run->Dream.bPicture)
		{
			Run->Dream.Notes.Add(TEXT("the stage could not be read back"));
		}
		ReadDreamNumbers(*Run);
		if (Run->Slate.NotApplicable.IsEmpty())
		{
			BuildSlate(*Run);
			DrawSlate(*Run);
		}
		else
		{
			Run->Slate.Notes.Add(FString::Printf(TEXT("n/a: %s"), *Run->Slate.NotApplicable));
		}
	});
	if (Run->Case.HasFill())
	{
		// The same text again as a solid mask: which pixels its face covers fully, where the fills' colours are compared.
		EnqueueDo([Run]()
		{
			ShowDreamMask(*Run);
		});
		EnqueueFrames(Stage, 2);
		EnqueueFramesUntilSettled(Stage);
		EnqueueDo([Stage, Run]()
		{
			Run->DreamMask.bPicture = Stage->ReadBack(Run->DreamMask.Pixels, Run->DreamMask.Size);
			if (!Run->DreamMask.bPicture)
			{
				Run->Dream.Notes.Add(TEXT("the stage could not be read back for the mask"));
			}
		});
	}
	// Slate rasterises a glyph the first time it paints it and uploads its atlas with a later frame.
	EnqueueFrames(Stage, 2);
	EnqueueDo([this, Run]()
	{
		if (Run->Slate.NotApplicable.IsEmpty())
		{
			DrawSlate(*Run);
			Run->Slate.bPicture = Run->SlateTarget.IsValid() && FDreamPixelProbe::ReadBack(Run->SlateTarget.Get(), Run->Slate.Pixels, Run->Slate.Size);
			if (!Run->Slate.bPicture)
			{
				Run->Slate.Notes.Add(TEXT("the widget renderer's target could not be read back"));
			}
			ReadSlateNumbers(*Run);
		}
		FinishCase(*this, *Run);
	});
	EnqueueTearDown(Stage);
	EnqueueDo([Run]()
	{
		Run->ReleaseSlate();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextParityFontCoverageTest,
	"DreamGUI.RHI.TextParity.FontCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * Which face of its font key draws each code point of each case, read from the font files with FreeType, in the order a
 * text in the case's language tries the faces and within each face's ranges: a code point no face has is drawn by
 * whatever each engine falls back to (Chrome, a system font), and is not a layout difference. Reported, with the faces
 * that are missing from disk; it fails only when the corpus cannot be read.
 */
bool FDreamTextParityFontCoverageTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextParity;
	FCorpus Corpus;
	FString Error;
	const bool bLoaded = LoadCorpus(Corpus, Error);
	if (!TestTrue(FString::Printf(TEXT("The corpus loads%s%s"), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error), bLoaded))
	{
		return false;
	}
	FString Markdown = TEXT("# Font coverage of the text parity corpus\n\n| Case | Font | Code points per face | Not covered |\n|---|---|---|---|\n");
	TSet<FString> MissingFiles;
	TSet<FString> OptionalMissingFiles;
	int32 Uncovered = 0;
	for (const FCase& Case : Corpus.Cases)
	{
		if (Case.Id != Case.BaseId)
		{
			continue;
		}
		FFontKey Key;
		if (!Corpus.FindFontKey(Case.Font, Key))
		{
			AddError(FString::Printf(TEXT("%s names the font key %s, which the fonts table does not have or cannot read."), *Case.Id, *Case.Font));
			continue;
		}
		for (const FFontFace& Face : Key.Faces)
		{
			if (!GetFaceCoverage(Face.File).bLoaded)
			{
				(Key.bOptional ? OptionalMissingFiles : MissingFiles).Add(Face.File);
			}
		}
		TArray<uint32> Codepoints;
		TArray<int32> Offsets;
		DecodeCodepoints(GetPlainText(Case), Codepoints, Offsets);
		TArray<int32> PerFace;
		PerFace.Init(0, Key.Faces.Num());
		TArray<FString> NotCovered;
		TSet<uint32> Seen;
		for (const uint32 Codepoint : Codepoints)
		{
			bool bAlreadySeen = false;
			Seen.Add(Codepoint, &bAlreadySeen);
			if (bAlreadySeen || IsDefaultIgnorable(Codepoint))
			{
				continue;
			}
			const int32 Face = FindCoveringFace(Key, Codepoint, Case.Lang);
			if (Face == INDEX_NONE)
			{
				NotCovered.Add(FString::Printf(TEXT("U+%04X"), Codepoint));
			}
			else
			{
				++PerFace[Face];
			}
		}
		TArray<FString> Shares;
		for (int32 Face = 0; Face < Key.Faces.Num(); ++Face)
		{
			Shares.Add(FString::Printf(TEXT("%s %d"), *FPaths::GetBaseFilename(Key.Faces[Face].File), PerFace[Face]));
		}
		Markdown += FString::Printf(TEXT("| %s | %s | %s | %s |\n"), *Case.Id, *Case.Font, *FString::Join(Shares, TEXT(", ")),
			NotCovered.Num() > 0 ? *FString::Join(NotCovered, TEXT(" ")) : TEXT("-"));
		if (NotCovered.Num() > 0)
		{
			Uncovered += NotCovered.Num();
			AddInfo(FString::Printf(TEXT("%s: no face of %s has %s; each engine draws them with whatever it falls back to."),
				*Case.Id, *Case.Font, *FString::Join(NotCovered, TEXT(" "))));
		}
	}
	for (const FString& File : MissingFiles)
	{
		AddWarning(FString::Printf(TEXT("The font file %s could not be read: every case that names it is drawn with what each engine falls back to."), *File));
	}
	for (const FString& File : OptionalMissingFiles)
	{
		AddInfo(FString::Printf(TEXT("The font file %s is not on this machine; the cases of its optional font key are skipped."), *File));
	}
	const FString Directory = GetOutputDirectory();
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, TEXT("coverage.md"));
	FFileHelper::SaveStringToFile(Markdown, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	AddInfo(FString::Printf(TEXT("%d code point(s) of the corpus have no face in their font key; the table is %s."), Uncovered, *Path));
	return true;
}

#endif
