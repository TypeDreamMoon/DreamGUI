// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "UnrealClient.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamInputPipelineTestTypes.h"
#include "RHI/DreamPixelProbe.h"

/*
 * A SPLIT SCREEN, AS UMG LAYS ONE OUT.
 *
 * UUserWidget::AddToPlayerScreen (UMG/Private/UserWidget.cpp) puts a widget on its owning player's layer, and
 * SGameLayerManager::AddOrUpdatePlayerLayers (Engine/Private/Slate/SGameLayerManager.cpp) lays every player's layer out
 * over the part of the viewport the split-screen layout gives that player -- ULocalPlayer::Origin and Size, as
 * UGameViewportClient::LayoutPlayers writes them -- and FindOrCreatePlayerLayer clips the layer to it. So a player's UI
 * is laid out in that player's part, a pointer is measured from the part's corner, nothing of it is hit past the part,
 * and it is drawn in that player's view alone.
 *
 * A DreamGUI screen-space canvas does the same once it is given its player (UDreamCanvas::SetViewportPlayerIndex), as
 * UDreamScreenUISubsystem gives every screen it makes for a local player. The rig's split screen builds a root canvas
 * per player and gives each its player, as the screen UI would; each test gives them their players again, which
 * changes nothing and keeps the test's premise in sight.
 *
 * Two players on a 1280 by 720 viewport, laid out by the engine's default two-player table -- one above the other.
 */
