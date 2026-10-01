// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIDataTexture.h"

#include "DreamGUI.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"

namespace DreamUIDataTextureLocal
{
	/**
	 * The texture's render-thread half: the RHI texture, and a copy of every pixel in it.
	 *
	 * The copy is what makes both halves of the job cheap. Writes land in it in the order they were made -- a row
	 * written whole and then a pixel of it again comes out as the second write left it -- and the rows they touched go
	 * up as one texture update per unbroken run, where each write used to be an update of its own. And growing is an
	 * upload of the copy into the taller texture, with no copy from texture to texture, and so no transitions around
	 * one.
	 */
	class FDataTextureResource : public FTextureResource
	{
	public:
		FDataTextureResource(FName InName, int32 InWidth, int32 InHeight, EPixelFormat InFormat, int32 InBytesPerPixel)
			: Name(InName)
			, Width(InWidth)
			, Height(InHeight)
			, Format(InFormat)
			, BytesPerPixel(InBytesPerPixel)
		{
		}

		virtual uint32 GetSizeX() const override { return static_cast<uint32>(Width); }
		virtual uint32 GetSizeY() const override { return static_cast<uint32>(Height); }
		virtual FString GetFriendlyName() const override { return Name.ToString(); }

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			// The shaders read these with Load, so no filter ever applies; point and clamp say as much to anything
			// that samples one instead.
			SamplerStateRHI = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Pixels.SetNumZeroed(RowPitch() * Height);
			ReplaceTexture(RHICmdList);
		}

		virtual void ReleaseRHI() override
		{
			if (TextureReferenceRHI.IsValid())
			{
				RHIClearTextureReference(TextureReferenceRHI);
			}
			FTextureResource::ReleaseRHI();
			Pixels.Empty();
		}

		void Grow_RenderThread(FRHICommandListImmediate& RHICmdList, int32 InHeight)
		{
			if (InHeight <= Height)
			{
				return;
			}
			Height = InHeight;
			// Only the new rows are zeroed; the ones there were keep what they hold.
			Pixels.SetNumZeroed(RowPitch() * Height);
			if (TextureRHI.IsValid())
			{
				ReplaceTexture(RHICmdList);
			}
		}

		void Upload_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FDreamUIDataTextureUpdate>& InUpdates)
		{
			if (!TextureRHI.IsValid() || Height <= 0)
			{
				return;
			}
			const int32 Pitch = RowPitch();
			TBitArray<> Written(false, Height);
			for (const FDreamUIDataTextureUpdate& Update : InUpdates)
			{
				if (Update.Y < 0 || Update.Y >= Height || Update.X < 0 || Update.X >= Width || Update.PixelCount <= 0)
				{
					continue;
				}
				const int32 RunBytes = FMath::Min(Update.PixelCount, Width - Update.X) * BytesPerPixel;
				const int32 SourceStride = Update.PixelCount * BytesPerPixel;
				for (int32 Row = 0; Row < FMath::Max(Update.Rows, 1) && Update.Y + Row < Height; ++Row)
				{
					const int32 Bytes = FMath::Min(RunBytes, Update.Data.Num() - Row * SourceStride);
					if (Bytes <= 0)
					{
						break;
					}
					FMemory::Memcpy(Pixels.GetData() + (Update.Y + Row) * Pitch + Update.X * BytesPerPixel, Update.Data.GetData() + Row * SourceStride, Bytes);
					Written[Update.Y + Row] = true;
				}
			}
			for (int32 Row = 0; Row < Height;)
			{
				if (!Written[Row])
				{
					++Row;
					continue;
				}
				int32 End = Row + 1;
				while (End < Height && Written[End])
				{
					++End;
				}
				RHICmdList.UpdateTexture2D(TextureRHI, 0, FUpdateTextureRegion2D(0, Row, 0, 0, Width, End - Row), Pitch, Pixels.GetData() + Row * Pitch);
				Row = End;
			}
		}

	private:
		int32 RowPitch() const
		{
			return Width * BytesPerPixel;
		}

		/**
		 * A texture Height rows tall holding the copy, and the reference pointed at it: every material and every
		 * built-in draw binds the reference, so this is the whole of what a growth has to tell them.
		 */
		void ReplaceTexture(FRHICommandListBase& RHICmdList)
		{
			const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DreamUIDataTexture"), Width, Height, Format)
				.SetFlags(ETextureCreateFlags::ShaderResource)
				.SetInitialState(ERHIAccess::SRVMask);
			FTextureRHIRef Texture = RHICmdList.CreateTexture(Desc);
			Texture->SetName(Name);
			RHICmdList.UpdateTexture2D(Texture, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height), RowPitch(), Pixels.GetData());
			TextureRHI = Texture;
			if (TextureReferenceRHI.IsValid())
			{
				RHICmdList.UpdateTextureReference(TextureReferenceRHI, TextureRHI);
			}
		}

		FName Name;
		int32 Width = 0;
		int32 Height = 0;
		EPixelFormat Format = PF_Unknown;
		int32 BytesPerPixel = 0;
		TArray<uint8> Pixels;
	};

	FDataTextureResource* ResourceOf(UDreamUIDataTexture& InTexture)
	{
		// CreateResource makes nothing else.
		return static_cast<FDataTextureResource*>(InTexture.GetResource());
	}
}

void UDreamUIDataTexture::Initialize(int32 InWidth, int32 InHeight, EPixelFormat InFormat, int32 InBytesPerPixel)
{
	if (Width > 0)
	{
		return;
	}
	if (!ensureMsgf(InWidth > 0 && InHeight > 0 && InBytesPerPixel > 0 && InFormat != PF_Unknown,
		TEXT("%s: refusing to make a %dx%d data texture of %d byte(s) a pixel."), *GetPathName(), InWidth, InHeight, InBytesPerPixel))
	{
		return;
	}
	Width = InWidth;
	Height = InHeight;
	DataFormat = InFormat;
	BytesPerPixel = InBytesPerPixel;
	SRGB = false;
	UpdateResource();
}

void UDreamUIDataTexture::Grow(int32 InHeight)
{
	if (InHeight <= Height)
	{
		return;
	}
	Height = InHeight;
	if (DreamUIDataTextureLocal::FDataTextureResource* Resource = DreamUIDataTextureLocal::ResourceOf(*this))
	{
		ENQUEUE_RENDER_COMMAND(FDreamUIDataTexture_Grow)(
			[Resource, InHeight](FRHICommandListImmediate& RHICmdList)
			{
				Resource->Grow_RenderThread(RHICmdList, InHeight);
			});
	}
}

void UDreamUIDataTexture::Upload(TArray<FDreamUIDataTextureUpdate>&& InUpdates)
{
	if (InUpdates.Num() == 0)
	{
		return;
	}
	if (DreamUIDataTextureLocal::FDataTextureResource* Resource = DreamUIDataTextureLocal::ResourceOf(*this))
	{
		ENQUEUE_RENDER_COMMAND(FDreamUIDataTexture_Upload)(
			[Resource, Updates = MoveTemp(InUpdates)](FRHICommandListImmediate& RHICmdList)
			{
				Resource->Upload_RenderThread(RHICmdList, Updates);
			});
	}
}

FTextureResource* UDreamUIDataTexture::CreateResource()
{
	// A copy that reached this texture has none of its size: it makes nothing, rather than a texture the RHI refuses.
	if (Width <= 0 || Height <= 0 || BytesPerPixel <= 0 || DataFormat == PF_Unknown)
	{
		return nullptr;
	}
	return new DreamUIDataTextureLocal::FDataTextureResource(GetFName(), Width, Height, DataFormat, BytesPerPixel);
}
