// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "BlueprintEditor.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWidgetBlueprintEditor.generated.h"

class FDreamUITextWriteBack;
class SDreamWidgetAnimationEditor;
class UDreamWidgetAnimation;
class SDreamWidgetEditorHierarchyView;
class UDreamWidget;
class UDreamWidgetBlueprint;
class SDreamWidgetDesignerViewport;
class SDreamWidgetDesignerDetails;
class AActor;
class FDreamWidgetDesignerScene;
class FDreamWidgetPreviewHost;
class UToolMenu;
class FObjectPreSaveContext;
struct FDreamUIControlDescriptor;

/** UMG-toolbar-style alignment target for a multi-widget selection (DreamGUI UI plane is YZ: horizontal=Y, vertical=Z). */
enum class EDreamUIWidgetAlignType : uint8
{
	LeftEdge,
	HorizontalCenter,
	RightEdge,
	TopEdge,
	VerticalCenter,
	BottomEdge,
};

/** How the design canvas gets its size: a resolution the author picked, or what the content measures. */
enum class EDreamUIDesignerSizeRule : uint8
{
	Custom,
	Desired,
	/**
	 * The canvas is the viewport, one design unit per pixel.
	 *
	 * A view rule rather than an asset edit: it follows the window, so recording it would dirty the
	 * asset on every resize and leave a number in the diff describing somebody's window instead of
	 * the UI. The resolution the author chose stays on the asset and comes back on Custom.
	 */
	FillScreen,
};

/**
 * How this author's designer behaves, as opposed to what the asset is. Grid snapping, the grid size
 * and the overlays are preferences: kept on the asset they dirtied assets whose content nobody had
 * touched, and travelled to teammates in the diff; kept in DefaultEditor.ini they were still one
 * shared project-wide value. Per-project-per-user is the same home UMG's designer uses. The canvas
 * size and the lock set stay on the asset, because those describe the hierarchy itself.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "DreamUI Designer"))
class DREAMGUIEDITOR_API UDreamUIDesignerSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** Snap designer moves and resizes to GridSize. */
	UPROPERTY(EditAnywhere, config, Category = "Grid Snapping")
	bool bGridSnapEnabled = true;
	UPROPERTY(EditAnywhere, config, Category = "Grid Snapping", meta = (ClampMin = "1.0"))
	float GridSize = 10.0f;
	/** Show alignment guides while dragging in the designer. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowDesignerGuides = true;
	/** Overlay common device resolutions on the design canvas, like UMG's designer surface. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowResolutionGuides = false;
	/**
	 * Draw the platform's title-safe area on the design canvas.
	 *
	 * Its own switch, not a rider on the resolution guides. The two answer different questions -- "how
	 * does this look on a phone" and "will the TV crop my HUD" -- and tying the safe area to the
	 * resolution overlay meant the only way to see it was to also draw six device rectangles over the
	 * screen being designed.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowSafeZone = false;
	/**
	 * Shrink the design canvas by the project's DPI curve, the way UMG's designer does.
	 *
	 * UUserInterfaceSettings::GetDPIScaleBasedOnSize turns a device resolution into the application
	 * scale Slate will apply, and a widget authored without it is authored at a size no device shows.
	 * Off by default because DreamCanvas has a scaler of its own: with both on, the canvas previews
	 * the device scale AND the canvas rule, which is what actually happens at runtime.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bPreviewDPIScale = false;
	/** Show the selected widget's measurement, arrangement, slot, ownership and clipping diagnostics. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowLayoutDebug = false;
	/** Rulers along the top and left of the design viewport, in the canvas's own units. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowDesignerRulers = true;
	/**
	 * Honour the designer locks recorded on the asset. Turning it off reaches a locked background
	 * for one edit without unlocking it, which is the only way back from a lock that would otherwise
	 * have to be undone and redone. The locks themselves are untouched: this decides who reads them.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Designer Locks")
	bool bRespectDesignerLocks = true;
	/** Draw the selection outlines, handles, guides and readouts over the design surface. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowDesignerChrome = true;
};

/**
 * The designer for one UDreamWidgetBlueprint.
 *
 * This is the designer's toolkit, retargeted rather than rewritten: the viewport, hierarchy,
 * palette and details panels are the same 11.8k lines, and what changed underneath them is what
 * they are looking at.
 *
 * ## Two halves
 *
 * The asset holds an inert object graph -- template widgets outered to the Blueprint, with no world,
 * never registered, unable to lay out or draw. So there are two halves and the split is deliberate
 * (see FDreamWidgetPreviewHost, which owns it): STRUCTURE is edited on the template and the preview
 * is rebuilt from it, while VALUES are edited on the live preview and mirrored back. Every panel
 * here sees the preview, because the preview is the only half that can answer a question about
 * geometry; anything that changes the shape of the hierarchy goes through DreamWidgetTreeEditing.
 *
 * ## What is gone from the designer
 *
 * Apply, and everything that hung off it: a prefab was edited live and serialised back on demand,
 * so there was a second copy to push and a status to report. A Blueprint is edited directly and
 * compiled. Sub-prefabs, because nesting is a class reference. The companion behaviour blueprint,
 * because the Widget Blueprint is the logic host now.
 */
