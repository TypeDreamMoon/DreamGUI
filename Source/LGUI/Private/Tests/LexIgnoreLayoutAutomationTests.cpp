// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"

/*
 * ULexWidget::SetIgnoreLayout wrote the flag and then called MarkLayoutForRebuild(this).
 *
 * MarkLayoutForRebuild breaks its ancestor walk on the first widget whose IgnoreLayout is set - and the
 * flag had just been set on the widget it starts from - so the walk stopped on iteration one and the
 * parent container, the one that has to close the gap the child left behind, was never dirtied.
 * MarkRebuildAllLayoutTree does not cover for that: it clears the tree cache, it sets no dirty flags.
 *
 * Turning the flag back off worked, because by then the flag reads false and the walk proceeds normally,
 * which made it present as a checkbox that only works in one direction.
 */

namespace LexIgnoreLayoutTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	struct FPanelWithTwoChildrenFixture
	{
		ULexWidget* Root = nullptr;
		ULexWidget* ChildA = nullptr;
		ULexWidget* ChildB = nullptr;
		ULexLayoutPassCountingOverlay* Overlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<ULexWidget>(World);
			ChildA = NewObject<ULexWidget>(Root);
			ChildB = NewObject<ULexWidget>(Root);
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			ChildA->SetWidth(10.0f);
			ChildA->SetHeight(10.0f);
			ChildB->SetWidth(10.0f);
			ChildB->SetHeight(10.0f);
			if (!ChildA->TrySetParent(Root, false) || !ChildB->TrySetParent(Root, false))
			{
				return false;
			}
			Overlay = Cast<ULexLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(ULexLayoutPassCountingOverlay::StaticClass()));
			if (!Overlay)
			{
				return false;
			}
			Root->OnRegister();
			ChildA->OnRegister();
			ChildB->OnRegister();
			return true;
		}

		void Settle(ULexUIManagerWorldSubsystem* Manager)
		{
			ULexWidget::MarkLayoutForRebuild(Root);
			Manager->TickLexUI(0.016f);
			Manager->TickLexUI(0.016f);
			Overlay->PassCount = 0;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexIgnoreLayoutEnablingDirtiesParentTest,
	"LGUI.Layout.IgnoreLayout.EnablingDirtiesParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexIgnoreLayoutEnablingDirtiesParentTest::RunTest(const FString& Parameters)
{
	using namespace LexIgnoreLayoutTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FPanelWithTwoChildrenFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}

	Fixture.Settle(Manager);
	TestEqual(TEXT("Settled before the edit"), Fixture.Overlay->PassCount, 0);

	// Turning IgnoreLayout ON: the direction that used to be swallowed.
	Fixture.ChildA->SetIgnoreLayout(true);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Enabling IgnoreLayout reflows the parent"), Fixture.Overlay->PassCount, 1);

	// And OFF, which always worked - pinned so the fix cannot trade one direction for the other.
	Fixture.Overlay->PassCount = 0;
	Fixture.ChildA->SetIgnoreLayout(false);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("Disabling IgnoreLayout reflows the parent"), Fixture.Overlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexIgnoreLayoutedChildLeavesTheArrangementTest,
	"LGUI.Layout.IgnoreLayout.IgnoredChildLeavesTheArrangement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexIgnoreLayoutedChildLeavesTheArrangementTest::RunTest(const FString& Parameters)
{
	using namespace LexIgnoreLayoutTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FPanelWithTwoChildrenFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Settle(Manager);

	// An overlay stretches every participating child to fill it. Once ignored, the child must stop being
	// arranged - so parking it at a size of its own must survive the next pass.
	TestEqual(TEXT("Child A is arranged while participating"), Fixture.ChildA->GetSize(), Fixture.Root->GetSize());

	Fixture.ChildA->SetIgnoreLayout(true);
	Manager->TickLexUI(0.016f);
	Fixture.ChildA->SetWidth(37.0f);
	Fixture.ChildA->SetHeight(23.0f);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	TestTrue(TEXT("An ignored child keeps the size it was given"),
		FMath::IsNearlyEqual(Fixture.ChildA->GetWidth(), 37.0f, 0.01f)
		&& FMath::IsNearlyEqual(Fixture.ChildA->GetHeight(), 23.0f, 0.01f));
	TestEqual(TEXT("The still-participating sibling is unaffected"),
		Fixture.ChildB->GetSize(), Fixture.Root->GetSize());

	Fixture.Root->DestroyWidget();
	return true;
}

#endif
