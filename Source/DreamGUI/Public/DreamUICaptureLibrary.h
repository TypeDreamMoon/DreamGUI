// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamUICaptureLibrary.generated.h"

class FViewport;
class UDreamCanvas;
class UTextureRenderTarget2D;

/**
 * Pictures of what DreamGUI draws, written out as PNG files -- a render target, a canvas that renders into one, or a
 * viewport with the scene and every panel on it -- so that a UI can be looked at away from the machine that drew it:
 * attached to a bug report, compared between two builds, kept by an automated run that wants to show what it saw.
 *
 * Every call reads the picture back from the GPU, and so waits for the render thread to finish what it has been
 * given: a tool for looking, not something to do every frame. Without a GPU (-nullrhi) there is nothing to read; the
 * call says so in the log and returns false.
 *
 * The console command DreamUI.Capture [Directory] runs CaptureAll for the world it is typed in.
 */
UCLASS()
class DREAMGUI_API UDreamUICaptureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** <Project>/Saved/DreamUI/Captures, the directory a relative path given to these functions is taken from. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Capture")
	static FString GetCaptureDirectory();

	/**
	 * InTarget's pixels, as a PNG at InFilePath (.png is added when the path has no extension). The target's alpha is
	 * kept, so what nothing was drawn over stays transparent in the file.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Capture")
	static bool SaveRenderTargetToPng(UTextureRenderTarget2D* InTarget, const FString& InFilePath);

	/**
	 * What InCanvas draws, as a PNG at InFilePath: the target of a canvas that renders into one. A canvas drawn on
	 * screen or in the world has no picture of its own -- it is part of the viewport's, which SaveViewportToPng writes.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Capture")
	static bool SaveCanvasToPng(UDreamCanvas* InCanvas, const FString& InFilePath);

	/**
	 * The viewport InWorldContextObject's world is shown in -- the game's, or in the editor a level viewport showing
	 * that world -- as a PNG at InFilePath: the scene with every panel on it, as a player sees it.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Capture", meta = (WorldContext = "InWorldContextObject"))
	static bool SaveViewportToPng(const UObject* InWorldContextObject, const FString& InFilePath);

	/**
	 * Everything there is to look at in InWorldContextObject's world, into InDirectory -- a new time-stamped
	 * directory under the capture directory when it is empty: the viewport as Viewport.png, and each root canvas that
	 * renders into a target as <its widget's name>.png. Returns the files written, in that order.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Capture", meta = (WorldContext = "InWorldContextObject"))
	static TArray<FString> CaptureAll(const UObject* InWorldContextObject, const FString& InDirectory);

	/** InPixels, InSize of them with the top row first, as a PNG at InFilePath; the directory is made when missing. */
	static bool SavePixelsToPng(const TArray<FColor>& InPixels, FIntPoint InSize, const FString& InFilePath);

	/** InTarget's pixels, top row first, once the render thread has finished drawing them. */
	static bool ReadRenderTargetPixels(UTextureRenderTarget2D* InTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize);

	/**
	 * The pixels InViewport shows, top row first, made opaque: a viewport's alpha is whatever the scene left in it,
	 * not how much of each pixel was drawn, and a PNG shows it as holes.
	 */
	static bool ReadViewportPixels(FViewport* InViewport, TArray<FColor>& OutPixels, FIntPoint& OutSize);

	/** The viewport InWorld is shown in, or null: its game viewport, or in the editor a level viewport showing it. */
	static FViewport* FindViewportOf(UWorld* InWorld);

	/** InFilePath made absolute against the capture directory, with .png added when it has no extension. */
	static FString ResolveCapturePath(const FString& InFilePath);
};
