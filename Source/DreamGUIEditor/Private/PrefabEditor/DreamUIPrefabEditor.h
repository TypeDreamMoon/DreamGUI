// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "PrefabSystem/DreamUIPrefab.h"
#pragma once

#include "Engine/DeveloperSettings.h"
#include "DreamUIPrefabEditor.generated.h"

class SDreamUIPrefabSequenceEditor;
class UDreamUIPrefabSequence;
class SDreamWidgetEditorHierarchyView;
class UDreamWidget;
class UDreamUIPrefab;
class SDreamUIPrefabEditorViewport;
class SDreamUIPrefabEditorDetails;
class SDreamUIPrefabRawDataViewer;
class SDreamUIPrefabOverridesViewer;
class SDreamUIPrefabBehaviourViewer;
class AActor;
class UDreamUIPrefabHelperObject;
class UDreamUIBehaviour;
class IDreamUIBehaviourEditorBackend;
enum class EDreamUIBehaviourHandlerType : uint8;
class IMessageLogListing;
class UToolMenu;
struct FDreamUISubPrefabData;
struct FDreamUIControlDescriptor;
namespace DreamUIPrefabBehaviourUtils { struct FDiscoveredEvent; }

enum class EDreamUIPrefabApplyStatus : uint8
{
	Unknown,
	Success,
	Warning,
	Error,
};

enum class EDreamUIPrefabCompilerSeverity : uint8
{
	Info,
	Warning,
	Error,
};

struct FDreamUIPrefabCompilerIssue
{
	EDreamUIPrefabCompilerSeverity Severity = EDreamUIPrefabCompilerSeverity::Warning;
	FString Message;
	TWeakObjectPtr<UObject> SourceObject;
	TWeakObjectPtr<UDreamUIPrefabSequence> Animation;
	bool bOpenRawData = false;
};

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
 * How this author's designer behaves, as opposed to what the prefab is. Grid snapping, the grid size
 * and the overlays are preferences: kept on the prefab asset they dirtied prefabs whose content
 * nobody had touched, and travelled to teammates in the diff; kept in DefaultEditor.ini they were
 * still one shared project-wide value. Per-project-per-user is the same home UMG's designer uses.
 * The canvas size and the lock set stay on the asset, because those describe the prefab itself.
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
	 * Honour the designer locks recorded on the prefab. Turning it off reaches a locked background
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
 *
 */
