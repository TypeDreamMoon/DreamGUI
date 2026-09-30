// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include <atomic>
#include "DreamUIRenderLayerTable.generated.h"

class UDreamUIDataTexture;
class UTexture;

/**
 * Where a world's render layers stand on their canvases, for the GPU: a row a layer, holding the layer's transform to its
 * canvas, in a texture every vertex shader that draws a canvas's batches reads (DreamUIRenderLayer.ush). An element of a
 * layer keeps its vertices relative to the layer, and its widget-property record names the layer's row; so a draw call
 * holds the elements of any number of layers, and a layer that moves writes its row and nothing else.
 *
 * Row 0 is no layer's and is never handed out: a record holding 0 is an element of no layer, which the shaders place where
 * it is without reading the table.
 *
 * Threads: a canvas acquires and releases rows on the game thread, where the table also flushes, once a frame, after the
 * canvases placed their layers (UDreamUIManagerWorldSubsystem::SubmitCanvasDrawCall). In between, a canvas may write and
 * read the rows it holds from any thread, several canvases at once: each row is its own layer's, and nothing else moves
 * the rows while that goes on.
 */
UCLASS(Transient)
class DREAMGUI_API UDreamUIRenderLayerTable : public UObject
{
	GENERATED_BODY()

public:
	/** Pixels on a row: the three columns of a layer's 3x4 transform and one unused, for a texture as wide as a power of two. */
	static constexpr int32 PixelsPerRow = 4;
	/** Frames a released row waits before it is handed out again: long enough for what was drawn through it to be transformed out. */
	static constexpr uint64 ReleaseDelayFrames = 8;

	/**
	 * A row for a layer, holding identity until it is written; 0 when the table cannot grow any more. Game thread, outside
	 * the writing between two flushes.
	 */
	int32 AcquireRow();
	/**
	 * InRow's layer is one no longer. What is drawn through it is transformed out of it at its canvas's next update, which
	 * is why the row is handed out again only ReleaseDelayFrames later, and keeps what it holds until then. Game thread.
	 */
	void ReleaseRow(int32 InRow);
	/** InRow's layer stands at InLayerToCanvas now: its row is sent up at the next flush. Any thread, for a row the caller holds. */
	void WriteRow(int32 InRow, const FMatrix44f& InLayerToCanvas);
	/** What InRow holds: identity for row 0 and for a row out of range. Any thread, for a row the caller holds. */
	FMatrix44f ReadRow(int32 InRow) const;
	/** Whether InRow was written since the last flush. Any thread, for a row the caller holds. */
	bool IsRowWrittenSinceFlush(int32 InRow) const;
	/** The rows written since the last flush sent up, a run of rows at a time, and the releases that waited long enough done. Game thread. */
	void Flush();

	/** The texture the shaders read. The same one for as long as the table lives: it grows in place. */
	UTexture* GetTexture() const;
	/** Rows held by layers now, row 0 left out. */
	int32 GetNumRowsInUse() const { return NumRowsInUse; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UDreamUIDataTexture> Texture;
	/** The rows as the texture is to hold them, PixelsPerRow pixels each: what is sent up, and what ReadRow answers. */
	TArray<FVector4f> Pixels;
	/** One a row: written since the last flush. Bytes, not bits, so that two threads writing their own rows touch no shared word. */
	TArray<uint8> Written;
	/** Whether anything was written since the last flush; set by whoever writes, from any thread. */
	std::atomic<bool> bAnyWritten{ false };
	/** Rows handed out and given back, to be handed out again. */
	TArray<int32> FreeRows;
	struct FReleasedRow
	{
		int32 Row = 0;
		uint64 Frame = 0;
	};
	/** Rows given back, waiting out ReleaseDelayFrames. */
	TArray<FReleasedRow> ReleasedRows;
	/** Rows ever handed out, row 0 included: the next new row. */
	int32 NumRowsMade = 0;
	int32 NumRowsInUse = 0;

	void EnsureTexture();
	/** InRow's pixels set to InLayerToCanvas. */
	void SetRowPixels(int32 InRow, const FMatrix44f& InLayerToCanvas);
};
