// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Templates/UniquePtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

class AActor;
class APlayerController;
class FAutomationTestBase;
class UDreamCanvas;
class UDreamEventSystem;
class UDreamDriverInputModule;
class UDreamScreenSpaceRaycaster;
class UDreamWidget;
class UGameInstance;
class ULocalPlayer;
class UUITextInput;
class UWorld;

namespace DreamTests
{
	struct FScopedGameWorld;
	struct FScopedGameInstanceWorld;
}

/**
 * Everything a pointer needs in order to exist, built and torn down together.
 *
 * A world, an actor to hang the input pieces on, an event system, a driver input module bound to it,
 * a screen-space raycaster, and a root widget carrying a ScreenSpaceOverlay canvas with a substituted
 * viewport size. Every one of those is built the way the runtime builds it -- the raycaster enrols
 * itself with the UI manager's raycaster list, the canvas enrols itself with its canvas list -- so a
 * trace here walks the same list a trace in a game would. Nothing is handed a result.
 *
 * THE WORLD belongs to a UGameInstance by default (FDreamRigOptions::bWithGameInstance), built the way
 * a standalone game builds one, because GameInstance subsystems -- the tween manager above all -- only
 * exist in such a world: in a bare UWorld::CreateWorld world every tween is refused and every animated
 * control either snaps or never moves. The bare world is still one option away, and is what the
 * designer previews in. Tearing the rig down shuts the game instance down and takes its world context
 * off the engine's list, so the next test does not find it.
 *
 * WHERE INPUT ENTERS is an option too (FDreamRigOptions::InputHost): straight into the driver's
 * module, as it always did, through a real input actor behind a real player controller, built by
 * DreamDriverGameHost, or as Slate's own events to the world's Slate input source, set up by
 * DreamDriverSlateHost. Whichever it is, the steps are the same; see FDreamDriverSequence.
 *
 * PLAYERS (FDreamRigOptions::PlayerCount, PlayerScreens). Player 0 is the rig's own and everything
 * that takes no player index answers for it, so a rig of one is exactly the rig there always was.
 * Every further player is built the way the same host builds player 0, with its own context: an
 * actor of its own carrying its screen raycaster (UserIndex N); under ModuleOnly an event system
 * for UserIndex N and a driver module feeding it -- a script player, as UDreamUIInputSubsystem calls
 * a player with no local player behind it; under an actor host a real ULocalPlayer on the game
 * instance, its APlayerController on the world's list, and an input actor listening as PlayerN, so
 * that player's buttons, keys, touches and pad go through its own controller's input stack. Driver(N)
 * and FDreamDriverSequence::AsPlayer send steps as player N; the pump ticks every player's controller.
 *  - Shared: every player's raycaster projects through the rig's root canvas -- one screen, several
 *    pointers.
 *  - Split (actor hosts only): each local player is given the part of the viewport the engine's
 *    split-screen layout gives it (UGameViewportClient::LayoutPlayers over the viewport client's
 *    SplitscreenInfo, for UGameMapsSettings' default layouts: two players top and bottom, three
 *    favouring the top, four in a grid), and every player has a screen-space root canvas of its own
 *    with its raycaster bound to it, given its player (UDreamCanvas::SetViewportPlayerIndex) as
 *    UDreamScreenUISubsystem gives every local player's screen -- so each screen is laid out over its
 *    player's part of the viewport, a pointer outside the part reaches nothing on it, and the canvas
 *    measures a pointer from the part's corner, as UMG lays a player's layer out
 *    (SGameLayerManager::AddOrUpdatePlayerLayers). A world pointer attached for a player
 *    (DreamDriverWorld::AttachWorldPointer with a player index) looks through that player's part, as
 *    UDreamWorldSpaceRaycaster::GenerateRay deprojects through the pointer's local player. Pixels are
 *    always the one viewport's: a widget on player 1's screen projects to a pixel in player 1's part
 *    (FDreamDriverProjection::WorldPointToPixel adds the part's corner). The screens let go of their
 *    players before the local players are taken off at tear-down. With bScreensFromScreenUI the
 *    screens are the screen UI's own for each player -- player 0's the rig's root, adopted -- so what
 *    the screen UI puts on a player's screen (AddToPlayerScreen, a tooltip, a popup) is on the one that
 *    player's raycaster projects through.
 * Refused, with the reason, rather than quietly built as player 0: more than four players or fewer
 * than one, several players under SlateSource (the source's test mappers make Slate user 0 the only
 * player, and a mapping of the rig's own for more would be testing the rig), and a split screen with
 * no local players to lay out (ModuleOnly, and every host without a game instance). Asking for a
 * player the rig has not got -- Context(5), Driver(5) -- is an error on the bound test.
 *
 * PROCESS-WIDE STATE the rig disturbs is put back when it goes -- UUITextInput's "a host delivers
 * characters" switch -- and the counters are checked settled: the rig world's layout pass and
 * desired-size memo depths before the world goes, the editor's compiling flag after; see
 * DescribeUnsettledProcessState.
 *
 * THE VIEWPORT. A game world with no player controller answers GetViewportSize with a 2x2 fallback,
 * which makes the canvas 2 units across and the projection matrix describe a 2x2 screen, and a ray
 * cast through that lands nowhere. UDreamCanvas::SetViewportSizeOverride is what this rig was the
 * reason for; without it none of the coordinate arithmetic means anything.
 *
 * Not copyable and not movable: the driver and every element it hands out point at the context that
 * lives inside this object, so a rig that could be moved would leave them pointing at the old one.
 * Constructing one from Headless() is still fine -- C++17 builds it straight into the caller's
 * variable rather than moving it there.
 */
