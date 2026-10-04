// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamTabView.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIActionBar.h"
#include "Interaction/DreamUIActionRouter.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * THE SHOULDER BUTTONS SWITCH TABS, AS COMMONUI'S TAB LIST DOES.
 *
 * A tab view takes the project's previous-tab and next-tab keys (LB and RB by default) when it holds the player's
 * focus, or is the one on their screen: the tab before or after the open one opens, the disabled ones are passed
 * over, and the ends go round. The focus goes with the tab when it was on the strip, where a click on the new tab
 * would have put it. While a player has such a tab view, the action bar shows the two prompts, for the device in
 * hand -- the defaults are pad keys, so a keyboard player is shown none.
 *
 * The keys go through Key, which under the rig's default host is DreamUIKeyRouting::RouteKey for player 0: the
 * bindings first, then the tab view (DreamUIKeyRouting::FindTabSwitchTarget).
 */
namespace DreamTabViewShoulderTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamTabView* MakeTabView(FDreamDriverRig& InRig, int32 InTabCount)
	{
		UDreamTabView* TabView = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0), FVector2D(0.0, 120.0));
		if (TabView == nullptr)
		{
			return nullptr;
		}
		TArray<FText> Labels;
		for (int32 Index = 0; Index < InTabCount; ++Index)
		{
			Labels.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Tab %d"), Index + 1)));
		}
		TabView->SetTabLabels(Labels);
		TabView->SetActiveTabIndex(0);
		return TabView;
	}

	UDreamWidget* TabAt(const UDreamTabView* InTabView, int32 InIndex)
	{
		return InTabView->Tabs.IsValidIndex(InIndex) ? InTabView->Tabs[InIndex].TabNode.Get() : nullptr;
	}

	bool IsFocusOnOrIn(FDreamDriverRig& InRig, const UDreamWidget* InWidget)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
		return Focused != nullptr && InWidget != nullptr && (Focused == InWidget || Focused->IsChildOf(InWidget));
	}

	/** The prompt the bar shows for InKey, or null. */
	const FDreamUIActionBinding* FindPrompt(const UDreamUIActionBar* InBar, const FKey& InKey)
	{
		for (const FDreamUIActionBinding& Prompt : InBar->GetPrompts())
		{
			if (Prompt.Key == InKey)
			{
				return &Prompt;
			}
		}
		return nullptr;
	}

	bool PressKey(FDreamDriverRig& InRig, const FKey& InKey)
	{
		return InRig.Driver()->Sequence().Key(InKey).WaitFrames(1).Perform();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewShoulderSwitchTest,
	"DreamGUI.TabView.TheShoulderButtonsSwitchTabsRoundTheEndsAndTheBarShowsTheirPrompts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabViewShoulderSwitchTest, "DreamGUI.TabView.TheShoulderButtonsSwitchTabsRoundTheEndsAndTheBarShowsTheirPrompts", "[Nav][Animated]")

/*
 * Three tabs, the focus on the first one, a pad in hand and a bar on the screen. The bar lists "previous tab" on LB
 * and "next tab" on RB. RB opens the second tab and the third, then goes round to the first; LB goes round
 * backwards to the third. Each switch is announced once, and the focus follows the open tab. With the keyboard in
 * hand the bar drops both prompts: no key of the default tables is a keyboard's.
 */
bool FDreamTabViewShoulderSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewShoulderTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamTabView* TabView = MakeTabView(Rig, 3);
	UDreamWidget* BarWidget = Rig.MakeWidget(TEXT("Prompts"), nullptr, FVector2D(600.0, 40.0), FVector2D(0.0, -260.0));
	UDreamUIActionBar* Bar = BarWidget != nullptr ? BarWidget->AddComponent<UDreamUIActionBar>() : nullptr;
	if (!TestTrue(TEXT("The rig, the tab view and the bar came up"),
		Rig.IsUsable() && TabView != nullptr && TabView->Tabs.Num() == 3 && Bar != nullptr && Rig.EventSystem() != nullptr))
	{
		return false;
	}
	TabView->OnTabChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleTabChanged);
	Rig.PumpFrames(2);

	TestTrue(TEXT("Clicking the first tab completes"), Rig.Driver()->Find(FDreamBy::Widget(TabAt(TabView, 0)))->Click());
	TestTrue(TEXT("The tab view takes the shoulder buttons"), TabView->CanSwitchTab(0));
	Rig.EventSystem()->ReportInputDevice(EDreamUIInputDevice::Gamepad);
	const FDreamUIActionBinding* PreviousPrompt = FindPrompt(Bar, EKeys::Gamepad_LeftShoulder);
	const FDreamUIActionBinding* NextPrompt = FindPrompt(Bar, EKeys::Gamepad_RightShoulder);
	TestNotNull(TEXT("With a pad in hand the bar shows LB's prompt"), PreviousPrompt);
	TestNotNull(TEXT("...and RB's"), NextPrompt);
	if (PreviousPrompt != nullptr && NextPrompt != nullptr)
	{
		TestFalse(TEXT("...each saying what it does"), PreviousPrompt->DisplayName.IsEmpty() || NextPrompt->DisplayName.IsEmpty());
	}

	TestTrue(TEXT("RB completes"), PressKey(Rig, EKeys::Gamepad_RightShoulder));
	TestEqual(TEXT("RB opened the second tab"), TabView->GetActiveTabIndex(), 1);
	TestTrue(TEXT("...and the focus went with it"), IsFocusOnOrIn(Rig, TabAt(TabView, 1)));
	TestTrue(TEXT("RB again completes"), PressKey(Rig, EKeys::Gamepad_RightShoulder));
	TestEqual(TEXT("RB opened the third tab"), TabView->GetActiveTabIndex(), 2);
	TestTrue(TEXT("RB at the last tab completes"), PressKey(Rig, EKeys::Gamepad_RightShoulder));
	TestEqual(TEXT("RB went round to the first tab"), TabView->GetActiveTabIndex(), 0);
	TestTrue(TEXT("LB at the first tab completes"), PressKey(Rig, EKeys::Gamepad_LeftShoulder));
	TestEqual(TEXT("LB went round backwards to the third tab"), TabView->GetActiveTabIndex(), 2);
	TestTrue(TEXT("...with the focus on it"), IsFocusOnOrIn(Rig, TabAt(TabView, 2)));
	if (TestEqual(TEXT("Each switch was announced once"), Listener->TabChangedIndices.Num(), 4))
	{
		TestEqual(TEXT("...the second tab"), Listener->TabChangedIndices[0], 1);
		TestEqual(TEXT("...the third"), Listener->TabChangedIndices[1], 2);
		TestEqual(TEXT("...the first"), Listener->TabChangedIndices[2], 0);
		TestEqual(TEXT("...and the third again"), Listener->TabChangedIndices[3], 2);
	}

	Rig.EventSystem()->ReportInputDevice(EDreamUIInputDevice::MouseAndKeyboard);
	TestNull(TEXT("With the keyboard in hand the bar shows no LB prompt"), FindPrompt(Bar, EKeys::Gamepad_LeftShoulder));
	TestNull(TEXT("...nor an RB one"), FindPrompt(Bar, EKeys::Gamepad_RightShoulder));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewSwitchRulesTest,
	"DreamGUI.TabView.SwitchingTabsPassesOverDisabledOnesGoesRoundAndNeedsAnotherTabToGoTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The tab view's own half, asked directly: one step skips a disabled tab, both ends go round, and with no other
 * enabled tab there is nothing to switch to -- and then the bar is told so, which it hears as its prompts moving.
 */
bool FDreamTabViewSwitchRulesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabViewShoulderTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamTabView* TabView = MakeTabView(Rig, 4);
	if (!TestTrue(TEXT("The rig and the tab view came up"), Rig.IsUsable() && TabView != nullptr && TabView->Tabs.Num() == 4))
	{
		return false;
	}
	Rig.PumpFrames(2);
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(Rig.GetWorld());
	int32 PromptNews = 0;
	FDelegateHandle NewsHandle;
	if (Router != nullptr)
	{
		NewsHandle = Router->GetBindingsChangedEvent().AddLambda([&PromptNews](int32) { ++PromptNews; });
	}

	TestTrue(TEXT("Four enabled tabs can be switched"), TabView->CanSwitchTab(0));
	TestTrue(TEXT("Next completes a switch"), TabView->SwitchTab(0, 1));
	TestEqual(TEXT("...to the second tab"), TabView->GetActiveTabIndex(), 1);
	TabView->SetTabEnabled(2, false);
	TestTrue(TEXT("Next again switches"), TabView->SwitchTab(0, 1));
	TestEqual(TEXT("...over the disabled third tab to the fourth"), TabView->GetActiveTabIndex(), 3);
	TestTrue(TEXT("Next at the end switches"), TabView->SwitchTab(0, 1));
	TestEqual(TEXT("...round to the first"), TabView->GetActiveTabIndex(), 0);
	TestTrue(TEXT("Previous at the start switches"), TabView->SwitchTab(0, -1));
	TestEqual(TEXT("...round to the last"), TabView->GetActiveTabIndex(), 3);
	TestTrue(TEXT("Previous again switches"), TabView->SwitchTab(0, -1));
	TestEqual(TEXT("...over the disabled tab to the second"), TabView->GetActiveTabIndex(), 1);

	const int32 NewsBefore = PromptNews;
	TabView->SetTabEnabledStates({ false, true, false, false });
	TestFalse(TEXT("With no other enabled tab there is nothing to switch to"), TabView->CanSwitchTab(0));
	TestFalse(TEXT("...and a switch does nothing"), TabView->SwitchTab(0, 1));
	TestEqual(TEXT("...the open tab stays open"), TabView->GetActiveTabIndex(), 1);
	TestTrue(TEXT("The bars were told the prompts moved"), Router == nullptr || PromptNews > NewsBefore);

	if (Router != nullptr)
	{
		Router->GetBindingsChangedEvent().Remove(NewsHandle);
	}
	return true;
}

#endif
