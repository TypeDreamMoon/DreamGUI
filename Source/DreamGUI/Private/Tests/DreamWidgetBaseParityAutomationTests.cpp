// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamPointerPolicy.h"
#include "GameFramework/Actor.h"
#include "Interaction/DreamUITooltip.h"
#include "Layout/FlowDirection.h"
#include "Tests/DreamLayoutInvalidationTestTypes.h"

/*
 * The generic members every UMG user reaches for on the base widget class: flow direction, the
 * cascading enabled switch, the tooltip widget class and the hover/focus queries.
 *
 * Each of these had to be reachable from Blueprint AND take effect at runtime, which is the half that
 * is easy to lose: a property that only writes its own field looks identical in the details panel and
 * does nothing when a graph sets it. The assertions below therefore read the CONSEQUENCE -- the
 * cascade cache, the container's pass count, the tooltip the subsystem resolves -- rather than
 * reading the field back.
 */

namespace DreamWidgetBaseParityTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Root with a counting overlay, one child, one grandchild -- three levels of inheritance to walk. */
	struct FThreeLevelFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;
		UDreamWidget* GrandChild = nullptr;
		UDreamLayoutPassCountingOverlay* RootOverlay = nullptr;
		UDreamLayoutPassCountingOverlay* ChildOverlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			Child = NewObject<UDreamWidget>(Root);
			GrandChild = NewObject<UDreamWidget>(Root);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Child->SetWidth(200.0f);
			Child->SetHeight(150.0f);
			GrandChild->SetWidth(50.0f);
			GrandChild->SetHeight(50.0f);
			if (!Child->TrySetParent(Root, false) || !GrandChild->TrySetParent(Child, false))
			{
				return false;
			}
			RootOverlay = Cast<UDreamLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
			ChildOverlay = Cast<UDreamLayoutPassCountingOverlay>(
				Child->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
			if (!RootOverlay || !ChildOverlay)
			{
				return false;
			}
			Root->OnRegister();
			Child->OnRegister();
			GrandChild->OnRegister();
			return true;
		}

		void Settle(UDreamUIManagerWorldSubsystem* Manager)
		{
			UDreamWidget::MarkLayoutForRebuild(Root);
			Manager->TickDreamUI(0.016f);
			Manager->TickDreamUI(0.016f);
			RootOverlay->PassCount = 0;
			ChildOverlay->PassCount = 0;
		}

		void Destroy()
		{
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}
	};

	/**
	 * An event system registered with the manager, which is what the hover and capture queries look
	 * up. Registered by hand: a world built for a test never begins play, registration is the part
	 * of BeginPlay these queries depend on, and BeginPlay itself is not a test's to call.
	 */
	UDreamEventSystem* MakeRegisteredEventSystem(UWorld* World)
	{
		AActor* Host = World->SpawnActor<AActor>();
		UDreamEventSystem* EventSystem = NewObject<UDreamEventSystem>(Host);
		EventSystem->RegisterComponent();
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
		{
			Manager->AddEventSystem(EventSystem);
		}
		return EventSystem;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetFlowDirectionInheritsFromTheNearestAncestorTest,
	"DreamGUI.Widget.FlowDirection.InheritTakesTheAnswerFromTheNearestAncestorThatStatesOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetFlowDirectionInheritsFromTheNearestAncestorTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// Nobody has an opinion yet, so the whole tree reads left to right.
	TestTrue(TEXT("an untouched tree resolves left to right at the root"),
		Fixture.Root->GetResolvedFlowDirection() == EDreamFlowDirection::LeftToRight);
	TestTrue(TEXT("an untouched tree resolves left to right at the leaf"),
		Fixture.GrandChild->GetResolvedFlowDirection() == EDreamFlowDirection::LeftToRight);

	Fixture.Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	TestTrue(TEXT("the root resolves to what it stated"),
		Fixture.Root->GetResolvedFlowDirection() == EDreamFlowDirection::RightToLeft);
	TestTrue(TEXT("a descendant that inherits follows the root"),
		Fixture.Child->GetResolvedFlowDirection() == EDreamFlowDirection::RightToLeft);
	TestTrue(TEXT("inheritance reaches past an intermediate widget that also inherits"),
		Fixture.GrandChild->GetResolvedFlowDirection() == EDreamFlowDirection::RightToLeft);

	// The nearest ancestor wins, which is what makes one deliberately pinned panel possible inside an
	// otherwise mirrored screen.
	Fixture.Child->SetFlowDirectionPreference(EDreamFlowDirectionPreference::LeftToRight);
	TestTrue(TEXT("a widget that states its own preference ignores the ancestor"),
		Fixture.Child->GetResolvedFlowDirection() == EDreamFlowDirection::LeftToRight);
	TestTrue(TEXT("its descendants inherit from it and not from the root"),
		Fixture.GrandChild->GetResolvedFlowDirection() == EDreamFlowDirection::LeftToRight);
	TestTrue(TEXT("the root is unaffected by what a descendant states"),
		Fixture.Root->GetResolvedFlowDirection() == EDreamFlowDirection::RightToLeft);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetFlowDirectionCultureFollowsTheRunningCultureTest,
	"DreamGUI.Widget.FlowDirection.CultureResolvesToTheRunningCulturesWritingDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetFlowDirectionCultureFollowsTheRunningCultureTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// The test cannot switch the editor's culture out from under itself, so it asserts the mapping
	// rather than a fixed answer: whatever the engine says the culture reads, that is what Culture
	// resolves to, and every inheriting descendant agrees with it.
	const EDreamFlowDirection Expected =
		FLayoutLocalization::GetLocalizedLayoutDirection() == EFlowDirection::RightToLeft
			? EDreamFlowDirection::RightToLeft
			: EDreamFlowDirection::LeftToRight;

	Fixture.Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::Culture);
	TestTrue(TEXT("Culture resolves to the engine's localized layout direction"),
		Fixture.Root->GetResolvedFlowDirection() == Expected);
	TestTrue(TEXT("descendants inherit the culture's answer"),
		Fixture.GrandChild->GetResolvedFlowDirection() == Expected);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetFlowDirectionChangeReArrangesTheSubtreeTest,
	"DreamGUI.Widget.FlowDirection.ChangingThePreferenceReArrangesTheContainersBelowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetFlowDirectionChangeReArrangesTheSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FThreeLevelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager)
		|| !TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}
	Fixture.Settle(Manager);

	// Setting the value it already holds must cost nothing at all: this runs from the details panel
	// and from .dui application, both of which write every property whether or not it changed.
	Fixture.Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::Inherit);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("re-setting the same preference arranges nothing"), Fixture.RootOverlay->PassCount, 0);

	Fixture.Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::RightToLeft);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("the widget's own container re-arranges"), Fixture.RootOverlay->PassCount > 0);
	TestTrue(TEXT("a descendant container that inherits the answer re-arranges too"),
		Fixture.ChildOverlay->PassCount > 0);

	// A descendant that states its own preference did not change answer, so nothing below it has to
	// move -- the recursion stops there.
	Fixture.Child->SetFlowDirectionPreference(EDreamFlowDirectionPreference::LeftToRight);
	Manager->TickDreamUI(0.016f);
	Fixture.RootOverlay->PassCount = 0;
	Fixture.ChildOverlay->PassCount = 0;
	Fixture.Root->SetFlowDirectionPreference(EDreamFlowDirectionPreference::Culture);
	Manager->TickDreamUI(0.016f);
	TestTrue(TEXT("the root still re-arranges"), Fixture.RootOverlay->PassCount > 0);
	TestEqual(TEXT("a pinned descendant is left alone"), Fixture.ChildOverlay->PassCount, 0);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetEnabledCascadesToTheWholeSubtreeTest,
	"DreamGUI.Widget.Enabled.DisablingAWidgetTakesItsWholeSubtreeOutOfInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetEnabledCascadesToTheWholeSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// The default has to leave every existing asset exactly where it was: everything enabled, and
	// therefore everything interactable, which is what the framework did before this switch existed.
	TestTrue(TEXT("a fresh widget is enabled"), Fixture.GrandChild->GetIsEnabled());
	TestTrue(TEXT("a fresh widget is enabled in hierarchy"), Fixture.GrandChild->GetIsEnabledInHierarchy());
	TestTrue(TEXT("a fresh widget is interactable in hierarchy"), Fixture.GrandChild->GetInteractableInHierarchy());

	Fixture.Root->SetIsEnabled(false);
	// Read the cascade, not the field: a setter that only wrote its own bool would pass a field read
	// and change nothing about what the pointer pipeline is allowed to hit.
	TestFalse(TEXT("the disabled widget is not interactable"), Fixture.Root->GetInteractableInHierarchy());
	TestFalse(TEXT("a child of a disabled widget is not interactable"), Fixture.Child->GetInteractableInHierarchy());
	TestFalse(TEXT("a grandchild of a disabled widget is not interactable"),
		Fixture.GrandChild->GetInteractableInHierarchy());
	TestFalse(TEXT("the cascade is visible as enabled-in-hierarchy"), Fixture.GrandChild->GetIsEnabledInHierarchy());
	TestTrue(TEXT("a descendant's OWN enabled switch is untouched"), Fixture.GrandChild->GetIsEnabled());

	Fixture.Root->SetIsEnabled(true);
	TestTrue(TEXT("re-enabling restores the subtree"), Fixture.GrandChild->GetInteractableInHierarchy());

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetDisabledAncestorBeatsPinnedInteractableTest,
	"DreamGUI.Widget.Enabled.ADisabledAncestorOutranksADescendantPinnedInteractable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetDisabledAncestorBeatsPinnedInteractableTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// Interactable=Enabled is how a widget opts out of an ancestor that turned interaction off. The
	// enabled switch is the other question and has to win, or "disable this dialog" leaves whichever
	// button was pinned still live.
	Fixture.Child->SetInteractable(EDreamWidgetInteractableType::Enabled);
	Fixture.Root->SetInteractable(EDreamWidgetInteractableType::Disabled);
	TestTrue(TEXT("a pinned descendant escapes an ancestor's Interactable=Disabled"),
		Fixture.Child->GetInteractableInHierarchy());

	Fixture.Root->SetIsEnabled(false);
	TestFalse(TEXT("but not an ancestor that is disabled outright"), Fixture.Child->GetInteractableInHierarchy());

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetTooltipWidgetClassIsAnOfferTest,
	"DreamGUI.Widget.Tooltip.AWidgetThatNamesATooltipClassIsItsOwnTooltipSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetTooltipWidgetClassIsAnOfferTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	Fixture.Root->SetToolTipText(FText::FromString(TEXT("the root explains itself")));
	TestTrue(TEXT("a child with nothing to say defers to the ancestor that has text"),
		DreamUITooltipPolicy::ResolveTooltipSource(Fixture.Child) == Fixture.Root);

	// The authored class is an offer in its own right. Before it was one, a widget that named a
	// tooltip class and no text was walked past and the ancestor's text bubble showed instead.
	Fixture.Child->SetToolTipWidgetClass(UDreamUserWidget::StaticClass());
	TestTrue(TEXT("a widget that names a tooltip class answers for itself"),
		DreamUITooltipPolicy::ResolveTooltipSource(Fixture.Child) == Fixture.Child);
	TestTrue(TEXT("and for its descendants"),
		DreamUITooltipPolicy::ResolveTooltipSource(Fixture.GrandChild) == Fixture.Child);

	Fixture.Child->SetToolTipWidgetClass(nullptr);
	TestTrue(TEXT("clearing it hands the answer back to the ancestor"),
		DreamUITooltipPolicy::ResolveTooltipSource(Fixture.Child) == Fixture.Root);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetResetCursorGivesTheClaimBackTest,
	"DreamGUI.Widget.Cursor.ResettingACursorHandsTheClaimBackToWhateverIsUnderneath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetResetCursorGivesTheClaimBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// Innermost first, the order the pointer pipeline hands the hover stack over in.
	UDreamWidget* HoverStack[] = { Fixture.GrandChild, Fixture.Child, Fixture.Root };
	Fixture.Root->SetCursor(EMouseCursor::Hand);
	Fixture.GrandChild->SetCursor(EMouseCursor::TextEditBeam);

	EMouseCursor::Type Resolved = EMouseCursor::Default;
	TestTrue(TEXT("somebody on the stack claims the cursor"),
		DreamPointerPolicy::ResolveCursor(HoverStack, Resolved));
	TestTrue(TEXT("the innermost widget with an opinion wins"), Resolved == EMouseCursor::TextEditBeam);

	Fixture.GrandChild->ResetCursor();
	Resolved = EMouseCursor::Default;
	TestTrue(TEXT("the ancestor's claim shows through once the leaf resets"),
		DreamPointerPolicy::ResolveCursor(HoverStack, Resolved));
	TestTrue(TEXT("and it is the ancestor's cursor"), Resolved == EMouseCursor::Hand);

	Fixture.Root->ResetCursor();
	Resolved = EMouseCursor::Hand;
	TestFalse(TEXT("nobody claiming is reported as nobody claiming"),
		DreamPointerPolicy::ResolveCursor(HoverStack, Resolved));

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetHoverAndCaptureQueriesTest,
	"DreamGUI.Widget.Queries.HoverAndCaptureAnswerFromTheLivePointerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetHoverAndCaptureQueriesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}
	UDreamEventSystem* EventSystem = MakeRegisteredEventSystem(TestWorld.World);
	UDreamPointerEventData* PointerEvent = EventSystem != nullptr ? EventSystem->GetPointerEventData(0, true) : nullptr;
	if (!TestNotNull(TEXT("a pointer to drive the queries with"), PointerEvent))
	{
		Fixture.Destroy();
		return false;
	}

	TestFalse(TEXT("nothing is hovered before a pointer enters anything"), Fixture.Child->IsHovered());
	TestFalse(TEXT("and nothing has captured a pointer"), Fixture.Child->HasMouseCapture());

	// What the pointer pipeline writes when the pointer arrives on the grandchild: the leaf, plus the
	// stack of everything it entered on the way in.
	PointerEvent->EnterWidget = Fixture.GrandChild;
	PointerEvent->EnterWidgetStack = { Fixture.Root, Fixture.Child, Fixture.GrandChild };
	TestTrue(TEXT("the widget under the pointer is hovered"), Fixture.GrandChild->IsHovered());
	TestTrue(TEXT("so is an ancestor it entered through"), Fixture.Child->IsHovered());

	// A pointer that only reached the root leaves everything inside it unhovered -- which is the
	// half a naive "is this the enter widget" check gets right and the ancestor half gets wrong.
	PointerEvent->EnterWidget = Fixture.Root;
	PointerEvent->EnterWidgetStack = { Fixture.Root };
	TestTrue(TEXT("the root is hovered"), Fixture.Root->IsHovered());
	TestFalse(TEXT("a descendant the pointer never entered is not"), Fixture.GrandChild->IsHovered());

	// Capture is "pressed here and still held": the release and every drag in between go to this
	// widget whatever the pointer travels over.
	PointerEvent->PressWidget = Fixture.Child;
	PointerEvent->bNowIsTriggerPressed = false;
	TestFalse(TEXT("a press that has already been released is not a capture"), Fixture.Child->HasMouseCapture());

	PointerEvent->bNowIsTriggerPressed = true;
	TestTrue(TEXT("a held press on this widget is a capture"), Fixture.Child->HasMouseCapture());
	TestFalse(TEXT("but not for a widget that was not pressed"), Fixture.GrandChild->HasMouseCapture());
	TestTrue(TEXT("the same answer addressed by user and pointer id"),
		Fixture.Child->HasMouseCaptureByUser(0, 0));
	TestFalse(TEXT("and no answer for a pointer id that is not this one"),
		Fixture.Child->HasMouseCaptureByUser(0, 7));

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetDesiredSizeComesFromTheMeasuringParentTest,
	"DreamGUI.Widget.Layout.DesiredSizeComesFromTheParentThatMeasuresIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetDesiredSizeComesFromTheMeasuringParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// The child sits in an overlay, which measures it; the answer has to be the panel's, not a
	// re-derivation of it, or the two would drift apart the moment a panel learned a new rule.
	const FVector2D FromPanel = Fixture.RootOverlay->GetDesiredSize(Fixture.Child);
	TestEqual(TEXT("the widget reports what its parent panel measures"), Fixture.Child->GetDesiredSize(), FromPanel);

	// A root has nobody measuring it, so the size it has is the size it wants. Answering zero there
	// would be worse than useless: it reads as "this wants no space".
	TestEqual(TEXT("a widget with no measuring parent reports its own size"),
		Fixture.Root->GetDesiredSize(), Fixture.Root->GetSize());

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRenderTransformAngleIsTheInPlaneRollTest,
	"DreamGUI.Widget.RenderTransform.TheTransformAngleIsTheInPlaneRotationAndLeavesTheOthersAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRenderTransformAngleIsTheInPlaneRollTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// Depth runs along local X here, so the rotation that keeps a widget flat against the canvas --
	// the one UMG's single angle means -- is the roll.
	Fixture.Child->SetRenderRotation(FRotator(20.0f, 30.0f, 0.0f));
	Fixture.Child->SetRenderTransformAngle(45.0f);
	TestEqual(TEXT("the angle reads back"), Fixture.Child->GetRenderTransformAngle(), 45.0f);
	TestEqual(TEXT("it is the roll channel"),
		static_cast<float>(Fixture.Child->GetRenderRotation().Roll), 45.0f);
	// A card mid-flip must not snap flat because a graph set its 2D angle.
	TestEqual(TEXT("pitch is left alone"),
		static_cast<float>(Fixture.Child->GetRenderRotation().Pitch), 20.0f);
	TestEqual(TEXT("yaw is left alone"),
		static_cast<float>(Fixture.Child->GetRenderRotation().Yaw), 30.0f);
	TestTrue(TEXT("the widget now has a render transform"), Fixture.Child->HasRenderTransform());

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRenderShearLeavesTheFastPathAloneTest,
	"DreamGUI.Widget.RenderTransform.AnUnshearedTreeStaysOnTheTransformPathToTheByte",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRenderShearLeavesTheFastPathAloneTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	// The whole reason the feature is allowed to exist: a tree that does not use it must reach the
	// vertex transform with the matrix it always had. Tolerance ZERO, because FMatrix and FTransform
	// do not agree to the last bit and pixel snapping was tuned against the FTransform answer.
	for (const UDreamWidget* Widget : { Fixture.Root, Fixture.Child, Fixture.GrandChild })
	{
		TestFalse(TEXT("nothing is sheared to begin with"), Widget->HasShearApplied());
		TestTrue(TEXT("so the drawn matrix is the transform, exactly"),
			Widget->GetWorldMatrix().Equals(Widget->GetWorldTransform().ToMatrixWithScale(), 0.0));
	}
	TestTrue(TEXT("and the inherited correction is identity"),
		Fixture.GrandChild->GetInheritedShearCorrection().Equals(FMatrix::Identity, 0.0));

	// Setting it and taking it away has to return to that same byte-for-byte answer, or an animation
	// that shears back to zero would leave the tree permanently on the slow path.
	Fixture.Root->SetRenderShear(FVector2D(30.0, 0.0));
	TestTrue(TEXT("shearing the root puts the subtree on the matrix path"),
		Fixture.GrandChild->HasShearApplied());
	Fixture.Root->SetRenderShear(FVector2D::ZeroVector);
	TestFalse(TEXT("clearing it takes the subtree back off"), Fixture.GrandChild->HasShearApplied());
	TestTrue(TEXT("and the matrix is the transform again, exactly"),
		Fixture.GrandChild->GetWorldMatrix().Equals(Fixture.GrandChild->GetWorldTransform().ToMatrixWithScale(), 0.0));

	// Shear is not part of bHasRenderTransform on purpose -- it cannot live in an FTransform -- so
	// ClearRenderTransform needs its own branch, and a widget that is ONLY sheared is the case that
	// branch exists for.
	Fixture.Child->SetRenderShear(FVector2D(0.0, 20.0));
	TestFalse(TEXT("a shear is not a render transform: it cannot survive one"),
		Fixture.Child->HasRenderTransform());
	Fixture.Child->ClearRenderTransform();
	TestFalse(TEXT("clearing the render transform still clears the shear"),
		Fixture.Child->HasOwnRenderShear());

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetRenderShearSlantsTheSubtreeTest,
	"DreamGUI.Widget.RenderTransform.AShearSlantsTheWidgetAndEverythingUnderItWithoutMovingTheLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetRenderShearSlantsTheSubtreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	const double ShearDegrees = 30.0;
	const double ExpectedSlope = FMath::Tan(FMath::DegreesToRadians(ShearDegrees));
	const FTransform LayoutBefore = Fixture.Root->GetWorldTransform();
	const FVector2D SizeBefore = Fixture.Root->GetSize();

	Fixture.Root->SetRenderShear(FVector2D(ShearDegrees, 0.0));

	// Layout is untouched. This is the line between a render channel and a layout one, and a shear
	// that moved the rect would make every panel above it re-measure for a decoration.
	TestTrue(TEXT("the layout transform did not move"),
		Fixture.Root->GetWorldTransform().Equals(LayoutBefore, 0.0));
	TestEqual(TEXT("and neither did the size"), Fixture.Root->GetSize(), SizeBefore);

	// Read the drawn positions back in the SHEARED widget's own frame, which is where the slant is
	// defined: a point h above the pivot is drawn h*tan(angle) to the right of where it sits.
	const FTransform RootInverse = Fixture.Root->GetWorldTransform().Inverse();
	const FMatrix RootDrawn = Fixture.Root->GetWorldMatrix();
	const FVector AtPivot = RootInverse.TransformPosition(RootDrawn.TransformPosition(FVector::ZeroVector));
	const FVector Above = RootInverse.TransformPosition(RootDrawn.TransformPosition(FVector(0.0, 0.0, 100.0)));
	TestTrue(TEXT("the pivot itself does not move"), AtPivot.IsNearlyZero(UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("a point above it slides sideways by tan(angle) per unit"),
		Above.Y, 100.0 * ExpectedSlope, 0.001);
	TestEqual(TEXT("and does not move up or down"), Above.Z, 100.0, 0.001);

	// The subtree comes with it, which is what makes this useful and what a per-widget matrix would
	// not give: the grandchild is not sheared itself and is still drawn slanted.
	TestFalse(TEXT("the grandchild declares no shear of its own"), Fixture.GrandChild->HasOwnRenderShear());
	const FVector InnerLocal(0.0, 0.0, 50.0);
	const FVector UnshearedInRoot =
		RootInverse.TransformPosition(Fixture.GrandChild->GetWorldTransform().TransformPosition(InnerLocal));
	const FVector DrawnInRoot =
		RootInverse.TransformPosition(Fixture.GrandChild->GetWorldMatrix().TransformPosition(InnerLocal));
	TestEqual(TEXT("a descendant's vertex slides by the same rule, measured in the sheared frame"),
		DrawnInRoot.Y, UnshearedInRoot.Y + UnshearedInRoot.Z * ExpectedSlope, 0.001);
	TestEqual(TEXT("and keeps its height"), DrawnInRoot.Z, UnshearedInRoot.Z, 0.001);

	Fixture.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetVisibleAndRenderedAreDifferentQuestionsTest,
	"DreamGUI.Widget.Queries.AFadedSubtreeIsVisibleButNotRendered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetVisibleAndRenderedAreDifferentQuestionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetBaseParityTestLocal;
	FScopedTestWorld TestWorld;
	FThreeLevelFixture Fixture;
	if (!TestTrue(TEXT("fixture builds"), Fixture.Build(TestWorld.World)))
	{
		Fixture.Destroy();
		return false;
	}

	TestTrue(TEXT("a plain widget is visible"), Fixture.GrandChild->IsVisible());
	TestTrue(TEXT("and rendered"), Fixture.GrandChild->IsRendered());

	// Opacity is the distinction UMG draws between the two: faded to nothing still occupies its
	// place and still answers hit tests, it simply contributes no colour.
	Fixture.Root->SetRenderOpacity(0.0f);
	TestTrue(TEXT("a faded subtree is still visible"), Fixture.GrandChild->IsVisible());
	TestFalse(TEXT("but it is not rendered"), Fixture.GrandChild->IsRendered());

	Fixture.Root->SetRenderOpacity(1.0f);
	Fixture.Root->SetVisibility(EDreamWidgetVisibility::Collapsed);
	TestFalse(TEXT("a collapsed ancestor takes the subtree off screen"), Fixture.GrandChild->IsVisible());
	TestFalse(TEXT("and therefore out of rendering"), Fixture.GrandChild->IsRendered());

	Fixture.Destroy();
	return true;
}

#endif
