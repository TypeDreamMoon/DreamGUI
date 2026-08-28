// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UISelectable.h"
#include "Engine/World.h"

/*
 * Where focus goes when a screen opens, and where it goes back to when that screen closes.
 *
 * DreamGUI could already restrict navigation to a subtree, but nothing remembered anything: open a
 * submenu, come back, and focus fell to whichever selectable had registered first -- registration
 * order, invisible to whoever authored the screen and not necessarily the same in a packaged build.
 * A scope is the thing that holds that answer, and the stack is the order the scopes opened in.
 */

namespace DreamNavigationScopeTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float Y, float Z, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		Widget->SetRelativeLocation(FVector(0, Y, Z));
		Widget->OnRegister();
		return Widget;
	}

	/** A scope that stays put until the test pushes it, so activation order is the test's to control. */
	UDreamUINavigationScope* MakeScope(UDreamWidget* Widget)
	{
		UDreamUINavigationScope* Scope = Widget->AddComponent<UDreamUINavigationScope>();
		Scope->SetActivateWhenEnabled(false);
		return Scope;
	}

	static const FVector Right = FVector(0, 1, 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScopeStackOrderTest,
	"DreamGUI.Navigation.Scope.StackOrdersByWhenScreensOpened",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScopeStackOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScopeTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUINavigationStack* Stack = TestWorld.World->GetSubsystem<UDreamUINavigationStack>();
	if (!TestNotNull(TEXT("Navigation stack subsystem exists"), Stack))
	{
		return false;
	}

	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 0.0f, 0.0f, 800.0f, 600.0f);
	UDreamUINavigationScope* Page = MakeScope(MakeWidget(TestWorld.World, Root, TEXT("Page"), 0.0f, 0.0f, 400.0f, 400.0f));
	UDreamUINavigationScope* Dialog = MakeScope(MakeWidget(TestWorld.World, Root, TEXT("Dialog"), 0.0f, 0.0f, 200.0f, 200.0f));

	TestNull(TEXT("Nothing is active to begin with"), Stack->GetActiveScope(0));
	Page->ActivateScope();
	TestEqual(TEXT("The page is on top"), Stack->GetActiveScope(0), Page);
	Dialog->ActivateScope();
	TestEqual(TEXT("The dialog opened in front of it"), Stack->GetActiveScope(0), Dialog);

	// Re-activating an open screen raises it rather than stacking a second copy; two entries would
	// need two closes, and the first would leave a ghost of the screen still on the stack.
	Page->ActivateScope();
	TestEqual(TEXT("Re-activating the page raises it"), Stack->GetActiveScope(0), Page);
	Page->DeactivateScope();
	TestEqual(TEXT("...and one close is enough to get back to the dialog"), Stack->GetActiveScope(0), Dialog);

	Dialog->DeactivateScope();
	TestNull(TEXT("Closing the last leaves nothing active"), Stack->GetActiveScope(0));

	// Players do not share a stack, and one opening a screen must not disturb the other.
	Dialog->SetUserIndex(1);
	Page->ActivateScope();
	Dialog->ActivateScope();
	TestEqual(TEXT("Player 0 still sees the page"), Stack->GetActiveScope(0), Page);
	TestEqual(TEXT("Player 1 sees only their own dialog"), Stack->GetActiveScope(1), Dialog);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScopeConfinementTest,
	"DreamGUI.Navigation.Scope.ConfinementKeepsMovesInsideTheTopScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScopeConfinementTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScopeTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUINavigationStack* Stack = TestWorld.World->GetSubsystem<UDreamUINavigationStack>();
	if (!TestNotNull(TEXT("Navigation stack subsystem exists"), Stack))
	{
		return false;
	}

	// Two panels side by side, each with one control. Nothing restricts navigation to start with.
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 0.0f, 0.0f, 800.0f, 400.0f);
	UDreamWidget* Left = MakeWidget(TestWorld.World, Root, TEXT("Left"), -200.0f, 0.0f, 300.0f, 300.0f);
	UDreamWidget* RightPanel = MakeWidget(TestWorld.World, Root, TEXT("Right"), 200.0f, 0.0f, 300.0f, 300.0f);
	UUISelectable* InLeft = MakeWidget(TestWorld.World, Left, TEXT("LeftButton"), 0.0f, 0.0f, 80.0f, 40.0f)->AddComponent<UUISelectable>();
	UUISelectable* InRight = MakeWidget(TestWorld.World, RightPanel, TEXT("RightButton"), 0.0f, 0.0f, 80.0f, 40.0f)->AddComponent<UUISelectable>();

	TestEqual(TEXT("Unconfined, right reaches the other panel"), InLeft->FindSelectable(Right, nullptr), InRight);

	// Now the left panel is the open screen. The control on the right is still on screen and still
	// interactable -- confinement is what has to stop the move, not visibility or interactability.
	UDreamUINavigationScope* LeftScope = MakeScope(Left);
	LeftScope->ActivateScope();
	TestEqual(TEXT("Confined, right finds nothing and stays put"), InLeft->FindSelectable(Right, nullptr), InLeft);

	// A screen that does not confine leaves navigation free, which is the setting for a HUD panel
	// that takes focus without wanting to trap it.
	LeftScope->SetConfineNavigation(false);
	TestEqual(TEXT("A non-confining screen lets the move through"), InLeft->FindSelectable(Right, nullptr), InRight);

	LeftScope->SetConfineNavigation(true);
	LeftScope->DeactivateScope();
	TestEqual(TEXT("Closing the screen lets it through again"), InLeft->FindSelectable(Right, nullptr), InRight);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNavigationScopeFocusMemoryTest,
	"DreamGUI.Navigation.Scope.FocusTargetPrefersWhereItLeftOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNavigationScopeFocusMemoryTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationScopeTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 0.0f, 0.0f, 800.0f, 400.0f);
	UDreamWidget* Page = MakeWidget(TestWorld.World, Root, TEXT("Page"), 0.0f, 0.0f, 400.0f, 400.0f);
	UDreamUINavigationScope* Scope = MakeScope(Page);
	UUISelectable* First = MakeWidget(TestWorld.World, Page, TEXT("First"), -60.0f, 0.0f, 80.0f, 40.0f)->AddComponent<UUISelectable>();
	UUISelectable* Second = MakeWidget(TestWorld.World, Page, TEXT("Second"), 60.0f, 0.0f, 80.0f, 40.0f)->AddComponent<UUISelectable>();
	UUISelectable* Elsewhere = MakeWidget(TestWorld.World, Root, TEXT("Elsewhere"), 300.0f, 0.0f, 80.0f, 40.0f)->AddComponent<UUISelectable>();

	// Nothing authored, nothing remembered: the first navigable control inside the scope. Note the
	// "inside" -- the old global default would happily have handed back a control on another screen.
	TestEqual(TEXT("Falls back to a control inside the scope"), Scope->ResolveFocusTarget(), First);

	Scope->SetDesiredFocusTarget(Second);
	TestEqual(TEXT("An authored target wins over the fallback"), Scope->ResolveFocusTarget(), Second);

	Scope->RememberFocus(First);
	TestEqual(TEXT("Where it left off wins over the authored target"), Scope->ResolveFocusTarget(), First);

	Scope->SetRestoreLastFocus(false);
	TestEqual(TEXT("Unless the screen is set to always start fresh"), Scope->ResolveFocusTarget(), Second);
	Scope->SetRestoreLastFocus(true);

	// A scope pushed while focus was still on the previous screen would otherwise adopt that screen's
	// control and restore to it later, dragging focus somewhere it never was.
	Scope->RememberFocus(Elsewhere);
	TestEqual(TEXT("Focus that was never ours is not remembered"), Scope->GetRememberedFocus(), First);
	// Nor is "nothing focused" worth forgetting a real answer over.
	Scope->RememberFocus(nullptr);
	TestEqual(TEXT("Nor is an empty selection"), Scope->GetRememberedFocus(), First);

	Root->DestroyWidget();
	return true;
}

#endif
