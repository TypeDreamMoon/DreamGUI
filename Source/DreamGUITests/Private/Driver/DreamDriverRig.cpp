// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverRig.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamGUIEditorSubsystem.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportSplitScreen.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Interaction/UITextInput.h"
#include "Misc/AutomationTest.h"
#include "Misc/CoreDelegates.h"

#include "Driver/DreamDriverGameHost.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverSlateHost.h"
#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDriverRig, Log, All);

FDreamDriverRig FDreamDriverRig::Headless(FIntPoint InViewportSize)
{
	FDreamRigOptions ViewportOnly;
	ViewportOnly.ViewportSize = InViewportSize;
	return FDreamDriverRig(ViewportOnly);
}

FDreamDriverRig FDreamDriverRig::Headless(const FDreamRigOptions& InOptions)
{
	return FDreamDriverRig(InOptions);
}

FDreamDriverRig::FDreamDriverRig(const FDreamRigOptions& InOptions)
	: Options(InOptions)
{
	const FIntPoint InViewportSize = Options.ViewportSize;
	DriverContext = MakeUnique<FDreamDriverContext>();
	// Created unconditionally, even if the build below goes wrong, so Driver() is always answerable
	// and a test that forgot to check IsUsable fails on an assertion rather than on a null driver.
	DriverInstance = MakeShared<FDreamDriver>(*DriverContext);
	DriverContext->InputHost = Options.InputHost;
	DriverContext->PlayerIndex = 0;

	// Before anything is built, because building is what disturbs them: the process-wide switches a
	// text field flips the first time it is typed into outlive every world, and a rig that left them
	// flipped would decide which road the NEXT test's characters take.
	CaptureProcessState();

	// A combination of players, screens and host the rig cannot build is refused before there is a
	// world, with the reason, rather than built as something else -- one player where two were asked
	// for is a test that passes about nothing.
	BuildFailure = DescribeRefusedPlayerOptions(Options);
	if (!BuildFailure.IsEmpty())
	{
		return;
	}

	// 1. The world. With a game instance by default, because the tween manager is a game instance
	// subsystem: without one every UDreamTweenManager::To answers null, every Selectable transition
	// silently snaps or never happens, and the controls are being tested in the designer's preview
	// world rather than in a game's. The bare world stays available for exactly that comparison.
	UWorld* BuildWorld = nullptr;
	if (Options.bWithGameInstance)
	{
		ScopedGameInstanceWorld = MakeUnique<DreamTests::FScopedGameInstanceWorld>();
		BuildWorld = ScopedGameInstanceWorld->World;
		DriverContext->GameInstance = ScopedGameInstanceWorld->GameInstance;
		if (BuildWorld == nullptr)
		{
			BuildFailure = TEXT("UGameInstance::InitializeStandalone did not give the game instance a world");
			return;
		}
	}
	else
	{
		ScopedWorld = MakeUnique<DreamTests::FScopedGameWorld>(EWorldType::Game);
		BuildWorld = ScopedWorld->World;
		if (BuildWorld == nullptr)
		{
			BuildFailure = TEXT("UWorld::CreateWorld did not make a world");
			return;
		}
	}
	DriverContext->World = BuildWorld;
	DriverContext->Manager = UDreamUIManagerWorldSubsystem::GetInstance(BuildWorld);

	// 2. The actor the raycaster hangs on -- and, when the rig injects straight into the module, the
	// event system and the module too.
	Host = BuildWorld->SpawnActor<AActor>();
	if (Host == nullptr)
	{
		BuildFailure = TEXT("the world would not spawn the rig's host actor");
		return;
	}

	// 3. Where input enters.
	if (IsActorInputHost(Options.InputHost))
	{
		// A real input actor behind a real player controller: the game host builds the local player,
		// the controller and the actor, and hands back the actor's own event system and module. The
		// rig does not second-guess what it built -- a host that cannot be built is a rig that is not
		// usable, said in the host's own words.
		FString WhyNot;
		if (!DreamDriverGameHost::Build(*DriverContext, Options.InputHost, WhyNot))
		{
			BuildFailure = FString::Printf(TEXT("the input host could not be built: %s"),
				WhyNot.IsEmpty() ? TEXT("the game host gave no reason") : *WhyNot);
			return;
		}
		if (!IsValid(DriverContext->EventSystem) || !IsValid(DriverContext->InputModule))
		{
			BuildFailure = TEXT("the input host was built but did not provide an event system and an input module");
			return;
		}
	}
	else
	{
		UDreamEventSystem* BuiltEventSystem = NewObject<UDreamEventSystem>(Host);
		// AddInstanceComponent before RegisterComponent, the way the world-space raycast fixture does it:
		// it is what makes the component belong to the actor rather than merely be outered to it.
		Host->AddInstanceComponent(BuiltEventSystem);
		BuiltEventSystem->RegisterComponent();
		DriverContext->EventSystem = BuiltEventSystem;

		UDreamDriverInputModule* BuiltInputModule = NewObject<UDreamDriverInputModule>(Host);
		Host->AddInstanceComponent(BuiltInputModule);
		BuiltInputModule->RegisterComponent();
		BuiltInputModule->RegisterInputModuleToEventSystem(BuiltEventSystem);
		DriverContext->InputModule = BuiltInputModule;

		// The Slate source on top of the same pieces: the event system and the module still carry the player's
		// pointers -- in a game the preset actor's do -- and the world's source feeds them, as Slate would.
		if (Options.InputHost == EDreamRigInputHost::SlateSource)
		{
			FString WhyNot;
			if (!DreamDriverSlateHost::Build(*DriverContext, InViewportSize, WhyNot))
			{
				BuildFailure = FString::Printf(TEXT("the Slate input source could not be set up: %s"),
					WhyNot.IsEmpty() ? TEXT("the Slate host gave no reason") : *WhyNot);
				return;
			}
		}
	}

	// 3b. The other players' input, by the same host, in player order -- after player 0's, because an actor host's
	// players are the game instance's local players in order and the world's controllers in order, and the first of
	// each is player 0's. Then, for a split screen, the engine's layout of them.
	if (!BuildOtherPlayersInput())
	{
		return;
	}
	if (Options.PlayerScreens == EDreamRigPlayerScreens::Split && Options.PlayerCount > 1 && !LayOutSplitScreen())
	{
		return;
	}

	// 4. The root, its canvas and the screen-space raycaster.
	UDreamWidget* BuiltRoot = nullptr;
	UDreamCanvas* BuiltCanvas = MakeScreenRoot(TEXT("Root"), BuiltRoot);
	DriverContext->Root = BuiltRoot;
	if (BuiltCanvas == nullptr)
	{
		BuildFailure = TEXT("the root widget would not take a canvas");
		return;
	}
	DriverContext->RootCanvas = BuiltCanvas;
	// On a split screen player 0's screen is player 0's, as every screen UDreamScreenUISubsystem makes for a local player
	// is: laid out over that player's part of the viewport, hit there and drawn there alone (UDreamCanvas::
	// SetViewportPlayerIndex, UMG's AddToPlayerScreen). Its local player is built by now, in step 3.
	if (IsSplitScreen())
	{
		BuiltCanvas->SetViewportPlayerIndex(0);
	}
	DriverContext->Raycaster = MakeScreenRaycaster(Host, BuiltCanvas, 0);

	// 4b. The other players' screens and raycasters, and every context told of every player.
	if (!BuildOtherPlayersScreens())
	{
		return;
	}

	// 5. Everything the world starts with exists; now it begins play, before any control is made on it.
	OpenBeginPlayGate();

	// Two settling frames before anyone touches it. The first runs the layout the attach queued, the
	// second gives anything the first dirtied its own pass -- which is the one-pass convergence the
	// manager's own counter calls healthy. Without them the first action would hit-test a tree whose
	// widgets are all still at the origin.
	DriverContext->PumpFrames(2);

	if (!IsUsable() && BuildFailure.IsEmpty())
	{
		// Every early return above says why; this is the net under a piece that came back null or
		// invalid without anything having refused outright.
		BuildFailure = TEXT("the rig was built but a piece it needs is missing or invalid (world, event system, input module, UI manager, root, canvas or raycaster)");
	}
}

