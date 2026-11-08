// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Psd/DreamPsdReader.h"
#include "Psd/DreamPsdColor.h"
#include "Misc/Compression.h"

bool DreamPsdPixels::UnpackBits(const uint8* InData, int64 InSize, TArray<uint8>& Out, int64 InExpected)
{
	const int64 Start = Out.Num();
	Out.Reserve(Start + InExpected);
	int64 Pos = 0;
	while (Pos < InSize && Out.Num() - Start < InExpected)
	{
		const int8 N = (int8)InData[Pos++];
		if (N >= 0)
		{
			const int64 Count = int64(N) + 1;
			if (Pos + Count > InSize)
			{
				return false;
			}
			Out.Append(InData + Pos, (int32)Count);
			Pos += Count;
		}
		else if (N != -128)
		{
			const int64 Count = 1 - int64(N);
			if (Pos >= InSize)
			{
				return false;
			}
			const uint8 Value = InData[Pos++];
			for (int64 i = 0; i < Count; ++i)
			{
				Out.Add(Value);
			}
		}
	}
	if (Out.Num() - Start != InExpected)
	{
		// A row that decodes long is cut back; one that decodes short is padded, and both are reported.
		Out.SetNumZeroed((int32)(Start + InExpected));
		return false;
	}
	return true;
}

void DreamPsdPixels::PackBits(const uint8* InData, int32 InSize, TArray<uint8>& Out)
{
	int32 Pos = 0;
	while (Pos < InSize)
	{
		// A run of at least three equal bytes is a repeat; anything else goes out literally, up to 128 at a time.
		int32 Run = 1;
		while (Pos + Run < InSize && Run < 128 && InData[Pos + Run] == InData[Pos])
		{
			++Run;
		}
		if (Run >= 3)
		{
			Out.Add((uint8)(int8)(1 - Run));
			Out.Add(InData[Pos]);
			Pos += Run;
			continue;
		}
		int32 Literal = 0;
		while (Pos + Literal < InSize && Literal < 128)
		{
			if (Pos + Literal + 2 < InSize && InData[Pos + Literal] == InData[Pos + Literal + 1] && InData[Pos + Literal] == InData[Pos + Literal + 2])
			{
				break;
			}
			++Literal;
		}
		Out.Add((uint8)(Literal - 1));
		Out.Append(InData + Pos, Literal);
		Pos += Literal;
	}
}

void DreamPsdPixels::UndoPrediction(uint8* InOutRow, int32 InWidth, int32 InDepth)
{
	if (InDepth == 8)
	{
		for (int32 i = 1; i < InWidth; ++i)
		{
			InOutRow[i] = uint8(InOutRow[i] + InOutRow[i - 1]);
		}
	}
	else if (InDepth == 16)
	{
		uint16 Previous = 0;
		for (int32 i = 0; i < InWidth; ++i)
		{
			uint16 V = (uint16(InOutRow[i * 2]) << 8) | uint16(InOutRow[i * 2 + 1]);
			V = uint16(V + Previous);
			Previous = V;
			InOutRow[i * 2] = uint8(V >> 8);
			InOutRow[i * 2 + 1] = uint8(V);
		}
	}
	else if (InDepth == 32)
	{
		// The row's bytes are delta-coded as one run, and the four bytes of each sample are stored apart: every first
		// byte, then every second... Undo the delta, then put each sample's bytes back together.
		const int32 Bytes = InWidth * 4;
		for (int32 i = 1; i < Bytes; ++i)
		{
			InOutRow[i] = uint8(InOutRow[i] + InOutRow[i - 1]);
		}
		TArray<uint8> Planar(InOutRow, Bytes);
		for (int32 i = 0; i < InWidth; ++i)
		{
			for (int32 b = 0; b < 4; ++b)
			{
				InOutRow[i * 4 + b] = Planar[b * InWidth + i];
			}
		}
	}
}

void DreamPsdPixels::ApplyPrediction(uint8* InOutRow, int32 InWidth, int32 InDepth)
{
	if (InDepth == 8)
	{
		for (int32 i = InWidth - 1; i > 0; --i)
		{
			InOutRow[i] = uint8(InOutRow[i] - InOutRow[i - 1]);
		}
	}
	else if (InDepth == 16)
	{
		for (int32 i = InWidth - 1; i > 0; --i)
		{
			const uint16 V = (uint16(InOutRow[i * 2]) << 8) | uint16(InOutRow[i * 2 + 1]);
			const uint16 P = (uint16(InOutRow[i * 2 - 2]) << 8) | uint16(InOutRow[i * 2 - 1]);
			const uint16 D = uint16(V - P);
			InOutRow[i * 2] = uint8(D >> 8);
			InOutRow[i * 2 + 1] = uint8(D);
		}
	}
	else if (InDepth == 32)
	{
		const int32 Bytes = InWidth * 4;
		TArray<uint8> Interleaved(InOutRow, Bytes);
		for (int32 i = 0; i < InWidth; ++i)
		{
			for (int32 b = 0; b < 4; ++b)
			{
				InOutRow[b * InWidth + i] = Interleaved[i * 4 + b];
			}
		}
		for (int32 i = Bytes - 1; i > 0; --i)
		{
			InOutRow[i] = uint8(InOutRow[i] - InOutRow[i - 1]);
		}
	}
}

