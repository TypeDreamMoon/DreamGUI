// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIWorldService.h"
#include "Engine/EngineBaseTypes.h"
#include "Event/DreamUIInputTypes.h"
#include "DreamUIInputSubsystem.generated.h"

class AActor;
class FDreamUISlateInputSource;
class SWidget;
class UDreamBaseRaycaster;
class UGameViewportClient;
class ULocalPlayer;
class UDreamEventSystem;
class UDreamUIInputSubsystem;
class UDreamUIInputUser;
class UDreamUIManagerWorldSubsystem;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIUserInputDeviceChangedDelegate, int32 /*UserIndex*/, EDreamUIInputDevice);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIInputUserDelegate, UDreamUIInputUser*);

/**
 * The world's input frame: every player's pipeline, once per frame, in player order.
 *
 * TG_PostPhysics, and ticking while the game is paused: after every player controller has run its input in
 * TG_PrePhysics -- so a click queued there is dispatched in the same frame, whatever order the actors happen to
 * tick in -- and before the UI manager lays out and draws, which the engine runs after the tick groups. The event
 * system components used to tick in TG_PrePhysics beside the controllers, with no order declared between them: a
 * click went out this frame or the next depending on which ticked first.
 */
USTRUCT()
struct FDreamUIInputTickFunction : public FTickFunction
{
	GENERATED_BODY()

