// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPixelProbe.h"

#include "DreamUICaptureLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
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

FString FDreamPixelProbe::GetCaptureDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Captures")));
}

FString FDreamPixelProbe::GetGoldenDirectory()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("DreamGUI"));
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(Base, TEXT("Source"), TEXT("DreamGUITests"), TEXT("Resources"), TEXT("Golden")));
}

FString FDreamPixelProbe::SaveCapture(const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InName)
{
	const FString Path = FPaths::Combine(GetCaptureDirectory(), InName + TEXT(".png"));
	return UDreamUICaptureLibrary::SavePixelsToPng(InPixels, InSize, Path) ? Path : FString();
}

bool FDreamPixelProbe::LoadPng(const FString& InFilePath, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	OutPixels.Reset();
	OutSize = FIntPoint::ZeroValue;
	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *InFilePath))
	{
		return false;
	}
	IImageWrapperModule& ImageWrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = ImageWrappers.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num())
		|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return false;
	}
	OutSize = FIntPoint(static_cast<int32>(Wrapper->GetWidth()), static_cast<int32>(Wrapper->GetHeight()));
	const int64 PixelCount = static_cast<int64>(OutSize.X) * OutSize.Y;
	if (PixelCount <= 0 || Raw.Num() < PixelCount * 4)
	{
		OutSize = FIntPoint::ZeroValue;
		return false;
	}
	// BGRA bytes are FColor's own layout.
	OutPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
	FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), PixelCount * sizeof(FColor));
	return true;
}

FDreamPixelProbe::FDifference FDreamPixelProbe::Compare(const TArray<FColor>& InActual, FIntPoint InActualSize,
	const TArray<FColor>& InExpected, FIntPoint InExpectedSize, uint8 InTolerance)
{
	using namespace DreamPixelProbeLocal;
	FDifference Result;
	if (InActualSize != InExpectedSize || InActual.Num() < InActualSize.X * InActualSize.Y || InExpected.Num() < InExpectedSize.X * InExpectedSize.Y)
	{
		Result.bSizesDiffer = true;
		return Result;
	}
	for (int32 Y = 0; Y < InActualSize.Y; ++Y)
	{
		for (int32 X = 0; X < InActualSize.X; ++X)
		{
			const FColor& Left = InActual[Y * InActualSize.X + X];
			const FColor& Right = InExpected[Y * InActualSize.X + X];
			const int32 Delta = FMath::Max(FMath::Max(ChannelDelta(Left.R, Right.R), ChannelDelta(Left.G, Right.G)),
				FMath::Max(ChannelDelta(Left.B, Right.B), ChannelDelta(Left.A, Right.A)));
			Result.LargestChannelDelta = FMath::Max(Result.LargestChannelDelta, Delta);
			if (Delta > static_cast<int32>(InTolerance))
			{
				if (Result.DifferingPixels == 0)
				{
					Result.FirstDifferingPixel = FIntPoint(X, Y);
				}
				++Result.DifferingPixels;
			}
		}
	}
	return Result;
}

FString FDreamPixelProbe::SaveDifferenceImage(const TArray<FColor>& InActual, FIntPoint InSize, const TArray<FColor>& InExpected, uint8 InTolerance, const FString& InName)
{
	using namespace DreamPixelProbeLocal;
	if (InSize.X <= 0 || InSize.Y <= 0 || InActual.Num() < InSize.X * InSize.Y || InExpected.Num() < InSize.X * InSize.Y)
	{
		return FString();
	}
	TArray<FColor> Marked;
	Marked.SetNumUninitialized(InSize.X * InSize.Y);
	for (int32 Index = 0; Index < Marked.Num(); ++Index)
	{
		const FColor& Left = InActual[Index];
		const FColor& Right = InExpected[Index];
		const int32 Delta = FMath::Max(FMath::Max(ChannelDelta(Left.R, Right.R), ChannelDelta(Left.G, Right.G)),
			FMath::Max(ChannelDelta(Left.B, Right.B), ChannelDelta(Left.A, Right.A)));
		if (Delta > static_cast<int32>(InTolerance))
		{
			Marked[Index] = FColor(255, 0, 255, 255);
		}
		else
		{
			const uint8 Grey = static_cast<uint8>((static_cast<int32>(Left.R) + Left.G + Left.B) / 9);
			Marked[Index] = FColor(Grey, Grey, Grey, 255);
		}
	}
	return SaveCapture(Marked, InSize, InName + TEXT(".diff"));
}

