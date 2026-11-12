// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// What a preview shows, measured on the 8-bit pixels of its PNG, so a model that cannot look at the picture still learns
// whether the screen drew anything, how much of the viewport it covers, what colour it mostly is, and how bright.
//
// Which pixels the screen covers is not guessed from the picture: the same frame is drawn again over black and over
// white. A pixel that is the same over both is covered by the screen; one that follows the backdrop is not; one in
// between is covered by something partly transparent. The thresholds are part of the endpoint's contract
// (Docs/tools/dreammcp.md lists them).

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace DreamGUIMCP
{
	struct FPreviewStatsThresholds
	{
		/** Over black and over white within this on every channel (0-255): covered by the screen. */
		static constexpr int32 CoveredTolerance = 8;
		/** At least this far apart on some channel: the backdrop shows through untouched. */
		static constexpr int32 UncoveredDifference = 247;
		static constexpr int32 NearWhite = 240;
		static constexpr int32 NearBlack = 12;
		/** A luminance standard deviation at or under this, with every channel as steady, is one flat colour. */
		static constexpr double FlatStdDev = 0.012;
		/** Frames whose mean per-pixel difference stays at or under this (0-255) are the same picture... */
		static constexpr double SameFrameDifference = 0.5;
		/**
		 * ...unless this share of their pixels or more changed, each by ChangedPixelDifference or more on some channel: a few
		 * sparks moving over a still screen barely move the mean.
		 */
		static constexpr double ChangedShare = 0.0002;
		static constexpr int32 ChangedPixelDifference = 32;
		/** Under this share of the viewport covered, the screen draws next to nothing. */
		static constexpr double NearlyEmpty = 0.005;
	};

	/** Sums over the pixels of one or more frames. */
	struct DREAMGUIEDITOR_API FPreviewStats
	{
		int64 Pixels = 0;
		/** Covered by the screen, opaque or partly. */
		int64 CoveredPixels = 0;
		int64 PartialPixels = 0;
		int64 NearWhitePixels = 0;
		int64 NearBlackPixels = 0;
		double Sum[3] = { 0.0, 0.0, 0.0 };
		double SumSquares[3] = { 0.0, 0.0, 0.0 };
		double LumaSum = 0.0;
		double LumaSumSquares = 0.0;
		double LumaMin = 1.0;
		double LumaMax = 0.0;

		/** One frame: the picture as shown, and the same frame over black and over white. All three the same size. */
		void Add(TConstArrayView<FColor> Picture, TConstArrayView<FColor> OverBlack, TConstArrayView<FColor> OverWhite);

		/** 0-1 sRGB-encoded mean of the covered pixels, as the picture shows them. */
		FLinearColor GetMeanColor() const;
		double GetLumaMean() const;
		double GetLumaStdDev() const;
		double GetChannelStdDev() const;
		/** Share of the viewport the screen covers, 0-1. */
		double GetCoverage() const;
		/** Share of what it covers that is partly transparent, 0-1. */
		double GetPartialShare() const;
		double GetNearWhiteShare() const;
		double GetNearBlackShare() const;
		bool IsFlat() const;

		TSharedRef<FJsonObject> ToJson() const;
	};

	/** "#RRGGBB" of a 0-1 sRGB-encoded colour. */
	DREAMGUIEDITOR_API FString FormatHexColor(const FLinearColor& Color);

	/**
	 * A plain name for a 0-1 sRGB-encoded colour, by its value and saturation before its hue: a grey under saturation 0.12,
	 * a grey with a tint under 0.4 ("pinkish grey", "dark bluish grey"), a hue above ("orange", "dark blue", "pale green"),
	 * dark orange and yellow named brown.
	 */
	DREAMGUIEDITOR_API FString NameColor(const FLinearColor& Color);

	/** The mean absolute difference between two frames of the same size, per channel, 0-255. */
	DREAMGUIEDITOR_API double MeanFrameDifference(TConstArrayView<FColor> A, TConstArrayView<FColor> B);

	/** The share of pixels that differ between two frames of the same size by ChangedPixelDifference or more on some channel. */
	DREAMGUIEDITOR_API double ChangedPixelShare(TConstArrayView<FColor> A, TConstArrayView<FColor> B);

	/** What a strip of frames does over its time. */
	struct FPreviewAnimation
	{
		int32 Frames = 0;
		double MaxDifference = 0.0;
		/** The largest share of pixels changed between the first frame and another. */
		double MaxChangedShare = 0.0;
		/** Over every three consecutive frames, the change first to third against the change through the middle: 1 a steady drift, 0 a jump there and back. */
		double Straightness = 1.0;
		bool bChanges = false;
		bool bUndersampled = false;
		int32 Darkest = 0;
		int32 Brightest = 0;
		static constexpr double UndersampledStraightness = 0.45;
	};

	DREAMGUIEDITOR_API FPreviewAnimation AnalysePreviewAnimation(TConstArrayView<TArray<FColor>> Frames, TConstArrayView<FPreviewStats> PerFrame);

	/** The sentence a strip's summary leads with, without its full stop; OutNext gets what to do about it. */
	DREAMGUIEDITOR_API FString DescribePreviewAnimation(const FPreviewAnimation& Animation, TConstArrayView<FPreviewStats> PerFrame, TConstArrayView<double> Times,
		double Duration, TArray<FString>& OutNext);

	DREAMGUIEDITOR_API TSharedRef<FJsonObject> PreviewAnimationToJson(const FPreviewAnimation& Animation, TConstArrayView<FPreviewStats> PerFrame,
		TConstArrayView<double> Times, double Duration);

	/** The one fact about a picture a model should know first, as a sentence without its full stop, and what to do in OutNext. */
	DREAMGUIEDITOR_API FString DescribePreview(const FPreviewStats& Stats, TArray<FString>& OutNext);

	/**
	 * The sentence a strip of frames leads with: Overall (the frames' sums together) said as DescribePreview says one
	 * picture, with how far the coverage ranges over the frames -- never "draws almost nothing" when some frame draws.
	 */
	DREAMGUIEDITOR_API FString DescribePreviewFrames(const FPreviewStats& Overall, TConstArrayView<FPreviewStats> PerFrame, TArray<FString>& OutNext);
}
