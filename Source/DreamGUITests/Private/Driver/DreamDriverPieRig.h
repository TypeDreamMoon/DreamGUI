// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

class AActor;
class APlayerController;
class FAutomationTestBase;
class FSceneViewport;
class SWidget;
class UDreamCanvas;
class UDreamScreenSpaceRaycaster;
class UDreamVisual;
class UDreamWidget;
class UDreamWorldSpaceRaycaster;
class UGameViewportClient;
class ULocalPlayer;
class UWorld;

/** Where the play session's picture goes. */
enum class EDreamPieViewportDestination : uint8
{
	/**
	 * Into the level editor's first active viewport, the way the engine's own FStartPIECommand plays:
	 * no window is opened, so nothing pops up in front of whoever is at the desk. Falls back to
	 * NewWindow when the level editor has no viewport in a window.
	 */
	LevelEditorViewport,
	/** A window of its own at FDreamPieRigOptions::ViewportSize, the way the engine's map-based automation plays. */
	NewWindow,
};

/** How a play-in-editor rig is set up. The defaults are what every smoke test wants. */
struct FDreamPieRigOptions
{
	/**
	 * The viewport size a play session is given when it comes up WITHOUT one -- which is what a
	 * headless editor does, because nothing lays its viewport out. A viewport that has a real size
	 * from a real window keeps it: the point of this layer is to meet the engine's own viewport, not
	 * to overrule it.
	 */
	FIntPoint ViewportSize = FIntPoint(1280, 720);
	/** Which input actor carries the player's input. ModuleOnly is the headless rig's and is refused here. */
	EDreamRigInputHost InputHost = EDreamRigInputHost::StandaloneActor;
	EDreamPieViewportDestination Destination = EDreamPieViewportDestination::LevelEditorViewport;
	/** Where the player starts and which way they look. The map is blank, so this is the only thing in view that anybody put there. */
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation = FRotator::ZeroRotator;
	/** How long the session may take to come up, and to go away again, before the rig gives up on it. */
	float StartTimeoutSeconds = 30.0f;
	float StopTimeoutSeconds = 15.0f;
	/** Engine frames let pass after the rig comes up and after every WhenReady, so what was built is laid out before anything aims at it. */
	int32 SettleFrames = 2;
};

/** What a play session turned out to be. Filled while the rig comes up; the probes print it. */
struct FDreamPieRigReport
{
	/** Wall-clock seconds from asking for the session to the player being ready, and from asking it to end to it being gone. */
	double SecondsToStart = 0.0;
	double SecondsToStop = 0.0;
	/** Engine frames from asking for the session to the player being ready. */
	int32 FramesToStart = 0;
	/** The viewport as the engine made it, before this rig did anything to it. */
	FIntPoint ViewportSizeAsStarted = FIntPoint::ZeroValue;
	/** The viewport as the rig left it: the same, unless it came up with no size. */
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	bool bViewportSizeWasGiven = false;
	/** What the rig's screen-space root canvas answers to GetViewportSize in the play world. */
	FIntPoint CanvasViewportSize = FIntPoint::ZeroValue;
	EDreamPieViewportDestination DestinationUsed = EDreamPieViewportDestination::LevelEditorViewport;
	FString ViewportClientClass;
	FString GameModeClass;
	FString PlayerControllerClass;
	FString PlayWorldName;
	/**
	 * Whether Slate's keyboard focus sat on the play session's viewport widget (or on a widget inside it)
	 * when the session came up, before the rig did anything about it.
	 */
	bool bKeyboardFocusOnViewport = false;
	/** Whether the rig handed the keyboard to the viewport itself, because the engine had not (see Start). */
	bool bKeyboardFocusGiven = false;
	/** Whether the keyboard focus was on the viewport (or inside it) once the rig was up: the state every typed character starts from. */
	bool bKeyboardFocusOnViewportWhenUp = false;
	/**
	 * The level editor viewport the session played in, as the rig found it -- meaningful when DestinationUsed
	 * is LevelEditorViewport. Ending a session carries the session viewport's size back into it, and the next
	 * session starts at whatever it holds, so this is what Finish puts back when the rig gave the session a size.
	 */
	FIntPoint EditorViewportSizeAtStart = FIntPoint::ZeroValue;
	/** Whether Finish put the editor viewport back to EditorViewportSizeAtStart after the session had carried the rig's size into it. */
	bool bEditorViewportSizeRestored = false;
	/** FApp::GetDeltaTime on the frame the rig came up: how long an engine frame is in this run. */
	double FrameSecondsAtStart = 0.0;
};