	TWeakObjectPtr<UDreamUIInputSubsystem> Subsystem;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FDreamUIInputTickFunction> : public TStructOpsTypeTraitsBase2<FDreamUIInputTickFunction>
{
	enum { WithCopy = false };
};

/**
 * The input system's side of UDreamUIInputServices, one per world: what the core asks for, answered from the
 * players' input, the action router and drag and drop.
 *
 * It owns the world's players (UDreamUIInputUser) -- one for each local player, made when the world begins
 * play and whenever a player joins, taken down when one leaves or the world ends -- and runs their frames from
 * its own tick function. It keeps the registry of the event systems placed for them, and the interaction
 * objects it had to create for a player: the transient host actor carrying that player's default raycasters,
 * and the event system spawned from UDreamGUISettings::EventSystemActorClass.
 *
 * Created in the same worlds as UDreamUIManagerWorldSubsystem, because every question the core asks here is
 * about a widget that manager registered.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient)
class DREAMGUIINPUT_API UDreamUIInputSubsystem : public UDreamUIInputServices, public IDreamUIWorldService
{
	GENERATED_BODY()

public:
	/** InWorldContext's world's input subsystem, or null. */
	static UDreamUIInputSubsystem* Get(const UObject* InWorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void PostInitialize() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual int32 GetTeardownPriority() const override { return DreamUI::WorldServiceTeardownPriority::Input; }
	virtual void TeardownForWorld(UWorld& InWorld) override;

	// ---- players

	/** Player InUserIndex, or null when it has none. */
	UDreamUIInputUser* GetUser(int32 InUserIndex) const;
	/**
	 * Player InUserIndex, made when it has none: for the local player at that index when there is one, else a
	 * script user a test or a headless rig drives by hand. Null once the world's input has been torn down.
	 */
	UDreamUIInputUser* GetOrCreateUser(int32 InUserIndex);
	/**
	 * Whether InUserIndex is somebody: a player made already, an event system placed for the index, a local player at
	 * it, or the first player -- the one a world with no local players at all, a test's or a headless rig's, is driven
	 * as. What a request that would make a player on the way asks first (FocusForNavigation, a Blueprint's implicit
	 * event system): one made for any index -- a scope set to player 3 in a one-player game -- was a phantom, counted
	 * among the players and given a focus nobody could see or move.
	 */
	bool HasPlayerAt(int32 InUserIndex) const;
	/** The player of this world Slate user InSlateUserIndex is: the local player whose Slate user it is, every Slate user with one local player; INDEX_NONE for none. */
	int32 FindUserIndexForSlateUser(int32 InSlateUserIndex) const;
	/** Every player, in player order. */
	void GetUsers(TArray<UDreamUIInputUser*>& OutUsers) const;
	/** Take player InUserIndex away: every hover exited, every press let go, then forgotten. */
	void RemoveUser(int32 InUserIndex);
	/**
	 * The player whose screen what InUserIndex shows goes on -- its drag visual, its virtual cursor: its own, or for a
	 * script player, which has no local player and so no screen of its own, the first local player's, the one screen
	 * there is for it to be pointing at.
	 */
	int32 GetScreenIndexForUser(int32 InUserIndex) const;

	/**
	 * A character typed on player InUserIndex's keyboard, as the game viewport receives it: to the field the player is
	 * typing into, and when no field takes it, as a KeyChar to what the player has focused. True when either took it.
	 */
	bool HandleViewportCharacter(int32 InUserIndex, TCHAR InCharacter);

	/**
	 * Whether this world's host delivers real characters: set the first time a field being edited here receives one,
	 * after which the fields' key table stops guessing printable characters. A fact about the world's game viewport,
	 * so the world's own -- one play session typing does not decide it for another.
	 */
	bool DoesHostDeliverCharacters() const { return bHostDeliversCharacters; }
	void NoteHostDeliversCharacters() { bHostDeliversCharacters = true; }
	/** For tests: say which road characters take in this world. */
	void SetHostDeliversCharactersForTesting(bool bInDelivers) { bHostDeliversCharacters = bInDelivers; }

	/** Whether this world's input is heard from Slate (UDreamGUISettings::bUseSlateInputSource); the preset actors stand down while it is. */
	bool IsSlateInputSourceActive() const { return SlateInputSource.IsValid(); }
	/** Hear this world's input from Slate, or stop: what the setting does when play begins, at any time. */
	void SetSlateInputSourceEnabled(bool bInEnabled);
	TSharedPtr<FDreamUISlateInputSource> GetSlateInputSource() const { return SlateInputSource; }

	/**
	 * Whether Slate's own navigation for Slate user InSlateUserIndex is to be swallowed now: that user's keyboard focus is
	 * on this world's bare game viewport, and the DreamGUI player the user is has UI up -- something focused, or a Tab stop
	 * to land on (DreamUIKeyRouting::HasTabStops). Slate turns a Tab nobody handled into a step of its own, and from the
	 * bare viewport that step descends into the viewport's children, the UMG layers: the first focusable UMG widget took
	 * the keyboard focus, and DreamGUI heard no key until the viewport had it back. Asked by the guard this subsystem chains
	 * onto the game viewport client's OnNavigationOverride, and by UDreamGameViewportClient::HandleNavigation.
	 */
	bool ShouldSwallowSlateNavigation(uint32 InSlateUserIndex) const;
	/**
	 * Chain this world's Slate navigation guard onto InClient's OnNavigationOverride, keeping whatever was bound there to
	 * be asked whenever the guard lets a navigation through -- the delegate holds one binding, and a project's own is not
	 * dropped. UnbindSlateNavigationGuard gives InClient its binding back, if the guard is still the one bound there.
	 * Begin play does the first for the world's game viewport client, the teardown the second.
	 */
	void BindSlateNavigationGuard(UGameViewportClient* InClient);
	void UnbindSlateNavigationGuard();

	/**
	 * One frame of every player's input, in player order. What the tick function runs -- and what a rig that pumps
	 * a world's frames itself calls in its place.
	 */
	void ProcessFrame(float InDeltaSeconds);

	/** Every pointer and navigation event of every player, after its handlers ran: what a world service listens to. */
	FDreamUIMulticastDelegateBaseEventData& GetOnInputEvent() { return OnInputEvent; }
	/** A player's device changed. */
	FDreamUIUserInputDeviceChangedDelegate& GetOnInputDeviceChanged() { return OnInputDeviceChanged; }
	FDreamUIInputUserDelegate& GetOnUserAdded() { return OnUserAdded; }
	/** Before the player is taken down: its pointers are still there to read. */
	FDreamUIInputUserDelegate& GetOnUserRemoved() { return OnUserRemoved; }

	// ---- the event-system registry

	/** The event system registered for local player InUserIndex, or null. */
	UDreamEventSystem* GetEventSystemByUserIndex(int32 InUserIndex = 0) const;
	const TMap<int32, TWeakObjectPtr<UDreamEventSystem>>& GetMapUserIndexToEventSystem() const { return MapUserIndexToEventSystem; }
	/**
	 * Register InEventSystem under its UserIndex, and make it its player's: the player takes its settings, and its
	 * Blueprint events relay the player's. A second live event system for an index that already has one is
	 * refused, with an error.
	 */
	void AddEventSystem(UDreamEventSystem* InEventSystem);
	/**
	 * Remove InEventSystem -- by identity, so a late unregister cannot evict the index's new owner. When it was
	 * the registered one, its player lets go of every pointer: the input source is gone, and every Enter still
	 * owes its Exit, every Down its Up.
	 */
	void RemoveEventSystem(UDreamEventSystem* InEventSystem);
	/** Forget InEventSystem's registration without dispatching anything: for an event system being collected. */
	void ForgetEventSystem(UDreamEventSystem* InEventSystem);
	/**
	 * The event system a Blueprint is handed for player InUserIndex when none is placed: an unregistered component
	 * no actor carries, speaking for that player. Null when the player cannot be made, or the index is nobody's (HasPlayerAt).
	 */
	UDreamEventSystem* GetOrCreateImplicitEventSystem(int32 InUserIndex);

	/** The host actor carrying InUserIndex's auto-created raycasters, or null if none was needed. */
	AActor* GetInteractionHost(int32 InUserIndex) const;
	/** The event system actor spawned for InUserIndex from the settings, or null if none was. */
	AActor* GetCreatedEventSystemActor(int32 InUserIndex) const;

	//~ Begin UDreamUIInputServices
	virtual bool SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) override;
	virtual bool HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const override;
	/**
	 * Takes the navigation cursor off InWidget too, when it was there: a widget that lost focus because it was hidden
	 * or disabled is no longer where navigation stands.
	 */
	virtual void ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) override;
	virtual bool HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const override;
	virtual bool IsHovered(const UDreamWidget* InWidget, int32 InUserIndex) const override;
	virtual bool HasMouseCapture(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerIndex) const override;
	/** The Users map's keys, players that have been taken down left out. */
	virtual void GetUserIndices(TArray<int32>& OutUserIndices) const override;
	virtual UDreamWidget* GetFocusedWidget(int32 InUserIndex) const override;
	/**
	 * On the navigation pointer, whose highlight follows the focus every move starts from, and recorded as code's
	 * (EDreamUIFocusCause::Script) unless a navigation step is landing. Makes the player when it has none yet -- a scope
	 * pushed for player 1 before anything else asked about player 1 still puts their focus somewhere -- but only for an
	 * index that is somebody's (HasPlayerAt): one for any index was a phantom player nobody could see or drive.
	 *
	 * "In play" is taken as registered with the world, and "active" and "drawn" are read from the widgets' own switches
	 * up the chain as well as from the hierarchy caches, so a scope focusing from inside the walk that wakes its screen,
	 * or from its screen's begin play, is not refused for caches and lifecycles that are a moment from settling. The
	 * focus-return paths ask for play themselves.
	 */
	virtual bool FocusForNavigation(UDreamWidget* InWidget, int32 InUserIndex) override;
	virtual UDreamWidget* ResolveScopeFocusTarget(int32 InUserIndex) const override;
	/** The player's own record (UDreamUIInputUser::GetFocusCause); None for an index that has no player. */
	virtual EDreamUIFocusCause GetFocusCause(int32 InUserIndex) const override;
	/** The player's own answer (UDreamUIInputUser::IsFocusVisible); for an index that has no player, only while the setting draws every focus. */
	virtual bool IsFocusVisible(int32 InUserIndex) const override;
	virtual UDreamPointerEventData* FindPointer(int32 InUserIndex, int32 InPointerId) const override;
	virtual bool CanListenForActions() const override;
	virtual FDreamUIActionHandle RegisterWidgetAction(UDreamWidget* InOwner, const FDataTableRowHandle& InAction,
		FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex, bool bInDisplayInActionBar) override;
	virtual void UnregisterAction(const FDreamUIActionHandle& InHandle) override;
	virtual bool IsDragDropping() const override;
	virtual UDreamDragDropOperation* GetDragOperationForPointer(int32 InPointerId) const override;
	virtual bool CancelActiveDrag() override;
	/**
	 * Give local player InUserIndex what it takes to point at DreamUI: an event system spawned from
	 * UDreamGUISettings::EventSystemActorClass listening to that player's controller, unless one is placed for it,
	 * and a raycaster of InKind on a transient "DreamInteractionHost_P%d" actor, unless the player already has one of
	 * that kind wherever it was placed -- which is what lets an authored raycaster override the default.
	 */
	virtual void EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind) override;
	/**
	 * EnsureInteractionForPlayer for a screen, and InRootCanvas projected through: by the screen raycaster this
	 * subsystem made for the player, or one of the player's that projects through no overlay root yet. A raycaster
	 * projecting through an overlay root of its own keeps it; when every one of the player's does, the root is given a
	 * raycaster of its own beside them.
	 */
	virtual void PrepareScreenInteraction(UDreamCanvas* InRootCanvas, int32 InUserIndex) override;
	//~ End UDreamUIInputServices

