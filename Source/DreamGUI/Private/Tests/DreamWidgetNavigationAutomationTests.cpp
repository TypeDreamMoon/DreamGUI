// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/World.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Interaction/UISelectable.h"
#include "Tests/DreamNavigationTestTypes.h"

/*
 * Per-widget navigation rules, the panel UMG puts on every widget and this framework had nowhere.
 *
 * Every navigation rule used to live on UUISelectable. That made the selectable component the price
 * of entry to navigation: a widget could be focusable (bIsFocusable and SetFocus have always
 * existed), could be given focus, and then could not be navigated away from, because the pipeline
 * resolves a move by walking up from the focused widget looking for a component that implements
 * IDreamNavigationInterface -- and only UUISelectable did. A border, a panel, a custom image with a
 * click handler was outside navigation entirely, and there was no Custom rule anywhere, so a grid
 * that wanted to compute its own neighbours had no way to say so.
 *
 * The rules now live on a UDreamWidgetNavigation that any widget can carry. What is pinned here:
 * each of the five rules answers what UMG's answers, a rule on the widget outranks the selectable's
 * own per-direction mode, and a widget whose ONLY navigation is these rules is a full participant.
 */

namespace DreamWidgetNavigationTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, const TCHAR* Name, float X, float Y)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		Widget->OnRegister();
		Widget->SetAnchoredPosition(FVector2D(X, Y));
		return Widget;
	}

	/** Run one navigation move the way the pipeline does, and report where it landed. */
	UDreamWidget* Navigate(UDreamUIBehaviour* InFrom, EDreamUINavigationDirection InDirection)
	{
		TScriptInterface<IDreamNavigationInterface> Result = nullptr;
		if (!IDreamNavigationInterface::Execute_OnNavigate(InFrom, InDirection, Result))
		{
			return nullptr;
		}
		UDreamUIBehaviour* Behaviour = Cast<UDreamUIBehaviour>(Result.GetObject());
		return Behaviour != nullptr ? Behaviour->GetWidget() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetNavigationRulesTest,
	"DreamGUI.Navigation.PerWidget.EachRuleAnswersWhatItsNameSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetNavigationRulesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetNavigationTestLocal;
	FScopedGameWorld TestWorld;

	UDreamWidget* From = MakeWidget(TestWorld.World, TEXT("From"), 0.0f, 0.0f);
	UDreamWidget* Target = MakeWidget(TestWorld.World, TEXT("Target"), 300.0f, 0.0f);

	// The accessor pair is the widget's face on the rules, and creating them must be idempotent --
	// a second call from a details panel button or from code must not leave two rule sets fighting.
	TestNull(TEXT("a plain widget carries no navigation rules"), From->GetNavigation());
	UDreamWidgetNavigation* Navigation = From->GetOrCreateNavigation();
	if (!TestNotNull(TEXT("rules can be created on any widget"), Navigation))
	{
		return false;
	}
	TestEqual(TEXT("...and are found again afterwards"), From->GetNavigation(), Navigation);
	TestEqual(TEXT("...without making a second set"), From->GetOrCreateNavigation(), Navigation);
	TestFalse(TEXT("a fresh set has no opinion about any direction"), Navigation->HasAnyRule());

	// Escape is the default and means "no opinion", which is what lets these rules sit beside a
	// selectable without the two disagreeing.
	TestFalse(TEXT("an untouched direction is not a rule"),
		Navigation->HasRuleFor(EDreamUINavigationDirection::Right));

	// Stop: the edge. Handled, and the answer is nobody.
	Navigation->SetRule(EDreamUINavigationDirection::Right, EDreamUINavigationRule::Stop);
	TestTrue(TEXT("Stop is a rule"), Navigation->HasRuleFor(EDreamUINavigationDirection::Right));
	TestTrue(TEXT("...and so the widget has an opinion now"), Navigation->HasAnyRule());
	TestNull(TEXT("Stop refuses to move"), Navigate(Navigation, EDreamUINavigationDirection::Right));

	// Explicit by pointer.
	UDreamWidgetNavigation* TargetNavigation = Target->GetOrCreateNavigation();
	Navigation->SetExplicitTarget(EDreamUINavigationDirection::Right, Target);
	TestEqual(TEXT("Explicit goes exactly where it was pointed"),
		Navigate(Navigation, EDreamUINavigationDirection::Right), Target);

	// Explicit by NAME, which is the only form a text-authored .dui can express: it can give a
	// property an asset path and never a sibling in the live tree.
	UDreamWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 0.0f, 0.0f);
	From->TrySetParent(Root, false);
	Target->TrySetParent(Root, false);
	FDreamWidgetNavigationData& RightRule = Navigation->GetNavigationData(EDreamUINavigationDirection::Right);
	RightRule.Widget = nullptr;
	RightRule.WidgetToFocus = TEXT("Target");
	TestEqual(TEXT("Explicit resolves a display name inside the same tree"),
		Navigate(Navigation, EDreamUINavigationDirection::Right), Target);

	// Clearing the target is "no opinion", not "Stop": an Explicit rule pointing at nothing would
	// pin focus in place for a reason nobody could see in the panel.
	Navigation->SetExplicitTarget(EDreamUINavigationDirection::Right, nullptr);
	TestFalse(TEXT("clearing an explicit link clears the rule with it"),
		Navigation->HasRuleFor(EDreamUINavigationDirection::Right));

	// Custom: the delegate decides. This is the rule a grid needs, where the neighbour is arithmetic
	// on an index and not a link anybody can draw in a panel.
	UDreamNavigationTargetProvider* Provider = NewObject<UDreamNavigationTargetProvider>(TestWorld.World);
	Provider->Target = Target;
	FDreamCustomWidgetNavigationDelegate Delegate;
	Delegate.BindUFunction(Provider, TEXT("Provide"));
	Navigation->SetCustomDelegate(EDreamUINavigationDirection::Down, Delegate);
	TestTrue(TEXT("binding a delegate sets the rule to Custom"),
		Navigation->GetNavigationData(EDreamUINavigationDirection::Down).Rule == EDreamUINavigationRule::Custom);
	TestEqual(TEXT("Custom goes where the delegate says"),
		Navigate(Navigation, EDreamUINavigationDirection::Down), Target);
	TestEqual(TEXT("...and the delegate was told which direction was asked for"),
		Provider->LastDirection, EDreamUINavigationDirection::Down);

	// A Custom rule whose delegate answers nothing is an honest "nowhere", not a crash and not a
	// fall-through to the scan: the screen said it would decide, and it decided.
	Provider->Target = nullptr;
	TestNull(TEXT("a Custom rule that answers nothing moves nowhere"),
		Navigate(Navigation, EDreamUINavigationDirection::Down));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetNavigationOutranksSelectableTest,
	"DreamGUI.Navigation.PerWidget.ARuleOnTheWidgetOutranksTheSelectablesOwnMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetNavigationOutranksSelectableTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetNavigationTestLocal;
	FScopedGameWorld TestWorld;

	/*
	 * Both components implement IDreamNavigationInterface, and the pipeline takes the first one it
	 * finds walking the component list -- which is the order they were added in, a property no author
	 * can see. So the tie is broken by MEANING instead: a rule that was actually authored wins, and an
	 * untouched rule set stays out of the way.
	 */
	UDreamWidget* FromWidget = MakeWidget(TestWorld.World, TEXT("From"), 0.0f, 0.0f);
	UDreamWidget* ByRule = MakeWidget(TestWorld.World, TEXT("ByRule"), 300.0f, 0.0f);
	UDreamWidget* BySelectable = MakeWidget(TestWorld.World, TEXT("BySelectable"), 600.0f, 0.0f);

	UUISelectable* Selectable = FromWidget->AddComponent<UUISelectable>();
	UUISelectable* SelectableTarget = BySelectable->AddComponent<UUISelectable>();
	UUISelectable* RuleTarget = ByRule->AddComponent<UUISelectable>();
	if (!TestNotNull(TEXT("the source carries a selectable"), Selectable)
		|| !TestNotNull(TEXT("the selectable's target carries one"), SelectableTarget)
		|| !TestNotNull(TEXT("the rule's target carries one"), RuleTarget))
	{
		return false;
	}
	Selectable->SetNavigationRight(EUISelectableNavigationMode::Explicit);
	Selectable->SetNavigationRightExplicit(SelectableTarget);
	TestEqual(TEXT("with no rule authored, the selectable's own mode answers"),
		Navigate(Selectable, EDreamUINavigationDirection::Right), BySelectable);

	UDreamWidgetNavigation* Navigation = FromWidget->GetOrCreateNavigation();
	TestEqual(TEXT("an untouched rule set changes nothing"),
		Navigate(Selectable, EDreamUINavigationDirection::Right), BySelectable);

	Navigation->SetExplicitTarget(EDreamUINavigationDirection::Right, ByRule);
	TestEqual(TEXT("an authored rule takes over"),
		Navigate(Selectable, EDreamUINavigationDirection::Right), ByRule);
	// The direction that was NOT given a rule still belongs to the selectable, so adopting the panel
	// for one direction does not silently switch the whole control over to it.
	Selectable->SetNavigationLeft(EUISelectableNavigationMode::Explicit);
	Selectable->SetNavigationLeftExplicit(SelectableTarget);
	TestEqual(TEXT("a direction with no rule is still the selectable's"),
		Navigate(Selectable, EDreamUINavigationDirection::Left), BySelectable);

	FromWidget->DestroyWidget();
	ByRule->DestroyWidget();
	BySelectable->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetNavigationWithoutSelectableTest,
	"DreamGUI.Navigation.PerWidget.AWidgetWithNoSelectableCanBeNavigatedToAndFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetNavigationWithoutSelectableTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetNavigationTestLocal;
	FScopedGameWorld TestWorld;

	// The whole point of the feature: neither of these two widgets is a button.
	UDreamWidget* PanelA = MakeWidget(TestWorld.World, TEXT("PanelA"), 0.0f, 0.0f);
	UDreamWidget* PanelB = MakeWidget(TestWorld.World, TEXT("PanelB"), 300.0f, 0.0f);
	UDreamWidgetNavigation* NavA = PanelA->GetOrCreateNavigation();
	UDreamWidgetNavigation* NavB = PanelB->GetOrCreateNavigation();
	if (!TestNotNull(TEXT("rules on a widget with no selectable"), NavA)
		|| !TestNotNull(TEXT("...on both of them"), NavB))
	{
		return false;
	}

	// Focusable, because focus is the precondition for navigation and SetFocus refuses a widget that
	// is not. UUISelectable does this in its own OnRegister for exactly the same reason.
	TestTrue(TEXT("carrying navigation rules makes a widget focusable"), PanelA->GetIsFocusable());

	// Navigable TO: what the pipeline asks before it will land a move on something.
	TestTrue(TEXT("a navigation-only widget accepts focus"),
		IDreamNavigationInterface::Execute_CanNavigateHere(NavB));
	TestTrue(TEXT("...and can be switched off like anything else"), NavB->bCanNavigateHere);
	NavB->bCanNavigateHere = false;
	TestFalse(TEXT("a widget that refuses navigation is not a target"),
		IDreamNavigationInterface::Execute_CanNavigateHere(NavB));
	NavB->bCanNavigateHere = true;

	// Navigable FROM, and onto another navigation-only widget: the move is delivered as the BEHAVIOUR
	// that will receive it, which is the change that made a non-selectable landing spot expressible
	// at all -- a UUISelectable* return could not name one.
	NavA->SetExplicitTarget(EDreamUINavigationDirection::Right, PanelB);
	TestEqual(TEXT("a move from one navigation-only widget lands on another"),
		Navigate(NavA, EDreamUINavigationDirection::Right), PanelB);

	// And a refusing target is not landed on, rather than being landed on and stranding focus.
	NavB->bCanNavigateHere = false;
	TestNull(TEXT("a target that refuses navigation is not landed on"),
		Navigate(NavA, EDreamUINavigationDirection::Right));

	PanelA->DestroyWidget();
	PanelB->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetNavigationCustomBoundaryTest,
	"DreamGUI.Navigation.Rules.CustomBoundaryIsAskedOnlyWhenTheMoveRunsOffTheEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetNavigationCustomBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetNavigationTestLocal;
	FScopedGameWorld TestWorld;

	// Two neighbours side by side, and a third widget somewhere else entirely that only the delegate
	// knows about -- "the next page", in the shape the rule exists for.
	UDreamWidget* Root = MakeWidget(TestWorld.World, TEXT("Root"), 0.0f, 0.0f);
	UDreamWidget* From = MakeWidget(TestWorld.World, TEXT("From"), 0.0f, 0.0f);
	UDreamWidget* Neighbour = MakeWidget(TestWorld.World, TEXT("Neighbour"), 200.0f, 0.0f);
	UDreamWidget* OffPage = MakeWidget(TestWorld.World, TEXT("OffPage"), 0.0f, -4000.0f);
	From->TrySetParent(Root, false);
	Neighbour->TrySetParent(Root, false);
	OffPage->TrySetParent(Root, false);

	UDreamWidgetNavigation* Navigation = From->GetOrCreateNavigation();
	Neighbour->GetOrCreateNavigation();
	OffPage->GetOrCreateNavigation();

	UDreamNavigationTargetProvider* Provider = NewObject<UDreamNavigationTargetProvider>(TestWorld.World);
	Provider->Target = OffPage;
	FDreamCustomWidgetNavigationDelegate Delegate;
	Delegate.BindUFunction(Provider, TEXT("Provide"));

	Navigation->SetNavigationRuleCustomBoundary(EDreamUINavigationDirection::Right, Delegate);
	Navigation->SetNavigationRuleCustomBoundary(EDreamUINavigationDirection::Left, Delegate);
	TestTrue(TEXT("binding through the boundary setter sets the boundary rule"),
		Navigation->GetNavigationData(EDreamUINavigationDirection::Right).Rule == EDreamUINavigationRule::CustomBoundary);
	TestTrue(TEXT("and it counts as a rule, so the widget answers its own moves"),
		Navigation->HasRuleFor(EDreamUINavigationDirection::Right));

	// The whole distinction from Custom: there IS a neighbour to the right, so the delegate must not
	// be consulted. A rule that answered here would hijack every step inside the page.
	TestEqual(TEXT("with a neighbour in that direction the scan wins"),
		Navigate(Navigation, EDreamUINavigationDirection::Right), Neighbour);
	TestEqual(TEXT("...and the delegate was never asked"), Provider->CallCount, 0);

	// Left has nothing beside it: this move leaves the area, which is the case the rule is for.
	TestEqual(TEXT("running off the edge hands the move to the delegate"),
		Navigate(Navigation, EDreamUINavigationDirection::Left), OffPage);
	TestEqual(TEXT("...which was asked exactly once"), Provider->CallCount, 1);
	TestEqual(TEXT("...and told which way the move was going"),
		Provider->LastDirection, EDreamUINavigationDirection::Left);

	// A delegate with no answer leaves the move where the scan left it -- nowhere -- rather than
	// inventing one. Same honesty as an unbound Custom, minus the pinning.
	Provider->Target = nullptr;
	TestNull(TEXT("a boundary delegate that answers nothing moves nowhere"),
		Navigate(Navigation, EDreamUINavigationDirection::Left));

	// Unbinding clears back to Escape, so "remove the rule" never leaves one pointing at nothing.
	Navigation->SetNavigationRuleCustomBoundary(EDreamUINavigationDirection::Left,
		FDreamCustomWidgetNavigationDelegate());
	TestFalse(TEXT("unbinding clears the rule"), Navigation->HasRuleFor(EDreamUINavigationDirection::Left));

	Root->DestroyWidget();
	return true;
}

#endif
