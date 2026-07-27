// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexVisualEmpty.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "LexUIBPLibrary.h"

/*
 * ULexWidget::AddChild and the creation verbs that feed it.
 *
 * The interesting cases are all about what AddChild must NOT do. It must not reach for the obvious
 * one-liner, because TrySetParent's same-parent branch forces an authored-geometry recapture and
 * would quietly promote an arranged rect into the saved prefab. It must not express a reorder as a
 * detach and a re-attach, because detaching destroys the slot. And it must not half-apply a
 * refusal.
 */

namespace LexAddChildTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexConstructWidgetParksTest,
	"LGUI.Widget.AddChild.ConstructedWidgetIsParkedUntilAdded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexConstructWidgetParksTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!Manager)
	{
		AddError(TEXT("No manager for the test world."));
		return false;
	}

	ULexWidget* Made = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Made"), ULexVisualEmpty::StaticClass());
	if (!TestNotNull(TEXT("ConstructWidget returns a widget"), Made))return false;

	TestEqual(TEXT("It is named as asked"), Made->GetDisplayName(), FString(TEXT("Made")));
	TestNotNull(TEXT("It got the requested visual"), Made->GetVisual());
	TestTrue(TEXT("It is registered, so it is anchored and can be attached properly later"), Made->HasRegistered());
	TestTrue(TEXT("The manager is holding it"), Manager->IsWidgetParked(Made));
	TestFalse(TEXT("It is not on screen"), Made->GetWidgetActiveInHierarchy());

	// Adding it is what brings it to life, and is also what stops the manager holding it: a widget
	// with a parent is reachable through that parent and no longer needs its own anchor.
	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 300.0f, 300.0f);
	Panel->OnRegister();
	Panel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	TestNotNull(TEXT("Adding it returns a slot"), Panel->AddChild(Made));
	TestFalse(TEXT("It is no longer parked"), Manager->IsWidgetParked(Made));
	TestTrue(TEXT("It is now active"), Made->GetWidgetActiveInHierarchy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexConstructWidgetKeepsAuthoredInactiveTest,
	"LGUI.Widget.AddChild.AddingDoesNotForceAWidgetOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexConstructWidgetKeepsAuthoredInactiveTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;

	// Parking borrows the active flag to mean "not added yet", so it has to give back exactly what it
	// took. A prefab deliberately saved hidden must still be hidden once it is added -- otherwise the
	// mechanism that keeps a widget off screen before adding would switch on things nobody asked for.
	ULexWidget* Hidden = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Hidden"), nullptr);
	if (!TestNotNull(TEXT("ConstructWidget returns a widget"), Hidden))return false;
	Hidden->SetWidgetActive(false);//as if authored hidden, while parked

	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 300.0f, 300.0f);
	Panel->OnRegister();
	Panel->AddChild(Hidden);
	TestFalse(TEXT("A widget switched off before being added stays off"), Hidden->GetWidgetActive());

	ULexWidget* Shown = ULexUIBPLibrary::ConstructWidget(TestWorld.World, TEXT("Shown"), nullptr);
	if (!TestNotNull(TEXT("ConstructWidget returns a widget"), Shown))return false;
	Panel->AddChild(Shown);
	TestTrue(TEXT("An ordinary widget is switched on when added"), Shown->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAddChildReorderTest,
	"LGUI.Widget.AddChild.ReorderKeepsTheSlotAndTheAuthoredGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAddChildReorderTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 300.0f, 300.0f);
	ULexWidget* A = MakeWidget(TestWorld.World, Panel, TEXT("A"), 100.0f, 40.0f);
	ULexWidget* B = MakeWidget(TestWorld.World, Panel, TEXT("B"), 100.0f, 40.0f);
	ULexWidget* C = MakeWidget(TestWorld.World, Panel, TEXT("C"), 100.0f, 40.0f);
	Panel->OnRegister();
	Panel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();

	ULexPanelSlot* SlotBefore = C->GetPanelSlot();
	if (!TestNotNull(TEXT("C has a slot"), SlotBefore))return false;
	SlotBefore->SetPadding(FMargin(3.0f, 7.0f, 11.0f, 13.0f));

	// Let a real layout pass arrange the children, so the live rect is no longer the authored one.
	// This is the state in which the naive implementation does its damage.
	ULexWidget::MarkLayoutForRebuild(Panel);
	ULexWidget::RebuildLayoutImmediately(Panel);
	const FVector2f AuthoredBefore = SlotBefore->GetAuthoredDesiredSizeFallback();

	// Move C to the front. This is a reorder, not a move between parents.
	ULexPanelSlot* Returned = Panel->AddChild(C, 0);

	TestTrue(TEXT("The same slot object comes back"), Returned == SlotBefore && C->GetPanelSlot() == SlotBefore);
	TestTrue(TEXT("The authored padding survives"), SlotBefore->Padding == FMargin(3.0f, 7.0f, 11.0f, 13.0f));
	// TrySetParent's same-parent branch forces CaptureAuthoredGeometry(true). Routing a reorder
	// through it would overwrite the authored size with whatever the layout pass just arranged, and
	// that overwrite is what gets saved.
	TestTrue(TEXT("The authored size is not overwritten by the arranged one"),
		SlotBefore->GetAuthoredDesiredSizeFallback().Equals(AuthoredBefore));

	TestEqual(TEXT("C is now first"), Panel->GetChildren().IndexOfByKey(C), 0);
	TestEqual(TEXT("A follows"), Panel->GetChildren().IndexOfByKey(A), 1);
	TestEqual(TEXT("B follows A"), Panel->GetChildren().IndexOfByKey(B), 2);
	TestEqual(TEXT("Sibling indices agree with the order"), C->GetSiblingIndex(), 0);
	TestEqual(TEXT("A's sibling index moved with it"), A->GetSiblingIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAddChildInsertTest,
	"LGUI.Widget.AddChild.InsertsAtTheRequestedIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAddChildInsertTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 300.0f, 300.0f);
	ULexWidget* A = MakeWidget(TestWorld.World, Panel, TEXT("A"), 100.0f, 40.0f);
	ULexWidget* B = MakeWidget(TestWorld.World, Panel, TEXT("B"), 100.0f, 40.0f);
	Panel->OnRegister();
	Panel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();

	ULexWidget* C = MakeWidget(TestWorld.World, nullptr, TEXT("C"), 100.0f, 40.0f);
	C->OnRegister();
	ULexPanelSlot* Slot = Panel->AddChild(C, 1);
	TestNotNull(TEXT("Adding under a panel yields a slot"), Slot);

	TestEqual(TEXT("A stays first"), Panel->GetChildren().IndexOfByKey(A), 0);
	TestEqual(TEXT("C lands where it was asked to"), Panel->GetChildren().IndexOfByKey(C), 1);
	TestEqual(TEXT("B is displaced, not overwritten"), Panel->GetChildren().IndexOfByKey(B), 2);
	TestEqual(TEXT("B's sibling index follows the displacement"), B->GetSiblingIndex(), 2);

	// Default index appends, like UMG.
	ULexWidget* D = MakeWidget(TestWorld.World, nullptr, TEXT("D"), 100.0f, 40.0f);
	D->OnRegister();
	Panel->AddChild(D);
	TestEqual(TEXT("No index means the end"), Panel->GetChildren().IndexOfByKey(D), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAddChildRefusalTest,
	"LGUI.Widget.AddChild.RefusalChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAddChildRefusalTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* SizeBox = MakeWidget(TestWorld.World, nullptr, TEXT("SizeBox"), 300.0f, 300.0f);
	ULexWidget* Occupant = MakeWidget(TestWorld.World, SizeBox, TEXT("Occupant"), 100.0f, 40.0f);
	SizeBox->OnRegister();
	SizeBox->CreateNewLayoutContainer<ULexLayoutContainerSizeBox>();
	ULexPanelSlot* OccupantSlot = Occupant->GetPanelSlot();

	ULexWidget* OtherPanel = MakeWidget(TestWorld.World, nullptr, TEXT("OtherPanel"), 300.0f, 300.0f);
	ULexWidget* Rejected = MakeWidget(TestWorld.World, OtherPanel, TEXT("Rejected"), 100.0f, 40.0f);
	OtherPanel->OnRegister();
	OtherPanel->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();

	// A size box takes one child. The capacity check has to happen before the child is detached from
	// where it currently lives, or a refused add leaves it parented to nothing.
	TestNull(TEXT("A full single-child panel refuses"), SizeBox->AddChild(Rejected));
	TestTrue(TEXT("The refused child keeps its parent"), Rejected->GetParent() == OtherPanel);
	TestTrue(TEXT("The refused child keeps its slot"), IsValid(Rejected->GetPanelSlot()));
	TestEqual(TEXT("The full panel is unchanged"), SizeBox->GetChildren().Num(), 1);
	TestTrue(TEXT("The sitting occupant is untouched"), Occupant->GetPanelSlot() == OccupantSlot);

	// Cycles and nonsense are refused the same way.
	TestNull(TEXT("A widget cannot adopt itself"), OtherPanel->AddChild(OtherPanel));
	TestNull(TEXT("A widget cannot adopt its own ancestor"), Rejected->AddChild(OtherPanel));
	TestNull(TEXT("Null is refused"), OtherPanel->AddChild(nullptr));
	TestTrue(TEXT("The hierarchy survived all of that"),
		Rejected->GetParent() == OtherPanel && OtherPanel->GetParent() == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAddChildNoPanelTest,
	"LGUI.Widget.AddChild.APlainWidgetTakesChildrenWithoutSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAddChildNoPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexAddChildTestLocal;
	FScopedGameWorld TestWorld;

	// UMG cannot express this: there, only a UPanelWidget has children at all. Here a widget with no
	// layout container is the ordinary case, and it holds children that are positioned by their own
	// anchors. A null return therefore means "no slots here", not "refused" -- the child really was
	// added, and that is why the doc comment tells callers to check the parent to tell them apart.
	ULexWidget* Plain = MakeWidget(TestWorld.World, nullptr, TEXT("Plain"), 300.0f, 300.0f);
	Plain->OnRegister();
	ULexWidget* Child = MakeWidget(TestWorld.World, nullptr, TEXT("Child"), 100.0f, 40.0f);
	Child->OnRegister();

	TestNull(TEXT("No panel means no slot to return"), Plain->AddChild(Child));
	TestTrue(TEXT("But the child really was added"), Child->GetParent() == Plain);
	TestEqual(TEXT("The parent knows about it"), Plain->GetChildren().Num(), 1);
	TestNull(TEXT("And it has no slot"), Child->GetPanelSlot());

	// Give the parent a panel afterwards and the existing child acquires a slot, which is the
	// arrangement AddChild has to cope with when it is asked to reorder later.
	Plain->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	TestNotNull(TEXT("A container hands out slots to children already present"), Child->GetPanelSlot());
	TestNotNull(TEXT("And a reorder now returns that slot"), Plain->AddChild(Child, 0));
	return true;
}

#endif