class FDreamUIPrefabEditor : public FAssetEditorToolkit
	, public FGCObject, public FEditorUndoClient
{
public:
	
	FDreamUIPrefabEditor();
	virtual ~FDreamUIPrefabEditor()override;

	// IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	// End of IToolkit interface

	//Begin EditorUndo
	virtual void PostUndo(bool bSuccess)override;
	virtual void PostRedo(bool bSuccess)override;
	//End EditorUndo

	// FAssetEditorToolkit
public:
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FString GetDocumentationLink() const override;
	virtual void OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void SaveAsset_Execute()override;
private:
	// End of FAssetEditorToolkit
	void SyncSelection();
	void HandlePostTransaction(bool bSuccess);
	bool bIsSelecting = false;
	bool bRegisteredForUndo = false;
	EDreamUIPrefabApplyStatus LastApplyStatus = EDreamUIPrefabApplyStatus::Unknown;
	bool bLastApplySerializationSucceeded = false;
	int32 LastApplyWarningCount = 0;
	int32 LastApplyErrorCount = 0;
	void OnApply();
	bool ApplyPrefabChanges();
	void SaveAppliedPrefabToDisk();
public:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName()const override { return TEXT("DreamUIPrefabEditor"); }

	void SelectWidgets(const TSet<UDreamWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor = true);
	const TArray<TWeakObjectPtr<UDreamWidget>>& GetSelectedWidgets(){return SelectedWidgets;}
	bool IsWidgetHiddenInDesigner(const UDreamWidget* Widget) const;
	void SetWidgetHiddenInDesigner(UDreamWidget* Widget, bool bHidden);
	/** Whether the prefab records this widget as locked, which is what the padlock column reports. */
	bool IsWidgetLockedInDesigner(const UDreamWidget* Widget) const;
	/**
	 * Whether a gesture must refuse this widget: locked AND locks are being honoured. Every picking,
	 * selection and drag gate asks this one rather than the record, so the toolbar's respect-locks
	 * switch reaches all of them and the padlock still shows what the prefab says.
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
	 * Run the prefab's own canvas-scaler rule over a device resolution: the canvas size that rule
	 * produces, and the scale the canvas would report. Returns false when the prefab's root carries
	 * no canvas of its own, leaving the canvas size equal to the device resolution.
	 */
	bool CalculateDesignerCanvasFor(FIntPoint InViewportSize, FIntPoint& OutCanvasSize, float& OutScale);
	/** Preview a device resolution: sizes the design canvas by the prefab's own rule, with undo. */
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
	/** What the prefab's content measures, or false when nothing in it can be measured. */
	bool GetDesignerDesiredSize(FVector2D& OutSize);
	/** A canvas size for a measured content size, keeping InFallback on any axis that measured nothing. */
	static FIntPoint DesignerViewportSizeFromDesired(const FVector2D& InDesiredSize, FIntPoint InFallback);
	TSharedPtr<SWidget> BuildWidgetContextMenu();

	void InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UDreamUIPrefab* InPrefab);

	/** Try to handle a drag-drop operation */
	FReply TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, UDreamWidget* InParentWidget = nullptr);

	FDreamUIPrefabInstanceScene* GetPreviewScene();
	UWorld* GetWorld();
	UDreamUIPrefab* GetPrefabBeingEdited()const { return PrefabBeingEdited; }

	static FDreamUIPrefabEditor* GetEditorForPrefabIfValid(UDreamUIPrefab* InPrefab);
	static bool WorldIsPrefabEditor(UWorld* InWorld);
	static TWeakPtr<FDreamUIPrefabEditor> GetEditorByWorld(UWorld* InWorld);
	static bool WidgetIsRootAgent(UDreamWidget* InWidget);
	static void IterateAllPrefabEditor(const TFunction<void(FDreamUIPrefabEditor*)>& InFunction);
	bool RefreshOnSubPrefabDirty(UDreamUIPrefab* InSubPrefab);

	/** One widget's own extent in world space: its drawn geometry when it has any, its rect otherwise. */
	static FBox GetWidgetWorldBox(const UDreamWidget* InWidget);
	/**
	 * Union of the active widgets among InWidgets. False when none of them contributed, in which
	 * case OutResult is zeroed -- never left as it was found.
	 */
	static bool AccumulateWidgetsBounds(const TArray<UDreamWidget*>& InWidgets, FBoxSphereBounds& OutResult);
	/** Where the design canvas is, for framing a prefab that has nothing active to frame. */
	static FBoxSphereBounds MakeCanvasFramingBounds(FIntPoint InCanvasSize);
	bool GetSelectedObjectsBounds(FBoxSphereBounds& OutResult);
	bool GetAllObjectsBounds(FBoxSphereBounds& OutResult);
	/** The same union, already fallen back to the canvas, for callers with no way to say "nothing". */
	FBoxSphereBounds GetAllObjectsBounds();
	bool WidgetBelongsToSubPrefab(UDreamWidget* InSubPrefabActor);
	bool WidgetIsSubPrefabRoot(UDreamWidget* InSubPrefabRootWidget);
	FDreamUISubPrefabData GetSubPrefabDataForActor(UDreamWidget* InSubPrefabWidget);
	void GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType);

	void OpenSubPrefab(UDreamWidget* InSubPrefabWidget);
	void SelectSubPrefab(UDreamWidget* InSubPrefabWidget);
	bool GetAnythingDirty()const;

	UDreamUIPrefabHelperObject* GetPrefabHelperObject()const { return PrefabBeingEdited->GetPrefabHelperObject(); }
	UDreamWidget* GetRootAgentWidget();
	UDreamWidget* GetLoadedRootWidget();

	TSharedPtr<SDreamUIPrefabSequenceEditor> GetSequencerEditor()const{return SequencerPtr;}
	static FName GetSequencerTabID();

	/**
	 * The animation currently selected in the animation list, or nullptr when none is.
	 * While one is selected the widgets in the viewport are being driven by Sequencer,
	 * so what is on screen is the animated pose rather than the prefab's design values.
	 */
	UDreamUIPrefabSequence* GetAnimationBeingEdited()const;
	bool IsInAnimationEditMode()const { return GetAnimationBeingEdited() != nullptr; }

	/** Fires whenever the selected set of widgets changes */
	FSimpleMulticastDelegate OnSelectionChanged;
