// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasHierarchyOrderTest,
	"DreamGUI.Canvas.HierarchyOrderInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasHierarchyOrderTest::RunTest(const FString& Parameters)
{
	UDreamWidget* Parent = NewObject<UDreamWidget>();
	UDreamWidget* Back = NewObject<UDreamWidget>(Parent);
	UDreamWidget* Front = NewObject<UDreamWidget>(Parent);
	TestNotNull(TEXT("Parent widget created"), Parent);
	TestNotNull(TEXT("Back widget created"), Back);
	TestNotNull(TEXT("Front widget created"), Front);
	if (!Parent || !Back || !Front)
	{
		return false;
	}

	Back->SiblingIndex = 0;
	Front->SiblingIndex = 1;
	Parent->Children = { Front, Back };
	Parent->bNeedSortUIChildren = true;

	const TArray<UDreamWidget*>& SortedChildren = Parent->GetChildren();
	TestEqual(TEXT("GetChildren returns the back widget first"), SortedChildren[0], Back);
	TestEqual(TEXT("GetChildren returns the front widget last"), SortedChildren[1], Front);

	UDreamCanvas* Canvas = NewObject<UDreamCanvas>();
	TestNotNull(TEXT("Canvas created"), Canvas);
	if (!Canvas)
	{
		return false;
	}

	Canvas->bNeedToGenerateWidgetList = false;
	Canvas->bShouldRebuildDrawCall = false;
	Canvas->bCanTickUpdate = false;
	Canvas->MarkCanvasHierarchyChanged();

	TestTrue(TEXT("Hierarchy changes invalidate the cached widget list"), Canvas->bNeedToGenerateWidgetList);
	TestTrue(TEXT("Hierarchy changes rebuild draw calls"), Canvas->bShouldRebuildDrawCall);
	TestTrue(TEXT("Hierarchy changes wake canvas updates"), Canvas->bCanTickUpdate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasProjectionIsFindableTest,
	"DreamGUI.Canvas.TheProjectionControlsAreNotBuriedInAdvancedDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasProjectionIsFindableTest::RunTest(const FString& Parameters)
{
	// ProjectionType and FieldOfView define the projection that every widget-level Perspective
	// scope is built against: the eye it re-aims geometry at is derived from them, and an
	// orthographic canvas disables the feature entirely. While they sat behind the Advanced twirl
	// an author had no reachable way to calibrate or diagnose perspective. This asserts the
	// reflection flags rather than any behaviour, because behaviour is exactly what stays green
	// when a property becomes unreachable from the panel.
	auto CheckReachable = [this](const TCHAR* PropertyName)
	{
		const FProperty* Property = UDreamCanvas::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (!TestNotNull(*FString::Printf(TEXT("%s is a reflected property"), PropertyName), Property))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s is editable"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_Edit));
		TestFalse(*FString::Printf(TEXT("%s is not hidden behind Advanced"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_AdvancedDisplay));
	};

	CheckReachable(TEXT("ProjectionType"));
	CheckReachable(TEXT("FieldOfView"));

	// The clip planes stay advanced -- they are genuine tuning, not calibration, and this pins the
	// distinction so the change above is not read as "un-advance everything in the category".
	const FProperty* NearClip = UDreamCanvas::StaticClass()->FindPropertyByName(TEXT("NearClipPlane"));
	if (TestNotNull(TEXT("NearClipPlane is a reflected property"), NearClip))
	{
		TestTrue(TEXT("NearClipPlane stays behind Advanced"),
			NearClip->HasAnyPropertyFlags(CPF_AdvancedDisplay));
	}
	return true;
}

namespace DreamCanvasProjectionTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasExactFitTest,
	"DreamGUI.Canvas.TheCanvasRectExactlyFillsItsOwnCameraFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasExactFitTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasProjectionTestLocal;
	// The invariant the editor preview rests on. CalculateDistanceToCamera pulls the eye back to
	// exactly (Width/2) / tan(FOV/2), which is the standoff at which the canvas rect maps to the
	// NDC square on BOTH axes -- the vertical falling out of the aspect multiplier rather than
	// being arranged. Two consequences depend on it, and neither is obvious enough to leave
	// unasserted. Standing the editor camera at this eye reproduces the shipped framing exactly.
	// And a point IN the canvas plane lands on the same NDC an orthographic view of the same rect
	// would give it -- both reduce to 2Y/Width -- which is why a perspective preview can be offered
	// without moving where flat widgets sit, and so without disturbing alignment work.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetDisplayName(TEXT("Root"));
	Root->SetWidth(1600.0f);
	Root->SetHeight(900.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("Canvas created"), Canvas))return false;
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	Canvas->SetProjectionType(ECameraProjectionMode::Perspective);

	// Size AFTER the render mode, and then check it stuck. Setting ScreenSpaceOverlay runs
	// CheckAndApplyViewportParameter, which asks GetViewportSize() -- and in a game world with no
	// player controller, as here, that returns its FIntPoint(2, 2) fallback and resizes the root
	// widget to 2x2. Every relationship below is scale-invariant, so a 2-unit canvas would satisfy
	// all of them while putting the eye 1.7 units off the plane and any realistic depth offset
	// behind it. This assertion is what makes the fixture's scale a claim rather than an accident.

	const FTransform& World = Root->GetWorldTransform();
	auto NDCOf = [&](const FVector& LocalPoint, FVector2D& OutNDC) -> bool
	{
		const FVector4 Clip = Canvas->GetViewProjectionMatrix()
			.TransformFVector4(FVector4(World.TransformPosition(LocalPoint), 1.0));
		if (Clip.W <= UE_KINDA_SMALL_NUMBER)return false;//at or behind the eye
		OutNDC = FVector2D(Clip.X / Clip.W, Clip.Y / Clip.W);
		return true;
	};

	// Two angles, because one cannot tell "the frame is derived from the field of view" apart from
	// "the frame happens to fit at 60 degrees".
	for (const float FOV : { 60.0f, 100.0f })
	{
		Canvas->SetFieldOfView(FOV);
		// Checked here rather than once up front, because the projection setters re-apply the
		// canvas's cached ViewportSize and can resize the root mid-test. Every relationship below
		// is scale-invariant and would pass just as well on the 2x2 fallback -- with the eye 1.7
		// units off the plane, where the depth offsets further down land behind it.
		if (!TestEqual(TEXT("The canvas keeps the fixture's authored width"), Root->GetWidth(), 1600.0f))return false;
		const double Expected = (Root->GetWidth() * 0.5) / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5));
		TestEqual(*FString::Printf(TEXT("At %.0f degrees the eye stands back by width/2 over tan(FOV/2)"), FOV),
			Canvas->GetViewLocation().X, -Expected, 0.01);

		const float Left = -Root->GetPivot().X * Root->GetWidth();
		const float Right = (1.0f - Root->GetPivot().X) * Root->GetWidth();
		const float Bottom = -Root->GetPivot().Y * Root->GetHeight();
		const float Top = (1.0f - Root->GetPivot().Y) * Root->GetHeight();
		for (const FVector& Corner : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
		{
			FVector2D NDC;
			if (!TestTrue(*FString::Printf(TEXT("At %.0f degrees the corner is in front of the eye"), FOV), NDCOf(Corner, NDC)))continue;
			TestEqual(*FString::Printf(TEXT("At %.0f degrees corner (%.0f,%.0f) sits on the horizontal frame edge"), FOV, Corner.Y, Corner.Z),
				FMath::Abs(NDC.X), 1.0, 0.001);
			TestEqual(*FString::Printf(TEXT("At %.0f degrees corner (%.0f,%.0f) sits on the vertical frame edge"), FOV, Corner.Y, Corner.Z),
				FMath::Abs(NDC.Y), 1.0, 0.001);
		}
	}

	Canvas->SetFieldOfView(60.0f);
	// A point in the plane lands where an orthographic view of the same rect would put it.
	const FVector InPlane(0.0, 300.0, -180.0);
	FVector2D Flat;
	if (TestTrue(TEXT("The in-plane point is visible"), NDCOf(InPlane, Flat)))
	{
		TestEqual(TEXT("An in-plane point lands on the orthographic fraction horizontally"),
			Flat.X, 2.0 * InPlane.Y / Root->GetWidth(), 0.0005);
		TestEqual(TEXT("An in-plane point lands on the orthographic fraction vertically"),
			Flat.Y, 2.0 * InPlane.Z / Root->GetHeight(), 0.0005);
	}

	// And the thing an author is actually looking for. Local +X is depth away from the eye, so
	// pushing the point back must shrink its offset and pulling it forward must grow it.
	FVector2D Pulled, Pushed;
	if (TestTrue(TEXT("The pulled-forward point is visible"), NDCOf(InPlane - FVector(200.0, 0.0, 0.0), Pulled))
		&& TestTrue(TEXT("The pushed-back point is visible"), NDCOf(InPlane + FVector(200.0, 0.0, 0.0), Pushed)))
	{
		TestTrue(TEXT("Nearer reads further from the centre"), FMath::Abs(Pulled.X) > FMath::Abs(Flat.X) + 0.01);
		TestTrue(TEXT("Further reads nearer to the centre"), FMath::Abs(Pushed.X) < FMath::Abs(Flat.X) - 0.01);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasShippedPlaneTest,
	"DreamGUI.Canvas.DepthBecomesVisibleWhenFoldedBackOntoTheCanvasPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasShippedPlaneTest::RunTest(const FString& Parameters)
{
	using namespace DreamCanvasProjectionTestLocal;
	// ProjectWorldPointOntoCanvasPlane exists so an ORTHOGRAPHIC editor viewport can show
	// foreshortening. Ortho has no perspective divide, so depth alone never changes anything on
	// screen; folding the point back onto the canvas plane converts the divide into a position,
	// which ortho can then draw. Two properties make it usable: it must be the identity on flat
	// content, so a design surface is not disturbed, and it must move things the right way.
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetDisplayName(TEXT("Root"));
	Root->SetWidth(1600.0f);
	Root->SetHeight(900.0f);
	Root->OnRegister();
	UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("Canvas created"), Canvas))return false;
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	Canvas->SetProjectionType(ECameraProjectionMode::Perspective);
	Canvas->SetFieldOfView(60.0f);
	if (!TestEqual(TEXT("The canvas keeps the fixture's authored width"), Root->GetWidth(), 1600.0f))return false;

	const FTransform& World = Root->GetWorldTransform();
	const FVector InPlane = World.TransformPosition(FVector(0.0, 300.0, -180.0));

	// Identity on the plane. This is what lets the outline stay silent for flat content, and so
	// what keeps it from becoming a permanent second outline that authors learn to ignore.
	FVector Folded;
	if (TestTrue(TEXT("An in-plane point projects"), Canvas->ProjectWorldPointOntoCanvasPlane(InPlane, Folded)))
	{
		TestTrue(TEXT("An in-plane point folds back onto itself"), Folded.Equals(InPlane, 0.01));
	}

	// Depth turns into lateral position, in the direction an author expects: local +X is away from
	// the eye, so pushing back pulls the point toward the vanishing centre and pulling forward
	// throws it outward. Under ortho this is the entire visible signal.
	const FVector Away = InPlane + World.TransformVector(FVector(400.0, 0.0, 0.0));
	const FVector Toward = InPlane - World.TransformVector(FVector(400.0, 0.0, 0.0));
	FVector FoldedAway, FoldedToward;
	if (TestTrue(TEXT("A pushed-back point projects"), Canvas->ProjectWorldPointOntoCanvasPlane(Away, FoldedAway))
		&& TestTrue(TEXT("A pulled-forward point projects"), Canvas->ProjectWorldPointOntoCanvasPlane(Toward, FoldedToward)))
	{
		const FVector Centre = World.GetLocation();
		TestTrue(TEXT("Everything lands on the canvas plane"),
			FMath::IsNearlyEqual(World.InverseTransformPosition(FoldedAway).X, 0.0, 0.01)
			&& FMath::IsNearlyEqual(World.InverseTransformPosition(FoldedToward).X, 0.0, 0.01));
		TestTrue(TEXT("Pushing back moves it toward the centre"),
			FVector::Dist(FoldedAway, Centre) < FVector::Dist(InPlane, Centre) - 1.0);
		TestTrue(TEXT("Pulling forward throws it outward"),
			FVector::Dist(FoldedToward, Centre) > FVector::Dist(InPlane, Centre) + 1.0);
	}

	// A point at or behind the eye has no image at all, and must be refused rather than mirrored:
	// FSceneView::ScreenToPixel flips a negative W instead of rejecting it, so an unguarded caller
	// would draw a plausible-looking outline folded inside out.
	FVector Unused;
	const FVector BehindTheEye = World.TransformPosition(FVector(-2000.0, 0.0, 0.0));
	TestFalse(TEXT("A point behind the eye is refused, not mirrored"),
		Canvas->ProjectWorldPointOntoCanvasPlane(BehindTheEye, Unused));
	return true;
}

#endif
