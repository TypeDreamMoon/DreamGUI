// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUICaptureLibrary.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamGUI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

namespace DreamUICaptureLibraryLocal
{
	/** A widget's name as a file name: whatever a file system will not take becomes an underscore. */
	FString ToFileName(const FString& InName)
	{
		const FString Name = FPaths::MakeValidFileName(InName, TEXT('_'));
		return Name.IsEmpty() ? FString(TEXT("Canvas")) : Name;
	}
}

FString UDreamUICaptureLibrary::GetCaptureDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamUI"), TEXT("Captures")));
}

FString UDreamUICaptureLibrary::ResolveCapturePath(const FString& InFilePath)
{
	FString Path = InFilePath.IsEmpty() ? FString(TEXT("Capture")) : InFilePath;
	if (FPaths::IsRelative(Path))
	{
		Path = FPaths::Combine(GetCaptureDirectory(), Path);
	}
	if (FPaths::GetExtension(Path).IsEmpty())
	{
		Path += TEXT(".png");
	}
	return FPaths::ConvertRelativePathToFull(Path);
}

bool UDreamUICaptureLibrary::SavePixelsToPng(const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InFilePath)
{
	if (InSize.X <= 0 || InSize.Y <= 0 || InPixels.Num() < InSize.X * InSize.Y)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d There is no picture to save to %s: %d pixel(s) for a %dx%d image."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InFilePath, InPixels.Num(), InSize.X, InSize.Y);
		return false;
	}
	TArray64<uint8> Compressed;
	FImageUtils::PNGCompressImageArray(InSize.X, InSize.Y, TArrayView64<const FColor>(InPixels.GetData(), static_cast<int64>(InSize.X) * InSize.Y), Compressed);
	if (Compressed.Num() == 0)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d A %dx%d picture could not be made into a PNG."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InSize.X, InSize.Y);
		return false;
	}
	const FString Path = ResolveCapturePath(InFilePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
	if (!FFileHelper::SaveArrayToFile(Compressed, *Path))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s could not be written."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Path);
		return false;
	}
	return true;
}

bool UDreamUICaptureLibrary::ReadRenderTargetPixels(UTextureRenderTarget2D* InTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	OutPixels.Reset();
	OutSize = FIntPoint::ZeroValue;
	if (!IsValid(InTarget))
	{
		return false;
	}
	FTextureRenderTargetResource* Resource = InTarget->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s has no resource to read: nothing has rendered into it, or there is no GPU (-nullrhi)."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InTarget->GetPathName());
		return false;
	}
	// The draw was recorded on the render thread, which runs up to a frame behind this one.
	FlushRenderingCommands();
	FRenderTarget* Surface = Resource;
	OutSize = Surface->GetSizeXY();
	if (OutSize.X <= 0 || OutSize.Y <= 0 || !Surface->ReadPixels(OutPixels) || OutPixels.Num() < OutSize.X * OutSize.Y)
	{
		OutPixels.Reset();
		return false;
	}
	return true;
}

bool UDreamUICaptureLibrary::ReadViewportPixels(FViewport* InViewport, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	OutPixels.Reset();
	OutSize = FIntPoint::ZeroValue;
	if (InViewport == nullptr)
	{
		return false;
	}
	FlushRenderingCommands();
	// A viewport drawn straight into its window -- a game's, standalone or packaged -- has its back buffer for a target
	// only while it draws a frame: read here, between frames, there is no texture (ReadSurfaceData ensured and the picture
	// came back black). The engine's screenshot request reads it inside the frame (FScreenshotRequest, HighResShot).
	if (!InViewport->GetRenderTargetTexture().IsValid())
	{
		return false;
	}
	OutSize = InViewport->GetSizeXY();
	if (OutSize.X <= 0 || OutSize.Y <= 0 || !InViewport->ReadPixels(OutPixels) || OutPixels.Num() < OutSize.X * OutSize.Y)
	{
		OutPixels.Reset();
		return false;
	}
	for (FColor& Pixel : OutPixels)
	{
		Pixel.A = 255;
	}
	return true;
}

FViewport* UDreamUICaptureLibrary::FindViewportOf(UWorld* InWorld)
{
	if (InWorld == nullptr)
	{
		return nullptr;
	}
	if (UGameViewportClient* GameViewport = InWorld->GetGameViewport(); GameViewport != nullptr && GameViewport->Viewport != nullptr)
	{
		return GameViewport->Viewport;
	}
#if WITH_EDITOR
	if (GEditor != nullptr)
	{
		// A perspective view first: that is where a panel placed in the level is seen as a player would.
		FViewport* AnyView = nullptr;
		for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
		{
			if (Client == nullptr || Client->Viewport == nullptr || Client->GetWorld() != InWorld)
			{
				continue;
			}
			if (Client->IsPerspective())
			{
				return Client->Viewport;
			}
			if (AnyView == nullptr)
			{
				AnyView = Client->Viewport;
			}
		}
		return AnyView;
	}
#endif
	return nullptr;
}

