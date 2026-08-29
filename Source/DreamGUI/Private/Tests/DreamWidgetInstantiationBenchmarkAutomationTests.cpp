// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "HAL/PlatformTime.h"

/*
 * What it costs to turn a stored widget hierarchy into a live one.
 *
 * The class model replaces the mechanism: today a hierarchy is rebuilt by reading a binary blob
 * property by property (WidgetSerializer), and under a generated class it will be instanced from a
 * template with FObjectInstancingGraph. Those are different enough that "it should be fine" is not
 * a claim anyone can make from reading the code, and the fork this came from went through a whole
 * rewrite of the foundation over exactly this kind of cost (upstream 54ad42cbb -- 5000 buttons that
 * would not hold 60fps because Actor/SceneComponent was too heavy a base).
 *
 * So this exists to produce comparable numbers BEFORE the mechanism changes. It reports rather than
 * asserts on wall-clock, because an absolute threshold is a machine-dependent flake generator and
 * would say nothing useful on someone else's hardware. What it does assert is the shape of the
 * curve: doubling the widget count must not square the cost. An accidental O(n^2) -- a full-tree
 * walk per attach, a per-widget linear lookup -- is the failure that actually happens here, and it
 * shows up in the ratio regardless of how fast the machine is.
 *
 * Read the reported lines, not the pass/fail, when comparing across a change.
 */

namespace DreamWidgetInstantiationBenchmarkLocal
{
	/** Small enough to stay a test rather than a build step; large enough for the ratio to mean something. */
	static constexpr int32 SmallBranches = 8;
	static constexpr int32 SmallLeavesPerBranch = 15;   // 1 + 8 + 120 = 129 widgets
	static constexpr int32 LargeBranches = 16;
	static constexpr int32 LargeLeavesPerBranch = 30;   // 1 + 16 + 480 = 497 widgets, ~3.85x the small one

	/** Best-of rather than mean: the fastest run is the one least polluted by whatever else the machine is doing. */
	static constexpr int32 RunsPerMeasurement = 3;

	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** A shallow, wide tree -- the shape real UI takes, and the shape that makes per-attach walks expensive. */
	UDreamWidgetTree* BuildTree(UObject* InOuter, int32 InBranches, int32 InLeavesPerBranch, int32& OutWidgetCount)
	{
		UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(InOuter);
		UDreamWidget* Root = Tree->ConstructWidget<UDreamWidget>();
		Root->SetDisplayName(TEXT("Root"));
		Tree->RootWidget = Root;
		OutWidgetCount = 1;

		for (int32 BranchIndex = 0; BranchIndex < InBranches; BranchIndex++)
		{
			UDreamWidget* Branch = Tree->ConstructWidget<UDreamWidget>();
			Branch->SetDisplayName(FString::Printf(TEXT("Branch%d"), BranchIndex));
			Branch->TrySetParent(Root, false);
			OutWidgetCount++;

			for (int32 LeafIndex = 0; LeafIndex < InLeavesPerBranch; LeafIndex++)
			{
				UDreamWidget* Leaf = Tree->ConstructWidget<UDreamWidget>();
				Leaf->SetDisplayName(FString::Printf(TEXT("Leaf%d_%d"), BranchIndex, LeafIndex));
				Leaf->TrySetParent(Branch, false);
				OutWidgetCount++;
			}
		}
		return Tree;
	}

	double BestOfMilliseconds(TFunctionRef<void()> InBody, int32 InRuns)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 Run = 0; Run < InRuns; Run++)
		{
			const double Start = FPlatformTime::Seconds();
			InBody();
			Best = FMath::Min(Best, (FPlatformTime::Seconds() - Start) * 1000.0);
		}
		return Best;
	}

	/** Instancing time for one tree, the mechanism a generated class will use. */
	double MeasureInstancing(UWorld* InWorld, UDreamWidgetTree* InTemplate, int32& OutInstancedCount)
	{
		int32 Count = 0;
		const double Milliseconds = BestOfMilliseconds([&]()
		{
			FObjectInstancingGraph InstancingGraph;
			UDreamWidgetTree* Instance = NewObject<UDreamWidgetTree>(
				InWorld, InTemplate->GetClass(), NAME_None, RF_Transactional,
				InTemplate, /*bCopyTransientsFromClassDefaults*/false, &InstancingGraph);
			Instance->RebuildParentLinks();
			Count = Instance->CountWidgets();
		}, RunsPerMeasurement);
		OutInstancedCount = Count;
		return Milliseconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetInstantiationBenchmarkTest,
	"DreamGUI.WidgetTree.Benchmark.InstantiationCostScalesWithWidgetCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetInstantiationBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetInstantiationBenchmarkLocal;
	FScopedGameWorld TestWorld;
	if (!TestNotNull(TEXT("the benchmark needs a world"), TestWorld.World))
	{
		return false;
	}

	int32 SmallCount = 0;
	int32 LargeCount = 0;
	TStrongObjectPtr<UDreamWidgetTree> SmallTemplate(BuildTree(GetTransientPackage(), SmallBranches, SmallLeavesPerBranch, SmallCount));
	TStrongObjectPtr<UDreamWidgetTree> LargeTemplate(BuildTree(GetTransientPackage(), LargeBranches, LargeLeavesPerBranch, LargeCount));

	int32 SmallInstanced = 0;
	int32 LargeInstanced = 0;
	const double SmallMs = MeasureInstancing(TestWorld.World, SmallTemplate.Get(), SmallInstanced);
	const double LargeMs = MeasureInstancing(TestWorld.World, LargeTemplate.Get(), LargeInstanced);

	AddInfo(FString::Printf(TEXT("instancing %d widgets: %.3f ms (%.4f ms/widget)"), SmallCount, SmallMs, SmallMs / SmallCount));
	AddInfo(FString::Printf(TEXT("instancing %d widgets: %.3f ms (%.4f ms/widget)"), LargeCount, LargeMs, LargeMs / LargeCount));

	// Instancing has to actually produce the whole tree, or the timing above is measuring nothing.
	TestEqual(TEXT("instancing the small template yields its whole tree"), SmallInstanced, SmallCount);
	TestEqual(TEXT("instancing the large template yields its whole tree"), LargeInstanced, LargeCount);

	// The scaling guard. Linear would put the ratio at the size ratio (~3.85x); quadratic would put it
	// near its square (~14.8x). The bound sits well above the first and clearly below the second, so it
	// stays quiet on a noisy machine and still catches a per-widget full-tree walk.
	const double SizeRatio = (double)LargeCount / (double)SmallCount;
	const double TimeRatio = SmallMs > 0.0 ? LargeMs / SmallMs : 0.0;
	AddInfo(FString::Printf(TEXT("%.2fx the widgets cost %.2fx the time (quadratic would be ~%.1fx)"),
		SizeRatio, TimeRatio, SizeRatio * SizeRatio));
	TestTrue(
		FString::Printf(TEXT("instancing scales sub-quadratically: %.2fx time for %.2fx widgets"), TimeRatio, SizeRatio),
		TimeRatio < SizeRatio * SizeRatio * 0.5);

	return true;
}

#endif
