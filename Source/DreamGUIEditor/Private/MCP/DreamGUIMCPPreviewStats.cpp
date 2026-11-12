// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MCP/DreamGUIMCPPreviewStats.h"

namespace DreamGUIMCP
{
	namespace
	{
		using FThresholds = FPreviewStatsThresholds;

		double Luma(const FColor& Pixel)
		{
			// Rec. 709 weights on the picture's encoded values: how bright the PNG looks, not how much light it stands for.
			return (0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B) / 255.0;
		}

		int32 MaxChannelDifference(const FColor& A, const FColor& B)
		{
			return FMath::Max3(FMath::Abs(int32(A.R) - int32(B.R)), FMath::Abs(int32(A.G) - int32(B.G)), FMath::Abs(int32(A.B) - int32(B.B)));
		}

		FString Percent(double Share)
		{
			const double Value = Share * 100.0;
			return Value > 0.0 && Value < 1.0 ? FString::Printf(TEXT("%.1f%%"), Value) : FString::Printf(TEXT("%.0f%%"), Value);
		}
	}

	void FPreviewStats::Add(TConstArrayView<FColor> Picture, TConstArrayView<FColor> OverBlack, TConstArrayView<FColor> OverWhite)
	{
		const bool bHasBackdrops = OverBlack.Num() == Picture.Num() && OverWhite.Num() == Picture.Num();
		for (int32 Index = 0; Index < Picture.Num(); ++Index)
		{
			++Pixels;
			const FColor& Pixel = Picture[Index];
			if (bHasBackdrops)
			{
				const int32 Difference = MaxChannelDifference(OverBlack[Index], OverWhite[Index]);
				if (Difference >= FThresholds::UncoveredDifference)
				{
					continue;
				}
				if (Difference > FThresholds::CoveredTolerance)
				{
					++PartialPixels;
				}
			}
			++CoveredPixels;
			Sum[0] += Pixel.R / 255.0;
			Sum[1] += Pixel.G / 255.0;
			Sum[2] += Pixel.B / 255.0;
			SumSquares[0] += FMath::Square(Pixel.R / 255.0);
			SumSquares[1] += FMath::Square(Pixel.G / 255.0);
			SumSquares[2] += FMath::Square(Pixel.B / 255.0);
			const double L = Luma(Pixel);
			LumaSum += L;
			LumaSumSquares += L * L;
			LumaMin = FMath::Min(LumaMin, L);
			LumaMax = FMath::Max(LumaMax, L);
			if (Pixel.R >= FThresholds::NearWhite && Pixel.G >= FThresholds::NearWhite && Pixel.B >= FThresholds::NearWhite)
			{
				++NearWhitePixels;
			}
			if (Pixel.R <= FThresholds::NearBlack && Pixel.G <= FThresholds::NearBlack && Pixel.B <= FThresholds::NearBlack)
			{
				++NearBlackPixels;
			}
		}
	}

	FLinearColor FPreviewStats::GetMeanColor() const
	{
		if (CoveredPixels <= 0)
		{
			return FLinearColor::Black;
		}
		const double N = double(CoveredPixels);
		return FLinearColor(float(Sum[0] / N), float(Sum[1] / N), float(Sum[2] / N), 1.0f);
	}

	double FPreviewStats::GetLumaMean() const
	{
		return CoveredPixels > 0 ? LumaSum / double(CoveredPixels) : 0.0;
	}

	double FPreviewStats::GetLumaStdDev() const
	{
		if (CoveredPixels <= 0)
		{
			return 0.0;
		}
		const double Mean = GetLumaMean();
		return FMath::Sqrt(FMath::Max(0.0, LumaSumSquares / double(CoveredPixels) - Mean * Mean));
	}