FString FDreamDriverRig::DescribeRefusedPlayerOptions(const FDreamRigOptions& InOptions)
{
	const int32 Count = InOptions.PlayerCount;
	if (Count < 1 || Count > DreamRigMaxPlayers)
	{
		return FString::Printf(TEXT("a rig is built for one to %d players, and %d were asked for: the engine's split-screen tables and UGameViewportClient::MaxSplitscreenPlayers stop at %d"),
			DreamRigMaxPlayers, Count, DreamRigMaxPlayers);
	}
	if (Count > 1 && InOptions.InputHost == EDreamRigInputHost::SlateSource)
	{
		return FString::Printf(TEXT("%d players were asked for under the Slate input source, which the rig can give only one: the source finds a Slate user's player through the local players' Slate users (UDreamUIInputSubsystem::FindUserIndexForSlateUser), a headless rig has no Slate users of its own, and its test mappers make Slate user 0 player 0 -- a mapping the rig wrote for more would be testing the rig. Use ModuleOnly or an actor host for several players"),
			Count);
	}
	if (Count > 1 && InOptions.PlayerScreens == EDreamRigPlayerScreens::Split && !IsActorInputHost(InOptions.InputHost))
	{
		return TEXT("a split screen was asked for without an actor host: a split screen is the engine laying out its local players (UGameViewportClient::LayoutPlayers sets ULocalPlayer::Origin and Size), and under ModuleOnly the rig's further players are script players with no local player to lay out -- and with none, the UI manager allows one screen-space overlay canvas, so a screen of their own would compete for it. Use StandaloneActor or EnhancedActor, or Shared");
	}
	return FString();
}

bool FDreamDriverRig::BuildOtherPlayersInput()
{
	UWorld* BuildWorld = DriverContext->World;
	for (int32 PlayerIndex = 1; PlayerIndex < Options.PlayerCount; ++PlayerIndex)
	{
		FOtherPlayer& Player = OtherPlayers.AddDefaulted_GetRef();
		Player.Context = MakeUnique<FDreamDriverContext>();
		FDreamDriverContext& PlayerContext = *Player.Context;
		Player.Driver = MakeShared<FDreamDriver>(PlayerContext);
		// The world's, the same on every player's context: what a step reads that belongs to nobody in particular.
		PlayerContext.World = BuildWorld;
		PlayerContext.Manager = DriverContext->Manager;
		PlayerContext.GameInstance = DriverContext->GameInstance;
		PlayerContext.InputHost = Options.InputHost;
		PlayerContext.CurrentTest = DriverContext->CurrentTest;
		PlayerContext.FrameSeconds = DriverContext->FrameSeconds;
		PlayerContext.PlayerIndex = PlayerIndex;

		// An actor of the player's own for its raycaster to ride on -- and, under ModuleOnly, its event system and
		// module, as player 0's ride on the rig's host.
		Player.Host = BuildWorld->SpawnActor<AActor>();
		if (Player.Host == nullptr)
		{
			BuildFailure = FString::Printf(TEXT("the world would not spawn player %d's host actor"), PlayerIndex);
			return false;
		}

		if (IsActorInputHost(Options.InputHost))
		{
			// The next local player, its controller and an input actor listening as it, built exactly as player 0's.
			FString WhyNot;
			if (!DreamDriverGameHost::Build(PlayerContext, Options.InputHost, WhyNot))
			{
				BuildFailure = FString::Printf(TEXT("player %d's input host could not be built: %s"), PlayerIndex,
					WhyNot.IsEmpty() ? TEXT("the game host gave no reason") : *WhyNot);
				return false;
			}
			if (!IsValid(PlayerContext.EventSystem) || !IsValid(PlayerContext.InputModule))
			{
				BuildFailure = FString::Printf(TEXT("player %d's input host was built but did not provide an event system and an input module"), PlayerIndex);
				return false;
			}
			continue;
		}

		// ModuleOnly (SlateSource takes one player): a script player, fed straight into its own module. The UserIndex
		// before the event system registers, so that it registers -- at the begin-play gate -- as the player it is.
		UDreamEventSystem* BuiltEventSystem = NewObject<UDreamEventSystem>(Player.Host);
		BuiltEventSystem->SetUserIndex(PlayerIndex);
		Player.Host->AddInstanceComponent(BuiltEventSystem);
		BuiltEventSystem->RegisterComponent();
		PlayerContext.EventSystem = BuiltEventSystem;

		UDreamDriverInputModule* BuiltInputModule = NewObject<UDreamDriverInputModule>(Player.Host);
		Player.Host->AddInstanceComponent(BuiltInputModule);
		BuiltInputModule->RegisterComponent();
		BuiltInputModule->RegisterInputModuleToEventSystem(BuiltEventSystem);
		PlayerContext.InputModule = BuiltInputModule;
	}
	return true;
}

bool FDreamDriverRig::LayOutSplitScreen()
{
	// The layout UGameViewportClient::UpdateActiveSplitscreenType picks for this many players under UGameMapsSettings'
	// defaults (EngineSettingsModule.cpp: two players Horizontal, three FavorTop; four Grid), read from the table the
	// engine fills in the viewport client's constructor -- the class the engine is configured to make, whose default
	// object carries it -- and written where LayoutPlayers writes it.
	const int32 Count = Options.PlayerCount;
	const ESplitScreenType::Type SplitType = Count == 2 ? ESplitScreenType::TwoPlayer_Horizontal
		: Count == 3 ? ESplitScreenType::ThreePlayer_FavorTop
		: ESplitScreenType::FourPlayer_Grid;
	const UGameViewportClient* Layouts = GEngine != nullptr && GEngine->GameViewportClientClass != nullptr
		? GEngine->GameViewportClientClass->GetDefaultObject<UGameViewportClient>()
		: GetDefault<UGameViewportClient>();
	if (Layouts == nullptr || !Layouts->SplitscreenInfo.IsValidIndex(SplitType)
		|| Layouts->SplitscreenInfo[SplitType].PlayerData.Num() < Count)
	{
		BuildFailure = FString::Printf(TEXT("the game viewport client's split-screen table has no layout of %d players to lay the local players out with"), Count);
		return false;
	}
	for (int32 PlayerIndex = 0; PlayerIndex < Count; ++PlayerIndex)
	{
		FDreamDriverContext* PlayerContext = FindPlayerContext(PlayerIndex);
		ULocalPlayer* LocalPlayer = PlayerContext != nullptr ? PlayerContext->LocalPlayer : nullptr;
		if (LocalPlayer == nullptr)
		{
			BuildFailure = FString::Printf(TEXT("player %d has no local player to give a part of the viewport to"), PlayerIndex);
			return false;
		}
		const FPerPlayerSplitscreenData& Part = Layouts->SplitscreenInfo[SplitType].PlayerData[PlayerIndex];
		LocalPlayer->Size.X = Part.SizeX;
		LocalPlayer->Size.Y = Part.SizeY;
		LocalPlayer->Origin.X = Part.OriginX;
		LocalPlayer->Origin.Y = Part.OriginY;
		PlayerContext->ViewOrigin01 = LocalPlayer->Origin;
		PlayerContext->ViewSize01 = LocalPlayer->Size;
	}
	return true;
}