class DREAMGUIEDITOR_API FDreamWidgetBlueprintEditor : public FBlueprintEditor
{
public:

	FDreamWidgetBlueprintEditor();
	virtual ~FDreamWidgetBlueprintEditor()override;

	//Begin EditorUndo
	/**
	 * Whether this designer has anything to do with the transaction being undone.
	 *
	 * Every open designer is an undo client, and PostUndo here rebuilds an entire preview tree and
	 * broadcasts a selection change. Without this gate, dragging an actor in the level and pressing
	 * Ctrl+Z paid that bill once per open Widget Blueprint window. The engine's own answer to that
	 * is this hook: return false and neither PostUndo nor PostRedo is called at all.
	 *
	 * The test is the asset's package, not the object's class: everything that is undoable about a
	 * DreamUI asset -- the widget tree, a behaviour's properties, the designer data, the graph -- is
	 * outered into it, and the one transaction that also records preview-world objects (the design
	 * screen size) records the Blueprint alongside them.
	 */
	virtual bool MatchesContext(const FTransactionContext& InContext,
		const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void PostUndo(bool bSuccess)override;
	virtual void PostRedo(bool bSuccess)override;
	//End EditorUndo

	// FBlueprintEditor
public:
	/** Designer and Graph. The switcher in the toolbar is the stock one. */
	virtual void RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated = false) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FString GetDocumentationLink() const override;
	virtual void OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit) override;
	/** Compile, then save. Compiling is what makes an authoring edit reach the class instances are built from. */
	virtual void SaveAsset_Execute()override;
	/** Where a stale preview is paid for -- once per frame, however many edits went into the gesture. */
	virtual void Tick(float DeltaTime) override;
	// End of FBlueprintEditor

	/** The designer panels, for the tab factories that host them. */
	TSharedPtr<SDreamWidgetDesignerViewport> GetViewportWidget() const { return ViewportPtr; }
	/**
	 * Kismet's compiler-results log widget, or null before the toolkit has built one.
	 *
	 * FBlueprintEditor::GetCompilerResults() dereferences it unguarded, and the designer mode builds
	 * its tabs while the toolkit is still being constructed. The widget itself is the base class's:
	 * the Designer and Graph modes show the SAME listing, which is what makes a compile error found
	 * in one readable in the other, and only one mode is active at a time.
	 */
	TSharedPtr<SWidget> GetCompilerResultsWidget() const { return CompilerResults; }
	TSharedPtr<SDreamWidgetEditorHierarchyView> GetHierarchyWidget() const { return OutlinerPtr; }
	TSharedPtr<class SDreamWidgetPalette> GetPaletteWidget() const { return PalettePtr; }
	TSharedPtr<SDreamWidgetDesignerDetails> GetDesignerDetailsWidget() const { return DetailsPtr; }
	/** Add the design surface's own view controls to a mode toolbar. */
	void ExtendDesignerToolbar(class UToolMenu* InToolbar);
	/** The Source File combo's dropdown: pick, open, reveal, and a line naming a file that is not there. */
	void FillTextSourceMenu(class UToolMenu* InMenu);
	/** File dialog to Blueprint, via DreamUITextAuthoring::SetAuthoredSourcePath -- which recompiles. */
	void PickTextSourceFile();
	/** Carries a committed Class Defaults edit (the resources block) into the .dui. */
	void OnAnyObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent);
	/** Writes a starter .dui, points the class at it, and compiles. Offered only when there is none. */
	void CreateTextSourceFile();
	/** The first DUI root, or where the project's would be. Never a directory outside every root. */
	FString GetDefaultTextSourceDirectory() const;

	/**
	 * Whether "Reveal in VS Code" has anywhere to point: a text-authored class whose .dui is on
	 * disk right now.
	 *
	 * Hand-authored classes have no file, and a class naming a file that is missing has nothing to
	 * open. Both are no, and neither turns into yes by clicking -- which is why the entry is hidden
	 * on a hand-authored class rather than greyed, matching the Source File combo above it.
	 */
	bool CanRevealInVSCode() const;

	/**
	 * Ask VSCode to put its cursor on the line of the .dui that declares InWidget.
	 *
	 * The mirror of the bridge's `reveal` action, which walks the other way (VSCode says a node id,
	 * the designer selects it). The position is found by PARSING the file rather than by
	 * remembering one: a widget in the preview came from a tree the compiler built, and the tree
	 * keeps ids, not offsets -- and the file may have been edited since that compile, in which case
	 * a remembered offset would land on whatever now occupies that line. Re-reading is a
	 * millisecond and is never wrong.
	 *
	 * A null widget, or one whose id the file does not contain, still reveals: the file opens at
	 * 1,1 with the id carried along, so VSCode can say which node it could not find. Silence there
	 * would be indistinguishable from a broken menu entry.
	 */
	void RevealInVSCode(const UDreamWidget* InWidget);
