// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamUIWidgetGeometryLibrary.generated.h"

class UDreamWidget;

/**
 * A widget's measured rect, and the handle every coordinate conversion below is asked through --
 * this framework's answer to Slate's FGeometry.
 *
 * FGeometry itself is no use here: it describes an SWidget's place inside a Slate window, and a
 * DreamGUI widget is a mesh in a scene that may not be on a window at all. What a caller actually
 * wants from one is the two things kept below -- how big the widget is in its own units, and where
 * its rect lands in viewport pixels -- plus a way to move a point between those spaces, which is
 * what the library does.
 *
 * THREE SPACES, named the way UMG names them:
 *   - LOCAL: the widget's own 2D space, origin at its BOTTOM-LEFT corner, X right, Y up, in widget
 *     units. Bottom-left rather than Slate's top-left because every other normalized coordinate in
 *     this framework (AnchorData.Pivot, RenderTransformPivot, the canvas's own conversions) already
 *     puts zero at the bottom left, and being consistent inside the fork beats matching the other
 *     engine on one axis.
 *   - ABSOLUTE: viewport pixels, origin TOP-LEFT -- the same numbers a mouse position arrives in,
 *     and the same space UDreamCanvas::ConvertPositionFromCanvasToViewport produces. Slate's
 *     "absolute" is desktop pixels; in a game viewport the two differ only by the window's own
 *     position, which nothing in this framework has a use for.
 *   - VIEWPORT: absolute divided by the DPI scale, which is what UMG's LocalToViewport hands back
 *     alongside the pixel position.
 *
 * bHasAbsolute is false for a widget whose rect cannot be placed in the viewport at all -- a
 * world-space canvas with no player camera to project through, a widget that is not under a canvas
 * yet, a point behind the eye. Every conversion answers false in the same cases rather than
 * returning a plausible number from the wrong space.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIWidgetGeometry
{
	GENERATED_BODY()

	/** The widget this was read from. Everything else is a snapshot of it at that moment. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI|Geometry")
	TWeakObjectPtr<UDreamWidget> Widget = nullptr;

	/** The widget's size in its own units -- UMG's GetLocalSize. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI|Geometry")
	FVector2D LocalSize = FVector2D::ZeroVector;

	/** The widget's rect corner in viewport pixels. Only meaningful while bHasAbsolute. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI|Geometry")
	FVector2D AbsolutePosition = FVector2D::ZeroVector;

	/** The widget's size in viewport pixels. Only meaningful while bHasAbsolute. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI|Geometry")
	FVector2D AbsoluteSize = FVector2D::ZeroVector;

	/** False when this rect cannot be placed in the viewport. See the class comment. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI|Geometry")
	bool bHasAbsolute = false;

	bool IsValidGeometry() const { return Widget.IsValid(); }
};

/**
 * Coordinate conversions between a widget's own space, the viewport and the screen -- this
 * framework's USlateBlueprintLibrary.
 *
 * Everything returns bool and writes its answer through an out-parameter, where UMG's equivalents
 * return the value directly. That is deliberate and it is about world-space UI: a panel welded to a
 * machine in the level has no viewport position until a camera is pointed at it, and the honest
 * answers are "here it is" and "nowhere", not a number computed in the wrong space. A caller that
 * ignores the bool gets a zero, not a lie that looks like a position.
 */
