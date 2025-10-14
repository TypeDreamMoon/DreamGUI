// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "LexUIManager.generated.h"

class ULexWidget;
class ULexVisualBatchMesh;
class ULexVisual;
class ULexCanvas;
class ULexBaseRaycaster;
class UUISelectableComponent;
class ULexUIBehaviour;
class ULexBaseInputModule;

DECLARE_MULTICAST_DELEGATE_OneParam(FLGUIEditorTickMulticastDelegate, float);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FLGUIEditorManagerOnComponentCreateDelete, bool, UActorComponent*, AActor*);

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIEditorManagerObject :public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static ULexUIEditorManagerObject* Instance;
	ULexUIEditorManagerObject();
	virtual void BeginDestroy()override;
public:
	//begin TickableEditorObject interface
	virtual void Tick(float DeltaTime)override;
	virtual bool IsTickable() const { return Instance == this; }
	virtual bool IsTickableInEditor()const { return Instance == this; }
	virtual TStatId GetStatId() const override;
	virtual bool IsEditorOnly()const override { return true; }
	//end TickableEditorObject interface
#if WITH_EDITORONLY_DATA
private:
	TMap<int32, uint32> EditorViewportIndexToKeyMap;
	int32 PrevEditorViewportCount = 0;
	int32 PrevScreenSpaceOverlayCanvasCount = 1;
	FSimpleMulticastDelegate EditorViewportIndexAndKeyChange;
public:
	int32 CurrentActiveViewportIndex = 0;
	uint32 CurrentActiveViewportKey = 0;
	static int IndexOfClickSelectUI;
#endif
#if WITH_EDITOR
	static FDelegateHandle RegisterEditorViewportIndexAndKeyChange(const TFunction<void()>& InFunction);
	static void UnregisterEditorViewportIndexAndKeyChange(const FDelegateHandle& InDelegateHandle);
private:
	static bool InitCheck();
public:
	static ULexUIEditorManagerObject* GetInstance(bool CreateIfNotValid = false);
	void CheckEditorViewportIndexAndKey();
	uint32 GetViewportKeyFromIndex(int32 InViewportIndex);
private:
	FDelegateHandle OnBlueprintPreCompileDelegateHandle;
	FDelegateHandle OnBlueprintCompiledDelegateHandle;
	void OnBlueprintPreCompile(UBlueprint* InBlueprint);
	void OnBlueprintCompiled();
private:
	FDelegateHandle OnAssetReimportDelegateHandle;
	void OnAssetReimport(UObject* asset);
	FDelegateHandle OnActorLabelChangedDelegateHandle;
	void OnActorLabelChanged(AActor* actor);
	FDelegateHandle OnMapOpenedDelegateHandle;
	void OnMapOpened(const FString& FileName, bool AsTemplate);
	FDelegateHandle OnPackageReloadedDelegateHandle;
	void OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
#endif
};

struct FLGUILifeCycleBehaviourArrayContainer
{
	TArray<TWeakObjectPtr<ULexUIBehaviour>> LGUILifeCycleBehaviourArray;
	/** Functions that wait for prefab serialization complete then execute */
	TArray<TFunction<void()>> Functions;
};

class ILexUICultureChangedInterface;
enum class ELexRenderMode : uint8;

UCLASS(NotBlueprintable, NotBlueprintType, Transient, NotPlaceable)
class LGUI_API ULexUIManagerWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
	virtual void Initialize(FSubsystemCollectionBase& Collection)override;
	virtual void PostInitialize()override;
	virtual void Deinitialize()override;

	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor()const override{ return false; }//use Ticker in editor, because Ticker can also tick when drag vector2/3 value while normal tick can't
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override;

	static ULexUIManagerWorldSubsystem* GetInstance(UWorld* InWorld);
#if WITH_EDITOR
	static bool GetIsPlaying() { return bIsPlaying; }
#endif
private:
#if WITH_EDITOR
	static TArray<ULexUIManagerWorldSubsystem*> InstanceArray;
	FTSTicker::FDelegateHandle EditorTickDelegateHandle;
	static bool bIsPlaying;