UDreamCanvas* FDreamDriverRig::MakeScreenRoot(const FString& InDisplayName, UDreamWidget*& OutRoot)
{
	UDreamWidget* BuiltRoot = NewObject<UDreamWidget>(DriverContext->World, NAME_None, RF_Public | RF_Transactional);
	BuiltRoot->SetDisplayName(InDisplayName);
	BuiltRoot->OnRegister();
	OutRoot = BuiltRoot;

	UDreamCanvas* BuiltCanvas = BuiltRoot->AddComponent<UDreamCanvas>();
	if (BuiltCanvas == nullptr)
	{
		return nullptr;
	}
	BuiltCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	// AFTER the render mode. Setting the mode applies the viewport parameters, and at that moment the
	// only viewport there is is the 2x2 fallback; handing the canvas a real size is what un-does that,
	// and doing it in the other order would leave the fallback applied on top. The whole viewport for
	// every player's screen, a split screen's included: the override stands in for the whole viewport,
	// and a canvas given its player takes that player's part of it, as it would of the real one.
	BuiltCanvas->SetViewportSizeOverride(Options.ViewportSize);
	// The scaler AFTER the viewport, through the canvas's own setters: each of them re-runs
	// OnViewportParameterChanged, which recomputes the root's size and CanvasScale from the viewport
	// size the canvas has cached -- so that has to be the substituted one already. The mode goes
	// last, once the reference and the match it will read are in place; nothing is written when the
	// options leave the mode unset, which keeps the canvas's own default (ConstantPixelSize) and
	// today's rig exactly.
	if (Options.CanvasScaleMode.IsSet())
	{
		BuiltCanvas->SetReferenceResolution(Options.ReferenceResolution);
		BuiltCanvas->SetMatchFromWidthToHeight(Options.MatchFromWidthToHeight);
		BuiltCanvas->SetScaleMode(Options.CanvasScaleMode.GetValue());
	}
	return BuiltCanvas;
}

UDreamScreenSpaceRaycaster* FDreamDriverRig::MakeScreenRaycaster(AActor* InHost, UDreamCanvas* InCanvas, int32 InUserIndex)
{
	UDreamScreenSpaceRaycaster* BuiltRaycaster = NewObject<UDreamScreenSpaceRaycaster>(InHost);
	// A raycaster answers one player's pointers (UDreamUIInputUser::LineTrace skips the others'); player 0's is left at
	// the class's own UserIndex, as it always was.
	if (InUserIndex != 0)
	{
		BuiltRaycaster->SetUserIndex(InUserIndex);
	}
	BuiltRaycaster->SetRootCanvas(InCanvas);
	InHost->AddInstanceComponent(BuiltRaycaster);
	BuiltRaycaster->RegisterComponent();
	// Explicit, although registering an auto-activating component normally gets here by itself: the
	// list this puts it on is the one UDreamPointerInputModule::LineTrace walks, so a rig that was
	// not on it would trace nothing and every test would fail identically and unhelpfully. Enrolling
	// twice is a no-op -- AddRaycaster refuses duplicates.
	BuiltRaycaster->ActivateRaycaster();
	return BuiltRaycaster;
}

bool FDreamDriverRig::BuildOtherPlayersScreens()
{
	if (OtherPlayers.Num() == 0)
	{
		// A rig of one keeps the context it always had: no list of players, and the pump's own path for them.
		return true;
	}
	const bool bSplit = Options.PlayerScreens == EDreamRigPlayerScreens::Split;
	for (FOtherPlayer& Player : OtherPlayers)
	{
		FDreamDriverContext& PlayerContext = *Player.Context;
		if (bSplit)
		{
			// A screen of the player's own, as the screen UI gives every local player a root of its own -- and given its
			// player as the screen UI gives it, so it is that player's part of the viewport (see step 4 for player 0's).
			UDreamWidget* PlayerRoot = nullptr;
			UDreamCanvas* PlayerCanvas = MakeScreenRoot(FString::Printf(TEXT("Player%dRoot"), PlayerContext.PlayerIndex), PlayerRoot);
			PlayerContext.Root = PlayerRoot;
			Player.bOwnsScreen = PlayerRoot != nullptr;
			if (PlayerCanvas == nullptr)
			{
				BuildFailure = FString::Printf(TEXT("player %d's screen root would not take a canvas"), PlayerContext.PlayerIndex);
				return false;
			}
			PlayerCanvas->SetViewportPlayerIndex(PlayerContext.PlayerIndex);
			PlayerContext.RootCanvas = PlayerCanvas;
		}
		else
		{
			PlayerContext.Root = DriverContext->Root;
			PlayerContext.RootCanvas = DriverContext->RootCanvas;
		}
		PlayerContext.Raycaster = MakeScreenRaycaster(Player.Host, PlayerContext.RootCanvas, PlayerContext.PlayerIndex);
	}

	TArray<FDreamDriverContext*> AllPlayers;
	AllPlayers.Add(DriverContext.Get());
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		AllPlayers.Add(Player.Context.Get());
	}
	for (FDreamDriverContext* PlayerContext : AllPlayers)
	{
		PlayerContext->Players = AllPlayers;
	}
	return true;
}

bool FDreamDriverRig::IsSplitScreen() const
{
	return Options.PlayerScreens == EDreamRigPlayerScreens::Split && Options.PlayerCount > 1;
}

void FDreamDriverRig::ReleaseScreensFromPlayers()
{
	// A canvas keeps its player as an index into the game instance's local players and looks the player up whenever it
	// measures its part; once the local players come off, an index left behind names nobody -- or, after another player
	// joins, somebody else. Every screen the rig made, whoever gave it a player: the rig, or a test.
	TArray<UDreamCanvas*, TInlineAllocator<DreamRigMaxPlayers>> Screens;
	Screens.Add(DriverContext->RootCanvas);
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		if (Player.bOwnsScreen && Player.Context.IsValid())
		{
			Screens.Add(Player.Context->RootCanvas);
		}
	}
	for (UDreamCanvas* Screen : Screens)
	{
		if (IsValid(Screen))
		{
			Screen->SetViewportPlayerIndex(INDEX_NONE);
		}
	}
}

