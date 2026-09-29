// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture.h"
#include "Core/DreamUIDataTexture.h"
#include "DreamUIDataAsTexture.generated.h"

UENUM(BlueprintType)
enum class EDreamUIDataAsTexturePixelFormat:uint8
{
	R8,
	R16,
	R32,
	R8G8B8A8,
	R16G16B16A16,
	R32G32B32A32,
};
/**
 * Rows of a texture handed out as blocks: a canvas's widget data, its clip data, the rect blocks' data. The rows
 * live in a UDreamUIDataTexture, which grows in place when they run out, so the texture this hands out is the same
 * object for as long as this lives, and whatever binds it follows every growth.
 */
UCLASS(ClassGroup = (DreamUI), BlueprintType)
class DREAMGUI_API UDreamUIDataAsTexture :public UDataAsset
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange)override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	virtual void BeginDestroy() override;
	/** A play session's copy of the world never holds one of these: see DreamUI::ReportCopiedIntoPlaySession. */
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
private:
	/**
	 * Texture to fill buffer data, and decode to buffer in shader. Made at run time in the transient
	 * package, and never saved, duplicated or copied: a copy would carry none of its size (see CreateTexture).
	 */
	UPROPERTY(VisibleAnywhere, Transient, DuplicateTransient, TextExportTransient, Category = "DreamUI")
	TObjectPtr<UTexture> Texture = nullptr;

	EDreamUIDataAsTexturePixelFormat PixelFormat = EDreamUIDataAsTexturePixelFormat::R8;
	int BytesPerPixel = 4;
	//how many bytes in single block
	int BlockSizeInByte = 4;
	//how many pixels in single block
	int BlockPixelCount = 1;

	int TextureWidth = 1;
	int TextureHeight = 1;
	//Pixel position
	int CurrentPosition = 0;
	bool bIsInitialized = false;
	TArray<int> NotUsingPositionArray;
	/** The writes of a batch, sent together when it is flushed. */
	TArray<FDreamUIDataTextureUpdate> PendingUpdates;
	bool bBatchUpdateMode = false;

	void CreateTexture();
	bool ExpandTexture();
protected:
	virtual void PostInitProperties()override;
public:
	/**
	 * Initialize this buffer.
	 * @param InBlockSizeInByte byte count
	 * @param InInitialTextureHeight texture size when first create it
	 */
	void Init(int InBlockSizeInByte, EDreamUIDataAsTexturePixelFormat InPixelFormat, int InInitialTextureHeight = 32);
	int GetBlockSizeInByte()const { return BlockSizeInByte; }
	/** Rows the texture has now; it grows as rows are handed out. */
	int GetTextureHeight()const { return TextureHeight; }
	/**
	 * Request a new block area with initialize block size.
	 * @return Start position in texture's data
	 */
	int RegisterBuffer();
	void UnregisterBuffer(int InPosition);
	void UpdateBlock(int InPositionY, TArray<uint8> InData);
	void UpdateBlock(int InPositionX, int InPositionY, TArray<uint8> InData, int InDataPixelCount);

	bool GetIsBatchUpdateMode()const { return bBatchUpdateMode; }
	void PrepareForBatchUpdate();
	void Flush();

	/** The same texture for as long as this lives: it grows in place (see UDreamUIDataTexture). */
	UTexture* GetDataTexture()const { return Texture; }
};
