// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIDataAsTexture.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "Engine/Texture2DDynamic.h"
#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "LWidgetDataAsTexture"

#if WITH_EDITOR
void ULexUIDataAsTexture::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}
void ULexUIDataAsTexture::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexUIDataAsTexture::BeginDestroy()
{
	Super::BeginDestroy();
}
void ULexUIDataAsTexture::CreateTexture()
{
	static int TextureNameSuffix = 0;
	auto TextureDynamic = NewObject<UTexture2DDynamic>(
		this,
		FName(*FString::Printf(TEXT("LexUIDataAsTexture_%d"), TextureNameSuffix++))
	);
	TextureDynamic->LODGroup = TEXTUREGROUP_UI;
	EPixelFormat GraphicPixelFormat;
	switch (PixelFormat)
	{
	default:
	case ELexUIDataAsTexturePixelFormat::R8:
		TextureDynamic->CompressionSettings = TC_Grayscale;
		GraphicPixelFormat = PF_R8;
		break;
	case ELexUIDataAsTexturePixelFormat::R16:
		TextureDynamic->CompressionSettings = TC_HalfFloat;
		GraphicPixelFormat = PF_R16F;
		break;
	case ELexUIDataAsTexturePixelFormat::R32:
		TextureDynamic->CompressionSettings = TC_SingleFloat;
		GraphicPixelFormat = PF_R32_FLOAT;
		break;
	case ELexUIDataAsTexturePixelFormat::R8G8B8A8:
		TextureDynamic->CompressionSettings = TC_VectorDisplacementmap;
		GraphicPixelFormat = PF_R8G8B8A8;
		break;
	case ELexUIDataAsTexturePixelFormat::R16G16B16A16:
		TextureDynamic->CompressionSettings = TC_HDR;
		GraphicPixelFormat = PF_A16B16G16R16;
		break;
	case ELexUIDataAsTexturePixelFormat::R32G32B32A32:
		TextureDynamic->CompressionSettings = TC_HDR_F32;
		GraphicPixelFormat = PF_A32B32G32R32F;
		break;
	}
	TextureDynamic->SRGB = false;
	TextureDynamic->Init(TextureWidth, TextureHeight, GraphicPixelFormat, false);
	if (TextureDynamic->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)TextureDynamic->GetResource();
		ENQUEUE_RENDER_COMMAND(FLexUIDataAsTexture_ZeroMemory)(
			[TextureRes, Width = TextureWidth, Height = TextureHeight, BytesPerPixel = BytesPerPixel](FRHICommandListImmediate& RHICmdList)
			{
				uint8* Data = new uint8[Width * Height * BytesPerPixel];
				FMemory::Memzero(Data, Width * Height * BytesPerPixel);
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height),
					BytesPerPixel * Width,
					Data
				);
				delete Data;
			});
	}

	Texture = TextureDynamic;
}
bool ULexUIDataAsTexture::ExpandTexture()
{
	uint32 NewTextureHeight = TextureHeight + TextureHeight;
	if (NewTextureHeight > GetMax2DTextureDimension())
	{
		auto WarningMsg = FText::Format(LOCTEXT("BufferTexture_Size_Error", "{0} Trying to expand buffer texture, result too large size that not supported! Maximum texture size is:{1}.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
			, GetMax2DTextureDimension());
		UE_LOG(LGUI, Error, TEXT("%s"), *WarningMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(WarningMsg);
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
		ENQUEUE_RENDER_COMMAND(FLFLexUIDataAsTexture_UpdateAndCopyDataTexture)(
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
				RHICmdList.FlushResources();//Flush resource, or the texture will not show correct result
			});
	}
	// set start position to bottom
	CurrentPosition = OldTextureHeight;

	OnDataTextureChange.Broadcast(Texture);

	return true;
}

void ULexUIDataAsTexture::Init(int InBlockSizeInByte, ELexUIDataAsTexturePixelFormat InPixelFormat, int InInitialTextureHeight)
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
	case ELexUIDataAsTexturePixelFormat::R8:
		BytesPerPixel = 1;
		break;
	case ELexUIDataAsTexturePixelFormat::R16:
		BytesPerPixel = 2;
		break;
	case ELexUIDataAsTexturePixelFormat::R32:
		BytesPerPixel = 4;
		break;
	case ELexUIDataAsTexturePixelFormat::R8G8B8A8:
		BytesPerPixel = 4;
		break;
	case ELexUIDataAsTexturePixelFormat::R16G16B16A16:
		BytesPerPixel = 8;
		break;
	case ELexUIDataAsTexturePixelFormat::R32G32B32A32:
		BytesPerPixel = 16;
		break;
	}
	BlockPixelCount = BlockSizeInByte / BytesPerPixel + ((BlockSizeInByte % BytesPerPixel) > 0 ? 1 : 0);
	TextureWidth = FLexUIUtils::CeilPowerOfTwo(BlockPixelCount);
	while (BlockPixelCount > TextureWidth)
	{
		TextureWidth *= 2;
	}
	TextureHeight = InInitialTextureHeight;
	CreateTexture();
}

int ULexUIDataAsTexture::RegisterBuffer()
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
void ULexUIDataAsTexture::UnregisterBuffer(int InPosition)
{
	NotUsingPositionArray.Add(InPosition);
}
void ULexUIDataAsTexture::UpdateBlock(int InPositionY, uint8* InData)
{
	if (Texture->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
		ENQUEUE_RENDER_COMMAND(FLexUIDataAsTexture_UpdateBlock)(
			[TextureRes, InPositionY, InData, BlockSizeInByte = this->BlockSizeInByte, BlockPixelCount = this->BlockPixelCount](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(0, InPositionY, 0, 0, BlockPixelCount, 1),
					BlockSizeInByte,
					InData
				);
				delete InData;
			});
	}
}

void ULexUIDataAsTexture::UpdateBlock(int InPositionX, int InPositionY, uint8* InData, int InDataPixelCount)
{
	if (Texture->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
		ENQUEUE_RENDER_COMMAND(FLexUIDataAsTexture_UpdateBlock)(
			[TextureRes, InPositionX, InPositionY, InData, BlockSizeInByte = this->BlockSizeInByte, InDataPixelCount](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(InPositionX, InPositionY, 0, 0, InDataPixelCount, 1),
					BlockSizeInByte,
					InData
				);
				delete InData;
			});
	}
}

void ULexUIDataAsTexture::PostInitProperties()
{
	Super::PostInitProperties();
}

#undef LOCTEXT_NAMESPACE
