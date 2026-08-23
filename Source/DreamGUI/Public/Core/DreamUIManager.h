// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "Containers/Ticker.h"
#include "DreamUIManager.generated.h"

struct FDreamUIHelperGizmoRenderParameter;
struct FDreamUIHelperGizmoVertex;
class UMaterialInterface;
class FEditorViewportClient;
class UDreamEventSystem;
class UDreamWidget;
class UDreamVisualBatchMesh;
class UDreamVisual;
class UDreamCanvas;
class UDreamBaseRaycaster;
class UUISelectable;
class UDreamUIBehaviour;
class UDreamBaseInputModule;

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIEditorTickMulticastDelegate, float);

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

/**
 * This manager is a single instance, mainly for manage DreamUI in Editor
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class DREAMGUI_API UDreamUIManagerObject :public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UDreamUIManagerObject();
	virtual void BeginDestroy()override;
public:
	//begin TickableEditorObject interface
	virtual void Tick(float DeltaTime)override;
	virtual bool IsTickable() const { return Instance == this; }
	virtual bool IsTickableInEditor()const { return Instance == this; }
	virtual TStatId GetStatId() const override;
	virtual bool IsEditorOnly()const override { return true; }
	//end TickableEditorObject interface
private:
	static UDreamUIManagerObject* Instance;
#if WITH_EDITORONLY_DATA
	static bool bIsBlueprintCompiling;
	FDreamUIEditorTickMulticastDelegate EditorTick;
	TArray<TTuple<int, TFunction<void()>>> OneShotFunctionsToExecuteInTick;
public:
	static void AddOneShotTickFunction(const TFunction<void()>& InFunction, int InDelayFrameCount = 0);
	FDreamUIEditorTickMulticastDelegate& GetEditorTickDelegate();

#endif
#if WITH_EDITOR
	static bool GetIsBlueprintCompiling(){return bIsBlueprintCompiling;}
private:
	static bool InitCheck();
public:
	static UDreamUIManagerObject* GetInstance(bool CreateIfNotValid = false);
private:
	FDelegateHandle OnBlueprintPreCompileDelegateHandle;
	FDelegateHandle OnBlueprintCompiledDelegateHandle;
	void OnBlueprintPreCompile(UBlueprint* InBlueprint);
	void OnBlueprintCompiled();
private:
	FDelegateHandle OnAssetReimportDelegateHandle;
	void OnAssetReimport(UObject* Asset);
	FDelegateHandle OnMapOpenedDelegateHandle;
	void OnMapOpened(const FString& FileName, bool AsTemplate);
	FDelegateHandle OnPackageReloadedDelegateHandle;
	void OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
#endif
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUI_API UDreamUISelection : public UObject
{
	GENERATED_BODY()

public:
	static UDreamUISelection* GetInstance(UWorld* InWorld);
	virtual bool IsEditorOnly() const override{return true;}
	void SelectWidget(UDreamWidget* Widget);
	/** Counterpart of SelectWidget: without one, a Ctrl+click can only ever add. */
	void DeselectWidget(UDreamWidget* Widget);
	void SelectComponent(UDreamUIBehaviour* Component);
	void ClearComponentSelection();
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

class FDreamUILayoutTree
{
public:
	TArray<TObjectPtr<UDreamWidget>> WidgetArray;
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUI_API UDreamUIManagerWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
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

