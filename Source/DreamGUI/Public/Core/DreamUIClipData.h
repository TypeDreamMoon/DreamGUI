// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIClipMask.h"

class UDreamWidget;
class UDreamUIDataAsTexture;
class UTexture;

/**
 * One level of a chain of clips, and the row of the clip data texture that holds the chain from it outwards
 * (DreamUIClip.ush says what a row holds): a clipping widget's own rect, with its mask if it has one (FDreamClipMask).
 */
class DREAMGUI_API FDreamUIClipData
{
public:
	static int InheritClipDepth;//max intersection clip bounds count, DreamUIClipLayout::Depth, which DREAMUI_CLIP_DEPTH in DreamUIClip.ush is held to
	/** The bytes a row has room for: every level masked. */
	static int BlockSizeInBytes;
	/** The bytes of a level without a mask. */
	static int SingleBlockSizeInBytes;

	FDreamUIClipData(const TSharedPtr<FDreamUIClipData>& InParent, UDreamUIDataAsTexture* InDataTexture, UDreamWidget* InWidget);
	~FDreamUIClipData();
	int GetBufferStartPos() const{return BufferStartPos;}
	UDreamWidget* GetWidget() const{return Widget.Get();}
	TSharedPtr<FDreamUIClipData> GetParent() const { return Parent.Pin(); }
	/**
	 * Recompute this clip rectangle from the owner's current world transform and upload it if it changed.
	 *
	 * Deliberately has no dirty flag. A clip rectangle depends on the owner's *world* transform, so it is
	 * invalidated by anything that moves any ancestor — layout, anchors, canvas rescale, hierarchy changes — none
	 * of which know this clip exists. Every code path that forgot to mark it silently left the shader clipping
	 * against a stale rectangle, culling the whole subtree while all CPU-side state still read healthy. So this
	 * is derived per tick and diffed instead, the way UGUI's RectMask2D recomputes in ClipperRegistry.Cull.
	 * The diff means an unchanged clip costs one matrix build and a memcmp, with no GPU upload.
	 *
	 * A chain with no mask on any level uploads the row as it was before masks, byte for byte (DreamUIClipLayout::
	 * UnmaskedRowBytes).
	 */
	void UpdateData();
	bool IsPointVisible(const FVector& WorldPoint)const;
	/**
	 * The textures this chain's masks read, in their slots: the outermost texture mask's first. What a draw of anything
	 * this chain clips binds (FDreamUIGeometry::MaskTextures), and what the row's levels say they read from.
	 */
	void GetMaskTextures(FDreamUIMaskTextures& OutTextures) const;
	/**
	 * The frame a clipper masks in. Inside a live Perspective scope the clipper is DRAWN somewhere
	 * other than where layout put it -- and possibly sheared -- while the pixels it must mask were
	 * baked through that same drawn matrix, so the mask has to be built from it too.
	 *
	 * Both halves of clipping go through here on purpose. The GPU builds a canvas-to-clipper matrix
	 * from it and the CPU inverts it to test a hit point, and those two agreeing is the whole
	 * contract: if one followed the drawn clipper and the other did not, content would become
	 * visible but unclickable, or clickable where nothing is drawn. They were previously wrong
	 * together, which is far less harmful than being wrong apart.
	 */
	static FMatrix44d GetClipperToWorldMatrix(const UDreamWidget* InWidget);
	/** World point in the clipper's own local frame, through the same derivation the GPU uses. */
	static FVector WorldPointToClipperLocal(const UDreamWidget* InWidget, const FVector& InWorldPoint);

	/** A level as the row holds it, and what the CPU needs to test a point against it. */
	struct FLevel
	{
		const FDreamUIClipData* Node = nullptr;
		/** The widget whose frame the level is in: the clipper. */
		const UDreamWidget* Frame = nullptr;
		/** The frame's centre to world. */
		FMatrix44d CentreToWorld = FMatrix44d::Identity;
		FVector2f HalfSize = FVector2f::ZeroVector;
		/** In UDreamWidget::ClippingCornerRadius's order. */
		FVector4f CornerRadius = FVector4f::Zero();
		/** The level's mask, when it has one. */
		const FDreamClipMask* Mask = nullptr;
		/** The texture the level reads, and the slot it reads it from (INDEX_NONE for none, or for a third along the chain). */
		UTexture* Texture = nullptr;
		int32 TextureSlot = INDEX_NONE;
		bool IsMasked() const { return Mask != nullptr; }
		/** The level's texels after its matrix and corner radius, as the shader reads them. */
		bool Pack(FVector4f OutMask[DreamUIClipLayout::MaskTexels]) const;
	};
	/** This chain's levels, innermost first, their textures given slots: what UpdateData writes and IsPointVisible tests. */
	void CollectLevels(TArray<FLevel, TInlineAllocator<16>>& OutLevels) const;
private:
	/** This level, or false when it clips nothing: its widget gone. */
	bool DescribeLevel(FLevel& OutLevel) const;
	bool IsPointVisible_CheckCornerRadius(const FVector2D& InLocalHitPoint, UDreamWidget* InWidget)const;
	/** Last block handed to the data texture; used to skip redundant uploads. Empty until the first upload. */
	TArray<uint8> LastUploadedBlock;
	/** The textures the chain read at the last update: a change asks every element it clips to be batched again. */
	FDreamUIMaskTextures LastMaskTextures;
	/** What the last warnings were about, so that a state is warned about once rather than every frame. */
	uint32 LastWarnings = 0;
	int BufferStartPos;
	TWeakObjectPtr<UDreamWidget> Widget;
	TWeakObjectPtr<UDreamUIDataAsTexture> DataTexture;
	TWeakPtr<FDreamUIClipData> Parent;
};
