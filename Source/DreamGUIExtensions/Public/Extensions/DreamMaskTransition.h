// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIClipMask.h"
#include "DreamTweener.h"
#include "DreamMaskTransition.generated.h"

DECLARE_DYNAMIC_DELEGATE(FDreamScreenTransitionEvent);

class UDreamTweener;
class UDreamWidget;
class UTexture;
class UTexture2D;

/** The shape a mask transition reveals its widget in. */
UENUM(BlueprintType)
enum class EDreamMaskTransitionPreset : uint8
{
	/** A straight edge sweeping across, from the left (turned by Angle). */
	Wipe,
	/** A hand sweeping clockwise about Centre from twelve o'clock (turned by Angle). */
	Clock,
	/** A circle opening from Centre. */
	Iris,
	/** A diamond opening from Centre. */
	Diamond,
	/** Count stripes, each opening from its left (turned by Angle). */
	Blinds,
	/** A board of squares Count across: one colour of squares wipes in, then the other. */
	Checker,
	/** Specks appearing all over, Grain pixels across. */
	Dissolve,
	/** A blot of ink spreading from Centre, its edge ragged. */
	Ink,
	/** Count shards appearing one by one, the ones near Centre first. */
	Shatter,
	/** The order a texture's channel gives: dark first, light last. */
	Texture,
};

/**
 * The arithmetic of the mask transitions, apart from any widget: the textures the Ink, Shatter and Checker presets read,
 * and the threshold a progress makes. Every texture holds an order, 0 for the first pixels to show to 1 for the last.
 */
class DREAMGUIEXTENSIONS_API FDreamMaskTransitionMath
{
public:
	/** One square of the Checker texture, InCells by InCells: two squares across and down, each InCells / 2 texels. */
	static void MakeCheckerTile(int32 InExtent, TArray<float>& OutOrder);
	/** The Ink order over InWidth by InHeight texels for a widget InAspect times as wide as high: from InCentre (0..1, y down), ragged by noise of InSeed. */
	static void MakeInk(int32 InWidth, int32 InHeight, float InAspect, FVector2f InCentre, int32 InSeed, TArray<float>& OutOrder);
	/** The Shatter order: InShards cells of a jittered grid, each one value, the ones near InCentre lowest. */
	static void MakeShatter(int32 InWidth, int32 InHeight, float InAspect, FVector2f InCentre, int32 InShards, int32 InSeed, TArray<float>& OutOrder);
	/** InValues scaled to run exactly 0 to 1 (all 0 when they are all the same). */
	static void Normalize(TArray<float>& InOutValues);
	/** Smooth value noise in about -0.5..0.5, four octaves, InSeed choosing which. */
	static float Fbm(FVector2f InPoint, uint32 InSeed);

	/**
	 * The threshold that shows (bInHide false) or hides as much of a source running InMin..InMax as InProgress says: at 0
	 * nothing shows (all shows, hiding), at 1 everything (nothing), with InSoftness of the source's units of ramp. The
	 * mask shows where the value passed to it is above the threshold.
	 */
	static float Threshold(float InProgress, float InMin, float InMax, float InSoftness, bool bInHide);
};

