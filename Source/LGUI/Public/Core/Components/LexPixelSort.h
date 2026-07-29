// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "LexVisualPostProcess.h"
#include "LexPixelSort.generated.h"

/** Which way runs are sorted. The axis is the WIDGET's, so a rotated widget rotates the effect. */
UENUM(BlueprintType)
enum class ELexPixelSortAxis : uint8
{
	Horizontal,
	Vertical,
};

/**
 * How runs are carved out of a line -- which is a SEPARATE question from what orders the pixels
 * inside them, and the one that decides most of the character of the result.
 *
 * Threshold follows the picture: runs end where the image stops being mid-tone, so the effect
 * drapes itself over whatever is behind. Random and Waves ignore the picture entirely and cut on a
 * spatial rule, which is where the regular banded look comes from. They are not variations on each
 * other; they produce recognisably different images from the same source.
 */
UENUM(BlueprintType)
enum class ELexPixelSortInterval : uint8
{
	/** Runs end where a pixel's key leaves the threshold band. Content-driven. */
	Threshold,
	/** Runs of pseudo-random length, averaging IntervalLength. Ignores the image. */
	Random,
	/** Runs of near-uniform length, IntervalLength, lightly jittered per line. Ignores the image. */
	Waves,
	/** No runs -- each whole line sorts end to end. */
	None,
};

/** What decides a pixel's place in the sort, and which pixels take part. */
UENUM(BlueprintType)
enum class ELexPixelSortKey : uint8
{
	/** Rec.709 luminance. The usual choice, and the one the threshold defaults are tuned for. */
	Luminance,
	/** Largest of R/G/B. Picks out highlights harder than luminance does. */
	Brightness,
	/** max-min over max. Sorts by colourfulness, leaving greys alone. */
	Saturation,
	/**
	 * Hue, 0..1 around the wheel. Sorts by colour rather than by tone, which gives the rainbow
	 * banding the effect is known for. Note it WRAPS: red is at both ends, so a run spanning red
	 * splits rather than sorting smoothly. That is inherent to sorting an angle, not a defect.
	 */
	Hue,
	/** R+G+B over three. Flatter than luminance -- it does not privilege green. */
	Intensity,
	/** Smallest of R/G/B. Picks out how close a pixel is to a pure hue. */
	Minimum,
	/**
	 * Alpha. Note that on a 10-bit backbuffer alpha carries two bits, so this collapses to four
	 * values and the sort becomes close to a no-op -- it is only useful on an 8-bit or float target.
	 */
	Alpha,
};

/**
 * Everything that decides whether a given texel takes part in the sort.
 *
 * Grouped because the answer depends on the interval mode, and passing four loose parameters through
 * every function was how the band ended up being the only rule in the first place.
 */
struct LGUI_API FLexPixelSortRunRules
{
	ELexPixelSortInterval Interval = ELexPixelSortInterval::Threshold;
	/** Threshold mode only. Ordered low-then-high by ResolveBand. */
	FVector2f Band = FVector2f(0.25f, 0.8f);
	/** Random and Waves: the average and the exact run length respectively, in texels. */
	int32 IntervalLength = 32;
	/** Fraction of runs left unsorted, 0..1. Breaks up the regularity of Waves in particular. */
	float Randomness = 0.0f;
	/** Which line of the image this is, so runs and randomness differ from row to row. */
	int32 LineIndex = 0;
};

/**
 * The sort's arithmetic, as free functions, so it can be tested.
 *
 * Everything here is mirrored by LexUIPostProcessPixelSort.usf and the two must agree. They cannot be
 * shared -- one is HLSL -- so the risk is that they drift, and the mitigation is that this half is
 * pinned by tests and both halves carry a comment pointing at the other. Anything added on one side
 * belongs on both.
 */
namespace LexPixelSort
{
	/** A threshold band, always ordered low-then-high whatever order it was typed in. */
	LGUI_API FVector2f ResolveBand(float InFirst, float InSecond);

