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
	TextureDynamic->CompressionSettings = TC_HDR_F32;
	TextureDynamic->LODGroup = TEXTUREGROUP_UI;
	TextureDynamic->Init(TextureWidth, TextureHeight, EPixelFormat::PF_A32B32G32R32F, false);
	if (TextureDynamic->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)TextureDynamic->GetResource();
		ENQUEUE_RENDER_COMMAND(FLWidgetDataAsTexture_ZeroMemory)(
			[TextureRes, Width = TextureWidth, Height = TextureHeight](FRHICommandListImmediate& RHICmdList)
			{
				uint8* Data = new uint8[Width * Height * 16];
				FMemory::Memzero(Data, Width * Height * 16);
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height),
					16 * Width,
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
		ENQUEUE_RENDER_COMMAND(FLGUIProceduralRectUpdateAndCopyDataTexture)(
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

void ULexUIDataAsTexture::Init(int InBlockSizeInByte, int InInitialTextureSize)
{
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;
	BlockSizeInByte = InBlockSizeInByte;
	BlockPixelCount = BlockSizeInByte / 16 + ((BlockSizeInByte % 16) > 0 ? 1 : 0);
	TextureWidth = InInitialTextureSize;
	while (BlockPixelCount > TextureWidth)
	{
		TextureWidth *= 2;
	}
	TextureHeight = InInitialTextureSize;
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
void ULexUIDataAsTexture::UpdateBlock(int InPosition, uint8* InData)
{
	if (Texture->GetResource())
	{
		auto TextureRes = (FTexture2DDynamicResource*)Texture->GetResource();
		ENQUEUE_RENDER_COMMAND(FLWidgetDataAsTexture_UpdateBlock)(
			[TextureRes, InPosition, InData, BlockSizeInByte = this->BlockSizeInByte, BlockPixelCount = this->BlockPixelCount](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.UpdateTexture2D(
					TextureRes->GetTexture2DRHI(),
					0,
					FUpdateTextureRegion2D(0, InPosition, 0, 0, BlockPixelCount, 1),
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
