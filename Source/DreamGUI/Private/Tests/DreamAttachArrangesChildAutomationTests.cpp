// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

/*
 * Reported from the prefab editor: dragging a Button out of the palette into an Overlay leaves it at its
 * authored 100x30 instead of filling the panel, and changing the slot alignment afterwards does nothing
 * either. Slot defaults are Fill on both axes, so "arranged at all" and "still 100x30" are the same
 * question.
 *
 * These reproduce the shape without the editor: a panel that already exists, a child attached to it
 * afterwards, and NOTHING marking the layout by hand - whatever invalidation exists has to come out of
 * the attach itself. Both registration orders are covered because the editor's prefab load and the
 * ordinary construction path do not agree on which comes first.
 */

namespace DreamAttachArrangesChildTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static UDreamWidget* MakeRootWithOverlay(UWorld* World)
	{
		UDreamWidget* Root = NewObject<UDreamWidget>(World);
		Root->SetWidth(1920.0f);
		Root->SetHeight(1080.0f);
		Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
		Root->OnRegister();
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachArrangesChildRegisterAfterTest,
	"DreamGUI.Layout.Attach.AttachThenRegisterArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachArrangesChildRegisterAfterTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickDreamUI(0.016f);

	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	if (!TestTrue(TEXT("Child attaches"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	Child->OnRegister();

	// No MarkLayoutForRebuild here on purpose - the attach is the whole event, exactly as in the editor.
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("A newly attached child is arranged by the panel it landed in"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachArrangesChildRegisterBeforeTest,
	"DreamGUI.Layout.Attach.RegisterThenAttachArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachArrangesChildRegisterBeforeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickDreamUI(0.016f);

	// The prefab loader builds and registers a whole hierarchy before parenting its root, which is the
	// order the palette drop actually takes.
	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	Child->OnRegister();
	if (!TestTrue(TEXT("Child attaches"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("A child registered before it was attached is still arranged"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachPrefabShapedSubtreeTest,
	"DreamGUI.Layout.Attach.PrefabShapedSubtreeIsArranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachPrefabShapedSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickDreamUI(0.016f);

	// WidgetSerializer_Deserialize builds the whole subtree unregistered, parents the created root into
	// the target, and only then walks AllWidgetArray calling OnRegister. A palette Button is exactly this
	// shape: a root with one Text child.
	UDreamWidget* Button = NewObject<UDreamWidget>(Root);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	UDreamWidget* Label = NewObject<UDreamWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);

	Button->SetParent(Root, false);

	Button->OnRegister();
	Label->OnRegister();

	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("A prefab-shaped subtree dropped into a panel is arranged"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachUnderStretchedPanelTest,
	"DreamGUI.Layout.Attach.StretchedPanelArrangesADroppedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachUnderStretchedPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// UDreamUIPrefabFactory gives a new prefab root stretch anchors and a zero size delta, so its size is
	// its parent's - the prefab scene's root agent - rather than anything authored. Every repro above
	// used a fixed-size panel, which is the one thing the real case is not.
	UDreamWidget* Agent = NewObject<UDreamWidget>(TestWorld.World);
	Agent->SetWidth(1920.0f);
	Agent->SetHeight(1080.0f);
	Agent->OnRegister();

	UDreamWidget* Panel = NewObject<UDreamWidget>(Agent);
	Panel->SetParent(Agent, false);
	Panel->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Panel->SetAnchoredPosition(FVector2D::ZeroVector);
	Panel->SetSizeDelta(FVector2D::ZeroVector);
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	Panel->OnRegister();
	Manager->TickDreamUI(0.016f);

	if (!TestEqual(TEXT("The stretched panel takes the agent's size"), Panel->GetSize(), FVector2D(1920.0, 1080.0)))
	{
		Agent->DestroyWidget();
		return false;
	}

	UDreamWidget* Button = NewObject<UDreamWidget>(Panel);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	UDreamWidget* Label = NewObject<UDreamWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);
	Button->SetParent(Panel, false);
	Button->OnRegister();
	Label->OnRegister();
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("A child dropped into a stretched panel fills it"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Agent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStretchedPanelFollowsAgentResizeTest,
	"DreamGUI.Layout.Attach.StretchedPanelRearrangesWhenItsOwnSizeChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStretchedPanelFollowsAgentResizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Agent = NewObject<UDreamWidget>(TestWorld.World);
	Agent->SetWidth(800.0f);
	Agent->SetHeight(600.0f);
	Agent->OnRegister();
	UDreamWidget* Panel = NewObject<UDreamWidget>(Agent);
	Panel->SetParent(Agent, false);
	Panel->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Panel->SetAnchoredPosition(FVector2D::ZeroVector);
	Panel->SetSizeDelta(FVector2D::ZeroVector);
	Panel->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	Panel->OnRegister();
	UDreamWidget* Child = NewObject<UDreamWidget>(Panel);
	Child->SetWidth(10.0f);
	Child->SetHeight(10.0f);
	Child->SetParent(Panel, false);
	Child->OnRegister();
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("The child starts filled to the agent size"), Child->GetSize(), FVector2D(800.0, 600.0));

	// A stretched panel's size changes through anchor propagation, not through its own setter. If that
	// path does not invalidate the panel's layout, the panel keeps arranging against the old size.
	Agent->SetWidth(1920.0f);
	Agent->SetHeight(1080.0f);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The panel itself followed"), Panel->GetSize(), FVector2D(1920.0, 1080.0));
	TestEqual(TEXT("...and re-arranged its child against the new size"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Agent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachInEditorWorldTest,
	"DreamGUI.Layout.Attach.EditorWorldArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachInEditorWorldTest::RunTest(const FString& Parameters)
{
	// FDreamUIPrefabScene builds its world as EWorldType::Editor (DreamUIPrefabScene.cpp:30), and several
	// DreamGUI paths branch on IsGameWorld. Same fixture, the world type the prefab editor actually uses.
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	ON_SCOPE_EXIT{ if (World) { World->DestroyWorld(false); } };

	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists in an editor world"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(World);
	Root->SetWidth(1920.0f);
	Root->SetHeight(1080.0f);
	Root->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	Root->OnRegister();
	Manager->TickDreamUI(0.016f);

	UDreamWidget* Button = NewObject<UDreamWidget>(Root);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	UDreamWidget* Label = NewObject<UDreamWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);
	Button->SetParent(Root, false);
	Button->OnRegister();
	Label->OnRegister();

	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("An editor-world drop is arranged too"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAttachThenAlignmentTakesEffectTest,
	"DreamGUI.Layout.Attach.AlignmentAfterAttachTakesEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAttachThenAlignmentTakesEffectTest::RunTest(const FString& Parameters)
{
	using namespace DreamAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickDreamUI(0.016f);
	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	Child->TrySetParent(Root, false);
	Child->OnRegister();
	Manager->TickDreamUI(0.016f);

	UDreamPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The child has a panel slot"), Slot))
	{
		Root->DestroyWidget();
		return false;
	}

	// The second half of the report: changing alignment afterwards has to take effect too.
	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Left alignment shrinks the child to its desired width"), Child->GetSize().X, 100.0);

	Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("Back to Fill stretches it again"), Child->GetSize().X, 1920.0);

	Root->DestroyWidget();
	return true;
}

#endif
