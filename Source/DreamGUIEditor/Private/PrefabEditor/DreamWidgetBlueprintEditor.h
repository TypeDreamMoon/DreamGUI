// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "BlueprintEditor.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWidgetBlueprintEditor.generated.h"

class SDreamUIPrefabSequenceEditor;
class UDreamUIPrefabSequence;
class SDreamWidgetEditorHierarchyView;
class UDreamWidget;
class UDreamWidgetBlueprint;
class SDreamUIPrefabEditorViewport;
class SDreamUIPrefabEditorDetails;
class AActor;
class FDreamUIPrefabInstanceScene;
class FDreamWidgetPreviewHost;
class UToolMenu;
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
};

/**
 * How this author's designer behaves, as opposed to what the asset is. Grid snapping, the grid size
 * and the overlays are preferences: kept on the asset they dirtied assets whose content nobody had
 * touched, and travelled to teammates in the diff; kept in DefaultEditor.ini they were still one
 * shared project-wide value. Per-project-per-user is the same home UMG's designer uses. The canvas
 * size and the lock set stay on the asset, because those describe the hierarchy itself.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "DreamUI Designer"))
class UDreamUIDesignerSettings : public UDeveloperSettings
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
	/** Show the selected widget's measurement, arrangement, slot, ownership and clipping diagnostics. */
	UPROPERTY(EditAnywhere, config, Category = "Visualization")
	bool bShowLayoutDebug = false;
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
 * This is the prefab editor's toolkit, retargeted rather than rewritten: the viewport, hierarchy,
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
 * ## What is gone from the prefab editor
 *
 * Apply, and everything that hung off it: a prefab was edited live and serialised back on demand,
 * so there was a second copy to push and a status to report. A Blueprint is edited directly and
 * compiled. Sub-prefabs, because nesting is a class reference. The companion behaviour blueprint,
 * because the Widget Blueprint is the logic host now.
 */
class FDreamWidgetBlueprintEditor : public FBlueprintEditor
{
public:

	FDreamWidgetBlueprintEditor();
	virtual ~FDreamWidgetBlueprintEditor()override;

	//Begin EditorUndo
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
	TSharedPtr<SDreamUIPrefabEditorViewport> GetViewportWidget() const { return ViewportPtr; }
	TSharedPtr<SDreamWidgetEditorHierarchyView> GetHierarchyWidget() const { return OutlinerPtr; }
	TSharedPtr<class SDreamUIPrefabPalette> GetPaletteWidget() const { return PalettePtr; }
	TSharedPtr<SDreamUIPrefabEditorDetails> GetDesignerDetailsWidget() const { return DetailsPtr; }
	/** Add the design surface's own view controls to a mode toolbar. */
	void ExtendDesignerToolbar(class UToolMenu* InToolbar);
private:
	void SyncSelection();
	void HandlePostTransaction(bool bSuccess);
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
	float SnapDesignerValue(float Value) const;
	/** The whole design canvas, which is what "fit" means here -- not whatever happens to be selected. */
	FBox GetDesignerFramingBox();
	void ZoomDesignerToFit();
	/** One design unit per screen pixel: the size the UI is really going to be. */
	void ZoomDesignerToActualSize();
	/** Screen pixels one design unit covers, or 0 when the view has no single scale (the 3D camera). */
	float GetDesignerPixelsPerUnit() const;
	/** Ortho zoom that puts InDesiredPixelsPerUnit pixels on a design unit, given where the view is now. */
	static float DesignerOrthoZoomFor(float InCurrentOrthoZoom, float InCurrentUnitsPerPixel, float InDesiredPixelsPerUnit);
	EDreamUIDesignerSizeRule GetDesignerSizeRule() const { return DesignerSizeRule; }
	/** Choosing Desired sizes the canvas to the content once; it does not keep following it. */
	void SetDesignerSizeRule(EDreamUIDesignerSizeRule InRule);
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

