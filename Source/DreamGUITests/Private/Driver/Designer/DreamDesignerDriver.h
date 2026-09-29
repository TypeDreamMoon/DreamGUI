// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
// By value rather than forward-declared: a TOptional member needs the whole type.
#include "Layout/Geometry.h"
#include "Math/IntPoint.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class FDragDropOperation;
class FDreamWidgetBlueprintEditor;
class FDreamWidgetDesignerViewportClient;
class FEditorViewportClient;
class FSceneViewport;
class SDreamWidgetDesignerViewport;
class UBlueprint;
class UClass;
class UDreamWidget;
struct FPointerEvent;

namespace DreamTests
{
	/**
	 * A Widget Blueprint designer, opened the way an author opens one, with the few handles a test
	 * needs to act on it: viewport pixels, drops, pointer input, compile and undo.
	 *
	 * The designer is a Slate host drawing a self-rendered preview, so there are two coordinate
	 * frames and both matter. The OUTER one is the shell widget's Slate geometry, which is what
	 * Slate hands OnDragOver/OnDrop and what the shell measures a screen position in. The INNER one
	 * is the FSceneViewport's pixel grid, which is what the viewport client hit-tests against and
	 * what GetMouseX/GetMouseY report. Everything a caller here names is an inner pixel, because
	 * that is the frame the designer's own rules are written in, and this type owns the conversion
	 * in both directions.
	 *
	 * Nothing is simulated. A drop goes through the shell's real drag handlers carrying the real
	 * palette operation, and pointer input goes in through FSceneViewport's ISlateViewport entry
	 * points -- the same functions Slate itself calls -- so the cursor cache the viewport client
	 * reads is updated by the same code that updates it for a user.
	 *
	 * Held by shared pointer, and only ever built by Open(), because a driver over a designer that
	 * never opened is a driver whose every answer would be a quiet zero.
	 */
	class FDreamDesignerDriver
	{
	public:
		/**
		 * Open InBlueprint's designer and take hold of its viewport, or return null having said at
		 * Warning verbosity which link of the chain was missing. The chain is: an editor engine, an
		 * asset editor subsystem, a toolkit for the asset, the designer viewport shell the toolkit
		 * builds for its Designer mode, and the FSceneViewport inside that shell.
		 */
		static TSharedPtr<FDreamDesignerDriver> Open(UBlueprint* InBlueprint);

		~FDreamDesignerDriver();

		/** Close the toolkit. Idempotent, and called for you on destruction. */
		void Close();

		/** Whether the toolkit, the shell and the viewport are all still there. */
		bool IsUsable() const;
		/** Whether the asset editor subsystem still reports THIS toolkit as the one editing the asset. */
		bool IsToolkitOpen() const;

		FDreamWidgetBlueprintEditor* Toolkit() const { return ToolkitPtr; }
		TSharedPtr<SDreamWidgetDesignerViewport> ViewportShell() const { return WeakViewportShell.Pin(); }
		TSharedPtr<FSceneViewport> SceneViewport() const { return WeakSceneViewport.Pin(); }
		/** The designer's viewport client, as the engine base -- everything used here is declared there. */
		FEditorViewportClient* ViewportClient() const;

		/**
		 * Give the viewport a size when nothing else has, and return whether it has one now.
		 *
		 * A headless editor never lays the designer out, so the scene viewport keeps a zero size and
		 * the shell keeps an empty geometry -- and then FSceneViewport drops every pointer event on
		 * its own zero-size guard, and every pixel named here means nothing. This puts a fixed size
		 * on the viewport and remembers a matching root geometry for the frames in which Slate has
		 * still cached none.
		 *
		 * Does nothing when the viewport already has a size, so it never overrides what a real
		 * window decided. Not called by the probe that measures the headless size, which has to
		 * report what it finds rather than what it could arrange.
		 */
		bool EnsureHeadlessSize(FIntPoint InSize);

		/** The inner pixel grid's extent. Zero on either axis means the viewport was never sized. */
		FIntPoint ViewportPixelSize() const;
		/** The middle of that grid, which is where a test drops something when it does not care where. */
		FIntPoint ViewportCentrePixel() const;

