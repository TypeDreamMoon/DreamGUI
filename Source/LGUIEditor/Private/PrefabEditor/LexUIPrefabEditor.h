// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "PrefabSystem/LexUIPrefab.h"
#pragma once

class SLexUIPrefabSequenceEditor;
class ULexUIPrefabSequence;
class SLexWidgetEditorHierarchyView;
class ULexWidget;
class ULexUIPrefab;
class SLexUIPrefabEditorViewport;
class SLexUIPrefabEditorDetails;
class SLexUIPrefabRawDataViewer;
class AActor;
class ULexUIPrefabHelperObject;
class UToolMenu;
struct FLexUISubPrefabData;
namespace LexUIPrefabBehaviourUtils { struct FDiscoveredEvent; }

/** UMG-toolbar-style alignment target for a multi-widget selection (LGUI UI plane is YZ: horizontal=Y, vertical=Z). */
enum class ELexUIWidgetAlignType : uint8
{
	LeftEdge,
	HorizontalCenter,
	RightEdge,
	TopEdge,
	VerticalCenter,
	BottomEdge,
};

/** UMG "Wrap With" container choices: a plain widget, a horizontal/vertical FlexBox, or a Grid. */
enum class ELexUIWrapType : uint8
{
	Widget,
	HorizontalBox,
	VerticalBox,
	Grid,
};

/**
 *
 */
class FLexUIPrefabEditor : public FAssetEditorToolkit
	, public FGCObject, public FEditorUndoClient
{
public:
	
	FLexUIPrefabEditor();
	virtual ~FLexUIPrefabEditor()override;

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
	bool bIsSelecting = false;
	bool bLastApplyHadProblems = false;
	bool bLastApplySerializationSucceeded = false;
	void OnApply();
	bool ApplyPrefabChanges();
	void SaveAppliedPrefabToDisk();
public:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName()const override { return TEXT("LexUIPrefabEditor"); }

	void SelectWidgets(const TSet<ULexWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor = true);
	const TArray<TWeakObjectPtr<ULexWidget>>& GetSelectedWidgets(){return SelectedWidgets;}
	bool IsWidgetHiddenInDesigner(const ULexWidget* Widget) const;
	void SetWidgetHiddenInDesigner(ULexWidget* Widget, bool bHidden);
	bool IsWidgetLockedInDesigner(const ULexWidget* Widget) const;
	void SetWidgetLockedInDesigner(ULexWidget* Widget, bool bLocked, bool bRecursive = true);
	bool IsDesignerGridSnapEnabled() const;
	void ToggleDesignerGridSnap();
	float GetDesignerGridSize() const;
	void SetDesignerGridSize(float GridSize);
	bool GetShowDesignerGuides() const;
	void ToggleDesignerGuides();
	float SnapDesignerValue(float Value) const;
	TSharedPtr<SWidget> BuildWidgetContextMenu();

	void InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, ULexUIPrefab* InPrefab);

	/** Try to handle a drag-drop operation */
	FReply TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, ULexWidget* InParentWidget = nullptr);

	FLexUIPrefabInstanceScene* GetPreviewScene();
	UWorld* GetWorld();
	ULexUIPrefab* GetPrefabBeingEdited()const { return PrefabBeingEdited; }

	static FLexUIPrefabEditor* GetEditorForPrefabIfValid(ULexUIPrefab* InPrefab);
	static bool WorldIsPrefabEditor(UWorld* InWorld);
	static TWeakPtr<FLexUIPrefabEditor> GetEditorByWorld(UWorld* InWorld);
	static bool WidgetIsRootAgent(ULexWidget* InWidget);
	static void IterateAllPrefabEditor(const TFunction<void(FLexUIPrefabEditor*)>& InFunction);
	bool RefreshOnSubPrefabDirty(ULexUIPrefab* InSubPrefab);

	bool GetSelectedObjectsBounds(FBoxSphereBounds& OutResult);
	FBoxSphereBounds GetAllObjectsBounds();
	bool WidgetBelongsToSubPrefab(ULexWidget* InSubPrefabActor);
	bool WidgetIsSubPrefabRoot(ULexWidget* InSubPrefabRootWidget);
	FLexUISubPrefabData GetSubPrefabDataForActor(ULexWidget* InSubPrefabWidget);
	void GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType);

	void OpenSubPrefab(ULexWidget* InSubPrefabWidget);
	void SelectSubPrefab(ULexWidget* InSubPrefabWidget);
	bool GetAnythingDirty()const;

	ULexUIPrefabHelperObject* GetPrefabHelperObject()const { return PrefabBeingEdited->GetPrefabHelperObject(); }
	ULexWidget* GetRootAgentWidget();
	ULexWidget* GetLoadedRootWidget();

	TSharedPtr<SLexUIPrefabSequenceEditor> GetSequencerEditor()const{return SequencerPtr;}
	static FName GetSequencerTabID();

	/**
	 * The animation currently selected in the animation list, or nullptr when none is.
	 * While one is selected the widgets in the viewport are being driven by Sequencer,
	 * so what is on screen is the animated pose rather than the prefab's design values.
	 */
	ULexUIPrefabSequence* GetAnimationBeingEdited()const;
	bool IsInAnimationEditMode()const { return GetAnimationBeingEdited() != nullptr; }

	/** Fires whenever the selected set of widgets changes */
	FSimpleMulticastDelegate OnSelectionChanged;
