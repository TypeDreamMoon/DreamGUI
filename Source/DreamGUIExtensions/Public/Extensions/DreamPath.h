// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Extensions/2DLineRenderer/Dream2DLineRendererBase.h"
#include "DreamPath.generated.h"

/** How a path runs through its points. */
UENUM(BlueprintType)
enum class EDreamPathCurve : uint8
{
	/** Straight from each point to the next. */
	Linear,
	/** A smooth curve through every point (Catmull-Rom). */
	Smooth,
};

/**
 * A path in a plane, measured along its length: a polyline sampled from points, or a circle's arc kept exact. Distances
 * run from its start; one past either end of an open path goes on along the end's direction, and a closed path wraps.
 * Points are in the plane of the widget it belongs to: x to the right, y up.
 */
struct DREAMGUIEXTENSIONS_API FDreamPathCurve
{
	/** The polyline, without the start repeated at the end when closed. Empty for a circle. */
	TArray<FVector2f> Points;
	/** Distance along the path to each point. For a closed polyline one more entry: the whole length, back at the start. */
	TArray<float> Distances;
	bool bClosed = false;

	bool bCircle = false;
	FVector2f Centre = FVector2f::ZeroVector;
	float Radius = 0.0f;
	/** Degrees clockwise from twelve o'clock the arc starts at. */
	float StartDegrees = 0.0f;
	/** Degrees the arc runs, 360 for the whole circle (which is then closed). */
	float SweepDegrees = 360.0f;
	bool bClockwise = true;

	/** Through InControls (two at least) as InCurve says, InSegmentsPerSpan pieces between two of them when smooth. */
	void Build(TConstArrayView<FVector2D> InControls, EDreamPathCurve InCurve, int32 InSegmentsPerSpan, bool bInClosed);
	/** Straight through InPoints, measured afresh: what Build makes from its samples, and what a moved copy of a path takes. */
	void SetPolyline(TArray<FVector2f> InPoints, bool bInClosed);
	/** An arc of the circle about InCentre of InRadius, from InStartDegrees (clockwise from twelve o'clock) for InSweepDegrees. */
	void BuildCircle(FVector2f InCentre, float InRadius, float InStartDegrees, float InSweepDegrees, bool bInClockwise);
	void Reset();

	float GetLength() const;
	bool IsValid() const { return GetLength() > 0.0f; }
	/** The point InDistance along, and the direction the path runs there (unit length). */
	void Evaluate(float InDistance, FVector2f& OutPoint, FVector2f& OutDirection) const;
	/** InCount points evenly along the whole path, both ends included when open. */
	void Sample(int32 InCount, TArray<FVector2D>& OutPoints) const;
};

/**
 * A path drawn as a line: points that can be moved in the designer, joined straight or by a smooth curve, open or closed.
 * It is drawn with the line renderer's brush and width; with Draw Line off it draws nothing and is only a path, for text
 * to run along (UDreamMeshModifierTextOnPath) or for anything else to measure (GetPointAtDistance).
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Path", meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamPath : public UDream2DLineRendererBase
{
	GENERATED_BODY()

public:
	UDreamPath(const FObjectInitializer& ObjectInitializer);

	/** Something about the path's shape changed: its points, its curve, whether it is closed. */
	DECLARE_MULTICAST_DELEGATE(FOnPathChanged);
	FOnPathChanged OnPathChanged;

protected:
	/** The points the path runs through, in the widget's local space. */
	UPROPERTY(EditAnywhere, Category = "Path")
	TArray<FVector2D> Points = { FVector2D(-100.0, -20.0), FVector2D(0.0, 40.0), FVector2D(100.0, -20.0) };
	UPROPERTY(EditAnywhere, Category = "Path")
	EDreamPathCurve Curve = EDreamPathCurve::Smooth;
	/** Straight pieces between two points of a smooth path. */
	UPROPERTY(EditAnywhere, Category = "Path", meta = (ClampMin = "1", ClampMax = "64", EditCondition = "Curve==EDreamPathCurve::Smooth", EditConditionHides))
	int32 SegmentsPerSpan = 12;
	/** The last point joins the first. The line then joins its ends too. */
	UPROPERTY(EditAnywhere, Category = "Path")
	bool bClosed = false;
	/** Draws the line; off, the path is there to be followed and nothing is drawn. */
	UPROPERTY(EditAnywhere, Category = "Path")
	bool bDrawLine = true;

	FDreamPathCurve CachedCurve;
	TArray<FVector2D> DrawnPoints;
	bool bCurveDirty = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void CalculatePoints() override;
	virtual const TArray<FVector2D>& GetCalcaultedPointArray() override;
	void MarkPathChanged();

public:
	/** The path as measured, made afresh after any change. */
	const FDreamPathCurve& GetCurve();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const TArray<FVector2D>& GetPoints() const { return Points; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamPathCurve GetCurveType() const { return Curve; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetSegmentsPerSpan() const { return SegmentsPerSpan; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetClosed() const { return bClosed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") bool GetDrawLine() const { return bDrawLine; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPoints(const TArray<FVector2D>& Value);
	/** One point, in local space; ignored past the end. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPoint(int32 InIndex, FVector2D InPoint);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetCurveType(EDreamPathCurve Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetSegmentsPerSpan(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetClosed(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetDrawLine(bool Value);

	/** The path's length in local units. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetLength();
	/** The point InDistance along the path, in local space, and the direction it runs there. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void GetPointAtDistance(float InDistance, FVector2D& OutPoint, FVector2D& OutDirection);
};