/**
 * Reveals or hides a widget and everything under it through a moving mask: a wipe, a clock, an iris, blinds, a checker
 * board, a dissolve, a blot of ink, shards, or a texture's order. Progress runs 0 (hidden) to 1 (shown) -- the other way
 * with Invert -- and can be driven by a tween (PlayForward, PlayBackward), a widget animation's property track, or
 * Blueprint.
 *
 * It drives the widget's clip mask (UDreamWidget::SetDrivenClipMask) without touching the Clip Mask the widget was
 * given, so nothing of it is saved; while it drives one, a widget that did not clip to its bounds does. Fully shown, it
 * lets go of the mask and costs nothing; fully hidden, nothing under it can be hit.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Mask Transition", meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamMaskTransition : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamMaskTransition();

protected:
	/** Off, the widget is shown whole whatever the progress: to compare with and without the transition. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (DreamGUI_LayerToggle))
	bool bEnabled = true;
	/** 0 hidden, 1 shown (with Invert: 0 shown, 1 hidden). */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetProgress, Category = "Transition", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Progress = 1.0f;
	UPROPERTY(EditAnywhere, Category = "Transition")
	EDreamMaskTransitionPreset Preset = EDreamMaskTransitionPreset::Wipe;
	/** How soft the moving edge is, as a share of the whole sweep: 0 is a hard edge. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Softness = 0.08f;
	/** Hides as Progress rises, by the same shape: what was shown first goes first. */
	UPROPERTY(EditAnywhere, Category = "Transition")
	bool bInvert = false;
	/** Runs the shape the other way: a wipe from the right, a clock anticlockwise, an iris closing in from the edge. */
	UPROPERTY(EditAnywhere, Category = "Transition")
	bool bReverse = false;
	/** Where the Clock, Iris, Diamond, Ink and Shatter shapes start, in the widget's rect: 0,0 its top left, 1,1 its bottom right. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (EditCondition = "Preset==EDreamMaskTransitionPreset::Clock||Preset==EDreamMaskTransitionPreset::Iris||Preset==EDreamMaskTransitionPreset::Diamond||Preset==EDreamMaskTransitionPreset::Ink||Preset==EDreamMaskTransitionPreset::Shatter", EditConditionHides))
	FVector2D Centre = FVector2D(0.5, 0.5);
	/** Degrees clockwise the Wipe, Clock and Blinds shapes are turned. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (UIMin = "-360.0", UIMax = "360.0", EditCondition = "Preset==EDreamMaskTransitionPreset::Wipe||Preset==EDreamMaskTransitionPreset::Clock||Preset==EDreamMaskTransitionPreset::Blinds", EditConditionHides))
	float Angle = 0.0f;
	/** Stripes for Blinds, squares across for Checker, shards for Shatter. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ClampMin = "1", ClampMax = "256", EditCondition = "Preset==EDreamMaskTransitionPreset::Blinds||Preset==EDreamMaskTransitionPreset::Checker||Preset==EDreamMaskTransitionPreset::Shatter", EditConditionHides))
	int32 Count = 8;
	/** Pixels across a speck of Dissolve. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ClampMin = "1.0", EditCondition = "Preset==EDreamMaskTransitionPreset::Dissolve", EditConditionHides))
	float Grain = 4.0f;
	/** Which specks, which ink, which shards. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ClampMin = "0", EditCondition = "Preset==EDreamMaskTransitionPreset::Dissolve||Preset==EDreamMaskTransitionPreset::Ink||Preset==EDreamMaskTransitionPreset::Shatter", EditConditionHides))
	int32 Seed = 0;
	/** The order for the Texture preset: where its channel is dark shows first. Stretched over the widget's rect. */
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (EditCondition = "Preset==EDreamMaskTransitionPreset::Texture", EditConditionHides))
	TObjectPtr<UTexture> Texture = nullptr;
	UPROPERTY(EditAnywhere, Category = "Transition", meta = (EditCondition = "Preset==EDreamMaskTransitionPreset::Texture", EditConditionHides))
	EDreamClipMaskChannel Channel = EDreamClipMaskChannel::Red;
	/**
	 * Clicks follow the mask while it moves, where the mask can be measured without a texture (Wipe, Clock, Iris, Diamond,
	 * Blinds, Dissolve); the others hit the whole rect until they are shown. Hidden, nothing is hit either way.
	 */
	UPROPERTY(EditAnywhere, Category = "Transition")
	bool bHitTestMask = true;

	/** The order texture of Ink, Shatter or Checker, made here and held while it is drawn. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> OrderTexture = nullptr;
	/** What OrderTexture was made for; a different one makes it again. */
	uint32 OrderTextureKey = 0;
	/** The mask the widget is drawn with while this drives it. */
	FDreamClipMask DrivenMask;
	bool bDriving = false;
	TWeakObjectPtr<UDreamWidget> DrivenWidget;
	TWeakObjectPtr<UDreamTweener> PlayingTween;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged) override;
	void StopDriving();
	/** The order texture this preset reads, made for the widget's size when it needs one; null for the others. */
	UTexture* PrepareOrderTexture(FVector2f InSize);

