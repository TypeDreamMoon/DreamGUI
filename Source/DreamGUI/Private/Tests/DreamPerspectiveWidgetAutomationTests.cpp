// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamPerspective.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"

/*
 * Perspective as it appears on a widget: declared by an ancestor, inherited by the subtree.
 *
 * The maths is pinned separately and does not need restating here. What these are about is the
 * wiring: that the scope is built from the right plane and eye, that the cached bit tracks the
 * hierarchy as it changes, that a declaring widget does not move itself, and above all that turning
 * a perspective on does not disturb layout -- which is the reason this was worth building rather
 * than opting widgets out of layout one at a time.
 */

namespace DreamPerspectiveWidgetTestLocal
{
	/**
	 * An EDITOR world, not a game world, and that is load-bearing rather than incidental.
	 *
	 * Setting a canvas to ScreenSpaceOverlay runs CheckAndApplyViewportParameter, which asks
	 * GetViewportSize() and then RESIZES the root widget to the answer. In a game world with no
	 * player controller -- every headless test -- that answer is its FIntPoint(2, 2) fallback, so
	 * every canvas in this file used to be two units wide with its eye 1.7 units off the plane,
	 * while the widgets under it were hundreds of units across. Everything asserted here is a
	 * relationship rather than a magnitude, so it all passed; it was simply passing about a
	 * configuration no author will ever have. In an editor world GetViewportSize() derives from the
	 * widget itself, so the authored size survives every later projection setter.
	 *
	 * UDreamUIManagerWorldSubsystem is still present -- UWorldSubsystem::DoesSupportWorldType admits
	 * Editor by default and it does not override that -- so layout still rebuilds. Only
	 * UDreamScreenUISubsystem opts out of editor worlds, and nothing here goes through the viewport.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** The scale the whole file assumes. Asserted, not hoped for -- see FScopedGameWorld. */
	bool CanvasKeptItsSize(const UDreamWidget* InRoot, float InExpectedWidth)
	{
		return InRoot != nullptr && FMath::IsNearlyEqual(InRoot->GetWidth(), InExpectedWidth, 0.01f);
	}

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
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
	FDreamPerspectiveInheritanceTest,
	"DreamGUI.Perspective.Widget.TheScopeCoversTheSubtreeAndNothingAbove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveInheritanceTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	// One assertion for the whole file: the canvas must not have resized the root out from under
	// the fixture. Everything here is a relationship rather than a magnitude, so all of it passed
	// for a long time on a 2x2 canvas whose eye stood 1.7 units off the plane -- a configuration no
	// author will ever have. See FScopedGameWorld for why the world type is what prevents it.
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	if (!TestTrue(TEXT("The canvas leaves the fixture's authored size alone"),
		CanvasKeptItsSize(Root, 800.0f)))return false;
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	UDreamWidget* Pip = MakeWidget(TestWorld.World, Card, TEXT("Pip"), 10.0f, 10.0f);
	UDreamWidget* Hud = MakeWidget(TestWorld.World, Root, TEXT("Hud"), 200.0f, 50.0f);

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
	// The declarer is inside its own scope -- unlike CSS, and on purpose, so that flipping one card
	// does not require wrapping it in a parent that exists for no other reason.
	TestTrue(TEXT("The declaring widget is inside its own scope"), Table->HasPerspectiveApplied());
	TestFalse(TEXT("...though no ANCESTOR declares one for it"), Table->HasInheritedPerspective());

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
	FDreamPerspectiveLayoutUntouchedTest,
	"DreamGUI.Perspective.Widget.LayoutDoesNotNoticeAndTheScopeDoesNotMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveLayoutUntouchedTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Panel = MakeWidget(TestWorld.World, nullptr, TEXT("Panel"), 800.0f, 600.0f);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Panel, TEXT("Table"), 400.0f, 300.0f);
	UDreamWidget* First = MakeWidget(TestWorld.World, Table, TEXT("First"), 100.0f, 40.0f);
	UDreamWidget* Second = MakeWidget(TestWorld.World, Table, TEXT("Second"), 100.0f, 40.0f);
	Table->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);

	const FVector TableWorldBefore = Table->GetWorldTransform().GetLocation();
	const FVector FirstAuthoredBefore = First->GetRelativeLocation();
	const FVector2D FirstAnchoredBefore = First->GetAnchorData().AnchoredPosition;
	const FVector SecondWorldBefore = Second->GetWorldTransform().GetLocation();

	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(53.0f);
	UDreamWidget::MarkLayoutForRebuild(Panel);
	UDreamWidget::RebuildLayoutImmediately(Panel);

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
	FDreamPerspectiveForeshortensDepthTest,
	"DreamGUI.Perspective.Widget.TheCanvasEyeSeesWhatTheScopeEyeWould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveForeshortensDepthTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("The root has a canvas"), Canvas))return false;
	// A fresh canvas is WorldSpace, where the scene camera does the projecting; perspective only
	// applies where the canvas projects through its own virtual camera.
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	Card->SetRelativeLocation(FVector(0.0, 150.0, 0.0));//off to one side, so foreshortening has a direction

	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(67.0f);
	if (!TestTrue(TEXT("The card is inside the scope"), Card->HasInheritedPerspective()))return false;
	if (!TestNotNull(TEXT("The card renders through the canvas"), Card->GetRenderCanvas()))return false;

	DreamPerspective::FScope Scope;
	if (!TestTrue(TEXT("The table declares a scope"), Table->GetPerspectiveScope(Scope)))return false;
	const FVector CanvasEye = Card->GetRenderCanvas()->GetRootCanvas()->GetViewLocation();

	// The property, at the widget level: where the canvas's eye sees the REMAPPED card must be
	// where the scope's own eye would have seen the UNremapped one. Comparing world positions
	// instead would prove nothing -- with the origin on the axis the remap only moves depth, and
	// the visible foreshortening appears when the projection divides by that depth.
	auto ImageOnScopePlane = [&Scope](const FVector& Eye, const FVector& Point, FVector& Out)
	{
		return DreamPerspective::ProjectOntoPlane(Eye, Point, Scope.PlanePoint, Scope.PlaneNormal, Out);
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
	// +X points INTO the screen and the eye stands at -X, so a POSITIVE render translation moves the
	// card AWAY. The pinhole pins the magnitudes too, not just the ordering: at eye distance D an
	// offset scales by D/(D + Depth), so 150 at the plane reads 118.6 at +80 and 204.0 at -80.
	const double Flat = ImageOffsetAtDepth(0.0);
	TestTrue(TEXT("Going away looks nearer to the origin"), ImageOffsetAtDepth(80.0) < Flat - 0.5);
	TestTrue(TEXT("Coming closer looks further from it"), ImageOffsetAtDepth(-80.0) > Flat + 0.5);

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
	FDreamPerspectiveDisabledIsExactTest,
	"DreamGUI.Perspective.Widget.WithoutAScopeTheMatrixIsTheTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveDisabledIsExactTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 140.0f);
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
	FDreamPerspectiveDeclarerIsInOwnScopeTest,
	"DreamGUI.Perspective.Widget.ADeclarerIsShapedByItsOwnScopeAndThoseAboveIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveDeclarerIsInOwnScopeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Outer = MakeWidget(TestWorld.World, Root, TEXT("Outer"), 500.0f, 400.0f);
	UDreamWidget* Inner = MakeWidget(TestWorld.World, Outer, TEXT("Inner"), 300.0f, 200.0f);
	UDreamWidget* Leaf = MakeWidget(TestWorld.World, Inner, TEXT("Leaf"), 60.0f, 60.0f);

	Outer->SetPerspective(true);
	// The angles matter here. A 40 degree scope on a 500-wide widget puts its eye at ~687, and the
	// 800-wide canvas puts its own at ~693 -- near enough that the outer remap collapses to identity
	// and every assertion below passes for the wrong reason. Chosen so all three eyes are far apart.
	Outer->SetPerspectiveFieldOfView(100.0f);
	Inner->SetPerspective(true);
	Inner->SetPerspectiveFieldOfView(120.0f);

	const FVector CanvasEye = Root->GetComponent<UDreamCanvas>()->GetRootCanvas()->GetViewLocation();

	// A declarer is shaped by its OWN scope and by every one above it. That is a deliberate
	// departure from CSS, where perspective applies only to descendants: there, flipping a single
	// card with perspective means wrapping it in a parent that exists for no other reason. Here the
	// scope's plane comes from where layout put the declarer, so the declarer's own rotation is a
	// departure from that plane and foreshortens, while a declarer with no render transform lies in
	// the plane and is untouched -- so turning Perspective on still changes nothing by itself.
	DreamPerspective::FScope OuterScope;
	if (!TestTrue(TEXT("The outer widget declares a scope"), Outer->GetPerspectiveScope(OuterScope)))return false;
	const FMatrix OuterOnly = DreamPerspective::MakeRemap(OuterScope, CanvasEye);

	// Guard against the assertions below being satisfied by everything collapsing to identity.
	TestFalse(TEXT("The outer scope actually does something"), OuterOnly.Equals(FMatrix::Identity, 0.01));
	// The inner declarer carries its own scope on top of the outer one, so it is NOT the outer
	// scope alone -- that was the old rule and is what this test was written to catch changing.
	TestFalse(TEXT("The inner declarer is not shaped by the outer scope alone"),
		Inner->GetInheritedPerspectiveRemap().Equals(OuterOnly, 0.01));
	// The outermost declarer has only its own.
	DreamPerspective::FScope OuterOwn;
	Outer->GetPerspectiveScope(OuterOwn);
	TestTrue(TEXT("The outer declarer is shaped by its own scope"),
		Outer->GetInheritedPerspectiveRemap().Equals(DreamPerspective::MakeRemap(OuterOwn, CanvasEye), 0.0001));
	// But a declarer with nothing off its layout plane is still left exactly alone, which is what
	// keeps switching the checkbox on from moving a flat interface.
	TestTrue(TEXT("A declarer with no render transform is not moved by its own scope"),
		Outer->GetWorldMatrix().TransformPosition(FVector::ZeroVector)
			.Equals(Outer->GetWorldTransform().GetLocation(), 0.01));

	// A leaf below both is shaped by both, and that has to differ from being shaped by one.
	const FMatrix LeafRemap = Leaf->GetInheritedPerspectiveRemap();
	TestFalse(TEXT("A leaf inside both is shaped differently from one inside only the outer"),
		LeafRemap.Equals(OuterOnly, 0.01));

	// And the point of self-inclusion, said through geometry: a declarer that rotates itself is
	// foreshortened by its own perspective, with no wrapper widget in sight.
	Inner->SetRenderRotation(FRotator(0.0, 50.0, 0.0));
	UDreamWidget* Corner = MakeWidget(TestWorld.World, Inner, TEXT("Corner"), 10.0f, 10.0f);
	Corner->SetRelativeLocation(FVector(0.0, 120.0, 0.0));
	const FVector WithOwnScope = Corner->GetWorldMatrix().TransformPosition(FVector::ZeroVector);
	const FVector WithoutAnyScope = Corner->GetWorldTransform().GetLocation();
	TestFalse(TEXT("A declarer's own rotation is foreshortened by its own scope"),
		WithOwnScope.Equals(WithoutAnyScope, 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveHitTestFollowsTheDrawingTest,
	"DreamGUI.Perspective.Widget.ClicksLandWhereTheWidgetIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveHitTestFollowsTheDrawingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	Card->SetRelativeLocation(FVector(0.0, 150.0, 0.0));
	UDreamVisual* Visual = Card->CreateNewVisual<UDreamVisualEmpty>();
	if (!TestNotNull(TEXT("The card has a visual to hit"), Visual))return false;

	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(67.0f);
	// The origin is deliberately off to one side. With it directly in front of the card the remap
	// would move the card only along its own normal, and a ray fired down that normal cannot tell a
	// plane from the same plane shifted along it -- the test would pass whatever the hit test did.
	// Off to the side, depth turns into sideways displacement, which a ray can actually miss.
	Table->SetPerspectiveOrigin(FVector2D(0.0, 0.5));
	Card->SetRenderTranslation(FVector(120.0, 0.0, 0.0));//pulled toward the viewer
	if (!TestTrue(TEXT("The card is inside the scope"), Card->HasInheritedPerspective()))return false;

	// A ray at the card's DRAWN centre must hit it. Before this commit the hit test inverted the
	// widget's FTransform and intersected the un-foreshortened rect, so a foreshortened card was
	// clickable where it used to be and not where it is -- which is the kind of bug that gets
	// blamed on the artist.
	const FVector Drawn = FVector(Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	const FVector Along = FVector(Card->GetWorldMatrix().TransformVector(FVector(1, 0, 0))).GetSafeNormal();
	{
		FDreamUIHitResult Hit;
		TestTrue(TEXT("A ray through where it is drawn hits it"),
			Visual->LineTraceUI(Hit, Drawn + Along * 500.0, Drawn - Along * 500.0));
	}

	// And a ray at where it merely used to be must miss, or the test would pass for a hit area that
	// simply got bigger rather than one that moved.
	const FVector Authored = Card->GetWorldTransform().GetLocation();
	// Sideways, specifically: the card is 100 wide, so the drawn centre has to be more than half of
	// that away from the authored one before a ray through the old place can be expected to miss.
	if (TestTrue(TEXT("The card is drawn more than half its width to the side"),
		FMath::Abs(Drawn.Y - Authored.Y) > 50.0))
	{
		FDreamUIHitResult Hit;
		const FVector AuthoredAlong = Card->GetWorldTransform().TransformVector(FVector(1, 0, 0)).GetSafeNormal();
		TestFalse(TEXT("A ray through where it is no longer drawn misses"),
			Visual->LineTraceUI(Hit, Authored + AuthoredAlong * 500.0, Authored - AuthoredAlong * 500.0));
	}

	// With no scope the two agree again, which is the case every existing interaction depends on.
	Table->SetPerspective(false);
	{
		FDreamUIHitResult Hit;
		const FVector Centre = Card->GetWorldTransform().GetLocation();
		const FVector Normal = Card->GetWorldTransform().TransformVector(FVector(1, 0, 0)).GetSafeNormal();
		TestTrue(TEXT("Without a scope the ordinary hit test still works"),
			Visual->LineTraceUI(Hit, Centre + Normal * 500.0, Centre - Normal * 500.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveRefusesWhereItCannotApplyTest,
	"DreamGUI.Perspective.Widget.InertWhereTheCanvasIsNotTheOneProjecting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveRefusesWhereItCannotApplyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("The root has a canvas"), Canvas))return false;
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	Table->SetPerspective(true);
	Card->SetRenderTranslation(FVector(90.0, 0.0, 0.0));//real depth, so there is something to foreshorten

	// The feature re-aims geometry at the eye the CANVAS projects from. In a world-space mode the
	// scene camera does the projecting and the canvas's eye is nobody, so re-aiming at it would
	// displace everything for a viewer that does not exist. This is what the designer previews
	// through by default, which is why the answer has to be "inert" and not "approximately right".
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	TestTrue(TEXT("World space leaves the geometry alone"),
		Card->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0));
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
	TestTrue(TEXT("So does world space through the DreamUI renderer"),
		Card->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0));

	// An orthographic canvas has its eye at infinity, and no affine map sends a finite point there.
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	Canvas->SetProjectionType(ECameraProjectionMode::Orthographic);
	TestTrue(TEXT("An orthographic canvas has no perspective to share"),
		Card->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0));

	// And where the canvas really is the one projecting, it applies -- otherwise the assertions
	// above would pass for a feature that never worked at all.
	Canvas->SetProjectionType(ECameraProjectionMode::Perspective);
	TestFalse(TEXT("A perspective screen-space canvas does apply it"),
		Card->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveSurvivesLoadTest,
	"DreamGUI.Perspective.Widget.AScopeDeclaredOffTheRootSurvivesALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveSurvivesLoadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);

	// Assembled the way WidgetSerializer assembles a prefab: objects, then raw property writes,
	// then hierarchy through SetParentBeforeRegister -- which fires no attach events -- then
	// registration. Only the ROOT of the loaded tree ever gets a real attach, and its refresh
	// stops recursing the moment a widget's bit does not change. So a declarer at the root happens
	// to work, and a declarer one level down silently loses its scope: the case pinned here.
	auto MakeBare = [&](const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		return Widget;
	};
	UDreamWidget* Sub = MakeBare(TEXT("Sub"), 600.0f, 400.0f);
	UDreamWidget* Table = MakeBare(TEXT("Table"), 400.0f, 300.0f);
	UDreamWidget* Card = MakeBare(TEXT("Card"), 100.0f, 140.0f);

	FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(TEXT("bPerspective"));
	if (!TestNotNull(TEXT("bPerspective exists"), Property))return false;
	*Property->ContainerPtrToValuePtr<bool>(Table) = true;

	Card->SetParentBeforeRegister(Table);
	Table->SetParentBeforeRegister(Sub);
	Sub->OnRegister();
	Table->OnRegister();
	Card->OnRegister();

	TestTrue(TEXT("The declarer knows its own scope after registration"), Table->HasPerspectiveInHierarchy());
	TestTrue(TEXT("...and the subtree inherited it"), Card->HasInheritedPerspective());

	// The one real attach a loaded tree gets. Sub's own bit does not change here, so without the
	// registration-time refresh the recursion never reaches Table and the scope stays lost.
	Sub->TrySetParent(Root, false);
	TestTrue(TEXT("Still inside the scope after the real attach"), Card->HasInheritedPerspective());
	TestFalse(TEXT("And the scope genuinely shapes geometry under the screen-space canvas"),
		Card->GetInheritedPerspectiveRemap().Equals(FMatrix::Identity, 0.0001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveEyeSideTest,
	"DreamGUI.Perspective.Widget.TheScopeEyeIsOnTheSameSideAsTheCanvasEye",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveEyeSideTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("The root has a canvas"), Canvas))return false;
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 400.0f, 300.0f);
	Table->SetPerspective(true);

	DreamPerspective::FScope Scope;
	if (!TestTrue(TEXT("The table declares a scope"), Table->GetPerspectiveScope(Scope)))return false;

	// A widget's local +X points INTO the screen: UDreamCanvas::GetViewLocation places the viewer at
	// "location - forward * distance". So a scope eye placed along +Normal would sit behind the
	// plane, on the opposite side from the eye that is actually looking, and the remap would invert
	// the foreshortening -- near reading as far. Rather than hard-coding a sign that a later reader
	// could "correct", this asserts the relationship: both eyes stand off the plane the same way.
	const FVector PlaneToCanvasEye = Canvas->GetViewLocation() - Scope.PlanePoint;
	const FVector PlaneToScopeEye = Scope.EyePosition - Scope.PlanePoint;
	const double CanvasSide = FVector::DotProduct(Scope.PlaneNormal, PlaneToCanvasEye);
	const double ScopeSide = FVector::DotProduct(Scope.PlaneNormal, PlaneToScopeEye);

	TestTrue(TEXT("The canvas eye is off the plane at all"), FMath::Abs(CanvasSide) > 1.0);
	TestTrue(TEXT("The scope eye is off the plane at all"), FMath::Abs(ScopeSide) > 1.0);
	TestTrue(TEXT("Both eyes are on the same side of the plane"),
		FMath::Sign(CanvasSide) == FMath::Sign(ScopeSide));

	// And the consequence an author would notice. Measuring it takes care, because the obvious
	// measure is unfalsifiable: the remap is rank-one along the plane normal, so with the origin on
	// the scope axis it moves a child in DEPTH only and its drawn world Y never budges however deep
	// it goes. This test used to assert on exactly that Y and could not have failed. So pin the
	// invariance as its own claim, then measure the thing that does move -- the projected image.
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 100.0f, 140.0f);
	Card->SetRelativeLocation(FVector(0.0, 150.0, 0.0));
	auto DrawnAtDepth = [&](double Depth)
	{
		Card->SetRenderTranslation(FVector(Depth, 0.0, 0.0));
		return FVector(Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	};
	TestEqual(TEXT("On the axis the remap changes depth and not lateral position"),
		DrawnAtDepth(-80.0).Y, DrawnAtDepth(80.0).Y, 0.01);

	// Projected from the SCOPE eye, which is what this test is about: were that eye placed on the
	// far side of the plane, every ordering below would come out reversed.
	auto ImageOffsetAtDepth = [&](double Depth) -> double
	{
		Card->SetRenderTranslation(FVector(Depth, 0.0, 0.0));
		FVector Seen;
		const bool bVisible = DreamPerspective::ProjectOntoPlane(Scope.EyePosition,
			Card->GetWorldTransform().GetLocation(), Scope.PlanePoint, Scope.PlaneNormal, Seen);
		TestTrue(*FString::Printf(TEXT("The scope eye sees the card at depth %.0f"), Depth), bVisible);
		return bVisible ? FMath::Abs(Seen.Y - Scope.PlanePoint.Y) : 0.0;
	};
	const double Flat = ImageOffsetAtDepth(0.0);
	// Toward the viewer is NEGATIVE X, for the same reason the eye is.
	TestTrue(TEXT("Coming toward the viewer pushes it outward"), ImageOffsetAtDepth(-80.0) > Flat + 0.5);
	TestTrue(TEXT("Going away pulls it inward"), ImageOffsetAtDepth(80.0) < Flat - 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveIsAuthorableTest,
	"DreamGUI.Perspective.Widget.EveryChannelIsReachableFromTheEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveIsAuthorableTest::RunTest(const FString& Parameters)
{
	// A perspective channel that is not a UPROPERTY still compiles, still has a working setter, and
	// still passes every behaviour test -- it simply never appears in the Details panel, never
	// serializes, and never animates. PerspectiveOrigin shipped that way for exactly one commit,
	// lost when an edit to the property above it swallowed its UPROPERTY line, and nothing caught
	// it because everything that exercises it goes through C++.
	auto CheckProperty = [this](const TCHAR* PropertyName, bool bExpectInterp)
	{
		const FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (!TestNotNull(*FString::Printf(TEXT("%s is a reflected property"), PropertyName), Property))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s is editable in the panel"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_Edit));
		TestEqual(*FString::Printf(TEXT("%s animatability"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_Interp), bExpectInterp);
	};

	CheckProperty(TEXT("bPerspective"), false);
	CheckProperty(TEXT("PerspectiveFieldOfView"), true);
	CheckProperty(TEXT("PerspectiveOrigin"), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveLayoutIsTheWriteBackSpaceTest,
	"DreamGUI.Perspective.Widget.MovingAWidgetIsMeasuredInLayoutSpaceNotDrawnSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveLayoutIsTheWriteBackSpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	// A guard against a plausible future "fix" rather than against today's code.
	//
	// Once the editor viewport projects through the canvas, a widget inside a Perspective scope is
	// DRAWN somewhere other than where it is LAID OUT, and the designer outline and its handles have
	// to pick one. The properties that decide what such a change may and may not do are recorded
	// here, because nothing else in the suite states them: every other perspective test asks where
	// a widget is drawn, not what a setter did or what a picked point means.
	//
	// The short version: moving the DRAW side is safe, and moving the WRITE-BACK is unnecessary --
	// but only because the remap happens to be rigid sideways. What is not safe is deprojecting
	// against the layout plane while drawing on the drawn one, since they are at different depths
	// and one screen pixel is then worth different world distances on each.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1600.0f, 900.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 800.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 120.0f, 160.0f);
	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(70.0f);
	Card->SetRelativeLocation(FVector(0.0, 200.0, 0.0));
	Card->SetRenderTranslation(FVector(-140.0, 0.0, 0.0));//pulled toward the viewer, so it magnifies
	if (!TestTrue(TEXT("The card is inside the scope"), Card->HasInheritedPerspective()))return false;

	const FVector LayoutBefore = Card->GetWorldTransform().GetLocation();
	const FVector DrawnBefore(Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	if (!TestFalse(TEXT("The card really is drawn away from where it is laid out"),
		DrawnBefore.Equals(LayoutBefore, 1.0)))return false;

	// A setter moves the widget by exactly what it was told, measured in LAYOUT space.
	const double Requested = 90.0;
	Card->SetRelativeLocation(FVector(0.0, 200.0 + Requested, 0.0));
	const FVector LayoutAfter = Card->GetWorldTransform().GetLocation();
	TestEqual(TEXT("A setter moves the layout position by exactly what it asked for"),
		LayoutAfter.Y - LayoutBefore.Y, Requested, 0.01);

	// And so does the drawn position -- which is the non-obvious part, and the reason a drag can
	// write its delta straight back through a setter. The remap is rank-one along the plane normal:
	// it displaces by an amount that depends ONLY on depth, so it translates each depth-plane
	// rigidly and a sideways move passes through it unchanged. Were it a lateral scale instead, a
	// delta measured on the drawn surface would have to be divided before being written back.
	const FVector DrawnAfter(Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	TestEqual(TEXT("Sideways motion passes through the remap one for one"),
		DrawnAfter.Y - DrawnBefore.Y, Requested, 0.01);
	// Depth is the direction it is NOT rigid in, which is what makes the two surfaces different
	// things to deproject against: they sit at different depths from the eye, so one screen pixel
	// is worth a different number of world units on each.
	TestFalse(TEXT("Depth is displaced, so the two surfaces are not the same plane"),
		FMath::IsNearlyEqual(DrawnAfter.X, LayoutAfter.X, 1.0));

	// The inverse of the drawn matrix is what converts a picked point back into the space setters
	// speak, which is why a deproject that intersects the DRAWN surface can still write back
	// correctly: local coordinates recovered through GetWorldMatrix are layout-local by
	// construction. Stated as a test so the conversion is not re-derived by guesswork.
	const FVector PickedOnDrawnSurface(Card->GetWorldMatrix().TransformPosition(FVector(0.0, 40.0, -25.0)));
	const FVector RecoveredLocal(Card->GetWorldMatrix().Inverse().TransformPosition(PickedOnDrawnSurface));
	TestTrue(TEXT("Inverting the drawn matrix recovers layout-local coordinates"),
		RecoveredLocal.Equals(FVector(0.0, 40.0, -25.0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveClipFollowsTheDrawnClipperTest,
	"DreamGUI.Perspective.Widget.AClipRectFollowsTheClipperItIsDrawnAs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveClipFollowsTheDrawnClipperTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	// Clipping has two halves that must agree: the GPU builds a canvas-to-clipper matrix, and the
	// CPU inverts the same frame to decide whether a raycast hit is masked. Both were built from
	// the LAYOUT transform while the pixels and hit points they judge are DRAWN, so a clipper with
	// depth inside a perspective scope masked along a rectangle nowhere near its visible border.
	//
	// They were wrong TOGETHER, which is why nothing looked visible-but-unclickable -- and which is
	// exactly why fixing one alone would have been worse than fixing neither. That is the property
	// this test exists to hold, more than the individual correctness of either half.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1600.0f, 900.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 900.0f, 600.0f);
	UDreamWidget* Clipper = MakeWidget(TestWorld.World, Table, TEXT("Clipper"), 300.0f, 200.0f);
	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(80.0f);
	// Off the scope's axis AND off its plane, so the remap displaces it rather than leaving it be.
	Clipper->SetRelativeLocation(FVector(0.0, 260.0, 0.0));
	Clipper->SetRenderTranslation(FVector(-180.0, 0.0, 0.0));
	if (!TestTrue(TEXT("The clipper is inside the scope"), Clipper->HasPerspectiveApplied()))return false;

	const FVector LayoutCentre = Clipper->GetWorldTransform().GetLocation();
	const FVector DrawnCentre(Clipper->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	if (!TestFalse(TEXT("The clipper really is drawn away from its layout position"),
		DrawnCentre.Equals(LayoutCentre, 1.0)))return false;

	// The frame the mask is built in must be the drawn one.
	const FMatrix44d Frame = FDreamUIClipData::GetClipperToWorldMatrix(Clipper);
	TestTrue(TEXT("The clip frame is the drawn frame"),
		FVector(Frame.TransformPosition(FVector::ZeroVector)).Equals(DrawnCentre, 0.01));

	// And the CPU test must invert that same frame, not a different one. Asserted by composition
	// rather than by repeating the arithmetic, so the two cannot drift apart without this failing.
	for (const FVector& Local : { FVector(0.0, 0.0, 0.0), FVector(0.0, 120.0, -70.0), FVector(0.0, -140.0, 90.0) })
	{
		const FVector OnDrawnSurface(Clipper->GetWorldMatrix().TransformPosition(Local));
		const FVector Recovered = FDreamUIClipData::WorldPointToClipperLocal(Clipper, OnDrawnSurface);
		TestTrue(*FString::Printf(TEXT("The CPU test inverts the same frame the GPU builds (%.0f,%.0f)"), Local.Y, Local.Z),
			Recovered.Equals(Local, 0.01));
	}

	// The consequence an author would see: a point on the clipper's drawn surface is inside the
	// mask, and the layout position it used to be judged against is now outside it.
	TestTrue(TEXT("A point on the drawn surface is inside the mask"),
		Clipper->IsPointVisibleOnClip(DrawnCentre));

	// A widget with no perspective must take the untouched path, bit for bit.
	UDreamWidget* Plain = MakeWidget(TestWorld.World, Root, TEXT("Plain"), 300.0f, 200.0f);
	TestFalse(TEXT("The plain widget has no perspective"), Plain->HasPerspectiveApplied());
	const FVector PlainPoint = Plain->GetWorldTransform().TransformPosition(FVector(0.0, 40.0, 20.0));
	TestTrue(TEXT("Without perspective the conversion is the transform's own"),
		FDreamUIClipData::WorldPointToClipperLocal(Plain, PlainPoint)
			.Equals(Plain->GetWorldTransform().InverseTransformPosition(PlainPoint), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPerspectiveRectBlockHitTest,
	"DreamGUI.Perspective.Widget.ClicksLandWhereARectBlockIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPerspectiveRectBlockHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamPerspectiveWidgetTestLocal;
	// The sibling of ClicksLandWhereTheWidgetIsDrawn, which used UDreamVisualEmpty and so exercised
	// the perspective-aware UDreamVisual::LineTraceUIRect. UDreamRectBlock OVERRIDES that function with
	// a copy of the pre-perspective version, so the shipped default drawable never ran any of it --
	// and the existing test was structurally incapable of noticing, because it tested a class that
	// does not override. Whatever else this asserts, it asserts that the override exists.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 1600.0f, 900.0f);
	Root->AddComponent<UDreamCanvas>()->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	UDreamWidget* Table = MakeWidget(TestWorld.World, Root, TEXT("Table"), 900.0f, 600.0f);
	UDreamWidget* Card = MakeWidget(TestWorld.World, Table, TEXT("Card"), 160.0f, 220.0f);
	UDreamVisual* Visual = Card->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("The card has a RectBlock to hit"), Visual))return false;
	Table->SetPerspective(true);
	Table->SetPerspectiveFieldOfView(80.0f);
	Card->SetRelativeLocation(FVector(0.0, 240.0, 0.0));
	// The origin is pushed to the scope's edge on purpose. Centred, the remap displaces along the
	// plane NORMAL only -- pure depth -- and a ray fired down that same normal cannot tell the two
	// positions apart, so the test would pass whether or not the override was fixed. Off to the
	// side, the displacement gains a lateral component a perpendicular ray can actually miss.
	Table->SetPerspectiveOrigin(FVector2D(0.0, 0.5));
	Card->SetRenderTranslation(FVector(-200.0, 0.0, 0.0));
	if (!TestTrue(TEXT("The card is inside the scope"), Card->HasPerspectiveApplied()))return false;

	const FVector Authored = Card->GetWorldTransform().GetLocation();
	const FVector Drawn(Card->GetWorldMatrix().TransformPosition(FVector::ZeroVector));
	// It has to move SIDEWAYS by more than half the card, or the drawn card still covers the
	// authored point and the miss assertion below is unfalsifiable.
	if (!TestTrue(TEXT("The card is drawn clear of where it is authored, laterally"),
		FMath::Abs(Drawn.Y - Authored.Y) > Card->GetWidth() * 0.5f))return false;

	// Rays along the canvas normal, from well in front, at each of the two candidate positions.
	auto TraceAt = [&](const FVector& Target)
	{
		FDreamUIHitResult Hit;
		const FVector Start = Target - FVector(2000.0, 0.0, 0.0);
		const FVector End = Target + FVector(2000.0, 0.0, 0.0);
		return Visual->LineTraceUI(Hit, Start, End);
	};
	TestTrue(TEXT("A ray through where the RectBlock is drawn hits it"), TraceAt(Drawn));
	TestFalse(TEXT("A ray through where it is no longer drawn misses"), TraceAt(Authored));
	return true;
}

#endif