namespace DreamSplitScreenInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D TargetSize(200.0, 100.0);

	FDreamRigOptions TwoPlayersSplit()
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = EDreamRigInputHost::StandaloneActor;
		Options.PlayerCount = 2;
		Options.PlayerScreens = EDreamRigPlayerScreens::Split;
		return Options;
	}

	/** The rig-came-up claim, carrying the rig's own reason when it did not. */
	FString RigCameUp(const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? FString(TEXT("The split-screen rig came up"))
			: FString::Printf(TEXT("The split-screen rig came up -- it did not: %s"), *WhyNot);
	}

	/** Each player's screen given to its player, as the screen UI gives every screen it makes for a local player. */
	void GiveEachScreenItsPlayer(FDreamDriverRig& InRig)
	{
		for (int32 PlayerIndex = 0; PlayerIndex < InRig.GetPlayerCount(); ++PlayerIndex)
		{
			if (UDreamCanvas* Canvas = InRig.RootCanvas(PlayerIndex))
			{
				Canvas->SetViewportPlayerIndex(PlayerIndex);
			}
		}
		InRig.PumpFrames(1);
	}

	/** The rig's view rect of a player, as the whole pixels the canvas's truncations give. */
	FIntRect PartOf(const FDreamDriverRig& InRig, int32 InPlayerIndex)
	{
		const FBox2D Part = InRig.GetPlayerViewRect(InPlayerIndex);
		return FIntRect(
			FIntPoint(FMath::RoundToInt32(Part.Min.X), FMath::RoundToInt32(Part.Min.Y)),
			FIntPoint(FMath::RoundToInt32(Part.Max.X), FMath::RoundToInt32(Part.Max.Y)));
	}

	FVector2D CentreOf(const FIntRect& InRect)
	{
		return FVector2D(InRect.Min.X + InRect.Width() * 0.5, InRect.Min.Y + InRect.Height() * 0.5);
	}

	FString Describe(const FIntRect& InRect)
	{
		return FString::Printf(TEXT("(%d, %d)-(%d, %d)"), InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y);
	}

	/** A hit-testable widget that keeps books on every pointer event it is told, and on whose pointer it was. */
	UDreamPointerLedger* MakeLedger(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize,
		const FVector2D& InPosition, UDreamWidget*& OutWidget)
	{
		OutWidget = InRig.MakeWidget(InName, InParent, InSize, InPosition);
		return IsValid(OutWidget) ? OutWidget->AddComponent<UDreamPointerLedger>() : nullptr;
	}

	/** A ledger widget that fills its screen: anchored to every edge, no size of its own beyond the screen's. */
	UDreamPointerLedger* MakeFillingLedger(FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, UDreamWidget*& OutWidget)
	{
		UDreamPointerLedger* Ledger = MakeLedger(InRig, InName, InParent, TargetSize, FVector2D::ZeroVector, OutWidget);
		if (Ledger != nullptr)
		{
			OutWidget->SetAnchorMin(FVector2D(0.0, 0.0));
			OutWidget->SetAnchorMax(FVector2D(1.0, 1.0));
			OutWidget->SetAnchoredPosition(FVector2D::ZeroVector);
			OutWidget->SetSizeDelta(FVector2D::ZeroVector);
		}
		return Ledger;
	}

	/** Every player's second press a press of its own, never the second half of a double click. */
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

	/** Player InPlayerIndex's mouse moved to InPixel, pressed and let go there. */
	bool ClickAsPlayer(FDreamDriverRig& InRig, int32 InPlayerIndex, const FVector2D& InPixel)
	{
		return InRig.Driver(InPlayerIndex)->Sequence().MoveToPixel(InPixel).Press().Release().Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenLayoutTest,
	"DreamGUI.Screen.OnASplitScreenEachPlayersScreenIsLaidOutOverThatPlayersPartOfTheViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * SGameLayerManager::AddOrUpdatePlayerLayers sizes and places a player's layer from the player's normalized rect
 * (GetNormalizeRect, over ULocalPlayer::GetProjectionData's truncations of Origin and Size). Given its player, each
 * screen canvas is that part: its rect, its size, the size of the root it lays out, and the corner a viewport pixel is
 * measured from. The rig gives each screen its player as it builds it, as the screen UI does. Given none again, a screen
 * is the shared layer: the whole viewport.
 */
bool FDreamSplitScreenLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit());
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Rig), Rig.IsUsable()))
	{
		return false;
	}
	const FIntRect Whole(FIntPoint::ZeroValue, ViewportSize);
	for (int32 PlayerIndex = 0; PlayerIndex < 2; ++PlayerIndex)
	{
		const UDreamCanvas* Canvas = Rig.RootCanvas(PlayerIndex);
		if (!TestNotNull(*FString::Printf(TEXT("Player %d has a screen canvas"), PlayerIndex), Canvas))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("The rig gave player %d's screen its player, as the screen UI gives a local player's screen"), PlayerIndex),
			Canvas->GetViewportPlayerIndex(), PlayerIndex);
	}

	GiveEachScreenItsPlayer(Rig);
	for (int32 PlayerIndex = 0; PlayerIndex < 2; ++PlayerIndex)
	{
		UDreamCanvas* Canvas = Rig.RootCanvas(PlayerIndex);
		const UDreamWidget* Root = Rig.Root(PlayerIndex);
		const FIntRect Part = PartOf(Rig, PlayerIndex);
		TestTrue(FString::Printf(TEXT("Player %d's part is a proper part of the viewport %s"), PlayerIndex, *Describe(Part)),
			Part.Area() > 0 && Part != Whole);
		TestEqual(FString::Printf(TEXT("Player %d's screen says whose it is"), PlayerIndex), Canvas->GetViewportPlayerIndex(), PlayerIndex);
		TestTrue(FString::Printf(TEXT("Player %d's screen fills a part of the viewport"), PlayerIndex), Canvas->FillsPartOfViewport());
		TestEqual(FString::Printf(TEXT("Player %d's screen is that player's part"), PlayerIndex), Describe(Canvas->GetViewportRect()), Describe(Part));
		TestEqual(TEXT("...the size it lays out at is the part's"), Canvas->GetViewportSize().ToString(), Part.Size().ToString());
		TestEqual(TEXT("...and its root is the part's width"), Root->GetWidth(), static_cast<float>(Part.Width()), 0.5f);
		TestEqual(TEXT("...and the part's height"), Root->GetHeight(), static_cast<float>(Part.Height()), 0.5f);

		// The part's top-left pixel is the screen's top-left corner: canvas space has its zero at the bottom-left.
		FVector2D CornerOnCanvas = FVector2D::ZeroVector;
		TestTrue(FString::Printf(TEXT("Player %d's part's corner converts onto the screen"), PlayerIndex),
			Canvas->ConvertPositionFromViewportToCanvas(FVector2D(Part.Min), CornerOnCanvas));
		TestTrue(FString::Printf(TEXT("...as the screen's top-left corner (it came to %s)"), *CornerOnCanvas.ToString()),
			CornerOnCanvas.Equals(FVector2D(0.0, Part.Height()), 0.5));
		FVector2D BackOnViewport = FVector2D::ZeroVector;
		TestTrue(FString::Printf(TEXT("Player %d's screen's bottom-left converts back onto the viewport"), PlayerIndex),
			Canvas->ConvertPositionFromCanvasToViewport(FVector2D::ZeroVector, BackOnViewport));
		TestTrue(FString::Printf(TEXT("...at the part's bottom-left pixel (it came to %s)"), *BackOnViewport.ToString()),
			BackOnViewport.Equals(FVector2D(Part.Min.X, Part.Max.Y), 0.5));
	}

	// Given no player again, a screen is the shared layer once more.
	Rig.RootCanvas(1)->SetViewportPlayerIndex(INDEX_NONE);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A screen given no player is the whole viewport again"), Describe(Rig.RootCanvas(1)->GetViewportRect()), Describe(Whole));
	TestEqual(TEXT("...and lays out over all of it"), Rig.Root(1)->GetHeight(), static_cast<float>(ViewportSize.Y), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenOwnPartClickTest,
	"DreamGUI.Screen.OnASplitScreenEachPlayersClickInTheMiddleOfItsPartLandsOnTheWidgetInTheMiddleOfItsScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget centred on a player's layer sits in the middle of that player's part (SGameLayerManager positions the layer at
 * the part), and the player's pointer there clicks it -- each player's own, through its own controller, stamped with its
 * own user index. On a screen the size of the whole viewport the same widget sat on the line between the two parts.
 */
bool FDreamSplitScreenOwnPartClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit());
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Rig), Rig.IsUsable()))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	GiveEachScreenItsPlayer(Rig);
	UDreamWidget* FirstWidget = nullptr;
	UDreamWidget* SecondWidget = nullptr;
	UDreamPointerLedger* FirstLedger = MakeLedger(Rig, TEXT("FirstPlayersMiddle"), Rig.Root(0), TargetSize, FVector2D::ZeroVector, FirstWidget);
	UDreamPointerLedger* SecondLedger = MakeLedger(Rig, TEXT("SecondPlayersMiddle"), Rig.Root(1), TargetSize, FVector2D::ZeroVector, SecondWidget);
	if (!TestTrue(TEXT("A widget in the middle of each player's screen"), FirstLedger != nullptr && SecondLedger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("Player 0 clicks the middle of its part"), ClickAsPlayer(Rig, 0, CentreOf(PartOf(Rig, 0))));
	TestEqual(TEXT("Player 0's click landed on the widget in the middle of player 0's screen"), FirstLedger->Click, 1);
	TestEqual(TEXT("...as player 0"), FirstLedger->LastUserIndex, 0);
	TestEqual(TEXT("...and nothing of player 1's screen heard it"), SecondLedger->Down, 0);

	TestTrue(TEXT("Player 1 clicks the middle of its part"), ClickAsPlayer(Rig, 1, CentreOf(PartOf(Rig, 1))));
	TestEqual(TEXT("Player 1's click landed on the widget in the middle of player 1's screen"), SecondLedger->Click, 1);
	TestEqual(TEXT("...as player 1"), SecondLedger->LastUserIndex, 1);
	TestEqual(TEXT("...and player 0's widget was not clicked again"), FirstLedger->Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenOtherPartReachesNothingTest,
	"DreamGUI.Screen.OnASplitScreenAPointerInAnotherPlayersPartReachesNothingOnItsOwnPlayersScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * FindOrCreatePlayerLayer gives a player's layer ClipToBoundsAlways, and Slate hit-tests only what is inside a widget's
 * clipped geometry: past a player's part there is nothing of that player's UI to hit. Each player's screen here is
 * covered edge to edge by one widget, so a screen the size of the whole viewport would take a press anywhere; the one
 * player 1 makes in player 0's part reaches neither player's widget, nor does player 0's in player 1's, and neither
 * pointer even hovers anything there. Back in its own part, each pointer is over its own widget again.
 */
bool FDreamSplitScreenOtherPartReachesNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit());
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Rig), Rig.IsUsable()))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	GiveEachScreenItsPlayer(Rig);
	UDreamWidget* FirstWidget = nullptr;
	UDreamWidget* SecondWidget = nullptr;
	UDreamPointerLedger* FirstLedger = MakeFillingLedger(Rig, TEXT("FirstPlayersScreenFill"), Rig.Root(0), FirstWidget);
	UDreamPointerLedger* SecondLedger = MakeFillingLedger(Rig, TEXT("SecondPlayersScreenFill"), Rig.Root(1), SecondWidget);
	if (!TestTrue(TEXT("A widget covering each player's screen"), FirstLedger != nullptr && SecondLedger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FIntRect FirstPart = PartOf(Rig, 0);
	const FIntRect SecondPart = PartOf(Rig, 1);
	TestEqual(TEXT("Player 1's covering widget is as tall as player 1's part"), SecondWidget->GetHeight(), static_cast<float>(SecondPart.Height()), 0.5f);

	TestTrue(TEXT("Player 1 clicks in the middle of player 0's part"), ClickAsPlayer(Rig, 1, CentreOf(FirstPart)));
	TestEqual(TEXT("Player 1's press in player 0's part is not on player 1's screen"), SecondLedger->Down, 0);
	TestEqual(TEXT("...not even as a hover"), SecondLedger->Enter, 0);
	TestEqual(TEXT("...and player 0's screen is not player 1's to press"), FirstLedger->Down, 0);

	TestTrue(TEXT("Player 0 clicks in the middle of player 1's part"), ClickAsPlayer(Rig, 0, CentreOf(SecondPart)));
	TestEqual(TEXT("Player 0's press in player 1's part is not on player 0's screen"), FirstLedger->Down, 0);
	TestEqual(TEXT("...not even as a hover"), FirstLedger->Enter, 0);
	TestEqual(TEXT("...and player 1's screen is not player 0's to press"), SecondLedger->Down, 0);

	TestTrue(TEXT("Each player clicks back in its own part"),
		ClickAsPlayer(Rig, 0, CentreOf(FirstPart)) && ClickAsPlayer(Rig, 1, CentreOf(SecondPart)));
	TestEqual(TEXT("Player 0's click in its own part is on its own screen"), FirstLedger->Click, 1);
	TestEqual(TEXT("...as player 0"), FirstLedger->LastUserIndex, 0);
	TestEqual(TEXT("Player 1's click in its own part is on its own screen"), SecondLedger->Click, 1);
	TestEqual(TEXT("...as player 1"), SecondLedger->LastUserIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenDragLeavesPartTest,
	"DreamGUI.Screen.OnASplitScreenADragThatLeavesThePlayersPartGoesOnFollowingThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A press on a player's layer captures the mouse (SButton, STableRow and the rest answer the press with CaptureMouse), and
 * a captured pointer is routed to the captor wherever it goes -- off the layer included. So a drag begun on player 1's
 * screen goes on being one when the pointer crosses into player 0's part, and ends when it is let go there; nothing of
 * player 0's screen is pressed by it.
 */
bool FDreamSplitScreenDragLeavesPartTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit());
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Rig), Rig.IsUsable()))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	GiveEachScreenItsPlayer(Rig);
	UDreamWidget* FirstWidget = nullptr;
	UDreamWidget* SecondWidget = nullptr;
	UDreamPointerLedger* FirstLedger = MakeFillingLedger(Rig, TEXT("FirstPlayersScreenFill"), Rig.Root(0), FirstWidget);
	UDreamPointerLedger* SecondLedger = MakeLedger(Rig, TEXT("SecondPlayersHandle"), Rig.Root(1), TargetSize, FVector2D::ZeroVector, SecondWidget);
	if (!TestTrue(TEXT("A widget covering player 0's screen and one in the middle of player 1's"), FirstLedger != nullptr && SecondLedger != nullptr)
		|| !TestNotNull(TEXT("Player 1 has a raycaster to read the drag threshold of"), Rig.Raycaster(1)))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FIntRect FirstPart = PartOf(Rig, 0);
	const FIntRect SecondPart = PartOf(Rig, 1);
	const FVector2D Start = CentreOf(SecondPart);
	const double PastThreshold = FMath::Sqrt(static_cast<double>(Rig.Raycaster(1)->GetScaledDragThresholdSquare())) + 2.0;

	TestTrue(TEXT("Player 1 presses in the middle of its part and drags into player 0's"),
		Rig.Driver(1)->Sequence()
			.MoveToPixel(Start)
			.Press()
			.MoveBy(FVector2D(0.0, -PastThreshold))
			.MoveToPixel(FVector2D(Start.X, SecondPart.Min.Y - 10.0))
			.MoveToPixel(CentreOf(FirstPart))
			.WaitFrames(1)
			.Then([this, SecondLedger](FDreamDriverContext&)
			{
				TestEqual(TEXT("The drag began on player 1's widget"), SecondLedger->BeginDrag, 1);
				TestTrue(TEXT("...and went on being dragged once the pointer left player 1's part"), SecondLedger->Drag >= 2);
				TestEqual(TEXT("...without ending there"), SecondLedger->EndDrag, 0);
			})
			.Release()
			.Perform());
	TestEqual(TEXT("Let go in player 0's part, the drag ended once"), SecondLedger->EndDrag, 1);
	TestEqual(TEXT("...as player 1's"), SecondLedger->LastUserIndex, 1);
	TestEqual(TEXT("Nothing of player 0's screen was pressed by player 1's pointer"), FirstLedger->Down, 0);
	TestEqual(TEXT("...nor entered"), FirstLedger->Enter, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenScreenUIRootTest,
	"DreamGUI.Screen.TheScreenTheScreenUIMakesForALocalPlayerOfASplitScreenIsThatPlayersPart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UUserWidget::AddToPlayerScreen hands its widget to the owning local player's layer (UGameViewportSubsystem::
 * AddWidgetForPlayer, then SGameLayerManager::AddWidgetForPlayer); the screen UI's equivalent of a layer is the screen root
 * it makes for a local player. That root is given its player as it is made: on a split screen it fills that player's part.
 *
 * The rig's own two screens are switched off first, so that the screen UI's is the only screen counted for player 1 -- the
 * UI manager allows one screen-space screen per local player -- and the screen UI's root, which has no viewport to read
 * the size of under the rig, is handed the rig's.
 */
bool FDreamSplitScreenScreenUIRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit());
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Rig), Rig.IsUsable()))
	{
		return false;
	}
	UDreamScreenUISubsystem* Screens = UDreamScreenUISubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has a screen UI"), Screens))
	{
		return false;
	}
	Rig.Root(0)->SetWidgetActive(false);
	Rig.Root(1)->SetWidgetActive(false);
	Rig.PumpFrames(1);

	UDreamWidget* PlayerScreen = Screens->GetOrCreateScreenRootForUserIndex(1);
	UDreamCanvas* PlayerCanvas = PlayerScreen != nullptr ? PlayerScreen->GetComponent<UDreamCanvas>() : nullptr;
	if (!TestNotNull(TEXT("The screen UI makes a screen for player 1"), PlayerCanvas))
	{
		return false;
	}
	PlayerCanvas->SetViewportSizeOverride(ViewportSize);
	Rig.PumpFrames(1);

	const FIntRect Part = PartOf(Rig, 1);
	TestEqual(TEXT("The screen the screen UI made for player 1 is player 1's"), PlayerCanvas->GetViewportPlayerIndex(), 1);
	TestTrue(TEXT("...and fills a part of the viewport"), PlayerCanvas->FillsPartOfViewport());
	TestEqual(TEXT("...player 1's part"), Describe(PlayerCanvas->GetViewportRect()), Describe(Part));
	TestEqual(TEXT("...and its root is laid out at that part's height"), PlayerScreen->GetHeight(), static_cast<float>(Part.Height()), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenSinglePlayerUnchangedTest,
	"DreamGUI.Screen.AScreenGivenTheOnlyPlayerOfAGameThatIsNotSplitIsTheWholeViewportAsBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * With one player UGameViewportClient::LayoutPlayers gives the player the whole viewport (SplitscreenInfo[None]: origin
 * 0, size 1), so the player's layer is the viewport and nothing about a screen changes for being that player's. The
 * player's local player is laid out the way LayoutPlayers lays it out, and the screen given that player is still the
 * whole viewport, measured from its top-left corner as before, and a click lands where it always did.
 */
