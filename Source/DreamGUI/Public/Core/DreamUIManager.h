// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Containers/Ticker.h"
#include "Core/DreamLayoutPassContext.h"
#include "Core/DreamUIWorldService.h"
#include "Core/DreamWidgetTreeHost.h"
#include "DreamUIManager.generated.h"

struct FDreamUIHelperGizmoRenderParameter;
struct FDreamUIHelperGizmoVertex;
class UMaterialInterface;
class FEditorViewportClient;
class UDreamWidget;
class UDreamVisualBatchMesh;
class UDreamVisual;
class UDreamCanvas;
class UDreamBaseRaycaster;
class UDreamUIBehaviour;
class ULevel;
class UDreamRectBlockData;
class UDreamUIDataAsTexture;
enum class EDreamUIDataAsTexturePixelFormat : uint8;

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIEditorTickMulticastDelegate, float);
class UDreamUIManagerWorldSubsystem;
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIDrawHelperGizmoDelegate, UDreamUIManagerWorldSubsystem* /*Manager*/);

/**
 * A widget that has been created but not yet added to anything -- the state a UMG-style
 * CreateWidget hands back. The manager holds it for two reasons: it is the GC anchor (nothing
 * else references a parentless widget), and holding it in a named, countable place is what makes
 * "created and then forgotten" a visible leak rather than a silent one.
 */