/**
 * The play-in-editor form of the driver's rig: the engine's own GameMode, LocalPlayer, viewport,
 * viewport client and frame loop, with the driver's context laid over them.
 *
 * WHY IT EXISTS. The headless rig is built by hand, and a hand-built rig can only be as faithful as
 * whoever wrote it guessed. This one is not built: it asks the editor for a play session and then
 * finds, in the world the engine made, the pieces the driver needs -- the player controller and its
 * local player (so a world-space raycaster deprojects through a real one), the viewport client (so a
 * character reaches a field the way the platform delivers it), the GameInstance (so tweens run in
 * engine frames). What it adds is only what a test needs and a game does not have: an input actor
 * whose module is the driver's, a screen-space root, and the context.
 *
 * EVERYTHING IS LATENT. A play session starts on one engine frame, its player arrives on a later one,
 * and every step the driver takes after that is one engine frame long. So a test body queues the
 * whole story and returns:
 *
 *     TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
 *     Rig->Start();
 *     Rig->WhenReady([](FDreamDriverPieRig& InRig) { InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0)); });
 *     Rig->Sequence().Click(FDreamBy::Name(TEXT("Play"))).Then([](FDreamDriverContext& InContext) { ... }).PerformLatent();
 *     Rig->Finish();
 *     return true;
 *
 * All four are queued from the test body, in that order, and the framework runs them in that order.
 * Nothing in the rig queues anything later, so a second rig queued after the first one's Finish
 * really does start after the first session has ended.
 *
 * LIFETIME. The context lives inside this object and FDreamDriverSequence's latent command holds it
 * by raw pointer; every command this rig queues -- and the gate step at the head of every sequence
 * it hands out -- holds the rig by shared reference, so the context outlives the commands that use
 * it. Hence Create rather than a constructor on the stack.
 *
 * FAILURE. When the session does not come up the rig says why on the running test and stays down:
 * WhenReady skips its action, a sequence fails at its first step (the gate) without running the rest,
 * and Finish still ends whatever session was started. A failing test never leaves a play session
 * running behind it for the next test to trip over, and the swapped viewport client class is put
 * back the moment the session exists (see Start).
 *
 * NO STRONG REFERENCES INTO THE PLAY WORLD. Ending a play session garbage-collects it and treats any
 * object from it that is still referenced as a fatal leak. The context and the rig therefore hold
 * raw pointers only -- they are cleared before the session is asked to end -- and anything a test
 * keeps between steps it keeps weakly.
 *
 * WHAT THE RIG CHANGES OUTSIDE THE PLAY WORLD, AND PUTS BACK. The next test runs in the same editor,
 * so everything the rig moves that outlives a session is put back the way it was found:
 *  - the engine's viewport client class and the saved "game gets mouse control" setting, for the one
 *    call that creates the session and not a moment longer (see Start);
 *  - the process-wide "a host delivers characters" switch, which typing turns on for good (Finish);
 *  - Slate's keyboard focus, which the rig hands to the session's viewport because a session whose
 *    game does not take the mouse only gets it when a player clicks in -- taken back in Finish if it is
 *    still there;
 *  - the level editor viewport's size. A viewport that came up with no size is given one, and ending
 *    the session carries that size back into the editor's own viewport, where the next session would
 *    start from it; Finish puts the editor viewport back once the session is gone.
 */
class FDreamDriverPieRig : public TSharedFromThis<FDreamDriverPieRig>
{
public:
	/** The only way to make one; see LIFETIME above. */
	static TSharedRef<FDreamDriverPieRig> Create(FAutomationTestBase& InTest, const FDreamPieRigOptions& InOptions = FDreamPieRigOptions());

