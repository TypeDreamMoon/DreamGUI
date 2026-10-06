// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"
#include "DreamUIBPLibrary.h"
#include "Event/DreamEventSystem.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * WIDGETS PUT ON A SCREEN THE WAY A GAME PUTS THEM THERE, AND PLAYERS OF A SPLIT SCREEN EACH USING THEIR OWN.
 *
 * UMG's Create Widget node makes a widget for an owning player, AddToViewport puts it on the viewport and AddToPlayerScreen
 * on its owner's layer (UUserWidget::AddToPlayerScreen, GameViewportSubsystem.cpp AddWidgetForPlayer), and from there it is
 * clicked like any widget. On a split screen each player's layer is laid out over, hit in and drawn in that player's part of
 * the viewport (SGameLayerManager::AddOrUpdatePlayerLayers), each player's keys go to that player's focus (FSlateUser keeps
 * one focus path per user, SlateUser.h), and what Slate puts up for a widget -- its tooltip, a menu it opens -- goes with the
 * user who brought it up (FSlateUser::UpdateTooltip is per user; a menu is a window over its anchor). DreamGUI's equivalent
 * of a player's layer is the screen root the screen UI keeps for that player (UDreamScreenUISubsystem); the rig's split
 * screen hands every player's screen to it (FDreamRigOptions::bScreensFromScreenUI) where the claim needs what the screen UI
 * puts there, and builds its own per player otherwise, as DreamGUI.Screen.* does.
 *
 * A 1280 by 720 viewport; on the split screen two players, one above the other, by the engine's default two-player table.
 */
namespace DreamPlayerScreensTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	FWaitTimeout ShortWait()
	{
		return FWaitTimeout::InSeconds(0.5);
	}

	FDreamRigOptions TwoPlayersSplit(bool bInScreensFromScreenUI)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = EDreamRigInputHost::StandaloneActor;
		Options.PlayerCount = 2;
		Options.PlayerScreens = EDreamRigPlayerScreens::Split;
		Options.bScreensFromScreenUI = bInScreensFromScreenUI;
		return Options;
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		const FString& WhyNot = InRig.GetBuildFailure();
		return InTest.TestTrue(WhyNot.IsEmpty() ? FString(TEXT("The rig came up"))
			: FString::Printf(TEXT("The rig came up -- it did not: %s"), *WhyNot), InRig.IsUsable());
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

	FVector2D CentreOf(const FBox2D& InRect)
	{
		return InRect.GetCenter();
	}

	/** Whether InInner lies inside InOuter, give or take a pixel. */
	bool IsInside(const FBox2D& InInner, const FBox2D& InOuter)
	{
		return InInner.Min.X >= InOuter.Min.X - 1.0 && InInner.Min.Y >= InOuter.Min.Y - 1.0
			&& InInner.Max.X <= InOuter.Max.X + 1.0 && InInner.Max.Y <= InOuter.Max.Y + 1.0;
	}

	FString Describe(const FBox2D& InRect)
	{
		return FString::Printf(TEXT("(%.0f, %.0f)-(%.0f, %.0f)"), InRect.Min.X, InRect.Min.Y, InRect.Max.X, InRect.Max.Y);
	}

	/** The Create Dream Widget node's two calls for InOwner, a button made the way a graph makes one, its events to InListener. */
	UDreamButton* CreateButtonForPlayer(UWorld* InWorld, APlayerController* InOwner, UDreamPressInteractionListener* InListener)
	{
		UDreamUserWidget* Begun = UDreamUIBPLibrary::BeginDeferredCreateDreamWidget(InWorld, UDreamButton::StaticClass(), InOwner);
		UDreamButton* Button = Cast<UDreamButton>(Begun != nullptr ? UDreamUIBPLibrary::FinishDeferredCreateDreamWidget(Begun) : nullptr);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
		}
		return Button;
	}

	/** A page placed by hand, as UMG's SetAlignmentInViewport / SetDesiredSizeInViewport / SetPositionInViewport place one. */
	void PlaceInViewport(UDreamUserWidget* InPage, const FVector2D& InSize, const FVector2D& InPosition)
	{
		InPage->SetAnchorsInViewport(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5));
		InPage->SetDesiredSizeInViewport(InSize);
		InPage->SetPositionInViewport(InPosition);
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCreatedButtonAddedToViewportTest,
	"DreamGUI.Button.MadeByTheCreateDreamWidgetNodeAndAddedToTheViewportItIsClickedWhereItWasPlacedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCreatedButtonAddedToViewportTest, "DreamGUI.Button.MadeByTheCreateDreamWidgetNodeAndAddedToTheViewportItIsClickedWhereItWasPlacedOnce", "[Pointer][Animated]")