	/**
	 * How many compare-exchange phases a strength maps to.
	 *
	 * Bounded on purpose, and the bound is the feature: after P phases a pixel has moved at most P
	 * places, so P is exactly the "how far does the smear reach" control a glitch effect wants, and
	 * it falls off to nothing smoothly. It is also the only thing standing between a Blueprint
	 * setting strength to 20 and several hundred render passes in one frame.
	 */
	LGUI_API int32 ResolvePassCount(float InStrength, int32 InMaxPasses);

	/** The sort key for one colour. Mirrored by ComputeKey in the shader. */
	LGUI_API float ComputeKey(const FLinearColor& InColor, ELexPixelSortKey InKey);

	/** Whether a key takes part in the sort at all. Out-of-band pixels are the walls between runs. */
	LGUI_API bool IsInBand(float InKey, const FVector2f& InBand);

	/** A stable 32-bit mix, so runs and randomness are identical on the CPU and in the shader. */
	LGUI_API uint32 Hash(uint32 InValue);

	/**
	 * Whether the texel at InIndex takes part, under whatever rule the interval mode implies.
	 *
	 * Threshold asks the key. Random and Waves ask only the position, so they carve the same runs
	 * out of any image. None lets everything through. Randomness then drops whole runs on top of
	 * that -- computed from the run's own index, so a run is either sorted or not, rather than
	 * dissolving into per-pixel noise.
	 */
	LGUI_API bool IsSortable(float InKey, int32 InIndex, const FLexPixelSortRunRules& InRules);

	/**
	 * The comparator, as ONE expression used by both sides of every pair.
	 *
	 * This being a single function is not tidiness. A pair is evaluated independently by two
	 * invocations, and if they disagree -- which is what writing `a < b` on one side and `a > b` on
	 * the other produces on a tie -- then one texel is duplicated and its partner erased. Flat UI
	 * backgrounds are nothing but ties, so that bug is not rare, it is the common case.
	 */
	LGUI_API bool ShouldExchange(float InLowerKey, float InUpperKey, bool bInDescending);

	/**
	 * Where the texel at InIndex ends up: the head of its run, plus how many of its neighbours
	 * belong before it. Mirrors RankPS.
	 *
	 * This replaced an odd-even transposition that needed one pass per texel of travel. Counting the
	 * rank directly is two passes for an exact result instead of many for an approximate one.
	 *
	 * The scan is bounded by InSearchRadius, which is what makes the cost predictable and is also
	 * the effect's reach control -- a texel cannot move further than it can see.
	 *
	 * THE TIE-BREAK IS THE WHOLE TRICK, and it is one character on each side: walking backward a
	 * neighbour counts when its key is <= this one, walking forward only when it is < . That
	 * asymmetry orders equal-valued texels by position, so no two can compute the same destination.
	 * Make both comparisons the same and every tie collides -- one destination claimed twice and
	 * another never at all, which on flat colour is most of the image.
	 */
	LGUI_API int32 ComputeDestination(const TArray<float>& InKeys, int32 InIndex,
		const FLexPixelSortRunRules& InRules, bool bInDescending, int32 InSearchRadius);

	LGUI_API FIntPoint ResolveRegionSize(bool bInUseFullSize, const FVector2f& InRectSize, const FIntPoint& InScreenSize);
}

/**
 * UI element that pixel-sorts whatever is behind it: within each row or column, runs of pixels whose
 * key falls inside a threshold band are sorted, and pixels outside the band stay put and act as the
 * walls between runs.
 *
 * Use it in ScreenSpace or WorldSpace-LexUIRenderer.
 * If android OpenGL ES3.1, need to enable "ProjectSettings/Platforms/Android/Build/Support Backbuffer
 * Sampling on OpenGL".
 *
 * Cost is linear in SortStrength x area and is bandwidth-bound: each phase reads and writes every
 * texel of the widget's rect. It is comfortable on desktop and wants a low strength on mobile.
 */