UCLASS()
class DREAMGUI_API UDreamUIWidgetGeometryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Read a widget's geometry -- UMG's GetCachedGeometry, GetTickSpaceGeometry and
	 * GetPaintSpaceGeometry all at once.
	 *
	 * One function rather than three because there is nothing here for the three to disagree about:
	 * UMG's split exists because Slate paints from a cached element list that can lag a tick behind
	 * the arrangement, and DreamGUI arranges and builds its draw calls in the same pass.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static FDreamUIWidgetGeometry GetWidgetGeometry(UDreamWidget* InWidget);

	/** The widget's size in its own units. Zero for a geometry that names no widget. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static FVector2D GetLocalSize(const FDreamUIWidgetGeometry& InGeometry);

	/**
	 * The corner local coordinates are measured from, in the widget's own space.
	 *
	 * Always zero, because LOCAL is defined with its origin at that corner -- the same thing UMG's
	 * GetLocalTopLeft answers for an ordinary widget. It exists so a graph written against UMG
	 * reads the same, and so the definition has somewhere to be stated.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static FVector2D GetLocalTopLeft(const FDreamUIWidgetGeometry& InGeometry);

	/** The widget's size in viewport pixels. False when the rect cannot be placed in the viewport. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool GetAbsoluteSize(const FDreamUIWidgetGeometry& InGeometry, FVector2D& OutAbsoluteSize);

	/** A point in the widget's own space, in viewport pixels. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool LocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry, FVector2D InLocalCoordinate,
		FVector2D& OutAbsoluteCoordinate);

	/** A viewport pixel position, in the widget's own space. Assumes the point lies in the widget's plane. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool AbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry, FVector2D InAbsoluteCoordinate,
		FVector2D& OutLocalCoordinate);

	/**
	 * A point in the widget's own space, as both viewport pixels and DPI-scaled viewport coordinates
	 * -- UMG's LocalToViewport, which hands back both for the same reason.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry", meta = (WorldContext = "WorldContextObject"))
	static bool LocalToViewport(UObject* WorldContextObject, const FDreamUIWidgetGeometry& InGeometry,
		FVector2D InLocalCoordinate, FVector2D& OutPixelPosition, FVector2D& OutViewportPosition);

	/** Viewport pixels, as both pixels and DPI-scaled viewport coordinates. UMG's AbsoluteToViewport. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry", meta = (WorldContext = "WorldContextObject"))
	static bool AbsoluteToViewport(UObject* WorldContextObject, FVector2D InAbsoluteCoordinate,
		FVector2D& OutPixelPosition, FVector2D& OutViewportPosition);

	/**
	 * A screen position -- what a mouse position or a world-to-screen projection gives you -- in the
	 * widget's own space. UMG's ScreenToWidgetLocal.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool ScreenToWidgetLocal(const FDreamUIWidgetGeometry& InGeometry, FVector2D InScreenPosition,
		FVector2D& OutLocalCoordinate);

	/** The same screen position in viewport pixels. Here the two are the same space, so this is identity. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool ScreenToWidgetAbsolute(FVector2D InScreenPosition, FVector2D& OutAbsoluteCoordinate);

	/** A screen position as DPI-scaled viewport coordinates. UMG's ScreenToViewport. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry", meta = (WorldContext = "WorldContextObject"))
	static bool ScreenToViewport(UObject* WorldContextObject, FVector2D InScreenPosition,
		FVector2D& OutViewportPosition);

	/** Is a viewport pixel position inside this widget's rect? UMG's IsUnderLocation. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool IsUnderLocation(const FDreamUIWidgetGeometry& InGeometry, FVector2D InAbsoluteCoordinate);

	/**
	 * A LENGTH converted between the two spaces, with no origin involved -- UMG's
	 * Scalar_LocalToAbsolute and friends.
	 *
	 * Derived from the rect rather than from a transform: the ratio of the widget's pixel size to its
	 * local size is exactly the scale a length is subject to, and taking it from the rect means a
	 * canvas scaler, a render scale and a parent's scale are all already in it. Uniform on both axes
	 * is assumed, as it is in UMG, and the wider axis decides when they differ.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool TransformScalarLocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry, float InLocalScalar,
		float& OutAbsoluteScalar);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool TransformScalarAbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry, float InAbsoluteScalar,
		float& OutLocalScalar);
	/** A DIRECTION and length, per axis. UMG's Vector_LocalToAbsolute. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool TransformVectorLocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry, FVector2D InLocalVector,
		FVector2D& OutAbsoluteVector);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Geometry")
	static bool TransformVectorAbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry, FVector2D InAbsoluteVector,
		FVector2D& OutLocalVector);
};
