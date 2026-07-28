// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LexScreenUISubsystem.generated.h"

class AActor;
class ULexCanvas;
class ULexUIPrefab;
class ULexWidget;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ELexUIScreenPageState : uint8
{
	Unloaded,
	Loading,
	Inactive,
	Active,
};

UENUM(BlueprintType)
enum class ELexUIScreenPageCachePolicy : uint8
{
	DestroyOnPop,
	KeepAlive,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLexUIScreenPageEvent, FName, PageName, ULexWidget*, Page);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLexUIScreenStackChangedEvent, FName, PreviousTop, FName, NewTop);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FLexUIScreenPageAsyncCallback, FName, PageName, ULexWidget*, Page, bool, bSuccess);

/** UMG-style viewport page management backed by one LexUI screen-space root canvas. */
UCLASS()
class LGUI_API ULexScreenUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static ULexScreenUISubsystem* Get(UWorld* InWorld);

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Screen|Events")
	FLexUIScreenPageEvent OnPageCreated;

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Screen|Events")
	FLexUIScreenPageEvent OnPageShown;

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Screen|Events")
	FLexUIScreenPageEvent OnPageHidden;

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Screen|Events")
	FLexUIScreenPageEvent OnPageRemoved;

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Screen|Events")
	FLexUIScreenStackChangedEvent OnStackChanged;

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get LexUI Screen UI Subsystem"), Category = "LGUI|Screen")
	static ULexScreenUISubsystem* GetLexScreenUISubsystem(UObject* WorldContextObject);

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* GetOrCreateScreenRoot();

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexWidget* GetScreenRoot() const { return ScreenRoot; }

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexCanvas* GetScreenCanvas() const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* LoadPrefabToScreen(ULexUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void AddToViewport(ULexWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveFromViewport(ULexWidget* InRoot);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	bool IsInViewport(ULexWidget* InRoot) const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RegisterUI(FName InName, ULexWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* ShowPrefab(FName InName, ULexUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexWidget* GetUI(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	bool IsUIShowing(FName InName) const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void SetUIVisible(FName InName, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveAllUI();

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	TArray<FName> GetAllUINames() const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Pages")
	bool RegisterPageAsset(FName InName, TSoftObjectPtr<ULexUIPrefab> InPrefab,
		ELexUIScreenPageCachePolicy InCachePolicy = ELexUIScreenPageCachePolicy::KeepAlive);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Pages")
	void UnregisterPageAsset(FName InName, bool bRemoveLoadedPage = true);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Pages")
	TSoftObjectPtr<ULexUIPrefab> GetPageAsset(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Pages")
	TArray<FName> GetRegisteredPageNames() const;

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "OnComplete"), Category = "LGUI|Screen|Pages")
	void PushPageAsync(FName InName, const FLexUIScreenPageAsyncCallback& OnComplete, bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Pages")
	bool CancelPageLoad(FName InName);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Pages")
	ELexUIScreenPageState GetPageState(FName InName) const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Stack")
	ULexWidget* PushPrefab(FName InName, ULexUIPrefab* InPrefab,
		ELexUIScreenPageCachePolicy InCachePolicy = ELexUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Stack")
	void PushUI(FName InName, ULexWidget* InRoot,
		ELexUIScreenPageCachePolicy InCachePolicy = ELexUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Stack")
	void PopUI();

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Stack")
	bool PopToUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen|Stack")
	void ClearStack(bool bRemovePages = false);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Stack")
	FName GetTopUI() const;

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Stack")
	int32 GetStackDepth() const { return Stack.Num(); }

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Stack")
	bool IsUIInStack(FName InName) const { return Stack.Contains(InName); }

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen|Stack")
	TArray<FName> GetUIStack() const { return Stack; }

private:
	struct FEntry
	{
		TWeakObjectPtr<ULexWidget> Root;
		TSoftObjectPtr<ULexUIPrefab> SourcePrefab;
		int32 SortOrder = 0;
		ELexUIScreenPageCachePolicy CachePolicy = ELexUIScreenPageCachePolicy::DestroyOnPop;
		ELexUIScreenPageState State = ELexUIScreenPageState::Inactive;
		bool bHidePrevious = true;
	};

	struct FPageDefinition
	{
		TSoftObjectPtr<ULexUIPrefab> Prefab;
		ELexUIScreenPageCachePolicy CachePolicy = ELexUIScreenPageCachePolicy::KeepAlive;
	};

	struct FPendingPageLoad
	{
		TSharedPtr<FStreamableHandle> Handle;
		TArray<FLexUIScreenPageAsyncCallback> Callbacks;
		bool bHidePrevious = true;
	};

	UPROPERTY(Transient)
	TObjectPtr<ULexWidget> ScreenRoot;

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

	bool IsUsablePage(const ULexWidget* InRoot) const;
	FName FindNameForWidget(const ULexWidget* InRoot) const;
	void ConfigurePage(ULexWidget* InRoot, int32 InSortOrder);
	void RegisterUIInternal(FName InName, ULexWidget* InRoot, int32 InSortOrder,
		ELexUIScreenPageCachePolicy InCachePolicy, TSoftObjectPtr<ULexUIPrefab> InSourcePrefab,
		bool bInitiallyVisible);
	void SetPageActive(FName InName, bool bActive);
	void RefreshStack(FName InPreviousTop);
	void RemoveEntry(FName InName, bool bDestroyPage);
	void CompletePageLoad(FName InName);
	void ExecuteLoadCallbacks(FName InName, FPendingPageLoad& InPendingLoad, ULexWidget* InPage, bool bSuccess);
	void DestroyPage(ULexWidget* InRoot);
	void EnsureInteractionObjects(ULexCanvas* InRootCanvas);
};
