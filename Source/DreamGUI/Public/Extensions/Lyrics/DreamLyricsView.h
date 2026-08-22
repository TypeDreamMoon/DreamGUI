// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUITextData.h"
#include "DreamSpring.h"
#include "Extensions/Lyrics/DreamLyricsData.h"
#include "DreamLyricsView.generated.h"

class UDreamWidget;
class UDreamText;
class UDreamUIFontData_BaseObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamLyricsLineChangedEvent, int32, LineIndex);

/**
 * An Apple Music-style lyric view: one text line per lyric line, the current line held at a fixed
 * height while the others glide above and below it on springs; words light up as they are sung
 * through the text's fill channels; lines further from the current one soften and fade.
 *
 * Put it on a widget that is the view's frame (its width wraps the lines, its height is the visible
 * area). Give it lyrics with SetLyrics and drive it with SetTime from your audio clock -- or
 * Play() to let it run on its own for previews.
 */
UCLASS(ClassGroup = (DreamGUI), meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamLyricsView : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamLyricsView();

	// ---- content
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lyrics")
	FDreamLyrics Lyrics;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text")
	TObjectPtr<UDreamUIFontData_BaseObject> Font = nullptr;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text", meta = (ClampMin = "1"))
	float FontSize = 36.0f;
	/** Size of backing-vocal lines relative to FontSize. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text", meta = (ClampMin = "0.1", ClampMax = "1"))
	float BackgroundLineScale = 0.7f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text")
	FColor TextColor = FColor::White;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text")
	FDreamTextStyle TextStyle;
	/** Extra glow (as a fraction of the style's glow width) for a word while it is being sung long enough to matter. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text", meta = (ClampMin = "0"))
	float EmphasisGlowBoost = 1.0f;
	/** Words at least this long (seconds) get the emphasis glow as they are sung. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text", meta = (ClampMin = "0"))
	float EmphasisMinDuration = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text")
	bool bShowTranslation = true;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Text", meta = (ClampMin = "0.1", ClampMax = "1"))
	float TranslationScale = 0.6f;

	// ---- layout
	/** Where the current line's top sits, as a fraction of the view's height from the top. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Layout", meta = (ClampMin = "0", ClampMax = "1"))
	float ActiveLineAnchor = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Layout", meta = (ClampMin = "0"))
	float LineSpacing = 24.0f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Layout", meta = (ClampMin = "0"))
	float HorizontalPadding = 16.0f;

	// ---- motion
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion")
	FDreamSpringParams PositionSpring = FDreamSpringParams(1.0f, 120.0f, 18.0f);
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion")
	FDreamSpringParams ScaleSpring = FDreamSpringParams(1.0f, 150.0f, 20.0f);
	/** Lines start moving this many seconds after the line before them (further lines lag more). */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0"))
	float StaggerSeconds = 0.04f;
	/** How many lines away the stagger keeps growing; beyond that every line moves together. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0"))
	int32 StaggerMaxLines = 8;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0.1", ClampMax = "1"))
	float InactiveScale = 0.92f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0", ClampMax = "1"))
	float InactiveAlpha = 0.55f;
	/** Alpha of lines already sung. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0", ClampMax = "1"))
	float SungAlpha = 0.35f;
	/** Face softness (em) added per line of distance from the current line; 0 turns the blur off. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0"))
	float BlurPerLine = 0.04f;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0"))
	int32 BlurMaxLines = 4;
	/** Seconds for alpha changes to mostly settle. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Motion", meta = (ClampMin = "0.01"))
	float AlphaSmoothing = 0.25f;

	// ---- playback
	/** Advance the clock every tick; off when an external player drives SetTime. */
	UPROPERTY(EditAnywhere, Category = "Lyrics|Playback")
	bool bPlaying = false;
	UPROPERTY(EditAnywhere, Category = "Lyrics|Playback", meta = (ClampMin = "0"))
	float PlaybackRate = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "Lyrics")
	FDreamLyricsLineChangedEvent OnActiveLineChanged;

	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void SetLyrics(const FDreamLyrics& InLyrics);
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	bool LoadTTML(const FString& Xml);
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	bool LoadLRC(const FString& Text);
	/** The track position in seconds; call every frame from the audio clock. */
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void SetTime(float Seconds);
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	float GetTime() const { return CurrentTime; }
	/** Jump without animating the lines into place. */
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void Seek(float Seconds);
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void Play() { bPlaying = true; }
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void Pause() { bPlaying = false; }
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	int32 GetActiveLineIndex() const { return ActiveLine; }
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	UDreamText* GetLineText(int32 LineIndex) const;
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	int32 GetLineCount() const { return Lines.Num(); }

	/** Rebuild the line widgets from Lyrics; called by SetLyrics and on Awake. */
	UFUNCTION(BlueprintCallable, Category = "Lyrics")
	void RebuildLines();

protected:
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnDestroy() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	struct FLineState
	{
		TWeakObjectPtr<UDreamWidget> Widget;
		TWeakObjectPtr<UDreamText> Text;
		TWeakObjectPtr<UDreamWidget> TranslationWidget;
		TWeakObjectPtr<UDreamText> TranslationText;
		/** Element (code point) index where each word starts, plus one past the last. */
		TArray<int32> WordElementStarts;
		FDreamSpringState PositionY;
		FDreamSpringState Scale;
		float Alpha = 1.0f;
		float Height = 0.0f;
		/** A retarget waiting out its stagger delay. */
		bool bHasPendingTarget = false;
		float PendingTargetY = 0.0f;
		float PendingDelay = 0.0f;
		int32 LastSoftnessLines = -1;
		float LastFillProgress = -1.0f;
		bool bHadSegments = false;
	};
	TArray<FLineState> Lines;
	float CurrentTime = 0.0f;
	int32 ActiveLine = INDEX_NONE;
	bool bLinesDirty = true;
	bool bSnapNextLayout = true;

	void DestroyLines();
	void ApplyTextSettings(UDreamText* Text, bool bBackground, bool bSecondary, bool bTranslation) const;
	void UpdateLineHeights();
	void UpdateTargets(bool bSnap);
	void UpdateFill(int32 LineIndex);
	void ApplyLineTransform(FLineState& Line);
	static int32 CountElements(const FString& Text);
};