bool DreamPsdPixels::DecodePlane(const uint8* InData, int64 InSize, uint16 InCompression, int32 InWidth, int32 InHeight,
	int32 InDepth, bool bInLarge, TArray<uint8>& OutBytes, FString& OutError)
{
	OutBytes.Reset();
	if (InWidth <= 0 || InHeight <= 0)
	{
		return true;
	}
	const int64 RowBytes = InDepth == 1 ? (int64(InWidth) + 7) / 8 : int64(InWidth) * (InDepth / 8);
	const int64 Expected = RowBytes * InHeight;
	if (Expected > int64(MAX_int32))
	{
		OutError = TEXT("a plane larger than this reader holds in memory");
		return false;
	}
	switch (InCompression)
	{
	case 0:
	{
		if (InSize < Expected)
		{
			OutError = FString::Printf(TEXT("raw data of %lld bytes where %lld are needed"), (long long)InSize, (long long)Expected);
			OutBytes.SetNumZeroed((int32)Expected);
			if (InSize > 0)
			{
				FMemory::Memcpy(OutBytes.GetData(), InData, InSize);
			}
			return false;
		}
		OutBytes.Append(InData, (int32)Expected);
		return true;
	}
	case 1:
	{
		const int64 CountBytes = int64(InHeight) * (bInLarge ? 4 : 2);
		if (InSize < CountBytes)
		{
			OutError = TEXT("run-length data shorter than its row counts");
			OutBytes.SetNumZeroed((int32)Expected);
			return false;
		}
		int64 Pos = CountBytes;
		bool bOk = true;
		for (int32 Row = 0; Row < InHeight; ++Row)
		{
			const uint8* C = InData + int64(Row) * (bInLarge ? 4 : 2);
			const int64 Count = bInLarge
				? (int64(C[0]) << 24 | int64(C[1]) << 16 | int64(C[2]) << 8 | int64(C[3]))
				: (int64(C[0]) << 8 | int64(C[1]));
			const int64 Available = FMath::Clamp<int64>(InSize - Pos, 0, Count);
			bOk &= DreamPsdPixels::UnpackBits(InData + Pos, Available, OutBytes, RowBytes) && Available == Count;
			Pos += Count;
		}
		if (!bOk)
		{
			OutError = TEXT("run-length rows that do not decode to the plane's width");
		}
		return bOk;
	}
	case 2:
	case 3:
	{
		OutBytes.SetNumUninitialized((int32)Expected);
		if (!FCompression::UncompressMemory(NAME_Zlib, OutBytes.GetData(), (int32)Expected, InData, (int32)InSize))
		{
			OutError = TEXT("zip data that does not inflate to the plane's size");
			FMemory::Memzero(OutBytes.GetData(), Expected);
			return false;
		}
		if (InCompression == 3)
		{
			for (int32 Row = 0; Row < InHeight; ++Row)
			{
				DreamPsdPixels::UndoPrediction(OutBytes.GetData() + int64(Row) * RowBytes, InWidth, InDepth);
			}
		}
		return true;
	}
	default:
		OutError = FString::Printf(TEXT("compression %u, which the format does not define"), InCompression);
		OutBytes.SetNumZeroed((int32)Expected);
		return false;
	}
}

void DreamPsdPixels::ToEightBit(const TArray<uint8>& InBytes, int32 InDepth, int32 InWidth, int32 InHeight, TArray<uint8>& Out)
{
	const int64 Count = int64(InWidth) * InHeight;
	Out.SetNumZeroed((int32)Count);
	switch (InDepth)
	{
	case 8:
		FMemory::Memcpy(Out.GetData(), InBytes.GetData(), FMath::Min<int64>(Count, InBytes.Num()));
		break;
	case 16:
		for (int64 i = 0; i < Count && i * 2 + 1 < InBytes.Num(); ++i)
		{
			const uint32 V = (uint32(InBytes[i * 2]) << 8) | uint32(InBytes[i * 2 + 1]);
			Out[i] = (uint8)((V * 255 + 32767) / 65535);
		}
		break;
	case 32:
		for (int64 i = 0; i < Count && i * 4 + 3 < InBytes.Num(); ++i)
		{
			const uint32 Bits = (uint32(InBytes[i * 4]) << 24) | (uint32(InBytes[i * 4 + 1]) << 16) | (uint32(InBytes[i * 4 + 2]) << 8) | uint32(InBytes[i * 4 + 3]);
			float V;
			FMemory::Memcpy(&V, &Bits, sizeof(V));
			Out[i] = (uint8)FMath::Clamp(FMath::RoundToInt(DreamPsdColor::LinearToEncoded(V) * 255.0f), 0, 255);
		}
		break;
	case 1:
	{
		const int32 RowBytes = (InWidth + 7) / 8;
		for (int32 y = 0; y < InHeight; ++y)
		{
			for (int32 x = 0; x < InWidth; ++x)
			{
				const int32 Byte = y * RowBytes + x / 8;
				const bool bSet = Byte < InBytes.Num() && (InBytes[Byte] & (0x80 >> (x % 8))) != 0;
				Out[int64(y) * InWidth + x] = bSet ? 0 : 255;
			}
		}
		break;
	}
	default:
		break;
	}
}
