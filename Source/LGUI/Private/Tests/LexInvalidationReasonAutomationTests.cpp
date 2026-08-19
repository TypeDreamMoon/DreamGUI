// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"
#include "Engine/World.h"

/*
 * Invalidation used to be one bit, so every change reached every layout on the ancestor chain. The
 * distinction was already written down - several position setters carry the comment "only position
 * change, if parent contains LayoutContainer then we should rebuild layout, otherwise not" - but with
 * one bool the only expressible choices were the whole chain or nothing.
 *
 * A panel measures its children by desired size and never by where they sit, so a widget that only moved
 * cannot change any preferred size above it. ELexLayoutInvalidation::Arrange says exactly that: the
 * parent re-arranges, the walk stops. What must NOT change is the visible outcome - a panel-managed
 * child still gets snapped back to where the panel wants it.
 */

namespace LexInvalidationReasonTestLocal
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

	/** Root(counting) -> Mid(counting) -> Leaf. Both panels report how often they really recomputed. */
	struct FCountedChainFixture
	{
		ULexWidget* Root = nullptr;
		ULexWidget* Mid = nullptr;
		ULexWidget* Leaf = nullptr;
		ULexLayoutPassCountingOverlay* RootOverlay = nullptr;
		ULexLayoutPassCountingOverlay* MidOverlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<ULexWidget>(World);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Mid = MakeChild(World, Root, 200.0f, 150.0f);
			Leaf = MakeChild(World, Mid, 40.0f, 30.0f);

			RootOverlay = Cast<ULexLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
			MidOverlay = Cast<ULexLayoutPassCountingOverlay>(
				Mid->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
			if (!RootOverlay || !MidOverlay)
			{
				return false;
			}
			Root->OnRegister();
			Mid->OnRegister();
			Leaf->OnRegister();
			return true;
		}

		void Settle(ULexUIManagerWorldSubsystem* Manager)
		{
			ULexWidget::MarkLayoutForRebuild(Root);
			Manager->TickLexUI(0.016f);
			Manager->TickLexUI(0.016f);
			RootOverlay->PassCount = 0;
			MidOverlay->PassCount = 0;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInvalidationMovingAChildStopsAtTheParentTest,
	"LGUI.Layout.InvalidationReason.MovingAChildStopsAtTheParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInvalidationMovingAChildStopsAtTheParentTest::RunTest(const FString& Parameters)
{
	using namespace LexInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FCountedChainFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);

	// Position only. Nothing above Mid can produce a different number.
	Fixture.Leaf->SetAnchoredPosition(FVector2D(11.0, 13.0));
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("The parent re-arranges"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("The grandparent does not"), Fixture.RootOverlay->PassCount, 0);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInvalidationResizingAChildStillReachesTheTopTest,
	"LGUI.Layout.InvalidationReason.ResizingAChildStillReachesTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInvalidationResizingAChildStillReachesTheTopTest::RunTest(const FString& Parameters)
{
	using namespace LexInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FCountedChainFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);

	// The half that must not regress. A size change really can move every preferred size above it, so
	// this one still has to reach the whole chain - narrowing the reach for position is only safe
	// because measurement is what does not depend on it.
	Fixture.Leaf->SetWidth(77.0f);
	Manager->TickLexUI(0.016f);

	TestEqual(TEXT("The parent recomputes"), Fixture.MidOverlay->PassCount, 1);
	TestEqual(TEXT("...and so does the grandparent"), Fixture.RootOverlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexInvalidationPanelStillOverridesAMovedChildTest,
	"LGUI.Layout.InvalidationReason.PanelStillOverridesAMovedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexInvalidationPanelStillOverridesAMovedChildTest::RunTest(const FString& Parameters)
{
	using namespace LexInvalidationReasonTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	ULexWidget* Child = MakeChild(TestWorld.World, Root, 40.0f, 30.0f);
	if (!Root->CreateNewLayoutContainer<ULexLayoutContainerOverlay>())
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	const FVector2D Arranged = Child->GetAnchoredPosition();

	// The behaviour that narrowing the reach must not break: a panel owns its children's positions, so
	// moving one by hand has to be undone on the next pass exactly as before.
	Child->SetAnchoredPosition(Arranged + FVector2D(37.0, 41.0));
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("The panel puts the child back where it wants it"), Child->GetAnchoredPosition(), Arranged);

	Root->DestroyWidget();
	return true;
}

#endif