void FDreamDriverRig::TearDownOtherPlayersInput(FAutomationTestBase* InTest)
{
	for (int32 Index = OtherPlayers.Num() - 1; Index >= 0; --Index)
	{
		FDreamDriverContext* PlayerContext = OtherPlayers[Index].Context.Get();
		if (PlayerContext == nullptr)
		{
			continue;
		}
		if (IsActorInputHost(PlayerContext->InputHost))
		{
			DreamDriverGameHost::Teardown(*PlayerContext);
			// Each player's host is checked gone, which the game host's own tear-down promises: a local player left on
			// the game instance, or a controller or input actor left in the world, is the next player's index taken.
			TArray<FString> LeftBehind;
			if (PlayerContext->InputActor != nullptr)
			{
				LeftBehind.Add(TEXT("its input actor"));
			}
			if (PlayerContext->PlayerController != nullptr)
			{
				LeftBehind.Add(TEXT("its player controller"));
			}
			if (PlayerContext->LocalPlayer != nullptr)
			{
				LeftBehind.Add(TEXT("its local player"));
			}
			if (LeftBehind.Num() > 0)
			{
				ReportRigProblem(InTest, FString::Printf(TEXT("Player %d's input host was torn down and left %s behind"),
					PlayerContext->PlayerIndex, *FString::Join(LeftBehind, TEXT(" and "))));
			}
		}
	}
}

void FDreamDriverRig::TearDownOtherPlayersScreens()
{
	for (int32 Index = OtherPlayers.Num() - 1; Index >= 0; --Index)
	{
		FOtherPlayer& Player = OtherPlayers[Index];
		FDreamDriverContext* PlayerContext = Player.Context.Get();
		if (PlayerContext == nullptr)
		{
			continue;
		}
		if (Player.bOwnsScreen)
		{
			if (UDreamWidget* PlayerRoot = PlayerContext->Root; IsValid(PlayerRoot))
			{
				PlayerRoot->DestroyWidget();
			}
		}
		if (UDreamScreenSpaceRaycaster* PlayerRaycaster = PlayerContext->Raycaster; IsValid(PlayerRaycaster))
		{
			PlayerRaycaster->DeactivateRaycaster();
		}
		if (!IsActorInputHost(PlayerContext->InputHost))
		{
			if (UDreamDriverInputModule* PlayerModule = PlayerContext->InputModule; IsValid(PlayerModule))
			{
				PlayerModule->UnregisterInputModuleFromEventSystem();
			}
		}
	}
}

void FDreamDriverRig::CaptureProcessState()
{
	// Nothing of the text input's: which field a player types into, and whether a host delivers
	// characters, are both kept on the world's input and go with the rig's world. EndLeakedTextEdit is
	// the check that is left.
	WatchBlueprintCompiles();
}

#if WITH_EDITOR
namespace DreamDriverRigCompileLog
{
	/**
	 * The editor's Blueprint compile announcements, for the rest of the session from the first rig on.
	 *
	 * The compiling flag checked at tear-down is process state, and so is whatever set it: the compile
	 * that leaves it set has as often as not happened before the rig was built -- in a designer test
	 * just ahead of it, say -- and a rig that heard only what happened while it was up could say
	 * neither which Blueprint that was nor whether its end was ever announced. So the announcements
	 * are heard once for the session, and the last few kept with the frame and the test they came in.
	 */
	struct FAnnouncement
	{
		/** OnBlueprintCompiled names no Blueprint; OnBlueprintPreCompile does. */
		bool bFinished = false;
		FString Blueprint;
		uint64 Frame = 0;
		FString DuringTest;
	};

	/** Enough to show what came just before a rig, without keeping the session's worth. */
	constexpr int32 KeptAnnouncements = 8;

	TArray<FAnnouncement> Recent;
	FDelegateHandle PreCompileHandle;
	FDelegateHandle CompiledHandle;
	FDelegateHandle EnginePreExitHandle;
	uint64 ListeningSinceFrame = 0;

	void Record(bool bInFinished, const UBlueprint* InBlueprint)
	{
		if (Recent.Num() >= KeptAnnouncements)
		{
			Recent.RemoveAt(0);
		}
		FAnnouncement& Announcement = Recent.AddDefaulted_GetRef();
		Announcement.bFinished = bInFinished;
		Announcement.Blueprint = InBlueprint != nullptr ? InBlueprint->GetPathName() : FString();
		Announcement.Frame = GFrameCounter;
		const FAutomationTestBase* Running = FAutomationTestFramework::Get().GetCurrentTest();
		Announcement.DuringTest = Running != nullptr ? Running->GetTestFullName() : FString(TEXT("no test"));
	}

	void EnsureListening()
	{
		if (GEditor == nullptr || PreCompileHandle.IsValid())
		{
			return;
		}
		ListeningSinceFrame = GFrameCounter;
		PreCompileHandle = GEditor->OnBlueprintPreCompile().AddLambda([](UBlueprint* InBlueprint)
		{
			Record(false, InBlueprint);
		});
		CompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([]()
		{
			Record(true, nullptr);
		});
		// Taken off again before the editor goes away: the two lambdas are code in this module.
		EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			if (GEditor != nullptr)
			{
				GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
				GEditor->OnBlueprintCompiled().Remove(CompiledHandle);
			}
			PreCompileHandle.Reset();
			CompiledHandle.Reset();
		});
	}

	FString Describe(const FAnnouncement& InAnnouncement)
	{
		return InAnnouncement.bFinished
			? FString::Printf(TEXT("frame %llu, a compile announced finished (during %s)"), InAnnouncement.Frame, *InAnnouncement.DuringTest)
			: FString::Printf(TEXT("frame %llu, %s began compiling (during %s)"), InAnnouncement.Frame, *InAnnouncement.Blueprint, *InAnnouncement.DuringTest);
	}

	/**
	 * What was heard, said for a compiling flag found set at tear-down -- which is always a leak.
	 *
	 * UDreamGUIEditorSubsystem sets the flag on the OnBlueprintPreCompile of a widget class and clears it
	 * on OnBlueprintCompiled itself, and the editor pairs the two for every compile, failed ones included. So a flag still set
	 * once the rig is down is a compile the editor never announced as finished, and the sentence names
	 * the Blueprint that began it, the frame and the test it came in, whether the flag was already set
	 * when the rig was built -- a compile left open by an earlier test -- and the announcements before.
	 */
	FString DescribeSetFlag(bool bInSetWhenBuilt, uint64 InBuiltAtFrame)
	{
		const FAnnouncement* Last = Recent.Num() > 0 ? &Recent.Last() : nullptr;
		FString Why;
		if (Last == nullptr)
		{
			Why = FString::Printf(TEXT("DreamGUI's recompiling flag is set and no compile has been announced since the rigs began listening at frame %llu."),
				ListeningSinceFrame);
		}
		else if (Last->bFinished)
		{
			// Not expected: the clear runs inside the very broadcast recorded here. Said as it is, so a
			// change to that ordering shows up as itself rather than as a mystery.
			Why = FString::Printf(TEXT("DreamGUI's recompiling flag is set although the last announcement heard, at frame %llu during %s, was a compile finishing."),
				Last->Frame, *Last->DuringTest);
		}
		else
		{
			Why = FString::Printf(TEXT("DreamGUI's recompiling flag is set: %s began compiling at frame %llu, during %s, and no compile has been announced finished since."),
				*Last->Blueprint, Last->Frame, *Last->DuringTest);
		}
		TArray<FString> Heard;
		for (const FAnnouncement& Announcement : Recent)
		{
			Heard.Add(Describe(Announcement));
		}
		Why += FString::Printf(TEXT(" The flag was %s when this rig was built, at frame %llu. Heard, oldest first: %s."),
			bInSetWhenBuilt ? TEXT("already set") : TEXT("clear"), InBuiltAtFrame,
			Heard.Num() > 0 ? *FString::Join(Heard, TEXT("; ")) : TEXT("nothing"));
		return Why;
	}

	/** Whether the editor's DreamGUI subsystem believes a widget class is recompiling. */
	bool IsRecompiling()
	{
		const UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
		return EditorSubsystem != nullptr && EditorSubsystem->IsRecompiling();
	}
}
#endif

