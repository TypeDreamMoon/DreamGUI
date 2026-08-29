// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

/*
 * Regression coverage for the ordering/sorting defect batch:
 *  - Overlay ignored slot ZOrder entirely (page layering came out reversed vs authored intent);
 *  - prefab reload overwrote restored SiblingIndex with tail indices on cross-parent moves;
 *  - EnsureUIChildrenSorted used an unstable sort, so duplicate keys oscillated every refresh;
 *  - WidgetSwitcher clamped a requested index to 0 while pages were not attached yet, losing intent;
 *  - UIToggleGroup cached its sort and returned stale hierarchy order after reorders.
 */

namespace DreamOrderingTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	// Outer is the WORLD, matching how the prefab system creates widgets (NewObject with OwnerObject as
	// outer) — the deserializer's reuse gate requires GetOuter() == OwnerObject, so a parent-outered
	// fixture widget would be "incompatible" and silently recreated on reload.
	UDreamWidget* MakeChild(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W = 20.0f, float H = 10.0f)
	{
		UDreamWidget* Child = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Child->SetDisplayName(Name);
		Child->SetWidth(W);
		Child->SetHeight(H);
		Child->TrySetParent(Parent, false);
		return Child;
	}

	TArray<FString> ChildNames(UDreamWidget* Parent)
	{
		TArray<FString> Names;
		for (UDreamWidget* Child : Parent->GetChildren())
		{
			Names.Add(IsValid(Child) ? Child->GetDisplayName() : TEXT("<invalid>"));
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOverlayHonorsZOrderTest,
	"DreamGUI.Ordering.OverlayHonorsZOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamOverlayHonorsZOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamOrderingTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetWidth(200.0f);
	Root->SetHeight(100.0f);
	// Authored like the project's page stack: sibling order Prompt, Round, Resolution with DESCENDING
	// ZOrder — the authored intent is Resolution lowest, Prompt on top.
	UDreamWidget* Prompt = MakeChild(TestWorld.World, Root, TEXT("Prompt"));
	UDreamWidget* Round = MakeChild(TestWorld.World, Root, TEXT("Round"));
	UDreamWidget* Resolution = MakeChild(TestWorld.World, Root, TEXT("Resolution"));
	TestNotNull(TEXT("Overlay created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>());
	Prompt->GetPanelSlot()->SetZOrder(100);
	Round->GetPanelSlot()->SetZOrder(90);
	Resolution->GetPanelSlot()->SetZOrder(80);
	Root->OnRegister();
	Prompt->OnRegister();
	Round->OnRegister();
	Resolution->OnRegister();

	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);

	// Paint order is sibling order: ascending ZOrder must come out first-to-last.
	const TArray<FString> Names = ChildNames(Root);
	TestEqual(TEXT("Lowest ZOrder paints first"), Names[0], FString(TEXT("Resolution")));
	TestEqual(TEXT("Middle ZOrder paints second"), Names[1], FString(TEXT("Round")));
	TestEqual(TEXT("Highest ZOrder paints last"), Names[2], FString(TEXT("Prompt")));

	// Equal ZOrder keeps sibling order (stable): zero everything, reorder must not change.
	Prompt->GetPanelSlot()->SetZOrder(0);
	Round->GetPanelSlot()->SetZOrder(0);
	Resolution->GetPanelSlot()->SetZOrder(0);
	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);
	TestEqual(TEXT("Equal ZOrder keeps current order"), ChildNames(Root)[0], FString(TEXT("Resolution")));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamChildrenSortIsStableTest,
	"DreamGUI.Ordering.DuplicateSiblingIndicesStayStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamChildrenSortIsStableTest::RunTest(const FString& Parameters)
{
	using namespace DreamOrderingTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* A = MakeChild(TestWorld.World, Root, TEXT("A"));
	UDreamWidget* B = MakeChild(TestWorld.World, Root, TEXT("B"));
	UDreamWidget* C = MakeChild(TestWorld.World, Root, TEXT("C"));
	UDreamWidget* D = MakeChild(TestWorld.World, Root, TEXT("D"));

	// Force the duplicate-key state legacy assets can carry (C and D both claim index 2) through the
	// prefab-facing raw setter, then sort repeatedly: the order must settle once and never oscillate
	// (the unstable sort deterministically swapped the equal-key pair on every refresh).
	D->RestoreSiblingIndexFromPrefab(2);
	const TArray<FString> FirstPass = ChildNames(Root);
	const TArray<FString> SecondPass = ChildNames(Root);
	UDreamWidget* Dummy = MakeChild(TestWorld.World, Root, TEXT("E"));
	Dummy->RestoreSiblingIndexFromPrefab(99);
	const TArray<FString> ThirdPass = ChildNames(Root);

	TestEqual(TEXT("Equal keys keep order across resorts (1v2)"), FirstPass, SecondPass);
	TestEqual(TEXT("C precedes D (insertion order) on first sort"), FirstPass[2], FString(TEXT("C")));
	TestEqual(TEXT("C still precedes D after another resort"), ThirdPass[2], FString(TEXT("C")));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetSwitcherKeepsRequestedIndexTest,
	"DreamGUI.Ordering.WidgetSwitcherKeepsRequestedIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetSwitcherKeepsRequestedIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamOrderingTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetWidth(100.0f);
	Root->SetHeight(100.0f);
	UDreamLayoutContainerWidgetSwitcher* Switcher = Root->CreateNewLayoutContainer<UDreamLayoutContainerWidgetSwitcher>();
	TestNotNull(TEXT("Switcher created"), Switcher);
	Root->OnRegister();

	// Request page 2 while no pages exist — the old clamp collapsed this to 0 permanently.
	Switcher->SetActiveWidgetIndex(2);

	UDreamWidget* Page0 = MakeChild(TestWorld.World, Root, TEXT("Page0"));
	UDreamWidget* Page1 = MakeChild(TestWorld.World, Root, TEXT("Page1"));
	UDreamWidget* Page2 = MakeChild(TestWorld.World, Root, TEXT("Page2"));
	Page0->OnRegister();
	Page1->OnRegister();
	Page2->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(Root);
	UDreamWidget::RebuildLayoutImmediately(Root);

	TestEqual(TEXT("Late-attached pages satisfy the requested index"),
		Switcher->GetActiveWidget(), Page2);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupTracksHierarchyOrderTest,
	"DreamGUI.Ordering.ToggleGroupTracksHierarchyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleGroupTracksHierarchyOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamOrderingTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	UDreamWidget* First = MakeChild(TestWorld.World, Root, TEXT("First"));
	UDreamWidget* Second = MakeChild(TestWorld.World, Root, TEXT("Second"));
	Root->OnRegister();
	First->OnRegister();
	Second->OnRegister();

	UUIToggle* ToggleA = First->AddComponent<UUIToggle>();
	UUIToggle* ToggleB = Second->AddComponent<UUIToggle>();
	TestNotNull(TEXT("Toggle A created"), ToggleA);
	TestNotNull(TEXT("Toggle B created"), ToggleB);
	UUIToggleGroup* Group = NewObject<UUIToggleGroup>(Root);
	Group->AddToggleComponent(ToggleA);
	Group->AddToggleComponent(ToggleB);

	TestEqual(TEXT("Initial order: A first"), Group->GetToggleIndex(ToggleA), 0);
	TestEqual(TEXT("Initial order: B second"), Group->GetToggleIndex(ToggleB), 1);

	// Reorder the hierarchy: the group must observe the new order instead of a cached one.
	Second->SetSiblingIndex(0);
	TestEqual(TEXT("After reorder: B first"), Group->GetToggleIndex(ToggleB), 0);
	TestEqual(TEXT("After reorder: A second"), Group->GetToggleIndex(ToggleA), 1);

	Root->DestroyWidget();
	return true;
}

#endif