	/** Public for MakeShared. Use Create. */
	FDreamDriverPieRig(FAutomationTestBase& InTest, const FDreamPieRigOptions& InOptions);
	~FDreamDriverPieRig();

	FDreamDriverPieRig(const FDreamDriverPieRig&) = delete;
	FDreamDriverPieRig& operator=(const FDreamDriverPieRig&) = delete;
	FDreamDriverPieRig(FDreamDriverPieRig&&) = delete;
	FDreamDriverPieRig& operator=(FDreamDriverPieRig&&) = delete;

	// ---------------------------------------------------------------- the story, queued from a test body

	/**
	 * Queue the way up: end any session a previous test left running, open a blank map, start a play
	 * session whose viewport client is UDreamGameViewportClient, wait for the player, give the viewport a
	 * size if it has none and the keyboard focus if the engine did not, attach the input actor, build the
	 * screen-space root, and let SettleFrames frames pass.
	 */
	void Start();

	/**
	 * Queue InAction to run once the rig is up -- the place to build widgets and bind listeners --
	 * followed by SettleFrames engine frames, so the next thing queued aims at laid-out widgets.
	 * Skipped when the rig did not come up; the reason has already been reported.
	 */
	void WhenReady(TFunction<void(FDreamDriverPieRig&)> InAction);

	/**
	 * A sequence over this rig's context, to be built up and PerformLatent'ed. Its first step is a gate
	 * that costs no frame: it fails the sequence, before any of the test's steps run, when the rig is
	 * not up, and otherwise refreshes the context's camera from the player's current view.
	 */
	FDreamDriverSequence Sequence();

	/**
	 * Queue the way down: take the input actor down through DreamDriverGameHost::Teardown (which leaves
	 * the session's own player alone), let go of every play-world object, end the play session, wait for
	 * it to be gone, and put back what the rig changed outside the play world -- the keyboard focus it
	 * gave, the editor viewport's size, the process-wide "a host delivers characters" switch -- the way
	 * Start found it. Runs whatever happened before it.
	 */
	void Finish();

	/**
	 * Add one step per character to InSequence that types the character the way the platform types it
	 * into a game: FSlateApplication::ProcessKeyCharEvent, which is where FSlateApplication::OnKeyChar
	 * puts a WM_CHAR, and which routes it along the keyboard user's focus path -- to the play session's
	 * viewport widget when that has the focus, then FSceneViewport::OnKeyChar, the viewport client's
	 * InputChar and, in UDreamGameViewportClient, the DreamGUI field being edited.
	 *
	 * The focus is checked before anything is sent: a character goes wherever Slate's keyboard focus is,
	 * and in an editor that could be whatever a person last clicked, so a step that finds the focus
	 * anywhere but on the viewport fails without sending. The rig gives the viewport the focus when the
	 * session comes up.
	 *
	 * A step succeeds only when the field being edited RECEIVED its character, whatever the field then
	 * did with it; otherwise it fails and names the gate that stopped it, in the order the character
	 * meets them (focus, viewport, console, client, field). "Slate handled it" is not delivery: a
	 * play-in-editor viewport claims every character it is given.
	 */
	static FDreamDriverSequence& TypeThroughViewport(FDreamDriverSequence& InSequence, const FString& InText);

	// ---------------------------------------------------------------- state

	/** Up: the session is running, the context is filled, and Finish has not begun. */
	bool IsAlive() const { return bAlive; }
	/** The rig gave up while coming up. GetFailure says why. */
	bool HasFailed() const { return !Failure.IsEmpty(); }
	const FString& GetFailure() const { return Failure; }
	const FDreamPieRigOptions& GetOptions() const { return Options; }
	const FDreamPieRigReport& GetReport() const { return Report; }
	FAutomationTestBase& GetTest() const { return *Test; }
	/**
	 * Whether Slate's keyboard focus is still on the viewport widget this rig handed it to. True while the
	 * rig is up (unless something took the focus since); false once Finish has taken it back -- and false
	 * when the rig never had to give it, or the widget went with its window.
	 */
	bool IsKeyboardFocusStillGiven() const;

