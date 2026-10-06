// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "Math/IntPoint.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"
#include "Driver/DreamDriverTypes.h"
#include "InputCoreTypes.h"

class FAutomationTestBase;
class UDreamWidget;

namespace DreamTests
{
	class FDreamDesignerDriver;

	/**
	 * The designer preferences an input test depends on, set for the test's length and put back after it.
	 *
	 * They are per-user settings, saved by the toolbar whenever an author flips one, so a test that relied on whatever the
	 * person running it last chose would pass on one desk and fail on the next. Nothing is saved here: the values go back
	 * in memory -- unless bInSaveWhenPuttingBack is set, for a test that flips a preference through the toolbar, which
	 * saves the flipped value; then the original is saved again on the way out.
	 */
	class FScopedDesignerPreferences
	{
	public:
		explicit FScopedDesignerPreferences(bool bInSaveWhenPuttingBack = false);
		~FScopedDesignerPreferences();

		FScopedDesignerPreferences(const FScopedDesignerPreferences&) = delete;
		FScopedDesignerPreferences& operator=(const FScopedDesignerPreferences&) = delete;

		/** Snap designer moves and resizes to InGridSize, or not at all. */
		void SetGridSnap(bool bInEnabled, float InGridSize = 10.0f);
		/** Show the alignment guides -- which is also what turns snapping to siblings' edges on. */
		void SetGuides(bool bInShown);
		/** The DPI preview, set without the toolbar (which would save it). */
		void SetPreviewDPIScale(bool bInPreview);

	private:
		bool bSaveWhenPuttingBack = false;
		bool bGridSnapEnabled = true;
		float GridSize = 10.0f;
		bool bShowDesignerGuides = true;
		bool bPreviewDPIScale = false;
		bool bShowDesignerRulers = true;
	};

	/**
	 * The two level-editor viewport preferences the designer's camera takes over: a wheel turn zooming about the cursor
	 * (bCenterZoomAroundCursor) and a drag moving the canvas with the pointer (bPanMovesCanvas). Both default to on in the
	 * engine's own settings (BaseEditorPerProjectUserSettings.ini), which is the editor a test means. Put back, unsaved.
	 */
	class FScopedViewportCameraPreferences
	{
	public:
		FScopedViewportCameraPreferences(bool bInCenterZoomAroundCursor, bool bInPanMovesCanvas);
		~FScopedViewportCameraPreferences();

		FScopedViewportCameraPreferences(const FScopedViewportCameraPreferences&) = delete;
		FScopedViewportCameraPreferences& operator=(const FScopedViewportCameraPreferences&) = delete;

	private:
		bool bCenterZoomAroundCursor = true;
		bool bPanMovesCanvas = true;
	};

	/** A pixel inside a box, as fractions of it from its top-left corner. */
	FIntPoint PointInBox(const FBox2D& InBox, double InFractionX, double InFractionY);

	/**
	 * One design unit per pixel, and the part of the Blueprint root that is on screen less a margin: at that zoom a widget
	 * is big on screen and every handle is far from every other, and a large design canvas runs off the viewport's edges,
	 * where a pixel reaches nothing. False, having said why on InTest, when too little of the root is on screen.
	 */
	bool PrepareDesignerOneToOne(FAutomationTestBase& InTest, FDreamDesignerDriver& InDriver, FBox2D& OutWorkArea);

	/**
	 * The palette's plain Widget row dropped at InPixel through the driver, a frame pumped, and the AUTHORED widget it made
	 * under the Blueprint root handed back -- null, said on InTest, when it did not arrive there. For tests that pump the
	 * designer themselves.
	 */
	UDreamWidget* DropPlainWidgetAt(FAutomationTestBase& InTest, FDreamDesignerDriver& InDriver, FIntPoint InPixel);

	/** The selection made exactly InTemplate's preview, the way the hierarchy panel selects a row, or nothing. */
	void SelectOnlyInDesigner(FDreamDesignerDriver& InDriver, const UDreamWidget* InTemplate);
	void SelectNothingInDesigner(FDreamDesignerDriver& InDriver);

	/**
	 * Screen pixels per design unit as they apply to InPreviewWidget's position: its PARENT's rect on screen over the
	 * parent's size, which is the frame an anchored position is measured in. Zero on an axis that cannot be measured.
	 */
	FVector2D PixelsPerUnitFor(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreviewWidget);

	// ------------------------------------------------------------------------------------------------ across frames

