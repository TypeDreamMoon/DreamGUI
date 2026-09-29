// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "DreamUICaptureLibrary.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "Driver/DreamDriverRig.h"

/*
 * The capture library's own arithmetic, without a GPU: what it writes is a PNG whose pixels are the ones it was
 * given, where it writes is decided the same way whatever the caller passes, and what it refuses it says why.
 * Reading a picture back from a target or a viewport needs a real RHI; the gallery and the play-session picture
 * test cover that.
 */
namespace DreamCaptureLibraryTestLocal
{
	/** A file of its own under the capture directory, removed again when this goes. */
	struct FScopedCaptureFile
	{
		FString Path;
		explicit FScopedCaptureFile(const TCHAR* InStem)
			: Path(UDreamUICaptureLibrary::ResolveCapturePath(FString::Printf(TEXT("AutomationTests/%s_%s.png"), InStem, *FGuid::NewGuid().ToString(EGuidFormats::Digits))))
		{
		}
		~FScopedCaptureFile()
		{
			IFileManager::Get().Delete(*Path, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
		}
	};

	/** The PNG at InPath decoded to BGRA, which is FColor's own layout. */
	bool Decode(const FString& InPath, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		TArray<uint8> Compressed;
		if (!FFileHelper::LoadFileToArray(Compressed, *InPath))
		{
			return false;
		}
		IImageWrapperModule& ImageWrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = ImageWrappers.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		OutSize = FIntPoint(static_cast<int32>(Wrapper->GetWidth()), static_cast<int32>(Wrapper->GetHeight()));
		OutPixels.SetNumUninitialized(OutSize.X * OutSize.Y);
		if (Raw.Num() < static_cast<int64>(OutPixels.Num()) * 4)
		{
			return false;
		}
		FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), OutPixels.Num() * sizeof(FColor));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCapturePngRoundTripTest,
	"DreamGUI.Capture.PixelsSavedAsAPngReadBackAsTheSamePixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCapturePngRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamCaptureLibraryTestLocal;
	// Not square, so that rows and columns cannot be swapped unseen, and with alpha, which a render target's picture
	// keeps: what nothing was drawn over is transparent in the file.
	const FIntPoint Size(7, 5);
	TArray<FColor> Pixels;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			Pixels.Add(FColor(static_cast<uint8>(X * 36), static_cast<uint8>(Y * 60), static_cast<uint8>(255 - X * 30), static_cast<uint8>(X == 0 ? 0 : 255 - Y * 40)));
		}
	}
	const FScopedCaptureFile File(TEXT("RoundTrip"));
	if (!TestTrue(TEXT("A 7x5 picture is written"), UDreamUICaptureLibrary::SavePixelsToPng(Pixels, Size, File.Path)))
	{
		return false;
	}
	TArray<FColor> Read;
	FIntPoint ReadSize = FIntPoint::ZeroValue;
	if (!TestTrue(TEXT("...as a PNG that decodes"), Decode(File.Path, Read, ReadSize)))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("...at the size it was given (%dx%d)"), ReadSize.X, ReadSize.Y), ReadSize == Size);
	TestTrue(TEXT("...with every pixel, alpha included, as it was given, top row first"), Read == Pixels);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCapturePathTest,
	"DreamGUI.Capture.ARelativePathIsTakenFromTheCaptureDirectoryAndMadeAPng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCapturePathTest::RunTest(const FString& Parameters)
{
	const FString Directory = UDreamUICaptureLibrary::GetCaptureDirectory();
	TestTrue(TEXT("The capture directory is under the project's Saved directory"),
		FPaths::IsUnderDirectory(Directory, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));
	TestEqual(TEXT("A relative path without an extension lands in the capture directory as a PNG"),
		UDreamUICaptureLibrary::ResolveCapturePath(TEXT("Menu/Main")), FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("Menu/Main.png"))));
	const FString Absolute = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Elsewhere.png")));
	TestEqual(TEXT("An absolute path with an extension is left as it is"), UDreamUICaptureLibrary::ResolveCapturePath(Absolute), Absolute);
	TestTrue(TEXT("No path at all still names a PNG in the capture directory"),
		FPaths::IsUnderDirectory(UDreamUICaptureLibrary::ResolveCapturePath(FString()), Directory)
		&& FPaths::GetExtension(UDreamUICaptureLibrary::ResolveCapturePath(FString())) == TEXT("png"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCaptureScreenCanvasTest,
	"DreamGUI.Capture.ACanvasDrawnOnScreenHasNoPictureOfItsOwnToSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCaptureScreenCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamCaptureLibraryTestLocal;
	// A screen-space canvas is part of its viewport's picture; asked for one of its own, the library says so and
	// writes nothing, rather than saving whatever target the canvas once had.
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(640, 360));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamCanvas* Canvas = Rig.RootCanvas();
	if (!TestNotNull(TEXT("The rig has a root canvas"), Canvas))
	{
		return false;
	}
	TestTrue(TEXT("...which renders to the screen, not into a target"), Canvas->GetActualRenderMode() != EDreamRenderMode::RenderTarget);
	const FScopedCaptureFile File(TEXT("ScreenCanvas"));
	AddExpectedMessagePlain(TEXT("does not render into a target of its own"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Saving the canvas's picture is refused"), UDreamUICaptureLibrary::SaveCanvasToPng(Canvas, File.Path));
	TestFalse(TEXT("...and no file is written"), FPaths::FileExists(File.Path));
	return true;
}

#endif