	double FPreviewStats::GetChannelStdDev() const
	{
		if (CoveredPixels <= 0)
		{
			return 0.0;
		}
		double Largest = 0.0;
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			const double Mean = Sum[Channel] / double(CoveredPixels);
			Largest = FMath::Max(Largest, FMath::Sqrt(FMath::Max(0.0, SumSquares[Channel] / double(CoveredPixels) - Mean * Mean)));
		}
		return Largest;
	}

	double FPreviewStats::GetCoverage() const { return Pixels > 0 ? double(CoveredPixels) / double(Pixels) : 0.0; }
	double FPreviewStats::GetPartialShare() const { return CoveredPixels > 0 ? double(PartialPixels) / double(CoveredPixels) : 0.0; }
	double FPreviewStats::GetNearWhiteShare() const { return CoveredPixels > 0 ? double(NearWhitePixels) / double(CoveredPixels) : 0.0; }
	double FPreviewStats::GetNearBlackShare() const { return CoveredPixels > 0 ? double(NearBlackPixels) / double(CoveredPixels) : 0.0; }

	bool FPreviewStats::IsFlat() const
	{
		return CoveredPixels > 0 && GetLumaStdDev() <= FThresholds::FlatStdDev && GetChannelStdDev() <= FThresholds::FlatStdDev;
	}

	TSharedRef<FJsonObject> FPreviewStats::ToJson() const
	{
		const auto Round3 = [](double Value) { return FMath::RoundToDouble(Value * 1000.0) / 1000.0; };
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("coverage"), Round3(GetCoverage()));
		Object->SetNumberField(TEXT("partlyTransparent"), Round3(GetPartialShare()));
		Object->SetStringField(TEXT("meanColor"), FormatHexColor(GetMeanColor()));
		Object->SetStringField(TEXT("meanColorName"), NameColor(GetMeanColor()));
		TSharedRef<FJsonObject> Luminance = MakeShared<FJsonObject>();
		Luminance->SetNumberField(TEXT("min"), Round3(CoveredPixels > 0 ? LumaMin : 0.0));
		Luminance->SetNumberField(TEXT("max"), Round3(CoveredPixels > 0 ? LumaMax : 0.0));
		Luminance->SetNumberField(TEXT("mean"), Round3(GetLumaMean()));
		Luminance->SetNumberField(TEXT("stdDev"), Round3(GetLumaStdDev()));
		Object->SetObjectField(TEXT("luminance"), Luminance);
		Object->SetNumberField(TEXT("nearWhite"), Round3(GetNearWhiteShare()));
		Object->SetNumberField(TEXT("nearBlack"), Round3(GetNearBlackShare()));
		Object->SetBoolField(TEXT("flat"), IsFlat());
		return Object;
	}

	FString FormatHexColor(const FLinearColor& Color)
	{
		const auto Byte = [](float Value) { return FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255); };
		return FString::Printf(TEXT("#%02X%02X%02X"), Byte(Color.R), Byte(Color.G), Byte(Color.B));
	}

	FString NameColor(const FLinearColor& Color)
	{
		const FLinearColor Hsv = Color.LinearRGBToHSV();
		const float Hue = Hsv.R;
		const float Saturation = Hsv.G;
		const float Value = Hsv.B;
		if (Value < 0.08f)
		{
			return TEXT("black");
		}
		if (Saturation < 0.12f)
		{
			if (Value > 0.92f) { return TEXT("white"); }
			if (Value > 0.7f) { return TEXT("light grey"); }
			return Value < 0.4f ? TEXT("dark grey") : TEXT("grey");
		}
		enum class EHue : uint8 { Red, Orange, Yellow, Green, Cyan, Blue, Purple, Magenta };
		EHue Family = EHue::Red;
		if (Hue >= 15.0f && Hue < 40.0f) { Family = EHue::Orange; }
		else if (Hue >= 40.0f && Hue < 65.0f) { Family = EHue::Yellow; }
		else if (Hue >= 65.0f && Hue < 160.0f) { Family = EHue::Green; }
		else if (Hue >= 160.0f && Hue < 195.0f) { Family = EHue::Cyan; }
		else if (Hue >= 195.0f && Hue < 255.0f) { Family = EHue::Blue; }
		else if (Hue >= 255.0f && Hue < 285.0f) { Family = EHue::Purple; }
		else if (Hue >= 285.0f && Hue < 345.0f) { Family = EHue::Magenta; }
		if (Saturation < 0.4f)
		{
			const bool bLight = Value > 0.55f;
			const TCHAR* Tint = TEXT("");
			switch (Family)
			{
			case EHue::Red:     Tint = bLight ? TEXT("pinkish") : TEXT("reddish"); break;
			case EHue::Orange:  Tint = bLight ? TEXT("warm") : TEXT("brownish"); break;
			case EHue::Yellow:  Tint = TEXT("yellowish"); break;
			case EHue::Green:   Tint = TEXT("greenish"); break;
			case EHue::Cyan:    Tint = TEXT("bluish-green"); break;
			case EHue::Blue:    Tint = TEXT("bluish"); break;
			case EHue::Purple:  Tint = TEXT("purplish"); break;
			case EHue::Magenta: Tint = bLight ? TEXT("pinkish") : TEXT("purplish"); break;
			}
			const TCHAR* Lightness = Value > 0.8f ? TEXT("light ") : (Value < 0.4f ? TEXT("dark ") : TEXT(""));
			return FString::Printf(TEXT("%s%s grey"), Lightness, Tint);
		}
		if ((Family == EHue::Orange || Family == EHue::Yellow) && Value < 0.5f)
		{
			return Value < 0.3f ? TEXT("dark brown") : TEXT("brown");
		}
		const TCHAR* Name = TEXT("red");
		switch (Family)
		{
		case EHue::Red:     Name = TEXT("red"); break;
		case EHue::Orange:  Name = TEXT("orange"); break;
		case EHue::Yellow:  Name = TEXT("yellow"); break;
		case EHue::Green:   Name = TEXT("green"); break;
		case EHue::Cyan:    Name = TEXT("cyan"); break;
		case EHue::Blue:    Name = TEXT("blue"); break;
		case EHue::Purple:  Name = TEXT("purple"); break;
		case EHue::Magenta: Name = TEXT("magenta"); break;
		}
		if (Value < 0.35f)
		{
			return FString::Printf(TEXT("dark %s"), Name);
		}
		if (Saturation < 0.5f && Value > 0.8f)
		{
			return FString::Printf(TEXT("pale %s"), Name);
		}
		return Name;
	}

	double MeanFrameDifference(TConstArrayView<FColor> A, TConstArrayView<FColor> B)
	{
		if (A.Num() != B.Num() || A.Num() == 0)
		{
			return 0.0;
		}
		double Total = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			Total += FMath::Abs(int32(A[Index].R) - int32(B[Index].R)) + FMath::Abs(int32(A[Index].G) - int32(B[Index].G))
				+ FMath::Abs(int32(A[Index].B) - int32(B[Index].B));
		}
		return Total / (3.0 * double(A.Num()));
	}

	double ChangedPixelShare(TConstArrayView<FColor> A, TConstArrayView<FColor> B)
	{
		if (A.Num() != B.Num() || A.Num() == 0)
		{
			return 0.0;
		}
		int32 Changed = 0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const int32 Most = FMath::Max3(FMath::Abs(int32(A[Index].R) - int32(B[Index].R)), FMath::Abs(int32(A[Index].G) - int32(B[Index].G)),
				FMath::Abs(int32(A[Index].B) - int32(B[Index].B)));
			Changed += Most >= FThresholds::ChangedPixelDifference ? 1 : 0;
		}
		return double(Changed) / double(A.Num());
	}

	FPreviewAnimation AnalysePreviewAnimation(TConstArrayView<TArray<FColor>> Frames, TConstArrayView<FPreviewStats> PerFrame)
	{
		FPreviewAnimation Animation;
		Animation.Frames = Frames.Num();
		for (int32 Index = 1; Index < Frames.Num(); ++Index)
		{
			Animation.MaxDifference = FMath::Max(Animation.MaxDifference, MeanFrameDifference(Frames[Index], Frames[0]));
			Animation.MaxChangedShare = FMath::Max(Animation.MaxChangedShare, ChangedPixelShare(Frames[Index], Frames[0]));
		}
		Animation.bChanges = Animation.MaxDifference > FThresholds::SameFrameDifference || Animation.MaxChangedShare >= FThresholds::ChangedShare;
		TArray<double> Adjacent;
		for (int32 Index = 0; Index + 1 < Frames.Num(); ++Index)
		{
			Adjacent.Add(MeanFrameDifference(Frames[Index], Frames[Index + 1]));
		}
		double Direct = 0.0;
		double Path = 0.0;
		for (int32 Index = 0; Index + 2 < Frames.Num(); ++Index)
		{
			Direct += MeanFrameDifference(Frames[Index], Frames[Index + 2]);
			Path += Adjacent[Index] + Adjacent[Index + 1];
		}
		Animation.Straightness = Path > 0.0 ? Direct / Path : 1.0;
		double AdjacentMean = 0.0;
		for (const double Step : Adjacent)
		{
			AdjacentMean += Step / double(Adjacent.Num());
		}
		Animation.bUndersampled = Animation.bChanges && Frames.Num() >= 4 && AdjacentMean >= 1.0
			&& Animation.Straightness < FPreviewAnimation::UndersampledStraightness;
		double Darkest = TNumericLimits<double>::Max();
		double Brightest = -1.0;
		for (int32 Index = 0; Index < PerFrame.Num(); ++Index)
		{
			const double L = PerFrame[Index].GetLumaMean();
			if (L < Darkest) { Darkest = L; Animation.Darkest = Index; }
			if (L > Brightest) { Brightest = L; Animation.Brightest = Index; }
		}
		return Animation;
	}

	FString DescribePreviewAnimation(const FPreviewAnimation& Animation, TConstArrayView<FPreviewStats> PerFrame, TConstArrayView<double> Times,
		const double Duration, TArray<FString>& OutNext)
	{
		if (!Animation.bChanges)
		{
			OutNext.Add(TEXT("Nothing moved. A timeline plays only when preview names it (timeline=<name>); a tween runs only when something starts it."));
			return FString::Printf(TEXT("The %d frames over %g s are the same: nothing animates"), Animation.Frames, Duration);
		}
		const auto At = [&Times](int32 Index) { return Times.IsValidIndex(Index) ? Times[Index] : 0.0; };
		FString Sentence = FString::Printf(TEXT("It animates over %d frames in %g s, changing by up to %.1f/255 per pixel on average and %.2f%% of the pixels: darkest at %g s, brightest at %g s"),
			Animation.Frames, Duration, Animation.MaxDifference, Animation.MaxChangedShare * 100.0, At(Animation.Darkest), At(Animation.Brightest));
		if (Animation.bUndersampled)
		{
			Sentence += TEXT("; it jumps back and forth between frames, so it may change faster than they sample it");
			OutNext.Add(FString::Printf(TEXT("The screen may change faster than %d frames in %g s can show. Shorten duration, or raise frames."), Animation.Frames, Duration));
		}
		return Sentence;
	}

	FString DescribePreviewFrames(const FPreviewStats& Overall, TConstArrayView<FPreviewStats> PerFrame, TArray<FString>& OutNext)
	{
		if (PerFrame.Num() <= 1)
		{
			return DescribePreview(PerFrame.Num() == 1 ? PerFrame[0] : Overall, OutNext);
		}
		int32 Emptiest = 0;
		int32 Fullest = 0;
		for (int32 Index = 1; Index < PerFrame.Num(); ++Index)
		{
			if (PerFrame[Index].GetCoverage() < PerFrame[Emptiest].GetCoverage()) { Emptiest = Index; }
			if (PerFrame[Index].GetCoverage() > PerFrame[Fullest].GetCoverage()) { Fullest = Index; }
		}
		const double Low = PerFrame[Emptiest].GetCoverage();
		const double High = PerFrame[Fullest].GetCoverage();
		if (High < FThresholds::NearlyEmpty)
		{
			OutNext.Add(TEXT("Run layout: it says where each node is and which are hidden, zero-sized or off the screen."));
			return FString::Printf(TEXT("The screen draws almost nothing in any of its %d frames: at most %s of the viewport is covered"), PerFrame.Num(), *Percent(High));
		}
		// The frames together, unless they average out to next to nothing: then the fullest frame speaks for them.
		TArray<FString> Ignored;
		FString Sentence = DescribePreview(Overall.GetCoverage() < FThresholds::NearlyEmpty ? PerFrame[Fullest] : Overall, Ignored);
		if (Sentence.StartsWith(TEXT("It ")))
		{
			Sentence[0] = TEXT('i');
		}
		Sentence = FString::Printf(TEXT("Over its %d frames %s"), PerFrame.Num(), *Sentence);
		if (High - Low >= 0.01)
		{
			Sentence += FString::Printf(TEXT(" (frame by frame %s to %s covered, least in frame %d, most in frame %d)"), *Percent(Low), *Percent(High), Emptiest + 1, Fullest + 1);
		}
		return Sentence;
	}

	TSharedRef<FJsonObject> PreviewAnimationToJson(const FPreviewAnimation& Animation, TConstArrayView<FPreviewStats> PerFrame,
		TConstArrayView<double> Times, const double Duration)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("frames"), Animation.Frames);
		Object->SetNumberField(TEXT("duration"), Duration);
		Object->SetNumberField(TEXT("maxDifference"), FMath::RoundToDouble(Animation.MaxDifference * 100.0) / 100.0);
		Object->SetNumberField(TEXT("changedShare"), FMath::RoundToDouble(Animation.MaxChangedShare * 100000.0) / 100000.0);
		Object->SetBoolField(TEXT("changes"), Animation.bChanges);
		Object->SetNumberField(TEXT("straightness"), FMath::RoundToDouble(Animation.Straightness * 1000.0) / 1000.0);
		Object->SetBoolField(TEXT("undersampled"), Animation.bUndersampled);
		TArray<TSharedPtr<FJsonValue>> Frames;
		for (int32 Index = 0; Index < PerFrame.Num(); ++Index)
		{
			TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetNumberField(TEXT("time"), Times.IsValidIndex(Index) ? Times[Index] : 0.0);
			Frame->SetStringField(TEXT("meanColor"), FormatHexColor(PerFrame[Index].GetMeanColor()));
			Frame->SetNumberField(TEXT("luminance"), FMath::RoundToDouble(PerFrame[Index].GetLumaMean() * 1000.0) / 1000.0);
			Frame->SetNumberField(TEXT("coverage"), FMath::RoundToDouble(PerFrame[Index].GetCoverage() * 1000.0) / 1000.0);
			Frames.Add(MakeShared<FJsonValueObject>(Frame));
		}
		Object->SetArrayField(TEXT("frames"), Frames);
		return Object;
	}

	FString DescribePreview(const FPreviewStats& Stats, TArray<FString>& OutNext)
	{
		const double Coverage = Stats.GetCoverage();
		if (Coverage < FThresholds::NearlyEmpty)
		{
			OutNext.Add(TEXT("Run layout: it says where each node is and which are hidden, zero-sized or off the screen."));
			return FString::Printf(TEXT("The screen draws almost nothing: %s of the viewport is covered"), *Percent(Coverage));
		}
		const FString Covers = FString::Printf(TEXT("It covers %s of the viewport"), *Percent(Coverage));
		if (Stats.GetNearBlackShare() >= 0.9)
		{
			return FString::Printf(TEXT("%s, and %s of that is near black"), *Covers, *Percent(Stats.GetNearBlackShare()));
		}
		if (Stats.GetNearWhiteShare() >= 0.3)
		{
			return FString::Printf(TEXT("%s, and %s of that is near white"), *Covers, *Percent(Stats.GetNearWhiteShare()));
		}
		const FLinearColor Mean = Stats.GetMeanColor();
		if (Stats.IsFlat())
		{
			return FString::Printf(TEXT("%s with one flat colour, %s (%s)"), *Covers, *FormatHexColor(Mean), *NameColor(Mean));
		}
		FString Sentence = FString::Printf(TEXT("%s; mean colour %s (%s), luminance %.2f to %.2f"), *Covers, *FormatHexColor(Mean), *NameColor(Mean),
			Stats.LumaMin, Stats.LumaMax);
		if (Stats.GetPartialShare() >= 0.05)
		{
			Sentence += FString::Printf(TEXT(", %s of it partly transparent"), *Percent(Stats.GetPartialShare()));
		}
		return Sentence;
	}
}
