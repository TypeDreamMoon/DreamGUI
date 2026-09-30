// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIRenderLayerTable.h"

#include "CoreGlobals.h"
#include "Core/DreamUIDataTexture.h"
#include "Core/DreamUIRuntimeObject.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "RHIGlobals.h"
#include "UObject/Package.h"

namespace DreamUIRenderLayerTableLocal
{
	/** Rows the texture starts with; it doubles from there. */
	constexpr int32 InitialRows = 64;
	constexpr int32 BytesPerPixel = sizeof(FVector4f);
}

void UDreamUIRenderLayerTable::EnsureTexture()
{
	using namespace DreamUIRenderLayerTableLocal;
	if (Texture != nullptr)
	{
		return;
	}
	// Made as the data textures are (UDreamUIDataAsTexture::CreateTexture): in the transient package, never saved,
	// duplicated or copied, and sized outside its properties.
	UPackage* TransientPackage = GetTransientPackage();
	Texture = NewObject<UDreamUIDataTexture>(TransientPackage,
		MakeUniqueObjectName(TransientPackage, UDreamUIDataTexture::StaticClass(), FName(TEXT("DreamUIRenderLayerTable"))),
		DreamUI::RuntimeObjectFlags);
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->CompressionSettings = TC_HDR_F32;
	Texture->Initialize(PixelsPerRow, InitialRows, PF_A32B32G32R32F, BytesPerPixel);
	Pixels.SetNumZeroed(InitialRows * PixelsPerRow);
	Written.SetNumZeroed(InitialRows);
	// Row 0, no layer's: identity, though no shader reads it.
	NumRowsMade = 1;
	SetRowPixels(0, FMatrix44f::Identity);
	Written[0] = 1;
	bAnyWritten.store(true, std::memory_order_relaxed);
}

int32 UDreamUIRenderLayerTable::AcquireRow()
{
	check(IsInGameThread());
	EnsureTexture();
	int32 Row = 0;
	if (FreeRows.Num() > 0)
	{
		Row = FreeRows.Pop(EAllowShrinking::No);
	}
	else
	{
		const int32 Height = Written.Num();
		if (NumRowsMade >= Height)
		{
			// Grown in place (UDreamUIDataTexture::Grow): every row stays where it is, and whatever binds the texture binds
			// its reference, which the growth points at the taller one.
			const int32 NewHeight = Height * 2;
			if (NewHeight > static_cast<int32>(GetMax2DTextureDimension()))
			{
				return 0;
			}
			Texture->Grow(NewHeight);
			Pixels.SetNumZeroed(NewHeight * PixelsPerRow);
			Written.SetNumZeroed(NewHeight);
		}
		Row = NumRowsMade++;
	}
	++NumRowsInUse;
	WriteRow(Row, FMatrix44f::Identity);
	return Row;
}

void UDreamUIRenderLayerTable::ReleaseRow(int32 InRow)
{
	check(IsInGameThread());
	if (InRow <= 0 || InRow >= NumRowsMade)
	{
		return;
	}
	ReleasedRows.Add({ InRow, GFrameCounter });
	--NumRowsInUse;
}

void UDreamUIRenderLayerTable::SetRowPixels(int32 InRow, const FMatrix44f& InLayerToCanvas)
{
	// A position times the matrix is a row vector times it, so each canvas coordinate is a column of it dotted with
	// (position, 1): the columns are what the shader reads, a pixel each.
	FVector4f* Row = Pixels.GetData() + InRow * PixelsPerRow;
	for (int32 Column = 0; Column < 3; ++Column)
	{
		Row[Column] = FVector4f(InLayerToCanvas.M[0][Column], InLayerToCanvas.M[1][Column], InLayerToCanvas.M[2][Column], InLayerToCanvas.M[3][Column]);
	}
	Row[3] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
}

void UDreamUIRenderLayerTable::WriteRow(int32 InRow, const FMatrix44f& InLayerToCanvas)
{
	if (InRow <= 0 || InRow >= NumRowsMade)
	{
		return;
	}
	SetRowPixels(InRow, InLayerToCanvas);
	Written[InRow] = 1;
	bAnyWritten.store(true, std::memory_order_relaxed);
}

FMatrix44f UDreamUIRenderLayerTable::ReadRow(int32 InRow) const
{
	if (InRow <= 0 || InRow >= NumRowsMade)
	{
		return FMatrix44f::Identity;
	}
	const FVector4f* Row = Pixels.GetData() + InRow * PixelsPerRow;
	FMatrix44f Result;
	for (int32 Column = 0; Column < 3; ++Column)
	{
		Result.M[0][Column] = Row[Column].X;
		Result.M[1][Column] = Row[Column].Y;
		Result.M[2][Column] = Row[Column].Z;
		Result.M[3][Column] = Row[Column].W;
	}
	Result.M[0][3] = 0.0f;
	Result.M[1][3] = 0.0f;
	Result.M[2][3] = 0.0f;
	Result.M[3][3] = 1.0f;
	return Result;
}

bool UDreamUIRenderLayerTable::IsRowWrittenSinceFlush(int32 InRow) const
{
	return InRow > 0 && InRow < NumRowsMade && Written[InRow] != 0;
}

void UDreamUIRenderLayerTable::Flush()
{
	check(IsInGameThread());
	using namespace DreamUIRenderLayerTableLocal;
	// Given back long enough ago: nothing is drawn through them any more, and they are handed out again.
	for (int32 Index = ReleasedRows.Num() - 1; Index >= 0; --Index)
	{
		if (GFrameCounter - ReleasedRows[Index].Frame >= ReleaseDelayFrames)
		{
			FreeRows.Add(ReleasedRows[Index].Row);
			ReleasedRows.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}
	if (!bAnyWritten.exchange(false, std::memory_order_relaxed) || Texture == nullptr)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_FlushRenderLayerTable);
	// A run of rows at a time: the layers a canvas made together hold rows next to each other, so a frame in which they
	// all moved goes up as one update.
	constexpr int32 RowBytes = PixelsPerRow * BytesPerPixel;
	TArray<FDreamUIDataTextureUpdate> Updates;
	int64 Bytes = 0;
	for (int32 Row = 0; Row < NumRowsMade;)
	{
		if (Written[Row] == 0)
		{
			++Row;
			continue;
		}
		int32 End = Row + 1;
		while (End < NumRowsMade && Written[End] != 0)
		{
			++End;
		}
		FDreamUIDataTextureUpdate& Update = Updates.AddDefaulted_GetRef();
		Update.X = 0;
		Update.Y = Row;
		Update.PixelCount = PixelsPerRow;
		Update.Rows = End - Row;
		Update.Data.SetNumUninitialized(Update.Rows * RowBytes);
		FMemory::Memcpy(Update.Data.GetData(), Pixels.GetData() + Row * PixelsPerRow, Update.Rows * RowBytes);
		FMemory::Memzero(Written.GetData() + Row, Update.Rows);
		Bytes += Update.Data.Num();
		Row = End;
	}
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::DataTextureUpdates, Updates.Num());
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::UploadedBytes, Bytes);
	Texture->Upload(MoveTemp(Updates));
}

UTexture* UDreamUIRenderLayerTable::GetTexture() const
{
	return Texture;
}