public:
	/** Shown: Progress at its showing end. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsFullyShown() const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsFullyHidden() const;
	/** The mask this transition makes for a widget of InSize at Progress: what it drives. */
	bool MakeMask(FVector2f InSize, FDreamClipMask& OutMask);
	/** Measures the mask on the widget again and hands it over: after any change. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Refresh();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool IsEnabled() const { return bEnabled; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetEnabled(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetProgress() const { return Progress; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetProgress(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamMaskTransitionPreset GetPreset() const { return Preset; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPreset(EDreamMaskTransitionPreset Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetSoftness() const { return Softness; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetReverse() const { return bReverse; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") FVector2D GetCentre() const { return Centre; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetAngle() const { return Angle; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetCount() const { return Count; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetGrain() const { return Grain; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetSeed() const { return Seed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetSoftness(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetInvert(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetInvert() const { return bInvert; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetReverse(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetCentre(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetAngle(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetCount(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetGrain(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetSeed(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetTexture(UTexture* Value, EDreamClipMaskChannel InChannel);
	/** The order texture this transition made for itself (Ink, Shatter, Checker), or null. */
	UTexture2D* GetOrderTexture() const { return OrderTexture; }

	/** Progress from where it is to its showing end over InDuration seconds. Returns the tween, to chain on. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (AdvancedDisplay = "InEase"))
	UDreamTweener* PlayForward(float InDuration = 0.5f, EDreamTweenEase InEase = EDreamTweenEase::InOutSine);
	/** Progress from where it is back to 0 over InDuration seconds. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (AdvancedDisplay = "InEase"))
	UDreamTweener* PlayBackward(float InDuration = 0.5f, EDreamTweenEase InEase = EDreamTweenEase::InOutSine);
	/** Progress from InFrom to InTo over InDuration seconds; a tween this transition was playing stops. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (AdvancedDisplay = "InEase"))
	UDreamTweener* PlayFromTo(float InFrom, float InTo, float InDuration = 0.5f, EDreamTweenEase InEase = EDreamTweenEase::InOutSine);
	/** Stops the tween this transition is playing, leaving Progress where it is. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Stop();

	/** The transition on InWidget, added when it has none. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	static UDreamMaskTransition* FindOrAdd(UDreamWidget* InWidget);
	/**
	 * InFrom gives way to InTo through one shape over InDuration seconds: InTo is revealed exactly where InFrom is hidden,
	 * pixel for pixel. Each gets a transition of InPreset if it has none (its other settings kept, so that both match when
	 * set alike); InFrom ends fully hidden, InTo fully shown. Either may be null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (AdvancedDisplay = "InEase"))
	static UDreamTweener* CrossFade(UDreamWidget* InFrom, UDreamWidget* InTo, EDreamMaskTransitionPreset InPreset = EDreamMaskTransitionPreset::Dissolve,
		float InDuration = 0.5f, EDreamTweenEase InEase = EDreamTweenEase::InOutSine);

	/**
	 * A transition of the whole screen: a cover of InColour put on the viewport above the pages, revealed by InPreset over
	 * InCoverSeconds; OnCovered then (load the next level, swap the screen), InHoldSeconds later the cover hides by the
	 * same shape over InUncoverSeconds, and is taken off the screen; OnFinished then. Returns the cover's transition, whose
	 * settings can still be changed while it covers. Null where there is no screen to cover.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "InColour,InSortOrder"))
	static UDreamMaskTransition* PlayScreenTransition(UObject* WorldContextObject, EDreamMaskTransitionPreset InPreset, float InCoverSeconds, float InHoldSeconds, float InUncoverSeconds,
		FDreamScreenTransitionEvent OnCovered, FDreamScreenTransitionEvent OnFinished, FLinearColor InColour = FLinearColor::Black, int32 InSortOrder = 900);
	/** PlayScreenTransition for C++ callers. */
	static UDreamMaskTransition* PlayScreenTransition(UObject* WorldContextObject, EDreamMaskTransitionPreset InPreset, float InCoverSeconds, float InHoldSeconds, float InUncoverSeconds,
		TFunction<void()> OnCovered, TFunction<void()> OnFinished, FLinearColor InColour = FLinearColor::Black, int32 InSortOrder = 900);
};