	static UDreamUIManagerWorldSubsystem* GetInstance(UWorld* InWorld);
#if WITH_EDITOR
	bool bShouldTickInEditor = false;
	UDreamUISelection* GetSelection()const;
	FSimpleMulticastDelegate OnDeinitialize;
	FSimpleMulticastDelegate OnEndPlay;
	FSimpleMulticastDelegate OnDreamUIWidgetOutlinerChanged;
	void MarkDreamUIWidgetOutlinerChanged();
private:
	bool bDreamUIWidgetOutlinerChanged = true;
#endif
	
private:
#if WITH_EDITOR
	static TArray<UDreamUIManagerWorldSubsystem*> InstanceArray;
	FTSTicker::FDelegateHandle EditorTickDelegateHandle;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	mutable TObjectPtr<UDreamUISelection> Selection;
#endif
	
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamCanvas>> AllCanvasArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TObjectPtr<UDreamWidget>> AllWidgetArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<FDreamParkedWidgetEntry> ParkedWidgets;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamBaseRaycaster>> AllRaycasterArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UUISelectable>> AllSelectableArray;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UObject>> AllCultureChangedArray;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TMap<int, TWeakObjectPtr<UDreamEventSystem>> MapUserIndexToEventSystem;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> DreamUIBehavioursForTick;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> DreamUIBehavioursForStart;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TArray<TWeakObjectPtr<UDreamWidget>> LayoutDirtyWidgetArray;
	
	TMap<TObjectPtr<UDreamWidget>, FDreamUILayoutTree> MapWidgetToLayoutTree;
	TSet<TObjectPtr<class UDreamLayoutContainer>> LayoutContainerArrayWhichHasSnapshot;

	bool bIsExecutingStart = false;
	bool bIsExecutingTick = false;
	bool bIsExecutingLayout = false;
	/**
	 * Passes the layout loop needed on the most recent tick that had anything to do.
	 *
	 * One means the frame settled without re-dirtying itself, which is the only healthy number: every
	 * pass beyond the first is work caused by the previous pass rather than by anything the user did.
	 * It used to be visible only under a debug macro, so the difference between "converged" and "ran
	 * eight times and happened to agree" was unobservable in a normal build.
	 */
	int32 LastLayoutPassCount = 0;
	int32 CurrentExecutingTickIndex = -1;
	UPROPERTY(Transient) TArray<UDreamUIBehaviour*> DreamUIBehavioursNeedToRemoveFromTick;
#if WITH_EDITORONLY_DATA
	int32 PrevScreenSpaceOverlayCanvasCount = 1;
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
	TArray<UDreamCanvas*> GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode)const;
#if WITH_EDITOR
	/**
	 * Root canvases in ScreenSpaceOverlay mode that are actually competing for the screen. Inactive
	 * ones are excluded: a parked widget draws nothing (DreamCanvas gates UpdateVisual on
	 * GetRenderVisibleInHierarchy), so counting it would report a conflict that does not exist.
	 * Extracted from the per-frame check so the rule can be asserted directly.
	 */
	int32 CountCompetingScreenSpaceOverlayCanvases()const;
#endif

	const TArray<TObjectPtr<UDreamWidget>>& GetAllWidgetArray()const{return AllWidgetArray;}
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
	/** Tears down registered widgets once per hierarchy root. Safe to call repeatedly during world shutdown. */
	void DestroyRegisteredWidgetTrees();

	void AddLayoutDirtyWidget(UDreamWidget* InWidget);
	void MarkRebuildLayoutTree(UDreamWidget* InWidget);
	void MarkRebuildAllLayoutTree();
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

	const TArray<TWeakObjectPtr<UUISelectable>>& GetAllSelectableArray() { return AllSelectableArray; }
	static void AddSelectable(UUISelectable* InSelectable);
	static void RemoveSelectable(UUISelectable* InSelectable);

	const TMap<int, TWeakObjectPtr<UDreamEventSystem>>& GetMapUserIndexToEventSystem() { return MapUserIndexToEventSystem; }
	UDreamEventSystem* GetEventSystemByUserIndex(int UserIndex = 0);
	void AddEventSystem(UDreamEventSystem* InEventSystem);
	void RemoveEventSystem(UDreamEventSystem* InEventSystem);
	
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
	void DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectable* InSelectable, bool IsScreenSpace = false);
	FEditorViewportClient* GetEditorViewportClient();
	
	static void DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld, FVector2D const& Rect, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
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
