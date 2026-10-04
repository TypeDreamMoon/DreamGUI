// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/World.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITabOrder.h"
#include "Interaction/UISelectable.h"
#include "DreamNavigationTestTypes.h"
#include "DreamScopedWorld.h"

/*
 * Tab order as a sequence (FDreamUITabOrder), tested where nothing but the order can decide: plain widgets with a
 * selectable each, in a bare world, so no geometry, key routing or focus pipeline is involved.
 *
 * It used to be geometry: Next was the nearest control to the right, else the nearest below, and Prev the mirror. A
 * 2x2 grid lost a cell each way, the first field of a column jumped to a right-aligned button, nothing reversed and
 * nothing wrapped. What is pinned here is the replacement: the widget tree, depth first, siblings by TabIndex, the
 * widget's own stop before those inside it; explicit links first; wrapping as the domain says; containers holding Tab
 * as their TabNavigation says; and every widget that cannot take focus left out.
 */

namespace DreamTabOrderTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** A registered widget under InParent (a root when null), placed at (InY, InZ) in its parent's plane. */
	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, float InY = 0.0f, float InZ = 0.0f)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(120.0f);
		Widget->SetHeight(40.0f);
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
		}
		Widget->SetRelativeLocation(FVector(0.0, InY, InZ));
		Widget->OnRegister();
		return Widget;
	}

	/** A widget with a selectable on it, which makes it focusable and navigable: a stop. */
	UDreamWidget* MakeStop(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, float InY = 0.0f, float InZ = 0.0f)
	{
		UDreamWidget* Widget = MakeWidget(InWorld, InParent, InName, InY, InZ);
		Widget->AddComponent<UUISelectable>();
		return Widget;
	}

	FDreamUITabDomain ScreenOf(UDreamWidget* InRoot, bool bInWraps = true)
	{
		FDreamUITabDomain Domain;
		Domain.Kind = EDreamUITabDomainKind::Screen;
		Domain.Root = InRoot;
		Domain.bWraps = bInWraps;
		return Domain;
	}

	FString NameOf(const UDreamWidget* InWidget)
	{
		return InWidget != nullptr ? InWidget->GetDisplayName() : FString(TEXT("(none)"));
	}

	FString NamesOf(const TArray<UDreamWidget*>& InWidgets)
	{
		TArray<FString> Names;
		for (const UDreamWidget* Widget : InWidgets)
		{
			Names.Add(NameOf(Widget));
		}
		return FString::Join(Names, TEXT(", "));
	}

	FString StopsOf(const FDreamUITabDomain& InDomain)
	{
		TArray<UDreamWidget*> Stops;
		FDreamUITabOrder::CollectStops(InDomain, Stops);
		return NamesOf(Stops);
	}

	/** InPresses presses of Tab (Shift+Tab when bInBackward) from InStart, through FindNextStop: where each landed. */
	FString Walk(const FDreamUITabDomain& InDomain, const UDreamWidget* InStart, int32 InPresses, bool bInBackward)
	{
		TArray<FString> Names;
		const UDreamWidget* At = InStart;
		for (int32 Press = 0; Press < InPresses; ++Press)
		{
			At = FDreamUITabOrder::FindNextStop(InDomain, At, bInBackward);
			Names.Add(NameOf(At));
			if (At == nullptr)
			{
				break;
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** The whole of one press for player 0 (Step), as where it landed: "(none)" when focus stays. */
	FString StepFrom(UWorld* InWorld, const UDreamWidget* InFrom, bool bInBackward)
	{
		return NameOf(FDreamUITabOrder::Step(InWorld, 0, InFrom, bInBackward).Target);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderFormTest,
	"DreamGUI.Navigation.TabOrder.AFormIsWalkedInHierarchyOrderAndShiftTabExactlyReversesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderFormTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	// A column of three fields and, lower down, a button pushed to the right and another to the left: the case where the
	// geometric Next went from the first field straight to the button on its right.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Form = MakeWidget(TestWorld.World, Root, TEXT("Form"), -200.0f, 0.0f);
	UDreamWidget* NameField = MakeStop(TestWorld.World, Form, TEXT("Name"), 0.0f, 100.0f);
	UDreamWidget* EmailField = MakeStop(TestWorld.World, Form, TEXT("Email"), 0.0f, 50.0f);
	MakeStop(TestWorld.World, Form, TEXT("Password"), 0.0f, 0.0f);
	MakeStop(TestWorld.World, Root, TEXT("Submit"), 300.0f, -60.0f);
	MakeStop(TestWorld.World, Root, TEXT("Cancel"), -300.0f, -60.0f);
	const FDreamUITabDomain Domain = ScreenOf(Root);

	TestEqual(TEXT("the stops are the hierarchy, depth first"), StopsOf(Domain), TEXT("Name, Email, Password, Submit, Cancel"));
	TestEqual(TEXT("the first field's Tab is the next field, not the button to its right"),
		FDreamUITabOrder::FindNextStop(Domain, NameField, false), EmailField);
	TestEqual(TEXT("Tab walks every stop in that order and goes round at the end of the screen"),
		Walk(Domain, NameField, 5, false), TEXT("Email, Password, Submit, Cancel, Name"));
	TestEqual(TEXT("Shift+Tab walks exactly the same stops backwards"),
		Walk(Domain, NameField, 5, true), TEXT("Cancel, Submit, Password, Email, Name"));
	// And the whole press, explicit links and all, says the same thing: a step is the order when nothing overrides it.
	TestEqual(TEXT("one press from the first field lands on the second"), StepFrom(TestWorld.World, NameField, false), TEXT("Email"));
	TestEqual(TEXT("and one back from the second lands on the first"), StepFrom(TestWorld.World, EmailField, true), TEXT("Name"));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderGridTest,
	"DreamGUI.Navigation.TabOrder.EveryCellOfATwoByTwoGridIsReachedOnceEachWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderGridTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	// A B / C D. Geometrically Tab went A, B, D and stopped, and Shift+Tab D, C, A: one cell lost each way.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Grid = MakeWidget(TestWorld.World, Root, TEXT("Grid"));
	UDreamWidget* CellA = MakeStop(TestWorld.World, Grid, TEXT("A"), -100.0f, 50.0f);
	MakeStop(TestWorld.World, Grid, TEXT("B"), 100.0f, 50.0f);
	MakeStop(TestWorld.World, Grid, TEXT("C"), -100.0f, -50.0f);
	UDreamWidget* CellD = MakeStop(TestWorld.World, Grid, TEXT("D"), 100.0f, -50.0f);
	const FDreamUITabDomain Domain = ScreenOf(Root);

	TestEqual(TEXT("Tab from A reaches B, C and D once each, then A again"), Walk(Domain, CellA, 4, false), TEXT("B, C, D, A"));
	TestEqual(TEXT("Shift+Tab from A reaches D, C and B once each, then A again"), Walk(Domain, CellA, 4, true), TEXT("D, C, B, A"));
	TestEqual(TEXT("the first stop of the grid is its first cell"), FDreamUITabOrder::FindFirstStop(Domain), CellA);
	TestEqual(TEXT("and the last its last"), FDreamUITabOrder::FindLastStop(Domain), CellD);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderWrapTest,
	"DreamGUI.Navigation.TabOrder.TheEndOfAScreenWrapsOnlyWhenTheScreenSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderWrapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* First = MakeStop(TestWorld.World, Root, TEXT("First"));
	MakeStop(TestWorld.World, Root, TEXT("Middle"));
	UDreamWidget* Last = MakeStop(TestWorld.World, Root, TEXT("Last"));

	TestEqual(TEXT("a screen that wraps goes round from its last stop to its first"),
		FDreamUITabOrder::FindNextStop(ScreenOf(Root, true), Last, false), First);
	TestEqual(TEXT("and from its first back to its last"),
		FDreamUITabOrder::FindNextStop(ScreenOf(Root, true), First, true), Last);
	TestNull(TEXT("a screen that does not wrap holds at its last stop"),
		FDreamUITabOrder::FindNextStop(ScreenOf(Root, false), Last, false));
	TestNull(TEXT("and at its first"), FDreamUITabOrder::FindNextStop(ScreenOf(Root, false), First, true));

	// The screen's own rule comes from the project setting.
	{
		TGuardValue<bool> NoScreenWrap(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, false);
		const FDreamUITabDomain Found = FDreamUITabOrder::FindDomain(TestWorld.World, 0, Last);
		TestTrue(TEXT("a screen's domain is found from the focus on it"), Found.Kind == EDreamUITabDomainKind::Screen && Found.GetRoot() == Root);
		TestFalse(TEXT("and wraps as bTabWrapsAtScreenEnd says"), Found.bWraps);
		TestEqual(TEXT("so a press at the end of the screen keeps the focus where it is"), StepFrom(TestWorld.World, Last, false), TEXT("(none)"));
	}
	{
		TGuardValue<bool> ScreenWrap(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, true);
		TestEqual(TEXT("with the setting on, the same press goes round"), StepFrom(TestWorld.World, Last, false), TEXT("First"));
	}

	// A root that says Cycle or Contained has the last word over the setting.
	Root->SetTabNavigation(EDreamWidgetTabNavigation::Contained);
	TestNull(TEXT("a Contained root holds even where the screen would wrap"), FDreamUITabOrder::FindNextStop(ScreenOf(Root, true), Last, false));
	Root->SetTabNavigation(EDreamWidgetTabNavigation::Cycle);
	TestEqual(TEXT("a Cycle root goes round even where the screen would hold"),
		FDreamUITabOrder::FindNextStop(ScreenOf(Root, false), Last, false), First);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderTabIndexTest,
	"DreamGUI.Navigation.TabOrder.TabIndexReordersSiblingsAndOnlySiblings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderTabIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	MakeStop(TestWorld.World, Root, TEXT("A"));
	MakeStop(TestWorld.World, Root, TEXT("B"))->SetTabIndex(-1);
	MakeStop(TestWorld.World, Root, TEXT("C"))->SetTabIndex(2);
	MakeStop(TestWorld.World, Root, TEXT("D"));
	TestEqual(TEXT("lower first, and siblings with the same index keep the hierarchy's order"), StopsOf(ScreenOf(Root)), TEXT("B, A, D, C"));

	// Only siblings are compared: a negative index deep inside a later panel does not pull its widget ahead of an
	// earlier panel's stops.
	UDreamWidget* Other = MakeWidget(TestWorld.World, nullptr, TEXT("Other"));
	UDreamWidget* Late = MakeWidget(TestWorld.World, Other, TEXT("Late"));
	Late->SetTabIndex(1);
	UDreamWidget* Early = MakeWidget(TestWorld.World, Other, TEXT("Early"));
	MakeStop(TestWorld.World, Late, TEXT("Late1"));
	MakeStop(TestWorld.World, Late, TEXT("Late2"))->SetTabIndex(-5);
	MakeStop(TestWorld.World, Early, TEXT("Early1"));
	TestEqual(TEXT("a panel's index orders it among its siblings, and its children's among theirs"),
		StopsOf(ScreenOf(Other)), TEXT("Early1, Late2, Late1"));
	TestEqual(TEXT("the accessor answers what was set"), Late->GetTabIndex(), 1);

	Root->DestroyWidget();
	Other->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderSkipTest,
	"DreamGUI.Navigation.TabOrder.WhatCannotTakeFocusIsSkipped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderSkipTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Open = MakeStop(TestWorld.World, Root, TEXT("Open"));
	UDreamWidget* NotTabStop = MakeStop(TestWorld.World, Root, TEXT("NotTabStop"));
	NotTabStop->SetIsTabStop(false);
	UDreamWidget* DisabledControl = MakeStop(TestWorld.World, Root, TEXT("DisabledControl"));
	DisabledControl->GetComponent<UUISelectable>()->SetInteractable(false);
	UDreamWidget* DisabledWidget = MakeStop(TestWorld.World, Root, TEXT("DisabledWidget"));
	DisabledWidget->SetIsEnabled(false);
	UDreamWidget* Hidden = MakeStop(TestWorld.World, Root, TEXT("Hidden"));
	Hidden->SetVisibility(EDreamWidgetVisibility::Hidden);
	UDreamWidget* Collapsed = MakeStop(TestWorld.World, Root, TEXT("Collapsed"));
	Collapsed->SetVisibility(EDreamWidgetVisibility::Collapsed);
	UDreamWidget* Inactive = MakeStop(TestWorld.World, Root, TEXT("Inactive"));
	Inactive->SetWidgetActive(false);
	// SetFocus refuses a widget that is not focusable, and navigation used to land on one anyway.
	UDreamWidget* Unfocusable = MakeStop(TestWorld.World, Root, TEXT("Unfocusable"));
	Unfocusable->SetIsFocusable(false);
	UDreamWidget* Refusing = MakeStop(TestWorld.World, Root, TEXT("Refusing"));
	Refusing->GetComponent<UUISelectable>()->SetCanNavigateHere(false);
	UDreamWidget* NoneContainer = MakeWidget(TestWorld.World, Root, TEXT("NoneContainer"));
	NoneContainer->SetTabNavigation(EDreamWidgetTabNavigation::None);
	UDreamWidget* InNone = MakeStop(TestWorld.World, NoneContainer, TEXT("InNone"));
	UDreamWidget* ScrollbarPart = MakeStop(TestWorld.World, Root, TEXT("ScrollbarPart"));
	ScrollbarPart->AddComponent<UDreamScrollbarPartTestBehaviour>();
	UDreamWidget* Plain = MakeWidget(TestWorld.World, Root, TEXT("Plain"));
	UDreamWidget* Last = MakeStop(TestWorld.World, Root, TEXT("Last"));
	const FDreamUITabDomain Domain = ScreenOf(Root);

	TestEqual(TEXT("only the two that can take focus are stops"), StopsOf(Domain), TEXT("Open, Last"));
	TestTrue(TEXT("an ordinary control is a stop"), FDreamUITabOrder::IsTabStop(Open));
	TestFalse(TEXT("bIsTabStop off is not"), FDreamUITabOrder::IsTabStop(NotTabStop));
	TestFalse(TEXT("a disabled control is not"), FDreamUITabOrder::IsTabStop(DisabledControl));
	TestFalse(TEXT("a widget disabled itself is not"), FDreamUITabOrder::IsTabStop(DisabledWidget));
	TestFalse(TEXT("a hidden one is not"), FDreamUITabOrder::IsTabStop(Hidden));
	TestFalse(TEXT("a collapsed one is not"), FDreamUITabOrder::IsTabStop(Collapsed));
	TestFalse(TEXT("an inactive one is not"), FDreamUITabOrder::IsTabStop(Inactive));
	TestFalse(TEXT("an unfocusable one is not"), FDreamUITabOrder::IsTabStop(Unfocusable));
	TestFalse(TEXT("one that refuses navigation is not"), FDreamUITabOrder::IsTabStop(Refusing));
	TestFalse(TEXT("nothing inside a None container is"), FDreamUITabOrder::IsTabStop(InNone));
	TestFalse(TEXT("a scroll bar's part is not"), FDreamUITabOrder::IsTabStop(ScrollbarPart));
	TestFalse(TEXT("a widget with nothing that takes navigation is not"), FDreamUITabOrder::IsTabStop(Plain));

	// Not being a stop does not make a widget a dead end: focus put there by a click or by code steps on from its place.
	TestEqual(TEXT("Tab from a widget that is no stop goes to the stop after it"),
		FDreamUITabOrder::FindNextStop(Domain, NotTabStop, false), Last);
	TestEqual(TEXT("and Shift+Tab to the one before it"),
		FDreamUITabOrder::FindNextStop(Domain, NotTabStop, true), Open);
	TestEqual(TEXT("as from inside a None container"), FDreamUITabOrder::FindNextStop(Domain, InNone, false), Last);

	// The unfocusable control is refused by the arrows too: the selectable's own answer.
	TestFalse(TEXT("an unfocusable control cannot be navigated to by any direction"),
		Unfocusable->GetComponent<UUISelectable>()->CanBeNavigatedTo());

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderEmptyStartTest,
	"DreamGUI.Navigation.TabOrder.WithNoStartTabEntersAtTheFirstStopAndShiftTabAtTheLast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderEmptyStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* First = MakeStop(TestWorld.World, Root, TEXT("First"));
	MakeStop(TestWorld.World, Root, TEXT("Middle"));
	UDreamWidget* Last = MakeStop(TestWorld.World, Root, TEXT("Last"));
	const FDreamUITabDomain Domain = ScreenOf(Root);

	// Both used to land on the "default" top-left control, so Shift+Tab could never start at the end.
	TestEqual(TEXT("Tab with nothing focused goes to the first stop"), FDreamUITabOrder::FindNextStop(Domain, nullptr, false), First);
	TestEqual(TEXT("Shift+Tab with nothing focused goes to the last"), FDreamUITabOrder::FindNextStop(Domain, nullptr, true), Last);
	// A start outside the domain counts as none: Tab enters the domain.
	UDreamWidget* Elsewhere = MakeStop(TestWorld.World, nullptr, TEXT("Elsewhere"));
	TestEqual(TEXT("a start outside the domain enters it at its first stop"), FDreamUITabOrder::FindNextStop(Domain, Elsewhere, false), First);
	TestEqual(TEXT("and backwards at its last"), FDreamUITabOrder::FindNextStop(Domain, Elsewhere, true), Last);

	Root->DestroyWidget();
	Elsewhere->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderContainerModesTest,
	"DreamGUI.Navigation.TabOrder.EachContainerModeHoldsTabAsItsNameSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderContainerModesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Before = MakeStop(TestWorld.World, Root, TEXT("Before"));
	UDreamTabEntryTestWidget* Group = NewObject<UDreamTabEntryTestWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Group->SetDisplayName(TEXT("Group"));
	Group->TrySetParent(Root, false);
	Group->OnRegister();
	UDreamWidget* InsideFirst = MakeStop(TestWorld.World, Group, TEXT("G1"));
	UDreamWidget* InsideLast = MakeStop(TestWorld.World, Group, TEXT("G2"));
	UDreamWidget* After = MakeStop(TestWorld.World, Root, TEXT("After"));
	const FDreamUITabDomain Domain = ScreenOf(Root);

	// Continue, the default: the group's stops are walked where it stands, and Tab goes on past them.
	TestEqual(TEXT("Continue: the stops inside are part of the walk"), StopsOf(Domain), TEXT("Before, G1, G2, After"));
	TestEqual(TEXT("Continue: Tab leaves the group at its end"), FDreamUITabOrder::FindNextStop(Domain, InsideLast, false), After);

	// Cycle: once Tab is inside it goes round, and it is still entered from outside as usual.
	Group->SetTabNavigation(EDreamWidgetTabNavigation::Cycle);
	TestEqual(TEXT("Cycle: Tab at the group's last stop goes round to its first"), FDreamUITabOrder::FindNextStop(Domain, InsideLast, false), InsideFirst);
	TestEqual(TEXT("Cycle: Shift+Tab at its first goes round to its last"), FDreamUITabOrder::FindNextStop(Domain, InsideFirst, true), InsideLast);
	TestEqual(TEXT("Cycle: Tab before it still enters it"), FDreamUITabOrder::FindNextStop(Domain, Before, false), InsideFirst);

	// Contained: held inside, without going round.
	Group->SetTabNavigation(EDreamWidgetTabNavigation::Contained);
	TestNull(TEXT("Contained: Tab at the group's last stop stays"), FDreamUITabOrder::FindNextStop(Domain, InsideLast, false));
	TestNull(TEXT("Contained: Shift+Tab at its first stays"), FDreamUITabOrder::FindNextStop(Domain, InsideFirst, true));
	TestEqual(TEXT("Contained: inside it Tab still moves"), FDreamUITabOrder::FindNextStop(Domain, InsideFirst, false), InsideLast);

	// None: nothing in it is a stop.
	Group->SetTabNavigation(EDreamWidgetTabNavigation::None);
	TestEqual(TEXT("None: the group's stops are not walked"), StopsOf(Domain), TEXT("Before, After"));

	// Once: one stop where it stands, a list's shape.
	Group->SetTabNavigation(EDreamWidgetTabNavigation::Once);
	TestEqual(TEXT("Once: the group is listed once, as itself"), StopsOf(Domain), TEXT("Before, Group, After"));
	TestEqual(TEXT("Once: the next Tab inside it leaves it"), FDreamUITabOrder::FindNextStop(Domain, InsideFirst, false), After);
	TestEqual(TEXT("Once: and the next Shift+Tab inside it leaves it backwards"), FDreamUITabOrder::FindNextStop(Domain, InsideLast, true), Before);

	// Entering it: a question (Peek) goes to its first stop, its last backwards, and never asks the container to scroll
	// or build anything; the press itself (Step) asks the container where to enter, a list's selected row.
	TestEqual(TEXT("Once: a question enters at its first stop"), NameOf(FDreamUITabOrder::Peek(TestWorld.World, 0, Before, false).Target), TEXT("G1"));
	TestEqual(TEXT("Once: and backwards at its last"), NameOf(FDreamUITabOrder::Peek(TestWorld.World, 0, After, true).Target), TEXT("G2"));
	TestEqual(TEXT("Once: no question asked the container"), Group->EntryCalls, 0);
	Group->Entry = InsideLast;
	const FDreamUITabStep Entered = FDreamUITabOrder::Step(TestWorld.World, 0, Before, false);
	TestEqual(TEXT("Once: a press enters where the container says"), NameOf(Entered.Target), TEXT("G2"));
	TestTrue(TEXT("Once: and says it came through the container"), Entered.bEnteredContainer);
	TestEqual(TEXT("Once: the container was asked once, forwards"), Group->EntryCalls, 1);
	TestFalse(TEXT("Once: forwards"), Group->bLastEntryBackward);
	TestTrue(TEXT("Once: the receiver is the landing widget's selectable"),
		Entered.Receiver == InsideLast->GetComponent<UUISelectable>());
	// A container with nothing to answer (an empty list) leaves the walk to its first stop.
	Group->Entry = nullptr;
	TestEqual(TEXT("Once: no answer from the container enters at its first stop"),
		NameOf(FDreamUITabOrder::Step(TestWorld.World, 0, Before, false).Target), TEXT("G1"));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderLinksTest,
	"DreamGUI.Navigation.TabOrder.ExplicitLinksAndRulesWinOverTheOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderLinksTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;
	TGuardValue<bool> NoScreenWrap(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, false);
	TGuardValue<EDreamUITabOrder> HierarchyOrder(GetMutableDefault<UDreamGUISettings>()->TabOrder, EDreamUITabOrder::Hierarchy);

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* WidgetA = MakeStop(TestWorld.World, Root, TEXT("A"));
	UDreamWidget* WidgetB = MakeStop(TestWorld.World, Root, TEXT("B"));
	UDreamWidget* WidgetC = MakeStop(TestWorld.World, Root, TEXT("C"));
	UDreamWidget* WidgetD = MakeStop(TestWorld.World, Root, TEXT("D"));
	UUISelectable* SelectableA = WidgetA->GetComponent<UUISelectable>();
	UUISelectable* SelectableB = WidgetB->GetComponent<UUISelectable>();
	UUISelectable* SelectableD = WidgetD->GetComponent<UUISelectable>();

	// A selectable's explicit Next wins over the order.
	SelectableA->SetNavigationNext(EUISelectableNavigationMode::Explicit);
	SelectableA->SetNavigationNextExplicit(SelectableD);
	TestEqual(TEXT("an explicit Next link wins"), StepFrom(TestWorld.World, WidgetA, false), TEXT("D"));
	// A link to something that cannot take focus leaves the order to answer: never a dead end.
	SelectableD->SetInteractable(false);
	TestEqual(TEXT("a link into a disabled control falls back to the order"), StepFrom(TestWorld.World, WidgetA, false), TEXT("B"));
	SelectableD->SetInteractable(true);
	SelectableA->SetNavigationNext(EUISelectableNavigationMode::Auto);

	// A selectable's None keeps focus where it is, as it does for the arrows.
	SelectableB->SetNavigationPrev(EUISelectableNavigationMode::None);
	TestEqual(TEXT("Prev set to None holds Shift+Tab"), StepFrom(TestWorld.World, WidgetB, true), TEXT("(none)"));
	TestNull(TEXT("and the selectable's own finder says navigation is off that way"), SelectableB->FindNavigableOn(EDreamUINavigationDirection::Prev));
	SelectableB->SetNavigationPrev(EUISelectableNavigationMode::Auto);

	// The widget's own rules come first, as for every direction.
	UDreamWidgetNavigation* RulesB = WidgetB->GetOrCreateNavigation();
	RulesB->SetRule(EDreamUINavigationDirection::Next, EDreamUINavigationRule::Stop);
	TestEqual(TEXT("a Stop rule on Next holds Tab"), StepFrom(TestWorld.World, WidgetB, false), TEXT("(none)"));
	FDreamWidgetNavigationData& NextOfB = RulesB->GetNavigationData(EDreamUINavigationDirection::Next);
	NextOfB.Rule = EDreamUINavigationRule::Explicit;
	NextOfB.WidgetToFocus = TEXT("A");
	TestEqual(TEXT("an Explicit rule by name sends Tab exactly there"), StepFrom(TestWorld.World, WidgetB, false), TEXT("A"));

	UDreamNavigationTargetProvider* Provider = NewObject<UDreamNavigationTargetProvider>(TestWorld.World);
	FDreamCustomWidgetNavigationDelegate Delegate;
	Delegate.BindUFunction(Provider, TEXT("Provide"));
	UDreamWidgetNavigation* RulesA = WidgetA->GetOrCreateNavigation();
	Provider->Target = WidgetC;
	RulesA->SetCustomDelegate(EDreamUINavigationDirection::Prev, Delegate);
	TestEqual(TEXT("a Custom rule on Previous decides Shift+Tab"), StepFrom(TestWorld.World, WidgetA, true), TEXT("C"));
	TestEqual(TEXT("and was told the direction"), Provider->LastDirection, EDreamUINavigationDirection::Prev);
	Provider->Target = nullptr;
	TestEqual(TEXT("a Custom rule that answers nothing keeps the focus"), StepFrom(TestWorld.World, WidgetA, true), TEXT("(none)"));

	// At the end of a screen that does not wrap: a Wrap rule goes round anyway, and a boundary rule is asked first.
	TestEqual(TEXT("the last stop of a screen that does not wrap holds"), StepFrom(TestWorld.World, WidgetD, false), TEXT("(none)"));
	UDreamWidgetNavigation* RulesD = WidgetD->GetOrCreateNavigation();
	RulesD->SetRule(EDreamUINavigationDirection::Next, EDreamUINavigationRule::Wrap);
	TestEqual(TEXT("a Wrap rule on Next goes round where the screen does not"), StepFrom(TestWorld.World, WidgetD, false), TEXT("A"));
	Provider->Target = WidgetB;
	Provider->CallCount = 0;
	RulesD->SetNavigationRuleCustomBoundary(EDreamUINavigationDirection::Next, Delegate);
	TestEqual(TEXT("a CustomBoundary rule is asked when the order runs out"), StepFrom(TestWorld.World, WidgetD, false), TEXT("B"));
	TestEqual(TEXT("once"), Provider->CallCount, 1);
	TestEqual(TEXT("and not while the order has somewhere to go"), StepFrom(TestWorld.World, WidgetC, false), TEXT("D"));
	TestEqual(TEXT("so it was not asked again"), Provider->CallCount, 1);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderDialogTest,
	"DreamGUI.Navigation.TabOrder.AConfiningScreenHoldsTabAndTabFromOutsideEntersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderDialogTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;
	TGuardValue<bool> ScreenWrap(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, true);
	if (!TestNotNull(TEXT("the world has a navigation stack"), UDreamUINavigationStack::Get(TestWorld.World)))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* Page = MakeWidget(TestWorld.World, Root, TEXT("Page"));
	UDreamWidget* OnPage = MakeStop(TestWorld.World, Page, TEXT("OnPage"));
	MakeStop(TestWorld.World, Page, TEXT("AlsoOnPage"));
	UDreamWidget* Dialog = MakeWidget(TestWorld.World, Root, TEXT("Dialog"));
	UDreamWidget* Ok = MakeStop(TestWorld.World, Dialog, TEXT("Ok"));
	UDreamWidget* Cancel = MakeStop(TestWorld.World, Dialog, TEXT("Cancel"));
	UDreamUINavigationScope* Scope = Dialog->AddComponent<UDreamUINavigationScope>();
	Scope->SetActivateWhenEnabled(false);
	Scope->ActivateScope();

	const FDreamUITabDomain Domain = FDreamUITabOrder::FindDomain(TestWorld.World, 0, OnPage);
	TestTrue(TEXT("with a confining screen up, the domain is that screen wherever the focus is"),
		Domain.Kind == EDreamUITabDomainKind::Scope && Domain.GetRoot() == Dialog);
	TestTrue(TEXT("and it goes round"), Domain.bWraps);
	TestEqual(TEXT("Tab from the page behind enters the dialog at its first stop"), StepFrom(TestWorld.World, OnPage, false), TEXT("Ok"));
	TestEqual(TEXT("Shift+Tab from the page enters at its last"), StepFrom(TestWorld.World, OnPage, true), TEXT("Cancel"));
	TestEqual(TEXT("Tab with nothing focused enters it too"), StepFrom(TestWorld.World, nullptr, false), TEXT("Ok"));
	TestEqual(TEXT("Tab at the dialog's last stop goes round inside it, never to the page"), StepFrom(TestWorld.World, Cancel, false), TEXT("Ok"));
	TestEqual(TEXT("and Shift+Tab at its first"), StepFrom(TestWorld.World, Ok, true), TEXT("Cancel"));

	// A screen that does not confine leaves Tab on the whole screen.
	Scope->SetConfineNavigation(false);
	TestEqual(TEXT("without confinement Tab from the dialog's last stop goes on to the page"), StepFrom(TestWorld.World, Cancel, false), TEXT("OnPage"));
	Scope->SetConfineNavigation(true);
	Scope->DeactivateScope();
	TestEqual(TEXT("closed, it holds nothing"), StepFrom(TestWorld.World, Cancel, false), TEXT("OnPage"));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderSelectableTest,
	"DreamGUI.Navigation.TabOrder.ASelectablesNextAndPrevAreTheTabOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderSelectableTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;
	TGuardValue<EDreamUITabOrder> HierarchyOrder(GetMutableDefault<UDreamGUISettings>()->TabOrder, EDreamUITabOrder::Hierarchy);
	TGuardValue<bool> ScreenWrap(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, true);

	// The selectable's own finder (what OnNavigate answers, and any caller asking it directly) is the Tab order too.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"));
	UDreamWidget* CellA = MakeStop(TestWorld.World, Root, TEXT("A"), -100.0f, 50.0f);
	UDreamWidget* CellB = MakeStop(TestWorld.World, Root, TEXT("B"), 100.0f, 50.0f);
	UDreamWidget* CellC = MakeStop(TestWorld.World, Root, TEXT("C"), -100.0f, -50.0f);
	UDreamWidget* CellD = MakeStop(TestWorld.World, Root, TEXT("D"), 100.0f, -50.0f);
	UUISelectable* SelectableA = CellA->GetComponent<UUISelectable>();
	UUISelectable* SelectableB = CellB->GetComponent<UUISelectable>();

	TestEqual(TEXT("Next from A is B"), SelectableA->FindSelectableOnNext(), SelectableB);
	TestEqual(TEXT("Next from B is C, the next in the tree, where the geometric order went down to D"),
		SelectableB->FindSelectableOnNext(), CellC->GetComponent<UUISelectable>());
	TestEqual(TEXT("Prev from A goes round to D"), SelectableA->FindSelectableOnPrev(), CellD->GetComponent<UUISelectable>());
	SelectableA->SetNavigationNext(EUISelectableNavigationMode::None);
	TestNull(TEXT("Next switched off answers null, as for the arrows"), SelectableA->FindNavigableOn(EDreamUINavigationDirection::Next));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabOrderPlayerScreenTest,
	"DreamGUI.Navigation.DefaultFocus.WithNoScopeTheSearchStaysOnThePlayersOwnScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabOrderPlayerScreenTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderTestLocal;
	FScopedGameWorld TestWorld;

	/*
	 * With nothing focused and no scope, the first press used to search every selectable in the world and take whichever
	 * registered first -- a panel welded to a machine in the level, a HUD, the other half of a split screen. It now looks
	 * at the player's own screen-space canvases only, and so does a Tab with nothing focused.
	 */
	UDreamWidget* WorldRoot = MakeWidget(TestWorld.World, nullptr, TEXT("WorldPanel"));
	UDreamCanvas* WorldCanvas = WorldRoot->AddComponent<UDreamCanvas>();
	UDreamWidget* ScreenRoot = MakeWidget(TestWorld.World, nullptr, TEXT("Screen"));
	UDreamCanvas* ScreenCanvas = ScreenRoot->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("a canvas for the world-space panel"), WorldCanvas) || !TestNotNull(TEXT("and one for the screen"), ScreenCanvas))
	{
		WorldRoot->DestroyWidget();
		ScreenRoot->DestroyWidget();
		return false;
	}
	WorldCanvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	ScreenCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	// The world-space control registers first and sits further up and left: what the old search would have taken.
	UDreamWidget* InWorld = MakeStop(TestWorld.World, WorldRoot, TEXT("InWorld"), -300.0f, 200.0f);
	UDreamWidget* OnScreen = MakeStop(TestWorld.World, ScreenRoot, TEXT("OnScreen"), 100.0f, -100.0f);
	TestTrue(TEXT("the panel's control is world-space UI"), InWorld->IsWorldSpaceUI());
	TestTrue(TEXT("and the screen's is screen-space UI"), OnScreen->IsScreenSpaceOverlayUI());

	TArray<UDreamWidget*> ScreenRoots;
	FDreamUITabOrder::GetPlayerScreenRoots(TestWorld.World, 0, ScreenRoots);
	TestTrue(TEXT("the player's screens include the screen-space canvas"), ScreenRoots.Contains(ScreenRoot));
	TestFalse(TEXT("and never the world-space one"), ScreenRoots.Contains(WorldRoot));
	TestEqual(TEXT("with no scope, the default control is on the player's own screen"),
		UUISelectable::FindDefaultSelectable(TestWorld.World, 0), OnScreen->GetComponent<UUISelectable>());
	TestEqual(TEXT("and a Tab with nothing focused starts there too"), StepFrom(TestWorld.World, nullptr, false), TEXT("OnScreen"));
	TestEqual(TEXT("as does a Shift+Tab"), StepFrom(TestWorld.World, nullptr, true), TEXT("OnScreen"));

	WorldRoot->DestroyWidget();
	ScreenRoot->DestroyWidget();
	return true;
}

#endif
