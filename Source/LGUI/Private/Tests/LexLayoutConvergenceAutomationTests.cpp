// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

/*
 * The manager loops until its dirty set empties, capped at 32 passes. Every pass past the first is work
 * the previous pass caused rather than work the user asked for, so the pass count is the honest measure
 * of whether the arrange/commit split bought anything. It used to be visible only under a debug macro,
 * which meant "converged" and "ran several times and happened to agree" looked identical.
 *
 * These pin the healthy number at one, for a nested tree deep enough that a re-dirtying pass would have
 * to show up.
 */

namespace LexLayoutConvergenceTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static ULexWidget* MakeChild(UWorld* World, ULexWidget* Parent, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(Parent ? (UObject*)Parent : (UObject*)World);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	static void FillSlot(ULexWidget* Widget)
	{
		if (ULexPanelSlot* Slot = Widget->GetPanelSlot())
		{
			Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(ELexPanelVerticalAlignment::Fill);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutNestedTreeConvergesInOnePassTest,
	"LGUI.Layout.Convergence.NestedTreeSettlesInOnePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutNestedTreeConvergesInOnePassTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutConvergenceTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// Root overlay -> stack box -> two leaves. Three nested panels, each of which writes geometry the
	// next one reads, which is exactly the shape that used to cost extra passes.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	ULexWidget* Mid = MakeChild(TestWorld.World, Root, 20.0f, 10.0f);
	ULexWidget* LeafA = MakeChild(TestWorld.World, Mid, 5.0f, 5.0f);
	ULexWidget* LeafB = MakeChild(TestWorld.World, Mid, 7.0f, 7.0f);

	if (!Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>()
		|| !Mid->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>())
	{
		return false;
	}
	Root->OnRegister();
	Mid->OnRegister();
	LeafA->OnRegister();
	LeafB->OnRegister();
	FillSlot(Mid);
	FillSlot(LeafA);
	FillSlot(LeafB);

	// Settle the tree first: registration churn is not what this measures.
	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	// One authored change, one tick.
	Root->SetWidth(500.0f);
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("A resize settles in a single layout pass"), Manager->GetLastLayoutPassCount(), 1);
	// The count only means something if the tree actually moved: a pass that laid nothing out would also
	// be one pass. Mid fills the overlay, and the leaves fill the vertical box's cross axis - the main
	// axis is theirs to keep, so only the width is the box's to hand down.
	TestEqual(TEXT("...and the mid panel actually followed the root"), Mid->GetSize(), FVector2D(500.0, 300.0));
	TestEqual(TEXT("...and leaf A took the box's full width"), LeafA->GetSize().X, 500.0);
	TestEqual(TEXT("...and so did leaf B"), LeafB->GetSize().X, 500.0);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexLayoutIdleFrameCostsNoPassTest,
	"LGUI.Layout.Convergence.IdleFrameRunsNoPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexLayoutIdleFrameCostsNoPassTest::RunTest(const FString& Parameters)
{
	using namespace LexLayoutConvergenceTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	ULexWidget* Child = MakeChild(TestWorld.World, Root, 20.0f, 10.0f);
	if (!Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>())
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();
	FillSlot(Child);

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	// A settled tree must not re-enter the loop at all: the counter only moves when the dirty set is
	// non-empty, so a pass here would mean the last one left something behind.
	Manager->TickLexUI(0.016f);
	const int32 AfterIdle = Manager->GetLastLayoutPassCount();
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("An idle frame does not run another pass"), Manager->GetLastLayoutPassCount(), AfterIdle);

	Root->DestroyWidget();
	return true;
}

#endif