private:
	TObjectPtr<ULexUIPrefab> PrefabBeingEdited = nullptr;
	static TArray<FLexUIPrefabEditor*> PrefabEditorInstanceCollection;

	TSharedPtr<SLexUIPrefabEditorViewport> ViewportPtr;
	TSharedPtr<SLexUIPrefabEditorDetails> DetailsPtr;
	TSharedPtr<SLexWidgetEditorHierarchyView> OutlinerPtr;
	TSharedPtr<class SLexUIPrefabPalette> PalettePtr;
	TSharedPtr<SLexUIPrefabSequenceEditor> SequencerPtr;
	TSharedPtr<SLexUIPrefabRawDataViewer> PrefabRawDataViewer;

	TArray<TWeakObjectPtr<ULexWidget>> SelectedWidgets;
private:

	void BindCommands();
	//void ExtendMenu();
	void ExtendToolbar();
	void GenerateApplyOptionsMenu(UToolMenu* InMenu);
	void GenerateSaveOnApplyMenu(UToolMenu* InMenu);
	void SetSaveOnApplyMode(int32 InMode);
	bool IsSaveOnApplyMode(int32 InMode)const;

	FText GetApplyButtonStatusTooltip()const;
	FSlateIcon GetApplyButtonStatusImage()const;

	void OnOpenRawDataViewerPanel();
	void OnOpenPrefabHelperObjectDetailsPanel();
public:
	/**
	 * UMG-WidgetBlueprint-style logic host: open the prefab's companion behaviour blueprint
	 * (a ULexUIBehaviour script on the root widget), creating & attaching "BP_<PrefabName>"
	 * next to the prefab asset if there is none yet.
	 */
	void CreateOrOpenBehaviourBlueprint();
	/**
	 * UMG "Is Variable" counterpart: add a member variable to the companion behaviour blueprint
	 * (created on demand) typed to InTarget's class, and bind it to InTarget. Serialized with
	 * the prefab (GUID-remapped), so it survives renames and needs no runtime lookup.
	 */
	void PromoteToBehaviourVariable(UObject* InTarget);
	/**
	 * UMG "Event +" counterpart: generate a signature-matching handler on the companion
	 * behaviour blueprint (created on demand), wire the given event to it, and open the
	 * blueprint at the new function. InEvent is one FLexUIEventDelegate discovered on a
	 * selected widget's behaviour (see LexUIPrefabBehaviourUtils::DiscoverEvents).
	 */
	void AddEventHandler(const LexUIPrefabBehaviourUtils::FDiscoveredEvent& InEvent);
	/**
	 * UMG-toolbar-style align: move every selected sibling widget so the chosen edge/center
	 * lines up with the selection's overall bound. Operates in the shared parent's frame;
	 * requires 2+ selected widgets that share a parent (cross-parent selections are refused).
	 */
	void AlignSelectedWidgets(ELexUIWidgetAlignType AlignType);
	/**
	 * UMG-toolbar-style distribute: keep the two outermost selected siblings fixed and space
	 * the rest so the gaps between adjacent widgets are equal, along the horizontal (bHorizontal)
	 * or vertical axis. Requires 3+ selected widgets that share a parent.
	 */
	void DistributeSelectedWidgets(bool bHorizontal);
	/**
	 * UMG "Wrap With": group the selected sibling widgets under a newly created container widget
	 * inserted at their position, sized to enclose them. Children keep their world position (a
	 * FlexBox/Grid container then arranges them). Needs 1+ selected widgets that share a parent.
	 */
	void WrapSelectedWidgets(ELexUIWrapType WrapType);
	void SaveEditorState();
private:
	FGuid FindOrAddWidgetGuid(ULexWidget* Widget);
	FGuid FindWidgetGuid(const ULexWidget* Widget) const;
	void ApplyDesignerState();
	/** Companion behaviour blueprint for this prefab, created + attached to the root widget on demand. Null on failure. */
	class UBlueprint* GetOrCreateBehaviourBlueprint();
public:

	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Outliner(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Palette(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Sequencer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PrefabRawDataViewer(const FSpawnTabArgs& Args);

	bool IsFilteredActor(const AActor* Actor);
	void OnOutlinerActorDoubleClick(AActor* Actor);
};
