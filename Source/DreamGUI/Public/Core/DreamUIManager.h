// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Tickable.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectKey.h"
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
class UDreamUIRenderLayerTable;
class UTexture;
enum class EDreamUIDataAsTexturePixelFormat : uint8;
struct FDreamGradient;
/** The paint rows' bookkeeping, defined where the paint rows are (DreamUIManager_Registry.cpp). */
struct FDreamUIPaintRowsState;

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
	/**
	 * Whether a world in InNetMode gets a manager: every mode but NM_DedicatedServer. A dedicated server -- a server
	 * process, or a play-in-editor server -- draws nothing, so it gets no manager and none of what one makes (the
	 * canvases' draw data, render resources, the paint rows). ShouldCreateSubsystem asks it with the world's net mode,
	 * and with NM_DedicatedServer whenever IsRunningDedicatedServer().
	 */
	static bool ShouldRunForNetMode(ENetMode InNetMode);
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
	 * Broadcast on every tick of this world's DreamGUI once the frame's layout passes are done -- every tick, whether a
	 * pass ran or not -- before the transform flush, the clips and the root canvases' update. For what places something
	 * against the frame's settled layout: the popup layer re-places its open popups after their openers here, and checks
	 * that the openers are still usable. A listener may move widgets (the flush right after announces the moves, this
	 * frame) and may mark layout dirty (the passes run again right after the broadcast, this frame); it must not destroy
	 * or reparent widgets of a tree a pass is walking -- there is none while it runs. Game thread. Costs nothing while
	 * nothing is bound: subscribe while there is work, unsubscribe after.
	 */
	FSimpleMulticastDelegate& GetOnLayoutPassesFinished() { return OnLayoutPassesFinished; }
	/**
	 * Broadcast on every tick of this world's DreamGUI right before the root canvases update their draw calls, after the
	 * transform flush. For what must repaint this frame and had no earlier moment to ask: the small-text sweep marks the
	 * vertices of texts whose device scale has settled dirty here. A listener may mark geometry dirty; it must not change
	 * layout or the widget tree, since nothing lays out again before the canvases update. Game thread. Costs nothing while
	 * nothing is bound.
	 */
	FSimpleMulticastDelegate& GetOnBeforeRootCanvasesUpdate() { return OnBeforeRootCanvasesUpdate; }

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

	/**
	 * Announce every widget transform change marked in this world since the last flush
	 * (UDreamWidget::CalculateObjectToWorldTransform): each moved widget's canvas and visual are told once,
	 * and its OnTransformChanged listeners hear it once, however often it moved. TickDreamUI runs it after
	 * the layout pass, so the clips and the canvases it goes on to update see every move of the frame so
	 * far; the world's end-of-frame updates run it again, for what moved after the tick.
	 *
	 * A listener that moves a widget again is heard in a further pass. Passes that never settle are cut off
	 * at a limit, with a warning, and what is left waits for the next flush.
	 */
	void FlushTransformChanges();
	/**
	 * Whether a transform change marked in this world waits for FlushTransformChanges rather than being
	 * announced on the spot: r.DreamUI.DeferTransformNotifications is on, this manager ticks, and its world
	 * has not been torn down.
	 */
	bool DefersTransformChanges()const;
	/** A widget whose own transform changed, where the next flush starts from. Once per change; the flush takes each once. */
	void AddTransformChangeRoot(UDreamWidget* InWidget);

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
	/**
	 * This world's rows of the default rect block data, as the texture a built-in draw binds for the rect blocks it draws
	 * (UDreamRectBlock::IsDrawnByBuiltInShader); null until a rect block of the default data made them.
	 */
	UTexture* GetBuiltInRectBlockRowsTexture() const;
	/**
	 * Where this world's render layers stand on their canvases, for the shaders (UDreamUIRenderLayerTable): made when the
	 * first canvas makes a layer, and gone with the world. Flushed once a frame, after the canvases placed their layers.
	 */
	UDreamUIRenderLayerTable* GetRenderLayerTable();

	/*
	 * PAINT ROWS: the world's gradients for painted text (FDreamTextPaint), as one RGBA32F texture of rows
	 * DreamPaintRows::RowWidth pixels wide -- a text table per painted text, a gradient row per distinct gradient, shared by
	 * every text that paints with it (DreamPaintRows has the layout). Made by PostInitialize, before any canvas binds its
	 * textures -- a canvas made earlier would bind the fallback until its next rebuild -- and gone with the world. Game
	 * thread throughout. Rows are written as they are asked for and sent to the render thread together once a frame, before
	 * the canvases submit their draw calls (FlushPaintRows); a write after that goes with the next frame's.
	 */
	/** Make the paint rows, once; what PostInitialize calls. */
	void CreatePaintRows();
	/** The paint rows, as a built-in draw and a material bind them (DreamUIShadeMaterial::PaintDataTextureParameter); null when there are none. */
	UTexture* GetPaintRowsTexture() const;
	/**
	 * A text table for a painted text: a row of its own, every pixel NoRow-filled for its slots and 0 elsewhere until the
	 * text writes it. INDEX_NONE when there are no paint rows, or DreamPaintRows::MaxTextTableRows are taken: the text then
	 * paints nothing. The text gives it back with ReleasePaintTextRow when it stops painting, leaves the world, or goes.
	 */
	int32 AcquirePaintTextRow();
	void ReleasePaintTextRow(int32 InRow);
	/**
	 * The row InGradient is drawn from (FDreamGradient::PackRow): one already holding exactly those pixels, shared, or a new
	 * row written with them. Every acquire is matched by one ReleasePaintGradientRow; the row is free again once the last
	 * holder released it. Changing a text's gradient is releasing the old row and acquiring the new one -- a row is never
	 * rewritten under its other holders. INDEX_NONE when there are no paint rows.
	 */
	int32 AcquirePaintGradientRow(const FDreamGradient& InGradient);
	void ReleasePaintGradientRow(int32 InRow);
	/** Write InPixels into row InRow from pixel InFirstPixel on: a text writing its own table. Gradient rows are written by acquiring them only. */
	void WritePaintRowPixels(int32 InRow, int32 InFirstPixel, TConstArrayView<FVector4f> InPixels);
	/** Send the rows written since the last flush to the render thread, in one go. SubmitCanvasDrawCall calls it each frame. */
	void FlushPaintRows();
	/** For the memory report: rows the texture has, text tables and gradient rows taken, and the texture's bytes. */
	void GetPaintRowsMemoryInfo(int32& OutTextureRows, int32& OutTextRows, int32& OutGradientRows, int64& OutTextureBytes) const;
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
	/** The editor ticker's call for this world: every frame, or less often for the level being edited while a session plays. */
	void TickFromEditorTicker(float DeltaTime);
	/** Time the editor ticker has held back while a session plays (r.DreamUI.EditorWorldTickIntervalDuringPlay). */
	float EditorTickHeldBackSeconds = 0.0f;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	mutable TObjectPtr<UDreamUISelection> Selection;
