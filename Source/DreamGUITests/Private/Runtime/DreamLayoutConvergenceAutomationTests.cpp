// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
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

namespace DreamLayoutConvergenceTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static UDreamWidget* MakeChild(UWorld* World, UDreamWidget* Parent, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(Parent ? (UObject*)Parent : (UObject*)World);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	static void FillSlot(UDreamWidget* Widget)
	{
		if (UDreamPanelSlot* Slot = Widget->GetPanelSlot())
		{
			Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutNestedTreeConvergesInOnePassTest,
	"DreamGUI.Layout.Convergence.NestedTreeSettlesInOnePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutNestedTreeConvergesInOnePassTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConvergenceTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// Root overlay -> stack box -> two leaves. Three nested panels, each of which writes geometry the
	// next one reads, which is exactly the shape that used to cost extra passes.
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	UDreamWidget* Mid = MakeChild(TestWorld.World, Root, 20.0f, 10.0f);
	UDreamWidget* LeafA = MakeChild(TestWorld.World, Mid, 5.0f, 5.0f);
	UDreamWidget* LeafB = MakeChild(TestWorld.World, Mid, 7.0f, 7.0f);

	if (!Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>()
		|| !Mid->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>())
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
	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	// One authored change, one tick.
	Root->SetWidth(500.0f);
	Manager->TickDreamUI(0.016f);

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
	FDreamLayoutIdleFrameCostsNoPassTest,
	"DreamGUI.Layout.Convergence.IdleFrameRunsNoPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutIdleFrameCostsNoPassTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutConvergenceTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	UDreamWidget* Child = MakeChild(TestWorld.World, Root, 20.0f, 10.0f);
	if (!Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>())
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();
	FillSlot(Child);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);

	// A settled tree must not re-enter the loop at all: the counter only moves when the dirty set is
	// non-empty, so a pass here would mean the last one left something behind.
	Manager->TickDreamUI(0.016f);
	const int32 AfterIdle = Manager->GetLastLayoutPassCount();
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("An idle frame does not run another pass"), Manager->GetLastLayoutPassCount(), AfterIdle);

	Root->DestroyWidget();
	return true;
}

#endif
