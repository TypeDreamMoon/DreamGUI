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
#include "DreamUICaptureLibrary.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"
#include "DreamTextParityCorpus.h"

/*
 * Text parity: every case of Resources/TextParity/corpus.json drawn by DreamGUI, by Slate and by Chrome, and the three
 * set side by side, in pictures and in numbers. DreamGUI.RHI.TextParity.<case id> is one case.
 *
 * DreamGUI draws on the gallery's render-target stage at 1:1, with distance-field fonts (outline multi-channel) made
 * from the same font files the case names, the first file the font and the rest its fallbacks. Its numbers come from
 * the text's display list: line starts and the caret per UTF-16 offset from the caret lines, the first baseline from
 * the pen of the first line's glyphs, all of it turned from the text's local space (origin at its top-left pivot, +Y
 * up) into canvas pixels from the top-left.
 *
 * Slate draws the same text through an FSlateTextLayout -- the layout STextBlock and SRichTextBlock wrap -- painted by a
 * small leaf widget, so that the picture and the numbers come from one layout object. The font is an
 * FStandaloneCompositeFont over the same files: the first file is the default typeface (with its true bold and italic
 * faces where the key has them), and each fallback file is a sub-typeface over exactly the code points of this case it
 * is the first file to have. Sizes are points at 96 DPI, the pixel size times 0.75. FWidgetRenderer draws it twice,
 * a few frames apart, because Slate rasterises a glyph the first time it paints it. Rich text is read with DreamGUI's
 * markup rules and handed to Slate as one style per run; a superscript or subscript keeps only its smaller size there,
 * since Slate cannot raise a run off the baseline.
 *
 * Chrome is read from Resources/TextParity/Chrome/<case>.png and .json, which Tools/TextParity/Make-ChromeReference.ps1
 * writes. Without them the case says so and compares DreamGUI and Slate with nothing.
 *
 * Everything goes to Saved/DreamGUITextParity: <case>_dream.png, <case>_slate.png, <case>_compare.png (DreamGUI | Slate
 * | Chrome), <case>_mask.png for the corner cases, cases/<case>.json, and report.json and report.md over every case on
 * disk. One thing is asserted: where a case is flagged "breaks", DreamGUI starts its lines where Chrome does. The rest
 * is reported, to be tightened into assertions once the numbers have been looked at.
 */
namespace DreamTextParityTestLocal
{
	using namespace DreamGalleryStage;
	using namespace DreamTextParity;

	/** Grey between the three pictures of a comparison, and the panel of a picture that is missing. */
	static const FColor SeparatorColour(128, 128, 128, 255);
	static const FColor MissingPanelColour(214, 214, 214, 255);
	static constexpr int32 SeparatorWidth = 4;
	/** A pixel is ink when it is this far from the paper, as a fraction of the full range on its farthest channel. */
	static constexpr double InkThreshold = 0.25;
	/** And solid ink, for the corner masks, past this. */
	static constexpr double MaskThreshold = 0.5;

	/** A caret, in canvas pixels from the top-left. */
	struct FCaretSample
	{
		double X = 0.0;
		double Y = 0.0;
	};

	/** What one engine drew for a case, every position in canvas pixels from the top-left. */
	struct FEngineResult
	{
		FString Engine;
		bool bPicture = false;
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		bool bNumbers = false;
		/** Why a picture or numbers are missing, or what is only approximated. */
		TArray<FString> Notes;
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
		double Mass = 0.0;
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
		return EDreamUITextParagraphHorizontalAlign::Left;
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

