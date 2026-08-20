// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidget;
class UWorld;

/** One widget whose rect the pick ray crossed. */
struct FDreamUIWidgetPickHit
{
	UDreamWidget* Widget = nullptr;
	FVector WorldLocation = FVector::ZeroVector;
	double Distance = 0.0;
};

/**
 * Designer picking by widget rect rather than by rendered triangles.
 *
 * UDreamUIManagerWorldSubsystem::RaycastHitUI answers "what did the player click", so it starts from
 * `if (auto Visual = Widget->GetVisual())` -- no mesh, no hit. Every UMG-family layout panel is
 * registered with a layout container and no visual (FDreamUIControlRegistry::MakePanel), so under that
 * question an Overlay does not exist: it cannot be clicked, and it cannot be a drop parent. The
 * designer asks a different question -- "which rect is under the cursor" -- and every widget can
 * answer it, because every widget owns a rect whether or not anything draws it. That is also the
 * question Slate and uGUI answer for their own designers.
 */
namespace DreamUIWidgetPicking
{
	/** Ray against InWidget's own rect, in the widget's plane. */
	DREAMGUIEDITOR_API bool LineTraceWidgetRect(const UDreamWidget* InWidget, const FVector& InLineStart, const FVector& InLineEnd, FVector& OutWorldLocation);

	/** Every crossed rect among InWidgets, front-to-back: nearest canvas first, then deepest child. */
	DREAMGUIEDITOR_API void RaycastWidgetRects(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd, TArray<FDreamUIWidgetPickHit>& OutHits);

	/**
	 * Front-most crossed rect. InOutCycleIndex advances one step deeper into the stack on each call,
	 * so clicking the same pixel repeatedly walks down through overlapping widgets; reset it to
	 * INDEX_NONE whenever the pick ray moves, or the walk continues from wherever the last stack
	 * happened to leave it.
	 */
	DREAMGUIEDITOR_API UDreamWidget* PickTopmostWidget(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd, int32& InOutCycleIndex);

	/**
	 * The widget a drop on InHitWidget should parent to: the nearest ancestor-or-self owning a layout
	 * container that still has room. Null when there is none -- callers fall back to the prefab root,
	 * which is what a container-less prefab wants anyway.
	 *
	 * Resolving to a container rather than to the hit widget is what makes a drop land somewhere that
	 * will arrange it. Dropping onto a Text inside a VerticalBox means the VerticalBox; nesting into
	 * the Text itself is a deliberate act and stays available from the hierarchy tree.
	 */
	DREAMGUIEDITOR_API UDreamWidget* ResolveDropContainer(UDreamWidget* InHitWidget);

	/** Every widget under every root canvas of InWorld, the canvas roots included. */
	DREAMGUIEDITOR_API void CollectPickableWidgets(const UWorld* InWorld, TArray<UDreamWidget*>& OutWidgets);
}