private:
	/** EnsureInteractionForPlayer, with the overlay root a screen raycaster made for it is to project through. */
	void EnsureInteraction(int32 InUserIndex, EDreamInteractionKind InKind, UDreamCanvas* InScreenRootCanvas);
	/**
	 * The raycaster of InKind on InUserIndex's interaction host, the host and the raycaster made when missing -- a screen
	 * one projecting through InScreenRootCanvas from before it is registered, since registering one in a world that has
	 * begun play begins it, and it begins by looking for its root canvas.
	 */
	UDreamBaseRaycaster* FindOrMakeHostRaycaster(int32 InUserIndex, EDreamInteractionKind InKind, UDreamCanvas* InScreenRootCanvas = nullptr);
#if WITH_EDITOR
	/** The selectables' navigation arrows, drawn when the manager draws its editor helpers. */
	void DrawNavigationVisualizers(UDreamUIManagerWorldSubsystem* InManager);
#endif
	void HandleLocalPlayerAdded(ULocalPlayer* InLocalPlayer);
	void HandleLocalPlayerRemoved(ULocalPlayer* InLocalPlayer);
	/** The event system actor and the interaction host made for player InUserIndex, destroyed. */
	void DestroyCreatedInteraction(int32 InUserIndex);
	/** What the guard chained onto OnNavigationOverride answers Slate with: swallowed (ShouldSwallowSlateNavigation), else the binding it replaced. */
	bool HandleSlateNavigation(const uint32 InSlateUserIndex, TSharedPtr<SWidget> InDestination);

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDreamUIInputUser>> Users;

	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
	TMap<int32, TWeakObjectPtr<UDreamEventSystem>> MapUserIndexToEventSystem;

	/** See GetOrCreateImplicitEventSystem. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UDreamEventSystem>> ImplicitEventSystems;

	/**
	 * One transient actor per local player, carrying whichever raycasters were created for them.
	 *
	 * Per PLAYER rather than per kind, so a player pointing at both a screen UI and a world-space panel has one
	 * host with two raycasters on it instead of two actors that mean the same thing.
	 */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AActor>> InteractionHosts;

	/** The event system actors spawned from project settings, one per player that needed one. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AActor>> CreatedEventSystemActors;

	FDreamUIInputTickFunction TickFunction;

	FDreamUIMulticastDelegateBaseEventData OnInputEvent;
	FDreamUIUserInputDeviceChangedDelegate OnInputDeviceChanged;
	FDreamUIInputUserDelegate OnUserAdded;
	FDreamUIInputUserDelegate OnUserRemoved;

	FDelegateHandle LocalPlayerAddedHandle;
	FDelegateHandle LocalPlayerRemovedHandle;
	/** Set by TeardownForWorld, which runs once. */
	bool bTornDownForWorld = false;
	/** Set while ProcessFrame runs, so a handler cannot start another frame from inside one. */
	bool bInFrame = false;
	/** See DoesHostDeliverCharacters. */
	bool bHostDeliversCharacters = false;
	/** Registered with Slate while this world's input is heard from it. */
	TSharedPtr<FDreamUISlateInputSource> SlateInputSource;
	/** The game viewport client the Slate navigation guard is chained onto, and the binding it found there. */
	TWeakObjectPtr<UGameViewportClient> SlateGuardClient;
	TDelegate<bool(const uint32, TSharedPtr<SWidget>)> PreviousNavigationOverride;
};
