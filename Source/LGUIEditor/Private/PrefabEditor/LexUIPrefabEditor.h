// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "PrefabSystem/LexUIPrefab.h"
#pragma once

class SLexWidgetEditorHierarchyView;
class ULexWidget;
class ULexUIPrefab;
class SLexUIPrefabEditorViewport;
class SLexUIPrefabEditorDetails;
class SLexUIPrefabRawDataViewer;
class AActor;
class ULexUIPrefabHelperObject;
struct FLexUISubPrefabData;

/**
 * 
 */
class FLexUIPrefabEditor : public FAssetEditorToolkit
	, public FGCObject, public FEditorUndoClient
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnSelectedWidgetsChanged)
	DECLARE_MULTICAST_DELEGATE(FOnWidgetPreviewUpdated)
	
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
public:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName()const { return TEXT("LGUIPrefabEditor"); }

	void SelectWidgets(const TSet<ULexWidget*>& Widgets, bool bAppendOrToggle, bool bNotifyGEditor = true);
	const TArray<TWeakObjectPtr<AActor>>& GetSelectedActors(){return SelectedActors;}
	TArray<TWeakObjectPtr<ULexWidget>> GetSelectedWidgets();

	void InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, ULexUIPrefab* InPrefab);
	TArray<AActor*> GetAllActors();

	/** Try to handle a drag-drop operation */
	FReply TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent, ULexWidget* InParentWidget = nullptr);

	FLexUIPrefabInstanceScene* GetPreviewScene();
	UWorld* GetWorld();
	ULexUIPrefab* GetPrefabBeingEdited()const { return PrefabBeingEdited; }

	static FLexUIPrefabEditor* GetEditorForPrefabIfValid(ULexUIPrefab* InPrefab);
	static bool WorldIsPrefabEditor(UWorld* InWorld);
	static TWeakPtr<FLexUIPrefabEditor> GetEditorByWorld(UWorld* InWorld);
	static bool ActorIsRootAgent(AActor* InActor);
	static void IterateAllPrefabEditor(const TFunction<void(FLexUIPrefabEditor*)>& InFunction);
	bool RefreshOnSubPrefabDirty(ULexUIPrefab* InSubPrefab);

	bool GetSelectedObjectsBounds(FBoxSphereBounds& OutResult);
	FBoxSphereBounds GetAllObjectsBounds();
	bool ActorBelongsToSubPrefab(AActor* InSubPrefabActor);
	bool ActorIsSubPrefabRoot(AActor* InSubPrefabRootActor);
	FLexUISubPrefabData GetSubPrefabDataForActor(AActor* InSubPrefabActor);
	void GetInitialViewSetting(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation, ELevelViewportType& OutViewType);

	void OpenSubPrefab(AActor* InSubPrefabActor);
	void SelectSubPrefab(AActor* InSubPrefabActor);
	bool GetAnythingDirty()const;

	ULexUIPrefabHelperObject* GetPrefabHelperObject()const { return PrefabBeingEdited->GetPrefabHelperObject(); }
	AActor* GetRootAgentActor();
	AActor* GetLoadedRootActor();

	/** Fires whenever the selected set of widgets changing */
	FOnSelectedWidgetsChanged OnSelectedWidgetsChanging;
	/** Fires whenever the selected set of widgets changes */
	FOnSelectedWidgetsChanged OnSelectedWidgetsChanged;
private:
	TObjectPtr<ULexUIPrefab> PrefabBeingEdited = nullptr;
	static TArray<FLexUIPrefabEditor*> PrefabEditorInstanceCollection;

	TSharedPtr<SLexUIPrefabEditorViewport> ViewportPtr;
	TSharedPtr<SLexUIPrefabEditorDetails> DetailsPtr;
	TSharedPtr<SLexWidgetEditorHierarchyView> OutlinerPtr;
	TSharedPtr<SLexUIPrefabRawDataViewer> PrefabRawDataViewer;

	TArray<TWeakObjectPtr<AActor>> SelectedActors;
private:

	void BindCommands();
	//void ExtendMenu();
	void ExtendToolbar();

	FText GetApplyButtonStatusTooltip()const;
	FSlateIcon GetApplyButtonStatusImage()const;

	void OnOpenRawDataViewerPanel();
	void OnOpenPrefabHelperObjectDetailsPanel();
	void SaveEditorState();

	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Outliner(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PrefabRawDataViewer(const FSpawnTabArgs& Args);

	bool IsFilteredActor(const AActor* Actor);
	void OnOutlinerActorDoubleClick(AActor* Actor);
};
