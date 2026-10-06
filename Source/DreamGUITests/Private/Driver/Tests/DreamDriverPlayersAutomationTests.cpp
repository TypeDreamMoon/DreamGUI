// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "DreamInputPipelineTestTypes.h"
#include "DreamPlayerScreenTestTypes.h"

/*
 * SEVERAL PLAYERS ON ONE RIG.
 *
 * A rig built with FDreamRigOptions::PlayerCount has a context per player -- its own event system, input entry and
 * screen raycaster, UserIndex 0, 1, ... -- and a step goes in as the player whose driver or sequence it is
 * (Rig.Driver(N), FDreamDriverSequence::AsPlayer). Under ModuleOnly a further player is what the input subsystem calls a
 * script player; under an actor host it is a real local player with its own controller and input actor, so its keys
 * and buttons go through its own controller's input stack. These pin what that has to mean for a test: each player's
 * pointer acts on what it is over and is stamped with its own UserIndex, each player's keys reach the widget that
 * player focused and nobody else's, a split screen gives each player its part of the viewport, and the rig refuses,
 * saying why, what it cannot build rather than building one player where two were asked for.
 */
namespace DreamDriverPlayersTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D TargetSize(200.0, 100.0);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	/** Every host that builds several players. SlateSource takes one; see the refusal test. */
	const FHostCase PlayerHosts[] = {
		{ EDreamRigInputHost::ModuleOnly, TEXT("module only") },
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};

	/** The hosts that have real local players, which is what a split screen lays out. */
	const FHostCase ActorHosts[] = {
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
	};

	FDreamRigOptions TwoPlayers(EDreamRigInputHost InHost, EDreamRigPlayerScreens InScreens)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		Options.PlayerCount = 2;
		Options.PlayerScreens = InScreens;
		return Options;
	}

	/** "[standalone input actor] what", so a failure says which host it failed under. */
	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	/** The rig-came-up claim, carrying the rig's own reason when it did not. */
	FString RigCameUp(const FHostCase& InCase, const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? Under(InCase, TEXT("The rig came up with two players"))
			: FString::Printf(TEXT("[%s] The rig came up with two players -- it did not: %s"), InCase.Name, *WhyNot);
	}

	/** A hit-testable widget that keeps books on every pointer event it is given, and on whose pointer it was. */
	UDreamPointerLedger* MakeLedger(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize,
		const FVector2D& InPosition, UDreamWidget*& OutWidget)
	{
		OutWidget = InRig.MakeWidget(InName, InParent, InSize, InPosition);
		return IsValid(OutWidget) ? OutWidget->AddComponent<UDreamPointerLedger>() : nullptr;
	}

	/**
	 * Every player's second press on a widget it has just clicked a press of its own, not the second half of a double
	 * click -- which Slate, and the pointer module after it, sends as a double click instead of a down.
	 */
	void NoDoubleClicks(FDreamDriverRig& InRig)
	{
		for (int32 PlayerIndex = 0; PlayerIndex < InRig.GetPlayerCount(); ++PlayerIndex)
		{
			if (UDreamEventSystem* EventSystem = InRig.EventSystem(PlayerIndex))
			{
				EventSystem->SetDoubleClickTime(0.0f);
			}
		}
	}

	/** Whether InKey reached player InPlayerIndex's controller: UPlayerInput keeps a key state for every key it is sent. */
	bool ControllerSawKey(const FDreamDriverRig& InRig, int32 InPlayerIndex, const FKey& InKey)
	{
		const APlayerController* Controller = InRig.GetPlayerController(InPlayerIndex);
		return Controller != nullptr && Controller->PlayerInput != nullptr && Controller->PlayerInput->GetKeyState(InKey) != nullptr;
	}

	/** Whether InWidget is what player InPlayerIndex's mouse is over now: the element query, asked of that player's driver. */
	bool IsHoveredBy(const FDreamDriverRig& InRig, int32 InPlayerIndex, UDreamWidget* InWidget)
	{
		return InWidget != nullptr && InRig.Driver(InPlayerIndex)->Find(FDreamBy::Widget(InWidget))->IsHovered();
	}
}