private:
	TObjectPtr<UDreamUIPrefab> PrefabBeingEdited = nullptr;
	static TArray<FDreamUIPrefabEditor*> PrefabEditorInstanceCollection;

	TSharedPtr<SDreamUIPrefabEditorViewport> ViewportPtr;
	TSharedPtr<SDreamUIPrefabEditorDetails> DetailsPtr;
	TSharedPtr<SDreamWidgetEditorHierarchyView> OutlinerPtr;
	TSharedPtr<class SDreamUIPrefabPalette> PalettePtr;
	TSharedPtr<SDreamUIPrefabSequenceEditor> SequencerPtr;
	TSharedPtr<SDreamUIPrefabRawDataViewer> PrefabRawDataViewer;
	TSharedPtr<SDreamUIPrefabOverridesViewer> PrefabOverridesViewer;
	TSharedPtr<SDreamUIPrefabBehaviourViewer> PrefabBehaviourViewer;
	TSharedPtr<IMessageLogListing> CompilerResultsListing;

	TArray<TWeakObjectPtr<UDreamWidget>> SelectedWidgets;
	/** Session state: the rule decided a canvas size, the size itself is what got stored. */
	EDreamUIDesignerSizeRule DesignerSizeRule = EDreamUIDesignerSizeRule::Custom;
private:

	void BindCommands();
	//void ExtendMenu();
	void ExtendToolbar();
	void GenerateApplyOptionsMenu(UToolMenu* InMenu);
	void GenerateBehaviourOptionsMenu(UToolMenu* InMenu);
	void GenerateSaveOnApplyMenu(UToolMenu* InMenu);
	void SetSaveOnApplyMode(int32 InMode);
	bool IsSaveOnApplyMode(int32 InMode)const;

	FText GetApplyButtonStatusTooltip()const;
	FSlateIcon GetApplyButtonStatusImage()const;

	void OnOpenRawDataViewerPanel();
	void OnOpenOverridesViewerPanel();
	void OnOpenBehaviourViewerPanel();
	void OnOpenPrefabHelperObjectDetailsPanel();
