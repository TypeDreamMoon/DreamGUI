// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
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

namespace LexAttachArrangesChildTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static ULexWidget* MakeRootWithOverlay(UWorld* World)
	{
		ULexWidget* Root = NewObject<ULexWidget>(World);
		Root->SetWidth(1920.0f);
		Root->SetHeight(1080.0f);
		Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
		Root->OnRegister();
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachArrangesChildRegisterAfterTest,
	"LGUI.Layout.Attach.AttachThenRegisterArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachArrangesChildRegisterAfterTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickLexUI(0.016f);

	ULexWidget* Child = NewObject<ULexWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	if (!TestTrue(TEXT("Child attaches"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	Child->OnRegister();

	// No MarkLayoutForRebuild here on purpose - the attach is the whole event, exactly as in the editor.
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("A newly attached child is arranged by the panel it landed in"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachArrangesChildRegisterBeforeTest,
	"LGUI.Layout.Attach.RegisterThenAttachArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachArrangesChildRegisterBeforeTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickLexUI(0.016f);

	// The prefab loader builds and registers a whole hierarchy before parenting its root, which is the
	// order the palette drop actually takes.
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	Child->OnRegister();
	if (!TestTrue(TEXT("Child attaches"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("A child registered before it was attached is still arranged"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachPrefabShapedSubtreeTest,
	"LGUI.Layout.Attach.PrefabShapedSubtreeIsArranged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachPrefabShapedSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickLexUI(0.016f);

	// WidgetSerializer_Deserialize builds the whole subtree unregistered, parents the created root into
	// the target, and only then walks AllWidgetArray calling OnRegister. A palette Button is exactly this
	// shape: a root with one Text child.
	ULexWidget* Button = NewObject<ULexWidget>(Root);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	ULexWidget* Label = NewObject<ULexWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);

	Button->SetParent(Root, false);

	Button->OnRegister();
	Label->OnRegister();

	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("A prefab-shaped subtree dropped into a panel is arranged"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachUnderStretchedPanelTest,
	"LGUI.Layout.Attach.StretchedPanelArrangesADroppedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachUnderStretchedPanelTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// ULexUIPrefabFactory gives a new prefab root stretch anchors and a zero size delta, so its size is
	// its parent's - the prefab scene's root agent - rather than anything authored. Every repro above
	// used a fixed-size panel, which is the one thing the real case is not.
	ULexWidget* Agent = NewObject<ULexWidget>(TestWorld.World);
	Agent->SetWidth(1920.0f);
	Agent->SetHeight(1080.0f);
	Agent->OnRegister();

	ULexWidget* Panel = NewObject<ULexWidget>(Agent);
	Panel->SetParent(Agent, false);
	Panel->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Panel->SetAnchoredPosition(FVector2D::ZeroVector);
	Panel->SetSizeDelta(FVector2D::ZeroVector);
	Panel->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	Panel->OnRegister();
	Manager->TickLexUI(0.016f);

	if (!TestEqual(TEXT("The stretched panel takes the agent's size"), Panel->GetSize(), FVector2D(1920.0, 1080.0)))
	{
		Agent->DestroyWidget();
		return false;
	}

	ULexWidget* Button = NewObject<ULexWidget>(Panel);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	ULexWidget* Label = NewObject<ULexWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);
	Button->SetParent(Panel, false);
	Button->OnRegister();
	Label->OnRegister();
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("A child dropped into a stretched panel fills it"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Agent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexStretchedPanelFollowsAgentResizeTest,
	"LGUI.Layout.Attach.StretchedPanelRearrangesWhenItsOwnSizeChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexStretchedPanelFollowsAgentResizeTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Agent = NewObject<ULexWidget>(TestWorld.World);
	Agent->SetWidth(800.0f);
	Agent->SetHeight(600.0f);
	Agent->OnRegister();
	ULexWidget* Panel = NewObject<ULexWidget>(Agent);
	Panel->SetParent(Agent, false);
	Panel->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Panel->SetAnchoredPosition(FVector2D::ZeroVector);
	Panel->SetSizeDelta(FVector2D::ZeroVector);
	Panel->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	Panel->OnRegister();
	ULexWidget* Child = NewObject<ULexWidget>(Panel);
	Child->SetWidth(10.0f);
	Child->SetHeight(10.0f);
	Child->SetParent(Panel, false);
	Child->OnRegister();
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("The child starts filled to the agent size"), Child->GetSize(), FVector2D(800.0, 600.0));

	// A stretched panel's size changes through anchor propagation, not through its own setter. If that
	// path does not invalidate the panel's layout, the panel keeps arranging against the old size.
	Agent->SetWidth(1920.0f);
	Agent->SetHeight(1080.0f);
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("The panel itself followed"), Panel->GetSize(), FVector2D(1920.0, 1080.0));
	TestEqual(TEXT("...and re-arranged its child against the new size"),
		Child->GetSize(), FVector2D(1920.0, 1080.0));

	Agent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachInEditorWorldTest,
	"LGUI.Layout.Attach.EditorWorldArrangesTheChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachInEditorWorldTest::RunTest(const FString& Parameters)
{
	// FLexUIPrefabScene builds its world as EWorldType::Editor (LexUIPrefabScene.cpp:30), and several
	// LGUI paths branch on IsGameWorld. Same fixture, the world type the prefab editor actually uses.
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	ON_SCOPE_EXIT{ if (World) { World->DestroyWorld(false); } };

	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists in an editor world"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(World);
	Root->SetWidth(1920.0f);
	Root->SetHeight(1080.0f);
	Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>();
	Root->OnRegister();
	Manager->TickLexUI(0.016f);

	ULexWidget* Button = NewObject<ULexWidget>(Root);
	Button->SetWidth(100.0f);
	Button->SetHeight(30.0f);
	ULexWidget* Label = NewObject<ULexWidget>(Button);
	Label->SetWidth(100.0f);
	Label->SetHeight(30.0f);
	Label->TrySetParent(Button, false);
	Button->SetParent(Root, false);
	Button->OnRegister();
	Label->OnRegister();

	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("An editor-world drop is arranged too"),
		Button->GetSize(), FVector2D(1920.0, 1080.0));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAttachThenAlignmentTakesEffectTest,
	"LGUI.Layout.Attach.AlignmentAfterAttachTakesEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAttachThenAlignmentTakesEffectTest::RunTest(const FString& Parameters)
{
	using namespace LexAttachArrangesChildTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = MakeRootWithOverlay(TestWorld.World);
	Manager->TickLexUI(0.016f);
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	Child->SetWidth(100.0f);
	Child->SetHeight(30.0f);
	Child->TrySetParent(Root, false);
	Child->OnRegister();
	Manager->TickLexUI(0.016f);

	ULexPanelSlot* Slot = Child->GetPanelSlot();
	if (!TestNotNull(TEXT("The child has a panel slot"), Slot))
	{
		Root->DestroyWidget();
		return false;
	}

	// The second half of the report: changing alignment afterwards has to take effect too.
	Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Left);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Left alignment shrinks the child to its desired width"), Child->GetSize().X, 100.0);

	Slot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Fill);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Back to Fill stretches it again"), Child->GetSize().X, 1920.0);

	Root->DestroyWidget();
	return true;
}

#endif
