// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshDeform.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "DreamMeshModifierJelly.generated.h"

/** The part of a jelly that holds still while the rest wobbles: the points far from it lag the most. */
UENUM(BlueprintType)
enum class EDreamJellyAnchor : uint8
{
	Centre,
	Bottom,
	Top,
	Left,
	Right,
	/** Nothing holds: every point lags alike, and the whole mesh trails behind its widget. */
	None,
};

/**
 * The springs of a jelly: a lattice of control points, each offset from where it rests and pulled back by a damped spring,
 * pushed by the widget's acceleration (each point by its own share) and drawn towards a target (a press, a drag). Offsets
 * are in local units. It steps in fixed steps of StepSeconds whatever the frame time, carrying the rest over, so the same
 * frame times give the same motion on any machine.
 */
struct DREAMGUIEXTENSIONS_API FDreamJellySprings
{
	static constexpr float StepSeconds = 1.0f / 240.0f;
	/** A frame longer than this many steps drops the rest, rather than stalling to catch up. */
	static constexpr int32 MaxStepsPerAdvance = 32;

	int32 Columns = 3;
	int32 Rows = 3;
	TArray<FVector2f> Offset;
	TArray<FVector2f> Velocity;
	/** Where each point is drawn to, as an offset from its rest. */
	TArray<FVector2f> Target;
	/** How much of the widget's acceleration each point feels, 0 to 1. */
	TArray<float> Weight;
	/** Oscillations a second of a point left to itself. */
	float Frequency = 4.0f;
	/** 1 settles without overshoot, less wobbles. */
	float DampingRatio = 0.3f;
	/** How far a point may move from its rest, in local units. */
	float MaxOffset = 100.0f;
	/** Time carried over to the next Advance, under one step. */
	float Remainder = 0.0f;

	/** InColumns by InRows points, every one at rest and feeling all of the acceleration. */
	void Reset(int32 InColumns, int32 InRows);
	/** Shares of the acceleration by distance from InAnchor, over the lattice's own measure (0 at the anchor, 1 farthest). */
	void SetAnchor(EDreamJellyAnchor InAnchor);
	/**
	 * InDeltaSeconds more of the motion, the widget accelerating at InAcceleration (local units a second squared) all the
	 * while. A point feels it the other way, as a passenger does. Returns the steps taken.
	 */
	int32 Advance(float InDeltaSeconds, FVector2f InAcceleration);
	/** Every point within InTolerance of its target and slower than InTolerance a second. */
	bool IsSettled(float InTolerance) const;
	/** Every point at its rest, still, and drawn nowhere: nothing to draw. */
	bool IsAtRest(float InTolerance) const;
	/** The lattice the offsets make for a box InMin..InMax, in FDreamMeshDeform::EvaluateLattice's order and measure. */
	void MakeLattice(FVector2f InMin, FVector2f InMax, TArray<FVector2D>& OutPoints) const;
};

