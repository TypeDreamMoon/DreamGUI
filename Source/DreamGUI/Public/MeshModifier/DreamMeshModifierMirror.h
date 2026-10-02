// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "MeshModifier/DreamMeshModifierBase.h"
#include "DreamMeshModifierMirror.generated.h"

/**
 * Mirrors a visual's mesh about the centre of its widget's rect -- left to right, top to bottom, or both -- and
 * leaves the widget, its rect and its children where they are.
 *
 * What UMG's border flips its background with for a right-to-left culture: SBorder draws its brush through a render
 * transform of scale (-1, 1) about the brush's centre, so the brush is mirrored and nothing it holds is.
 * UDreamLayoutContainerBorder makes one of these for its bFlipForRightToLeftFlowDirection.
 *
 * The vertices are reflected with their texture coordinates kept, so whatever the visual draws -- a sprite, a sliced
 * frame, a rect with uneven corners -- is drawn reflected. A reflection through one axis turns every triangle over, so
 * the triangles are rewound as well, or a material that culls its back faces would draw nothing.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, DisplayName = "Mirror", meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamMeshModifierMirror : public UDreamMeshModifierBase
{
	GENERATED_BODY()

public:
	UDreamMeshModifierMirror();

	virtual void ModifyUIGeometry(FDreamUIGeometry& InGeometry
		, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
	) override;
	/**
	 * Positions and triangles, every rebuild: declaring both is what makes the batch mesh hand this modifier freshly
	 * built ones each time, so a reflection is never applied to a mesh it already reflected.
	 */
	virtual void ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor) override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetMirrorHorizontally() const { return bMirrorHorizontally; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMirrorHorizontally(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetMirrorVertically() const { return bMirrorVertically; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetMirrorVertically(bool Value);

	/**
	 * Reflect InGeometry's vertices about InCentre, in the visual's local plane (Y across, Z up): across it when
	 * bInHorizontally, up and down when bInVertically, and the triangles rewound when exactly one of the two turned
	 * them over. Static and pure, so the arithmetic can be checked against a mesh built by hand.
	 */
	static void MirrorGeometry(FDreamUIGeometry& InGeometry, const FVector2f& InCentre, bool bInHorizontally, bool bInVertically);

protected:
	/** Reflect left to right, about the rect's vertical centre line. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bMirrorHorizontally = true;
	/** Reflect top to bottom, about the rect's horizontal centre line. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bMirrorVertically = false;
};
