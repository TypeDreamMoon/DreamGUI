// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Tests/LexLayoutInvalidationTestTypes.h"
#include "Engine/World.h"

/*
 * ULexLayoutSelfAspectRatio used to ask one all-or-nothing question - "does the parent panel own this
 * widget's geometry?" - which required a panel slot plus all four control bits at once. A container that
 * owns only the two *position* bits cannot satisfy that, so under such a parent the answer
 * was always no, and AspectRatio's FitInParent/EnvelopeParent overwrote the anchored position the
 * container had just written.
 *
 * Measured, not assumed: this does *not* fail to converge. AspectRatio recomputes from the parent's size,
 * which the pass does not change, so it writes the same value every time and the setter's equality check
 * absorbs it. It settles - at the wrong answer. The child ends up parked at the aspect-centred position
 * (0,0 for a default pivot), on top of its siblings, and stays there quietly.
 *
 * The control data is now consulted per axis, so the container places the child and AspectRatio sizes it.
 */

namespace LexAspectRatioLayoutTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Horizontal box root with a plain child followed by a square-aspect child. */
	struct FPanelWithAspectChildFixture
	{
		ULexWidget* Root = nullptr;
		ULexWidget* Plain = nullptr;
		ULexWidget* Aspect = nullptr;
		ULexLayoutSelfAspectRatio* AspectSelf = nullptr;

		bool Build(UWorld* World, ELexLayoutAspectRatioType Type)
		{
			Root = NewObject<ULexWidget>(World);
			Plain = NewObject<ULexWidget>(Root);
			Aspect = NewObject<ULexWidget>(Root);
			Root->SetWidth(400.0f);
			Root->SetHeight(200.0f);
			Plain->SetWidth(50.0f);
			Plain->SetHeight(50.0f);
			Aspect->SetWidth(30.0f);
			Aspect->SetHeight(90.0f);
			if (!Plain->TrySetParent(Root, false) || !Aspect->TrySetParent(Root, false))
			{
				return false;
			}
			if (!Root->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>())
			{
				return false;
			}
			AspectSelf = Aspect->CreateNewLayoutSelf<ULexLayoutSelfAspectRatio>();
			if (!AspectSelf)
			{
				return false;
			}
			AspectSelf->SetAspectRatio(1.0f);
			AspectSelf->SetAspectRatioType(Type);
			Root->OnRegister();
			Plain->OnRegister();
			Aspect->OnRegister();
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAspectRatioUnderPanelConvergesTest,
	"LGUI.Layout.AspectRatio.FitInParentUnderPanelConverges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAspectRatioUnderPanelConvergesTest::RunTest(const FString& Parameters)
{
	using namespace LexAspectRatioLayoutTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FPanelWithAspectChildFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager)
		|| !Fixture.Build(TestWorld.World, ELexLayoutAspectRatioType::FitInParent))
	{
		return false;
	}

	ULexWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickLexUI(0.016f);
	const FVector2D AfterFirst = Fixture.Aspect->GetAnchoredPosition();

	// Convergence: once settled, further ticks must not move anything. Two layouts fighting over the same
	// axis produce a different answer on every pass, so this is the assertion that actually catches it.
	for (int32 I = 0; I < 4; ++I)
	{
		Manager->TickLexUI(0.016f);
	}
	TestEqual(TEXT("Aspect child position is stable across ticks"),
		Fixture.Aspect->GetAnchoredPosition(), AfterFirst);

	// The container places the child. Before the fix AspectRatio stamped the aspect-centred position,
	// which is exactly (0,0) for the default pivot, dropping it on top of its sibling.
	TestNotEqual(TEXT("The container owns the horizontal position, not AspectRatio"),
		Fixture.Aspect->GetAnchoredPosition().X, 0.0);
	TestTrue(TEXT("The two children are laid out apart, not stacked"),
		!FMath::IsNearlyEqual(Fixture.Aspect->GetAnchoredPosition().X, Fixture.Plain->GetAnchoredPosition().X));

	// AspectRatio still does its own job: the size is square, from a 30x90 start.
	TestTrue(TEXT("AspectRatio still sized the child to its ratio"),
		FMath::IsNearlyEqual(Fixture.Aspect->GetWidth(), Fixture.Aspect->GetHeight(), 0.01f));

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAspectRatioEnvelopeUnderPanelConvergesTest,
	"LGUI.Layout.AspectRatio.EnvelopeParentUnderPanelConverges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAspectRatioEnvelopeUnderPanelConvergesTest::RunTest(const FString& Parameters)
{
	using namespace LexAspectRatioLayoutTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FPanelWithAspectChildFixture Fixture;
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager)
		|| !Fixture.Build(TestWorld.World, ELexLayoutAspectRatioType::EnvelopeParent))
	{
		return false;
	}

	ULexWidget::MarkLayoutForRebuild(Fixture.Root);
	Manager->TickLexUI(0.016f);
	const FVector2D AfterFirst = Fixture.Aspect->GetAnchoredPosition();
	for (int32 I = 0; I < 4; ++I)
	{
		Manager->TickLexUI(0.016f);
	}

	TestEqual(TEXT("Aspect child position is stable across ticks"),
		Fixture.Aspect->GetAnchoredPosition(), AfterFirst);
	TestNotEqual(TEXT("The container owns the horizontal position, not AspectRatio"),
		Fixture.Aspect->GetAnchoredPosition().X, 0.0);

	// Envelope covers the parent, so a 400x200 parent asks for a 400x400 child; with a 50-wide sibling
	// that never fits a 400-wide row, and the box shrinks it. The final size is the container's call -
	// what AspectRatio still owes is a square *desired* size, which is what the container then works from.
	const FVector2f Preferred = Fixture.AspectSelf->GetLayoutPreferredSize();
	TestTrue(TEXT("AspectRatio still reports a square desired size"),
		FMath::IsNearlyEqual(Preferred.X, Preferred.Y, 0.01f));
	TestTrue(TEXT("...and it envelopes rather than fits the parent"),
		Preferred.X >= Fixture.Root->GetWidth() - 0.01f);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAspectRatioMeasurementDoesNotWriteTest,
	"LGUI.Layout.AspectRatio.MeasurementDoesNotWriteTheWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAspectRatioMeasurementDoesNotWriteTest::RunTest(const FString& Parameters)
{
	using namespace LexAspectRatioLayoutTestLocal;
	FScopedTestWorld TestWorld;

	// The parent has no layout container, so nothing else claims an axis. This is exactly the case where
	// CalculateSize writes both the anchored position and the size - which is what makes it a usable probe
	// for whether measurement is still secretly running that write pass.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	Child->SetWidth(30.0f);
	Child->SetHeight(90.0f);
	if (!TestTrue(TEXT("Child parented"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	ULexApplyCountingAspectRatio* AspectSelf = Child->CreateNewLayoutSelf<ULexApplyCountingAspectRatio>();
	if (!TestNotNull(TEXT("AspectRatio created"), AspectSelf))
	{
		return false;
	}
	AspectSelf->SetAspectRatio(1.0f);
	AspectSelf->SetAspectRatioType(ELexLayoutAspectRatioType::FitInParent);
	Root->OnRegister();
	Child->OnRegister();

	// Looking at the widget cannot tell the two halves apart. AspectRatio re-solves eagerly from
	// OnDimensionChanged, so it always already sits on its own answer - a redundant write lands on an
	// equal value and the setter swallows it. Even resizing the parent does not open a gap: that
	// propagates down and re-solves the child before anything can observe the stale state. So count the
	// apply pass instead, which is the property in question stated directly.
	const int32 AppliesBefore = AspectSelf->ApplyCount;
	const FVector2f Preferred = AspectSelf->GetLayoutPreferredSize();

	// 400x200 parent, ratio 1, fit inside => 200x200. Measuring still has to answer correctly.
	TestTrue(TEXT("Measurement reports the fitted square"),
		FMath::IsNearlyEqual(Preferred.X, 200.0f, 0.01f) && FMath::IsNearlyEqual(Preferred.Y, 200.0f, 0.01f));

	// ...and it has to answer without running the write pass. This is the whole point of the split:
	// GetLayoutPreferredSize used to be CalculateSize() plus a read of the cache it filled.
	TestEqual(TEXT("Measuring does not run the apply pass"), AspectSelf->ApplyCount, AppliesBefore);

	// The apply half still applies - the split must not have quietly disabled the layout.
	Child->SetWidth(17.0f);
	Child->SetHeight(19.0f);
	AspectSelf->CalculateSize();
	TestTrue(TEXT("Applying still sizes the widget to the fitted square"),
		FMath::IsNearlyEqual(static_cast<float>(Child->GetWidth()), 200.0f, 0.01f)
		&& FMath::IsNearlyEqual(static_cast<float>(Child->GetHeight()), 200.0f, 0.01f));
	TestTrue(TEXT("...and that did count as an apply"), AspectSelf->ApplyCount > AppliesBefore);

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexAspectRatioWithoutParentLayoutStillOwnsGeometryTest,
	"LGUI.Layout.AspectRatio.WithoutParentLayoutStillOwnsGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexAspectRatioWithoutParentLayoutStillOwnsGeometryTest::RunTest(const FString& Parameters)
{
	using namespace LexAspectRatioLayoutTestLocal;
	FScopedTestWorld TestWorld;
	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("LexUI manager subsystem exists"), Manager))
	{
		return false;
	}

	// No parent layout at all: nothing claims any axis, so AspectRatio must keep full control. This is the
	// half of the change that must NOT regress - per-axis yielding is only meant to defer to a real claim.
	ULexWidget* Root = NewObject<ULexWidget>(TestWorld.World);
	ULexWidget* Child = NewObject<ULexWidget>(Root);
	Root->SetWidth(400.0f);
	Root->SetHeight(200.0f);
	Child->SetWidth(30.0f);
	Child->SetHeight(90.0f);
	if (!TestTrue(TEXT("Child parented"), Child->TrySetParent(Root, false)))
	{
		return false;
	}
	ULexLayoutSelfAspectRatio* AspectSelf = Child->CreateNewLayoutSelf<ULexLayoutSelfAspectRatio>();
	if (!TestNotNull(TEXT("AspectRatio LayoutSelf created"), AspectSelf))
	{
		return false;
	}
	AspectSelf->SetAspectRatio(2.0f);
	AspectSelf->SetAspectRatioType(ELexLayoutAspectRatioType::FitInParent);
	Root->OnRegister();
	Child->OnRegister();

	ULexWidget::MarkLayoutForRebuild(Root);
	Manager->TickLexUI(0.016f);

	// FitInParent with ratio 2 inside 400x200 is width-limited: 400x200 exactly.
	TestTrue(TEXT("AspectRatio fitted the child to the parent"),
		FMath::IsNearlyEqual(Child->GetWidth(), 400.0f, 0.01f)
		&& FMath::IsNearlyEqual(Child->GetHeight(), 200.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

#endif
