// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/World.h"

/*
 * Asking a panel for a child's desired size measures the whole subtree under that child, because a child
 * that is itself a panel answers through its container's preferred size, which measures ITS children in
 * turn. Nothing pruned that, so one question cost O(children^depth) - and a StackBox asks four times per
 * child in a single arrange: to total the fixed extent, to place, from inside ApplyChildRect, and again
 * from MeasureLayout.
 *
 * The memo that fixes it is only sound because an arrange pass stopped writing: a child's desired size
 * cannot change while the pass that would change it is recording into a fragment instead of applying it.
 * These tests hold both ends - the count really does collapse, and the geometry is unchanged by it.
 */

namespace LexDesiredSizeMemoTestLocal
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

	/** Root -> 3 boxes -> 3 boxes each -> 3 leaves each. 3 levels of panel, 40 widgets. */
	static ULexWidget* BuildDeepTree(UWorld* World, TArray<ULexWidget*>& OutLeaves)
	{
		ULexWidget* Root = NewObject<ULexWidget>(World);
		Root->SetWidth(600.0f);
		Root->SetHeight(400.0f);
		Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
		Root->OnRegister();
		for (int32 A = 0; A < 3; ++A)
		{
			ULexWidget* Mid = MakeChild(World, Root, 40.0f, 30.0f);
			Mid->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
			Mid->OnRegister();
			for (int32 B = 0; B < 3; ++B)
			{
				ULexWidget* Inner = MakeChild(World, Mid, 20.0f, 15.0f);
				Inner->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
				Inner->OnRegister();
				for (int32 C = 0; C < 3; ++C)
				{
					ULexWidget* Leaf = MakeChild(World, Inner, 8.0f, 6.0f);
					Leaf->OnRegister();
					OutLeaves.Add(Leaf);
				}
			}
		}
		return Root;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDesiredSizeMemoCollapsesMeasurementTest,
	"LGUI.Layout.DesiredSizeMemo.CollapsesRepeatedMeasurement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDesiredSizeMemoCollapsesMeasurementTest::RunTest(const FString& Parameters)
{
	using namespace LexDesiredSizeMemoTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	TArray<ULexWidget*> Leaves;
	ULexWidget* Root = BuildDeepTree(TestWorld.World, Leaves);
	if (!TestEqual(TEXT("Tree has 27 leaves"), Leaves.Num(), 27))
	{
		return false;
	}

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);

	ULexPanelLayoutBase::ResetDesiredSizeComputeCount();
	Root->SetWidth(700.0f);
	Manager->TickLexUI(0.016f);
	const int64 Computes = ULexPanelLayoutBase::GetDesiredSizeComputeCount();

	// Measured on this fixture: 408 without the memo, 102 with - exactly the factor of four the code
	// predicts, since a StackBox asks for each child's desired size four times in one arrange. The
	// bound sits between the two rather than at either, so it discriminates without needing a rebaseline
	// every time a panel gains or loses a loop.
	AddInfo(FString::Printf(TEXT("GetDesiredSize computed %lld times for a 40-widget, 3-level tree"), Computes));
	TestTrue(TEXT("A resize measures each child about once, not once per loop that asks"), Computes < 200);
	TestTrue(TEXT("...and still measures something"), Computes > 0);

	// The memo must not have changed the answer.
	TestEqual(TEXT("The tree still laid out"), Root->GetSize(), FVector2D(700.0, 400.0));
	for (ULexWidget* Leaf : Leaves)
	{
		if (Leaf->GetSize().X <= 0.0)
		{
			AddError(TEXT("A leaf came out with no width"));
			break;
		}
	}

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexDesiredSizeMemoDoesNotOutliveThePassTest,
	"LGUI.Layout.DesiredSizeMemo.DoesNotOutliveThePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexDesiredSizeMemoDoesNotOutliveThePassTest::RunTest(const FString& Parameters)
{
	using namespace LexDesiredSizeMemoTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// A stack box whose child is sized from its own content, so the child's desired size is what decides
	// the arrangement - exactly the value a stale memo would get wrong.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	ULexLayoutContainerVerticalBox* Box = Root->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	if (!TestNotNull(TEXT("Box created"), Box))
	{
		return false;
	}
	ULexWidget* Child = MakeChild(TestWorld.World, Root, 30.0f, 40.0f);
	Root->OnRegister();
	Child->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("The child starts at its authored height"), Child->GetSize().Y, 40.0);

	// Re-author the child between frames. If the memo survived the pass that produced it, the next frame
	// would arrange against the old 40 and the change would simply not appear.
	Child->SetHeight(90.0f);
	Manager->TickLexUI(0.016f);
	Manager->TickLexUI(0.016f);
	TestEqual(TEXT("The next frame arranges against the new height"), Child->GetSize().Y, 90.0);

	Root->DestroyWidget();
	return true;
}

#endif