private:
	void SyncSelection();
	void HandlePostTransaction(bool bSuccess);
	/** SelectWidgets with the re-entrancy handling stripped out; never call it directly. */
	void ApplyWidgetSelection(const TSet<UDreamWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor);
	/** A selection asked for from inside OnSelectionChanged, applied once the broadcast has finished. */
	struct FPendingWidgetSelection
	{
		TArray<TWeakObjectPtr<UDreamWidget>> Widgets;
		bool bAppendOrToggle = false;
		bool bNotifyGEditor = true;
	};
	TOptional<FPendingWidgetSelection> PendingSelection;
	/**
	 * The culture the preview is resolving its text in, or empty for the editor's own.
	 *
	 * Per designer rather than global: the preview is a per-asset question, and the localization
	 * preview it drives is process-wide, so whoever turned it on has to be the one that turns it off.
	 * OnClose does, which is why this is not a config setting -- an editor restarted into a language
	 * nobody chose is a bug report about the editor, not about the designer.
	 */
	FString PreviewCultureName;
	bool bIsSelecting = false;
	bool bRegisteredForUndo = false;
public:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName()const override { return TEXT("DreamWidgetBlueprintEditor"); }

	void SelectWidgets(const TSet<UDreamWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor = true);
	const TArray<TWeakObjectPtr<UDreamWidget>>& GetSelectedWidgets(){return SelectedWidgets;}
	bool IsWidgetHiddenInDesigner(const UDreamWidget* Widget) const;
	void SetWidgetHiddenInDesigner(UDreamWidget* Widget, bool bHidden);
	/** Whether the asset records this widget as locked, which is what the padlock column reports. */
	bool IsWidgetLockedInDesigner(const UDreamWidget* Widget) const;
	/**
	 * Whether a gesture must refuse this widget: locked AND locks are being honoured. Every picking,
	 * selection and drag gate asks this one rather than the record, so the toolbar's respect-locks
	 * switch reaches all of them and the padlock still shows what the asset says.
	 */
	bool IsWidgetLockedForInteraction(const UDreamWidget* Widget) const;
	void SetWidgetLockedInDesigner(UDreamWidget* Widget, bool bLocked, bool bRecursive = true);
	bool GetRespectDesignerLocks() const;
	void ToggleRespectDesignerLocks();
	bool GetShowDesignerChrome() const;
	void ToggleShowDesignerChrome();
	/** Rebuild the hierarchy tree, for operations that change what rows there are. */
	void RefreshOutliner();
	bool IsDesignerGridSnapEnabled() const;
	void ToggleDesignerGridSnap();
	float GetDesignerGridSize() const;
	void SetDesignerGridSize(float GridSize);
	bool GetShowDesignerGuides() const;
	bool GetShowResolutionGuides() const;
	void ToggleResolutionGuides();
	/** The title-safe overlay, on its own switch rather than riding the resolution guides. */
	bool GetShowSafeZone() const;
	void ToggleShowSafeZone();
	/** Preview the project's DPI curve on the design canvas, the way UMG's designer surface does. */
	bool GetPreviewDPIScale() const;
	void TogglePreviewDPIScale();
	/**
	 * The application scale UUserInterfaceSettings would apply at this device resolution, or 1 when
	 * the preview is switched off. Static and taking the answer as an argument so the arithmetic it
	 * feeds can be tested without a project's settings.
	 */
	float GetDesignerDPIScale() const;
	/** The canvas a device resolution leaves once the DPI scale has taken its share. Never zero. */
	static FIntPoint ApplyDPIScaleToViewportSize(FIntPoint InViewportSize, float InDPIScale);
	/**
	 * Preview the UI in another culture, the way UMG's designer Localization Preview does.
	 *
	 * FTextLocalizationManager's game-localization preview, not FInternationalization::SetCurrentCulture:
	 * the former re-resolves game text only, the latter would re-language the whole editor around the
	 * author. An empty culture name turns the preview off, which is also what closing the designer does.
	 */
	FString GetPreviewCulture() const;
	void SetPreviewCulture(const FString& InCultureName);
	/** Current design canvas size (the root agent widget's rect; falls back to the stored value). */
	FIntPoint GetDesignerCanvasSize();
	/** Device resolution being previewed; assets predating the scale rule fall back to the canvas size. */
	FIntPoint GetDesignerViewportSize();
	/**
	 * Run the hierarchy's own canvas-scaler rule over a device resolution: the canvas size that rule
	 * produces, and the scale the canvas would report. Returns false when the root carries no canvas
	 * of its own, leaving the canvas size equal to the device resolution.
	 */
	bool CalculateDesignerCanvasFor(FIntPoint InViewportSize, FIntPoint& OutCanvasSize, float& OutScale);
	/** Preview a device resolution: sizes the design canvas by the hierarchy's own rule, with undo. */
	void SetDesignerViewportSize(FIntPoint NewViewportSize);
	void ToggleDesignerGuides();
	bool GetShowLayoutDebug() const;
	void ToggleLayoutDebug();

	bool GetShowDesignerRulers() const;
	void ToggleDesignerRulers();
	float SnapDesignerValue(float Value) const;
	/** The whole design canvas, which is what "fit" means here -- not whatever happens to be selected. */
	FBox GetDesignerFramingBox();
	void ZoomDesignerToFit();
	/** One design unit per screen pixel: the size the UI is really going to be. */
	void ZoomDesignerToActualSize();
	/**
	 * Put exactly this many screen pixels on a design unit. 1.0 is ZoomDesignerToActualSize.
	 *
	 * What the toolbar's zoom box writes. A designer with only "fit" and "1:1" can be framed and it
	 * can be true-size, and there is no way to ask for 200% to look at a 12-pixel icon.
	 */
	void SetDesignerPixelsPerUnit(float InPixelsPerUnit);
	/** Screen pixels one design unit covers, or 0 when the view has no single scale (the 3D camera). */
	float GetDesignerPixelsPerUnit() const;
	/** Ortho zoom that puts InDesiredPixelsPerUnit pixels on a design unit, given where the view is now. */
	static float DesignerOrthoZoomFor(float InCurrentOrthoZoom, float InCurrentUnitsPerPixel, float InDesiredPixelsPerUnit);
	EDreamUIDesignerSizeRule GetDesignerSizeRule() const { return DesignerSizeRule; }
	/** Choosing Desired sizes the canvas to the content once; it does not keep following it. */
	void SetDesignerSizeRule(EDreamUIDesignerSizeRule InRule);

	/** The viewport's size in pixels, which under Fill Screen is also the canvas size. Zero if none. */
	FIntPoint GetDesignerViewportPixelSize() const;

	/**
	 * Size the design canvas, recording the choice on the asset only when the author made one.
	 *
	 * The recording half is what separates a resolution somebody picked from a canvas that is merely
	 * following the window: only the first belongs in the asset, and in the diff.
	 */
	void ApplyDesignerViewportSize(FIntPoint NewViewportSize, bool bRecordOnAsset);
	/** What the hierarchy's content measures, or false when nothing in it can be measured. */
	bool GetDesignerDesiredSize(FVector2D& OutSize);
	/** A canvas size for a measured content size, keeping InFallback on any axis that measured nothing. */
	static FIntPoint DesignerViewportSizeFromDesired(const FVector2D& InDesiredSize, FIntPoint InFallback);
	TSharedPtr<SWidget> BuildWidgetContextMenu();

	void InitDesigner(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UDreamWidgetBlueprint* InBlueprint);
	/** The mode a freshly opened asset lands in. */
	static FName GetDefaultModeName();

	/** Try to handle a drag-drop operation */
	FReply TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, UDreamWidget* InParentWidget = nullptr);

	FDreamWidgetDesignerScene* GetPreviewScene();
	UWorld* GetWorld();
	UDreamWidgetBlueprint* GetWidgetBlueprint()const { return BlueprintBeingEdited; }
	TSharedPtr<FDreamWidgetPreviewHost> GetPreviewHost()const { return PreviewHost; }

	static bool WorldIsDesigner(UWorld* InWorld);
	static TWeakPtr<FDreamWidgetBlueprintEditor> GetEditorByWorld(UWorld* InWorld);
	static bool WidgetIsRootAgent(UDreamWidget* InWidget);
	static void IterateAllDesigners(const TFunction<void(FDreamWidgetBlueprintEditor*)>& InFunction);
	/** The open designer editing InBlueprint, or null. The compiler's route to the preview host. */
	static FDreamWidgetBlueprintEditor* FindEditorForBlueprint(const UDreamWidgetBlueprint* InBlueprint);
	/** Every open designer whose asset is InBlueprint rebuilds its preview. */
	static void RefreshDesignersFor(UDreamWidgetBlueprint* InBlueprint);

	/** One widget's own extent in world space: its drawn geometry when it has any, its rect otherwise. */
	static FBox GetWidgetWorldBox(const UDreamWidget* InWidget);
	/**
	 * Union of the active widgets among InWidgets. False when none of them contributed, in which
	 * case OutResult is zeroed -- never left as it was found.
	 */
	static bool AccumulateWidgetsBounds(const TArray<UDreamWidget*>& InWidgets, FBoxSphereBounds& OutResult);
	/** Where the design canvas is, for framing a hierarchy that has nothing active to frame. */
	static FBoxSphereBounds MakeCanvasFramingBounds(FIntPoint InCanvasSize);
	bool GetSelectedObjectsBounds(FBoxSphereBounds& OutResult);
	bool GetAllObjectsBounds(FBoxSphereBounds& OutResult);
	/** The same union, already fallen back to the canvas, for callers with no way to say "nothing". */
	FBoxSphereBounds GetAllObjectsBounds();
	void GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType);

	bool GetAnythingDirty()const;

	UDreamWidget* GetRootAgentWidget();
	/**
	 * Root of the PREVIEW hierarchy -- the counterpart of the authoring tree's root, and what every
	 * panel that draws or picks is looking at. The authoring root is
	 * GetWidgetBlueprint()->WidgetTree->RootWidget, which is inert.
	 */
	/**
	 * Tear the preview world down here, while it still exists.
	 *
	 * The destructor cannot be trusted with it: a toolkit dies whenever its last reference goes, which
	 * can be after the transient preview world has already been collected -- and then the widget tree
	 * is cleaned up by UDreamWidget's own last-resort path, which says so at Error verbosity.
	 */
	virtual void OnClose() override;