#endif
	/** See GetRectBlockDataRows. */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TMap<TObjectPtr<UDreamRectBlockData>, TObjectPtr<UDreamUIDataAsTexture>> RectBlockDataRows;
	/** See GetRenderLayerTable. */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UDreamUIRenderLayerTable> RenderLayerTable;
	/** See CreatePaintRows: the rows, the manager's, and never saved, duplicated or copied, as the rect block rows. */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UDreamUIDataAsTexture> PaintRows;
	/** What the paint rows hold: which rows are taken, and the gradient rows by content with their holders. Made with PaintRows. */
	TSharedPtr<FDreamUIPaintRowsState> PaintRowsState;
	
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamCanvas>> AllCanvasArray;
	/**
	 * The root canvases ForEachRootCanvasInRenderModeOrder takes, one list per render mode in the order it takes them, as
	 * they were sorted at RootCanvasOrderGeneration (InvalidateRootCanvasOrder). Sorting them was a walk of the whole
	 * registry, twice a frame, asking each canvas for its root and its mode.
	 */
	TArray<TWeakObjectPtr<UDreamCanvas>> RootCanvasesByPass[4];
	/**
	 * RootCanvasesByPass as weak look-ups found them while the count of objects gone read RootCanvasesRawGone
	 * (DreamUIGone): while the count reads the same, each is that canvas, alive and as registered as it was, and the
	 * passes take them without a look-up of the object array for each -- two passes a frame over every panel of a world.
	 */
	TArray<UDreamCanvas*> RootCanvasesByPassRaw[4];
	uint64 RootCanvasesRawGone = 0;
	/** See GetAllCanvasesResolved: the canvases, and the order generation and count of objects gone they were found at. */
	TArray<UDreamCanvas*> AllCanvasesResolved;
	uint64 AllCanvasesResolvedOrder = 0;
	uint64 AllCanvasesResolvedGone = 0;
	uint64 RootCanvasOrderGeneration = 0;
	/** RootCanvasesByPass sorted again, if a canvas came or went since (InvalidateRootCanvasOrder). */
	void SortRootCanvasesIfStale();
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
	/**
	 * Whether InBehaviour is on the per-frame tick visit. Exposed for the same reason: a behaviour whose Tick does nothing
	 * (UDreamUIBehaviour::DeclareTickUnused) is kept off it, and nothing else can tell.
	 */
	bool IsBehaviourOnTickVisit(const UDreamUIBehaviour* InBehaviour) const;
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
	/** See GetOnLayoutPassesFinished and GetOnBeforeRootCanvasesUpdate. */
	FSimpleMulticastDelegate OnLayoutPassesFinished;
	FSimpleMulticastDelegate OnBeforeRootCanvasesUpdate;
	/**
	 * The layout passes, until nothing is dirty or the per-run cap: the body of TickDreamUI's layout step, which runs a
	 * second time when the post-layout listeners dirtied layout. InOutPassesThisTick counts across both runs, and is what
	 * LastLayoutPassCount reports.
	 */
	void RunLayoutPasses(int32& InOutPassesThisTick);
	/** See GetHitTestGeneration. */
	uint64 HitTestGeneration = 0;
	/**
	 * The widgets whose own transform changed since the last flush, in the order they changed, weakly and
	 * with repeats: a flush skips one it has already reached. See FlushTransformChanges.
	 */
	TArray<TWeakObjectPtr<UDreamWidget>> TransformChangeRoots;
	/** A pass of the flush's roots, swapped out of TransformChangeRoots so that neither array gives up its memory. */
	TArray<TWeakObjectPtr<UDreamWidget>> TransformChangeRootsBeingFlushed;
	/** A flush is running. A listener's move lands in its next pass, not in a flush of its own. */
	bool bIsFlushingTransformChanges = false;
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
	/** The frame the world's end-of-frame updates last submitted the canvases in, and whether they updated since. */
	uint64 LastEndOfFrameSubmitFrame = MAX_uint64;
	bool bCanvasesUpdatedSinceSubmit = true;
	/**
	 * The canvases that asked to be drawn to their render targets (AddRenderTargetDrawRequest) and to be sorted
	 * (AddRenderPrioritySortRequest): what the submit and the tick look at, instead of every registered canvas every frame.
	 */
	TArray<TWeakObjectPtr<UDreamCanvas>> RenderTargetDrawRequests;
	TArray<TWeakObjectPtr<UDreamCanvas>> RenderPrioritySortRequests;
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
	/**
	 * GetAllCanvasArray with each canvas as a weak look-up found it while the count of objects gone read the same as now
	 * -- null for one gone -- made again only once a canvas came or went or the count moved since. Game thread: what a
	 * raycast hands to as many threads as there are, which then read the canvases without a look-up of the object
	 * array for each of a world of panels.
	 */
	const TArray<UDreamCanvas*>& GetAllCanvasesResolved();
	void AddCanvas(UDreamCanvas* InCanvas);
	void RemoveCanvas(UDreamCanvas* InCanvas);
	/** InCanvas asks to be drawn to its render target once this frame's sections have gone (UDreamCanvas::DrawRenderTargetIfRequested). */
	void AddRenderTargetDrawRequest(UDreamCanvas* InCanvas);
	/** InCanvas asks for its draw calls to be sorted by render priority at the next tick (UDreamCanvas::ConsumePendingRenderPrioritySort). */
	void AddRenderPrioritySortRequest(UDreamCanvas* InCanvas);
	/**
	 * The registered canvases as they are now, for a loop whose calls may register or unregister a
	 * canvas -- updating a root canvas can make a render target and tell whoever listens, and a listener
	 * may add or remove a canvas. A ranged-for over the registry itself asserts the moment that happens.
	 * Check each entry with IsCanvasStillRegistered before calling into it.
	 */
	TArray<TWeakObjectPtr<UDreamCanvas>> SnapshotCanvases()const{return AllCanvasArray;}
	/**
	 * Calls InFunction on every registered root canvas, screen space first, then world space, then render targets, by
	 * each canvas's actual render mode or the one it is set to. Safe against calls that register or unregister canvases.
	 */
	void ForEachRootCanvasInRenderModeOrder(bool bInActualRenderMode, TFunctionRef<void(UDreamCanvas*)> InFunction);
	/**
	 * A canvas came or went, found another root, or changed its render mode: every manager sorts its root canvases again
	 * before its next pass over them (ForEachRootCanvasInRenderModeOrder). Any thread.
	 */
	static void InvalidateRootCanvasOrder();
	/** Whether a canvas from a snapshot is alive and still registered here. */
	bool IsCanvasStillRegistered(const TWeakObjectPtr<UDreamCanvas>& InCanvas)const;
	TArray<UDreamCanvas*> GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode)const;
	/**
	 * The largest number of active ScreenSpaceOverlay root canvases in any one UI layer: INDEX_NONE
	 * is shared, each viewport player index is its own layer. More than one in a layer competes for
	 * its projection. Inactive roots are excluded: a parked widget draws nothing, so it cannot compete.
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