	/**
	 * One designer held across the engine frames of a latent test: for what only engine frames bring -- Slate laying the
	 * designer's tabs out and painting them, routing a key along the focus path, a camera transition timed in real time,
	 * the viewport drawn by a real RHI.
	 */
	struct FDesignerLatentState
	{
		FDesignerTestAsset Asset;
		TSharedPtr<FDreamDesignerDriver> Driver;
		/** Cleared by the first step that cannot go on; latent commands all run, so every step asks. */
		bool bAlive = true;
		/** Engine frames spent by the test's own waits, for the report. */
		int32 Frames = 0;
		/** The road the input took, and why, for the report. */
		FString Route;
		/** Widgets the test made, by a name of its own, held weakly: the asset owns them. */
		TMap<FName, TWeakObjectPtr<UDreamWidget>> Made;
		/** Preferences the test pinned, put back by the teardown. */
		TSharedPtr<FScopedDesignerPreferences> Preferences;
		TSharedPtr<FScopedViewportCameraPreferences> CameraPreferences;
		/** Run by the teardown after the designer has closed, whatever happened before it. */
		TArray<TFunction<void()>> OnTeardown;

		UDreamWidget* Get(FName InName) const;
		FDreamDesignerDriver* GetDriver() const { return Driver.Get(); }
	};
	using FDesignerLatentRef = TSharedRef<FDesignerLatentState>;

	/**
	 * A fresh asset -- a Canvas Panel on its root unless asked otherwise -- and its designer, open, the viewport given a
	 * size where nothing laid it out. The state is not alive, having said why on InTest, when any of that failed.
	 */
	FDesignerLatentRef OpenLatentDesigner(FAutomationTestBase& InTest, const TCHAR* InName, bool bGiveRootAPanel = true);

	/** Queue InStep as one latent command, run every frame until it answers true. */
	void EnqueueDesignerStep(TFunction<bool()> InStep);
	/** Queue InAction to run once, in the next frame, while the designer is still standing. */
	void EnqueueDesignerAction(const FDesignerLatentRef& InState, TFunction<void()> InAction);
	/** Let InFrames engine frames pass; with bInDraw, the designer viewport is drawn in each (FDreamDesignerDriver::DrawFrame). */
	void EnqueueDesignerFrames(const FDesignerLatentRef& InState, int32 InFrames, bool bInDraw = false);
	/**
	 * Frames until InReady answers true, for at most InTimeoutSeconds of real time; then, if it never did, InTest is told
	 * what was being waited for -- and, given InWhatIsThere, what there was instead -- and the designer is given up on.
	 * With bInDraw the viewport is drawn in each frame waited.
	 */
	void EnqueueDesignerUntil(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, TFunction<bool()> InReady,
		double InTimeoutSeconds, const FString& InWhat, bool bInDraw = false, TFunction<FString()> InWhatIsThere = nullptr);
	/**
	 * A pointer drag across engine frames, one pointer event a frame as a hand's spans them: a move to InFrom, InButton
	 * down, InSteps even moves to InTo, a frame for the designer's own tick to apply the last of them, the release. The
	 * engine ticks the designer's viewport client between them, which is where a held press becomes a drag; nothing is
	 * pumped here.
	 */
	void EnqueueDesignerPointerDrag(const FDesignerLatentRef& InState, FIntPoint InFrom, FIntPoint InTo, int32 InSteps,
		const FKey& InButton = EKeys::LeftMouseButton);
	/**
	 * The same, the ends asked for when the drag begins rather than when it is queued: a test queues every step from its
	 * body, before the frame that decides where the widget to drag will be on screen.
	 */
	void EnqueueDesignerPointerDrag(const FDesignerLatentRef& InState, TFunction<FIntPoint()> InFrom, TFunction<FIntPoint()> InTo,
		int32 InSteps, const FKey& InButton = EKeys::LeftMouseButton);
	/**
	 * Frames until Slate gives the designer viewport the keyboard (FDreamDesignerDriver::FocusForKeyboard), asked again
	 * on each, for at most InTimeoutSeconds: the tab has to have been laid out into a window for there to be a path.
	 */
	void EnqueueDesignerKeyboardFocus(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, double InTimeoutSeconds = 5.0);
	/** A shortcut through Slate to the viewport, as a step of its own; a refusal is an error on InTest and stops the test. */
	void EnqueueDesignerShortcut(const FDesignerLatentRef& InState, FAutomationTestBase* InTest, const FKey& InKey,
		EDreamDriverModifierKeys InModifiers = EDreamDriverModifierKeys::None);

	/**
	 * Close the designer, let a frame pass, let the asset go, put the pinned preferences back and run OnTeardown -- in
	 * that order, whatever the steps before did: a toolkit still alive still ticks, and a rebuild on a half-collected
	 * asset is its own crash.
	 */
	void EnqueueDesignerTeardown(const FDesignerLatentRef& InState);
}