	/** Null unless the rig is up. */
	UWorld* GetWorld() const;
	APlayerController* GetPlayerController() const;
	ULocalPlayer* GetLocalPlayer() const;
	UGameViewportClient* GetViewportClient() const;
	FSceneViewport* GetSceneViewport() const;
	UDreamWidget* Root() const;
	UDreamCanvas* RootCanvas() const;
	UDreamScreenSpaceRaycaster* Raycaster() const;
	/** The actor the rig's raycasters hang on. Not the input actor, which is Context().InputActor. */
	AActor* GetHostActor() const;

	FDreamDriverContext& Context() const;
	/** For queries (Exists, IsHovered, GetCentrePixel...). Its actions refuse under the engine pump; queue a Sequence instead. */
	FDreamDriverRef Driver() const;

	// ---------------------------------------------------------------- building, from inside WhenReady

	/**
	 * A hit-testable widget under InParent (the screen root when null), made in the same order the
	 * headless rig makes one -- register, parent, place, and the visual last, so the visual is enrolled
	 * with a render canvas -- and begun, because the play world's UI manager has begun play.
	 * InAnchoredPosition is in canvas units with Y upward, as there.
	 */
	UDreamWidget* MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/** The same with a visual of the caller's choosing -- a texture to paint a block, say. */
	UDreamWidget* MakeWidgetWithVisual(TSubclassOf<UDreamVisual> InVisualClass, const FString& InDisplayName,
		UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/** A control through the runtime's own factory (CreateDreamWidget), exactly as FDreamDriverRig::MakeControl builds one. */
	template<class T>
	T* MakeControl(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUserWidget>::Value,
			"MakeControl builds user widgets; a plain UDreamWidget is MakeWidget's job");
		return Cast<T>(MakeControl(T::StaticClass(), InDisplayName, InParent, InSize, InAnchoredPosition));
	}

	UDreamWidget* MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/**
	 * A world-space panel: a root widget of InSize carrying a WorldSpace canvas, put into the world on
	 * a scene component at InTransform through the runtime's own two calls (ConstructWidget and
	 * AttachWidgetToSceneComponent). The panel's face is its local X=0 plane, 2D X along local Y and 2D
	 * Y along local Z -- so a transform whose rotation is the player's view puts it square to the view.
	 * Controls go on it with MakeControl(..., Panel, ...).
	 */
	UDreamWidget* MakeWorldPanel(const FString& InDisplayName, const FTransform& InTransform, const FVector2D& InSize);

	/**
	 * The production world-space raycaster, for the rig's player, on the rig's host actor: the one that
	 * deprojects the pointer through ULocalPlayer::GetProjectionData. Placed rather than left to
	 * UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer, which is only asked by world widget
	 * components and screen pages -- neither of which a test's hand-built panel is.
	 */
	UDreamWorldSpaceRaycaster* AddWorldPointer();

	/**
	 * A locator for a widget this rig made, by the display name it was made with. Resolves without
	 * searching under the screen root, which is what a widget on a world-space panel needs: FDreamBy
	 * searches under the context's root, and a world panel is a root of its own. Unset when no widget
	 * of that name was made or it has gone.
	 */
	FDreamLocatorRef Made(const FString& InDisplayName) const;
	/** The widget itself, for a test reading its state. */
	UDreamWidget* FindMade(const FString& InDisplayName) const;

	/**
	 * Point Context().Camera at what the player sees now, through DreamDriverWorld::MirrorPlayerView --
	 * ULocalPlayer::GetViewPoint's reads (the camera manager's cached view, its FOV, then the
	 * controller's view point), the viewport's current size and the player's aspect-ratio axis
	 * constraint. The gate at the head of every Sequence calls it. False, with OutWhyNot saying why when
	 * given, when there is no player to look through.
	 */
	bool RefreshCameraFromPlayer(FString* OutWhyNot = nullptr);

private:
	/** One Update of the way up, called once per engine frame by the command Start queues. True when done, either way. */
	bool UpdateStart();
	/** One Update of the way down. */
	bool UpdateFinish();

