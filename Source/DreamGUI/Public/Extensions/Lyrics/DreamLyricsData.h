// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamLyricsData.generated.h"

/** One timed word (or syllable) of a lyric line. Times are seconds from the start of the track. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLyricWord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	FString Text;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	float EndTime = 0.0f;

	float Duration() const { return FMath::Max(EndTime - StartTime, 0.0f); }
};

/** A line: its words in order, the line's own time span, and optional secondary texts. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLyricLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	TArray<FDreamLyricWord> Words;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	float EndTime = 0.0f;
	/** A translation shown under the line, when the source carries one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	FString Translation;
	/** A romanization / pronunciation guide, when the source carries one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	FString Roman;
	/** Sung by the second voice (a duet); players usually right-align these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	bool bSecondary = false;
	/** A backing vocal, shown smaller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	bool bBackground = false;

	/** The words joined as written. */
	FString GetText() const;
	float Duration() const { return FMath::Max(EndTime - StartTime, 0.0f); }
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamLyrics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lyrics")
	TArray<FDreamLyricLine> Lines;

	/** Index of the last line that has started by Time, or INDEX_NONE before the first. */
	int32 FindLineAtTime(float Time) const;
};

UCLASS()
class DREAMGUI_API UDreamLyricsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Parse TTML of the kind Apple Music and AMLL use: <p begin end> lines of <span begin end> words,
	 * with ttm:agent for the second voice, ttm:role="x-bg" spans for backing vocals and
	 * ttm:role="x-translation" / "x-roman" spans for the secondary texts. A <p> without word spans
	 * becomes one word spanning the line.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Lyrics")
	static bool ParseTTML(const FString& Xml, FDreamLyrics& OutLyrics, FString& OutError);

	/**
	 * Parse LRC: "[mm:ss.xx]text" lines, with inline <mm:ss.xx> word times when present (enhanced LRC).
	 * Line ends are the next line's start.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Lyrics")
	static bool ParseLRC(const FString& Text, FDreamLyrics& OutLyrics);

	/** "hh:mm:ss.mmm", "mm:ss.mmm", "ss.mmm" or "123ms"/"1.5s" to seconds; false if unreadable. */
	static bool ParseTime(const FString& Text, float& OutSeconds);
};