void FDreamDriverRig::WatchBlueprintCompiles()
{
#if WITH_EDITOR
	DreamDriverRigCompileLog::EnsureListening();
	bBlueprintCompilingWhenBuilt = DreamDriverRigCompileLog::IsRecompiling();
	BuiltAtFrame = GFrameCounter;
#endif
}

TArray<FString> FDreamDriverRig::DescribeUnsettledProcessState(int32 InLayoutPassDepth, int32 InDesiredSizeMemoDepth, bool bInBlueprintCompiling)
{
	TArray<FString> Complaints;
	if (InLayoutPassDepth != 0)
	{
		Complaints.Add(FString::Printf(TEXT("The rig world's layout pass depth is %d once its tree was torn down; a layout pass was entered and never left, so every later size edit in that world was being taken for layout output"), InLayoutPassDepth));
	}
	if (InDesiredSizeMemoDepth != 0)
	{
		Complaints.Add(FString::Printf(TEXT("The rig world's desired-size memo depth is %d once its tree was torn down; the memo was still live, so later measurements in that world could be answered from a pass that is over"), InDesiredSizeMemoDepth));
	}
	if (bInBlueprintCompiling)
	{
		Complaints.Add(TEXT("UDreamGUIEditorSubsystem still believes a widget class is recompiling after the rig was torn down; a compile it never heard finish leaves the trees it let go of unbuilt"));
	}
	return Complaints;
}

UUITextInput* FDreamDriverRig::FindEditInRigTree() const
{
	UUITextInput* Active = UUITextInput::GetActiveTextInput();
	UDreamWidget* RigRoot = DriverContext.IsValid() ? DriverContext->Root : nullptr;
	if (Active == nullptr || !IsValid(RigRoot))
	{
		return nullptr;
	}
	// Only a field under the rig's own root. A tree built elsewhere in the world -- a world-space
	// panel is its own root -- goes down with the world, not with this root, so its edit is still
	// legitimately open at this point and is checked after the world instead.
	for (UDreamWidget* Walk = Active->GetWidget(); Walk != nullptr; Walk = Walk->GetParent())
	{
		if (Walk == RigRoot)
		{
			return Active;
		}
	}
	return nullptr;
}

bool FDreamDriverRig::IsUnderRigScreens(const UDreamWidget* InWidget) const
{
	TArray<const UDreamWidget*, TInlineAllocator<DreamRigMaxPlayers>> Screens;
	if (DriverContext.IsValid() && IsValid(DriverContext->Root))
	{
		Screens.Add(DriverContext->Root);
	}
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		if (Player.bOwnsScreen && Player.Context.IsValid() && IsValid(Player.Context->Root))
		{
			Screens.Add(Player.Context->Root);
		}
	}
	for (const UDreamWidget* Walk = InWidget; Walk != nullptr; Walk = Walk->GetParent())
	{
		if (Screens.Contains(Walk))
		{
			return true;
		}
	}
	return false;
}

TMap<int32, TWeakObjectPtr<UUITextInput>> FDreamDriverRig::FindPlayerEditsInRigTree() const
{
	// Each player's own field, from that player's input in the rig's world (UUITextInput::GetActiveTextInputForPlayer):
	// with several players the process-wide answer is whichever player's field is found first, which would let one
	// player's leaked edit hide another's. A field on a world-space panel goes with the world, as it does for one player.
	TMap<int32, TWeakObjectPtr<UUITextInput>> Edits;
	UWorld* RigWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	if (RigWorld == nullptr)
	{
		return Edits;
	}
	for (int32 PlayerIndex = 0; PlayerIndex < GetPlayerCount(); ++PlayerIndex)
	{
		UUITextInput* Field = UUITextInput::GetActiveTextInputForPlayer(RigWorld, PlayerIndex);
		if (Field != nullptr && IsUnderRigScreens(Field->GetWidget()))
		{
			Edits.Add(PlayerIndex, Field);
		}
	}
	return Edits;
}

void FDreamDriverRig::EndLeakedPlayerTextEdits(const TMap<int32, TWeakObjectPtr<UUITextInput>>& InEditsInRigTree, FAutomationTestBase* InTest)
{
	// EndLeakedTextEdit, player by player: the trees are gone, so a field still being edited by the player who was
	// editing it is one tear-down missed, ended through its own DeactivateInput while the world still stands.
	UWorld* RigWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	for (const TPair<int32, TWeakObjectPtr<UUITextInput>>& Edit : InEditsInRigTree)
	{
		UUITextInput* Field = Edit.Value.Get();
		if (Field != nullptr && RigWorld != nullptr && UUITextInput::GetActiveTextInputForPlayer(RigWorld, Edit.Key) == Field)
		{
			ReportRigProblem(InTest, FString::Printf(TEXT("Player %d's text field was still the field it was editing after the rig's trees were destroyed; its edit has been ended here so it does not leak into the next test"),
				Edit.Key));
			Field->DeactivateInput(false);
		}
	}
}

void FDreamDriverRig::EndLeakedTextEdit(UUITextInput* InEditInRigTree, FAutomationTestBase* InTest)
{
	// The field being edited is not put back to what it was before the rig: its only writer is
	// ActivateInput, and reviving a field from before this rig -- from another test's world, most
	// likely already gone -- is not something a tear-down should do. What is guaranteed is weaker and
	// is the part that matters: the process-wide pointer does not name anything of THIS rig's once
	// it is gone. The rig's tree has been destroyed by the time this runs, which ends an edit on
	// unregister (UUITextInput::OnUnregister); the field that was being edited in it and is still
	// named here is one that tear-down missed, which is a runtime fault to report, and the edit is
	// ended through the field's own DeactivateInput -- while its world still stands -- so it cannot
	// route the next test's characters into a dead world.
	if (InEditInRigTree != nullptr && UUITextInput::GetActiveTextInput() == InEditInRigTree)
	{
		ReportRigProblem(InTest, TEXT("A text field of this rig was still the active text input after its tree was destroyed; its edit has been ended here so it does not leak into the next test"));
		InEditInRigTree->DeactivateInput(false);
	}
}

void FDreamDriverRig::ReportTextEditOutlivingWorld(const UWorld* InRigWorld, FAutomationTestBase* InTest)
{
	// After the world: every tree in it has gone down with the UI manager (its Deinitialize destroys
	// the registered trees), a world-space panel's included, and every edit with them. The weak
	// pointer answers null for a field the world took with it, so anything still named here survived
	// its own world -- reported, and left alone, because there is no longer a world to end it in.
	UUITextInput* StillActive = UUITextInput::GetActiveTextInput();
	if (StillActive != nullptr && InRigWorld != nullptr && StillActive->GetWorld() == InRigWorld)
	{
		ReportRigProblem(InTest, TEXT("A text field of this rig's world is still the active text input after the world was destroyed"));
	}
}

