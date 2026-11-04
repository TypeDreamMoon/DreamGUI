// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RHIResources.h"
#include "RendererInterface.h"
#include "DreamUIRender/DreamUIBaseShaders.h"
#include "DreamUIRender/DreamUIBlendMode.h"

/**
 * A group's composite, as the marker that opens it carries it to the render thread (EDreamUIRendererPrimitiveType::Group).
 *
 * What is drawn between a group's two markers goes into a texture of its own, cleared to transparent, the size of what it
 * covers on the target; the marker that closes it puts that texture over what is behind it: by the group's blend mode, at
 * its opacity, through its mask. Three kinds of group share it, by Role:
 *
 * - Group: a layer group (UDreamLayerGroup), or an element drawn through a material with a blend mode its material cannot
 *   do, as a group of one.
 * - ClipBase and ClipBody: clipping to the layer below where the layer below is something no clip level can follow (a
 *   text, a tiled image, a group): the layer below is drawn into a texture and put down as it is; what clips to it is
 *   drawn into a second texture, which is put down only where the first has alpha.
 *
 * A group can also be drawn already (bExternal): a texture kept from frame to frame -- a retainer box's -- put down the same
 * way over a rect of the canvas, the group's markers holding nothing between them.
 */
struct FDreamUICompositeDesc
{
	enum class ERole : uint8
	{
		Group,
		ClipBase,
		ClipBody,
	};
	ERole Role = ERole::Group;
	/** Which group a closing marker closes; a ClipBody has its ClipBase's. */
	uint32 Id = 0;
	EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;
	float Opacity = 1.0f;
	/** What is drawn inside, in world space: the texture covers it, on the target, and no more. Invalid covers the view. */
	FBox WorldBounds = FBox(ForceInit);
	/** The group's canvas to world: its mask is measured on the canvas. */
	FMatrix CanvasToWorld = FMatrix::Identity;
	/** The mask's row of the clip data texture (DreamUIClip.ush), 0 for none. */
	float MaskClipCoordinate = 0.0f;
	/**
	 * The clip data texture and the mask's textures, as a built-in draw names and binds them: ClipDataTexture, MaskTexture0
	 * and MaskTexture1 alone are read.
	 */
	FDreamUIBuiltInDrawParams Textures;
	/**
	 * The group's texture is Textures.MainTexture, drawn already, premultiplied: it is put down over the rect of the canvas
	 * CanvasToUV maps onto 0..1 (canvas position to texture coordinate, in xy), and nothing is drawn into it here.
	 */
	bool bExternal = false;
	FMatrix44f CanvasToUV = FMatrix44f::Identity;

	bool HasMask() const { return MaskClipCoordinate > 0.5f; }
	/** Whether the closing marker reads what is behind, rather than leaving the blend to the hardware. */
	bool ReadsBackdrop() const { return DreamUIBlend::ReadsBackdrop(BlendMode); }
};

/** The arithmetic of putting a group down, which is all decided before anything is drawn: see FDreamUICompositeStack. */
struct DREAMGUIRENDERER_API FDreamUICompositor
{
	/**
	 * The pixels of the target InWorldBox covers through InWorldToClip, drawn into InViewRect: whole pixels, a pixel of room
	 * all round for antialiasing, within InViewRect. The whole of InViewRect where the box is invalid or reaches behind the
	 * eye; an empty rect where none of it is in view.
	 */
	static FIntRect ProjectBounds(const FBox& InWorldBox, const FMatrix& InWorldToClip, const FIntRect& InViewRect);
};

/**
 * The targets one recording of the UI draws into while groups are open: the view's target at the bottom, a group's
 * texture above it for each group open, in the order they opened. Render thread, for one RDG recording.
 *
 * Every texture is in the bottom target's pixels shifted by its Origin: a pass drawing into a group's texture draws through
 * the view's rect moved by minus the texture's origin, and lands where it would have landed on the target.
 */
