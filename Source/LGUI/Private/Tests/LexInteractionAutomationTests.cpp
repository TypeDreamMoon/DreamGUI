// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "Event/LexPointerPolicy.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

/*
 * The interaction layer's first tests.
 *
 * Thirty-one test files covered layout, prefabs, rendering, perspective and clipping, and not one
 * of them touched a selectable, a toggle, a cursor or the pointer's own decisions. That gap is not
 * incidental -- it is the reason a commented-out line was able to disable the keyboard navigation
 * cursor, the reason ULexWidget::Cursor could ship with no caller anywhere, and the reason the drop
 * event could be unreachable in the ordinary case without anyone noticing.
 *
 * These deliberately avoid the full pointer pipeline, which needs a world, a registered raycaster
 * and a live event system. The behaviour worth pinning is not "does the raycaster raycast" -- it is
 * the small decisions layered on top of it, and those are answerable directly.
 */

namespace LexInteractionTestLocal
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		// Editor world for the same reason the perspective fixtures use one: a canvas resizes its
		// root widget from a cached viewport size, which is a 2x2 fallback in a headless game world.
		FScopedWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W = 100.0f, float H = 100.0f)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInteractionDragSourceTest,
	"LGUI.Interaction.Pointer.TheDraggedWidgetStopsBlockingWhatIsUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInteractionDragSourceTest::RunTest(const FString& Parameters)
{
	using namespace LexInteractionTestLocal;
	// The decision that makes the drop event reachable at all. A dragged widget stays raycastable
	// and sits under the cursor, so it wins its own hit test; the drop event is then dispatched only
	// when the widget under the cursor is NOT the drag source, which is never.
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Card = MakeWidget(TestWorld.World, Root, TEXT("Card"));
	ULexWidget* CardArt = MakeWidget(TestWorld.World, Card, TEXT("CardArt"));
	ULexWidget* Slot = MakeWidget(TestWorld.World, Root, TEXT("Slot"));

	TestTrue(TEXT("The dragged widget itself is ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(Card, Card, true));
	// The drag visual is a subtree, and half of it blocking would be worse than all of it.
	TestTrue(TEXT("A descendant of the dragged widget is ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(CardArt, Card, true));
	TestFalse(TEXT("The drop target underneath is NOT ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(Slot, Card, true));
	// An ancestor is not part of the thing being dragged, so it keeps blocking as it always did.
	TestFalse(TEXT("An ancestor of the dragged widget is not ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(Root, Card, true));

	// And nothing at all changes when no drag is in flight -- this must not alter ordinary clicking.
	TestFalse(TEXT("Not dragging: the same widget is not ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(Card, Card, false));
	TestFalse(TEXT("No drag widget: nothing is ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(Card, nullptr, true));
	TestFalse(TEXT("A null hit is not ignored"),
		LexPointerPolicy::ShouldIgnoreHitWhileDragging(nullptr, Card, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInteractionCursorTest,
	"LGUI.Interaction.Pointer.TheInnermostWidgetClaimingACursorWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInteractionCursorTest::RunTest(const FString& Parameters)
{
	using namespace LexInteractionTestLocal;
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Panel = MakeWidget(TestWorld.World, Root, TEXT("Panel"));
	ULexWidget* Button = MakeWidget(TestWorld.World, Panel, TEXT("Button"));

	EMouseCursor::Type Cursor = EMouseCursor::None;
	ULexWidget* Stack[] = { Button, Panel, Root };

	// Nothing claims one yet, and "no claim" must be distinguishable from "claims the arrow" -- the
	// caller has to know whether to restore the previous cursor or write a new one.
	TestFalse(TEXT("An unclaimed stack resolves to nothing"),
		LexPointerPolicy::ResolveCursor(Stack, Cursor));

	Button->SetCursor(EMouseCursor::Hand);
	if (TestTrue(TEXT("A claim is found"), LexPointerPolicy::ResolveCursor(Stack, Cursor)))
	{
		TestEqual(TEXT("The claimed cursor is the button's"), Cursor, EMouseCursor::Hand);
	}

	// The innermost claim wins over an outer one.
	Root->SetCursor(EMouseCursor::Crosshairs);
	if (TestTrue(TEXT("A claim is still found"), LexPointerPolicy::ResolveCursor(Stack, Cursor)))
	{
		TestEqual(TEXT("The inner claim beats the outer one"), Cursor, EMouseCursor::Hand);
	}

	// Default means "no opinion", not "force the arrow". Otherwise an ordinary container sitting
	// between a button and the root would cancel the button's claim -- which is nearly every
	// hierarchy, so getting this backwards would make the feature useless exactly where it matters.
	Button->SetCursor(EMouseCursor::Default);
	if (TestTrue(TEXT("The outer claim is reached"), LexPointerPolicy::ResolveCursor(Stack, Cursor)))
	{
		TestEqual(TEXT("A Default in the way does not cancel an outer claim"), Cursor, EMouseCursor::Crosshairs);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInteractionInteractableTest,
	"LGUI.Interaction.Selectable.DisablingAnAncestorDisablesTheSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInteractionInteractableTest::RunTest(const FString& Parameters)
{
	using namespace LexInteractionTestLocal;
	// uGUI's CanvasGroup.interactable, and the escape hatch that goes with it. Nothing covered this,
	// and it decides whether a whole panel greys out -- one of the most visible behaviours there is.
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Panel = MakeWidget(TestWorld.World, Root, TEXT("Panel"));
	ULexWidget* Button = MakeWidget(TestWorld.World, Panel, TEXT("Button"));
	ULexWidget* Escapee = MakeWidget(TestWorld.World, Panel, TEXT("Escapee"));

	TestTrue(TEXT("Everything starts interactable"),
		Root->GetInteractableInHierarchy() && Panel->GetInteractableInHierarchy() && Button->GetInteractableInHierarchy());

	Panel->SetInteractable(ELexWidgetInteractableType::Disabled);
	TestFalse(TEXT("The disabled widget is not interactable"), Panel->GetInteractableInHierarchy());
	TestFalse(TEXT("Its descendant is not interactable either"), Button->GetInteractableInHierarchy());
	TestTrue(TEXT("Its ancestor is unaffected"), Root->GetInteractableInHierarchy());

	// The ignoreParentGroups escape hatch: an explicit Enabled below a Disabled wins.
	Escapee->SetInteractable(ELexWidgetInteractableType::Enabled);
	TestTrue(TEXT("An explicit Enabled overrides a disabled ancestor"), Escapee->GetInteractableInHierarchy());
	TestFalse(TEXT("Its Inherit sibling is still disabled"), Button->GetInteractableInHierarchy());

	Panel->SetInteractable(ELexWidgetInteractableType::Inherit);
	TestTrue(TEXT("Restoring the ancestor restores the subtree"), Button->GetInteractableInHierarchy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInteractionToggleGroupTest,
	"LGUI.Interaction.Toggle.AGroupKeepsExactlyOneOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInteractionToggleGroupTest::RunTest(const FString& Parameters)
{
	using namespace LexInteractionTestLocal;
	FScopedWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* GroupWidget = MakeWidget(TestWorld.World, Root, TEXT("Group"));
	UUIToggleGroup* Group = GroupWidget->AddComponent<UUIToggleGroup>();
	if (!TestNotNull(TEXT("Group created"), Group))return false;

	TArray<UUIToggle*> Toggles;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ULexWidget* W = MakeWidget(TestWorld.World, GroupWidget, *FString::Printf(TEXT("Toggle%d"), Index));
		UUIToggle* T = W->AddComponent<UUIToggle>();
		if (!TestNotNull(TEXT("Toggle created"), T))return false;
		T->SetToggleGroup(Group);
		Toggles.Add(T);
	}

	auto CountOn = [&Toggles]() { int32 N = 0; for (UUIToggle* T : Toggles)N += T->GetValue() ? 1 : 0; return N; };

	// Joining is where this starts. bIsOn defaults to TRUE and SetValue is a no-op when the value is
	// unchanged, so three toggles dropped into a group are all on and the group is never told --
	// LastSelect stays null and has nothing to switch off. A "toggle group" holding three selections
	// is not doing the one thing it exists for, so membership changes reconcile.
	TestEqual(TEXT("Joining a group leaves exactly one selected"), CountOn(), 1);
	TestTrue(TEXT("The last one to join is the selected one"), Toggles.Last()->GetValue());

	// And the ordinary path: turning one on turns the previous one off.
	Toggles[0]->SetValue(true);
	TestEqual(TEXT("Turning another on still leaves exactly one"), CountOn(), 1);
	TestTrue(TEXT("The newly set one is the one that is on"), Toggles[0]->GetValue());
	TestFalse(TEXT("The previously selected one went off"), Toggles.Last()->GetValue());
	return true;
}

#endif