		/**
		 * An inner viewport pixel as an absolute Slate (screen) position, in the shell's own frame.
		 *
		 * The exact inverse of the shell's ToViewportPixel, which reads
		 *   Pixel = round(Geometry.AbsoluteToLocal(Screen) * ViewportSize / Geometry.GetLocalSize())
		 * so this reads
		 *   Screen = Geometry.LocalToAbsolute(Pixel * Geometry.GetLocalSize() / ViewportSize).
		 * There is no DPI term on either side and there must not be: the geometry's accumulated
		 * render transform already carries the window's DPI scale, LocalToAbsolute applies it and
		 * AbsoluteToLocal removes it, so a scale factor written in here would be counted twice.
		 */
		FVector2D PixelToScreen(FIntPoint InPixel) const;
		/** The shell's own ToViewportPixel, restated, so a test can assert the round trip. */
		FIntPoint ScreenToPixel(const FVector2D& InScreenPosition) const;

		/**
		 * Where a preview widget's centre lands on the inner pixel grid, or unset when it cannot be
		 * projected -- no viewport, no scene view, or a widget at or behind the eye. Uses the same
		 * world-to-pixel path the designer's own overlay drawing uses, so a pixel this answers is a
		 * pixel a click would arrive at.
		 */
		TOptional<FIntPoint> WidgetPixel(const UDreamWidget* InPreviewWidget) const;

		/**
		 * Drag a palette row onto the design surface and let go at InPixel.
		 *
		 * The class is resolved against the control registry the palette itself lists from -- a
		 * control class first, then a panel's layout container, then a visual -- so a caller names
		 * UDreamButton and gets the row an author would have dragged. Null names the Basic group's
		 * plain Widget row, which carries no visual at all.
		 */
		bool DropFromPalette(UClass* InWidgetClass, FIntPoint InPixel);
		/** The same drop, naming the registry row directly ("Button", "Overlay", "VerticalBox"). */
		bool DropFromPalette(FName InPaletteRowName, FIntPoint InPixel);

		/** Drag an existing preview widget out of the hierarchy and drop it on the surface at InPixel. */
		bool DropFromHierarchy(UDreamWidget* InSource, FIntPoint InPixel);

		/**
		 * Move the pointer to an inner pixel. Every press and release afterwards happens there.
		 *
		 * While a button is held this also hands the travel on as MouseX/MouseY axis input, which is
		 * what FSceneViewport does for a viewport that holds mouse capture. A user's press always
		 * takes capture -- the application is active -- and a headless one never does, so without
		 * this the engine's own tracking sees a drag of any length as a motionless click and
		 * processes a click on release behind whatever the designer's gesture just did.
		 */
		bool MoveTo(FIntPoint InPixel);
		bool Press(const FKey& InKey);
		bool Release(const FKey& InKey);

		/**
		 * One engine frame of this designer's own work, synchronously: the viewport client's tick,
		 * which is where a held press becomes a drag and a drag follows the pointer, then the
		 * toolkit's, which is where a stale preview is rebuilt.
		 *
		 * For tests that run inside RunTest. A test being driven by engine frames already has the
		 * engine ticking both, and pumping as well would tick them twice.
		 */
		void PumpFrame(float InDeltaSeconds = 1.0f / 60.0f);
		/** Move, press, release, with a frame after each: a click, as the designer sees one. */
		bool ClickAt(FIntPoint InPixel, const FKey& InButton = EKeys::LeftMouseButton);
		/**
		 * Press the left button at InFrom, travel to InTo in InSteps even moves with a frame after
		 * each, and let go there. The last frame is pumped before the release because a designer
		 * drag applies the pointer on its tick and finishing it reads nothing new.
		 */
		bool DragFromTo(FIntPoint InFrom, FIntPoint InTo, int32 InSteps = 4);

		/** The widget's four projected corners as an axis-aligned box of inner pixels, or unset. */
		TOptional<FBox2D> WidgetPixelRect(const UDreamWidget* InPreviewWidget) const;
		/** What the toolkit has selected: the live entries of its selection, preview widgets all. */
		TArray<UDreamWidget*> SelectedWidgets() const;

		/**
		 * Give the viewport this size even when it already has one.
		 *
		 * EnsureHeadlessSize fills a hole and never overrules; this is the other thing a test needs,
		 * a resize while the designer is running, which reallocates the viewport's render target.
		 */
		bool ResizeViewport(FIntPoint InSize);

