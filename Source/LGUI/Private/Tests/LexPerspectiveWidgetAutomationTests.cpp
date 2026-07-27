// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexPerspective.h"
#include "Engine/World.h"

/*
 * Perspective as it appears on a widget: declared by an ancestor, inherited by the subtree.
 *
 * The maths is pinned separately and does not need restating here. What these are about is the
 * wiring: that the scope is built from the right plane and eye, that the cached bit tracks the
 * hierarchy as it changes, that a declaring widget does not move itself, and above all that turning
 * a perspective on does not disturb layout -- which is the reason this was worth building rather
 * than opting widgets out of layout one at a time.
 */

namespace LexPerspectiveWidgetTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveInheritanceTest,
	"LGUI.Perspective.Widget.TheScopeCoversTheSubtreeAndNothingAbove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveInheritanceTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	ULexWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	ULexWidget* Pip = MakeWidget(TestWorld.World, Card, TEXT("Pip"), 10.0f, 10.0f);
	ULexWidget* Hud = MakeWidget(TestWorld.World, Root, TEXT("Hud"), 200.0f, 50.0f);

	TestFalse(TEXT("Nothing declares one to begin with"), Table->HasPerspectiveInHierarchy());
	TestFalse(TEXT("...so nothing inherits one"), Card->HasInheritedPerspective());

	Table->SetPerspective(true);

	// Everything under the declaring widget is inside the scope, however deep.
	TestTrue(TEXT("The declaring widget reports the scope"), Table->HasPerspectiveInHierarchy());
	TestTrue(TEXT("A child inherits it"), Card->HasInheritedPerspective());
	TestTrue(TEXT("So does a grandchild"), Pip->HasInheritedPerspective());
	// ...and nothing outside it does. A perspective on the card table must not tilt the HUD.
	TestFalse(TEXT("A sibling subtree does not"), Hud->HasInheritedPerspective());
	TestFalse(TEXT("Nor does the ancestor above it"), Root->HasInheritedPerspective());
	// The declaring widget is not inside its own scope: CSS perspective applies to descendants.
	TestFalse(TEXT("The declaring widget is not inside its own scope"), Table->HasInheritedPerspective());

	// The bit has to follow the hierarchy, not just the moment it was set.
	Card->TrySetParent(Root, false);
	TestFalse(TEXT("A widget moved out of the scope leaves it"), Card->HasInheritedPerspective());
	TestFalse(TEXT("...taking its subtree with it"), Pip->HasInheritedPerspective());
	Card->TrySetParent(Table, false);
	TestTrue(TEXT("And moving back in re-enters it"), Card->HasInheritedPerspective());
	TestTrue(TEXT("...subtree included"), Pip->HasInheritedPerspective());

	Table->SetPerspective(false);
	TestFalse(TEXT("Turning it off clears the subtree"), Card->HasInheritedPerspective());
	TestFalse(TEXT("...all of it"), Pip->HasInheritedPerspective());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveLayoutUntouchedTest,
	"LGUI.Perspective.Widget.LayoutDoesNotNoticeAndTheScopeDoesNotMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveLayoutUntouchedTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 800.0f, 600.0f);
	ULexWidget* Table = MakeWidget(TestWorld.World, Panel, TEXT("Table"), 400.0f, 300.0f);
	ULexWidget* First = MakeWidget(TestWorld.World, Table, TEXT("First"), 100.0f, 40.0f);
	ULexWidget* Second = MakeWidget(TestWorld.World, Table, TEXT("Second"), 100.0f, 40.0f);
	Table->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexWidget::MarkLayoutForRebuild(Panel);
	ULexWidget::RebuildLayoutImmediately(Panel);

	const FVector TableWorldBefore = Table->GetWorldTransform().GetLocation();
	const FVector FirstAuthoredBefore = First->GetRelativeLocation();
	const FVector2D FirstAnchoredBefore = First->GetAnchorData().AnchoredPosition;
	const FVector SecondWorldBefore = Second->GetWorldTransform().GetLocation();

	Table->SetPerspective(true);
	Table->SetPerspectiveDistance(400.0f);
	ULexWidget::MarkLayoutForRebuild(Panel);
	ULexWidget::RebuildLayoutImmediately(Panel);

	// Declaring a perspective must not move the declarer: the remap fixes its plane pointwise, which
	// is what lets an author switch it on and see nothing change until something gains depth.
	TestTrue(TEXT("The declaring widget did not move"),
		Table->GetWorldTransform().GetLocation().Equals(TableWorldBefore, 0.001));
	// Nor may it touch layout, which is the whole reason for the feature.
	TestTrue(TEXT("The authored position is untouched"),
		First->GetRelativeLocation().Equals(FirstAuthoredBefore, 0.001));
	TestTrue(TEXT("The anchors are untouched"),
		First->GetAnchorData().AnchoredPosition.Equals(FirstAnchoredBefore, 0.001));
	TestTrue(TEXT("A sibling was not rearranged"),
		Second->GetWorldTransform().GetLocation().Equals(SecondWorldBefore, 0.001));

	// A child that has no depth is drawn exactly where it was: the remap only touches the depth
	// direction, which is what makes a flat interface cost nothing to put inside a scope.
	TestTrue(TEXT("A flat child is drawn where it always was"),
		First->GetWorldMatrix().TransformPosition(FVector::ZeroVector)
			.Equals(First->GetWorldTransform().GetLocation(), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveForeshortensDepthTest,
	"LGUI.Perspective.Widget.TheCanvasEyeSeesWhatTheScopeEyeWould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveForeshortensDepthTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexCanvas* Canvas = Root->AddComponent<ULexCanvas>();
	if (!TestNotNull(TEXT("The root has a canvas"), Canvas))return false;
	ULexWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	ULexWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	Card->SetRelativeLocation(FVector(0.0, 150.0, 0.0));//off to one side, so foreshortening has a direction

	Table->SetPerspective(true);
	Table->SetPerspectiveDistance(300.0f);
	if (!TestTrue(TEXT("The card is inside the scope"), Card->HasInheritedPerspective()))return false;
	if (!TestNotNull(TEXT("The card renders through the canvas"), Card->GetRenderCanvas()))return false;

	LexPerspective::FScope Scope;
	if (!TestTrue(TEXT("The table declares a scope"), Table->GetPerspectiveScope(Scope)))return false;
	const FVector CanvasEye = Card->GetRenderCanvas()->GetRootCanvas()->GetViewLocation();

	// The property, at the widget level: where the canvas's eye sees the REMAPPED card must be
	// where the scope's own eye would have seen the UNremapped one. Comparing world positions
	// instead would prove nothing -- with the origin on the axis the remap only moves depth, and
	// the visible foreshortening appears when the projection divides by that depth.
	auto ImageOnScopePlane = [&Scope](const FVector& Eye, const FVector& Point, FVector& Out)
	{
		return LexPerspective::ProjectOntoPlane(Eye, Point, Scope.PlanePoint, Scope.PlaneNormal, Out);
	};

	for (const double Depth : { 80.0, -80.0, 0.0 })
	{
		Card->SetRenderTranslation(FVector(Depth, 0.0, 0.0));
		const FVector Authored = Card->GetWorldTransform().GetLocation();
		const FVector Drawn = Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector);

		FVector SeenByScopeEye, SeenByCanvasEye;
		if (!TestTrue(TEXT("The scope eye sees the card"), ImageOnScopePlane(Scope.EyePosition, Authored, SeenByScopeEye)))continue;
		if (!TestTrue(TEXT("The canvas eye sees the drawn card"), ImageOnScopePlane(CanvasEye, Drawn, SeenByCanvasEye)))continue;
		TestTrue(*FString::Printf(TEXT("At depth %.0f the two eyes agree"), Depth),
			SeenByScopeEye.Equals(SeenByCanvasEye, 0.05));
	}

	// Direction, measured on the projected image rather than in world space: nearer is further out.
	auto ImageOffsetAtDepth = [&](double Depth)
	{
		Card->SetRenderTranslation(FVector(Depth, 0.0, 0.0));
		FVector Seen;
		ImageOnScopePlane(Scope.EyePosition, Card->GetWorldTransform().GetLocation(), Seen);
		return FMath::Abs(Seen.Y - Scope.PlanePoint.Y);
	};
	const double Flat = ImageOffsetAtDepth(0.0);
	TestTrue(TEXT("Coming closer looks further from the origin"), ImageOffsetAtDepth(80.0) > Flat + 0.5);
	TestTrue(TEXT("Going away looks nearer to it"), ImageOffsetAtDepth(-80.0) < Flat - 0.5);

	// The origin decides what it converges on, so moving it must change the answer -- otherwise a
	// scope that ignored PerspectiveOrigin entirely would pass everything above.
	Card->SetRenderTranslation(FVector(-80.0, 0.0, 0.0));
	const FVector DrawnCentreOrigin = Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector);
	Table->SetPerspectiveOrigin(FVector2D(0.0, 0.5));
	const FVector DrawnEdgeOrigin = Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector);
	TestFalse(TEXT("Moving the origin changes where it converges"),
		DrawnEdgeOrigin.Equals(DrawnCentreOrigin, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveDisabledIsExactTest,
	"LGUI.Perspective.Widget.WithoutAScopeTheMatrixIsTheTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveDisabledIsExactTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	ULexWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 140.0f);
	Child->SetRenderTranslation(FVector(40.0, 25.0, -10.0));
	Child->SetRenderRotation(FRotator(0.0, 30.0, 12.0));

	// With no scope anywhere the matrix path must agree with the transform path exactly, not
	// approximately: everything downstream of geometry -- pixel snapping most of all -- was tuned
	// against the FTransform result, and a silently different code path for the common case would
	// shift the whole interface by a fraction of a pixel for no reason.
	TestFalse(TEXT("No scope is in play"), Child->HasInheritedPerspective());
	TestTrue(TEXT("The matrix is exactly the transform"),
		Child->GetWorldMatrix().Equals(Child->GetWorldTransform().ToMatrixWithScale(), 0.0));

	// And the inherited remap really is identity rather than something that merely rounds to it.
	TestTrue(TEXT("The inherited remap is identity"),
		Child->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPerspectiveDeclarerNotInOwnScopeTest,
	"LGUI.Perspective.Widget.ADeclarerIsShapedByTheScopesAboveItOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPerspectiveDeclarerNotInOwnScopeTest::RunTest(const FString& Parameters)
{
	using namespace LexPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->AddComponent<ULexCanvas>();
	ULexWidget* Outer = MakeWidget(TestWorld.World, Root, TEXT("Outer"), 500.0f, 400.0f);
	ULexWidget* Inner = MakeWidget(TestWorld.World, Outer, TEXT("Inner"), 300.0f, 200.0f);
	ULexWidget* Leaf = MakeWidget(TestWorld.World, Inner, TEXT("Leaf"), 60.0f, 60.0f);

	Outer->SetPerspective(true);
	Outer->SetPerspectiveDistance(700.0f);
	Inner->SetPerspective(true);
	Inner->SetPerspectiveDistance(250.0f);

	const FVector CanvasEye = Root->GetComponent<ULexCanvas>()->GetRootCanvas()->GetViewLocation();

	// A widget that declares a perspective is shaped by the scopes ABOVE it and not by its own --
	// CSS perspective applies to descendants. Getting this wrong is invisible on a single scope,
	// because a lone declarer has nothing above it to disagree with; it only shows once one
	// declarer sits inside another.
	LexPerspective::FScope OuterScope;
	if (!TestTrue(TEXT("The outer widget declares a scope"), Outer->GetPerspectiveScope(OuterScope)))return false;
	const FMatrix OuterOnly = LexPerspective::MakeRemap(OuterScope, CanvasEye);

	TestTrue(TEXT("The inner declarer is shaped by the outer scope alone"),
		Inner->GetInheritedPerspectiveRemap().Equals(OuterOnly, 0.0001));
	// The outermost declarer has nothing above it at all.
	TestTrue(TEXT("The outer declarer is shaped by nothing"),
		Outer->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0));

	// A leaf below both is shaped by both, and that has to differ from being shaped by one.
	const FMatrix LeafRemap = Leaf->GetInheritedPerspectiveRemap();
	TestFalse(TEXT("A leaf inside both is shaped differently from one inside only the outer"),
		LeafRemap.Equals(OuterOnly, 0.01));

	// Same thing said through a point with depth, so the claim is about what gets drawn and not
	// only about matrix contents.
	Inner->SetRenderTranslation(FVector(50.0, 0.0, 0.0));
	const FVector InnerDrawn = Inner->GetWorldMatrix().TransformPosition(FVector::ZeroVector);
	const FVector InnerByOuterOnly = (Inner->GetWorldTransform().ToMatrixWithScale() * OuterOnly)
		.TransformPosition(FVector::ZeroVector);
	TestTrue(TEXT("The inner declarer is drawn where the outer scope alone puts it"),
		InnerDrawn.Equals(InnerByOuterOnly, 0.01));
	return true;
}

#endif
