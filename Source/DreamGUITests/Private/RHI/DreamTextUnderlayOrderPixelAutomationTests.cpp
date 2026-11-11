// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/DreamUITextData.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * A distance-field text's underlay on the GPU, where the letters of one text overlap: tight negative letter spacing and
 * italic, the way a heavy condensed title is set. The underlay is a shadow of the whole text, so every glyph's underlay
 * draws before any glyph's outline or face -- a later glyph's shadow never lands on an earlier glyph.
 *
 * Each case is drawn twice, side by side at whole-pixel positions so the two fall on the pixel grid alike: once without
 * the underlay, once with it. Wherever the text without it is wholly face, or wholly outline, the text with it must be the
 * same colour: a shadow goes under what the text draws, never over it. Drawn glyph by glyph -- each glyph's underlay,
 * glow and outline in one quad -- the next letter's shadow covered this one's outline wherever they overlapped.
 */
namespace DreamTextUnderlayOrderPixelTestLocal
{
	using namespace DreamGalleryStage;

	static const FIntPoint StageSize(840, 360);
	static const FVector2D TextBox(400.0, 150.0);
	/** The two columns, centred this far left and right of the stage's centre: the text without the underlay, then with it. */
	static constexpr double ColumnX = 205.0;
	static constexpr float FontSize = 96.0f;
	/** Letter spacing in em: tight enough that each letter's left stem sits inside the one before it. */
	static constexpr float SpacingEm = -0.34f;
	static const FColor FaceColor = FColor::White;
	static const FColor OutlineColor(0, 150, 255, 255);
	static const FColor UnderlayColor(255, 0, 0, 255);
	/** Right and a little down: across the next letter's left stem onto the one before it. */
	static const FVector2f UnderlayOffset(0.11f, 0.03f);
	/** Codes a channel may differ by and still be the same colour: both copies are drawn the same way. */
	static constexpr int32 SameTolerance = 6;

	struct FCase
	{
		const TCHAR* Name;
		bool bOutline;
		/** The row's centre above the stage's centre, +Y up. */
		double Y;
	};
	static const FCase Cases[] =
	{
		{ TEXT("Face"), false, 85.0 },
		{ TEXT("Outline"), true, -85.0 },
	};

	/** A pixel rect of the stage: InSize centred at InPosition from the stage's centre, +Y up, clipped to the picture. */
	FIntRect StageRect(FIntPoint InStageSize, const FVector2D& InSize, const FVector2D& InPosition)
	{
		const int32 Left = FMath::RoundToInt32(InStageSize.X * 0.5 + InPosition.X - InSize.X * 0.5);
		const int32 Top = FMath::RoundToInt32(InStageSize.Y * 0.5 - InPosition.Y - InSize.Y * 0.5);
		FIntRect Rect(Left, Top, Left + FMath::RoundToInt32(InSize.X), Top + FMath::RoundToInt32(InSize.Y));
		Rect.Clip(FIntRect(0, 0, InStageSize.X, InStageSize.Y));
		return Rect;
	}

	int32 ChannelDelta(const FColor& InA, const FColor& InB)
	{
		return FMath::Max3(FMath::Abs(InA.R - InB.R), FMath::Abs(InA.G - InB.G), FMath::Abs(InA.B - InB.B));
	}

	double LinearOfCode(uint8 InCode)
	{
		const double Encoded = static_cast<double>(InCode) / 255.0;
		return Encoded <= 0.04045 ? Encoded / 12.92 : FMath::Pow((Encoded + 0.055) / 1.055, 2.4);
	}

	/** The largest difference on any channel in linear light: how much of something else shows through, whatever the encode. */
	double LinearDelta(const FColor& InA, const FColor& InB)
	{
		return FMath::Max3(FMath::Abs(LinearOfCode(InA.R) - LinearOfCode(InB.R)), FMath::Abs(LinearOfCode(InA.G) - LinearOfCode(InB.G)),
			FMath::Abs(LinearOfCode(InA.B) - LinearOfCode(InB.B)));
	}
	/**
	 * Where two letters' outlines overlap, each anti-aliased, together they may leave a percent or two of what is under them
	 * showing -- now the shadow, as it should; a shadow drawn over an outline replaces most of it.
	 */
	static constexpr double SameLinearTolerance = 0.04;