USTRUCT()
struct FDreamParkedWidgetEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UDreamWidget> Widget = nullptr;

	/** Seconds on the world clock, for the optional never-attached diagnostic. */
	UPROPERTY()
	double ParkedAtSeconds = 0.0;
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUI_API UDreamUISelection : public UObject
{
	GENERATED_BODY()

public:
	/** Scriptable so editor automation (the unreal-bridge) can drive the designer's selection. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Editor")
	static UDreamUISelection* GetInstance(UWorld* InWorld);
	virtual bool IsEditorOnly() const override{return true;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Editor")
	void SelectWidget(UDreamWidget* Widget);
	/** Counterpart of SelectWidget: without one, a Ctrl+click can only ever add. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Editor")
	void DeselectWidget(UDreamWidget* Widget);
	void SelectComponent(UDreamUIBehaviour* Component);
	void ClearComponentSelection();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Editor")
	void SelectNone();
	bool IsSelected(UDreamWidget* Widget)const;
	TArray<TWeakObjectPtr<UDreamWidget>> GetSelectedWidgets()const{return SelectedWidgetArray;}
	TArray<TWeakObjectPtr<UDreamUIBehaviour>> GetSelectedComponents()const{return SelectedComponentArray;}
	FSimpleMulticastDelegate OnSelectionChanged;
private:
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamWidget>> SelectedWidgetArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamUIBehaviour>> SelectedComponentArray;
};

class IDreamUICultureChangedInterface;
enum class EDreamRenderMode : uint8;

/** Which kind of pointer a player needs a raycaster for. See UDreamUIInputServices::EnsureInteractionForPlayer. */
UENUM()
enum class EDreamInteractionKind : uint8
{
	Screen,
	World,
};

class FDreamUILayoutTree
{
public:
	/**
	 * Weak on purpose. This cache lives outside UPROPERTY reflection, so a TObjectPtr here is invisible
	 * to the garbage collector: it neither keeps a widget alive nor gets cleared when one goes away,
	 * which left IsValid() being asked about memory that may already have been recycled. What keeps a
	 * listed widget alive is its tree's host, or the manager's pool of trees no host holds.
	 */
	TArray<TWeakObjectPtr<UDreamWidget>> WidgetArray;
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUI_API UDreamUIManagerWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** The engine's three, and editor previews: a preview's widgets need a manager as much as a level's. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection)override;
	virtual void PostInitialize()override;
	virtual void Deinitialize()override;
	virtual void BeginDestroy() override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor()const override{ return false; }//use Ticker in editor, because Ticker can also tick when drag vector2/3 value while normal tick can't
	virtual void Tick(float DeltaTime)override;
	void TickDreamUI(float DeltaTime);
	void OnWorldPreSendAllEndOfFrameUpdates(UWorld* InWorld);
#if WITH_EDITOR
	void DrawHelperGizmo();
#endif
	void SubmitCanvasDrawCall();
	virtual bool IsTickableWhenPaused() const override;
	bool HasBegunPlay()const{return bHasCalledBeginPlay;}

	/** See LastLayoutPassCount. One is the only healthy value. */
	int32 GetLastLayoutPassCount()const{return LastLayoutPassCount;}

	/**
	 * Moves on whenever what a ray would hit in this world may have changed: a canvas updating (layout,
	 * transform, visibility, geometry and sort all reach the draw calls through one), a canvas or a
	 * raycaster coming or going, a widget's active, visible, raycastable or interactable state being
	 * worked out again. The input system traces a pointer that has not moved again only when this has
	 * moved since its last trace -- an idle screen costs no raycasts.
	 */
	uint64 GetHitTestGeneration()const{ return HitTestGeneration; }
	void BumpHitTestGeneration(){ ++HitTestGeneration; }
	/** BumpHitTestGeneration on InWorldContext's manager, when it has one. */
	static void BumpHitTestGenerationFor(const UObject* InWorldContext);

	/** The layout-pass state of this world's widgets; see UDreamWidget::GetLayoutPassContext. */
	FDreamLayoutPassContext& GetLayoutPassContext() { return LayoutPassContext; }
	const FDreamLayoutPassContext& GetLayoutPassContext() const { return LayoutPassContext; }

	/**
	 * Enrol a DreamGUI service of this world for the world's teardown (IDreamUIWorldService). Every service
	 * does it from its Initialize, through DreamUI::EnrolWorldService, whichever module it lives in. Kept
	 * weakly: a service already gone when the teardown comes is skipped.
	 */
	void RegisterWorldService(UObject* InServiceObject, IDreamUIWorldService* InService);
	void UnregisterWorldService(const UObject* InServiceObject);
	/** Whether InServiceObject is enrolled for this world's teardown and has not been taken down yet. */
	bool HasWorldService(const UObject* InServiceObject) const;
	/**
	 * The one path this world's DreamGUI comes down by, taken once: every enrolled service, highest
	 * priority first; then every widget tree still registered; then the manager's own state. A game
	 * world's EndPlay takes it, and any world's cleanup takes it when nothing did before -- which is how
	 * an editor or preview world comes down. Deinitialize only checks that it was taken.
	 */
	void TeardownWorld();
	bool HasTornDownWorld() const { return bWorldTornDown; }

	static UDreamUIManagerWorldSubsystem* GetInstance(UWorld* InWorld);

	/**
	 * The rows this world's rect blocks of InData keep their shapes in: one set per world and rect block data asset,
	 * made when the first of those rect blocks asks, and gone with the world.
	 *
	 * They used to live in the asset, which every world shares -- the editor's, each play session's, each preview's --
	 * so a rect block in one world grew the texture every other world drew with, and rows a play session never gave
	 * back were still taken in the next one. The asset keeps what it is for, the material.
	 */
	UDreamUIDataAsTexture* GetRectBlockDataRows(UDreamRectBlockData* InData, int32 InBlockSizeInBytes, EDreamUIDataAsTexturePixelFormat InPixelFormat);
#if WITH_EDITOR
	/**
	 * Broadcast on every editor tick of a world nobody plays -- the level editor's, a preview's -- for what
	 * animates there without play: a canvas scaler following its viewport, an image sequence previewing, a
	 * UMG widget shown in the editor. It is this world's, and goes with it.
	 */
	FDreamUIEditorTickMulticastDelegate& GetEditorTickDelegate() { return EditorTick; }
	bool bShouldTickInEditor = false;
	UDreamUISelection* GetSelection()const;
	FSimpleMulticastDelegate OnDeinitialize;
	FSimpleMulticastDelegate OnEndPlay;
	FSimpleMulticastDelegate OnDreamUIWidgetOutlinerChanged;
	/**
	 * Broadcast at the end of DrawHelperGizmo, for the editor helpers that belong to someone else: the
	 * input system draws the selectables' navigation arrows from here, with DrawNavigationArrow.
	 */
	FDreamUIDrawHelperGizmoDelegate OnDrawHelperGizmo;
	void MarkDreamUIWidgetOutlinerChanged();
private:
	bool bDreamUIWidgetOutlinerChanged = true;
#endif
	
private:
#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorTickDelegateHandle;
	FDreamUIEditorTickMulticastDelegate EditorTick;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	mutable TObjectPtr<UDreamUISelection> Selection;
#endif
	/** See GetRectBlockDataRows. */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TMap<TObjectPtr<UDreamRectBlockData>, TObjectPtr<UDreamUIDataAsTexture>> RectBlockDataRows;
	
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamCanvas>> AllCanvasArray;
	/**
	 * Every registered widget, weakly: registering is not owning. A tree is kept alive by its host --
	 * the component, subsystem or preview that made it -- and the host lets it go; see FreeRoots for the
	 * trees no host holds.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamWidget>> RegisteredWidgets;
	/**
	 * The roots of registered trees nothing but this manager holds: made with no host (CreateDreamWidget
	 * with no parent, a Blueprint's ConstructWidget, a test's widget made straight in the world), or taken
	 * off their parent while registered. The pool is the manager's to empty -- on attach, on destroy, and
	 * at the world's teardown -- and nothing in it is reported as a leak.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "DreamGUI")
	TArray<TObjectPtr<UDreamWidget>> FreeRoots;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<FDreamParkedWidgetEntry> ParkedWidgets;
	/** The objects that host trees in this world (IDreamWidgetTreeHost), for the world's teardown to ask. */
	TArray<TWeakObjectPtr<UObject>> TreeHosts;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamBaseRaycaster>> AllRaycasterArray;
	/**
	 * Every registered selectable, as the behaviour it is. The navigation scan and the input system walk
	 * it; neither needs the core to know the selectable class, which belongs to the input system.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> AllSelectableArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UObject>> AllCultureChangedArray;

public:
	/** Register a user widget whose class carries property bindings, so they are evaluated per frame. */
	void AddPropertyBindingUser(class UDreamUserWidget* InUserWidget);
	void RemovePropertyBindingUser(class UDreamUserWidget* InUserWidget);
	/**
	 * How many user widgets are on the per-frame polled-binding visit.
	 *
	 * Exposed because "is this widget still being polled once it has left play" is otherwise
	 * unobservable: a widget unregistered without being destroyed is never marked garbage, so the
	 * list's own IsValid sweep cannot answer it and neither can a test.
	 */
	int32 GetPropertyBindingUserCount() const { return PropertyBindingUsers.Num(); }
private:
	/** Weak, and swept as it is walked: a widget can be destroyed between two frames. */
	TArray<TWeakObjectPtr<class UDreamUserWidget>> PropertyBindingUsers;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> DreamUIBehavioursForTick;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> DreamUIBehavioursForStart;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamWidget>> LayoutDirtyWidgetArray;
	
	/** Weak for the same reason as FDreamUILayoutTree::WidgetArray: neither container is a UPROPERTY. */
	TMap<TWeakObjectPtr<UDreamWidget>, FDreamUILayoutTree> MapWidgetToLayoutTree;
	TSet<TWeakObjectPtr<class UDreamLayoutContainer>> LayoutContainerArrayWhichHasSnapshot;

	bool bIsExecutingStart = false;
	bool bIsExecutingTick = false;
	bool bIsExecutingLayout = false;
	/** A tree rebuild was asked for while a pass was running, and is owed as soon as it ends. */
	bool bPendingLayoutTreeRebuild = false;
	/**
	 * Passes the layout loop needed on the most recent tick that had anything to do.
	 *
	 * One means the frame settled without re-dirtying itself, which is the only healthy number: every
	 * pass beyond the first is work caused by the previous pass rather than by anything the user did.
	 * It used to be visible only under a debug macro, so the difference between "converged" and "ran
	 * eight times and happened to agree" was unobservable in a normal build.
	 */
	int32 LastLayoutPassCount = 0;
	/** See GetHitTestGeneration. */
	uint64 HitTestGeneration = 0;
	/** The writer stack, pass depth and desired-size memo every layout pass in this world shares. */
	FDreamLayoutPassContext LayoutPassContext;
	struct FWorldServiceEntry
	{
		TWeakObjectPtr<UObject> Object;
		IDreamUIWorldService* Service = nullptr;
	};
	/** What TeardownWorld takes down, in the order the services enrolled. */
	TArray<FWorldServiceEntry> WorldServices;
	bool bWorldTornDown = false;
	void HandleWorldCleanup(UWorld* InWorld, bool bInSessionEnded, bool bInCleanupResources);
	/** A level leaving this world takes the trees of the hosts in it down with it. */
	void HandleLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld);
	/** Ask every registered host to let its trees go, for InReason; the hosts in InOnlyLevel only, when given. */
	void ReleaseHostTrees(EDreamTreeReleaseReason InReason, const ULevel* InOnlyLevel = nullptr);
	int32 CurrentExecutingTickIndex = -1;
	UPROPERTY(Transient) TArray<UDreamUIBehaviour*> DreamUIBehavioursNeedToRemoveFromTick;
#if !UE_BUILD_SHIPPING
	/** Paired with the per-frame "only one ScreenSpaceOverlay canvas" check, which is not editor-only. */
	int32 PrevScreenSpaceOverlayCanvasCount = 1;
#endif
#if WITH_EDITORONLY_DATA
	TMap<FString, int> LayoutCalculationCounterMap;
#endif
	void OnCultureChanged();
	bool bShouldUpdateOnCultureChanged = false;
	FDelegateHandle OnCultureChangedDelegateHandle;

	TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> MainViewportViewExtension;
public:
#if WITH_EDITOR
	static void RefreshAllUI(UWorld* InWorld = nullptr);
	FSimpleMulticastDelegate EventOnOutlineChanged;
#endif
	
	const TArray<TWeakObjectPtr<UDreamCanvas>>& GetAllCanvasArray()const{return AllCanvasArray;}
	void AddCanvas(UDreamCanvas* InCanvas);
	void RemoveCanvas(UDreamCanvas* InCanvas);
	/**
	 * The registered canvases as they are now, for a loop whose calls may register or unregister a
	 * canvas -- updating a root canvas can make a render target and tell whoever listens, and a listener
	 * may add or remove a canvas. A ranged-for over the registry itself asserts the moment that happens.
	 * Check each entry with IsCanvasStillRegistered before calling into it.
	 */
	TArray<TWeakObjectPtr<UDreamCanvas>> SnapshotCanvases()const{return AllCanvasArray;}
	/** Whether a canvas from a snapshot is alive and still registered here. */
	bool IsCanvasStillRegistered(const TWeakObjectPtr<UDreamCanvas>& InCanvas)const{return InCanvas.IsValid() && AllCanvasArray.Contains(InCanvas);}
	TArray<UDreamCanvas*> GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode)const;
	/**
	 * Root canvases in ScreenSpaceOverlay mode that are actually competing for the screen. Inactive
	 * ones are excluded: a parked widget draws nothing (DreamCanvas gates UpdateVisual on
	 * GetRenderVisibleInHierarchy), so counting it would report a conflict that does not exist.
	 * Extracted from the per-frame check so the rule can be asserted directly.
	 *
	 * Available outside the editor because the rule it checks is a runtime one -- two overlay
	 * canvases in one world make one of the two UIs disappear in a packaged game just as surely.
	 */
	int32 CountCompetingScreenSpaceOverlayCanvases()const;

	/** Every widget registered here and still alive, in registration order. */
	TArray<UDreamWidget*> GetRegisteredWidgets()const;
	bool IsWidgetRegistered(const UDreamWidget* InWidget)const;
	/**
	 * Whether InRoot, a hierarchy root, is held by something other than this manager: the tree it is the
	 * root of is outered to a host, or it is outered to one itself. A root outered to the world, to this
	 * manager, or to a widget -- one taken off its parent -- has nobody else, and is pooled (FreeRoots).
	 */
	bool IsHeldByHost(const UDreamWidget* InRoot)const;
	/** Pool InRoot if it is a registered hierarchy root no host holds; see FreeRoots. */
	void AdoptIfFreeRoot(UDreamWidget* InRoot);
	/** Let InWidget out of the pool: it has a parent now, or is being destroyed. */
	void ForgetFreeRoot(const UDreamWidget* InWidget);
	bool IsFreeRoot(const UDreamWidget* InWidget)const;
	/**
	 * Enrol InHost -- an object implementing IDreamWidgetTreeHost -- as a host of trees in this world, so
	 * the world's teardown and a level's removal can ask it to let them go. Kept weakly; idempotent.
	 */
	void RegisterTreeHost(UObject* InHost);
	void UnregisterTreeHost(const UObject* InHost);
	bool IsTreeHostRegistered(const UObject* InHost)const;
	/**
	 * Hold a freshly created widget in the not-yet-added state: set its parked bit so it draws
	 * nothing and its behaviours stay disabled, and keep a reference so the caller is not the only
	 * thing standing between it and GC. The widget's own active flag is left alone, so a caller can
	 * still switch it off while configuring and have that stick once it is added.
	 */
	void ParkWidget(UDreamWidget* InWidget);
	/**
	 * Take a widget out of the parked set, which lets its own active flag take effect. Returns false
	 * for a widget that was never parked, which is the common case -- this runs on every attach,
	 * including every child restored by the prefab loader.
	 */
	bool UnparkWidget(UDreamWidget* InWidget);
	bool IsWidgetParked(const UDreamWidget* InWidget)const;
	const TArray<FDreamParkedWidgetEntry>& GetParkedWidgets()const{return ParkedWidgets;}
	/**
	 * Report and destroy widgets that were created and never added, once they are older than
	 * UDreamUISettings::GetParkedWidgetLifetimeSeconds. Destroying is the point: merely dropping the
	 * reference would leave GC to collect a still-registered widget, and that path logs the
	 * "not destroyed by its owner" error from an unrelated stack. Returns how many it took.
	 */
	int32 SweepExpiredParkedWidgets();
	void AddWidget(UDreamWidget* InWidget);
	void RemoveWidget(UDreamWidget* InWidget);
	/**
	 * Tears down registered widgets once per hierarchy root, and empties the pool of free roots. With
	 * bInReportTreesOutlivingHosts, a tree whose host still holds it is reported first: its host was asked
	 * to let it go (ReleaseHostTrees) and did not. Safe to call repeatedly during world shutdown.
	 */
	void DestroyRegisteredWidgetTrees(bool bInReportTreesOutlivingHosts = false);

	/** Ask for a layout pass on this widget next frame -- UMG's InvalidateLayoutAndVolatility. */
	void AddLayoutDirtyWidget(UDreamWidget* InWidget);
	void MarkRebuildLayoutTree(UDreamWidget* InWidget);
	void MarkRebuildAllLayoutTree();
	/** Do the tree rebuild a mid-pass caller was made to wait for. Called once the pass is over. */
	void FlushPendingLayoutTreeRebuild();
	/** Lay this widget's tree out NOW rather than next frame -- UMG's ForceLayoutPrepass. */
	void RebuildLayoutImmediately(UDreamWidget* InWidget);
	void CalculateLayoutTree(UDreamWidget* RootLayoutWidget);