	/** Report InReason on the test, stay down, and end the way up. */
	bool FailStart(const FString& InReason);
	/** Fill the context and build the screen root once the player is ready. Empty on success, the reason otherwise. */
	FString BuildOnPlayWorld();
	/** Whether the play world has a ready player: actors initialized, match started, a controller with a local player and a camera manager. */
	bool IsPlayerReady() const;
	/** DreamDriverGameHost::Teardown on the context, while the world it points into is still the live play world. */
	void TearDownInputHost();
	/** Let go of every play-world pointer, in the context and here. */
	void ReleasePlayWorld();
	/** Put back the viewport client class and the character switch, once. */
	void RestoreProcessState();
	/** Hand Slate's keyboard focus to the session's viewport, through the engine's own call for that, when the engine has not. */
	void GiveViewportKeyboardFocus(UGameViewportClient* InClient);
	/**
	 * Once the session has ended: take back what the rig changed that outlives the session -- the keyboard
	 * focus it gave, and the size the session carried back into the editor's level viewport. Once.
	 */
	void RestoreEditorState();
	/** Remember a widget this rig made, for Made/FindMade. */
	void RememberMade(const FString& InDisplayName, UDreamWidget* InWidget);
	/** Queue one latent command running InUpdate until it returns true. */
	static void Enqueue(TFunction<bool()> InUpdate);

	enum class EStartPhase : uint8
	{
		NotStarted,
		WaitForLeftoverSession,
		MakeMap,
		Launch,
		WaitForPlayer,
		Settle,
		Up,
		Failed,
	};

	enum class EFinishPhase : uint8
	{
		NotStarted,
		WaitForSessionToEnd,
		Done,
	};

	/** The test that queued this rig. Automation tests are static objects, so this never dangles. */
	FAutomationTestBase* Test = nullptr;
	FDreamPieRigOptions Options;
	FDreamPieRigReport Report;
	FString Failure;

	/** By pointer so its address survives anything that happens to this object; the sequence command holds it raw. */
	TUniquePtr<FDreamDriverContext> DriverContext;
	TSharedPtr<FDreamDriver> DriverInstance;

	EStartPhase StartPhase = EStartPhase::NotStarted;
	EFinishPhase FinishPhase = EFinishPhase::NotStarted;
	bool bAlive = false;
	/** This rig asked for the session that is (or was) running, so ending it is this rig's job. */
	bool bSessionStarted = false;
	bool bSessionEnded = false;
	double PhaseStartSeconds = 0.0;
	double LaunchSeconds = 0.0;
	int32 FramesInPhase = 0;
	/** Consecutive frames the player has been ready; see WaitForPlayer. */
	int32 ReadyFramesSeen = 0;
	int32 SettleFramesLeft = 0;

	/** What the process had before this rig changed it. */
	bool bRememberedProcessState = false;
	bool bRestoredProcessState = false;
	bool bHostDeliveredCharacterEventsAtStart = false;

	/**
	 * What the editor had before this rig changed it, for RestoreEditorState. Weak, and Slate's or the level
	 * editor's rather than the play world's: none of these is a UObject, so none of them is the kind of
	 * reference the end of a session treats as a leak -- and a new window's widget goes with its window.
	 */
	bool bRestoredEditorState = false;
	/** The level editor viewport's own scene viewport the session was played in. Unset for a window of its own. */
	TWeakPtr<FSceneViewport> EditorViewport;
	/** The widget the rig handed the keyboard focus to, and the one that had it before. */
	TWeakPtr<SWidget> KeyboardFocusGivenTo;
	TWeakPtr<SWidget> KeyboardFocusBeforeGiving;

	/** Play-world objects, raw on purpose (see NO STRONG REFERENCES). Cleared by ReleasePlayWorld. */
	AActor* HostActor = nullptr;
	UGameViewportClient* ViewportClient = nullptr;
	TArray<TWeakObjectPtr<UDreamWorldSpaceRaycaster>> WorldPointers;
	TMap<FString, TWeakObjectPtr<UDreamWidget>> MadeWidgets;
};