/**
 * A wobble: the mesh on a lattice of control points held by springs (FDreamJellySprings), which lag behind as the widget
 * moves and spring back, squash while the widget is pressed and bounce when it is let go, and stretch after a drag. The
 * mesh is cut and bent as the warp does it (FDreamMeshDeform), so the corners of a rect block and the glyphs of a text
 * bend with it. Still and untouched, it costs nothing.
 *
 * Presses and drags reach it as they reach any component of a widget that takes pointer events -- the widget must be hit
 * by the pointer -- and it passes them on: a button under it still clicks, a scroll view above it still scrolls.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Jelly", meta = (BlueprintSpawnableComponent))
class DREAMGUIEXTENSIONS_API UDreamMeshModifierJelly : public UDreamMeshModifierBase, public IDreamPointerDownUpInterface, public IDreamPointerDragInterface
{
	GENERATED_BODY()

public:
	UDreamMeshModifierJelly();

protected:
	/** Control points across, 2 to 5. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "2", ClampMax = "5"))
	int32 Columns = 3;
	/** Control points down, 2 to 5. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "2", ClampMax = "5"))
	int32 Rows = 3;
	/** Oscillations a second: higher is stiffer. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.1", UIMax = "12.0"))
	float Frequency = 4.0f;
	/** 1 settles without overshoot, less wobbles longer. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DampingRatio = 0.3f;
	/** How much the widget's own acceleration pushes the points: 0 ignores its moves. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float Inertia = 1.0f;
	/** What holds still. */
	UPROPERTY(EditAnywhere, Category = "Jelly")
	EDreamJellyAnchor Anchor = EDreamJellyAnchor::Centre;
	/** How far a point may stretch, as a share of the box's larger side. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxStretch = 0.3f;
	/** How far the points draw in towards the press while the widget is pressed, as a share of their distance from it. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PressSquash = 0.08f;
	/** How much of a drag's distance the nearest points follow while the widget is dragged: 0 ignores drags. */
	UPROPERTY(EditAnywhere, Category = "Jelly", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DragStretch = 0.4f;
	/** The box the lattice spans: the widget's rect, or what the visual draws. */
	UPROPERTY(EditAnywhere, Category = "Jelly")
	EDreamWarpBounds Bounds = EDreamWarpBounds::Widget;
	UPROPERTY(EditAnywhere, Category = "Jelly|Subdivision", meta = (ClampMin = "1.0", UIMin = "2.0", UIMax = "64.0"))
	float CellLength = 10.0f;
	UPROPERTY(EditAnywhere, Category = "Jelly|Subdivision", meta = (ClampMin = "64", ClampMax = "65535"))
	int32 MaxVertices = 8192;

	FDreamJellySprings Springs;
	/** The widget's place in the world on the last two ticks, for its velocity and acceleration. */
	FVector LastLocation = FVector::ZeroVector;
	FVector LastVelocity = FVector::ZeroVector;
	int32 LocationSamples = 0;
	bool bPressed = false;
	bool bDragging = false;
	/** Where the press or drag began, and where the drag is now, in local units. */
	FVector2f PressPoint = FVector2f::ZeroVector;
	FVector2f DragPoint = FVector2f::ZeroVector;
	/** Whether the last geometry was bent, so that a jelly coming to rest rebuilds once more, flat. */
	bool bDrawnBent = false;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnRegister() override;
	virtual void OnEnable() override;
	virtual void Tick(float DeltaTime) override;
	void ConfigureSprings();
	void UpdateTargets();
	/** The box the lattice spans now. */
	FBox2f GetBox(const FDreamUIGeometry* InGeometry) const;
	FVector2f ToLocal(const FVector& InWorldPoint) const;
	void MarkDirty();

public:
	virtual EDreamMeshModifierStage GetStage() const override { return EDreamMeshModifierStage::Deform; }
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor) override;
	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged) override;

	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;

	/** The springs, to read; Simulate and the input calls are what move them. */
	const FDreamJellySprings& GetSprings() const { return Springs; }
	/**
	 * Advances the springs by InDeltaSeconds under the widget acceleration InAcceleration (local units a second squared),
	 * as a tick does with what it measured: for tests and for code that drives a jelly by hand.
	 */
	void Simulate(float InDeltaSeconds, FVector2f InAcceleration);
	/** A push at once: every point's velocity changed by InVelocity (local units a second) times its share. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Poke(FVector2D InVelocity);
	/** The press as the pointer would make it, at InLocalPoint; false lets go. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPressed(bool bInPressed, FVector2D InLocalPoint);
	/** A drag as the pointer would make it: from where it was pressed to InLocalPoint. EndDrag lets go. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void DragTo(FVector2D InLocalPoint);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void EndDrag();
	/** Every point at rest at once, still. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void Settle();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsAtRest() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetFrequency(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetDampingRatio(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetInertia(float Value) { Inertia = FMath::Max(Value, 0.0f); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetAnchor(EDreamJellyAnchor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetPressSquash(float Value) { PressSquash = FMath::Clamp(Value, 0.0f, 0.5f); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetDragStretch(float Value) { DragStretch = FMath::Clamp(Value, 0.0f, 1.0f); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetMaxStretch(float Value) { MaxStretch = FMath::Clamp(Value, 0.0f, 1.0f); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") void SetLatticeSize(int32 InColumns, int32 InRows);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetFrequency() const { return Frequency; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetDampingRatio() const { return DampingRatio; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") float GetInertia() const { return Inertia; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI") EDreamJellyAnchor GetAnchor() const { return Anchor; }
};
