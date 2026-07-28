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
	 * Alpha. Note that on a 10-bit backbuffer alpha carries two bits, so this collapses to four
	 * values and the sort becomes close to a no-op -- it is only useful on an 8-bit or float target.
	 */
	Alpha,
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
	 * One compare-exchange phase over a line of keys, in place, for the test harness and as the
	 * readable statement of what the shader does per pass.
	 *
	 * InPhase alternates which element of each pair is the anchor; without alternating parity the
	 * array stalls half-sorted.
	 */
	LGUI_API void ApplyPhase(TArray<float>& InOutKeys, int32 InPhase, const FVector2f& InBand, bool bInDescending);

	/**
	 * The index one texel gathers from, decided WITHOUT seeing what any other texel decided.
	 *
	 * This is the shader's formulation, and it exists here so the correspondence can be asserted
	 * rather than merely intended. A pixel shader has no way to swap two texels: each invocation can
	 * only choose which source texel to read. The pair therefore gets TWO independent decisions, and
	 * they must agree -- if the two sides disagree, one texel is duplicated and its partner erased,
	 * which on a flat background (all ties) is every pair, every pass.
	 *
	 * Agreement is guaranteed here by both sides asking the same question about the same ORDERED
	 * pair rather than about "me and my partner". Writing it the second way is the natural thing to
	 * do and it is the bug. The test pins that running this over a whole line reproduces ApplyPhase
	 * exactly, so a shader written from this function is checkable against the sort.
	 */
	LGUI_API int32 GatherIndex(const TArray<float>& InKeys, int32 InIndex, int32 InPhase, const FVector2f& InBand, bool bInDescending);
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
	 * The ceiling on travel, in passes -- and one pass is one texel of movement, measured in the
	 * widget's own UI units rather than screen pixels. Each pass is a full read and write of the
	 * region, so this is the effect's cost dial as much as its quality dial.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "1", ClampMax = "128"))
		int32 MaxSortPasses = 32;
	/** Pixels whose key is below this are left alone and act as run boundaries. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float ThresholdMin = 0.25f;
	/** Pixels whose key is above this are left alone and act as run boundaries. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
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