class DREAMGUIRENDERER_API FDreamUICompositeStack
{
public:
	struct FFrame
	{
		FRDGTextureRef Texture = nullptr;
		/** The same texture as the RHI sees it, for the post processes, which take one. */
		FTextureRHIRef TextureRHI;
		/** The bottom target's pixel at this texture's first texel. */
		FIntPoint Origin = FIntPoint::ZeroValue;
		/** What it covers, in the bottom target's pixels. */
		FIntRect Rect;
		uint8 NumSamples = 1;
		/** Nothing of it is in view: what is drawn inside is not drawn at all. */
		bool bCulled = false;
		FDreamUICompositeDesc Desc;
		/** A ClipBase put down, held for the ClipBody that follows it: the texture and its origin. */
		FRDGTextureRef HeldBase = nullptr;
		FIntPoint HeldBaseOrigin = FIntPoint::ZeroValue;
		uint32 HeldBaseId = 0;
		bool bHeldBaseCulled = false;
		/** The pool's element the texture is, held until the graph has run. */
		TRefCountPtr<IPooledRenderTarget> Pooled;
	};

	/**
	 * InTarget is what the view draws into, InViewRect where. A viewport's picture (bInOpaqueBackdrop) has an alpha that
	 * says nothing, and counts as opaque to what blends over it; a linear target (bInLinearTarget) has its colours encoded
	 * for the blend.
	 */
	FDreamUICompositeStack(FRDGBuilder& InGraphBuilder, class FDreamUIRenderer* InRenderer, FRDGTextureRef InTarget, FTextureRHIRef InTargetRHI,
		const FIntRect& InViewRect, uint8 InNumSamples, const FMatrix& InWorldToClip, bool bInOpaqueBackdrop, bool bInLinearTarget,
		ERHIFeatureLevel::Type InFeatureLevel);

	const FFrame& Current() const { return Frames.Last(); }
	bool IsAtBottom() const { return Frames.Num() == 1; }
	/** Whether anything drawn now is drawn: not inside a group that is out of view, or one drawn already. */
	bool IsDrawing() const { return !Frames.Last().bCulled && Frames.Last().Texture != nullptr; }
	/** The view's rect, as a pass drawing into the current texture sets its viewport. */
	FIntRect GetCurrentViewport() const { return ViewRect - Current().Origin; }
	const FIntRect& GetViewRect() const { return ViewRect; }
	/** Whether what is behind the current texture is a viewport's picture, opaque whatever its alpha says. */
	bool IsBackdropOpaque() const { return bOpaqueBackdrop && IsAtBottom(); }
	/** A post process's view texture scale and offset, from InBottom, the bottom target's, for the current texture. */
	FVector4f GetCurrentTextureScaleOffset(const FVector4f& InBottom) const;

	/** Opens a group: what is drawn until its closing marker goes into a texture of its own. */
	void Begin(const FDreamUICompositeDesc& InDesc);
	/** Closes the innermost group with InId, and any opened inside it and left open, putting each down in turn. */
	void End(uint32 InId);
	/** Closes every group still open: the recording ends. */
	void EndAll();

	/**
	 * What the current texture holds under InRect (the bottom target's pixels, clipped to the texture), copied for a draw to
	 * blend over: the copy, and the pixel of the current texture its first texel was taken from. Null for an empty rect.
	 */
	FRDGTextureRef CopyBackdrop(const FIntRect& InRect, FIntPoint& OutOrigin, const TCHAR* InName);

	/** How many groups were put down in the recording: for the frame's counters. */
	int32 GetCompositeCount() const { return CompositeCount; }

private:
	/** Puts the top frame down onto the one under it and pops it. */
	void PopAndComposite();
	/** What InFrame holds under InRect (the bottom target's pixels): see CopyBackdrop. */
	FRDGTextureRef CopyFrom(const FFrame& InFrame, const FIntRect& InRect, FIntPoint& OutOrigin, const TCHAR* InName);

	FRDGBuilder& GraphBuilder;
	/** What resolves a multisampled target, for a copy of it. */
	class FDreamUIRenderer* Renderer = nullptr;
	TArray<FFrame, TInlineAllocator<4>> Frames;
	FIntRect ViewRect;
	FMatrix WorldToClip;
	bool bOpaqueBackdrop = false;
	bool bLinearTarget = false;
	ERHIFeatureLevel::Type FeatureLevel;
	int32 CompositeCount = 0;
};