private:
	/** Shared by OnClose and the destructor's backstop. */
	void ShutdownPreview();
public:

	UDreamWidget* GetPreviewRootWidget();

	/**
	 * The widget animations are AUTHORED on: the authoring tree's root, not the preview's.
	 *
	 * An animation is data on the asset, like the hierarchy is. Put on the preview it lives until the
	 * next rebuild and never reaches the saved asset -- the same loss every other edit had, in the one
	 * place that was still making it. What the preview is for is watching them play, which is the
	 * playback context's job, not this one's.
	 */
	UDreamWidget* GetAnimationHostWidget() const;

	/** The preview counterpart of an authored widget, for scrubbing. Null outside a designer. */
	static UDreamWidget* FindPreviewForAnimationContext(UDreamWidget* InAuthoredWidget);
	/** The host that owns that preview, for anything that needs to hear when it is replaced. */
	static TSharedPtr<FDreamWidgetPreviewHost> FindPreviewHostForAnimationContext(UDreamWidget* InAuthoredWidget);
	/** The template counterpart of a preview widget, for anything about to write authored data. */
	UDreamWidget* GetTemplateWidget(const UDreamWidget* InPreviewWidget) const;
	/**
	 * Where a drop onto InPreviewParent lands in the AUTHORING tree.
	 *
	 * For a preview widget with a template counterpart: that template, and no slot. For the hole of
	 * a placed control -- the "Content" row under a Button, a widget the control built and this asset
	 * never authored, so has no template for -- the template of the instance that opened it, and the
	 * hole's name: the content goes under that instance in this asset's tree, bound to that slot.
	 * Anything else (the design canvas, the designer's own wrapper above the authored root) is
	 * nowhere to put a widget, and answers false.
	 */
	bool ResolveTemplateParentFor(const UDreamWidget* InPreviewParent, UDreamWidget*& OutParentTemplate, FName& OutSlotName) const;

	TSharedPtr<SDreamWidgetAnimationEditor> GetSequencerEditor()const{return SequencerPtr;}
	static FName GetSequencerTabID();
	/** Opens the Animations tab and selects the animation with this display name, if it exists. */
	void FocusAnimationByDisplayName(const FString& InDisplayName);

	/**
	 * The animation currently selected in the animation list, or nullptr when none is.
	 * While one is selected the widgets in the viewport are being driven by Sequencer,
	 * so what is on screen is the animated pose rather than the authored design values.
	 */
	UDreamWidgetAnimation* GetAnimationBeingEdited()const;
	bool IsInAnimationEditMode()const { return GetAnimationBeingEdited() != nullptr; }

	/** Fires whenever the selected set of widgets changes */
	FSimpleMulticastDelegate OnSelectionChanged;