bool FDreamSplitScreenSinglePlayerUnchangedTest::RunTest(const FString& Parameters)
{
	using namespace DreamSplitScreenInteractionTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	ULocalPlayer* OnlyPlayer = Rig.IsUsable() ? Rig.GetLocalPlayer(0) : nullptr;
	if (!TestNotNull(TEXT("The rig came up with a local player"), OnlyPlayer))
	{
		return false;
	}
	OnlyPlayer->Origin = FVector2D(0.0, 0.0);
	OnlyPlayer->Size = FVector2D(1.0, 1.0);
	UDreamWidget* Target = nullptr;
	UDreamPointerLedger* Ledger = MakeLedger(Rig, TEXT("OffCentre"), nullptr, TargetSize, FVector2D(300.0, 200.0), Target);
	if (!TestNotNull(TEXT("A widget away from the middle"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TOptional<FVector2D> Before = Rig.Driver()->Find(FDreamBy::Widget(Target))->GetCentrePixel();

	Rig.RootCanvas()->SetViewportPlayerIndex(0);
	Rig.PumpFrames(1);
	const UDreamCanvas* Canvas = Rig.RootCanvas();
	TestEqual(TEXT("The screen says it is player 0's"), Canvas->GetViewportPlayerIndex(), 0);
	TestFalse(TEXT("...and fills no part of the viewport: the part is all of it"), Canvas->FillsPartOfViewport());
	TestEqual(TEXT("...so it is the whole viewport"), Describe(Canvas->GetViewportRect()), Describe(FIntRect(FIntPoint::ZeroValue, ViewportSize)));
	TestTrue(TEXT("...and every pixel of it is on the screen"), Canvas->ContainsViewportPoint(FVector2D(0.0, 0.0))
		&& Canvas->ContainsViewportPoint(FVector2D(ViewportSize.X - 1.0, ViewportSize.Y - 1.0)));
	const TOptional<FVector2D> After = Rig.Driver()->Find(FDreamBy::Widget(Target))->GetCentrePixel();
	TestTrue(TEXT("The widget is drawn at the same pixel as before"),
		Before.IsSet() && After.IsSet() && Before->Equals(After.GetValue(), 0.5));
	TestTrue(TEXT("A click on it completes"), Rig.Driver()->Find(FDreamBy::Widget(Target))->Click());
	TestEqual(TEXT("...and lands on it once"), Ledger->Click, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenPictureTest,
	"DreamGUI.Screen.RHI.OnASplitScreenAPlayersScreenIsDrawnInThatPlayersPartAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * The picture a split screen leaves: UGameViewportClient draws one view per local player, each into that player's rect, and
 * SGameLayerManager paints a player's layer over that player's part only. A play session is given a second local player
 * (UGameInstance::CreateLocalPlayer, which the viewport client lays out as a split screen from the next frame), the rig's
 * screen is given player 0, and a magenta block goes in the middle of it.
 *
 * In the picture the block is in the middle of player 0's part, its full height, and nowhere in player 1's part. A screen
 * drawn into every player's view, the size of the whole viewport, put it in both parts at half its height.
 *
 * On a real RHI only: under -nullrhi a viewport has no pixels to read.
 */
bool FDreamSplitScreenPictureTest::RunTest(const FString& Parameters)
{
	static const FColor Magenta = FColor(255, 0, 255, 255);
	constexpr float BlockWidth = 240.0f;
	constexpr float BlockHeight = 120.0f;
	TSharedRef<TWeakObjectPtr<ULocalPlayer>> Joined = MakeShared<TWeakObjectPtr<ULocalPlayer>>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, Joined, BlockWidth, BlockHeight](FDreamDriverPieRig& InRig)
	{
		UWorld* World = InRig.GetWorld();
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		if (!TestNotNull(TEXT("The play session has a game instance to join a player to"), GameInstance))
		{
			return;
		}
		FString WhyNot;
		ULocalPlayer* Second = GameInstance->CreateLocalPlayer(1, WhyNot, true);
		if (!TestNotNull(*FString::Printf(TEXT("A second local player joins the play session (%s)"), *WhyNot), Second))
		{
			return;
		}
		*Joined = Second;
		if (UDreamCanvas* Canvas = InRig.RootCanvas())
		{
			Canvas->SetViewportPlayerIndex(0);
		}
		UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), TEXT("FirstPlayersBlock"), nullptr, FVector2D(BlockWidth, BlockHeight));
		if (UDreamTexture* Visual = Block != nullptr ? Cast<UDreamTexture>(Block->GetVisual()) : nullptr)
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(Magenta);
		}
		else
		{
			AddError(TEXT("A magenta block can be made on player 0's screen"));
		}
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	// The layout reaches the players on the frame after the join, the screen follows its player's part on the frame after
	// that, and the canvas batches on a worker while the scene render trails the tick.
	Steps.WaitFrames(8);
	Steps.Then([this, Joined, BlockHeight](FDreamDriverContext& InContext)
	{
		const ULocalPlayer* First = InContext.LocalPlayer;
		const ULocalPlayer* Second = Joined->Get();
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		FViewport* Viewport = UDreamUICaptureLibrary::FindViewportOf(InContext.World);
		if (!TestNotNull(TEXT("The play session's first player is known"), First)
			|| !TestNotNull(TEXT("The second player is still in the session"), Second)
			|| !TestTrue(TEXT("The play session's viewport reads back"), UDreamUICaptureLibrary::ReadViewportPixels(Viewport, Pixels, Size)))
		{
			return;
		}
		const FString File = FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("SplitScreen_FirstPlayersBlock"));
		AddInfo(FString::Printf(TEXT("The split play session's viewport, %dx%d, is in %s."), Size.X, Size.Y, File.IsEmpty() ? TEXT("(not written)") : *File));

		const auto PartOfPlayer = [&Size](const ULocalPlayer& InPlayer)
		{
			const FIntPoint Min(FMath::TruncToInt32(InPlayer.Origin.X * Size.X), FMath::TruncToInt32(InPlayer.Origin.Y * Size.Y));
			const FIntPoint Extent(FMath::TruncToInt32(InPlayer.Size.X * Size.X), FMath::TruncToInt32(InPlayer.Size.Y * Size.Y));
			return FIntRect(Min, Min + Extent);
		};
		const FIntRect FirstPart = PartOfPlayer(*First);
		const FIntRect SecondPart = PartOfPlayer(*Second);
		if (!TestTrue(TEXT("The play session is split: each player has a part of the viewport"),
			FirstPart.Area() > 0 && SecondPart.Area() > 0 && FirstPart.Area() < Size.X * Size.Y))
		{
			return;
		}
		const FIntPoint FirstCentre = FirstPart.Min + FirstPart.Size() / 2;
		const FIntPoint SecondCentre = SecondPart.Min + SecondPart.Size() / 2;
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, Size, FirstCentre, Magenta, 8, TEXT("the middle of player 0's part, where player 0's block is"));
		TestEqual(TEXT("Nothing of player 0's screen is drawn in player 1's part"),
			FDreamPixelProbe::CountColor(Pixels, Size, SecondPart, Magenta, 8), 0);
		TestFalse(TEXT("...the middle of player 1's part is not the block"),
			FDreamPixelProbe::IsNear(Pixels[SecondCentre.Y * Size.X + SecondCentre.X], Magenta, 8));
		// Its full height down the column through its middle: drawn into a view the size of the part, not squeezed into it.
		const int32 Column = FDreamPixelProbe::CountColor(Pixels, Size, FIntRect(FirstCentre.X, FirstPart.Min.Y, FirstCentre.X + 1, FirstPart.Max.Y), Magenta, 8);
		TestTrue(FString::Printf(TEXT("The block is its full %.0f pixels tall in player 0's part (it is %d)"), BlockHeight, Column),
			FMath::Abs(Column - FMath::RoundToInt32(BlockHeight)) <= 4);

		if (UGameInstance* GameInstance = InContext.World != nullptr ? InContext.World->GetGameInstance() : nullptr)
		{
			GameInstance->RemoveLocalPlayer(const_cast<ULocalPlayer*>(Second));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
