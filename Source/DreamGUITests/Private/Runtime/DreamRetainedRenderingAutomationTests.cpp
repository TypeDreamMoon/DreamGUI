// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIMeshIndex.h"
#include "Extensions/DreamRetainerBox.h"

/*
 * Retained rendering, cached draw-calls, and the two arithmetic decisions underneath them.
 *
 * The policies are static functions taking their inputs, so they can be checked frame by frame with
 * no world, no canvas, no RHI and no waiting -- which is the only way to assert a "once every N
 * frames" rule at all. What the behaviours do with the answer (configure a canvas, ask it for a
 * render-target update, suspend its rebuild) is wiring on top, and the canvas half of that wiring is
 * asserted separately below.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRetainerPhaseTest,
	"DreamGUI.Retainer.ARetainerRedrawsOnItsOwnFrameOfThePhaseCycleAndOnRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRetainerPhaseTest::RunTest(const FString& Parameters)
{
	auto ShouldRender = [](bool bRetain, int32 Phase, int32 PhaseCount, bool bRequested, uint64 Frame)
	{
		return UDreamRetainerBox::ShouldRenderThisFrame(bRetain, Phase, PhaseCount, bRequested, Frame);
	};

	// Not retaining is the escape hatch: the subtree draws live, so nothing is ever skipped.
	for (uint64 Frame = 0; Frame < 4; Frame++)
	{
		TestTrue(TEXT("With retaining off every frame draws"), ShouldRender(false, 0, 4, false, Frame));
	}

	// A cycle of one is "every frame", which is what a retainer left at its defaults does.
	for (uint64 Frame = 0; Frame < 4; Frame++)
	{
		TestTrue(TEXT("A phase count of one draws every frame"), ShouldRender(true, 0, 1, false, Frame));
	}

	// The point of phases: two retainers on different phases of the same cycle never redraw together.
	for (uint64 Frame = 0; Frame < 8; Frame++)
	{
		const bool bFirst = ShouldRender(true, 0, 4, false, Frame);
		const bool bSecond = ShouldRender(true, 1, 4, false, Frame);
		const bool bThird = ShouldRender(true, 2, 4, false, Frame);
		const bool bFourth = ShouldRender(true, 3, 4, false, Frame);
		const int32 HowManyDrewThisFrame = (bFirst ? 1 : 0) + (bSecond ? 1 : 0) + (bThird ? 1 : 0) + (bFourth ? 1 : 0);
		TestEqual(TEXT("Exactly one of four phases draws on any given frame"), HowManyDrewThisFrame, 1);
	}
	TestTrue(TEXT("Phase 0 draws on frame 0"), ShouldRender(true, 0, 4, false, 0));
	TestFalse(TEXT("...and not on frame 1"), ShouldRender(true, 0, 4, false, 1));
	TestTrue(TEXT("...and again one cycle later"), ShouldRender(true, 0, 4, false, 4));

	// A request outranks the phase, which is the whole contract of RequestRender.
	TestTrue(TEXT("A requested render happens on a frame the phase would have skipped"),
		ShouldRender(true, 0, 4, true, 1));

	// Nonsense input must not stop a retainer drawing at all -- a frozen UI is a worse failure than
	// an ignored setting.
	TestTrue(TEXT("A phase count of zero falls back to drawing every frame"), ShouldRender(true, 0, 0, false, 7));
	TestTrue(TEXT("A negative phase count does too"), ShouldRender(true, 0, -3, false, 7));
	// A phase past the end wraps rather than clamping, so out-of-range phases stay spread out instead
	// of all piling onto the last frame of the cycle.
	TestEqual(TEXT("Phase 5 of 4 behaves as phase 1"),
		ShouldRender(true, 5, 4, false, 1), ShouldRender(true, 1, 4, false, 1));
	TestFalse(TEXT("...and not as phase 3"), ShouldRender(true, 5, 4, false, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInvalidationCachePolicyTest,
	"DreamGUI.Retainer.ACachedSubtreeOnlyRebuildsItsDrawCallsAfterItIsInvalidated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInvalidationCachePolicyTest::RunTest(const FString& Parameters)
{
	// Caching off is a pass-through: the canvas rebuilds whenever it is dirty, exactly as it did
	// before invalidation boxes existed.
	TestTrue(TEXT("With caching off a rebuild is always allowed"), UDreamInvalidationBox::ShouldRebuildDrawCall(false, false));
	TestTrue(TEXT("...invalidated or not"), UDreamInvalidationBox::ShouldRebuildDrawCall(false, true));

	// Caching on holds the draw-call list until something says it is stale.
	TestFalse(TEXT("A cached, un-invalidated subtree does not rebuild"), UDreamInvalidationBox::ShouldRebuildDrawCall(true, false));
	TestTrue(TEXT("An invalidated one does"), UDreamInvalidationBox::ShouldRebuildDrawCall(true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasSuspendedRebuildKeepsTheRequestTest,
	"DreamGUI.Retainer.SuspendingRebuildsDefersThemRatherThanLosingThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasSuspendedRebuildKeepsTheRequestTest::RunTest(const FString& Parameters)
{
	/*
	 * The half of caching that can go quietly wrong: a subtree that goes dirty while cached must come
	 * back rebuilt, not stale. The suspend must therefore gate the rebuild without clearing the
	 * request, and releasing must wake the update pass -- a plain refresh may well have consumed
	 * bCanTickUpdate in the meantime, leaving a canvas that is dirty and never looked at again.
	 */
	UDreamCanvas* Canvas = NewObject<UDreamCanvas>();
	if (!TestNotNull(TEXT("Canvas created"), Canvas))
	{
		return false;
	}

	Canvas->bShouldRebuildDrawCall = false;
	Canvas->bCanTickUpdate = false;
	Canvas->SetDrawCallRebuildSuspended(true);
	TestTrue(TEXT("The canvas reports itself suspended"), Canvas->GetDrawCallRebuildSuspended());

	Canvas->MarkCanvasUpdate(true);
	TestTrue(TEXT("A rebuild asked for while suspended is still outstanding"), Canvas->bShouldRebuildDrawCall);

	//the refresh pass consuming its wake-up is the case that made this worth asserting
	Canvas->bCanTickUpdate = false;
	Canvas->SetDrawCallRebuildSuspended(false);
	TestFalse(TEXT("The canvas is no longer suspended"), Canvas->GetDrawCallRebuildSuspended());
	TestTrue(TEXT("The deferred rebuild is still asked for"), Canvas->bShouldRebuildDrawCall);
	TestTrue(TEXT("...and the update pass has been woken to notice it"), Canvas->bCanTickUpdate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCanvasRenderScaleTest,
	"DreamGUI.Canvas.RenderScaleShrinksTheScreenSpacePassWithoutEverRoundingToNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamCanvasRenderScaleTest::RunTest(const FString& Parameters)
{
	float Applied = 0.0f;

	// Full scale has to be exactly the viewport. Anything else would put every project that never
	// touches this setting through a rescale for a rounding error's sake.
	FIntPoint Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(1920, 1080), 1.0f, Applied);
	TestEqual(TEXT("Scale 1 gives back the viewport width"), Size.X, 1920);
	TestEqual(TEXT("Scale 1 gives back the viewport height"), Size.Y, 1080);
	TestEqual(TEXT("...and reports itself as full scale"), Applied, 1.0f);

	Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(1920, 1080), 0.5f, Applied);
	TestEqual(TEXT("Half scale halves the width"), Size.X, 960);
	TestEqual(TEXT("Half scale halves the height"), Size.Y, 540);
	TestEqual(TEXT("...and reports half"), Applied, 0.5f);

	// Clamped, not honoured: a scale above 1 would mean supersampling, which this path does not do,
	// and a scale near zero would mean a target with no pixels in it.
	Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(800, 600), 4.0f, Applied);
	TestEqual(TEXT("A scale above one is clamped back to the viewport"), Size.X, 800);
	Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(800, 600), 0.0f, Applied);
	TestTrue(TEXT("A scale of zero still produces a target with pixels in it"), Size.X > 0 && Size.Y > 0);

	// A tiny viewport must not round away to a zero-sized target.
	Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(3, 1), 0.1f, Applied);
	TestEqual(TEXT("A one-pixel-tall viewport keeps its pixel"), Size.Y, 1);
	TestTrue(TEXT("...and its width"), Size.X >= 1);
	// And a viewport of nothing at all is answered, not divided by.
	Size = UDreamCanvas::CalculateRenderScaledSize(FIntPoint(0, 0), 0.5f, Applied);
	TestTrue(TEXT("An empty viewport produces a one-pixel target rather than a division by zero"),
		Size.X >= 1 && Size.Y >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMeshIndexBudgetTest,
	"DreamGUI.Canvas.TheDrawCallVertexBudgetFitsTheIndexWidthItIsCompiledWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMeshIndexBudgetTest::RunTest(const FString& Parameters)
{
	/*
	 * The 16-bit and 32-bit index configurations are both supported, and only one of them is ever
	 * compiled. The static_asserts in DreamUIMeshIndex.h are the real guard -- the 32-bit build simply
	 * will not compile if the budget outgrows the index type or the batcher's sum overflows -- and
	 * this restates them at runtime so the reason is in the test log of whichever build is running
	 * rather than only in a compiler error nobody has seen since the define was last switched.
	 */
	TestTrue(TEXT("Every vertex the budget allows can be named by one index"),
		(int64)LEXUI_MAX_VERTEX_COUNT <= (int64)TNumericLimits<FDreamUIMeshIndex>::Max());
	TestTrue(TEXT("Two draw-calls' vertex counts can be added without overflowing int32"),
		(int64)LEXUI_MAX_VERTEX_COUNT * 2 <= (int64)MAX_int32);
	TestTrue(TEXT("The budget is large enough to be worth having"), LEXUI_MAX_VERTEX_COUNT >= 65535);
	TestEqual(TEXT("The index buffer stores the index type the rest of the plugin memcpys"),
		(int32)sizeof(decltype(FDreamUIMeshIndexBuffer::Indices)::ElementType), (int32)sizeof(FDreamUIMeshIndex));
	return true;
}

#endif