class FDreamDriverRig
{
public:
	/**
	 * A complete headless rig with a substituted viewport of InViewportSize.
	 *
	 * Build it into a local and keep it for the whole test:
	 *     FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	 */
	static FDreamDriverRig Headless(FIntPoint InViewportSize);

	/**
	 * A headless rig built to InOptions. Headless(FIntPoint) is this with only the viewport size set,
	 * which means it, too, builds a world that belongs to a GameInstance -- see FDreamRigOptions.
	 *
	 *     FDreamRigOptions Options;
	 *     Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
	 *     Options.ReferenceResolution = FVector2D(1920.0, 1080.0);
	 *     FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	 */
	static FDreamDriverRig Headless(const FDreamRigOptions& InOptions);

	~FDreamDriverRig();

	FDreamDriverRig(const FDreamDriverRig&) = delete;
	FDreamDriverRig& operator=(const FDreamDriverRig&) = delete;
	FDreamDriverRig(FDreamDriverRig&&) = delete;
	FDreamDriverRig& operator=(FDreamDriverRig&&) = delete;

	/** Whether every piece came up. A test should say so and stop rather than act on half a rig. */
	bool IsUsable() const;

	UWorld* GetWorld() const;
	UDreamWidget* Root() const;
	UDreamCanvas* RootCanvas() const;
	UDreamEventSystem* EventSystem() const;
	UDreamDriverInputModule* InputModule() const;
	UDreamScreenSpaceRaycaster* Raycaster() const;

	FDreamDriverContext& Context() const;
	FDreamDriverRef Driver() const;

	/** What this rig was built from. */
	const FDreamRigOptions& GetOptions() const;
	/** The GameInstance the world belongs to; null when bWithGameInstance was false. */
	UGameInstance* GetGameInstance() const;
	/** Player 0's controller; null until EnsureGameInputHost or an actor input host has made one. */
	APlayerController* GetPlayerController() const;
	/** The actor the rig hangs its raycaster on -- and, under ModuleOnly and SlateSource, its event system and input module. */
	AActor* GetHostActor() const;
	/** Why IsUsable() is false, in words; empty while it is true. */
	const FString& GetBuildFailure() const;