public:
	/**
	 * UMG-WidgetBlueprint-style logic host: open the prefab's companion behaviour blueprint
	 * (a UDreamUIBehaviour script on the root widget), creating & attaching "BP_<PrefabName>"
	 * next to the prefab asset if there is none yet.
	 */
	void CreateOrOpenBehaviourBlueprint();
	void CreateAndAssignBehaviourBlueprint();
	void PickBehaviourClass();
	void RemovePrimaryBehaviour();
	UClass* GetEffectiveBehaviourClass() const;
	UDreamUIBehaviour* GetPrimaryBehaviour() const;
	bool CanAuthorBehaviour() const;
	/**
	 * UMG "Is Variable" counterpart: add a member variable to the companion behaviour blueprint
	 * (created on demand) typed to InTarget's class, and bind it to InTarget. Serialized with
	 * the prefab (GUID-remapped), so it survives renames and needs no runtime lookup.
	 */
	void PromoteToBehaviourVariable(UObject* InTarget);
	/**
	 * UMG "Event +" counterpart: generate a signature-matching handler on the companion
	 * behaviour blueprint (created on demand), wire the given event to it, and open the
	 * blueprint at the new function. InEvent is one FDreamUIEventDelegate discovered on a
	 * selected widget's behaviour (see DreamUIPrefabBehaviourUtils::DiscoverEvents).
	 */
	void AddEventHandler(const DreamUIPrefabBehaviourUtils::FDiscoveredEvent& InEvent, EDreamUIBehaviourHandlerType InHandlerType);
	bool CanAddEventHandler(EDreamUIBehaviourHandlerType InHandlerType) const;
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
	 * selected widget that already has a layout container, and refuses widgets owned by a
	 * sub prefab. Returns quietly when the target panel cannot hold that many children.
	 */
	void ReplaceSelectedWidgetLayout(UClass* PanelClass);
	/**
	 * The registered layout containers, sorted by display name -- the panels the palette can create,
	 * which is the only list a menu offering panels should ever build. A descriptor that also names
	 * a visual or a behaviour is a control that happens to use a panel, not a panel, and is left out.
	 * InExcludeClass drops the one a widget already has, for menus where offering it does nothing.
	 */
	static void CollectLayoutPanelDescriptors(const UClass* InExcludeClass, TArray<const FDreamUIControlDescriptor*>& OutDescriptors);
	/**
	 * UMG "Find References": search the prefab's companion behaviour blueprint for the variable
	 * the selected widget binds to. UMG can search by widget name because there the variable is
	 * the widget; here the link is the same sanitized-display-name convention auto-bind uses, so
	 * that is what gets searched. Does nothing (with a note) when the prefab has no companion
	 * blueprint -- searching should never be the thing that creates one.
	 */
	void FindReferencesForSelectedWidget();
	bool CanFindReferencesForSelectedWidget() const;
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
private:
	FGuid FindOrAddWidgetGuid(UDreamWidget* Widget);
	FGuid FindWidgetGuid(const UDreamWidget* Widget) const;
	void ApplyDesignerState();
	/** Companion behaviour blueprint for this prefab, created + attached to the root widget on demand. Null on failure. */
	class UBlueprint* GetOrCreateBehaviourBlueprint();
	bool AssignBehaviourClass(UClass* InClass);
	bool ReplacePrimaryBehaviour(UDreamUIBehaviour* InOldBehaviour, UDreamUIBehaviour* InNewBehaviour, bool bNewBehaviourWasCreated);
	TSharedPtr<IDreamUIBehaviourEditorBackend> GetBehaviourEditorBackend() const;
	TArray<FString> PendingBehaviourWarnings;
	void ValidatePrefabReferences(TArray<FDreamUIPrefabCompilerIssue>& OutIssues) const;
	void PublishCompilerResults(const FText& PageTitle, const TArray<FDreamUIPrefabCompilerIssue>& Issues,
		const FText& Summary, bool bAutoOpenOnProblems);
	void RunInitialReferenceValidation();
	void NavigateToCompilerObject(TWeakObjectPtr<UObject> InObject);
	void NavigateToAnimation(TWeakObjectPtr<UDreamUIPrefabSequence> InAnimation);
public:

	/**
	 * One row per dockable panel. Registration, un-registration, the Window menu entry and the
	 * SDockTab label all read the same row, so a new panel is one entry here plus a slot in
	 * CreateDefaultLayout -- nothing to keep in step by hand.
	 */
	struct FTabDescriptor
	{
		FName Id;
		FText Label;
		FSlateIcon Icon;
		/** Builds the content for a freshly spawned dock tab. */
		TFunction<TSharedRef<SWidget>()> MakeContent;
		/** Runs before MakeContent so a panel can refresh against the current prefab. */
		TFunction<void()> OnSpawn;
		/** Runs when the user closes the tab, so a panel can drop editor-wide state it was driving. */
		TFunction<void()> OnClosed;
		/** Debug panels stay out of the Window menu; the toolbar's Debug menu opens them. */
		bool bListedInWindowMenu = true;
	};
	TArray<FTabDescriptor> TabDescriptors;
	void BuildTabDescriptors();
	const FTabDescriptor* FindTabDescriptor(FName TabId) const;
	TSharedRef<SDockTab> SpawnTabFromDescriptor(const FSpawnTabArgs& Args, FName TabId);
	/** The layout a fresh install gets; the name carries a version so a change here wins over saved layouts. */
	static TSharedRef<FTabManager::FLayout> CreateDefaultLayout();
	bool HasAnySubPrefab() const;
	void GenerateDebugMenu(UToolMenu* InMenu);

	bool IsFilteredActor(const AActor* Actor);
	void OnOutlinerActorDoubleClick(AActor* Actor);
};