private:
	TObjectPtr<UDreamWidgetBlueprint> BlueprintBeingEdited = nullptr;
	/** The preview world, the design canvas, the live instance and the template correspondence. */
	TSharedPtr<FDreamWidgetPreviewHost> PreviewHost;

	/**
	 * Turns designer edits back into lines of the .dui, for a text-authored asset. Null for every
	 * other asset, and holding it here is what keeps it alive: it subscribes to the host on
	 * construction and unsubscribes on destruction, so its lifetime IS the subscription.
	 */
	TSharedPtr<FDreamUITextWriteBack> TextWriteBack;
	/** Class Defaults edits (the resources block) reach the file through this; see InitDesigner. */
	FDelegateHandle DefaultsChangedHandle;
	/**
	 * Package-save hook, so the designer's view state is captured by EVERY save of this asset.
	 *
	 * SaveAsset_Execute is only the toolkit's own Save button. Save All, Ctrl+Shift+S, the
	 * save-on-close prompt and a save started from the Content Browser never reach it, so the
	 * camera, the design canvas size, the canvas render mode and the collapsed rows were all
	 * silently dropped -- the state was recorded onto the asset AFTER the bytes had been written.
	 */
	FDelegateHandle PreSaveHandle;
	/** SaveEditorState, when the object about to be written is the asset this designer edits. */
	void OnObjectPreSave(UObject* InObject, FObjectPreSaveContext InContext);
	static TArray<FDreamWidgetBlueprintEditor*> DesignerInstances;

	TSharedPtr<SDreamWidgetDesignerViewport> ViewportPtr;
	TSharedPtr<SDreamWidgetDesignerDetails> DetailsPtr;
	TSharedPtr<SDreamWidgetEditorHierarchyView> OutlinerPtr;
	TSharedPtr<class SDreamWidgetPalette> PalettePtr;
	TSharedPtr<SDreamWidgetAnimationEditor> SequencerPtr;

	TArray<TWeakObjectPtr<UDreamWidget>> SelectedWidgets;
	/** Session state: the rule decided a canvas size, the size itself is what got stored. */
	EDreamUIDesignerSizeRule DesignerSizeRule = EDreamUIDesignerSizeRule::Custom;
	/**
	 * What the canvas was before Fill Screen took over, so leaving it can put that back.
	 *
	 * Remembered rather than asked for: GetDesignerViewportSize falls back to the canvas's CURRENT
	 * size when the asset never recorded a resolution -- which every converted asset is -- so under
	 * Fill Screen it answers with the fill size, and "restore" restored it to itself.
	 */
	FIntPoint DesignerSizeBeforeFillScreen = FIntPoint::ZeroValue;
	/** Size the canvas to the viewport, once per resize. A no-op when it already is. */
	void ApplyFillScreenSize();
