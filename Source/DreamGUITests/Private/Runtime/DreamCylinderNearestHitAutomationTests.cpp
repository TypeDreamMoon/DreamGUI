// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamCylinderNearestHitTest,
	"DreamGUI.Extensions.RenderTargetSurface.CylinderTraceChoosesTheNearestAllowedIntersection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamCylinderNearestHitTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Two-sided trace from the left"));
	OutTestCommands.Add(TEXT("LeftBoth"));
	OutBeautifiedNames.Add(TEXT("Two-sided trace from the right"));
	OutTestCommands.Add(TEXT("RightBoth"));
	OutBeautifiedNames.Add(TEXT("Front-face-only trace from the left"));
	OutTestCommands.Add(TEXT("LeftFront"));
	OutBeautifiedNames.Add(TEXT("Front-face-only trace from the right"));
	OutTestCommands.Add(TEXT("RightFront"));
}

bool FDreamCylinderNearestHitTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(640, 480));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the runtime rig is available"), Rig.IsUsable()))return false;
	const auto Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("CurvedScreen"),
		FTransform(FRotator(13.0, 27.0, 8.0), FVector(300.0, 40.0, 25.0)), FIntPoint(512, 256));
	if (!TestTrue(TEXT("the canvas and its displayed surface exist"), Screen.IsComplete()))return false;
	UDreamUIRenderTargetGeometrySource* Surface = Screen.Surface;
	Surface->SetGeometryMode(EDreamUIRenderTargetGeometryMode::Cylinder);
	Surface->SetCylinderArcAngle(90.0f);
	const bool bFromRight = Parameters.StartsWith(TEXT("Right"));
	const bool bBothSides = Parameters.EndsWith(TEXT("Both"));
	Surface->SetEnableInteractOnBackside(bBothSides);

	// X = 10 cuts the actual faceted surface twice, away from any segment boundary. Both hits
	// have the same height. Their U values are near 0.029 and 0.971; reversing the ray swaps them.
	const FTransform Transform = Surface->GetComponentTransform();
	const FVector Left = Transform.TransformPosition(FVector(10.0, -400.0, 0.0));
	const FVector Right = Transform.TransformPosition(FVector(10.0, 400.0, 0.0));
	const FVector Start = bFromRight ? Right : Left;
	const FVector End = bFromRight ? Left : Right;
	const TArray<FDynamicMeshVertex>& Vertices = Surface->GetMeshVertices();
	if (!TestTrue(TEXT("the cylinder has mesh segments"), Vertices.Num() >= 4))return false;

	// Interpolate the two boundary segments' intersections in surface space. This oracle uses
	// the public mesh being displayed, independent of the trace's per-segment coordinate frames.
	const FVector3f First = Vertices[0].Position;
	const FVector3f Next = Vertices[2].Position;
	const double Ratio = (10.0 - First.X) / (Next.X - First.X);
	const double LeftU = FMath::Lerp(static_cast<double>(Vertices[0].TextureCoordinate[0].X),
		static_cast<double>(Vertices[2].TextureCoordinate[0].X), Ratio);
	TestTrue(TEXT("the near-side oracle lies inside its first segment"), Ratio > 0.0 && Ratio < 1.0);
	// With backside interaction disabled, the first intersection is culled, so the far one wins.
	const bool bExpectRight = bBothSides ? bFromRight : !bFromRight;
	const double ExpectedU = bExpectRight ? 1.0 - LeftU : LeftU;
	FVector2D HitUV = FVector2D::ZeroVector;
	if (TestTrue(TEXT("the public interaction source hits the cylinder"),
		IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(
			Surface, INDEX_NONE, FVector::ZeroVector, Start, End, HitUV)))
	{
		TestEqual(TEXT("the chosen UV belongs to the nearest allowed intersection"), HitUV.X, ExpectedU, 0.0001);
		TestEqual(TEXT("the hit remains halfway up the surface"), HitUV.Y, 0.5, 0.0001);
	}
	return true;
}

#endif
