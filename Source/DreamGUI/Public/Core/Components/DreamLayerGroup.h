// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIClipMask.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "DreamLayerGroup.generated.h"

class FDreamUIClipData;
class UDreamCanvas;
struct FDreamUICompositeDesc;

/** Whether a layer group's subtree is drawn apart before it is put down (UDreamLayerGroup). */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamLayerGroupIsolation : uint8
{
	/** Drawn as if there were no group, unless an opacity, a mode or a mask below asks for it to be drawn apart. */
	PassThrough	UMETA(DisplayName = "Pass Through"),
	/** Drawn into a texture of its own first, then put down as one: its children's blend modes blend with each other only. */
	Isolated	UMETA(DisplayName = "Isolated"),
};

/**
 * Makes the widget's subtree one layer: drawn into a texture of its own, cleared to transparent, then put over what is
 * behind it once -- by its own blend mode, at its own opacity, through its own mask.
 *
 * - GroupOpacity fades the subtree as one: children that overlap do not show through each other on the way out, as they do
 *   when each takes the widget's RenderOpacity.
 * - GroupBlendMode blends the whole subtree with what is behind it (EDreamUIBlendMode): a group of a few elements over a
 *   picture, in Multiply, darkens it by what the group looks like, not by each element.
 * - Inside an isolated group, a child's blend mode blends with what the group has drawn so far, never with what is behind
 *   the group.
 * - GroupMask masks the group as it is put down, in the widget's rect (FDreamClipMask): a mask of the composite, not of
 *   each element. It does not take part in hit tests.
 *
 * The texture covers what the subtree draws on the target, and no more, and is the frame's alone: a group costs a texture
 * the size of what it draws, a clear and a full-screen pass over that rect, every frame it is drawn. Groups nest. On a
 * world-space canvas a group is drawn as if passed through, and a warning says so.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Layer Group")
class DREAMGUI_API UDreamLayerGroup : public UDreamUIBehaviour
{
	GENERATED_BODY()
public:
	UDreamLayerGroup();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamLayerGroupIsolation GetIsolation()const { return Isolation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetIsolation(EDreamLayerGroupIsolation Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetGroupOpacity()const { return GroupOpacity; }
	/** Applied once to the composited subtree. Changing it redraws nothing but the composite. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGroupOpacity(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamUIBlendMode GetGroupBlendMode()const { return GroupBlendMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGroupBlendMode(EDreamUIBlendMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const FDreamClipMask& GetGroupMask()const { return GroupMask; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetGroupMask(const FDreamClipMask& Value);

	/** Whether the subtree is drawn apart: Isolated, or an opacity, a mode or a mask other than the default asks for it. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsIsolated()const;

	/**
	 * The group's own part of its composite, as it is now: its mode, its opacity, and its mask's row of InCanvas's clip data
	 * with the textures that row reads. Game thread; the canvas asks every frame it draws the group.
	 */
	void FillComposite(FDreamUICompositeDesc& InOutDesc, UDreamCanvas* InCanvas);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamLayerGroupIsolation Isolation = EDreamLayerGroupIsolation::PassThrough;
	/** Applied once to the composited subtree, so overlapping children fade together. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float GroupOpacity = 1.0f;
	/** How the composited subtree blends with what is behind it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	EDreamUIBlendMode GroupBlendMode = EDreamUIBlendMode::Alpha;
	/** Masks the composited subtree as it is put down, measured in this widget's rect; not in hit tests. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FDreamClipMask GroupMask;

private:
	/**
	 * Something about the group changed: the canvas draws again, and its draw calls are made again when the group starts or
	 * stops being drawn apart -- the opacity, mode and mask alone the canvas reads every frame.
	 */
	void NoteChanged();
	/** The mask's own chain in InCanvas's root's clip data, made when first asked for there; null while there is no mask. */
	const FDreamUIClipData* GetMaskClip(UDreamCanvas* InCanvas);
	void ReleaseMaskClip();

	TSharedPtr<FDreamUIClipData> MaskClip;
	/** The root canvas whose clip data MaskClip is in. */
	TWeakObjectPtr<UDreamCanvas> MaskClipCanvas;
	/** Whether it was drawn apart when the canvas last made its draw calls: a change asks for them to be made again. */
	bool bWasIsolated = false;
};