void FDreamDriverRig::RestoreAndVerifyProcessState(FAutomationTestBase* InTest, int32 InLayoutPassDepth, int32 InDesiredSizeMemoDepth)
{
	// Last, once the tree and the world are both gone. The layout counters were read from the world's
	// layout context before the world went (the destructor hands them in): a layout pass or a
	// desired-size memo scope entered and never left. They die with the world, so they cannot reach the
	// next test, but one still open is a pass that never ended in this one. The recompiling flag is the
	// editor subsystem's and does outlive worlds: a compile it never heard finish is one whose released
	// trees nothing builds again.
	bool bCompiling = false;
#if WITH_EDITOR
	// Set at all is the leak: the editor subsystem clears it on the announcement that a compile is over,
	// so nothing is still queued by now. Reported, with what was heard and in which test, whether the
	// compile began while the rig was up or before it was built (DreamDriverRigCompileLog).
	if (DreamDriverRigCompileLog::IsRecompiling())
	{
		bCompiling = true;
		const FString Why = DreamDriverRigCompileLog::DescribeSetFlag(bBlueprintCompilingWhenBuilt, BuiltAtFrame);
		if (InTest != nullptr)
		{
			InTest->AddInfo(Why);
		}
		else
		{
			UE_LOG(LogDreamDriverRig, Display, TEXT("%s"), *Why);
		}
	}
#endif
	for (const FString& Complaint : DescribeUnsettledProcessState(InLayoutPassDepth, InDesiredSizeMemoDepth, bCompiling))
	{
		ReportRigProblem(InTest, Complaint);
	}
}

void FDreamDriverRig::ReportRigProblem(FAutomationTestBase* InTest, const FString& InMessage)
{
	// The bound test when there is one, which is where the failure belongs; otherwise the log at
	// Error, which the automation framework still turns into a failure of whatever test is running.
	if (InTest != nullptr)
	{
		InTest->AddError(InMessage);
	}
	else
	{
		UE_LOG(LogDreamDriverRig, Error, TEXT("%s"), *InMessage);
	}
}

void FDreamDriverRig::OpenBeginPlayGate()
{
	UWorld* HostWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	UDreamUIManagerWorldSubsystem* HostManager = DriverContext.IsValid() ? DriverContext->Manager : nullptr;
	if (HostWorld == nullptr || !IsValid(HostManager))
	{
		return;
	}

	// The event system's half: what UDreamEventSystem::BeginPlay does when a world begins play is
	// enrol with the world's input subsystem, and that enrolment is all it does. It is done here
	// directly rather than by calling the component's BeginPlay, which would also mark it begun in a
	// world that is not -- and it is guarded, so EnsureGameInputHost, which makes the same call, stays a
	// no-op after it.
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	UDreamUIInputSubsystem* HostInput = UDreamUIInputSubsystem::Get(HostWorld);
	if (IsValid(HostEventSystem) && HostInput != nullptr
		&& HostInput->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		HostInput->AddEventSystem(HostEventSystem);
	}
	// Every other player's, in player order, the same way: an input actor's event system has enrolled already, in its
	// own BeginPlay, and a script player's is enrolled here, as the player its UserIndex names.
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		UDreamEventSystem* PlayerEventSystem = Player.Context.IsValid() ? Player.Context->EventSystem : nullptr;
		if (IsValid(PlayerEventSystem) && HostInput != nullptr
			&& HostInput->GetEventSystemByUserIndex(PlayerEventSystem->GetUserIndex()) != PlayerEventSystem)
		{
			HostInput->AddEventSystem(PlayerEventSystem);
		}
	}

	// The UI manager's half: OnWorldBeginPlay begins every registered widget that has not begun,
	// which at this point is the root and its canvas and nothing else. Once, because the engine base
	// ensures on a second call and a widget's BeginPlay checks it has not begun -- HasBegunPlay is the
	// guard for both. From here RegisterDreamWidgetHierarchy (MakeControl's road) begins every control
	// it registers, and the pump's TickDreamUI runs their Start and Tick; nothing else drives either.
	if (!HostManager->HasBegunPlay())
	{
		HostManager->OnWorldBeginPlay(*HostWorld);
	}
}

FDreamDriverRig::~FDreamDriverRig()
{
	// Taken before the context goes: the checks at the very end report against the test that was
	// running, and by then the context that knew it has been released.
	FAutomationTestBase* TeardownTest = DriverContext.IsValid() ? DriverContext->CurrentTest : nullptr;
	UWorld* RigWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	int32 LayoutPassDepthAtTeardown = 0;
	int32 MemoDepthAtTeardown = 0;

	// Reverse order, and the widget tree before the world: DestroyWidget unregisters the tree from
	// the UI manager while the world is still whole, which is where the manager expects to be told.
	if (DriverContext.IsValid())
	{
		// The input host first, as it was built last of the input pieces: its local player would
		// otherwise outlive the world (local players belong to the game instance, not the world), and
		// its actor would still be delivering input into a tree that is being taken apart. The Slate
		// source the same way: off before the tree it feeds goes. The other players' hosts before
		// player 0's, last player first, so that every local player comes off the game instance from
		// the end and no player's index moves under it. Before any of that, the screens let go of their
		// players, which the input hosts' tear-down takes off the game instance (ReleaseScreensFromPlayers).
		ReleaseScreensFromPlayers();
		TearDownOtherPlayersInput(TeardownTest);
		if (IsActorInputHost(DriverContext->InputHost))
		{
			DreamDriverGameHost::Teardown(*DriverContext);
			// On a rig of several, player 0 is held to what the others are (TearDownOtherPlayersInput), and the game
			// instance is to have none of the rig's local players left.
			if (OtherPlayers.Num() > 0)
			{
				if (DriverContext->InputActor != nullptr || DriverContext->PlayerController != nullptr || DriverContext->LocalPlayer != nullptr)
				{
					ReportRigProblem(TeardownTest, TEXT("Player 0's input host was torn down and left its input actor, controller or local player behind"));
				}
				if (const UGameInstance* RigGameInstance = DriverContext->GameInstance; IsValid(RigGameInstance) && RigGameInstance->GetNumLocalPlayers() > 0)
				{
					ReportRigProblem(TeardownTest, FString::Printf(TEXT("The rig's game instance still has %d local player(s) after every player's input host was torn down"),
						RigGameInstance->GetNumLocalPlayers()));
				}
			}
		}
		else if (DriverContext->InputHost == EDreamRigInputHost::SlateSource)
		{
			DreamDriverSlateHost::Teardown(*DriverContext);
		}
		// Asked before the tree goes, while "is it under the rig's root" still has an answer. A rig of one asks the
		// process-wide question it always asked; a rig of several asks each player.
		const bool bSeveralPlayers = OtherPlayers.Num() > 0;
		UUITextInput* EditInRigTree = bSeveralPlayers ? nullptr : FindEditInRigTree();
		const TMap<int32, TWeakObjectPtr<UUITextInput>> PlayerEditsInRigTree = bSeveralPlayers
			? FindPlayerEditsInRigTree() : TMap<int32, TWeakObjectPtr<UUITextInput>>();
		TearDownOtherPlayersScreens();
		if (UDreamWidget* RootWidget = DriverContext->Root; IsValid(RootWidget))
		{
			RootWidget->DestroyWidget();
		}
		if (UDreamScreenSpaceRaycaster* RigRaycaster = DriverContext->Raycaster; IsValid(RigRaycaster))
		{
			RigRaycaster->DeactivateRaycaster();
		}
		// Only the module the rig made itself. An input actor's module is the actor's, and the game
		// host's tear-down is what releases it.
		if (!IsActorInputHost(DriverContext->InputHost))
		{
			if (UDreamDriverInputModule* RigInputModule = DriverContext->InputModule; IsValid(RigInputModule))
			{
				RigInputModule->UnregisterInputModuleFromEventSystem();
			}
		}
		// After the tree, before the world: the tree's destruction is what should have ended an edit
		// in it, and the world still being whole is what lets a missed one be ended properly.
		EndLeakedTextEdit(EditInRigTree, TeardownTest);
		EndLeakedPlayerTextEdits(PlayerEditsInRigTree, TeardownTest);
		// The world's layout context goes with its manager, so what it says about passes left open
		// is read now, with the tree gone and the world still whole.
		if (const UDreamUIManagerWorldSubsystem* RigManager = DriverContext->Manager; IsValid(RigManager))
		{
			LayoutPassDepthAtTeardown = RigManager->GetLayoutPassContext().GetPassDepth();
			MemoDepthAtTeardown = RigManager->GetLayoutPassContext().GetMemoDepth();
		}
	}
	// The other players' drivers and contexts before player 0's: their contexts list player 0's.
	OtherPlayers.Reset();
	DriverInstance.Reset();
	DriverContext.Reset();
	// Exactly one of these holds the world; the game instance one also shuts its game instance down
	// and takes its world context off the engine's list.
	ScopedWorld.Reset();
	ScopedGameInstanceWorld.Reset();

	ReportTextEditOutlivingWorld(RigWorld, TeardownTest);
	RestoreAndVerifyProcessState(TeardownTest, LayoutPassDepthAtTeardown, MemoDepthAtTeardown);
}

