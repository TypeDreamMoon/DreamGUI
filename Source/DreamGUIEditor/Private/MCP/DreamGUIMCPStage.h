// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// Where `layout` and `preview` put a screen: a preview world of the endpoint's own, a root the size of the viewport
// asked for, a render-target canvas on it, and the compiled widget Blueprint instanced under the root the way the
// designer and the Content Browser's thumbnails instance it (FDreamWidgetPreviewHost::InstancePreview). Nothing of the
// user's level is touched, and the world's manager is ticked by the stage alone.
//
// Layout needs no renderer: sizes, anchors and the layout containers are arithmetic, and a text measures itself with
// its font's metrics. So a stage that does not draw works in a commandlet as well -- the world is made inside a
// UDreamUIManagerWorldSubsystem::FCommandletWorldScope there, which is what gives it a manager.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class FDreamWidgetPreviewScene;
class UDreamCanvas;
class UDreamUserWidget;
class UDreamWidget;
class UDreamWidgetBlueprint;
class UTextureRenderTarget2D;
class UWorld;

namespace DreamGUIMCP
{
	struct FStageOptions
	{
		/** The viewport, in pixels: the root is this big, one canvas unit to a pixel. */
		FIntPoint Viewport = FIntPoint(1920, 1080);
		/** Draw into the target (preview). False: lay out only, nothing is drawn, no renderer is needed. */
		bool bDraw = false;
		/** What the picture is cleared to behind the screen. */
		FColor Backdrop = FColor(0, 0, 0, 255);
	};

	class DREAMGUIEDITOR_API FScreenStage : public FGCObject
	{
	public:
		/** The one stage of the process; its world is made on first use and kept. */
		static FScreenStage& Get();
		/** Takes the world down (module shutdown). */
		static void Shutdown();

		/** InBlueprint's screen on a fresh root of InOptions.Viewport. False with OutError when it cannot be instanced. */
		bool Open(UDreamWidgetBlueprint* InBlueprint, const FStageOptions& InOptions, FString& OutError);

		/**
		 * One frame of the stage: the manager's tick and the end-of-frame submit, glyphs rasterised on the spot, and -- when
		 * drawing -- the target drawn. InDeltaSeconds advances the screen's own time (tweens, timelines).
		 */
		void Pass(float InDeltaSeconds);

		/** Glyphs the screen's fonts still have on a worker; with bFinish, each such font is made to wait for its worker first. */
		int32 CountPendingGlyphs(bool bFinish) const;

		/** The picture, top row first. Flushes the render thread. False when not drawing or nothing could be read. */
		bool ReadPixels(TArray<FColor>& OutPixels, FIntPoint& OutSize) const;

		/** Takes the screen and the root down; the world stays for the next call. */
		void Close();

		bool IsOpen() const { return Root != nullptr; }
		UWorld* GetWorld() const;
		UDreamWidget* GetRoot() const { return Root; }
		UDreamCanvas* GetCanvas() const { return Canvas; }
		UDreamUserWidget* GetScreen() const { return Screen; }
		/** The authored root of the screen: the widget the file's root node built. */
		UDreamWidget* GetContentRoot() const;
		const FStageOptions& GetOptions() const { return Options; }
		/** Seconds of screen time the passes have advanced since Open. */
		double GetScreenTime() const { return ScreenTime; }

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("DreamGUIMCP::FScreenStage"); }

		~FScreenStage();

	private:
		UWorld* EnsureWorld();
		UTextureRenderTarget2D* EnsureTarget(FIntPoint InSize);

		TUniquePtr<FDreamWidgetPreviewScene> Scene;
		TObjectPtr<UTextureRenderTarget2D> Target = nullptr;
		TObjectPtr<UDreamWidget> Root = nullptr;
		TObjectPtr<UDreamCanvas> Canvas = nullptr;
		TObjectPtr<UDreamUserWidget> Screen = nullptr;
		FStageOptions Options;
		double ScreenTime = 0.0;
	};
}