/*
 * Made by the Create Dream Widget node's calls for the player, added to the viewport and placed by hand off the middle of
 * the screen: it is on the screen, and a click where it was placed clicks it once, through the player controller; a click
 * in the middle of the screen, where it is not, clicks nothing.
 */
bool FDreamCreatedButtonAddedToViewportTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamScreenUISubsystem* Screens = UDreamScreenUISubsystem::Get(Rig.GetWorld());
	UDreamButton* Button = CreateButtonForPlayer(Rig.GetWorld(), Rig.GetPlayerController(), Listener.Get());
	if (!TestNotNull(TEXT("The world has a screen UI"), Screens) || !TestNotNull(TEXT("The node's calls made a button for the player"), Button))
	{
		return false;
	}
	TestNull(TEXT("Made, it is on no screen yet"), Button->GetParent());

	Button->AddToViewport(0);
	PlaceInViewport(Button, ButtonSize, FVector2D(300.0, 150.0));
	Rig.PumpFrames(2);
	TestTrue(TEXT("Added, it is in the viewport"), Screens->IsInViewport(Button));
	TestTrue(TEXT("...on the player's screen, which is the screen the rig points at"), Button->GetParent() == Rig.Root());

	TestTrue(TEXT("A click in the middle of the screen completes"),
		Rig.Driver()->Sequence().MoveToPixel(FVector2D(ViewportSize.X * 0.5, ViewportSize.Y * 0.5)).Press().Release().Perform());
	TestEqual(TEXT("...and clicks nothing of the button placed off the middle"), Listener->PressedCount, 0);
	TestTrue(TEXT("Clicking the button where it was placed completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("...and clicks it once"), Listener->ClickedCount, 1);

	Screens->RemoveFromViewport(Button);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonOnSecondPlayersScreenTest,
	"DreamGUI.Button.AddedToTheSecondPlayersScreenOfASplitScreenItIsInThatPlayersPartAndOnlyThatPlayersClickReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonOnSecondPlayersScreenTest, "DreamGUI.Button.AddedToTheSecondPlayersScreenOfASplitScreenItIsInThatPlayersPartAndOnlyThatPlayersClickReachesIt", "[Pointer][Animated]")

/*
 * Made for the second player and put on that player's screen with AddToPlayerScreen: it is on the screen the screen UI keeps
 * for that player, drawn inside that player's part of the viewport. The first player's click on its pixel reaches nothing --
 * it is not the first player's layer -- and the second player's clicks it once.
 */
bool FDreamButtonOnSecondPlayersScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit(/*bInScreensFromScreenUI*/ true));
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	UDreamScreenUISubsystem* Screens = UDreamScreenUISubsystem::Get(Rig.GetWorld());
	UDreamButton* Button = CreateButtonForPlayer(Rig.GetWorld(), Rig.GetPlayerController(1), Listener.Get());
	if (!TestNotNull(TEXT("The world has a screen UI"), Screens) || !TestNotNull(TEXT("The node's calls made a button for the second player"), Button))
	{
		return false;
	}
	TestTrue(TEXT("AddToPlayerScreen puts it on its owner's screen"), Button->AddToPlayerScreen(0));
	PlaceInViewport(Button, ButtonSize, FVector2D::ZeroVector);
	Rig.PumpFrames(2);
	TestTrue(TEXT("...the screen the screen UI keeps for the second player"), Button->GetParent() == Screens->GetScreenRoot(Rig.GetPlayerController(1)));
	TestTrue(TEXT("...which is the one the second player's pointer is aimed through"), Button->GetParent() == Rig.Root(1));
	const TOptional<FBox2D> Drawn = FDreamDriverProjection::WidgetToPixelRect(Button);
	const FBox2D SecondPart = Rig.GetPlayerViewRect(1);
	if (!TestTrue(TEXT("The button is drawn on the viewport"), Drawn.IsSet())
		|| !TestTrue(FString::Printf(TEXT("...inside the second player's part %s (it is at %s)"), *Describe(SecondPart), *Describe(Drawn.GetValue())),
			IsInside(Drawn.GetValue(), SecondPart)))
	{
		return false;
	}

	TestTrue(TEXT("The first player's click on the button's pixel completes"),
		Rig.Driver(0)->Sequence().MoveToPixel(CentreOf(Drawn.GetValue())).Press().Release().Perform());
	TestEqual(TEXT("...and reaches nothing of the second player's screen"), Listener->PressedCount, 0);
	TestTrue(TEXT("The second player's click on it completes"),
		Rig.Driver(1)->Sequence().MoveToPixel(CentreOf(Drawn.GetValue())).Press().Release().Perform());
	TestEqual(TEXT("...and clicks it once"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenTypingTest,
	"DreamGUI.TextInput.OnASplitScreenEachPlayersCharactersGoIntoTheFieldThatPlayerIsEditing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSplitScreenTypingTest, "DreamGUI.TextInput.OnASplitScreenEachPlayersCharactersGoIntoTheFieldThatPlayerIsEditing", "[Pointer][Text][Animated]")

/*
 * A field in the middle of each player's screen, each clicked into by its own player: both are being edited at once, one
 * per player, as each Slate user has a focus of its own. What the first player types goes into the first field and what the
 * second types into the second, whatever order they type in, and each player's Enter ends that player's edit only.
 */
bool FDreamSplitScreenTypingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit(/*bInScreensFromScreenUI*/ false));
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	UDreamTextInput* First = Rig.MakeControl<UDreamTextInput>(TEXT("FirstPlayersField"), Rig.Root(0), FVector2D(320.0, 40.0));
	UDreamTextInput* Second = Rig.MakeControl<UDreamTextInput>(TEXT("SecondPlayersField"), Rig.Root(1), FVector2D(320.0, 40.0));
	if (!TestTrue(TEXT("A field on each player's screen"), First != nullptr && Second != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	TestTrue(TEXT("The first player clicks into the first field"), Rig.Driver(0)->Find(FDreamBy::Widget(First))->Click());
	TestTrue(TEXT("The second player clicks into the second field"), Rig.Driver(1)->Find(FDreamBy::Widget(Second))->Click());
	if (!TestTrue(TEXT("Both fields are being edited at once"), IsEditing(First) && IsEditing(Second)))
	{
		return false;
	}
	TestTrue(TEXT("The first player types, then the second, then the first again"),
		Rig.Driver()->Sequence()
			.AsPlayer(0).Type(TEXT("ab"))
			.AsPlayer(1).Type(TEXT("xy"))
			.AsPlayer(0).Type(TEXT("c"))
			.Perform());
	TestEqual(TEXT("The first field holds the first player's characters"), First->GetText(), FString(TEXT("abc")));
	TestEqual(TEXT("The second field holds the second player's"), Second->GetText(), FString(TEXT("xy")));

	TestTrue(TEXT("The first player's Enter completes"), Rig.Driver(0)->Sequence().Key(EKeys::Enter).Perform());
	TestFalse(TEXT("...and ends the first player's edit"), IsEditing(First));
	TestTrue(TEXT("...leaving the second player's going"), IsEditing(Second));
	TestTrue(TEXT("The second player's Enter completes"), Rig.Driver(1)->Sequence().Key(EKeys::Enter).Perform());
	TestFalse(TEXT("...and ends the second player's"), IsEditing(Second));
	TestEqual(TEXT("Neither field took the other player's Enter as text"), First->GetText() + TEXT("|") + Second->GetText(), FString(TEXT("abc|xy")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenEnterTest,
	"DreamGUI.Button.OnASplitScreenEachPlayersEnterClicksTheButtonThatPlayerFocusedAndNoOtherPlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSplitScreenEnterTest, "DreamGUI.Button.OnASplitScreenEachPlayersEnterClicksTheButtonThatPlayerFocusedAndNoOtherPlayers", "[Nav][Animated]")

/*
 * Each player's click focuses the button on that player's screen. Then each player's Enter -- SButton's Accept on the
 * focused button, through that player's own controller -- clicks the button that player focused, and not the other's.
 */
bool FDreamSplitScreenEnterTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> FirstListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> SecondListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit(/*bInScreensFromScreenUI*/ false));
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	UDreamButton* First = Rig.MakeControl<UDreamButton>(TEXT("FirstPlayersButton"), Rig.Root(0), ButtonSize);
	UDreamButton* Second = Rig.MakeControl<UDreamButton>(TEXT("SecondPlayersButton"), Rig.Root(1), ButtonSize);
	if (!TestTrue(TEXT("A button on each player's screen"), First != nullptr && Second != nullptr))
	{
		return false;
	}
	First->OnClicked.AddDynamic(FirstListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Second->OnClicked.AddDynamic(SecondListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);

	TestTrue(TEXT("The first player clicks its button"), Rig.Driver(0)->Find(FDreamBy::Widget(First))->Click());
	TestTrue(TEXT("The second player clicks its own"), Rig.Driver(1)->Find(FDreamBy::Widget(Second))->Click());
	if (!TestTrue(TEXT("Each was clicked once by its player's pointer"), FirstListener->ClickedCount == 1 && SecondListener->ClickedCount == 1))
	{
		return false;
	}

	TestTrue(TEXT("The first player's Enter completes"), Rig.Driver(0)->Sequence().Key(EKeys::Enter).WaitFrames(1).Perform());
	TestEqual(TEXT("...and clicks the button the first player focused"), FirstListener->ClickedCount, 2);
	TestEqual(TEXT("...and not the second player's"), SecondListener->ClickedCount, 1);
	TestTrue(TEXT("The second player's Enter completes"), Rig.Driver(1)->Sequence().Key(EKeys::Enter).WaitFrames(1).Perform());
	TestEqual(TEXT("...and clicks the button the second player focused"), SecondListener->ClickedCount, 2);
	TestEqual(TEXT("...and not the first player's"), FirstListener->ClickedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenTooltipTest,
	"DreamGUI.Tooltip.OnASplitScreenTheBubbleComesUpForThePlayerWhosePointerRestsOnTheWidgetInsideThatPlayersPart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSplitScreenTooltipTest, "DreamGUI.Tooltip.OnASplitScreenTheBubbleComesUpForThePlayerWhosePointerRestsOnTheWidgetInsideThatPlayersPart", "[Pointer][Animated]")

/*
 * A button with a tooltip on the second player's screen, the second player's pointer resting on it. FSlateUser::
 * UpdateTooltip runs per user: the bubble is the second player's, it comes up on that player's screen inside that player's
 * part, and the first player has none. The screen UI makes no second screen for the player to put it on, which the UI
 * manager would report as a screen-space screen too many.
 */
bool FDreamSplitScreenTooltipTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit(/*bInScreensFromScreenUI*/ true));
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
	UDreamButton* Help = Tooltip != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Help"), Rig.Root(1), ButtonSize) : nullptr;
	if (!TestNotNull(TEXT("A tooltip service and a button on the second player's screen"), Help))
	{
		return false;
	}
	Help->SetToolTipText(FText::AsCultureInvariant(TEXT("About help")));
	Rig.PumpFrames(2);

	TestTrue(TEXT("The second player's pointer moves onto the button"), Rig.Driver(1)->Find(FDreamBy::Widget(Help))->Hover());
	const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0 * UDreamGUISettings::Get()->TooltipDelaySeconds + 1.0);
	if (!TestTrue(TEXT("Resting there brings the button's bubble up for the second player"), Rig.Driver(1)->Wait(
		FDreamUntil::Condition([Tooltip, Help]() { return Tooltip->GetShownForUser(1) == Help; }, Timeout), Timeout,
		TEXT("the second player's tooltip"))))
	{
		return false;
	}
	Rig.PumpFrames(2);
	UDreamWidget* Bubble = Tooltip->GetBubbleForUser(1);
	TestNull(TEXT("The first player has no bubble"), Tooltip->GetBubbleForUser(0));
	TestNull(TEXT("...and nothing shown"), Tooltip->GetShownForUser(0));
	const TOptional<FBox2D> Drawn = Bubble != nullptr ? FDreamDriverProjection::WidgetToPixelRect(Bubble) : TOptional<FBox2D>();
	const FBox2D SecondPart = Rig.GetPlayerViewRect(1);
	if (TestTrue(TEXT("The second player's bubble is drawn"), Drawn.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("...inside the second player's part %s (it is at %s)"), *Describe(SecondPart), *Describe(Drawn.GetValue())),
			IsInside(Drawn.GetValue(), SecondPart));
	}
	const UDreamCanvas* BubbleRoot = Bubble != nullptr && Bubble->GetRenderCanvas() != nullptr ? Bubble->GetRenderCanvas()->GetRootCanvas() : nullptr;
	TestTrue(TEXT("...on the second player's own screen"), BubbleRoot != nullptr && BubbleRoot == Rig.RootCanvas(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSplitScreenDropdownTest,
	"DreamGUI.Dropdown.OnASplitScreenTheListThePlayerOpensComesUpInThatPlayersPartAndThatPlayerChoosesFromIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSplitScreenDropdownTest, "DreamGUI.Dropdown.OnASplitScreenTheListThePlayerOpensComesUpInThatPlayersPartAndThatPlayerChoosesFromIt", "[Pointer][Animated]")

/*
 * A dropdown on the second player's screen, clicked open by the second player: its list comes up on that player's screen,
 * inside that player's part, the way a combo box's menu opens over its anchor -- not on the first player's screen, where
 * a list lifted to a shared layer would be. The second player's click on the second option chooses it.
 */
bool FDreamSplitScreenDropdownTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayerScreensTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(TwoPlayersSplit(/*bInScreensFromScreenUI*/ true));
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	NoDoubleClicks(Rig);
	// High on the second player's screen, so the list has room to open downward inside the part.
	UDreamDropdown* Dropdown = Rig.MakeControl<UDreamDropdown>(TEXT("Quality"), Rig.Root(1), FVector2D(200.0, 40.0), FVector2D(0.0, 110.0));
	if (!TestNotNull(TEXT("A dropdown on the second player's screen"), Dropdown))
	{
		return false;
	}
	Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
	Dropdown->SetSelectedIndex(0);
	Dropdown->OnSelectionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleSelectionChanged);
	Rig.PumpFrames(2);

	TestTrue(TEXT("The second player clicks the dropdown"), Rig.Driver(1)->Find(FDreamBy::Widget(Dropdown))->Click());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("...and its list opens"), Dropdown->IsOpen()))
	{
		return false;
	}
	UDreamWidget* Column = Dropdown->ListNode != nullptr ? Dropdown->ListNode->FindChildByDisplayName(TEXT("Column")) : nullptr;
	TArray<UDreamWidget*> Rows;
	if (Column != nullptr)
	{
		for (UDreamWidget* Row : Column->GetChildren())
		{
			if (IsValid(Row) && Row != Dropdown->ItemTemplateNode.Get())
			{
				Rows.Add(Row);
			}
		}
	}
	if (!TestEqual(TEXT("The list holds a row per option"), Rows.Num(), 3))
	{
		return false;
	}
	const TOptional<FBox2D> ListDrawn = FDreamDriverProjection::WidgetToPixelRect(Dropdown->ListNode.Get());
	const FBox2D SecondPart = Rig.GetPlayerViewRect(1);
	if (TestTrue(TEXT("The open list is drawn"), ListDrawn.IsSet()))
	{
		TestTrue(FString::Printf(TEXT("...inside the second player's part %s (it is at %s)"), *Describe(SecondPart), *Describe(ListDrawn.GetValue())),
			IsInside(ListDrawn.GetValue(), SecondPart));
	}
	const UDreamCanvas* ListRoot = Dropdown->ListNode->GetRenderCanvas() != nullptr ? Dropdown->ListNode->GetRenderCanvas()->GetRootCanvas() : nullptr;
	TestTrue(TEXT("...on the second player's own screen"), ListRoot != nullptr && ListRoot == Rig.RootCanvas(1));

	TestTrue(TEXT("The second player clicks the second option"), Rig.Driver(1)->Find(FDreamBy::Widget(Rows[1]))->Click());
	TestEqual(TEXT("...which is now the selection"), Dropdown->GetSelectedIndex(), 1);
	if (TestEqual(TEXT("...announced once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("...naming the second option"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("Choosing closed the list"), Dropdown->IsOpen());
	return true;
}

#endif