bool FDreamDriverRig::IsUsable() const
{
	// A recorded failure wins even if every pointer happens to be set: a half-built input host can
	// leave an event system behind it and still not be a host anything should be driven through.
	if (!BuildFailure.IsEmpty()
		|| !DriverContext.IsValid()
		|| !DriverContext->IsUsable()
		|| !IsValid(DriverContext->RootCanvas)
		|| !IsValid(DriverContext->Raycaster))
	{
		return false;
	}
	// Every player whole, or the rig is not: a test of two players on half a second player is a test of one.
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		if (!Player.Context.IsValid() || !Player.Context->IsUsable()
			|| !IsValid(Player.Context->RootCanvas) || !IsValid(Player.Context->Raycaster))
		{
			return false;
		}
	}
	return OtherPlayers.Num() == Options.PlayerCount - 1;
}

UWorld* FDreamDriverRig::GetWorld() const
{
	return DriverContext.IsValid() ? DriverContext->World : nullptr;
}

UDreamWidget* FDreamDriverRig::Root() const
{
	return DriverContext.IsValid() ? DriverContext->Root : nullptr;
}

UDreamCanvas* FDreamDriverRig::RootCanvas() const
{
	return DriverContext.IsValid() ? DriverContext->RootCanvas : nullptr;
}

UDreamEventSystem* FDreamDriverRig::EventSystem() const
{
	return DriverContext.IsValid() ? DriverContext->EventSystem : nullptr;
}

UDreamDriverInputModule* FDreamDriverRig::InputModule() const
{
	return DriverContext.IsValid() ? DriverContext->InputModule : nullptr;
}

UDreamScreenSpaceRaycaster* FDreamDriverRig::Raycaster() const
{
	return DriverContext.IsValid() ? DriverContext->Raycaster : nullptr;
}

FDreamDriverContext& FDreamDriverRig::Context() const
{
	return *DriverContext;
}

FDreamDriverRef FDreamDriverRig::Driver() const
{
	return DriverInstance.ToSharedRef();
}

const FDreamRigOptions& FDreamDriverRig::GetOptions() const
{
	return Options;
}

UGameInstance* FDreamDriverRig::GetGameInstance() const
{
	return DriverContext.IsValid() ? DriverContext->GameInstance : nullptr;
}

APlayerController* FDreamDriverRig::GetPlayerController() const
{
	return DriverContext.IsValid() ? DriverContext->PlayerController : nullptr;
}

AActor* FDreamDriverRig::GetHostActor() const
{
	return Host;
}

const FString& FDreamDriverRig::GetBuildFailure() const
{
	return BuildFailure;
}

void FDreamDriverRig::BindTest(FAutomationTestBase* InTest)
{
	if (DriverContext.IsValid())
	{
		DriverContext->CurrentTest = InTest;
	}
	for (FOtherPlayer& Player : OtherPlayers)
	{
		if (Player.Context.IsValid())
		{
			Player.Context->CurrentTest = InTest;
		}
	}
}

int32 FDreamDriverRig::GetPlayerCount() const
{
	return 1 + OtherPlayers.Num();
}

FDreamDriverContext* FDreamDriverRig::FindPlayerContext(int32 InPlayerIndex) const
{
	if (InPlayerIndex == 0)
	{
		return DriverContext.Get();
	}
	const int32 OtherIndex = InPlayerIndex - 1;
	return OtherPlayers.IsValidIndex(OtherIndex) ? OtherPlayers[OtherIndex].Context.Get() : nullptr;
}

FDreamDriverContext& FDreamDriverRig::Context(int32 InPlayerIndex) const
{
	if (FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex))
	{
		return *Found;
	}
	ReportRigProblem(DriverContext.IsValid() ? DriverContext->CurrentTest : nullptr,
		FString::Printf(TEXT("The rig was asked for player %d and was built for %d player(s); player 0's context is answered so the test fails here rather than crashing"),
			InPlayerIndex, GetPlayerCount()));
	return *DriverContext;
}

FDreamDriverRef FDreamDriverRig::Driver(int32 InPlayerIndex) const
{
	if (InPlayerIndex == 0)
	{
		return DriverInstance.ToSharedRef();
	}
	const int32 OtherIndex = InPlayerIndex - 1;
	if (OtherPlayers.IsValidIndex(OtherIndex) && OtherPlayers[OtherIndex].Driver.IsValid())
	{
		return OtherPlayers[OtherIndex].Driver.ToSharedRef();
	}
	ReportRigProblem(DriverContext.IsValid() ? DriverContext->CurrentTest : nullptr,
		FString::Printf(TEXT("The rig was asked for player %d's driver and was built for %d player(s); player 0's is answered so the test fails here rather than crashing"),
			InPlayerIndex, GetPlayerCount()));
	return DriverInstance.ToSharedRef();
}

UDreamWidget* FDreamDriverRig::Root(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->Root : nullptr;
}

UDreamCanvas* FDreamDriverRig::RootCanvas(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->RootCanvas : nullptr;
}

UDreamEventSystem* FDreamDriverRig::EventSystem(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->EventSystem : nullptr;
}

