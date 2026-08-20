// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIDataAsTexture.h"
#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "Engine/Texture2DDynamic.h"
#include "RenderingThread.h"

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
void UDreamUIDataAsTexture::CreateTexture()
{
	static int TextureNameSuffix = 0;
	auto TextureDynamic = NewObject<UTexture2DDynamic>(
		this,
		FName(*FString::Printf(TEXT("DreamUIDataAsTexture_%d"), TextureNameSuffix++))
	);
	TextureDynamic->LODGroup = TEXTUREGROUP_UI;
	EPixelFormat GraphicPixelFormat;
	switch (PixelFormat)
	{
	default:
	case EDreamUIDataAsTexturePixelFormat::R8:
		TextureDynamic->CompressionSettings = TC_Grayscale;
		GraphicPixelFormat = PF_R8;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16:
		TextureDynamic->CompressionSettings = TC_HalfFloat;
		GraphicPixelFormat = PF_R16F;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32:
		TextureDynamic->CompressionSettings = TC_SingleFloat;
		GraphicPixelFormat = PF_R32_FLOAT;
		break;
	case EDreamUIDataAsTexturePixelFormat::R8G8B8A8:
		TextureDynamic->CompressionSettings = TC_VectorDisplacementmap;
		GraphicPixelFormat = PF_R8G8B8A8;
		break;
	case EDreamUIDataAsTexturePixelFormat::R16G16B16A16:
		TextureDynamic->CompressionSettings = TC_HDR;
		GraphicPixelFormat = PF_A16B16G16R16;
		break;
	case EDreamUIDataAsTexturePixelFormat::R32G32B32A32:
		TextureDynamic->CompressionSettings = TC_HDR_F32;
		GraphicPixelFormat = PF_A32B32G32R32F;
		break;
	}
	TextureDynamic->SRGB = false;
	TextureDynamic->Init(TextureWidth, TextureHeight, GraphicPixelFormat, false);
	if (TextureDynamic->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)TextureDynamic->GetResource();
		ENQUEUE_RENDER_COMMAND(FDreamUIDataAsTexture_ZeroMemory)(
			[TextureRes, Width = TextureWidth, Height = TextureHeight, BytesPerPixel = BytesPerPixel](FRHICommandListImmediate& RHICmdList)
			{
				TArray<uint8> Data;
				Data.SetNumZeroed(Width * Height * BytesPerPixel);
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height),
					BytesPerPixel * Width,
					Data.GetData()
				);
			});
	}

	Texture = TextureDynamic;
}
bool UDreamUIDataAsTexture::ExpandTexture()
{
	uint32 NewTextureHeight = TextureHeight + TextureHeight;
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
	auto OldTexture = Texture;
	auto OldTextureHeight = TextureHeight;
	TextureHeight = NewTextureHeight;
	CreateTexture();

	//copy existing data
	auto NewTexture = Texture;
	if (OldTexture->GetResource() != nullptr && NewTexture->GetResource() != nullptr)
	{
		ENQUEUE_RENDER_COMMAND(FLFDreamUIDataAsTexture_UpdateAndCopyDataTexture)(
			[OldTexture, NewTexture, Width = TextureWidth, OldTextureHeight](FRHICommandListImmediate& RHICmdList)
			{
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition = FIntVector(0, 0, 0);
				CopyInfo.Size = FIntVector(Width, OldTextureHeight, 0);
				CopyInfo.DestPosition = FIntVector(0, 0, 0);
				RHICmdList.CopyTexture(
					((FTexture2DDynamicResource*)OldTexture->GetResource())->GetTexture2DRHI(),
					((FTexture2DDynamicResource*)NewTexture->GetResource())->GetTexture2DRHI(),
					CopyInfo
				);
				RHIFlushResources();//UE5.8: FRHICommandListImmediate::FlushResources removed; global RHIFlushResources replaces it
			});
	}
	// set start position to bottom
	CurrentPosition = OldTextureHeight;

	OnDataTextureChange.Broadcast(Texture);

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
		NotUsingPositionArray.RemoveSwap(Pos);
		return Pos;
	}
	auto PrevPos = CurrentPosition;
	CurrentPosition += 1;
	if (CurrentPosition >= TextureHeight)//need to expand texture size
	{
		if (ExpandTexture())
		{
			return RegisterBuffer();
		}
	}
	return PrevPos;
}
void UDreamUIDataAsTexture::UnregisterBuffer(int InPosition)
{
	NotUsingPositionArray.Add(InPosition);
}
void UDreamUIDataAsTexture::UpdateBlock(int InPositionY, TArray<uint8> InData)
{
	if (bBatchUpdateMode)
	{
		FPendingUpdateData Data;
		Data.PosX = 0;
		Data.PosY = InPositionY;
		Data.Data = MoveTemp(InData);
		Data.DataPixelCount = this->BlockPixelCount;
		PendingUpdateDataArray.Add(MoveTemp(Data));
	}
	else
	{
		if (IsValid(Texture) && Texture->GetResource())
		{
			auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
			ENQUEUE_RENDER_COMMAND(FDreamUIDataAsTexture_UpdateBlock)(
				[TextureRes, InPositionY, InData = MoveTemp(InData), BlockSizeInByte = this->BlockSizeInByte, BlockPixelCount = this->BlockPixelCount](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.UpdateTexture2D(
						TextureRes->GetTexture2DRHI(),
						0,
						FUpdateTextureRegion2D(0, InPositionY, 0, 0, BlockPixelCount, 1),
						BlockSizeInByte,
						InData.GetData()
					);
				});
		}
	}
}

