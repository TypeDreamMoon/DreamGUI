// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"

struct DREAMGUIPSD_API FDreamPsdReadOptions
{
	/** Decode every layer's channels (the importer needs them; a test of the records alone may not). */
	bool bLayerPixels = true;
	/** Decode the merged image. */
	bool bComposite = true;
};

/**
 * Reads a layered image document: PSD and PSB; 1, 8, 16 and 32 bits; RGB, grayscale, indexed, CMYK and Lab (the last
 * two converted to sRGB by formula, with a warning); pixels raw, run-length, zip and zip with prediction.
 *
 * Never throws and never reads past the bytes it was given: a block that does not read is skipped with a warning
 * where the format lets the reader find the next one (every tagged block carries its length), and the read fails
 * only when the header, or the section lengths that frame everything after it, do not hold.
 */
namespace DreamPsdReader
{
	DREAMGUIPSD_API bool Read(const TArray<uint8>& InBytes, FDreamPsdDocument& OutDocument, FDreamPsdMessages& OutMessages,
		const FDreamPsdReadOptions& InOptions = FDreamPsdReadOptions());

	DREAMGUIPSD_API bool ReadFile(const FString& InPath, FDreamPsdDocument& OutDocument, FDreamPsdMessages& OutMessages,
		const FDreamPsdReadOptions& InOptions = FDreamPsdReadOptions());

	/** A blend mode key ('mul ', 'norm', 'pass'...) as the editor names it, for messages. */
	DREAMGUIPSD_API FString DescribeBlendKey(const FString& InKey);
}

/** The decoders, public so the tests can hold them to the format on their own. */
namespace DreamPsdPixels
{
	/** PackBits: decode InSize bytes into exactly InExpected bytes. False when the data runs short or long. */
	DREAMGUIPSD_API bool UnpackBits(const uint8* InData, int64 InSize, TArray<uint8>& Out, int64 InExpected);
	DREAMGUIPSD_API void PackBits(const uint8* InData, int32 InSize, TArray<uint8>& Out);

	/**
	 * One plane of InWidth x InHeight samples of InDepth bits, compressed as InCompression (0 raw, 1 run-length, 2 zip,
	 * 3 zip with prediction), into its bytes at the file's depth (big-endian samples). bInLarge: run-length row
	 * counts are 4 bytes (PSB).
	 */
	DREAMGUIPSD_API bool DecodePlane(const uint8* InData, int64 InSize, uint16 InCompression, int32 InWidth, int32 InHeight,
		int32 InDepth, bool bInLarge, TArray<uint8>& OutBytes, FString& OutError);

	/** Samples at the file's depth to 8 bits: 16-bit scaled, 32-bit float (linear) encoded to sRGB, 1-bit 1 = black. */
	DREAMGUIPSD_API void ToEightBit(const TArray<uint8>& InBytes, int32 InDepth, int32 InWidth, int32 InHeight, TArray<uint8>& Out);

	/** Zip with prediction, undone in place on one row of bytes at a depth. */
	DREAMGUIPSD_API void UndoPrediction(uint8* InOutRow, int32 InWidth, int32 InDepth);
	DREAMGUIPSD_API void ApplyPrediction(uint8* InOutRow, int32 InWidth, int32 InDepth);
}