	/** The case's text, italic and tightly spaced, with an outline when the case has one and an underlay when asked. */
	UDreamText* AddCaseText(FGalleryStage& InStage, const FCase& InCase, bool bInUnderlay)
	{
		const FString Name = FString::Printf(TEXT("%s%s"), InCase.Name, bInUnderlay ? TEXT("Underlay") : TEXT("Plain"));
		UDreamText* Text = InStage.AddText(*Name, TEXT("HNHMH"), FontSize, TextBox,
			FVector2D(bInUnderlay ? ColumnX : -ColumnX, InCase.Y), FaceColor);
		if (Text == nullptr)
		{
			return nullptr;
		}
		Text->SetFontStyle(EDreamUITextFontStyle::Italic);
		Text->SetFontSpace(FVector2D(SpacingEm * FontSize, 0.0));
		FDreamTextStyle Style = Text->GetTextStyle();
		if (InCase.bOutline)
		{
			Style.OutlineColor = OutlineColor;
			Style.OutlineWidth = 0.06f;
		}
		if (bInUnderlay)
		{
			Style.UnderlayColor = UnderlayColor;
			Style.UnderlayOffset = UnderlayOffset;
			Style.UnderlaySoftness = 0.0f;
		}
		Text->SetTextStyle(Style);
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextUnderlayUnderOverlappingLettersTest,
	"DreamGUI.RHI.Text.AnUnderlayDrawsUnderEveryLetterOfItsTextWhereTheLettersOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamTextUnderlayUnderOverlappingLettersTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextUnderlayOrderPixelTestLocal;
	FStageRef Stage = BeginStage(*this, StageSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	for (const FCase& Case : Cases)
	{
		UDreamText* Plain = AddCaseText(*Stage, Case, false);
		UDreamText* Shadowed = AddCaseText(*Stage, Case, true);
		if (!TestTrue(FString::Printf(TEXT("%s: both texts on the stage"), Case.Name), Plain != nullptr && Shadowed != nullptr))
		{
			Stage->TearDown();
			return false;
		}
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 8000);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([this, Stage]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The stage reads back"), Stage->ReadBack(Pixels, Size)) || !TestTrue(TEXT("...at its size"), Size == StageSize))
		{
			return;
		}
		const FString CapturePath = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Text_UnderlayUnderOverlappingLetters"));
		for (const FCase& Case : Cases)
		{
			const FIntRect PlainBox = StageRect(Size, TextBox, FVector2D(-ColumnX, Case.Y));
			const FIntRect ShadowedBox = StageRect(Size, TextBox, FVector2D(ColumnX, Case.Y));
			const FIntPoint Shift = ShadowedBox.Min - PlainBox.Min;
			int32 FacePixels = 0;
			int32 FaceCovered = 0;
			int32 OutlinePixels = 0;
			int32 OutlineCovered = 0;
			int32 ShadowPixels = 0;
			FIntPoint FirstCovered(-1, -1);
			FColor FirstCoveredColor = FColor::Black;
			// Wholly one colour: the pixel and its eight neighbours, in the text without the underlay. A pixel on the
			// anti-aliased edge of an outline lets a little of what is under it through -- the backdrop there, the shadow
			// here -- and the sRGB encode makes a percent of red read as thirty codes.
			auto IsWhollyAt = [&Pixels, &Size](int32 InX, int32 InY, const FColor& InColor)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						if (ChannelDelta(Pixels[(InY + Dy) * Size.X + InX + Dx], InColor) > 2)
						{
							return false;
						}
					}
				}
				return true;
			};
			for (int32 Y = PlainBox.Min.Y + 1; Y < PlainBox.Max.Y - 1; ++Y)
			{
				for (int32 X = PlainBox.Min.X + 1; X < PlainBox.Max.X - 1; ++X)
				{
					const FColor& Without = Pixels[Y * Size.X + X];
					const FColor& With = Pixels[(Y + Shift.Y) * Size.X + X + Shift.X];
					if (ChannelDelta(With, UnderlayColor) <= SameTolerance)
					{
						++ShadowPixels;
					}
					const bool bFace = IsWhollyAt(X, Y, FaceColor);
					const bool bOutline = Case.bOutline && IsWhollyAt(X, Y, OutlineColor);
					if (!bFace && !bOutline)
					{
						continue;
					}
					const bool bCovered = LinearDelta(With, Without) > SameLinearTolerance;
					(bFace ? FacePixels : OutlinePixels)++;
					if (bCovered)
					{
						(bFace ? FaceCovered : OutlineCovered)++;
						if (FirstCovered.X < 0)
						{
							FirstCovered = FIntPoint(X + Shift.X, Y + Shift.Y);
							FirstCoveredColor = With;
						}
					}
				}
			}
			AddInfo(FString::Printf(TEXT("%s: %d face pixels (%d changed by the underlay), %d outline pixels (%d changed), %d pixels of shadow showing; picture: %s."),
				Case.Name, FacePixels, FaceCovered, OutlinePixels, OutlineCovered, ShadowPixels, *CapturePath));
			if (!TestTrue(FString::Printf(TEXT("%s: the text draws its face (%d pixels wholly face) and its underlay shows (%d pixels of shadow)"), Case.Name, FacePixels, ShadowPixels),
				FacePixels > 1500 && ShadowPixels > 300 && (!Case.bOutline || OutlinePixels > 400)))
			{
				continue;
			}
			const FString Where = FirstCovered.X < 0 ? FString()
				: FString::Printf(TEXT("; the first at (%d, %d) is %s"), FirstCovered.X, FirstCovered.Y, *FDreamPixelProbe::Describe(FirstCoveredColor));
			TestTrue(FString::Printf(TEXT("%s: no pixel the face wholly covers changes under the underlay (%d of %d do%s)"), Case.Name, FaceCovered, FacePixels, *Where),
				FaceCovered == 0);
			if (Case.bOutline)
			{
				TestTrue(FString::Printf(TEXT("%s: no pixel the outline wholly covers changes under the underlay (%d of %d do%s)"), Case.Name, OutlineCovered, OutlinePixels, *Where),
					OutlineCovered == 0);
			}
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