#endif
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexWidget>> AllRootWidgetArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexCanvas>> ScreenSpaceCanvasArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexCanvas>> WorldSpaceUECanvasArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexCanvas>> WorldSpaceLexCanvasArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexCanvas>> RenderTargetSpaceLexUICanvasArray;

	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexBaseRaycaster>> AllRaycasterArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TWeakObjectPtr<ULexBaseInputModule> CurrentInputModule = nullptr;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<UUISelectableComponent>> AllSelectableArray;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<UObject>> AllCultureChangedArray;

	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexUIBehaviour>> LexUIBehavioursForUpdate;
	UPROPERTY(VisibleAnywhere, Category = "LGUI")
		TArray<TWeakObjectPtr<ULexUIBehaviour>> LexUIBehavioursForStart;
	bool bIsExecutingStart = false;
	bool bIsExecutingUpdate = false;
	int32 CurrentExecutingUpdateIndex = -1;
	UPROPERTY(Transient) TArray<ULexUIBehaviour*> LexUIBehavioursNeedToRemoveFromUpdate;
#if WITH_EDITORONLY_DATA
	int32 PrevScreenSpaceOverlayCanvasCount = 1;
#endif
	void OnCultureChanged();
	bool bShouldUpdateOnCultureChanged = false;
	FDelegateHandle OnCultureChangedDelegateHandle;

	TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> MainViewportViewExtension;
public:
#if WITH_EDITOR
	static void RefreshAllUI(UWorld* InWorld = nullptr);
	static void AddRootWidget(ULexWidget* InWidget);
	static void RemoveRootWidget(ULexWidget* InWidget);
	const TArray<TWeakObjectPtr<ULexWidget>>& GetAllRootUIItemArray()const { return AllRootWidgetArray; }
#endif

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void RegisterLexUICultureChangedEvent(TScriptInterface<ILexUICultureChangedInterface> InItem);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void UnregisterLexUICultureChangedEvent(TScriptInterface<ILexUICultureChangedInterface> InItem);

	static void AddCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode);
	static void RemoveCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode);
	static void CanvasRenderModeChange(ULexCanvas* InCanvas, ELexRenderMode InOldRenderMode, ELexRenderMode InNewRenderMode);
	const TArray<TWeakObjectPtr<ULexCanvas>>& GetCanvasArray(ELexRenderMode RenderMode);

	static TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist);

	const TArray<TWeakObjectPtr<ULexBaseRaycaster>>& GetAllRaycasterArray(){ return AllRaycasterArray; }
	static void AddRaycaster(ULexBaseRaycaster* InRaycaster);
	static void RemoveRaycaster(ULexBaseRaycaster* InRaycaster);

	TWeakObjectPtr<ULexBaseInputModule> GetCurrentInputModule() { return CurrentInputModule; }
	static void SetCurrentInputModule(ULexBaseInputModule* InInputModule);
	static void ClearCurrentInputModule(ULexBaseInputModule* InInputModule);

	const TArray<TWeakObjectPtr<UUISelectableComponent>>& GetAllSelectableArray() { return AllSelectableArray; }
	static void AddSelectable(UUISelectableComponent* InSelectable);
	static void RemoveSelectable(UUISelectableComponent* InSelectable);
	
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
	static bool RaycastHitUI(UWorld* InWorld, const TArray<ULexWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
		, ULexWidget*& ResultSelectTarget, int& InOutTargetIndexInHitArray
	);
	void DrawFrameOnWidget(ULexWidget* InItem, bool ScreenOrWorld = false);
	void DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, void* Object, const FString& DebugName, bool ScreenOrWorld = false);
	void DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectableComponent* InSelectable, bool IsScreenSpace = false);
	FEditorViewportClient* GetEditorViewportClient();
private:
	//this is cached when call GetEditorViewportClient
	FEditorViewportClient* CacheViewportClient = nullptr;
	void OnEndOfFrame();
	static void DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix44f& LocalToWorld, FVector const& Box, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld);
#endif
private:
	/** Map prefab-deserialize-section-id to LexUIBehaviour array */
	TMap<FGuid, FLGUILifeCycleBehaviourArrayContainer> LexUIBehaviours_PrefabSystemProcessing;
	void ProcessLexUILifecycleEvent(ULexUIBehaviour* InComp);
public:
	void BeginPrefabSystemProcessingActor(const FGuid& InSessionId);
	void EndPrefabSystemProcessingActor(const FGuid& InSessionId);
	/**
	 * Add a function that execute after prefab system serialization and before Awake called
	 * @param	InPrefabActor	Current prefab system processing actor
	 * @param	InFunction		Function to call after prefab system serialization complete and before Awake called
	 */
	void AddFunctionForPrefabSystemExecutionBeforeAwake(AActor* InPrefabActor, const TFunction<void()>& InFunction);

	static void AddLexUIBehaviourForLifecycleEvent(ULexUIBehaviour* InComp);
	static void AddLexUIBehavioursForUpdate(ULexUIBehaviour* InComp);
	static void RemoveLexUIBehavioursFromUpdate(ULexUIBehaviour* InComp);
};
