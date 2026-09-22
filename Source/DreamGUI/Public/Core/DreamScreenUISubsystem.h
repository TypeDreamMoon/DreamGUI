// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamScreenUISubsystem.generated.h"

class AActor;
class APlayerController;
class UDreamCanvas;
class UDreamUserWidget;
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

/**
 * UMG-style viewport page management, backed by one DreamUI screen-space root canvas PER LOCAL PLAYER.
 *
 * Every verb here takes an optional owning player and answers about that player's screen; passing
 * nothing means the first local player, which is the whole of a single-player game and is why none of
 * the existing call sites had to change. Split screen is then the ordinary case rather than a mode:
 * player 1 and player 2 each get their own root canvas, their own screen-space raycaster carrying
 * their own UserIndex, and their own page stack, so a full-screen page pushed by one never covers the
 * other. UMG draws the same line with AddToViewport (shared) versus AddToPlayerScreen (one player's
 * layer); the difference is that here there is no shared layer to fall back to, so AddToViewport is
 * AddToPlayerScreen for whoever owns the widget.
 */
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

	/** The screen root for InOwningPlayer (the first local player when null), creating it on demand. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen", meta = (AdvancedDisplay = "InOwningPlayer"))
	UDreamWidget* GetOrCreateScreenRoot(APlayerController* InOwningPlayer = nullptr);

	/**
	 * The screen root of whichever player InContextWidget belongs to, creating it on demand.
	 *
	 * What an overlay service -- a tooltip, a drag visual, a modal dim, a virtual cursor -- wants:
	 * the overlay belongs on the same screen as the thing it is about, and the thing it is about
	 * knows its own player. Falls back to the first local player when the context is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	UDreamWidget* GetOrCreateScreenRootForWidget(UDreamWidget* InContextWidget);

	/**
	 * The screen root for a local player index -- the number an event, a raycaster and an event system
	 * all carry.
	 *
	 * This is the entry point for anything driven by a POINTER: UDreamPointerEventData::UserIndex says
	 * which player that pointer belongs to, so a drag visual, a tooltip or a virtual cursor lands on
	 * the screen of the player who is pointing rather than always on the first player's.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	UDreamWidget* GetOrCreateScreenRootForUserIndex(int32 InUserIndex);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen", meta = (AdvancedDisplay = "InOwningPlayer"))
	UDreamWidget* GetScreenRoot(APlayerController* InOwningPlayer = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen", meta = (AdvancedDisplay = "InOwningPlayer"))
	UDreamCanvas* GetScreenCanvas(APlayerController* InOwningPlayer = nullptr) const;

	/** Every local player index that currently has a screen. Useful for iterating split-screen state. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	TArray<int32> GetScreenPlayerIndices() const;

	/**
	 * Creates a widget of InWidgetClass and puts it on InOwningPlayer's screen.
	 *
	 * The return pin takes the shape of InWidgetClass, so a graph reaches the class's own variables
	 * and functions without a Cast, the way UMG's Create Widget node does.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen", meta = (DeterminesOutputType = "InWidgetClass", AdvancedDisplay = "InOwningPlayer"))
	UDreamWidget* CreateWidgetOnScreen(TSubclassOf<UDreamUserWidget> InWidgetClass, int32 InSortOrder = 0,
		APlayerController* InOwningPlayer = nullptr);

	/**
	 * UMG's AddToPlayerScreen: put InRoot on ONE local player's screen rather than on whichever one it
	 * already belonged to.
	 *
	 * This is AddToViewport plus an owner: it points the widget at InOwningPlayer first, so everything
	 * downstream -- which root it is parented to, which event system gives it focus, whose input it
	 * hears -- follows from the widget rather than from a parameter nobody remembers to pass.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void AddToPlayerScreen(UDreamWidget* InRoot, APlayerController* InOwningPlayer, int32 InSortOrder = 0);

	/**
	 * Puts an existing widget on the screen at InSortOrder, and switches it on.
	 *
	 * Sort orders of StackBaseSortOrder (1000) and up are reserved for the page stack, which
	 * rewrites its pages' sort orders on every refresh; a free-standing page placed in that band
	 * draws in an undefined order against the stack and loses the argument on the next refresh.
	 * Asking for one logs a warning.
	 *
	 * The screen it lands on is the widget's OWNING PLAYER's -- see AddToPlayerScreen, which is this
	 * with the owner named.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void AddToViewport(UDreamWidget* InRoot, int32 InSortOrder = 0);

	/** Takes a page off the screen and DESTROYS it. To keep the widget alive, use ForgetPage. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveFromViewport(UDreamWidget* InRoot);

	/**
	 * Stop tracking a page without destroying it: the name is freed, the stack drops it, the widget
	 * lives on wherever its caller puts it next.
	 *
	 * This is the half UMG's RemoveFromParent has and RemoveFromViewport does not, and without it a
	 * widget detached from the screen root by any other route left its name and its entry behind,
	 * pointing at a page that is registered, alive and nowhere.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	bool ForgetPage(UDreamWidget* InRoot);

	/**
	 * Stop (or resume) forcing full-bleed geometry on one page.
	 *
	 * Pages are laid out edge to edge and the stack re-applies that on every refresh, which is right
	 * for a screen and wrong for a floating window. UDreamUserWidget::SetPositionInViewport and its
	 * three companions call this for you; it is public so a plain widget placed by hand can say the
	 * same thing. True when InRoot is a tracked page and the flag was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	bool SetPageHasCustomPlacement(UDreamWidget* InRoot, bool bInCustomPlacement);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	bool GetPageHasCustomPlacement(UDreamWidget* InRoot) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	bool IsInViewport(UDreamWidget* InRoot) const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RegisterUI(FName InName, UDreamWidget* InRoot, int32 InSortOrder = 0);

	/** Creates a widget of InWidgetClass, registers it under InName and shows it. Return pin follows InWidgetClass. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen", meta = (DeterminesOutputType = "InWidgetClass", AdvancedDisplay = "InOwningPlayer"))
	UDreamWidget* ShowWidgetOfClass(FName InName, TSubclassOf<UDreamUserWidget> InWidgetClass, int32 InSortOrder = 0,
		APlayerController* InOwningPlayer = nullptr);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	UDreamWidget* GetUI(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	bool IsUIShowing(FName InName) const;

	/**
	 * Shows or hides a page. Hiding switches it OFF as well as collapsing it: its behaviours run
	 * OnDisable, it stops ticking and its polled property bindings stop being evaluated, exactly as
	 * a page the stack has covered does.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void SetUIVisible(FName InName, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen")
	void RemoveAllUI();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen")
	TArray<FName> GetAllUINames() const;

	/**
	 * Names the subsystem is holding, INCLUDING any whose page has died behind its back.
	 *
	 * GetAllUINames filters those out, which is right for callers and useless for asking whether the
	 * entry itself was ever reclaimed -- the question PruneDeadEntries exists to answer.
	 */
	int32 GetPageEntryCount() const { return Entries.Num(); }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	bool RegisterPageClass(FName InName, TSoftClassPtr<UDreamUserWidget> InWidgetClass,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::KeepAlive);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	void UnregisterPageClass(FName InName, bool bRemoveLoadedPage = true);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	TSoftClassPtr<UDreamUserWidget> GetPageClass(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	TArray<FName> GetRegisteredPageNames() const;

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "OnComplete"), Category = "DreamGUI|Screen|Pages")
	void PushPageAsync(FName InName, const FDreamUIScreenPageAsyncCallback& OnComplete, bool bHidePrevious = true);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Pages")
	bool CancelPageLoad(FName InName);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Pages")
	EDreamUIScreenPageState GetPageState(FName InName) const;

	/**
	 * Pushes a new page of InWidgetClass onto the stack. Return pin follows InWidgetClass.
	 *
	 * With bHidePrevious, the pages below are switched off, not merely collapsed -- see SetUIVisible.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack", meta = (DeterminesOutputType = "InWidgetClass", AdvancedDisplay = "InOwningPlayer"))
	UDreamWidget* PushWidgetOfClass(FName InName, TSubclassOf<UDreamUserWidget> InWidgetClass,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true, APlayerController* InOwningPlayer = nullptr);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	void PushUI(FName InName, UDreamWidget* InRoot,
		EDreamUIScreenPageCachePolicy InCachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop,
		bool bHidePrevious = true);

	/** Pops the top page of InOwningPlayer's stack. One player popping never touches another's. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack", meta = (AdvancedDisplay = "InOwningPlayer"))
	void PopUI(APlayerController* InOwningPlayer = nullptr);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack")
	bool PopToUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen|Stack", meta = (AdvancedDisplay = "InOwningPlayer"))
	void ClearStack(bool bRemovePages = false, APlayerController* InOwningPlayer = nullptr);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack", meta = (AdvancedDisplay = "InOwningPlayer"))
	FName GetTopUI(APlayerController* InOwningPlayer = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack", meta = (AdvancedDisplay = "InOwningPlayer"))
	int32 GetStackDepth(APlayerController* InOwningPlayer = nullptr) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack")
	bool IsUIInStack(FName InName) const { return Stack.Contains(InName); }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen|Stack", meta = (AdvancedDisplay = "InOwningPlayer"))
	TArray<FName> GetUIStack(APlayerController* InOwningPlayer = nullptr) const;

private:
	struct FEntry
	{
		TWeakObjectPtr<UDreamWidget> Root;
		TSoftClassPtr<UDreamUserWidget> SourceClass;
		int32 SortOrder = 0;
		EDreamUIScreenPageCachePolicy CachePolicy = EDreamUIScreenPageCachePolicy::DestroyOnPop;
		EDreamUIScreenPageState State = EDreamUIScreenPageState::Inactive;
		bool bHidePrevious = true;
		/** Which local player's screen this page lives on. Everything per-player keys off it. */
		int32 PlayerIndex = 0;
		/**
		 * The caller placed this page by hand (SetPositionInViewport and friends), so ConfigurePage
		 * leaves its anchors, position and size alone. Sort order and shown/hidden are still the
		 * stack's -- a floating window is still a page.
		 */
		bool bCustomPlacement = false;
	};

	struct FPageDefinition
	{
		TSoftClassPtr<UDreamUserWidget> PageClass;
		EDreamUIScreenPageCachePolicy CachePolicy = EDreamUIScreenPageCachePolicy::KeepAlive;
	};

	struct FPendingPageLoad
	{
		TSharedPtr<FStreamableHandle> Handle;
		TArray<FDreamUIScreenPageAsyncCallback> Callbacks;
		bool bHidePrevious = true;
	};

	/** One screen-space root canvas per local player. Keyed by local player index. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDreamWidget>> ScreenRoots;

	/** Indices whose root this subsystem made, and therefore has to destroy. */
	TSet<int32> OwnedScreenRoots;

	TMap<FName, FEntry> Entries;
	TMap<FName, FPageDefinition> PageDefinitions;
	TMap<FName, FPendingPageLoad> PendingPageLoads;
	/**
	 * Every page ever pushed, in push order, across all players.
	 *
	 * One array rather than one per player, because a page name is global here and always has been --
	 * the per-player view is a filter on this by the entry's PlayerIndex, which is what StackFor()
	 * hands out. That keeps "which page is on top" a single ordering question and makes it impossible
	 * for a name to be on two stacks at once.
	 */
	TArray<FName> Stack;
	int32 AutoNameCounter = 0;
	bool bRefreshingStack = false;
	/** Player indices whose stack asked to be refreshed while a refresh was already running. */
	TSet<int32> StackRefreshRequests;

	static constexpr int32 StackBaseSortOrder = 1000;
	static constexpr int32 StackSortOrderStep = 10;

	bool IsUsablePage(const UDreamWidget* InRoot) const;
	FName FindNameForWidget(const UDreamWidget* InRoot) const;
	void ConfigurePage(UDreamWidget* InRoot, int32 InSortOrder, int32 InPlayerIndex, bool bInCustomPlacement);
	void RegisterUIInternal(FName InName, UDreamWidget* InRoot, int32 InSortOrder,
		EDreamUIScreenPageCachePolicy InCachePolicy, TSoftClassPtr<UDreamUserWidget> InSourceClass,
		bool bInitiallyVisible, int32 InPlayerIndex);
	void SetPageActive(FName InName, bool bActive);
	void RefreshStack(int32 InPlayerIndex, FName InPreviousTop);
	void RemoveEntry(FName InName);
	/** Drops the names of pages that were destroyed behind the subsystem's back. Returns how many. */
	int32 PruneDeadEntries();
	void WarnIfSortOrderReserved(FName InName, int32 InSortOrder) const;
	void CompletePageLoad(FName InName);
	void ExecuteLoadCallbacks(FName InName, FPendingPageLoad& InPendingLoad, UDreamWidget* InPage, bool bSuccess);
	void DestroyPage(UDreamWidget* InRoot);
	/**
	 * Make sure this player can point at their screen: the manager supplies the event system and the
	 * screen raycaster, and this binds that raycaster to InRootCanvas.
	 */
	void EnsureInteractionObjects(UDreamCanvas* InRootCanvas, int32 InPlayerIndex);

	/** The local player index of InOwningPlayer, or of the first local player when it is null. */
	int32 ResolvePlayerIndex(const APlayerController* InOwningPlayer) const;
	/** The player a widget already belongs to: its own owning player, or the first local player. */
	int32 PlayerIndexForWidget(const UDreamWidget* InRoot) const;
	/** The page names on InPlayerIndex's stack, bottom first. */
	TArray<FName> StackFor(int32 InPlayerIndex) const;
	/** Topmost live page name on InPlayerIndex's stack, or None. */
	FName GetTopUIForIndex(int32 InPlayerIndex) const;
	/** The player index recorded for a page name, or the first local player when it is not a page. */
	int32 PlayerIndexForPage(FName InName) const;
	UDreamWidget* GetOrCreateScreenRootForIndex(int32 InPlayerIndex);
};
