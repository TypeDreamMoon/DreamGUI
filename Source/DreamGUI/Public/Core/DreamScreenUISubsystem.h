// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamScreenUISubsystem.generated.h"

class AActor;
class UDreamCanvas;
class UDreamUIPrefab;
class UDreamWidget;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EDreamUIScreenPageState : uint8
{
	Unloaded,
	Loading,
	Inactive,
	Active,
};

UENUM(BlueprintType)
enum class EDreamUIScreenPageCachePolicy : uint8
{
	DestroyOnPop,
	KeepAlive,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamUIScreenPageEvent, FName, PageName, UDreamWidget*, Page);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamUIScreenStackChangedEvent, FName, PreviousTop, FName, NewTop);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FDreamUIScreenPageAsyncCallback, FName, PageName, UDreamWidget*, Page, bool, bSuccess);

/** UMG-style viewport page management backed by one DreamUI screen-space root canvas. */
UCLASS()
class DREAMGUI_API UDreamScreenUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamScreenUISubsystem* Get(UWorld* InWorld);

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Screen|Events")
	FDreamUIScreenPageEvent OnPageCreated;

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Screen|Events")
	FDreamUIScreenPageEvent OnPageShown;

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Screen|Events")
	FDreamUIScreenPageEvent OnPageHidden;

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Screen|Events")
	FDreamUIScreenPageEvent OnPageRemoved;

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Screen|Events")
	FDreamUIScreenStackChangedEvent OnStackChanged;

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Screen UI Subsystem"), Category = "DreamGUI|Screen")
	static UDreamScreenUISubsystem* GetDreamScreenUISubsystem(UObject* WorldContextObject);

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	UDreamWidget* GetOrCreateScreenRoot();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	UDreamWidget* GetScreenRoot() const { return ScreenRoot; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	UDreamCanvas* GetScreenCanvas() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	UDreamWidget* LoadPrefabToScreen(UDreamUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void AddToViewport(UDreamWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveFromViewport(UDreamWidget* InRoot);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	bool IsInViewport(UDreamWidget* InRoot) const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RegisterUI(FName InName, UDreamWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	UDreamWidget* ShowPrefab(FName InName, UDreamUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	UDreamWidget* GetUI(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	bool IsUIShowing(FName InName) const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void SetUIVisible(FName InName, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveAllUI();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	TArray<FName> GetAllUINames() const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	bool RegisterPageAsset(FName InName, TSoftObjectPtr<UDreamUIPrefab> InPrefab,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::KeepAlive);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	void UnregisterPageAsset(FName InName, bool bRemoveLoadedPage = true);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	TSoftObjectPtr<UDreamUIPrefab> GetPageAsset(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	TArray<FName> GetRegisteredPageNames() const;

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "OnComplete"), Category = "DreamGUI|Screen|Pages")
	void PushPageAsync(FName InName, const FDreamUIScreenPageAsyncCallback& OnComplete, bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	bool CancelPageLoad(FName InName);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	EDreamUIScreenPageState GetPageState(FName InName) const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	UDreamWidget* PushPrefab(FName InName, UDreamUIPrefab* InPrefab,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	void PushUI(FName InName, UDreamWidget* InRoot,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	void PopUI();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	bool PopToUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	void ClearStack(bool bRemovePages = false);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack")
	FName GetTopUI() const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack")
	int32 GetStackDepth() const { return Stack.Num(); }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack")
	bool IsUIInStack(FName InName) const { return Stack.Contains(InName); }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack")
	TArray<FName> GetUIStack() const { return Stack; }

private:
	struct FEntry
	{
		TWeakObjectPtr<UDreamWidget> Root;
		TSoftObjectPtr<UDreamUIPrefab> SourcePrefab;
		int32 SortOrder = 0;
		EDreamUIScreenPageCachePolicy CachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop;
		EDreamUIScreenPageState State = EDreamUIScreenPageState::Inactive;
		bool bHidePrevious = true;
	};

	struct FPageDefinition
	{
		TSoftObjectPtr<UDreamUIPrefab> Prefab;
		EDreamUIScreenPageCachePolicy CachePolicy = EDreamUIScreenPageCachePolicy::KeepAlive;
	};

	struct FPendingPageLoad
	{
		TSharedPtr<FStreamableHandle> Handle;
		TArray<FDreamUIScreenPageAsyncCallback> Callbacks;
		bool bHidePrevious = true;
	};

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> ScreenRoot;

	UPROPERTY(Transient)
	TObjectPtr<AActor> InteractionHost;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CreatedEventSystemActor;

	TMap<FName, FEntry> Entries;
	TMap<FName, FPageDefinition> PageDefinitions;
	TMap<FName, FPendingPageLoad> PendingPageLoads;
	TArray<FName> Stack;
	int32 AutoNameCounter = 0;
	bool bOwnsScreenRoot = false;
	bool bRefreshingStack = false;
	bool bStackRefreshRequested = false;

	static constexpr int32 StackBaseSortOrder = 1000;
	static constexpr int32 StackSortOrderStep = 10;

	bool IsUsablePage(const UDreamWidget* InRoot) const;
	FName FindNameForWidget(const UDreamWidget* InRoot) const;
	void ConfigurePage(UDreamWidget* InRoot, int32 InSortOrder);
	void RegisterUIInternal(FName InName, UDreamWidget* InRoot, int32 InSortOrder,
		EDreamUIScreenPageCachePolicy InCachePolicy, TSoftObjectPtr<UDreamUIPrefab> InSourcePrefab,
		bool bInitiallyVisible);
	void SetPageActive(FName InName, bool bActive);
	void RefreshStack(FName InPreviousTop);
	void RemoveEntry(FName InName, bool bDestroyPage);
	void CompletePageLoad(FName InName);
	void ExecuteLoadCallbacks(FName InName, FPendingPageLoad& InPendingLoad, UDreamWidget* InPage, bool bSuccess);
	void DestroyPage(UDreamWidget* InRoot);
	void EnsureInteractionObjects(UDreamCanvas* InRootCanvas);
};