#if WITH_EDITOR
	int IncreateLayoutCalculationCounter(const FString& InPathName);
#endif

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	static void RegisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	static void UnregisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem);

	static TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist);

	const TArray<TWeakObjectPtr<UDreamBaseRaycaster>>& GetAllRaycasterArray(){ return AllRaycasterArray; }
	static void AddRaycaster(UDreamBaseRaycaster* InRaycaster);
	static void RemoveRaycaster(UDreamBaseRaycaster* InRaycaster);

	const TArray<TWeakObjectPtr<UDreamUIBehaviour>>& GetAllSelectableArray() { return AllSelectableArray; }
	static void AddSelectable(UDreamUIBehaviour* InSelectable);
	static void RemoveSelectable(UDreamUIBehaviour* InSelectable);
	
#if WITH_EDITOR
	/**
	 * Editor raycast hit all visible UIBaseRenderable object.
	 * @param InWorld
	 * @param InWidgets
	 * @param LineStart
	 * @param LineEnd
	 * @param ResultSelectTarget
	 * @param InOutTargetIndexInHitArray	Pass in desired item index, and result selected item index. Default is -1, will use first one as result.
	 * \return 
	 */
	static bool RaycastHitUI(UWorld* InWorld, const TArray<UDreamWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
		, UDreamWidget*& ResultSelectTarget, int& InOutTargetIndexInHitArray
	);
	void DrawFrameOnWidget(UDreamWidget* InItem, bool ScreenOrWorld = false);
	void DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, void* Object, const FString& DebugName, bool ScreenOrWorld = false);
	FEditorViewportClient* GetEditorViewportClient();
	
	static void DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld, FVector2D const& Rect, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
	/** A cross with a small diamond at the widget's own origin: where its pivot actually sits. */
	static void DrawDebugPivot(UWorld* InWorld, const FMatrix& LocalToWorld, float Size, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
	static void DrawDebugBox(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld, FVector const& Box, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
	static void DrawDebugLine(UWorld* InWorld, const FMatrix& LocalToWorld, const TArray<FVector3f>& LinePoints, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
private:
	//this is cached when call GetEditorViewportClient
	FEditorViewportClient* CacheViewportClient = nullptr;
	void OnEndOfFrame();
	void OnEnginePreExit();
#endif
public:

	static void AddDreamUIBehavioursForTick(UDreamUIBehaviour* InComp);
	static void RemoveDreamUIBehavioursFromTick(UDreamUIBehaviour* InComp);
	static void AddDreamUIBehavioursForStart(UDreamUIBehaviour* InComp);
	static void RemoveDreamUIBehavioursFromStart(UDreamUIBehaviour* InComp);
};

namespace DreamUI
{
	/**
	 * What a world service's Initialize does to take part in its world's teardown: enrol with the world's
	 * manager, making the manager first if it is not yet. False where the world has no manager -- a
	 * commandlet's, a game preview's -- and the service then takes itself down in its own Deinitialize.
	 */
	DREAMGUI_API bool EnrolWorldService(FSubsystemCollectionBase& InCollection, UObject& InServiceObject, IDreamUIWorldService& InService);
}
