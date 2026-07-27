// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

/**
 * Per-subtree perspective, in the shape CSS uses: an ancestor declares a perspective, and its
 * descendants foreshorten within it according to their depth.
 *
 * The trick that makes this cheap is that the canvas ALREADY renders through a perspective
 * projection. So a subtree does not need its own projection or its own pass -- it needs its
 * geometry re-aimed, so that the canvas's existing projection produces the picture the subtree's
 * own eye would have produced. That re-aiming is a plain affine map, which is why this costs no
 * vertex format change, no shader change and no extra draw call.
 */
namespace LexPerspective
{
	/** One declared perspective: a plane to foreshorten about, and the eye to foreshorten from. */
	struct LGUI_API FScope
	{
		/** A point on the plane the perspective is measured from -- the declaring widget's origin. */
		FVector PlanePoint = FVector::ZeroVector;
		/** The plane's normal: the declaring widget's forward (local +X) in world space. */
		FVector PlaneNormal = FVector::XAxisVector;
		/** Where this subtree is being looked at from. */
		FVector EyePosition = FVector::ZeroVector;
	};

	/**
	 * The affine map that makes projecting from OuterEye reproduce what projecting from
	 * Scope.EyePosition would have shown.
	 *
	 * It is the unique affine map that fixes the scope's plane pointwise and carries the scope's eye
	 * onto OuterEye. That is enough to prove it right in one line: an affine map takes lines to
	 * lines, and it leaves the plane alone, so the point where the sight line from the inner eye
	 * crosses the plane does not move -- and the image line from the outer eye must therefore pass
	 * through the same place. Everything else in this feature is bookkeeping around that sentence.
	 *
	 * Returns identity when the two eyes coincide (nothing to re-aim) and when the scope is
	 * degenerate: an eye lying in its own plane has no perspective to speak of, and an outer eye at
	 * infinity -- an orthographic canvas -- cannot be reached by any affine map at all.
	 */
	LGUI_API FMatrix MakeRemap(const FScope& Scope, const FVector& OuterEye);

	/**
	 * The composed map for a widget sitting inside nested perspectives.
	 *
	 * Scopes are ordered innermost first, and each one hands off to the next: the innermost re-aims
	 * from its own eye to the eye of the scope enclosing it, and only the outermost re-aims onto the
	 * canvas's. Reading it the other way -- "the nearest perspective ancestor wins, ignore the rest"
	 * -- gives an answer that looks plausible on a single scope and is wrong the moment anyone nests
	 * two, which is why the nesting case is pinned by its own test.
	 */
	LGUI_API FMatrix ComposeRemap(TArrayView<const FScope> ScopesInnermostFirst, const FVector& CanvasEye);

	/**
	 * Where the sight line from Eye through Point crosses the plane. This is what "the same picture"
	 * means, so it is also how the tests state their expectations rather than restating the algebra.
	 * Returns false when the line runs parallel to the plane.
	 */
	LGUI_API bool ProjectOntoPlane(const FVector& Eye, const FVector& Point,
		const FVector& PlanePoint, const FVector& PlaneNormal, FVector& OutOnPlane);
}