FString FDreamPixelProbe::DescribeDifference(const FDifference& InDifference, const TArray<FColor>& InActual, FIntPoint InActualSize,
	const TArray<FColor>& InExpected, FIntPoint InExpectedSize, uint8 InTolerance, int32 InAllowed)
{
	if (InDifference.bSizesDiffer)
	{
		return FString::Printf(TEXT("the pictures are %dx%d and %dx%d"), InActualSize.X, InActualSize.Y, InExpectedSize.X, InExpectedSize.Y);
	}
	FString Text = FString::Printf(TEXT("%d of %d pixels differ by more than %d on a channel (at most %d may), the worst by %d"),
		InDifference.DifferingPixels, InActualSize.X * InActualSize.Y, static_cast<int32>(InTolerance), InAllowed, InDifference.LargestChannelDelta);
	const FIntPoint First = InDifference.FirstDifferingPixel;
	if (First.X >= 0 && First.Y >= 0)
	{
		const int32 Index = First.Y * InActualSize.X + First.X;
		Text += FString::Printf(TEXT("; the first at (%d,%d) is %s here and %s in the other"),
			First.X, First.Y, *Describe(InActual[Index]), *Describe(InExpected[Index]));
	}
	return Text;
}

bool FDreamPixelProbe::ExpectMatchesGolden(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize,
	const FString& InName, uint8 InTolerance, double InAllowedFraction)
{
	const FString CapturePath = SaveCapture(InPixels, InSize, InName);
	const FString GoldenPath = FPaths::Combine(GetGoldenDirectory(), InName + TEXT(".png"));
	if (FParse::Param(FCommandLine::Get(), TEXT("DreamGUIWriteGoldens")))
	{
		if (UDreamUICaptureLibrary::SavePixelsToPng(InPixels, InSize, GoldenPath))
		{
			InTest.AddInfo(FString::Printf(TEXT("%s: this run's picture was written over its golden image, %s."), *InName, *GoldenPath));
			return true;
		}
		InTest.AddError(FString::Printf(TEXT("%s: the golden image %s could not be written."), *InName, *GoldenPath));
		return false;
	}
	if (!FPaths::FileExists(GoldenPath))
	{
		InTest.AddWarning(FString::Printf(TEXT("%s has no golden image yet. This run's picture is %s: look at it, and once it is right, copy it to %s."),
			*InName, CapturePath.IsEmpty() ? TEXT("(not written)") : *CapturePath, *GoldenPath));
		return true;
	}
	TArray<FColor> Golden;
	FIntPoint GoldenSize = FIntPoint::ZeroValue;
	if (!LoadPng(GoldenPath, Golden, GoldenSize))
	{
		InTest.AddError(FString::Printf(TEXT("%s: the golden image %s could not be read."), *InName, *GoldenPath));
		return false;
	}
	const FDifference Difference = Compare(InPixels, InSize, Golden, GoldenSize, InTolerance);
	const int32 Allowed = FMath::FloorToInt32(InAllowedFraction * static_cast<double>(InSize.X) * static_cast<double>(InSize.Y));
	if (!Difference.bSizesDiffer && Difference.DifferingPixels <= Allowed)
	{
		return true;
	}
	const FString DiffPath = Difference.bSizesDiffer ? FString() : SaveDifferenceImage(InPixels, InSize, Golden, InTolerance, InName);
	InTest.AddError(FString::Printf(TEXT("%s does not match its golden image: %s. This run: %s; the golden: %s; where they differ: %s."),
		*InName, *DescribeDifference(Difference, InPixels, InSize, Golden, GoldenSize, InTolerance, Allowed),
		CapturePath.IsEmpty() ? TEXT("(not written)") : *CapturePath, *GoldenPath, DiffPath.IsEmpty() ? TEXT("(no image)") : *DiffPath));
	return false;
}

bool FDreamPixelProbe::ExpectPicturesMatch(FAutomationTestBase& InTest, const TArray<FColor>& InLeft, FIntPoint InLeftSize,
	const TArray<FColor>& InRight, FIntPoint InRightSize, const FString& InName, uint8 InTolerance, double InAllowedFraction)
{
	const FDifference Difference = Compare(InLeft, InLeftSize, InRight, InRightSize, InTolerance);
	const int32 Allowed = FMath::FloorToInt32(InAllowedFraction * static_cast<double>(InLeftSize.X) * static_cast<double>(InLeftSize.Y));
	if (!Difference.bSizesDiffer && Difference.DifferingPixels <= Allowed)
	{
		return true;
	}
	const FString DiffPath = Difference.bSizesDiffer ? FString() : SaveDifferenceImage(InLeft, InLeftSize, InRight, InTolerance, InName);
	InTest.AddError(FString::Printf(TEXT("%s: the two pictures do not match: %s. Where they differ: %s."),
		*InName, *DescribeDifference(Difference, InLeft, InLeftSize, InRight, InRightSize, InTolerance, Allowed),
		DiffPath.IsEmpty() ? TEXT("(no image)") : *DiffPath));
	return false;
}
