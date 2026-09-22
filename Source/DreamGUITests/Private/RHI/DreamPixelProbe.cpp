// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPixelProbe.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace DreamPixelProbeLocal
{
	/** One channel's distance, in the width the caller thinks in. */
	static int32 ChannelDelta(uint8 InLeft, uint8 InRight)
	{
		return FMath::Abs(static_cast<int32>(InLeft) - static_cast<int32>(InRight));
	}
}

bool FDreamPixelProbe::ReadBack(UTextureRenderTarget2D* InTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	// Emptied first, so a caller that ignores the return value gets nothing rather than the previous
	// read's pixels -- which would be the same picture as a target that never changed.
	OutPixels.Reset();
	OutSize = FIntPoint::ZeroValue;

	if (!IsValid(InTarget))
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = InTarget->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		// No resource means the target was never handed to the RHI -- which is what a -nullrhi run
		// looks like from here, and is why every test that calls this carries NonNullRHI.
		return false;
	}

	// See the note in the header: the render thread is the one that drew, so it has to be caught up
	// with before its work is readable.
	FlushRenderingCommands();

	// Through the render-target face of the resource. A render target resource inherits from both a
	// texture and a render target, and only one of those two knows how to hand pixels back; naming
	// the base says which one is being asked, and leaves the virtual call to do its work.
	FRenderTarget* Surface = Resource;
	OutSize = Surface->GetSizeXY();
	if (OutSize.X <= 0 || OutSize.Y <= 0)
	{
		return false;
	}
	if (!Surface->ReadPixels(OutPixels))
	{
		return false;
	}
	return OutPixels.Num() >= OutSize.X * OutSize.Y;
}

bool FDreamPixelProbe::ExpectColorAt(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize,
	FIntPoint InPixel, FColor InExpected, uint8 InTolerance, const TCHAR* InWhat)
{
	if (InSize.X <= 0 || InSize.Y <= 0 || InPixels.Num() < InSize.X * InSize.Y)
	{
		InTest.AddError(FString::Printf(
			TEXT("%s: there is nothing to look at -- %d pixels read back for a %dx%d target."),
			InWhat, InPixels.Num(), InSize.X, InSize.Y));
		return false;
	}
	if (InPixel.X < 0 || InPixel.Y < 0 || InPixel.X >= InSize.X || InPixel.Y >= InSize.Y)
	{
		// Out of bounds is a failing assertion and not a silent skip: the coordinate was computed
		// from the widget's own projection, so a coordinate off the image means the widget is not
		// where the test thinks it is -- exactly the thing being tested.
		InTest.AddError(FString::Printf(
			TEXT("%s: pixel (%d,%d) is outside the %dx%d target."),
			InWhat, InPixel.X, InPixel.Y, InSize.X, InSize.Y));
		return false;
	}

	const FColor Actual = InPixels[InPixel.Y * InSize.X + InPixel.X];
	if (!IsNear(Actual, InExpected, InTolerance))
	{
		InTest.AddError(FString::Printf(
			TEXT("%s: pixel (%d,%d) is %s, expected %s within %d per channel."),
			InWhat, InPixel.X, InPixel.Y, *Describe(Actual), *Describe(InExpected), static_cast<int32>(InTolerance)));
		return false;
	}
	return true;
}

int32 FDreamPixelProbe::CountColor(const TArray<FColor>& InPixels, FIntPoint InSize, const FIntRect& InRegion,
	FColor InExpected, uint8 InTolerance)
{
	if (InSize.X <= 0 || InSize.Y <= 0 || InPixels.Num() < InSize.X * InSize.Y)
	{
		return 0;
	}

	const int32 MinX = FMath::Clamp(FMath::Min(InRegion.Min.X, InRegion.Max.X), 0, InSize.X);
	const int32 MaxX = FMath::Clamp(FMath::Max(InRegion.Min.X, InRegion.Max.X), 0, InSize.X);
	const int32 MinY = FMath::Clamp(FMath::Min(InRegion.Min.Y, InRegion.Max.Y), 0, InSize.Y);
	const int32 MaxY = FMath::Clamp(FMath::Max(InRegion.Min.Y, InRegion.Max.Y), 0, InSize.Y);

	int32 Count = 0;
	for (int32 Y = MinY; Y < MaxY; ++Y)
	{
		const int32 RowStart = Y * InSize.X;
		for (int32 X = MinX; X < MaxX; ++X)
		{
			if (IsNear(InPixels[RowStart + X], InExpected, InTolerance))
			{
				++Count;
			}
		}
	}
	return Count;
}

bool FDreamPixelProbe::IsNear(const FColor& InLeft, const FColor& InRight, uint8 InTolerance)
{
	using namespace DreamPixelProbeLocal;
	const int32 Tolerance = static_cast<int32>(InTolerance);
	return ChannelDelta(InLeft.R, InRight.R) <= Tolerance
		&& ChannelDelta(InLeft.G, InRight.G) <= Tolerance
		&& ChannelDelta(InLeft.B, InRight.B) <= Tolerance
		&& ChannelDelta(InLeft.A, InRight.A) <= Tolerance;
}

FString FDreamPixelProbe::Describe(const FColor& InColor)
{
	return FString::Printf(TEXT("(R=%d,G=%d,B=%d,A=%d)"),
		static_cast<int32>(InColor.R), static_cast<int32>(InColor.G),
		static_cast<int32>(InColor.B), static_cast<int32>(InColor.A));
}
