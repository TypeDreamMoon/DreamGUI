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
	 * The crossed rect a drop on the ray resolves from (ResolveDropContainer): the front-most that can hold what is
	 * dropped -- one with a layout container, one already holding children, a placed control -- looking through every
	 * rect in front of it that holds nothing. A widget that holds nothing gives what is dropped on it to whatever holds
	 * it, and the front-most of those is in another branch whenever the empty rect is a layer over the whole page: a
	 * panel's effect (a pixel sort, a light) filling its root's overlay. Resolved from that layer, a row dragged a few
	 * pixels inside its group went to the overlay, and so did a Text dropped on the group. A Text in a box still gives
	 * the box, which is the next rect behind it. Null when nothing is crossed; the front-most rect when nothing crossed
	 * can hold anything.
	 */
	DREAMGUIEDITOR_API UDreamWidget* PickDropTarget(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd);

	/**
	 * The widget a drop on InHitWidget should parent to: the nearest ancestor-or-self that places
	 * children and still has room -- one owning a layout container, or a group: a widget with no panel
	 * that holds children, which stand where their anchors put them as on a Canvas Panel. Null when
	 * there is none -- callers fall back to the prefab root, which is what a container-less prefab
	 * wants anyway.
	 *
	 * Resolving to a container rather than to the hit widget is what makes a drop land somewhere that
	 * will arrange it. Dropping onto a Text inside a VerticalBox means the VerticalBox; nesting into
	 * the Text itself -- or into any widget that holds nothing yet -- is a deliberate act and stays
	 * available from the hierarchy tree.
	 *
	 * InTreeRoot is where the walk stops, and a designer has to pass it. The widget being authored is
	 * not the top of the preview world: the designer stands it inside a wrapper of its own, and that
	 * wrapper carries an Overlay so the authored root is given the design size. An unbounded walk from
	 * a root with no container climbs straight out of the asset and answers with that wrapper -- a
	 * container, with room -- and the drop then parents the widget to a piece of the editor: it fills
	 * the screen, belongs to nothing that is saved, and drops out of the hierarchy. A hit that is not
	 * inside InTreeRoot at all resolves to nothing for the same reason. Null means unbounded.
	 */
	DREAMGUIEDITOR_API UDreamWidget* ResolveDropContainer(UDreamWidget* InHitWidget, const UDreamWidget* InTreeRoot = nullptr);

	/** Every widget under every root canvas of InWorld, the canvas roots included. */
	DREAMGUIEDITOR_API void CollectPickableWidgets(const UWorld* InWorld, TArray<UDreamWidget*>& OutWidgets);
}