private:

	void BindCommands();
public:
	/**
	 * UMG-toolbar-style align: move every selected sibling widget so the chosen edge/center
	 * lines up with the selection's overall bound. Operates in the shared parent's frame;
	 * requires 2+ selected widgets that share a parent (cross-parent selections are refused).
	 */
	void AlignSelectedWidgets(EDreamUIWidgetAlignType AlignType);
	/**
	 * UMG-toolbar-style distribute: keep the two outermost selected siblings fixed and space
	 * the rest so the gaps between adjacent widgets are equal, along the horizontal (bHorizontal)
	 * or vertical axis. Requires 3+ selected widgets that share a parent.
	 */
	void DistributeSelectedWidgets(bool bHorizontal);
	/**
	 * The Align and Distribute submenus, built once and offered from everywhere they belong.
	 *
	 * They used to be written inline in the hierarchy panel's context menu, which is why they could
	 * only be reached by right-clicking a ROW -- the design surface, where the widgets being aligned
	 * actually are, had no entry point at all, and neither did the mode toolbar. Same entries, same
	 * enablement, one definition: a second copy is how the viewport comes to offer a Distribute the
	 * hierarchy does not.
	 *
	 * Align needs two selected widgets and Distribute three; below that the submenu is simply absent,
	 * because "Align" with one widget selected is not an operation that could succeed.
	 */
	static void FillAlignDistributeMenu(class FMenuBuilder& InMenuBuilder, TWeakPtr<FDreamWidgetBlueprintEditor> InEditor);
	/** Whether FillAlignDistributeMenu would put anything in a menu: two or more widgets selected. */
	static bool HasAlignDistributeEntries(TWeakPtr<FDreamWidgetBlueprintEditor> InEditor);
	/**
	 * UMG "Wrap With": group the selected sibling widgets under a newly created container widget
	 * inserted at their position, sized to enclose them. Children keep their world position (the
	 * chosen panel then arranges them). Needs 1+ selected widgets that share a parent.
	 * InLayoutContainerClass is any registered layout container, or null for a plain widget with no
	 * panel at all -- which is the one choice the registry has no descriptor for.
	 */
	void WrapSelectedWidgets(UClass* InLayoutContainerClass);
	/**
	 * UMG's Unwrap: take the selected widget out from between its parent and its children.
	 *
	 * The exact inverse of Wrap With, and the reason Wrap With needed one -- a wrapper chosen by
	 * mistake could only be undone at the time, and a hierarchy imported from a .dui or inherited
	 * from a parent class had no "at the time" to go back to. The children keep their order and land
	 * where the wrapper stood; the wrapper is then deleted.
	 *
	 * One widget, which must have a parent (the root has nowhere to unwrap INTO) and at least one
	 * child (unwrapping a leaf is just deleting it, and Delete says so plainly).
	 */
	bool CanUnwrapSelectedWidget() const;
	void UnwrapSelectedWidget();
	/**
	 * Find everything that names the selected widget: the Blueprint's own graphs, and the project.
	 *
	 * Two searches because there are two ways to name one. Inside this asset a widget is a member
	 * VARIABLE, so Kismet's Find-in-Blueprint over its own graphs is the answer, and it is the same
	 * search UMG's hierarchy offers. Outside it, a widget that is an instance of another DreamUI
	 * class is named by that CLASS, so the asset registry's referencers of that class's package are
	 * the other half -- a plain widget has no class asset and gets only the first search.
	 */
	bool CanFindReferencesToSelectedWidget() const;
	void FindReferencesToSelectedWidget();
	/**
	 * UMG "Replace With", adapted to this fork's shape. UMG swaps one panel *widget* for another
	 * and has to carry the children across; here the panel is an instanced UDreamLayoutContainer
	 * hanging off the widget, so the swap touches nothing else -- name, parent, sibling index,
	 * anchors, visual, components, slot and children all stay as they are. Needs exactly one
	 * selected widget that already has a layout container. Returns quietly when the target panel
	 * cannot hold that many children.
	 */
	void ReplaceSelectedWidgetLayout(UClass* PanelClass);
	/**
	 * The registered layout containers, sorted by display name -- the panels the palette can create,
	 * which is the only list a menu offering panels should ever build. A descriptor that also names
	 * a visual or a behaviour is a control that happens to use a panel, not a panel, and is left out.
	 * InExcludeClass drops the one a widget already has, for menus where offering it does nothing.
	 */
	static void CollectLayoutPanelDescriptors(const UClass* InExcludeClass, TArray<const FDreamUIControlDescriptor*>& OutDescriptors);
	void SaveEditorState();
	/**
	 * Flip the preview between the canvas's own virtual camera (ScreenSpaceOverlay -- what play
	 * shows, where Perspective reads true) and the editor camera (world space -- what you orbit).
	 * Only preview state until the asset is saved; SaveEditorState records the current mode.
	 */
	void TogglePreviewRenderMode();
	bool IsPreviewingScreenSpace()const;
	/** Stand the viewport camera at the canvas's own virtual camera. See the viewport client. */
	void FrameViewportFromCanvasEye();
	bool CanFrameViewportFromCanvasEye()const;
	/**
	 * Write the authored geometry of these PREVIEW widgets onto their templates.
	 *
	 * Every gesture that moves a widget -- a drag, a nudge, Align, Distribute -- writes onto the
	 * preview, because the preview is where layout runs and where the result can be seen. The
	 * preview is thrown away and rebuilt from the template, so a gesture that stops there is a
	 * gesture that silently did nothing. This is the other half of it.
	 */
	void CommitWidgetGeometryToTemplate(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	/** The same, for whatever is selected -- which is what every menu-driven gesture operates on. */
	void CommitSelectedWidgetGeometryToTemplate();
	/**
	 * Structural editing, as the designer has to do it.
	 *
	 * FDreamUIEditorTools' create/delete/duplicate/copy/paste were written when the widget you had
	 * selected WAS the thing being saved, so they build and destroy live objects. In a designer the
	 * selection is a PREVIEW, rebuilt from the authoring tree, and an edit made there is gone at the
	 * next rebuild -- looking, until then, exactly like an edit that worked. So the tools route here
	 * when FindDesignerForWidget answers, and these do the same operation on the templates.
	 *
	 * Each rebuilds the preview before returning and hands back PREVIEW widgets, because that is what
	 * every caller goes on to select, name and show.
	 */
	static FDreamWidgetBlueprintEditor* FindDesignerForWidget(const UDreamWidget* InWidget);

	/**
	 * Carry a details-panel edit from the selected preview widgets onto their templates.
	 *
	 * The panel shows previews because a preview is the half that can answer a question about
	 * geometry. It is also the half that gets thrown away, so this is the other end of the same
	 * arrangement UMG uses (FWidgetBlueprintEditor::MigrateFromChain).
	 */
	/** InEditedObjects is what the details panel is showing -- widgets, visuals or behaviours. */
	void MigrateDetailsChangeToTemplate(TConstArrayView<UObject*> InEditedObjects, FEditPropertyChain& InChain, bool bIsModify);

	/**
	 * Whether a details mirror is already on the stack. NOT a convenience: see the body.
	 *
	 * Migrating fires PostEditChangeProperty on the TEMPLATE, which the engine broadcasts on
	 * FCoreUObjectDelegates::OnObjectPropertyChanged -- and an open colour picker listens to that
	 * and closes itself, which commits its value, which notifies the property node, which calls the
	 * mirror again. Sixty-four thousand frames later the game thread runs out of stack.
	 */
	bool bMigratingDetailsChange = false;

	/** InConfigureTemplate runs on the new TEMPLATE, before the preview is rebuilt from it. */
	UDreamWidget* DesignerCreateWidget(UDreamWidget* InPreviewParent, TSubclassOf<UDreamWidget> InWidgetClass,
		const FString& InDesiredName, TFunction<void(UDreamWidget*)> InConfigureTemplate = nullptr);
	bool DesignerDeleteWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	/**
	 * Behaviours are instanced sub-objects of a widget, so adding one to a preview builds it into a
	 * copy that is about to be rebuilt away. These put it on the template and hand back the preview's
	 * counterpart, matched by position, for the panel to select.
	 */
	class UDreamUIBehaviour* DesignerAddComponents(UDreamWidget* InPreviewWidget, TConstArrayView<UClass*> InComponentClasses);
	/**
	 * The general form: InAddToTemplate is handed the TEMPLATE widget and returns the component it
	 * created on it. Paste and duplicate need it because what they add is not describable as a class.
	 */
	class UDreamUIBehaviour* DesignerAddComponentBy(UDreamWidget* InPreviewWidget,
		TFunctionRef<class UDreamUIBehaviour*(UDreamWidget*)> InAddToTemplate);
	bool DesignerRemoveComponent(UDreamWidget* InPreviewWidget, class UDreamUIBehaviour* InPreviewComponent);
	/**
	 * Reorder a behaviour on the TEMPLATE, matched to the preview's by position.
	 *
	 * The component list's drag-to-reorder moved the preview's array and nothing else, so the new
	 * order lasted exactly until the next rebuild and never reached the asset. Same shape as the
	 * add and the remove: edit the template, republish, hand the panel the rebuilt preview.
	 */
	bool DesignerMoveComponent(UDreamWidget* InPreviewWidget, class UDreamUIBehaviour* InPreviewComponent, int32 InNewIndex);
	/**
	 * Where this Blueprint's own graphs read the variables these widgets own, one line per node.
	 *
	 * The variables are the compiler's, named after the widgets, so deleting a widget deletes its
	 * variable and every node reading it stops compiling. This is what lets the delete say so first.
	 */
	TArray<FText> CollectGraphReferencesToWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets) const;
	TArray<UDreamWidget*> DesignerDuplicateWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	void DesignerCopyWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	TArray<UDreamWidget*> DesignerPasteWidgets(UDreamWidget* InPreviewParent);
	static bool DesignerHasClipboardContent();
	/**
	 * Empty the designer clipboard.
	 *
	 * Called by every copy that could not be performed. The clipboard is one process-wide object that
	 * outlives the designer that filled it, so a failed copy which merely returned left the PREVIOUS
	 * copy sitting there -- and the next paste handed it over as though the user had asked for it,
	 * possibly in a different asset. Emptying it makes a failed copy fail visibly: Paste greys out.
	 */
	static void DesignerClearClipboard();
	/** Returns the name actually applied, which differs when it had to be disambiguated. */
	FString DesignerRenameWidget(UDreamWidget* InPreviewWidget, const FString& InNewDisplayName);
	/**
	 * Throw the preview away and project the authoring tree again, right now, keeping the selection.
	 *
	 * The one entry point for "the template changed underneath the preview", and it is called from
	 * both directions: a structural edit that has just rewritten the tree, and an undo or redo that
	 * has just restored it (HandlePostTransaction). The selection survives because it is carried
	 * across as TEMPLATES -- the preview objects it named are destroyed here -- and found again by
	 * the widget ids the two halves share.
	 */
	void RebuildPreviewPreservingSelection();
	/** RebuildPreviewPreservingSelection, plus the rebuilt previews of the templates named here. */
	void RepublishPreviewAndSelect(TConstArrayView<UDreamWidget*> InTemplates, TArray<UDreamWidget*>& OutPreviews);

	/**
	 * Mirror a reparent performed on the surface onto the authoring tree.
	 *
	 * The gesture happens on the preview first, deliberately: keeping a widget where it was dropped
	 * needs the new parent's real transform, and a template has none. So the preview does the move,
	 * the engine works out the resulting geometry, and this carries BOTH across -- the structure by
	 * reparenting the templates, the geometry by CommitWidgetGeometryToTemplate.
	 *
	 * Without it a drag between containers looks right until the next rebuild and then is gone.
	 */
	bool ReparentTemplatesFrom(TConstArrayView<UDreamWidget*> InPreviewWidgets, UDreamWidget* InPreviewNewParent);
	/**
	 * Mirror a Wrap With performed on the surface onto the authoring tree.
	 *
	 * Same arrangement as the reparent: the preview does it first, because working out a rect that
	 * encloses a selection needs real geometry and a template has none. The wrapper the preview built
	 * has no template counterpart -- it was made with NewObject, not from the tree -- so its rect is
	 * copied across by value rather than looked up by name.
	 */
	bool WrapTemplatesFrom(UDreamWidget* InPreviewWrapper, TConstArrayView<UDreamWidget*> InPreviewChildren,
		UClass* InLayoutContainerClass);
	/**
	 * Say that the authored hierarchy changed in a way that does not add or remove a widget.
	 *
	 * Marks the Blueprint modified and nothing else. It deliberately does NOT rebuild the preview:
	 * the values were written onto the preview, so it is already showing them, and a rebuild in the
	 * middle of a drag would destroy the widget being dragged. Structural changes go through
	 * DreamWidgetTreeEditing, whose MarkBlueprintAsStructurallyModified reaches the preview host on
	 * its own.
	 */
	void MarkDesignChanged();
private:
	/** Push the asset's recorded hidden set onto the preview widgets it names. */
	void ApplyDesignerState();
public:

	bool IsFilteredActor(const AActor* Actor);
	void OnOutlinerActorDoubleClick(AActor* Actor);
};