	FDreamUIPrefabInstanceScene* GetPreviewScene();
	UWorld* GetWorld();
	UDreamWidgetBlueprint* GetWidgetBlueprint()const { return BlueprintBeingEdited; }
	TSharedPtr<FDreamWidgetPreviewHost> GetPreviewHost()const { return PreviewHost; }

	static bool WorldIsDesigner(UWorld* InWorld);
	static TWeakPtr<FDreamWidgetBlueprintEditor> GetEditorByWorld(UWorld* InWorld);
	static bool WidgetIsRootAgent(UDreamWidget* InWidget);
	static void IterateAllDesigners(const TFunction<void(FDreamWidgetBlueprintEditor*)>& InFunction);
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
	UDreamWidget* GetPreviewRootWidget();
	/** The template counterpart of a preview widget, for anything about to write authored data. */
	UDreamWidget* GetTemplateWidget(const UDreamWidget* InPreviewWidget) const;

	TSharedPtr<SDreamUIPrefabSequenceEditor> GetSequencerEditor()const{return SequencerPtr;}
	static FName GetSequencerTabID();
	/** Opens the Animations tab and selects the animation with this display name, if it exists. */
	void FocusAnimationByDisplayName(const FString& InDisplayName);

	/**
	 * The animation currently selected in the animation list, or nullptr when none is.
	 * While one is selected the widgets in the viewport are being driven by Sequencer,
	 * so what is on screen is the animated pose rather than the authored design values.
	 */
	UDreamUIPrefabSequence* GetAnimationBeingEdited()const;
	bool IsInAnimationEditMode()const { return GetAnimationBeingEdited() != nullptr; }

	/** Fires whenever the selected set of widgets changes */
	FSimpleMulticastDelegate OnSelectionChanged;
private:
	TObjectPtr<UDreamWidgetBlueprint> BlueprintBeingEdited = nullptr;
	/** The preview world, the design canvas, the live instance and the template correspondence. */
	TSharedPtr<FDreamWidgetPreviewHost> PreviewHost;
	static TArray<FDreamWidgetBlueprintEditor*> DesignerInstances;

	TSharedPtr<SDreamUIPrefabEditorViewport> ViewportPtr;
	TSharedPtr<SDreamUIPrefabEditorDetails> DetailsPtr;
	TSharedPtr<SDreamWidgetEditorHierarchyView> OutlinerPtr;
	TSharedPtr<class SDreamUIPrefabPalette> PalettePtr;
	TSharedPtr<SDreamUIPrefabSequenceEditor> SequencerPtr;

	TArray<TWeakObjectPtr<UDreamWidget>> SelectedWidgets;
	/** Session state: the rule decided a canvas size, the size itself is what got stored. */
	EDreamUIDesignerSizeRule DesignerSizeRule = EDreamUIDesignerSizeRule::Custom;
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
	 * UMG "Wrap With": group the selected sibling widgets under a newly created container widget
	 * inserted at their position, sized to enclose them. Children keep their world position (the
	 * chosen panel then arranges them). Needs 1+ selected widgets that share a parent.
	 * InLayoutContainerClass is any registered layout container, or null for a plain widget with no
	 * panel at all -- which is the one choice the registry has no descriptor for.
	 */
	void WrapSelectedWidgets(UClass* InLayoutContainerClass);
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
	void MigrateDetailsChangeToTemplate(FEditPropertyChain& InChain, bool bIsModify);

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
	TArray<UDreamWidget*> DesignerDuplicateWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	void DesignerCopyWidgets(TConstArrayView<UDreamWidget*> InPreviewWidgets);
	TArray<UDreamWidget*> DesignerPasteWidgets(UDreamWidget* InPreviewParent);
	static bool DesignerHasClipboardContent();
	/** Returns the name actually applied, which differs when it had to be disambiguated. */
	FString DesignerRenameWidget(UDreamWidget* InPreviewWidget, const FString& InNewDisplayName);
	/** Rebuild the preview right now and select the previews of these templates. */
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
