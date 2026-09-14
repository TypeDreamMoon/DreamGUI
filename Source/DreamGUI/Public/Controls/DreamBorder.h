// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamBorder.generated.h"

class UDreamWidget;

/**
 * A border whose hierarchy is code, not an asset: a face, a padding, and a hole to put things in.
 *
 * UMG's Border is a single-child panel that draws a brush behind whatever it holds, and this library
 * already offers that SHAPE as a palette panel -- an overlay carrying an image visual. This is the
 * CONTROL spelling, and the difference is the one that runs through this whole family: a panel's
 * brush, colour and padding are authored per instance, and a control's come from the project style
 * sheet, so restyling a project's boxes is one edit rather than one per box.
 *
 * SEVERAL CHILDREN, deliberately, where UMG's border takes one. The face carries an overlay, so a
 * `Native.Border { A B }` stacks A under B with both filling the padded area -- which is what an
 * overlay means and what a reader of those two lines expects. UDreamScrollBox's content slot makes
 * the same call for the same reason; a hole that took exactly one child would send every second
 * child somewhere invisible.
 *
 * The outline is part of the face rather than a second widget: UDreamRectBlock draws a border of its
 * own, so BorderThickness and BorderColor are pushed into the rect and cost no node. Zero thickness
 * is UMG's plain border, which is what the style ships.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Border")
class DREAMGUI_API UDreamBorder : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Border")
	FDreamBorderStyle Style;

	/** The face: what draws the brush, the tint and the outline, and what clips to the rounding. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Border")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The hole. Whatever a host nests on this control ends up here. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Border")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;

	/** Where things go. Parent into this, or nest in `.dui`, and the padding holds them off the edge. */
	UFUNCTION(BlueprintCallable, Category = "Border")
	UDreamWidget* GetContentNode() const { return ContentNode; }

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
};
