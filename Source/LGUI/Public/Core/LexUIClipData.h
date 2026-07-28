// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class ULexWidget;
class ULexUIDataAsTexture;

class FLexUIClipData
{
public:
	static int InheritClipDepth;//max intersection clip bounds count, this must be the same as LWidgetClipData.ush LWidget_Clip_Depth
	static int BlockSizeInBytes;
	static int SingleBlockSizeInBytes;
	
	FLexUIClipData(const TSharedPtr<FLexUIClipData>& InParent, ULexUIDataAsTexture* InDataTexture, ULexWidget* InWidget);
	~FLexUIClipData();
	int GetBufferStartPos() const{return BufferStartPos;}
	ULexWidget* GetWidget() const{return Widget.Get();}
	/**
	 * Recompute this clip rectangle from the owner's current world transform and upload it if it changed.
	 *
	 * Deliberately has no dirty flag. A clip rectangle depends on the owner's *world* transform, so it is
	 * invalidated by anything that moves any ancestor — layout, anchors, canvas rescale, hierarchy changes — none
	 * of which know this clip exists. Every code path that forgot to mark it silently left the shader clipping
	 * against a stale rectangle, culling the whole subtree while all CPU-side state still read healthy. So this
	 * is derived per tick and diffed instead, the way UGUI's RectMask2D recomputes in ClipperRegistry.Cull.
	 * The diff means an unchanged clip costs one matrix build and a memcmp, with no GPU upload.
	 */
	LGUI_API void UpdateData();
	LGUI_API bool IsPointVisible(const FVector& WorldPoint)const;
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
	static LGUI_API FMatrix44d GetClipperToWorldMatrix(const ULexWidget* InWidget);
	/** World point in the clipper's own local frame, through the same derivation the GPU uses. */
	static LGUI_API FVector WorldPointToClipperLocal(const ULexWidget* InWidget, const FVector& InWorldPoint);
private:
	static void Add2DTranslationToMatrix(FMatrix44d& Matrix, const FVector2d& Translation);
	bool IsPointVisible_CheckCornerRadius(const FVector2D& InLocalHitPoint, ULexWidget* InWidget)const;
	/** Last block handed to the data texture; used to skip redundant uploads. Empty until the first upload. */
	TArray<uint8> LastUploadedBlock;
	int BufferStartPos;
	TWeakObjectPtr<ULexWidget> Widget;
	TWeakObjectPtr<ULexUIDataAsTexture> DataTexture;
	TWeakPtr<FLexUIClipData> Parent;
};
