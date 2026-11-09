// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshDeform.h"
#include "DreamMeshModifierWarp.generated.h"

/** How a warp bends its box. */
UENUM(BlueprintType)
enum class EDreamWarpMode : uint8
{
	/** One of the named styles, by Bend and the two distortions. */
	Style,
	/** A Bezier lattice whose control points are moved by hand (or by the designer's handles). */
	Lattice,
};

/**
 * Bends what a visual draws: an image, a rect block, a text, a line -- any batch mesh. The mesh is first cut finer, the
 * pieces no longer than Cell Length, then every vertex is moved by a named warp style or a Bezier lattice. Every texture
 * coordinate is carried along, so a rect block's corners, a glyph's distance field and an image's pixels bend with the
 * mesh; clipping and masks are measured where the pixels end up, and a visual set to hit test by its mesh (Raycast Type
 * Mesh or Visible Pixel) is hit where the bent triangles are.
 *
 * On a text, every glyph is cut by itself: the style names and their Bend and distortions are those of the image editors'
 * text warp, so a document's warp maps here by name. It runs after the modifiers that work on characters (a text
 * animation, text on a path) and before those that copy the mesh (a shadow follows the bent text).
 *
 * At rest -- no bend, no distortion, an unmoved lattice -- it changes nothing and costs nothing.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Warp", meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamMeshModifierWarp : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:
	UDreamMeshModifierWarp();

protected:
	UPROPERTY(EditAnywhere, Category = "Warp")
	EDreamWarpMode Mode = EDreamWarpMode::Style;
	UPROPERTY(EditAnywhere, Category = "Warp", meta = (EditCondition = "Mode==EDreamWarpMode::Style", EditConditionHides))
	EDreamWarpStyle Style = EDreamWarpStyle::Arc;
	UPROPERTY(EditAnywhere, Category = "Warp", meta = (EditCondition = "Mode==EDreamWarpMode::Style", EditConditionHides))
	EDreamWarpOrientation Orientation = EDreamWarpOrientation::Horizontal;
	/** How far the style bends, -100 to 100 percent; a negative bend bends the other way. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetBend, Category = "Warp", meta = (UIMin = "-100", UIMax = "100", ClampMin = "-100", ClampMax = "100", EditCondition = "Mode==EDreamWarpMode::Style", EditConditionHides))
	float Bend = 50.0f;
	/** -100 to 100 percent: a perspective along the length, the right end the larger one when positive. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalDistortion, Category = "Warp", meta = (UIMin = "-100", UIMax = "100", ClampMin = "-100", ClampMax = "100", EditCondition = "Mode==EDreamWarpMode::Style", EditConditionHides))
	float HorizontalDistortion = 0.0f;
	/** -100 to 100 percent: a perspective across, the top the larger one when positive. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVerticalDistortion, Category = "Warp", meta = (UIMin = "-100", UIMax = "100", ClampMin = "-100", ClampMax = "100", EditCondition = "Mode==EDreamWarpMode::Style", EditConditionHides))
	float VerticalDistortion = 0.0f;
	/** Turns of the Flag and Wave styles' waves: animate it, or let Phase Speed turn it. Nothing to the other styles. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPhase, Category = "Warp", meta = (EditCondition = "Mode==EDreamWarpMode::Style&&(Style==EDreamWarpStyle::Flag||Style==EDreamWarpStyle::Wave)", EditConditionHides))
	float Phase = 0.0f;
	/** Turns a second that Phase moves by while the game runs: a flag waving. 0 holds it still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Warp", meta = (EditCondition = "Mode==EDreamWarpMode::Style&&(Style==EDreamWarpStyle::Flag||Style==EDreamWarpStyle::Wave)", EditConditionHides))
	float PhaseSpeed = 0.0f;

	/** Control points across, 2 to 5. Changing it lays the lattice out afresh. */
	UPROPERTY(EditAnywhere, Category = "Warp", meta = (ClampMin = "2", ClampMax = "5", EditCondition = "Mode==EDreamWarpMode::Lattice", EditConditionHides))
	int32 LatticeColumns = 4;
	/** Control points down, 2 to 5. Changing it lays the lattice out afresh. */
	UPROPERTY(EditAnywhere, Category = "Warp", meta = (ClampMin = "2", ClampMax = "5", EditCondition = "Mode==EDreamWarpMode::Lattice", EditConditionHides))
	int32 LatticeRows = 4;
	/**
	 * The lattice's control points, row by row from the top, each row from the left, in the box's own measure: 0,0 its
	 * top left corner, 1,1 its bottom right. Evenly spread, they move nothing.
	 */
	UPROPERTY(EditAnywhere, Category = "Warp", meta = (EditCondition = "Mode==EDreamWarpMode::Lattice", EditConditionHides))
	TArray<FVector2D> LatticePoints;

	/** The box the warp bends: what the visual draws, or the widget's rect. */
	UPROPERTY(EditAnywhere, Category = "Warp")
	EDreamWarpBounds Bounds = EDreamWarpBounds::Geometry;
	/** The longest side a piece of the cut mesh may have, in local units. Smaller bends more smoothly and costs more vertices. */
	UPROPERTY(EditAnywhere, Category = "Warp|Subdivision", meta = (ClampMin = "1.0", UIMin = "2.0", UIMax = "64.0"))
	float CellLength = 8.0f;
	/** The most vertices the cut mesh may have; the pieces grow until it fits. */
	UPROPERTY(EditAnywhere, Category = "Warp|Subdivision", meta = (ClampMin = "64", ClampMax = "65535"))
	int32 MaxVertices = 8192;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnRegister() override;
	virtual void Tick(float DeltaTime) override;
	void MarkDirty();
	/** LatticePoints laid out evenly for the lattice's size when they are not as many as it has. */
	void FitLattice();

