// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"

/*
 * UDreamLayoutContainer::SnapshotLayout used to open with RefreshChildren().
 *
 * That call was non-virtual, and so was the legacy FlexBox container's same-named function, so it always
 * bound to the *base* version - for FlexBox, for Grid and for every panel alike. The Children array it
 * filled is read only by FlexBox, which repopulates it itself in DoCalculate, so its output was never
 * observed. Its side effect was: it rewrote child anchors to 0.5/0.5, on every container in the tree, on
 * every pass, dirty or not, under its own filter rule.
 *
 * The two filters disagree. The base version took every child that is *active*; FlexBox takes every
 * child that is *layout-visible*. Those are independent flags - Collapsed clears layout-visible and
 * leaves active alone - so a collapsed child under a FlexBox had its authored stretch anchors destroyed
 * by a pass belonging to a container that then never laid it out. Nothing restores those anchors, and
 * the write is not transacted, so undo does not bring them back either.
 */

namespace DreamLayoutSnapshotChildrenTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	static bool IsHorizontallyStretched(const UDreamWidget* Widget)
	{
		return !FMath::IsNearlyEqual(Widget->GetAnchorMin().X, Widget->GetAnchorMax().X);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutCollapsedChildKeepsAuthoredAnchorsTest,
	"DreamGUI.Layout.SnapshotChildren.CollapsedChildKeepsAuthoredAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutCollapsedChildKeepsAuthoredAnchorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutSnapshotChildrenTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	UDreamWidget* Collapsed = NewObject<UDreamWidget>(Root);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	if (!TestTrue(TEXT("Child parented"), Collapsed->TrySetParent(Root, false)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("FlexBox created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}
	Root->OnRegister();
	Collapsed->OnRegister();

	// Authored to stretch horizontally, then collapsed. FlexBox never lays this child out, so nothing is
	// entitled to touch its anchors.
	Collapsed->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0));
	Collapsed->SetVisibility(EDreamWidgetVisibility::Collapsed);
	if (!TestTrue(TEXT("Child starts horizontally stretched"), IsHorizontallyStretched(Collapsed)))
	{
		return false;
	}
	TestTrue(TEXT("Collapsed child is still active in hierarchy"), Collapsed->GetWidgetActiveInHierarchy());
	TestFalse(TEXT("Collapsed child is not layout-visible"), Collapsed->GetLayoutVisibleInHierarchy());

	UDreamWidget::MarkLayoutForRebuild(Root);
	for (int32 I = 0; I < 3; ++I)
	{
		Manager->TickDreamUI(0.016f);
	}

	TestTrue(TEXT("A collapsed child keeps its authored stretch anchors"), IsHorizontallyStretched(Collapsed));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutParticipatingChildStillNormalizedTest,
	"DreamGUI.Layout.SnapshotChildren.ParticipatingChildStillNormalized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutParticipatingChildStillNormalizedTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutSnapshotChildrenTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}

	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World);
	UDreamWidget* Child = NewObject<UDreamWidget>(Root);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	Child->SetWidth(50.0f);
	Child->SetHeight(50.0f);
	if (!TestTrue(TEXT("Child parented"), Child->TrySetParent(Root, false))
		|| !TestNotNull(TEXT("FlexBox created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		return false;
	}
	Root->OnRegister();
	Child->OnRegister();

	// The half that must NOT regress: a child the FlexBox actually arranges still gets its unsupported
	// stretch anchor normalized, by FlexBox's own RefreshChildren in DoCalculate.
	Child->SetHorizontalAnchorMinMax(FVector2D(0.0, 1.0));
	TestTrue(TEXT("Child starts horizontally stretched"), IsHorizontallyStretched(Child));

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestTrue(TEXT("Participating child is still layout-visible"), Child->GetLayoutVisibleInHierarchy());
	TestFalse(TEXT("FlexBox still normalizes the anchor of a child it arranges"), IsHorizontallyStretched(Child));

	Root->DestroyWidget();
	return true;
}

#endif
