// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "DreamTweener.h"
#include "Core/Components/DreamImage.h"
#include "DreamShape.generated.h"

/** What a Shape's points are measured in. */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamShapePointUnit : uint8
{
	/** 0..1 across the widget's rect, (0,0) at the bottom left -- the shape stretches with the rect. */
	Normalized,
	/** Pixels from the rect's bottom-left corner -- the shape keeps its size when the rect changes. */
	Pixels,
};

/**
 * A filled polygon through any points you give it: triangles, slanted bars, arrows, cursor slashes, star bursts.
 * `.dui` tag `Shape`.
 *
 *     Shape Slash {
 *         Points = [(0, 0.2), (1, 1), (0.85, 0.35)]     // normalized, (0,0) bottom left, y up
 *         Color = #FF1E3C
 *     }
 *
 * Polygon draws regular N-gons only (each vertex can move along its own ray, never sideways), so a parallelogram or an
 * arbitrary triangle cannot be made from it. This visual takes the outline directly. The outline is a simple polygon
 * -- convex or concave, either winding, no self-intersections -- and is filled by ear clipping. The brush paints it
 * the way it paints an Image: the texture (or material) is laid over the widget's rect and each point samples where it
 * sits in that rect, so a shape cut from a picture keeps the picture in place.
 *
 * PointsTo morphs the outline into another with the same number of points, which is how a cursor stretches from one
 * item to the next. Hit-testing is the rect's, like every other image-derived visual.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamShape : public UDreamImage
{
	GENERATED_BODY()

public:
	UDreamShape(const FObjectInitializer& ObjectInitializer);

protected:
	/** The outline, in order around the shape. Fewer than three points draws nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter)
	TArray<FVector2D> Points;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter)
	EDreamShapePointUnit PointUnit = EDreamShapePointUnit::Normalized;

	virtual void OnUpdateGeometry(FDreamUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged) override;

public:
	virtual float GetPreferredWidth() const override;
	virtual float GetPreferredHeight() const override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const TArray<FVector2D>& GetPoints() const { return Points; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPoints(const TArray<FVector2D>& InPoints);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamShapePointUnit GetPointUnit() const { return PointUnit; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPointUnit(EDreamShapePointUnit InUnit);

	/**
	 * Morph from the current outline to InPoints. With a different point count there is nothing to pair the points
	 * by, so the outline is replaced at once and no tween is returned.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
	UDreamTweener* PointsTo(const TArray<FVector2D>& InPoints, float Duration = 0.5f, float Delay = 0.0f, EDreamTweenEase EaseType = EDreamTweenEase::OutCubic);

	/**
	 * The triangles that fill InOutline, three indices each, appended to OutIndices. Ear clipping; returns false (and
	 * appends nothing) when the outline has fewer than three points or is not a simple polygon it can fill.
	 */
	static bool Triangulate(TConstArrayView<FVector2D> InOutline, TArray<int32>& OutIndices);

private:
	TArray<FVector2D> MorphFrom;
	TArray<FVector2D> MorphTo;
	float MorphAlpha = 1.0f;
	void SetMorphAlpha(float InAlpha);

	/** The point count the geometry was last sized for; a change means the triangle list must be rebuilt. */
	int32 BuiltPointCount = -1;
	TArray<int32> CachedIndices;
};