		/**
		 * Draw the designer viewport once, now, the way the editor's own loop draws a realtime
		 * viewport: inside the client's world switch, through FViewport::Draw.
		 *
		 * Synchronous on purpose. The editor loop draws this viewport as well whenever it draws at
		 * all, but whether it does in an unattended run turns on the state of every window in the
		 * process, and a scenario that meant to render and silently did not would pass for the
		 * wrong reason. Returns false, and draws nothing, without a real RHI or without a size.
		 */
		bool DrawFrame();

		/** Compile through the toolkit, which is what the designer's own Compile button does. */
		void Compile();
		void Undo();
		void Redo();

		/**
		 * Root of the previewed CONTENTS: the preview counterpart of the Blueprint tree's root, and
		 * the boundary every drop is resolved against. What a dropped widget's parent must be.
		 */
		UDreamWidget* BlueprintRoot() const;
		/**
		 * The design canvas the preview hangs under -- the designer's own wrapper, ABOVE the
		 * Blueprint root and not part of the asset. Named here so a test can assert a drop did NOT
		 * land on it, which is the shape the wrapper bug took.
		 */
		UDreamWidget* PreviewRoot() const;

		/** Children of InParent that are actually there; a collected null is not a child. */
		int32 ChildCountUnder(const UDreamWidget* InParent) const;

		/**
		 * The preview widget standing for an authored one right now, or null.
		 *
		 * Asked again after anything that may rebuild the preview -- a drop, a compile, an undo -- because
		 * a rebuild replaces every preview widget, while the authored one it answers for stays put.
		 */
		UDreamWidget* PreviewFor(const UDreamWidget* InTemplate) const;

	private:
		FDreamDesignerDriver() = default;

		/**
		 * The frame a pointer event is measured in.
		 *
		 * Two different widgets receive the two kinds of input: the shell handles drag and drop, and
		 * the SViewport inside it handles the pointer. Their geometries differ by whatever the
		 * viewport toolbar and padding take, so a position built in one frame and delivered in the
		 * other is off by that much. Each caller asks for the frame it is about to deliver into.
		 */
		bool GetShellGeometry(FGeometry& OutGeometry) const;
		bool GetViewportGeometry(FGeometry& OutGeometry) const;
		FVector2D PixelToScreenIn(const FGeometry& InGeometry, FIntPoint InPixel) const;
		FPointerEvent MakePointerEvent(const FVector2D& InScreenPosition, const FKey& InEffectingButton) const;
		/** Over, then drop -- the order Slate delivers them in, and the order the designer expects. */
		bool DeliverDragDrop(const TSharedPtr<FDragDropOperation>& InOperation, const FPointerEvent& InEvent);
		/** Whether a pointer event sent now would reach the viewport client at all. */
		bool CanTakePointerInput() const;
		/** Hand held-button travel on as axis input, the way a viewport holding capture does. */
		void DeliverHeldPointerTravel(FSceneViewport& InViewport, const FVector2D& InTravel);

		TWeakObjectPtr<UBlueprint> WeakBlueprint;
		/**
		 * Raw, like the toolkit pointer every other designer test holds: the asset editor subsystem
		 * owns the toolkit and keeps it alive until the asset is closed, which is this type's own
		 * last act.
		 */
		FDreamWidgetBlueprintEditor* ToolkitPtr = nullptr;
		/**
		 * Weak, unlike the toolkit: a shared reference to the shell or its viewport would outlive
		 * the close and keep a torn-down designer's Slate widgets breathing into the next test.
		 */
		TWeakPtr<SDreamWidgetDesignerViewport> WeakViewportShell;
		TWeakPtr<FSceneViewport> WeakSceneViewport;

		/**
		 * The frame EnsureHeadlessSize arranged, used only while Slate has cached none of its own.
		 *
		 * Unset by default, so on a real designer with a real window nothing here is synthetic and
		 * every position is measured in the geometry Slate actually laid out.
		 */
		TOptional<FGeometry> SyntheticGeometry;

		/** The pointer state a synthesised event carries, kept the way Slate keeps its own. */
		TSet<FKey> PressedButtons;
		/** Where the pointer is, in absolute Slate space. Zero until the first MoveTo. */
		FVector2D LastScreenPosition = FVector2D::ZeroVector;
	};
}
