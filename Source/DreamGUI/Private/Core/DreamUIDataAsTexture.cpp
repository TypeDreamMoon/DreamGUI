// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIDetailTrace.h"
#include "DreamGUI.h"
#include "Core/DreamUIDataTexture.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Utils/DreamUIUtils.h"
#include "RHIGlobals.h"
#include "DreamUIRender/DreamUIRenderStats.h"

#define LOCTEXT_NAMESPACE "LWidgetDataAsTexture"

#if WITH_EDITOR
void UDreamUIDataAsTexture::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}
void UDreamUIDataAsTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamUIDataAsTexture::BeginDestroy()
{
	Super::BeginDestroy();
}
void UDreamUIDataAsTexture::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::PIE)
	{
		DreamUI::ReportCopiedIntoPlaySession(*this);
	}
}

void UDreamUIDataAsTexture::CreateTexture()
{
	// The texture keeps its size and format outside its properties, so a copy of it -- a play-in-editor duplication
	// of the world, a Duplicate, a copy that reached it -- has none, and makes no resource (see UDreamUIDataTexture).
	// It lives in the transient package, outside every world a duplication starts from, and carries the flags that keep
	// it out of a duplication, a copy and a save even when something reaches it by reference.
	if (!ensureMsgf(TextureWidth > 0 && TextureHeight > 0, TEXT("%s: refusing to create a %dx%d data texture."), *GetPathName(), TextureWidth, TextureHeight))
	{
		return;
	}
	EPixelFormat GraphicPixelFormat;
	TextureCompressionSettings Compression;
	switch (PixelFormat)
	{
	default:
	case EDreamUIDataAsTexturePixelFormat::R8:
		Compression = TC_Grayscale;
		GraphicPixelFormat = PF_R8;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16:
		Compression = TC_HalfFloat;
		GraphicPixelFormat = PF_R16F;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32:
		Compression = TC_SingleFloat;
		GraphicPixelFormat = PF_R32_FLOAT;
		break;
	case EDreamUIDataAsTexturePixelFormat::R8G8B8A8:
		Compression = TC_VectorDisplacementmap;
		GraphicPixelFormat = PF_R8G8B8A8;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16G16B16A16:
		Compression = TC_HDR;
		GraphicPixelFormat = PF_A16B16G16R16;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32G32B32A32:
		Compression = TC_HDR_F32;
		GraphicPixelFormat = PF_A32B32G32R32F;
		break;
	}
	UPackage* TransientPackage = GetTransientPackage();
	UDreamUIDataTexture* DataTexture = NewObject<UDreamUIDataTexture>(
		TransientPackage,
		MakeUniqueObjectName(TransientPackage, UDreamUIDataTexture::StaticClass(), FName(TEXT("DreamUIDataTexture"))),
		DreamUI::RuntimeObjectFlags);
	DataTexture->LODGroup = TEXTUREGROUP_UI;
	// What a material's sampler expects of the texture: the same answer the texture this replaced gave.
	DataTexture->CompressionSettings = Compression;
	DataTexture->Initialize(TextureWidth, TextureHeight, GraphicPixelFormat, BytesPerPixel);
	Texture = DataTexture;
}
bool UDreamUIDataAsTexture::ExpandTexture()
{
	const uint32 NewTextureHeight = static_cast<uint32>(TextureHeight) * 2;
	if (NewTextureHeight > GetMax2DTextureDimension())
	{
		auto WarningMsg = FText::Format(LOCTEXT("BufferTexture_Size_Error", "{0} Trying to expand buffer texture, result too large size that not supported! Maximum texture size is:{1}.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
			, GetMax2DTextureDimension());
		UE_LOG(DreamGUI, Error, TEXT("%s"), *WarningMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(WarningMsg, false);
#endif
		return false;
	}
	const int32 OldTextureHeight = TextureHeight;
	TextureHeight = static_cast<int32>(NewTextureHeight);
	// Grown in place: every row stays where it was, and whatever samples the texture -- a material instance, pooled
	// or not, a built-in draw on the render thread -- binds its reference, which the growth points at the taller one.
	// Nothing is told, because nothing has to be.
	if (UDreamUIDataTexture* DataTexture = Cast<UDreamUIDataTexture>(Texture))
	{
		DataTexture->Grow(TextureHeight);
	}
	// set start position to bottom
	CurrentPosition = OldTextureHeight;
	return true;
}

void UDreamUIDataAsTexture::Init(int InBlockSizeInByte, EDreamUIDataAsTexturePixelFormat InPixelFormat, int InInitialTextureHeight)
{
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;
	BlockSizeInByte = InBlockSizeInByte;
	PixelFormat = InPixelFormat;
	switch (PixelFormat)
	{
	case EDreamUIDataAsTexturePixelFormat::R8:
		BytesPerPixel = 1;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16:
		BytesPerPixel = 2;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32:
		BytesPerPixel = 4;
		break;
	case EDreamUIDataAsTexturePixelFormat::R8G8B8A8:
		BytesPerPixel = 4;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16G16B16A16:
		BytesPerPixel = 8;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32G32B32A32:
		BytesPerPixel = 16;
		break;
	}
	BlockPixelCount = BlockSizeInByte / BytesPerPixel + ((BlockSizeInByte % BytesPerPixel) > 0 ? 1 : 0);
	TextureWidth = FDreamUIUtils::CeilPowerOfTwo(BlockPixelCount);
	while (BlockPixelCount > TextureWidth)
	{
		TextureWidth *= 2;
	}
	TextureHeight = InInitialTextureHeight;
	CreateTexture();
}

int UDreamUIDataAsTexture::RegisterBuffer()
{
	if (NotUsingPositionArray.Num() > 0)
	{
		auto Pos = NotUsingPositionArray[0];
		NotUsingPositionArray.RemoveAtSwap(0);
		return Pos;
	}
	const auto PrevPos = CurrentPosition;
	CurrentPosition += 1;
	if (CurrentPosition >= TextureHeight)//the next caller would run off the end, so grow now
	{
		/**
		 * The row that was just claimed is inside the texture either way -- the growth here is for the
		 * caller after this one. Recursing instead (which is what used to happen) threw PrevPos away
		 * and returned the row above it, so every expansion permanently lost one row: never handed
		 * out, never on the free list.
		 */
		if (!ExpandTexture())
		{
			//the texture is already at the platform maximum (ExpandTexture has logged that). Stay on
			//the last row rather than walking past the end of it: rows handed out from here overlap,
			//which draws wrong, where an out-of-range row writes outside the texture.
			CurrentPosition = TextureHeight - 1;
		}
	}
	return PrevPos;
}
void UDreamUIDataAsTexture::UnregisterBuffer(int InPosition)
{
	if (InPosition <= INDEX_NONE)return;
	//a row can only be free once; letting a repeated unregister queue it twice hands the same row to
	//two different owners
	NotUsingPositionArray.AddUnique(InPosition);
}
void UDreamUIDataAsTexture::UpdateBlock(int InPositionY, TArray<uint8> InData)
{
	UpdateBlock(0, InPositionY, MoveTemp(InData), BlockPixelCount);
}

void UDreamUIDataAsTexture::UpdateBlock(int InPositionX, int InPositionY, TArray<uint8> InData, int InDataPixelCount)
{
	FDreamUIDataTextureUpdate Update;
	Update.X = InPositionX;
	Update.Y = InPositionY;
	Update.PixelCount = InDataPixelCount;
	Update.Data = MoveTemp(InData);
	if (bBatchUpdateMode)
	{
		PendingUpdates.Add(MoveTemp(Update));
		return;
	}
	if (UDreamUIDataTexture* DataTexture = Cast<UDreamUIDataTexture>(Texture))
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::DataTextureUpdates, 1);
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::UploadedBytes, Update.Data.Num());
		TArray<FDreamUIDataTextureUpdate> Updates;
		Updates.Add(MoveTemp(Update));
		DataTexture->Upload(MoveTemp(Updates));
	}
}

void UDreamUIDataAsTexture::PrepareForBatchUpdate()
{
	check(!bBatchUpdateMode);
	bBatchUpdateMode = true;
}

void UDreamUIDataAsTexture::Flush()
{
	check(bBatchUpdateMode);
	bBatchUpdateMode = false;
	if (PendingUpdates.Num() <= 0)return;
	DREAMUI_DETAIL_SCOPE(DreamUI_DataTextureFlush);
	if (UDreamUIDataTexture* DataTexture = Cast<UDreamUIDataTexture>(Texture))
	{
		int64 PendingBytes = 0;
		for (const FDreamUIDataTextureUpdate& Pending : PendingUpdates)
		{
			PendingBytes += Pending.Data.Num();
		}
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::DataTextureUpdates, PendingUpdates.Num());
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::UploadedBytes, PendingBytes);
		// One command for the frame's writes, which go up as one texture update per run of rows they touched.
		DataTexture->Upload(MoveTemp(PendingUpdates));
	}
	PendingUpdates.Reset();
}

void UDreamUIDataAsTexture::PostInitProperties()
{
	Super::PostInitProperties();
}

#undef LOCTEXT_NAMESPACE