	/** How far a pixel is from the paper, 0..1, on its farthest channel. */
	double Coverage(const FColor& InPixel, const FColor& InPaper)
	{
		const int32 Delta = FMath::Max3(FMath::Abs(static_cast<int32>(InPixel.R) - InPaper.R), FMath::Abs(static_cast<int32>(InPixel.G) - InPaper.G),
			FMath::Abs(static_cast<int32>(InPixel.B) - InPaper.B));
		return static_cast<double>(Delta) / 255.0;
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
				const double Amount = Coverage(InResult.Pixels[Y * InResult.Size.X + X], InPaper);
				Ink.Mass += Amount;
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

	/** DreamGUI | Slate | Chrome, each in a panel the size of the canvas, grey between them. */
	void ComposeComparison(const FCaseRun& InRun, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		const FIntPoint Panel = InRun.Case.Canvas;
		OutSize = FIntPoint(Panel.X * 3 + SeparatorWidth * 2, Panel.Y);
		OutPixels.Init(SeparatorColour, OutSize.X * OutSize.Y);
		const FEngineResult* Results[] = { &InRun.Dream, &InRun.Slate, &InRun.Chrome };
		for (int32 PanelIndex = 0; PanelIndex < 3; ++PanelIndex)
		{
			const FEngineResult& Result = *Results[PanelIndex];
			const int32 Left = PanelIndex * (Panel.X + SeparatorWidth);
			for (int32 Y = 0; Y < Panel.Y; ++Y)
			{
				for (int32 X = 0; X < Panel.X; ++X)
				{
					const bool bInside = Result.bPicture && X < Result.Size.X && Y < Result.Size.Y;
					OutPixels[Y * OutSize.X + Left + X] = bInside ? Result.Pixels[Y * Result.Size.X + X] : MissingPanelColour;
				}
			}
		}
	}

	/** The DreamGUI text for a case, its box's top-left at (padding, padding) of the canvas. */
	UDreamText* AddDreamText(FGalleryStage& InStage, const FCaseRun& InRun, UDreamUIFontData_BaseObject* InFont)
	{
		const FCase& Case = InRun.Case;
		const int32 Padding = InRun.Corpus.Padding;
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
		Text->SetParagraphHorizontalAlignment(ToDreamAlign(Case.Align));
		Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Top);
		Text->SetFlowDirection(ToDreamDirection(Case.Dir));
		Text->SetFontSpace(FVector2D(Case.LetterSpacing, 0.0));
		Text->SetLineHeightPercentage(Case.LineHeight > 0.0f ? Case.LineHeight : 1.0f);
		Text->SetWrappingPolicy(Case.Wrap.Equals(TEXT("normal"), ESearchCase::IgnoreCase) ? ETextWrappingPolicy::DefaultWrapping : ETextWrappingPolicy::AllowPerCharacterWrapping);
		if (Case.Overflow.Equals(TEXT("ellipsis"), ESearchCase::IgnoreCase))
		{
			Text->SetOverflowType(EDreamUITextOverflowType::Ellipsis);
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
		if (Case.OutlineEm > 0.0f)
		{
			FDreamTextStyle Style = Text->GetTextStyle();
			Style.OutlineColor = Case.OutlineColor;
			Style.OutlineWidth = Case.OutlineEm;
			Text->SetTextStyle(Style);
		}
		Text->SetText(FText::FromString(Case.Text));
		return Text;
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

	/** Slate's composite font for a case: the key's primary as the default typeface, each fallback over its share of the text. */
	TSharedPtr<FStandaloneCompositeFont> MakeSlateFont(const FFontKey& InKey, const FString& InPlainText)
	{
		TSharedPtr<FStandaloneCompositeFont> Font = MakeShared<FStandaloneCompositeFont>();
		Font->DefaultTypeface.AppendFont(TEXT("Regular"), InKey.Faces[0], EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
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
			const int32 Face = FindCoveringFace(InKey, Codepoint);
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
			SubFont.Typeface.AppendFont(TEXT("Regular"), InKey.Faces[Face], EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
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
		InOutRun.SlateFont = MakeSlateFont(InOutRun.FontKey, InOutRun.PlainText);
		const FTextBlockStyle DefaultStyle = MakeSlateStyle(InOutRun, Case.Size, false, false, false, false, TOptional<FColor>());

		TSharedRef<SDreamParityText> TextWidget = SNew(SDreamParityText, DefaultStyle, Box);
		FSlateTextLayout& Layout = TextWidget->GetLayout();
		Layout.SetScale(1.0f);
		Layout.SetWrappingWidth(Case.Width > 0.0f ? Case.Width : 0.0f);
		Layout.SetWrappingPolicy(Case.Wrap.Equals(TEXT("normal"), ESearchCase::IgnoreCase) ? ETextWrappingPolicy::DefaultWrapping : ETextWrappingPolicy::AllowPerCharacterWrapping);
		Layout.SetLineHeightPercentage(Case.LineHeight > 0.0f ? Case.LineHeight : 1.0f);
		Layout.SetJustification(ToSlateJustify(Case.Align));
		Layout.SetTextFlowDirection(ToSlateDirection(Case.Dir));
		if (Case.Overflow.Equals(TEXT("ellipsis"), ESearchCase::IgnoreCase))
		{
			Layout.SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		}
		else if (Case.Overflow.Equals(TEXT("clamp"), ESearchCase::IgnoreCase))
		{
			Layout.SetTextOverflowPolicy(ETextOverflowPolicy::MultilineEllipsis);
			InOutRun.Slate.Notes.Add(TEXT("the clamp is Slate's multi-line ellipsis over the whole box, not a line count"));
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
	 */
	void DrawSlate(FCaseRun& InOutRun)
	{
		if (!InOutRun.SlateRoot.IsValid())
		{
			return;
		}
		const FVector2D DrawSize(InOutRun.Case.Canvas);
		if (InOutRun.SlateRenderer == nullptr)
		{
			InOutRun.SlateRenderer = new FWidgetRenderer(true, true);
		}
		if (!InOutRun.SlateTarget.IsValid())
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->Filter = TF_Nearest;
			Target->ClearColor = FLinearColor::Transparent;
			Target->InitCustomFormat(static_cast<uint32>(InOutRun.Case.Canvas.X), static_cast<uint32>(InOutRun.Case.Canvas.Y),
				EPixelFormat::PF_B8G8R8A8, true);
			Target->UpdateResourceImmediate(true);
			InOutRun.SlateTarget.Reset(Target);
		}
		if (InOutRun.SlateTarget.IsValid())
		{
			InOutRun.SlateRenderer->DrawWidget(InOutRun.SlateTarget.Get(), InOutRun.SlateRoot.ToSharedRef(), DrawSize, 0.0f);
		}
	}

	/** Slate's numbers, from the same layout its picture was painted from. */
	void ReadSlateNumbers(FCaseRun& InOutRun)
	{
		FEngineResult& Result = InOutRun.Slate;
		if (!InOutRun.SlateText.IsValid())
		{
			return;
		}
		const FSlateTextLayout& Layout = InOutRun.SlateText->GetLayout();
		const double Padding = static_cast<double>(InOutRun.Corpus.Padding);
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
				Sample.X = Padding + Location.X;
				Sample.Y = Padding + Location.Y;
				Result.Carets.Add(ModelStarts[ModelIndex] + Local, Sample);
			}
		}
		if (Views.Num() > 0)
		{
			const float Scale = Layout.GetScale();
			int32 AboveBaseline = 0;
			for (const TSharedRef<ILayoutBlock>& Block : Views[0].Blocks)
			{
				const TSharedRef<IRun> Run = Block->GetRun();
				AboveBaseline = FMath::Max(AboveBaseline, static_cast<int32>(Run->GetMaxHeight(Scale)) + static_cast<int32>(Run->GetBaseLine(Scale)));
			}
			Result.Baseline = Padding + Views[0].Offset.Y + AboveBaseline;
			Result.Pitch = Views.Num() >= 2 ? Views[1].Offset.Y - Views[0].Offset.Y : Views[0].Size.Y;
		}
		if (InOutRun.Case.Transform.Equals(TEXT("uppercase"), ESearchCase::IgnoreCase))
		{
			Result.Notes.Add(TEXT("Slate is given the upper-cased text, so its offsets count the upper-cased text"));
		}
		Result.bNumbers = Result.LineStarts.Num() > 0;
	}

	/** Chrome's picture and numbers, when the reference script has made them. */
	void ReadChrome(FCaseRun& InOutRun)
	{
		FEngineResult& Result = InOutRun.Chrome;
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

	/** How one engine's numbers stand against Chrome's. */
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
	};

	FDeviation Compare(const FEngineResult& InEngine, const FEngineResult& InChrome, const FInk& InEngineInk, const FInk& InChromeInk, bool bInMask, const FColor& InPaper)
	{
		FDeviation Deviation;
		if (InEngine.bNumbers && InChrome.bNumbers)
		{
			Deviation.bComparable = true;
			Deviation.bLineStartsEqual = InEngine.LineStarts == InChrome.LineStarts;
			double Sum = 0.0;
			for (const TPair<int32, FCaretSample>& Caret : InEngine.Carets)
			{
				if (const FCaretSample* Reference = InChrome.Carets.Find(Caret.Key))
				{
					const double Delta = FMath::Abs(Caret.Value.X - Reference->X);
					Deviation.CaretMax = FMath::Max(Deviation.CaretMax, Delta);
					Sum += Delta;
					++Deviation.CaretsCompared;
				}
			}
			Deviation.CaretMean = Deviation.CaretsCompared > 0 ? Sum / Deviation.CaretsCompared : 0.0;
			if (InEngine.Baseline.IsSet() && InChrome.Baseline.IsSet())
			{
				Deviation.Baseline = InEngine.Baseline.GetValue() - InChrome.Baseline.GetValue();
			}
			if (InEngine.Pitch.IsSet() && InChrome.Pitch.IsSet())
			{
				Deviation.Pitch = InEngine.Pitch.GetValue() - InChrome.Pitch.GetValue();
			}
		}
		if (InEngineInk.bAny && InChromeInk.bAny)
		{
			Deviation.bInk = true;
			Deviation.InkBounds = FIntRect(InEngineInk.Bounds.Min - InChromeInk.Bounds.Min, InEngineInk.Bounds.Max - InChromeInk.Bounds.Max);
			if (InChromeInk.Mass > 0.0)
			{
				Deviation.InkMass = (InEngineInk.Mass - InChromeInk.Mass) / InChromeInk.Mass;
			}
		}
		if (bInMask)
		{
			Deviation.MaskDifference = CountMaskDifference(InEngine, InChrome, InPaper);
		}
		return Deviation;
	}

	TSharedRef<FJsonObject> EngineJson(const FEngineResult& InResult, const FInk& InInk)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("picture"), InResult.bPicture);
		Object->SetBoolField(TEXT("numbers"), InResult.bNumbers);
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
			Object->SetObjectField(TEXT("ink"), Ink);
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
		if (InDeviation.MaskDifference >= 0)
		{
			Object->SetNumberField(TEXT("maskDifference"), InDeviation.MaskDifference);
		}
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
		const TSharedPtr<FJsonObject> Against = Field(InCase, TEXT("vsChrome"));
		const TSharedPtr<FJsonObject> DreamDelta = Field(Against, TEXT("DreamGUI"));
		const TSharedPtr<FJsonObject> SlateDelta = Field(Against, TEXT("Slate"));
		auto Caret = [&Number](const TSharedPtr<FJsonObject>& InDelta) -> FString
		{
			const TOptional<double> Max = Number(InDelta, TEXT("caretMax"));
			const TOptional<double> Mean = Number(InDelta, TEXT("caretMean"));
			return Max.IsSet() && Mean.IsSet() ? FString::Printf(TEXT("%.1f / %.1f"), Max.GetValue(), Mean.GetValue()) : FString(TEXT("-"));
		};
		auto Mass = [&Number](const TSharedPtr<FJsonObject>& InDelta) -> FString
		{
			const TOptional<double> Value = Number(InDelta, TEXT("inkMass"));
			return Value.IsSet() ? FString::Printf(TEXT("%+.0f%%"), Value.GetValue() * 100.0) : FString(TEXT("-"));
		};
		auto Mask = [&Number](const TSharedPtr<FJsonObject>& InDelta) -> FString
		{
			const TOptional<double> Value = Number(InDelta, TEXT("maskDifference"));
			return Value.IsSet() ? FString::Printf(TEXT("%.0f"), Value.GetValue()) : FString(TEXT("-"));
		};
		bool bEqual = false;
		const bool bHasEqual = DreamDelta.IsValid() && DreamDelta->TryGetBoolField(TEXT("lineStartsEqual"), bEqual);
		FString Id;
		InCase->TryGetStringField(TEXT("id"), Id);
		return FString::Printf(TEXT("| %s | %s | %s | %s | %s | %s | %s | %s / %s | %s / %s | %s / %s | %s / %s |"),
			*Id, *Lines(Dream), *Lines(Slate), *Lines(Chrome),
			bHasEqual ? (bEqual ? TEXT("yes") : TEXT("**no**")) : TEXT("-"),
			*Caret(DreamDelta), *Caret(SlateDelta),
			*Signed(Number(DreamDelta, TEXT("baseline"))), *Signed(Number(SlateDelta, TEXT("baseline"))),
			*Signed(Number(DreamDelta, TEXT("pitch"))), *Signed(Number(SlateDelta, TEXT("pitch"))),
			*Mass(DreamDelta), *Mass(SlateDelta),
			*Mask(DreamDelta), *Mask(SlateDelta));
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
		Markdown += TEXT("Pixels of the canvas. Lines are the UTF-16 offsets where lines start. Caret is the largest and the mean distance in x ");
		Markdown += TEXT("from Chrome's caret over the offsets both have. Baseline and pitch are signed differences from Chrome. Ink is the ");
		Markdown += TEXT("difference in the sum of coverage. Mask counts the pixels solid in one picture and not in Chrome's (corner cases). ");
		Markdown += TEXT("D is DreamGUI and S is Slate. `<case>_compare.png` shows DreamGUI | Slate | Chrome.\n\n");
		Markdown += TEXT("| Case | Lines D | Lines S | Lines C | D breaks = C | Caret D max/mean | Caret S | Baseline D / S | Pitch D / S | Ink D / S | Mask D / S |\n");
		Markdown += TEXT("|---|---|---|---|---|---|---|---|---|---|---|\n");
		for (const FString& Row : Rows)
		{
			Markdown += Row + TEXT("\n");
		}
		FFileHelper::SaveStringToFile(Markdown, *FPaths::Combine(GetOutputDirectory(), TEXT("report.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** Writes the case's pictures and numbers, adds it to the report, and holds DreamGUI's line breaks to Chrome's. */
	void FinishCase(FAutomationTestBase& InTest, FCaseRun& InOutRun)
	{
		const FCase& Case = InOutRun.Case;
		const FColor Paper = Case.GetPaper();
		const FString Directory = GetOutputDirectory();
		IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory, TEXT("cases")), true);

		ReadChrome(InOutRun);
		const FInk DreamInk = MeasureInk(InOutRun.Dream, Paper);
		const FInk SlateInk = MeasureInk(InOutRun.Slate, Paper);
		const FInk ChromeInk = MeasureInk(InOutRun.Chrome, Paper);
		const bool bCorners = Case.HasFlag(TEXT("corners"));
		const FDeviation DreamDeviation = Compare(InOutRun.Dream, InOutRun.Chrome, DreamInk, ChromeInk, bCorners, Paper);
		const FDeviation SlateDeviation = Compare(InOutRun.Slate, InOutRun.Chrome, SlateInk, ChromeInk, bCorners, Paper);

		const FString DreamPath = FPaths::Combine(Directory, Case.Id + TEXT("_dream.png"));
		const FString SlatePath = FPaths::Combine(Directory, Case.Id + TEXT("_slate.png"));
		const FString ComparePath = FPaths::Combine(Directory, Case.Id + TEXT("_compare.png"));
		if (InOutRun.Dream.bPicture)
		{
			UDreamUICaptureLibrary::SavePixelsToPng(InOutRun.Dream.Pixels, InOutRun.Dream.Size, DreamPath);
		}
		if (InOutRun.Slate.bPicture)
		{
			UDreamUICaptureLibrary::SavePixelsToPng(InOutRun.Slate.Pixels, InOutRun.Slate.Size, SlatePath);
		}
		TArray<FColor> Composite;
		FIntPoint CompositeSize = FIntPoint::ZeroValue;
		ComposeComparison(InOutRun, Composite, CompositeSize);
		UDreamUICaptureLibrary::SavePixelsToPng(Composite, CompositeSize, ComparePath);
		if (bCorners && InOutRun.Dream.bPicture && InOutRun.Chrome.bPicture)
		{
			TArray<FColor> Mask;
			FIntPoint MaskSize = FIntPoint::ZeroValue;
			MakeMaskPicture(InOutRun.Dream, InOutRun.Chrome, Paper, Mask, MaskSize);
			UDreamUICaptureLibrary::SavePixelsToPng(Mask, MaskSize, FPaths::Combine(Directory, Case.Id + TEXT("_mask.png")));
		}

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Case.Id);
		Entry->SetStringField(TEXT("base"), Case.BaseId);
		Entry->SetStringField(TEXT("written"), FDateTime::UtcNow().ToIso8601());
		Entry->SetStringField(TEXT("font"), Case.Font);
		Entry->SetNumberField(TEXT("size"), Case.Size);
		Entry->SetNumberField(TEXT("width"), Case.Width);
		Entry->SetField(TEXT("canvas"), IntArrayValue({ Case.Canvas.X, Case.Canvas.Y }));
		TArray<TSharedPtr<FJsonValue>> Flags;
		for (const FString& Flag : Case.Flags)
		{
			Flags.Add(MakeShared<FJsonValueString>(Flag));
		}
		Entry->SetArrayField(TEXT("flags"), Flags);
		TSharedRef<FJsonObject> Engines = MakeShared<FJsonObject>();
		Engines->SetObjectField(TEXT("DreamGUI"), EngineJson(InOutRun.Dream, DreamInk));
		Engines->SetObjectField(TEXT("Slate"), EngineJson(InOutRun.Slate, SlateInk));
		Engines->SetObjectField(TEXT("Chrome"), EngineJson(InOutRun.Chrome, ChromeInk));
		Entry->SetObjectField(TEXT("engines"), Engines);
		TSharedRef<FJsonObject> Against = MakeShared<FJsonObject>();
		Against->SetObjectField(TEXT("DreamGUI"), DeviationJson(DreamDeviation));
		Against->SetObjectField(TEXT("Slate"), DeviationJson(SlateDeviation));
		Entry->SetObjectField(TEXT("vsChrome"), Against);
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
		InTest.AddInfo(FString::Printf(TEXT("%s: lines start at DreamGUI [%s], Slate [%s], Chrome [%s]; caret x off Chrome by at most %.1f px (DreamGUI) and %.1f px (Slate); %s"),
			*Case.Id, *IntListText(InOutRun.Dream.LineStarts), *IntListText(InOutRun.Slate.LineStarts), *IntListText(InOutRun.Chrome.LineStarts),
			DreamDeviation.CaretMax, SlateDeviation.CaretMax, *ComparePath));
		if (bCorners && DreamDeviation.MaskDifference >= 0)
		{
			InTest.AddInfo(FString::Printf(TEXT("%s: %d pixel(s) solid in DreamGUI's picture and not in Chrome's or the other way, %d in Slate's."),
				*Case.Id, DreamDeviation.MaskDifference, SlateDeviation.MaskDifference));
		}

		if (Case.HasFlag(TEXT("breaks")))
		{
			if (!InOutRun.Chrome.bNumbers)
			{
				InTest.AddInfo(FString::Printf(TEXT("%s: the line breaks are not checked, there is no Chrome reference. Run Tools/TextParity/Make-ChromeReference.ps1 to make one."), *Case.Id));
			}
			else
			{
				InTest.TestTrue(FString::Printf(TEXT("%s: DreamGUI starts its lines where Chrome does (DreamGUI [%s], Chrome [%s])"),
					*Case.Id, *IntListText(InOutRun.Dream.LineStarts), *IntListText(InOutRun.Chrome.LineStarts)),
					InOutRun.Dream.bNumbers && InOutRun.Dream.LineStarts == InOutRun.Chrome.LineStarts);
			}
		}
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
 * written and reported, and for a case flagged "breaks" DreamGUI's line starts must equal Chrome's. Nothing else is
 * asserted yet; a missing Chrome reference is reported, never a failure.
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
	if (!TestTrue(FString::Printf(TEXT("%s: the font key %s is in the fonts table"), *Run->Case.Id, *Run->Case.Font), Run->Corpus.FindFontKey(Run->Case.Font, Run->FontKey)))
	{
		return false;
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

	FStageRef Stage = BeginStage(*this, Run->Case.Canvas, Run->Case.GetPaper());
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
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
		BuildSlate(*Run);
		DrawSlate(*Run);
	});
	// Slate rasterises a glyph the first time it paints it and uploads its atlas with a later frame.
	EnqueueFrames(Stage, 2);
	EnqueueDo([this, Run]()
	{
		DrawSlate(*Run);
		Run->Slate.bPicture = Run->SlateTarget.IsValid() && FDreamPixelProbe::ReadBack(Run->SlateTarget.Get(), Run->Slate.Pixels, Run->Slate.Size);
		if (!Run->Slate.bPicture)
		{
			Run->Slate.Notes.Add(TEXT("the widget renderer's target could not be read back"));
		}
		ReadSlateNumbers(*Run);
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
 * Which face of its font key has a glyph for each code point of each case, read from the font files with FreeType: a
 * code point no face has is drawn by whatever each engine falls back to (Chrome, a system font), and is not a layout
 * difference. Reported, with the faces that are missing from disk; it fails only when the corpus cannot be read.
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
			AddError(FString::Printf(TEXT("%s names the font key %s, which the fonts table does not have."), *Case.Id, *Case.Font));
			continue;
		}
		for (const FString& Face : Key.Faces)
		{
			if (!GetFaceCoverage(Face).bLoaded)
			{
				MissingFiles.Add(Face);
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
			const int32 Face = FindCoveringFace(Key, Codepoint);
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
			Shares.Add(FString::Printf(TEXT("%s %d"), *FPaths::GetBaseFilename(Key.Faces[Face]), PerFace[Face]));
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
	const FString Directory = GetOutputDirectory();
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, TEXT("coverage.md"));
	FFileHelper::SaveStringToFile(Markdown, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	AddInfo(FString::Printf(TEXT("%d code point(s) of the corpus have no face in their font key; the table is %s."), Uncovered, *Path));
	return true;
}

#endif