public:
	virtual EDreamMeshModifierStage GetStage() const override { return EDreamMeshModifierStage::Deform; }
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor) override;
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged) override;

	/** Whether the warp moves nothing now: no style bend or distortion, or a lattice at rest. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsAtRest() const;
	/** The style as the arithmetic takes it (FDreamMeshDeform::FStyle). */
	FDreamMeshDeform::FStyle MakeStyle() const;
	/** Where InLocalPoint goes, for a box InMin..InMax: the warp itself, without any cutting. */
	FVector2f WarpPoint(FVector2f InLocalPoint, FVector2f InMin, FVector2f InMax) const;
	/** The box the warp bends for InGeometry, as Bounds says. */
	FBox2f GetWarpBox(const FDreamUIGeometry& InGeometry) const;
	/** The box the last geometry was bent in, for drawing the lattice over it (the designer's handles); invalid before. */
	FBox2f GetLastWarpBox() const { return LastWarpBox; }
private:
	FBox2f LastWarpBox = FBox2f(ForceInit);
public:

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamWarpMode GetMode() const { return Mode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamWarpStyle GetStyle() const { return Style; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamWarpOrientation GetOrientation() const { return Orientation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetBend() const { return Bend; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetHorizontalDistortion() const { return HorizontalDistortion; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetVerticalDistortion() const { return VerticalDistortion; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetPhase() const { return Phase; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetPhaseSpeed() const { return PhaseSpeed; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetLatticeColumns() const { return LatticeColumns; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetLatticeRows() const { return LatticeRows; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") const TArray<FVector2D>& GetLatticePoints() const { return LatticePoints; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamWarpBounds GetBounds() const { return Bounds; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetCellLength() const { return CellLength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") int32 GetMaxVertices() const { return MaxVertices; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetMode(EDreamWarpMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetStyle(EDreamWarpStyle Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetOrientation(EDreamWarpOrientation Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetBend(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetHorizontalDistortion(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetVerticalDistortion(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPhase(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPhaseSpeed(float Value);
	/** Sets the lattice's size and lays its points out evenly. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetLatticeSize(int32 InColumns, int32 InRows);
	/** Every point at once, in LatticePoints' order and measure; ignored unless there are Columns x Rows of them. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetLatticePoints(const TArray<FVector2D>& Value);
	/** One control point, in the box's measure (0,0 top left, 1,1 bottom right). */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetLatticePoint(int32 InColumn, int32 InRow, FVector2D InPoint);
	/** The lattice laid out evenly again: no warp. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void ResetLattice();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetBounds(EDreamWarpBounds Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetCellLength(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetMaxVertices(int32 Value);
};