	/*
	 * Each player's pieces, by player index; index 0 answers what the accessors without one answer. A player the rig has
	 * not got answers null from the pointer accessors, and is an error on the bound test from the two that hand back a
	 * reference (Context, Driver), which then answer player 0's so the test fails where it stands rather than crashing.
	 */
	/** How many players the rig was built with. */
	int32 GetPlayerCount() const;
	/** Player InPlayerIndex's context, or null when the rig has no such player. */
	FDreamDriverContext* FindPlayerContext(int32 InPlayerIndex) const;
	FDreamDriverContext& Context(int32 InPlayerIndex) const;
	/** A driver whose elements and sequences act as player InPlayerIndex: its pointer, its keys, its screen to find things on. */
	FDreamDriverRef Driver(int32 InPlayerIndex) const;
	/** The root of the screen player InPlayerIndex points at: the rig's root, or under a split screen that player's own. */
	UDreamWidget* Root(int32 InPlayerIndex) const;
	UDreamCanvas* RootCanvas(int32 InPlayerIndex) const;
	UDreamEventSystem* EventSystem(int32 InPlayerIndex) const;
	UDreamDriverInputModule* InputModule(int32 InPlayerIndex) const;
	UDreamScreenSpaceRaycaster* Raycaster(int32 InPlayerIndex) const;
	/** Null for a script player (ModuleOnly), which has no controller; under ModuleOnly player 0's once EnsureGameInputHost made it. */
	APlayerController* GetPlayerController(int32 InPlayerIndex) const;
	/** Null for a script player. */
	ULocalPlayer* GetLocalPlayer(int32 InPlayerIndex) const;
	/** The actor that player's raycaster rides on -- under ModuleOnly its event system and module too. */
	AActor* GetHostActor(int32 InPlayerIndex) const;
	/**
	 * The part of the viewport player InPlayerIndex sees, in viewport pixels, truncated as ULocalPlayer::GetProjectionData
	 * truncates it: the whole viewport except on a split screen. Empty for a player the rig has not got.
	 */
	FBox2D GetPlayerViewRect(int32 InPlayerIndex) const;

	/** Tell the rig which test is running, so a failing step reports against it -- every player's step. */
	void BindTest(FAutomationTestBase* InTest);

	/**
	 * A hit-testable child widget under InParent (the root when null), sized and placed.
	 *
	 * The order is not arbitrary: register, then parent, then give it its visual. A visual is only
	 * enrolled with its widget's render canvas if there is one by the time it is created, and a visual
	 * that was never enrolled is one the raycaster never walks over -- the widget would be there,
	 * laid out, drawn in no draw call and unclickable.
	 *
	 * InAnchoredPosition is in canvas units with Y UPWARD, because that is what the anchor system
	 * speaks; it is the projection, not the authoring, that flips Y.
	 */
	UDreamWidget* MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/**
	 * A control -- a user widget such as UDreamButton or UDreamSlider -- built the way the runtime
	 * builds one, under InParent (the root when null), sized and placed like MakeWidget.
	 *
	 * It goes through CreateDreamWidget, the class model's own factory: instance the class, Initialize
	 * (which realizes the control's parts and wires its behaviours), parent it without firing attach
	 * events, then RegisterDreamWidgetHierarchy, which registers the WHOLE subtree -- the control and
	 * every part inside it -- and hands the subtree the parent's render canvas, visibility and
	 * interactability. That last step is what a hand-rolled NewObject + OnRegister would miss: a
	 * control's parts are its children, and OnRegister registers only the widget it is called on.
	 *
	 * The name and size are written before the hierarchy registers, so the first layout pass that
	 * sees the control already sees it at its size; the anchored position is written after, the same
	 * as MakeWidget, because anchoring is resolved against a parent the widget is registered under.
	 *
	 * Nothing is pumped here. A test calls PumpFrames when it wants the control laid out.
	 *
	 * The control's parts -- a button's face, a slider's handle, a list's rows -- are reached through
	 * the control's own API and aimed at with FDreamBy::Widget(part).
	 */
	template<class T>
	T* MakeControl(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUserWidget>::Value,
			"MakeControl builds user widgets; a plain UDreamWidget is MakeWidget's job");
		return Cast<T>(MakeControl(T::StaticClass(), InDisplayName, InParent, InSize, InAnchoredPosition));
	}

	/** MakeControl for a class only known at run time -- a Blueprint subclass, a class picked by a parameter. */
	UDreamWidget* MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/**
	 * Give the rig the two things a game world has, a bare fixture lacks, and a CONTROL's input path
	 * assumes. MakeControl calls it; a test building a control by hand calls it before the first click.
	 *
	 * 1. Every player's event system registered with the UI manager, which is what UDreamEventSystem::BeginPlay
	 *    does and what a world that never began play never gets. Everything that asks "which event
	 *    system is this player's" goes through that registration -- UUISelectable taking the selection
	 *    on a press, UUITextInput selecting itself when an edit begins, UDreamUINavigationStack::HandleBack
	 *    finding the field Escape should cancel -- so without it a click focuses nothing.
	 * 2. Player 0's controller, on the world's controller list and with its input system up.
	 *    UUITextInput::ActivateInput binds the field's keys on an actor whose InputComponent only
	 *    exists once the world's player 0 has enabled its input, and binds them through that
	 *    component without asking whether it exists. With no findable player controller, clicking a
	 *    text field dereferences null -- in every control that carries one, a spin box included.
	 *    Spawning is not enough in a world nobody initialized for play; see the definition. A script
	 *    player has no controller and needs none: its keyboard is bound on nothing (UDreamUIInputUser::RefreshTextKeys).
	 *
	 * Both are idempotent. Neither is done by the rig's constructor, so a test that never asks keeps
	 * exactly the rig it had before this existed. A controller the context already has -- an input
	 * host's, a PIE player's -- is the one used; one spawned here is written back to the context, so
	 * GetPlayerController answers from then on.
	 */
	void EnsureGameInputHost();

	/** Let frames pass with no input, for a test that wants the tree to settle. */
	void PumpFrames(int32 InFrameCount);

	/**
	 * What the rig's tear-down says about the counters it checks, as sentences -- empty when all of
	 * them are settled. The destructor calls it with the live values (the rig world's layout pass and
	 * desired-size memo depths, read from its layout context once the tree is gone and before the
	 * world is; UDreamGUIEditorSubsystem's recompiling flag) and reports every sentence against the bound
	 * test.
	 *
	 * Taken apart and public because the state it exists to catch -- a layout pass entered and never
	 * left -- is not one a test can safely produce for real: the only way to be inside a layout pass
	 * at tear-down is to tear down from inside one. A test hands it the values instead.
	 */
	static TArray<FString> DescribeUnsettledProcessState(int32 InLayoutPassDepth, int32 InDesiredSizeMemoDepth, bool bInBlueprintCompiling);