/**
 * Two players on one screen, each with a mouse: each pointer is over, presses and clicks what it is over, and the
 * events say whose they were -- UDreamUIInputUser keeps every player's pointers apart, and a raycaster answers only its
 * own player's (UDreamUIInputUser::LineTrace). Under the actor hosts each player's button goes through its own
 * controller, which is the second player's controller being a real one. The two players' steps interleave in one
 * sequence through AsPlayer, as two hands on two mice do.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPlayersSharedScreenClickTest,
	"DreamGUI.Driver.Players.TwoPlayersOnOneScreenEachClickWhatTheirOwnPointerIsOverUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPlayersSharedScreenClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPlayersTestLocal;
	for (const FHostCase& Case : PlayerHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayers(Case.Host, EDreamRigPlayerScreens::Shared));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		TestEqual(Under(Case, TEXT("The rig says it has two players")), Rig.GetPlayerCount(), 2);
		NoDoubleClicks(Rig);
		TestEqual(Under(Case, TEXT("The second player's event system speaks for player 1")),
			Rig.EventSystem(1) != nullptr ? Rig.EventSystem(1)->GetUserIndex() : INDEX_NONE, 1);
		TestTrue(Under(Case, TEXT("On a shared screen both players point at the rig's own root")), Rig.Root(1) == Rig.Root());
		const bool bActorHost = IsActorInputHost(Case.Host);
		if (bActorHost)
		{
			TestNotNull(*Under(Case, TEXT("The second player is a real local player")), Rig.GetLocalPlayer(1));
			TestTrue(Under(Case, TEXT("...with a controller of its own")),
				Rig.GetPlayerController(1) != nullptr && Rig.GetPlayerController(1) != Rig.GetPlayerController());
		}
		else
		{
			TestNull(*Under(Case, TEXT("Under the module the second player has no controller: a script player")), Rig.GetPlayerController(1));
		}

		UDreamWidget* West = nullptr;
		UDreamWidget* East = nullptr;
		UDreamPointerLedger* WestLedger = MakeLedger(Rig, TEXT("West"), nullptr, TargetSize, FVector2D(-300.0, 0.0), West);
		UDreamPointerLedger* EastLedger = MakeLedger(Rig, TEXT("East"), nullptr, TargetSize, FVector2D(300.0, 0.0), East);
		if (!TestTrue(Under(Case, TEXT("Two widgets that keep books")), WestLedger != nullptr && EastLedger != nullptr))
		{
			continue;
		}
		Rig.PumpFrames(1);

		// Each player's driver clicks as that player.
		TestTrue(Under(Case, TEXT("Player 0 clicks the west widget")), Rig.Driver()->Find(FDreamBy::Widget(West))->Click());
		TestTrue(Under(Case, TEXT("Player 1 clicks the east widget")), Rig.Driver(1)->Find(FDreamBy::Widget(East))->Click());
		TestEqual(Under(Case, TEXT("The west widget was clicked once")), WestLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...by player 0")), WestLedger->LastUserIndex, 0);
		TestEqual(Under(Case, TEXT("The east widget was clicked once")), EastLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...by player 1")), EastLedger->LastUserIndex, 1);
		if (bActorHost)
		{
			TestTrue(Under(Case, TEXT("Player 1's button went through player 1's controller")), ControllerSawKey(Rig, 1, EKeys::LeftMouseButton));
			TestTrue(Under(Case, TEXT("...and player 0's through player 0's")), ControllerSawKey(Rig, 0, EKeys::LeftMouseButton));
		}

		// Both at once, interleaved in one sequence: player 0 holds the west widget while player 1 hovers, presses and
		// releases the east one, and only then lets go.
		TestTrue(Under(Case, TEXT("The two players' interleaved steps complete")), Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(West))
			.AsPlayer(1).MoveTo(FDreamBy::Widget(East))
			.AsPlayer(0).Press()
			.Then([this, &Case, &Rig, West, East, WestLedger, EastLedger](FDreamDriverContext&)
			{
				TestTrue(Under(Case, TEXT("Player 0's pointer is over the west widget")), IsHoveredBy(Rig, 0, West));
				TestTrue(Under(Case, TEXT("Player 1's is over the east one at the same time")), IsHoveredBy(Rig, 1, East));
				TestEqual(Under(Case, TEXT("Player 0's press went down on the west widget")), WestLedger->Down, 2);
				TestEqual(Under(Case, TEXT("...and nothing went down on the east one")), EastLedger->Down, 1);
			})
			.AsPlayer(1).Press().Release()
			.Then([this, &Case, WestLedger, EastLedger](FDreamDriverContext&)
			{
				TestEqual(Under(Case, TEXT("Player 1 clicked the east widget while player 0 held the west one")), EastLedger->Click, 2);
				TestEqual(Under(Case, TEXT("...and the west one is still held, unclicked")), WestLedger->Click, 1);
			})
			.AsPlayer(0).Release()
			.Perform());
		TestEqual(Under(Case, TEXT("Player 0 let go over the west widget, which is its second click")), WestLedger->Click, 2);
		TestEqual(Under(Case, TEXT("...by player 0")), WestLedger->LastUserIndex, 0);
		TestEqual(Under(Case, TEXT("The east widget's last click was player 1's")), EastLedger->LastUserIndex, 1);
	}
	return true;
}

/**
 * A second player's keys reach the widget that player focused and no other, through the road its host gives keys:
 * DreamUIKeyRouting::RouteKey for that player under ModuleOnly, its own controller's input stack and its own input
 * actor under the actor hosts. The focus is each player's own (UDreamUIInputUser::GetFocusedWidget), so two players
 * focusing two widgets each have one, and a key is one player's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPlayersKeysTest,
	"DreamGUI.Driver.Players.ASecondPlayersKeysReachOnlyTheWidgetThatPlayerFocusedUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPlayersKeysTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPlayersTestLocal;
	for (const FHostCase& Case : PlayerHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayers(Case.Host, EDreamRigPlayerScreens::Shared));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		// The controller a game would have for player 0; the actor hosts have one for each player already.
		Rig.EnsureGameInputHost();
		UDreamWidget* First = Rig.MakeWidget(TEXT("FirstPlayersWidget"), nullptr, TargetSize, FVector2D(-300.0, 0.0));
		UDreamWidget* Second = Rig.MakeWidget(TEXT("SecondPlayersWidget"), nullptr, TargetSize, FVector2D(300.0, 0.0));
		UDreamKeyRecordingBehaviour* FirstRecorder = IsValid(First) ? First->AddComponent<UDreamKeyRecordingBehaviour>() : nullptr;
		UDreamKeyRecordingBehaviour* SecondRecorder = IsValid(Second) ? Second->AddComponent<UDreamKeyRecordingBehaviour>() : nullptr;
		if (!TestTrue(Under(Case, TEXT("Two widgets that record keys")), FirstRecorder != nullptr && SecondRecorder != nullptr
			&& Rig.EventSystem(0) != nullptr && Rig.EventSystem(1) != nullptr))
		{
			continue;
		}
		Rig.PumpFrames(1);
		// Each player focuses its own, the way MakeFocusedRecorder focuses one for player 0 in the key tests.
		Rig.EventSystem(0)->SetSelectComponentWithDefault(First);
		Rig.EventSystem(1)->SetSelectComponentWithDefault(Second);
		TestTrue(Under(Case, TEXT("Player 0 has the first widget focused")), Rig.EventSystem(0)->GetCurrentSelectedComponent(0) == First);
		TestTrue(Under(Case, TEXT("Player 1 has the second")), Rig.EventSystem(1)->GetCurrentSelectedComponent(0) == Second);

		TestTrue(Under(Case, TEXT("Player 1's keystroke completes")), Rig.Driver(1)->Sequence().Key(EKeys::F9).Perform());
		TestEqual(Under(Case, TEXT("Player 1's key reached the widget player 1 focused")), SecondRecorder->KeyDownCount, 1);
		TestTrue(Under(Case, TEXT("...as the key it is")), SecondRecorder->LastKey == EKeys::F9);
		TestEqual(Under(Case, TEXT("...and its release")), SecondRecorder->KeyUpCount, 1);
		TestEqual(Under(Case, TEXT("Player 0's widget heard nothing of it")), FirstRecorder->KeyDownCount, 0);
		if (IsActorInputHost(Case.Host))
		{
			TestTrue(Under(Case, TEXT("The key went through player 1's controller")), ControllerSawKey(Rig, 1, EKeys::F9));
			TestFalse(Under(Case, TEXT("...and never reached player 0's")), ControllerSawKey(Rig, 0, EKeys::F9));
		}

		// Player 0's keystroke, then player 1's again, in one sequence.
		TestTrue(Under(Case, TEXT("Both players' keystrokes in one sequence complete")), Rig.Driver()->Sequence()
			.Key(EKeys::F8)
			.AsPlayer(1).Key(EKeys::F7)
			.Perform());
		TestEqual(Under(Case, TEXT("Player 0's key reached the widget player 0 focused")), FirstRecorder->KeyDownCount, 1);
		TestTrue(Under(Case, TEXT("...as the key player 0 pressed")), FirstRecorder->LastKey == EKeys::F8);
		TestEqual(Under(Case, TEXT("Player 1's second key reached its widget too")), SecondRecorder->KeyDownCount, 2);
		TestTrue(Under(Case, TEXT("...as the key player 1 pressed")), SecondRecorder->LastKey == EKeys::F7);
		if (IsActorInputHost(Case.Host))
		{
			TestFalse(Under(Case, TEXT("Player 1's controller never saw player 0's key")), ControllerSawKey(Rig, 1, EKeys::F8));
		}
	}
	return true;
}

/**
 * A two-player split screen, as the engine lays one out: each local player its part of the viewport (UGameViewportClient::
 * LayoutPlayers), and a screen of its own.
 *
 * The halves are where a world-space pointer deprojects (UDreamWorldSpaceRaycaster::GenerateRay goes through the
 * pointer's own local player's view): each player looks at a panel of its own through its own camera, and a click in
 * the player's part of the viewport lands on what that player sees -- while a click the same player makes in the other
 * player's part is outside its view and lands on nothing. The screens are each player's part too (the rig
 * gives each screen its player, see FDreamDriverRig): the same place on both players' screens is a pixel in each
 * player's own part, and each player's click there is on its own screen's widget.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPlayersSplitScreenTest,
	"DreamGUI.Driver.Players.OnASplitScreenEachPlayersClickLandsOnWhatThatPlayerSeesInItsOwnPartOfTheViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPlayersSplitScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPlayersTestLocal;
	for (const FHostCase& Case : ActorHosts)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayers(Case.Host, EDreamRigPlayerScreens::Split));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}

		NoDoubleClicks(Rig);

		// The layout: two parts, disjoint, together the whole viewport -- whichever way the table splits it.
		const FBox2D FirstPart = Rig.GetPlayerViewRect(0);
		const FBox2D SecondPart = Rig.GetPlayerViewRect(1);
		const double ViewportArea = static_cast<double>(ViewportSize.X) * ViewportSize.Y;
		TestTrue(Under(Case, TEXT("Each player sees a part of the viewport")), FirstPart.bIsValid && SecondPart.bIsValid);
		TestEqual(Under(Case, TEXT("Player 0's part is half of it")), FirstPart.GetArea(), ViewportArea * 0.5, 1.0);
		TestEqual(Under(Case, TEXT("Player 1's part is the other half")), SecondPart.GetArea(), ViewportArea * 0.5, 1.0);
		TestFalse(Under(Case, TEXT("The two parts do not overlap")), FirstPart.Overlap(SecondPart).GetArea() > 0.0);
		const ULocalPlayer* SecondLocalPlayer = Rig.GetLocalPlayer(1);
		TestTrue(Under(Case, TEXT("Player 1's local player holds the part it was given, as LayoutPlayers writes it")),
			SecondLocalPlayer != nullptr && !SecondLocalPlayer->Size.Equals(FVector2D(1.0, 1.0)));
		TestTrue(Under(Case, TEXT("Each player has a screen of its own")), Rig.Root(1) != nullptr && Rig.Root(1) != Rig.Root());

		// The screens: the same place on each player's screen, a widget of each player's own -- inside the part, which is
		// half the viewport's height (360 of 720), so 100 up from the screen's middle and not 200.
		UDreamWidget* FirstScreenWidget = nullptr;
		UDreamWidget* SecondScreenWidget = nullptr;
		const FVector2D ScreenPosition(-400.0, 100.0);
		UDreamPointerLedger* FirstScreenLedger = MakeLedger(Rig, TEXT("FirstScreenWidget"), Rig.Root(0), TargetSize, ScreenPosition, FirstScreenWidget);
		UDreamPointerLedger* SecondScreenLedger = MakeLedger(Rig, TEXT("SecondScreenWidget"), Rig.Root(1), TargetSize, ScreenPosition, SecondScreenWidget);
		if (!TestTrue(Under(Case, TEXT("A widget on each player's screen")), FirstScreenLedger != nullptr && SecondScreenLedger != nullptr))
		{
			continue;
		}

		// The world: each player's camera in front of a panel of its own, far apart, each camera's view the shape of the
		// player's part so the panel is seen undistorted. Attached before any panel exists, as AttachWorldPointer asks.
		const FVector FirstEye(-500.0, 0.0, 0.0);
		const FVector SecondEye(-500.0, 10000.0, 0.0);
		const FIntPoint FirstPartSize(FMath::RoundToInt32(FirstPart.GetSize().X), FMath::RoundToInt32(FirstPart.GetSize().Y));
		const FIntPoint SecondPartSize(FMath::RoundToInt32(SecondPart.GetSize().X), FMath::RoundToInt32(SecondPart.GetSize().Y));
		UDreamDriverWorldSpaceRaycaster* FirstPointer = DreamDriverWorld::AttachWorldPointer(Rig, 0,
			DreamDriverWorld::MakeView(FirstEye, FRotator::ZeroRotator, 60.0f, FirstPartSize), EDreamWorldPointerSource::Mouse);
		UDreamDriverWorldSpaceRaycaster* SecondPointer = DreamDriverWorld::AttachWorldPointer(Rig, 1,
			DreamDriverWorld::MakeView(SecondEye, FRotator::ZeroRotator, 60.0f, SecondPartSize), EDreamWorldPointerSource::Mouse);
		if (!TestTrue(Under(Case, TEXT("A world pointer for each player")), FirstPointer != nullptr && SecondPointer != nullptr))
		{
			continue;
		}
		TestEqual(Under(Case, TEXT("Player 1's world pointer answers player 1")), SecondPointer->GetUserIndex(), 1);
		UDreamWidget* FirstPanel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("FirstPanel"), FTransform(FVector(0.0, 0.0, 0.0)), FVector2D(200.0, 150.0));
		UDreamWidget* SecondPanel = DreamDriverWorld::MakeWorldPanel(Rig, TEXT("SecondPanel"), FTransform(FVector(0.0, 10000.0, 0.0)), FVector2D(200.0, 150.0));
		UDreamWidget* FirstTarget = nullptr;
		UDreamWidget* SecondTarget = nullptr;
		UDreamPointerLedger* FirstTargetLedger = MakeLedger(Rig, TEXT("FirstTarget"), FirstPanel, FVector2D(100.0, 60.0), FVector2D::ZeroVector, FirstTarget);
		UDreamPointerLedger* SecondTargetLedger = MakeLedger(Rig, TEXT("SecondTarget"), SecondPanel, FVector2D(100.0, 60.0), FVector2D::ZeroVector, SecondTarget);
		if (!TestTrue(Under(Case, TEXT("A panel in front of each player, a target on each")), FirstTargetLedger != nullptr && SecondTargetLedger != nullptr))
		{
			continue;
		}
		Rig.PumpFrames(2);

		// Each player aims through its own camera, so the pixel the driver works out for its target is in its own part.
		const TOptional<FVector2D> FirstTargetPixel = Rig.Driver(0)->Find(FDreamBy::Widget(FirstTarget))->GetCentrePixel();
		const TOptional<FVector2D> SecondTargetPixel = Rig.Driver(1)->Find(FDreamBy::Widget(SecondTarget))->GetCentrePixel();
		if (!TestTrue(Under(Case, TEXT("Each target projects to a pixel through its player's camera")),
			FirstTargetPixel.IsSet() && SecondTargetPixel.IsSet()))
		{
			continue;
		}
		TestTrue(Under(Case, TEXT("Player 0's target is in player 0's part of the viewport")), FirstPart.IsInside(FirstTargetPixel.GetValue()));
		TestTrue(Under(Case, TEXT("Player 1's target is in player 1's part")), SecondPart.IsInside(SecondTargetPixel.GetValue()));

		TestTrue(Under(Case, TEXT("Player 0 clicks what it sees")), Rig.Driver(0)->Find(FDreamBy::Widget(FirstTarget))->Click());
		TestTrue(Under(Case, TEXT("Player 1 clicks what it sees")), Rig.Driver(1)->Find(FDreamBy::Widget(SecondTarget))->Click());
		TestEqual(Under(Case, TEXT("Player 0's click landed on player 0's panel")), FirstTargetLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...as player 0")), FirstTargetLedger->LastUserIndex, 0);
		TestEqual(Under(Case, TEXT("Player 1's click landed on player 1's panel")), SecondTargetLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...as player 1")), SecondTargetLedger->LastUserIndex, 1);

		// Player 1 clicks where player 0's target is drawn: that pixel is in player 0's part, outside player 1's view,
		// and its ray from player 1's camera reaches neither panel.
		TestTrue(Under(Case, TEXT("Player 1's click in player 0's part completes")), Rig.Driver(1)->Sequence()
			.MoveToPixel(FirstTargetPixel.GetValue())
			.Press()
			.Release()
			.Perform());
		TestEqual(Under(Case, TEXT("A click player 1 makes in player 0's part does not land on player 0's panel")), FirstTargetLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...nor on player 1's own, which player 1 does not see there")), SecondTargetLedger->Click, 1);

		// The screens, last, at the same pixel for both: each player's click is its own screen's.
		TestTrue(Under(Case, TEXT("Player 1 clicks the widget on its screen")), Rig.Driver(1)->Find(FDreamBy::Widget(SecondScreenWidget))->Click());
		TestEqual(Under(Case, TEXT("Player 1's click is on player 1's screen")), SecondScreenLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...not on player 0's, at the same place on the other screen")), FirstScreenLedger->Click, 0);
		TestTrue(Under(Case, TEXT("Player 0 clicks the widget on its screen")), Rig.Driver(0)->Find(FDreamBy::Widget(FirstScreenWidget))->Click());
		TestEqual(Under(Case, TEXT("Player 0's click is on player 0's screen")), FirstScreenLedger->Click, 1);
		TestEqual(Under(Case, TEXT("...as player 0")), FirstScreenLedger->LastUserIndex, 0);
		TestEqual(Under(Case, TEXT("Player 1's screen widget was not clicked again")), SecondScreenLedger->Click, 1);
	}
	return true;
}

/**
 * What the rig cannot build it refuses, saying why, and builds nothing: a rig of no players or of more than the engine
 * splits a screen for, several players under the Slate source, a split screen with no local players to lay out. And a
 * step sent as a player the rig has not got fails, naming the player, rather than going in as player 0.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPlayersRefusalTest,
	"DreamGUI.Driver.Players.ARigRefusesThePlayersItCannotBuildAndAStepForAPlayerItHasNotGotFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPlayersRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPlayersTestLocal;
	struct FRefusal
	{
		const TCHAR* What;
		FDreamRigOptions Options;
		const TCHAR* Because;
	};
	FDreamRigOptions NoPlayers;
	NoPlayers.PlayerCount = 0;
	FDreamRigOptions FivePlayers;
	FivePlayers.PlayerCount = 5;
	const FRefusal Refusals[] = {
		{ TEXT("No players"), NoPlayers, TEXT("one to") },
		{ TEXT("Five players"), FivePlayers, TEXT("one to") },
		{ TEXT("Two players under the Slate source"), TwoPlayers(EDreamRigInputHost::SlateSource, EDreamRigPlayerScreens::Shared), TEXT("Slate input source") },
		{ TEXT("A split screen under the module"), TwoPlayers(EDreamRigInputHost::ModuleOnly, EDreamRigPlayerScreens::Split), TEXT("split screen") },
	};
	for (const FRefusal& Refusal : Refusals)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(Refusal.Options);
		Rig.BindTest(this);
		TestFalse(FString::Printf(TEXT("%s: the rig is refused"), Refusal.What), Rig.IsUsable());
		TestTrue(FString::Printf(TEXT("%s: and says why (it said: %s)"), Refusal.What, *Rig.GetBuildFailure()),
			Rig.GetBuildFailure().Contains(Refusal.Because));
		TestNull(*FString::Printf(TEXT("%s: and built no world"), Refusal.What), Rig.GetWorld());
	}

	// A player the rig has not got: the step fails and says so, and nothing is sent as player 0 instead.
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayers(EDreamRigInputHost::ModuleOnly, EDreamRigPlayerScreens::Shared));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("A two-player rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Target = nullptr;
	UDreamPointerLedger* Ledger = MakeLedger(Rig, TEXT("Target"), nullptr, TargetSize, FVector2D::ZeroVector, Target);
	if (!TestNotNull(TEXT("A widget that keeps books"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(1);
	AddExpectedErrorPlain(TEXT("no player 3"));
	TestFalse(TEXT("A click sent as player 3 of a two-player rig fails"),
		Rig.Driver()->Sequence().AsPlayer(3).Click(FDreamBy::Widget(Target)).Perform());
	TestEqual(TEXT("...and nothing reached the widget as anybody else"), Ledger->Down, 0);
	TestEqual(TEXT("...not even a hover"), Ledger->Enter, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