void UDreamUIDataAsTexture::UpdateBlock(int InPositionX, int InPositionY, TArray<uint8> InData, int InDataPixelCount)
{
	if (bBatchUpdateMode)
	{
		FPendingUpdateData Data;
		Data.PosX = InPositionX;
		Data.PosY = InPositionY;
		Data.Data = MoveTemp(InData);
		Data.DataPixelCount = InDataPixelCount;
		PendingUpdateDataArray.Add(MoveTemp(Data));
	}
	else
	{
		if (IsValid(Texture) && Texture->GetResource())
		{
			auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
			ENQUEUE_RENDER_COMMAND(FDreamUIDataAsTexture_UpdateBlock)(
				[TextureRes, InPositionX, InPositionY, InData = MoveTemp(InData), BlockSizeInByte = this->BlockSizeInByte, InDataPixelCount](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.UpdateTexture2D(
						TextureRes->GetTexture2DRHI(),
						0,
						FUpdateTextureRegion2D(InPositionX, InPositionY, 0, 0, InDataPixelCount, 1),
						BlockSizeInByte,
						InData.GetData()
					);
				});
		}
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
	if (PendingUpdateDataArray.Num() <= 0)return;
	if (IsValid(Texture) && Texture->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
		ENQUEUE_RENDER_COMMAND(FDreamUIDataAsTexture_FlushData)(
			[TextureRes, PendingUpdateDataArray = MoveTemp(PendingUpdateDataArray), BlockSizeInByte = this->BlockSizeInByte](FRHICommandListImmediate& RHICmdList)
			{
				for (auto& PendingUpdateData : PendingUpdateDataArray)
				{
					RHICmdList.UpdateTexture2D(
						TextureRes->GetTexture2DRHI(),
						0,
						FUpdateTextureRegion2D(PendingUpdateData.PosX, PendingUpdateData.PosY, 0, 0, PendingUpdateData.DataPixelCount, 1),
						BlockSizeInByte,
						PendingUpdateData.Data.GetData()
					);
				}
			});
	}
	else
	{
		PendingUpdateDataArray.Reset();
	}
}

void UDreamUIDataAsTexture::PostInitProperties()
{
	Super::PostInitProperties();
}

#undef LOCTEXT_NAMESPACE