private:
	explicit FDreamDriverRig(const FDreamRigOptions& InOptions);

	/**
	 * The world beginning play, as far as DreamUI can tell -- once, after the world, the event system
	 * and the root canvas exist and before any control is made, which is the ordinary order in a game:
	 * the level starts, then screens are built at runtime.
	 *
	 * Without it nothing in the rig ever begins play: the UI manager reports HasBegunPlay false, so
	 * RegisterDreamWidgetHierarchy registers controls without beginning them and no behaviour ever
	 * reaches Awake, OnEnable, Start or Tick. Pointer events still arrive, which is what made the
	 * difference silent -- a scroll bar that places its handle in Start and a scroll view whose
	 * inertia runs in Tick were being tested in a state no game is ever in.
	 */
	void OpenBeginPlayGate();

	/**
	 * Process-wide state that outlives worlds, and so outlives rigs: remembered when the rig is
	 * built, put back or checked when it goes. See the definitions for which is which and why.
	 */
	void CaptureProcessState();
	UUITextInput* FindEditInRigTree() const;
	void EndLeakedTextEdit(UUITextInput* InEditInRigTree, FAutomationTestBase* InTest);
	/** Whether InWidget is under any of the rig's screens: player 0's root, or a split screen's player root. */
	bool IsUnderRigScreens(const UDreamWidget* InWidget) const;
	/** For a rig of several players: each player's field being edited, by player index, if it is under the rig's screens. */
	TMap<int32, TWeakObjectPtr<UUITextInput>> FindPlayerEditsInRigTree() const;
	void EndLeakedPlayerTextEdits(const TMap<int32, TWeakObjectPtr<UUITextInput>>& InEditsInRigTree, FAutomationTestBase* InTest);

	/**
	 * Why these options cannot be built, or empty when they can: the player count, and the combinations of players,
	 * screens and hosts the rig refuses (see the class comment).
	 */
	static FString DescribeRefusedPlayerOptions(const FDreamRigOptions& InOptions);
	/** Step 3 for the players after player 0, under the rig's host. False having set BuildFailure. */
	bool BuildOtherPlayersInput();
	/** The engine's split-screen layout onto the local players and the contexts. False having set BuildFailure. */
	bool LayOutSplitScreen();
	/** Whether the options ask for a split screen the rig builds: several players, each with a screen of its own. */
	bool IsSplitScreen() const;
	/** Whether that split screen's screens are the screen UI's (FDreamRigOptions::bScreensFromScreenUI). */
	bool UsesScreenUIScreens() const;
	/** The rig's substituted viewport and the scaler the options ask for, onto a screen-space root canvas whose render mode is set. */
	void GiveScreenTheRigsViewport(UDreamCanvas* InCanvas) const;
	/**
	 * The first step of tearing down: every screen the rig made given no player again, before the input hosts take the
	 * local players its index names off the game instance.
	 */
	void ReleaseScreensFromPlayers();
	/**
	 * A ScreenSpaceOverlay root canvas the rig's way -- the render mode, then the substituted viewport, then the scaler
	 * the options ask for -- on a new registered root named InDisplayName. Null when the root would not take a canvas.
	 */
	UDreamCanvas* MakeScreenRoot(const FString& InDisplayName, UDreamWidget*& OutRoot);
	/** A screen raycaster for InUserIndex on InHost, projecting through InCanvas, enrolled. */
	UDreamScreenSpaceRaycaster* MakeScreenRaycaster(AActor* InHost, UDreamCanvas* InCanvas, int32 InUserIndex);
	/** Step 4 for the players after player 0: their screens and raycasters, and every context told of every player. False having set BuildFailure. */
	bool BuildOtherPlayersScreens();
	/**
	 * The first step of tearing down the players after player 0: their input hosts, last player first -- the local
	 * players come off the game instance from the end, so no player's index moves under it -- and, on a rig of several,
	 * a complaint for each piece of a player's host left behind.
	 */
	void TearDownOtherPlayersInput(FAutomationTestBase* InTest);
	/** With player 0's tree: their own screens destroyed (a split screen's), their raycasters off, their modules unregistered. */
	void TearDownOtherPlayersScreens();
	static void ReportTextEditOutlivingWorld(const UWorld* InRigWorld, FAutomationTestBase* InTest);
	void RestoreAndVerifyProcessState(FAutomationTestBase* InTest, int32 InLayoutPassDepth, int32 InDesiredSizeMemoDepth);
	static void ReportRigProblem(FAutomationTestBase* InTest, const FString& InMessage);

	/**
	 * The editor's Blueprint compile announcements, heard for the session from the first rig on (the
	 * listener lives in the .cpp), and the compiling flag as this rig found it.
	 *
	 * UDreamGUIEditorSubsystem's recompiling flag, checked at tear-down, is set on a widget class's pre-compile and
	 * cleared on the announcement that the compile is over, which the editor makes for every compile,
	 * failed ones included -- so a flag still set at tear-down is always a compile the editor never
	 * announced as finished. It is process state, and what left it set has as often as not happened
	 * before the rig was built, in whatever test ran first; what was heard, and when, is how the rig
	 * says which Blueprint it was.
	 */
	void WatchBlueprintCompiles();
	/** Whether UDreamGUIEditorSubsystem believed a widget class was recompiling when the rig was built. */
	bool bBlueprintCompilingWhenBuilt = false;
	/** GFrameCounter when the rig was built, for the report. */
	uint64 BuiltAtFrame = 0;

	/** Torn down last, because everything below lives inside it. Exactly one of the two exists, chosen by bWithGameInstance. */
	TUniquePtr<DreamTests::FScopedGameWorld> ScopedWorld;
	TUniquePtr<DreamTests::FScopedGameInstanceWorld> ScopedGameInstanceWorld;
	/** By pointer so its address survives anything that happens to this object. */
	TUniquePtr<FDreamDriverContext> DriverContext;
	TSharedPtr<FDreamDriver> DriverInstance;
	AActor* Host = nullptr;

	/** A player after player 0: its context and the driver over it, by pointer for the same reason as player 0's. */
	struct FOtherPlayer
	{
		TUniquePtr<FDreamDriverContext> Context;
		TSharedPtr<FDreamDriver> Driver;
		AActor* Host = nullptr;
		/** Whether the root in its context is its own (a split screen's), which the rig then destroys. */
		bool bOwnsScreen = false;
	};
	/** Players 1, 2, 3, in order. Empty on a rig of one. */
	TArray<FOtherPlayer> OtherPlayers;

	FDreamRigOptions Options;
	FString BuildFailure;
	/** UUITextInput's "a host delivers characters" switch as the rig found it. */
};
