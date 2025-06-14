// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LexUIDataAsTexture.generated.h"

UCLASS(ClassGroup = (LexUI), BlueprintType)
class LGUI_API ULexUIDataAsTexture :public UDataAsset
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange)override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
	virtual void BeginDestroy() override;
private:
	/** Texture to fill buffer data, and decode to buffer in shader. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LexUI")
	TObjectPtr<UTexture> Texture = nullptr;

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

	void CreateTexture();
	bool ExpandTexture();
protected:
	virtual void PostInitProperties()override;
public:
	/**
	 * Initialize this buffer.
	 * @param InBlockSizeInByte byte count
	 * @param InInitialTextureSize texture size when first create it
	 */
	void Init(int InBlockSizeInByte, int InInitialTextureSize = 32);
	int GetBlockSizeInByte()const { return BlockSizeInByte; }
	/**
	 * Request a new block area with initialize block size.
	 * @return Start position in texture's data
	 */
	int RegisterBuffer();
	void UnregisterBuffer(int InPosition);
	void UpdateBlock(int InPosition, uint8* InData);

	UTexture* GetDataTexture()const { return Texture; }

	DECLARE_EVENT_OneParam(ULexUIDataAsTexture, FOnDataTextureChange, UTexture*);
	FOnDataTextureChange OnDataTextureChange;
};