UDreamDriverInputModule* FDreamDriverRig::InputModule(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->InputModule : nullptr;
}

UDreamScreenSpaceRaycaster* FDreamDriverRig::Raycaster(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->Raycaster : nullptr;
}

APlayerController* FDreamDriverRig::GetPlayerController(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->PlayerController : nullptr;
}

ULocalPlayer* FDreamDriverRig::GetLocalPlayer(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	return Found != nullptr ? Found->LocalPlayer : nullptr;
}

AActor* FDreamDriverRig::GetHostActor(int32 InPlayerIndex) const
{
	if (InPlayerIndex == 0)
	{
		return Host;
	}
	const int32 OtherIndex = InPlayerIndex - 1;
	return OtherPlayers.IsValidIndex(OtherIndex) ? OtherPlayers[OtherIndex].Host : nullptr;
}

FBox2D FDreamDriverRig::GetPlayerViewRect(int32 InPlayerIndex) const
{
	const FDreamDriverContext* Found = FindPlayerContext(InPlayerIndex);
	if (Found == nullptr)
	{
		return FBox2D(ForceInit);
	}
	// ULocalPlayer::GetProjectionData's truncations, so the rect is the one a world pointer of the player deprojects in.
	const FIntPoint Size = Options.ViewportSize;
	const int32 X = FMath::TruncToInt(Found->ViewOrigin01.X * Size.X);
	const int32 Y = FMath::TruncToInt(Found->ViewOrigin01.Y * Size.Y);
	const int32 SizeX = FMath::TruncToInt(Found->ViewSize01.X * Size.X);
	const int32 SizeY = FMath::TruncToInt(Found->ViewSize01.Y * Size.Y);
	return FBox2D(FVector2D(X, Y), FVector2D(X + SizeX, Y + SizeY));
}

UDreamWidget* FDreamDriverRig::MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
	const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}

	UDreamWidget* NewWidget = NewObject<UDreamWidget>(DriverContext->World, NAME_None, RF_Public | RF_Transactional);
	NewWidget->SetDisplayName(InDisplayName);
	NewWidget->SetWidth(InSize.X);
	NewWidget->SetHeight(InSize.Y);
	NewWidget->OnRegister();
	NewWidget->TrySetParent(Parent, false);
	NewWidget->SetAnchoredPosition(InAnchoredPosition);
	// Last, once the widget has a parent and therefore a render canvas to be enrolled with.
	NewWidget->CreateNewVisual<UDreamVisualEmpty>();
	// The rule the runtime's own creation roads apply -- UDreamUIBPLibrary's RegisterAndPark and
	// RegisterDreamWidgetHierarchy both begin a widget made after the MANAGER has begun play. Without it
	// a widget made here would sit registered in a begun world without ever having begun, a state no
	// game can reach, and a behaviour added to it later would never Awake.
	if (IsValid(DriverContext->Manager) && DriverContext->Manager->HasBegunPlay() && !NewWidget->HasBegunPlay())
	{
		NewWidget->BeginPlay();
	}
	return NewWidget;
}

UDreamWidget* FDreamDriverRig::MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName,
	UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr || !IsValid(InClass))
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}
	// Before the control exists, so nothing it does on the way in -- a part taking the selection, a
	// field beginning an edit -- meets a world without the host a game would have given it.
	EnsureGameInputHost();

	// The runtime's own factory, not a copy of it. What it does in order -- instance, Initialize,
	// parent before register, register the whole hierarchy -- is exactly the part a fixture would get
	// subtly wrong by hand, and the callback is the seam it offers for writing properties before
	// anything registered can observe them.
	UDreamUserWidget* Control = CreateDreamWidget(DriverContext->World, InClass, Parent,
		[&InDisplayName, &InSize](UDreamUserWidget* InBuilt)
		{
			InBuilt->SetDisplayName(InDisplayName);
			InBuilt->SetWidth(InSize.X);
			InBuilt->SetHeight(InSize.Y);
		});
	if (Control == nullptr)
	{
		return nullptr;
	}
	// After registration, as in MakeWidget: the anchor is resolved against the parent the control is
	// now registered under.
	Control->SetAnchoredPosition(InAnchoredPosition);
	return Control;
}

void FDreamDriverRig::EnsureGameInputHost()
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return;
	}
	UWorld* HostWorld = DriverContext->World;

	UDreamUIInputSubsystem* HostInput = UDreamUIInputSubsystem::Get(HostWorld);
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	if (HostInput != nullptr && IsValid(HostEventSystem)
		&& HostInput->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		// The same call UDreamEventSystem::BeginPlay makes. Not BeginPlay itself: that would also mark
		// the component as having begun play in a world that never did, and nothing here needs the
		// rest of what that means.
		HostInput->AddEventSystem(HostEventSystem);
	}
	for (const FOtherPlayer& Player : OtherPlayers)
	{
		UDreamEventSystem* PlayerEventSystem = Player.Context.IsValid() ? Player.Context->EventSystem : nullptr;
		if (HostInput != nullptr && IsValid(PlayerEventSystem)
			&& HostInput->GetEventSystemByUserIndex(PlayerEventSystem->GetUserIndex()) != PlayerEventSystem)
		{
			HostInput->AddEventSystem(PlayerEventSystem);
		}
	}

	// The context's controller first: an input host (or a PIE rig) has already given the world its
	// player 0, with a local player behind it, and a second controller spawned here would be a second
	// player 0 that half of the lookups find and half do not.
	APlayerController* HostController = DriverContext->PlayerController;
	if (!IsValid(HostController))
	{
		HostController = HostWorld->GetFirstPlayerController();
	}
	if (HostController == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		HostController = HostWorld->SpawnActor<APlayerController>(SpawnParameters);
		if (HostController != nullptr)
		{
			// Spawning alone does NOT put it on the world's controller list here. That happens in
			// AController::PostInitializeComponents, and AActor::PostActorConstruction runs
			// Pre/PostInitializeComponents only when World->AreActorsInitialized() -- which a world made
			// with UWorld::CreateWorld never is, because nothing ran InitializeActorsForPlay on it. Off the
			// list, the controller is invisible to GetFirstPlayerController and to
			// UGameplayStatics::GetPlayerController, so the text field's key agent found no player to
			// enable its input on, kept a null InputComponent, and BindKeys dereferenced it. This is the
			// one call the skipped PostInitializeComponents would have made that anything here needs.
			HostWorld->AddController(HostController);
		}
	}
	if (HostController != nullptr && HostController->PlayerInput == nullptr)
	{
		// What a game gives the controller when a local player is assigned to it (SetPlayer): its
		// PlayerInput and its own InputComponent. It also pushes input onto every AutoReceiveInput actor
		// that registered while there was no controller to enable it on (ULevel::PushPendingAutoReceiveInput),
		// which is the other half of how a field's key agent gets its InputComponent. AFTER AddController:
		// that push finds the controller's player index by walking the world's controller list.
		HostController->InitInputSystem();
	}
	// Remembered, so GetPlayerController answers and the next call reuses it rather than looking again.
	if (HostController != nullptr)
	{
		DriverContext->PlayerController = HostController;
	}
}

void FDreamDriverRig::PumpFrames(int32 InFrameCount)
{
	if (DriverContext.IsValid())
	{
		DriverContext->PumpFrames(InFrameCount);
	}
}
