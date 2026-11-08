// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "MeshModifier/DreamMeshModifierBase.h"
#include "Extensions/DreamPath.h"
#include "DreamMeshModifierTextOnPath.generated.h"

class UDreamWidget;

/** What the text runs along. */
UENUM(BlueprintType)
enum class EDreamTextPathSource : uint8
{
	/** A circle or an arc of one, about a point of this widget. */
	Circle,
	/** The path drawn by another widget's Path visual (UDreamPath). */
	Path,
};

/** Where along the path the text sits. */
UENUM(BlueprintType)
enum class EDreamTextPathAlign : uint8
{
	/** Its start at the path's start (plus Start Offset). */
	Start,
	/** Its middle at the path's middle. */
	Centre,
	/** Its end at the path's end. */
	End,
};

/** Which line of the text the path runs through. */
UENUM(BlueprintType)
enum class EDreamTextPathAnchor : uint8
{
	/** The middle of the glyphs, top to bottom. */
	Middle,
	/** The bottom of the glyphs: the letters stand on the path. */
	Bottom,
	/** The top of the glyphs: the letters hang from it. */
	Top,
};

/**
 * Lays a text's glyphs along a path: a circle, an arc, or another widget's Path. Each glyph goes to the point as far
 * along the path as it was along the line, turned to follow the path (or, with Bend Glyphs, bent along it), lifted off
 * the path by Baseline Offset; Align, Start Offset and Letter Spacing place the run, Flip puts it on the path's other
 * side, and Offset Speed scrolls it along -- a marquee.
 *
 * It runs after the modifiers that work on characters, so a text animation's moves are kept: a character it lifts is
 * lifted off the path, one it turns is turned from the path's direction. On anything but a text, the whole mesh is bent
 * along the path.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Text On Path", meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamMeshModifierTextOnPath : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:
	UDreamMeshModifierTextOnPath();

protected:
	UPROPERTY(EditAnywhere, Category = "Text On Path")
	EDreamTextPathSource Source = EDreamTextPathSource::Circle;
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (ClampMin = "0.0", EditCondition = "Source==EDreamTextPathSource::Circle", EditConditionHides))
	float Radius = 100.0f;
	/** The circle's centre, from the widget's centre, in local units (y up). */
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (EditCondition = "Source==EDreamTextPathSource::Circle", EditConditionHides))
	FVector2D CircleCentre = FVector2D(0.0, -100.0);
	/** Degrees clockwise from twelve o'clock the arc starts at. */
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (UIMin = "-360.0", UIMax = "360.0", EditCondition = "Source==EDreamTextPathSource::Circle", EditConditionHides))
	float StartAngle = -90.0f;
	/** Degrees the arc runs; 360 is the whole circle, which the text then wraps around. */
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (ClampMin = "0.0", ClampMax = "360.0", EditCondition = "Source==EDreamTextPathSource::Circle", EditConditionHides))
	float SweepAngle = 180.0f;
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (EditCondition = "Source==EDreamTextPathSource::Circle", EditConditionHides))
	bool bClockwise = true;
	/** The widget whose visual is the Path to follow. */
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (EditCondition = "Source==EDreamTextPathSource::Path", EditConditionHides))
	TWeakObjectPtr<UDreamWidget> PathWidget;

	UPROPERTY(EditAnywhere, Category = "Text On Path")
	EDreamTextPathAlign Align = EDreamTextPathAlign::Centre;
	/** Moves the run along the path, in local units. Animate it, or let Offset Speed move it. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStartOffset, Category = "Text On Path")
	float StartOffset = 0.0f;
	/** Local units a second Start Offset moves by while the game runs; on a closed path the text goes round and round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text On Path")
	float OffsetSpeed = 0.0f;
	/** Space added between one glyph and the next, in local units. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetLetterSpacing, Category = "Text On Path")
	float LetterSpacing = 0.0f;
	/** How far off the path the anchor line sits, in local units, outward from the path's left side. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetBaselineOffset, Category = "Text On Path")
	float BaselineOffset = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Text On Path")
	EDreamTextPathAnchor Anchor = EDreamTextPathAnchor::Bottom;
	/** Runs the text the other way along the path, on its other side. */
	UPROPERTY(EditAnywhere, Category = "Text On Path")
	bool bFlip = false;
	/** Bends each glyph along the path, rather than turning it whole to the path's direction at its middle. */
	UPROPERTY(EditAnywhere, Category = "Text On Path")
	bool bBendGlyphs = false;
	/** On an open path, hides the glyphs whose middle is past either end, instead of carrying them on straight. */
	UPROPERTY(EditAnywhere, Category = "Text On Path")
	bool bHideOutside = false;
	/** The widest a bent glyph's columns may be, in local units. */
	UPROPERTY(EditAnywhere, Category = "Text On Path", meta = (ClampMin = "1.0", EditCondition = "bBendGlyphs", EditConditionHides))
	float CellLength = 4.0f;

	/**
	 * What is listened to while following a path widget: the path's shape, and where the path widget and this widget are --
	 * either moving moves the path in this widget's space.
	 */
	TWeakObjectPtr<UDreamPath> ListenedPath;
	FDelegateHandle PathChangedHandle;
	TWeakObjectPtr<UDreamWidget> ListenedPathWidget;
	FDelegateHandle PathMovedHandle;
	TWeakObjectPtr<UDreamWidget> ListenedOwnWidget;
	FDelegateHandle OwnMovedHandle;
	void StopListening();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;
	void ListenToPath();
	void UpdateTicking();
	void MarkDirty(bool bInAll);
	/** The path widget's local space to this widget's local space. */
	bool GetPathToLocal(FMatrix& OutMatrix) const;

public:
	virtual EDreamMeshModifierStage GetStage() const override { return EDreamMeshModifierStage::Layout; }
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor) override;
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged) override;

	/** The path as this widget's local space has it now; false when there is none to follow. */
	bool BuildPath(FDreamPathCurve& OutCurve) const;
	/** The path to follow, the Path visual of Path Widget. */
	UDreamPath* GetPath() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetSource(EDreamTextPathSource Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetCircle(float InRadius, FVector2D InCentre, float InStartAngle, float InSweepAngle, bool bInClockwise);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPathWidget(UDreamWidget* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetAlign(EDreamTextPathAlign Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetStartOffset(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetOffsetSpeed(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetLetterSpacing(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetBaselineOffset(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetAnchor(EDreamTextPathAnchor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFlip(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetBendGlyphs(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetHideOutside(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetStartOffset() const { return StartOffset; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetLetterSpacing() const { return LetterSpacing; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetBaselineOffset() const { return BaselineOffset; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") UDreamWidget* GetPathWidget() const { return PathWidget.Get(); }
};