UCLASS(ClassGroup = (LGUI), NotBlueprintable)
class LGUI_API ULexPixelSort : public ULexVisualPostProcess
{
	GENERATED_BODY()

public:
	ULexPixelSort(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Which way the runs lie. This is the WIDGET's axis, so rotating the widget rotates the sort. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexPixelSortAxis SortAxis = ELexPixelSortAxis::Vertical;
	/**
	 * How runs are carved out of each line. This changes the character of the result more than any
	 * other setting -- Threshold drapes the effect over the image, Random and Waves impose a pattern
	 * on it regardless of what is behind.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexPixelSortInterval IntervalMode = ELexPixelSortInterval::Threshold;
	/** Run length in texels for Random (average) and Waves (exact). Unused by the other modes. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "2", ClampMax = "512",
		EditCondition = "IntervalMode == ELexPixelSortInterval::Random || IntervalMode == ELexPixelSortInterval::Waves"))
		int32 IntervalLength = 32;
	/** Fraction of runs left untouched. Mostly used to break up how regular Waves looks. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float Randomness = 0.0f;
	/** What orders the pixels, and what the threshold band is measured against. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexPixelSortKey SortKey = ELexPixelSortKey::Luminance;
	/**
	 * How far pixels are allowed to travel, as a fraction of MaxSortPasses. 0 disables the effect
	 * entirely -- and note that disables the whole visual, so the widget shows nothing rather than
	 * showing the untouched background.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float SortStrength = 0.5f;
	/**
	 * The ceiling on how far a pixel may travel, in texels of the widget's own UI units rather than
	 * screen pixels. It is the effect's cost dial as much as its reach dial: each pixel scans this
	 * far in both directions, so cost is linear in it.
	 *
	 * KEEP THIS AT OR ABOVE THE RUN LENGTH YOU ARE SORTING, or the result is speckle rather than a
	 * milder version of the effect. A pixel that cannot scan to the start of its own run disagrees
	 * with its neighbours about where the run begins, their destinations collide, and the pixels
	 * nobody claims are left where they are. Hence the ceiling of 512: it matches IntervalLength's,
	 * so Random and Waves runs can always be covered. Threshold runs follow the image and can be
	 * longer than any clamp, which is the one case where this still has to be judged by eye.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "1", ClampMax = "512"))
		int32 MaxSortPasses = 32;
	/**
	 * Pixels whose key is below this are left alone and act as run boundaries. The default of 0.25
	 * with an upper of 0.8 is the pair the reference implementations settled on.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0",
		EditCondition = "IntervalMode == ELexPixelSortInterval::Threshold"))
		float ThresholdMin = 0.25f;
	/** Pixels whose key is above this are left alone and act as run boundaries. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0",
		EditCondition = "IntervalMode == ELexPixelSortInterval::Threshold"))
		float ThresholdMax = 0.8f;
	/** Sort bright-to-dark instead of dark-to-bright. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bDescending = false;

public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ELexPixelSortAxis GetSortAxis()const { return SortAxis; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ELexPixelSortKey GetSortKey()const { return SortKey; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ELexPixelSortInterval GetIntervalMode()const { return IntervalMode; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		int32 GetIntervalLength()const { return IntervalLength; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetRandomness()const { return Randomness; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetSortStrength()const { return SortStrength; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		int32 GetMaxSortPasses()const { return MaxSortPasses; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetThresholdMin()const { return ThresholdMin; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetThresholdMax()const { return ThresholdMax; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool GetDescending()const { return bDescending; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSortAxis(ELexPixelSortAxis Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSortKey(ELexPixelSortKey Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetIntervalMode(ELexPixelSortInterval Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetIntervalLength(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRandomness(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetSortStrength(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetMaxSortPasses(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetThresholdMin(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetThresholdMax(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetDescending(bool Value);

	virtual FLexVisualPostProcessRenderProxy* GetRenderProxy()override;
	virtual void MarkAllDirty()override;

protected:
	virtual void SendRegionVertexDataToRenderProxy()override;
	void SendOthersDataToRenderProxy();
};