bool UDreamUICaptureLibrary::SaveRenderTargetToPng(UTextureRenderTarget2D* InTarget, const FString& InFilePath)
{
	TArray<FColor> Pixels;
	FIntPoint Size;
	if (!ReadRenderTargetPixels(InTarget, Pixels, Size))
	{
		return false;
	}
	return SavePixelsToPng(Pixels, Size, InFilePath);
}

bool UDreamUICaptureLibrary::SaveCanvasToPng(UDreamCanvas* InCanvas, const FString& InFilePath)
{
	if (!IsValid(InCanvas))
	{
		return false;
	}
	if (InCanvas->GetActualRenderMode() != EDreamRenderMode::RenderTarget || InCanvas->GetActualRenderTarget() == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s does not render into a target of its own, so it has no picture apart from its viewport's; SaveViewportToPng writes that."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InCanvas->GetPathName());
		return false;
	}
	return SaveRenderTargetToPng(InCanvas->GetActualRenderTarget(), InFilePath);
}

bool UDreamUICaptureLibrary::SaveViewportToPng(const UObject* InWorldContextObject, const FString& InFilePath)
{
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	FViewport* Viewport = FindViewportOf(World);
	if (Viewport == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s is not shown in any viewport."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(World));
		return false;
	}
	TArray<FColor> Pixels;
	FIntPoint Size;
	if (!ReadViewportPixels(Viewport, Pixels, Size))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d The viewport showing %s could not be read back; a game's viewport, drawn straight into its window, is read only by the engine's screenshot request (HighResShot)."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(World));
		return false;
	}
	return SavePixelsToPng(Pixels, Size, InFilePath);
}

TArray<FString> UDreamUICaptureLibrary::CaptureAll(const UObject* InWorldContextObject, const FString& InDirectory)
{
	using namespace DreamUICaptureLibraryLocal;
	TArray<FString> Written;
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(InWorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (World == nullptr)
	{
		return Written;
	}
	FString Directory = InDirectory;
	if (Directory.IsEmpty())
	{
		Directory = FPaths::Combine(GetCaptureDirectory(), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	}
	else if (FPaths::IsRelative(Directory))
	{
		Directory = FPaths::Combine(GetCaptureDirectory(), Directory);
	}

	const FString ViewportFile = ResolveCapturePath(FPaths::Combine(Directory, TEXT("Viewport.png")));
	if (SaveViewportToPng(World, ViewportFile))
	{
		Written.Add(ViewportFile);
	}
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
	{
		TSet<FString> Taken = { TEXT("Viewport") };
		for (const TWeakObjectPtr<UDreamCanvas>& WeakCanvas : Manager->SnapshotCanvases())
		{
			UDreamCanvas* Canvas = WeakCanvas.Get();
			if (!IsValid(Canvas) || !Canvas->IsRootCanvas() || Canvas->GetActualRenderMode() != EDreamRenderMode::RenderTarget)
			{
				continue;
			}
			const UDreamWidget* Widget = Canvas->GetWidget();
			const FString Base = ToFileName(Widget != nullptr ? Widget->GetDisplayName() : Canvas->GetName());
			FString Name = Base;
			for (int32 Suffix = 2; Taken.Contains(Name); ++Suffix)
			{
				Name = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
			}
			Taken.Add(Name);
			const FString CanvasFile = ResolveCapturePath(FPaths::Combine(Directory, Name + TEXT(".png")));
			if (SaveCanvasToPng(Canvas, CanvasFile))
			{
				Written.Add(CanvasFile);
			}
		}
	}
	return Written;
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GDreamUICaptureCommand(
	TEXT("DreamUI.Capture"),
	TEXT("Writes PNG pictures of this world's UI: the viewport, and every root canvas that renders into a target. DreamUI.Capture [Directory]; without a directory, a new one under Saved/DreamUI/Captures."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* InWorld, FOutputDevice& Ar)
	{
		const TArray<FString> Written = UDreamUICaptureLibrary::CaptureAll(InWorld, InArgs.Num() > 0 ? InArgs[0] : FString());
		if (Written.Num() == 0)
		{
			Ar.Log(TEXT("DreamUI.Capture wrote nothing: see the log for why."));
			return;
		}
		for (const FString& File : Written)
		{
			Ar.Logf(TEXT("DreamUI.Capture wrote %s"), *File);
		}
	}));
