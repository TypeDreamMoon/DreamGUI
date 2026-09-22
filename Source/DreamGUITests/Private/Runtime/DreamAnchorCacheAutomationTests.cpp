// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweener.h"
#include "Engine/World.h"
#include "DreamScopedWorld.h"

/*
 * The resolved-size cache, asked about after the anchor setters rather than about them.
 *
 * Every existing test around anchors asserts AnchorData -- the serialized half -- and AnchorData was
 * always right. The defect lived one layer over: SetAnchorMin, SetAnchorMax and SetAnchorOffset each
 * wrote a size DELTA into CacheWidth/CacheHeight (the RESOLVED size) and then declined to dirty the
 * flag that would have made anyone re-resolve it. On a stretched axis the two differ by the parent's
 * span across the anchors, so GetWidth() answered with the delta from that moment until something
 * unrelated happened to dirty the cache. Nothing in the suite asked GetWidth() after an anchor change,
 * which is exactly why 2026-09-01's fixes to SetWidth/SetHeight/SetAnchoredPositionAndSizeDelta could
 * be made without these three being noticed.
 *
 * So every test here reads the RESOLVED size back, and does it while an anchor is stretched.
 */

namespace DreamAnchorCacheTestLocal
{
	using DreamTests::FScopedGameWorld;

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
	FDreamAnchorMaxStretchResolvesAgainstTheParentTest,
	"DreamGUI.Anchors.StretchingWithSetAnchorMaxWidensTheWidgetInsteadOfPublishingItsSizeDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorMaxStretchResolvesAgainstTheParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);

	// Point-anchored to start with, so the delta and the resolved width are the same number and nothing
	// can be proven yet.
	if (!TestTrue(TEXT("The child starts point-anchored at 100 wide"),
		FMath::IsNearlyEqual(Child->GetWidth(), 100.0f, 0.01f)
		&& FMath::IsNearlyEqual(static_cast<float>(Child->GetSizeDelta().X), 100.0f, 0.01f)))
	{
		return false;
	}

	// Stretch the horizontal axis. Anchors default to (0.5, 0.5) on both ends, so moving AnchorMax.X to
	// 1 opens a span of half the parent. AnchoredPosition, SizeDelta and Pivot are untouched by an anchor
	// move, so the offsets stay put and the RESOLVED width becomes delta + that span.
	Child->SetAnchorMax(FVector2D(1.0, 0.5));

	TestTrue(TEXT("The size delta is what it always was"),
		FMath::IsNearlyEqual(static_cast<float>(Child->GetSizeDelta().X), 100.0f, 0.01f));
	// 100 + 400 * (1 - 0.5). Before the fix this answered 100: the delta, published as the resolved width.
	TestTrue(TEXT("...and the resolved width now includes the parent's span across the anchors"),
		FMath::IsNearlyEqual(Child->GetWidth(), 300.0f, 0.01f));

	// The vertical axis was not stretched, so it must not have moved.
	TestTrue(TEXT("The untouched axis is unchanged"),
		FMath::IsNearlyEqual(Child->GetHeight(), 50.0f, 0.01f));

	// And the cache has to keep tracking the parent rather than freezing at one answer.
	Root->SetWidth(600.0f);
	TestTrue(TEXT("Resizing the parent moves the stretched child with it"),
		FMath::IsNearlyEqual(Child->GetWidth(), 400.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorMinStretchResolvesAgainstTheParentTest,
	"DreamGUI.Anchors.StretchingWithSetAnchorMinWidensTheWidgetInsteadOfPublishingItsSizeDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorMinStretchResolvesAgainstTheParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);

	// The mirror image, on the other axis and the other end: AnchorMax.Y stays at its default 0.5, so
	// dropping AnchorMin.Y to 0 opens a span of half the parent's height.
	Child->SetAnchorMin(FVector2D(0.5, 0.0));

	TestTrue(TEXT("The size delta is what it always was"),
		FMath::IsNearlyEqual(static_cast<float>(Child->GetSizeDelta().Y), 50.0f, 0.01f));
	// 50 + 300 * (0.5 - 0).
	TestTrue(TEXT("...and the resolved height includes the parent's span across the anchors"),
		FMath::IsNearlyEqual(Child->GetHeight(), 200.0f, 0.01f));
	TestTrue(TEXT("The untouched axis is unchanged"),
		FMath::IsNearlyEqual(Child->GetWidth(), 100.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorMinMaxWithoutKeepSizeLeavesACleanCacheTest,
	"DreamGUI.Anchors.SettingBothAnchorsWithoutKeepingSizeStillAnswersWithAResolvedWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorMinMaxWithoutKeepSizeLeavesACleanCacheTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);

	// This is the deterministic path, not a timing-dependent one: the function asks GetWidth() first,
	// which RESOLVES and clears the dirty flag, then calls the two anchor setters -- and with
	// bKeepSize false there is no SetWidth afterwards to repair whatever they left behind. Before the
	// fix the cache was therefore guaranteed wrong here, every time.
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);

	// Full stretch over a 400x300 parent, delta still 100x50.
	TestTrue(TEXT("The horizontal answer covers the parent plus the delta"),
		FMath::IsNearlyEqual(Child->GetWidth(), 500.0f, 0.01f));
	TestTrue(TEXT("The vertical answer covers the parent plus the delta"),
		FMath::IsNearlyEqual(Child->GetHeight(), 350.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorOffsetOnAStretchedNodeIsAnInsetNotANegativeSizeTest,
	"DreamGUI.Anchors.SetAnchorOffsetOnAStretchedNodeInsetsItInsteadOfMakingItNegativelyWide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorOffsetOnAStretchedNodeIsAnInsetNotANegativeSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);

	// The exact pair DreamTextInput runs on its clip and placeholder nodes, and the registry on the
	// children it builds: stretch to the parent, then inset by the style's padding.
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
	Child->SetAnchorOffset(FMargin(10.0f, 4.0f, 10.0f, 4.0f));

	// 400 - 10 - 10 and 300 - 4 - 4. Before the fix this answered -(10 + 10) and -(4 + 4): the negated
	// padding sums, written into the resolved-size cache with no dirty flag to make anyone re-ask.
	TestTrue(TEXT("The inset width is the parent's span less the horizontal padding"),
		FMath::IsNearlyEqual(Child->GetWidth(), 380.0f, 0.01f));
	TestTrue(TEXT("The inset height is the parent's span less the vertical padding"),
		FMath::IsNearlyEqual(Child->GetHeight(), 292.0f, 0.01f));

	// The offsets have to read back exactly as set, on all four edges -- the old early-exit tail cleared
	// only the LEFT dirty flag, leaving the other three owed a resolve against data Left had moved on from.
	TestTrue(TEXT("Left reads back"), FMath::IsNearlyEqual(Child->GetAnchorOffsetLeft(), 10.0f, 0.01f));
	TestTrue(TEXT("Right reads back"), FMath::IsNearlyEqual(Child->GetAnchorOffsetRight(), 10.0f, 0.01f));
	TestTrue(TEXT("Top reads back"), FMath::IsNearlyEqual(Child->GetAnchorOffsetTop(), 4.0f, 0.01f));
	TestTrue(TEXT("Bottom reads back"), FMath::IsNearlyEqual(Child->GetAnchorOffsetBottom(), 4.0f, 0.01f));

	// Still a stretch: the inset must follow the parent rather than having been baked into a fixed size.
	Root->SetWidth(600.0f);
	TestTrue(TEXT("The inset tracks the parent"),
		FMath::IsNearlyEqual(Child->GetWidth(), 580.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnchorOffsetOnAPointAnchoredNodeSetsItsSizeTest,
	"DreamGUI.Anchors.SetAnchorOffsetOnAPointAnchoredNodeStillMeansTheSizeItAlwaysMeant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAnchorOffsetOnAPointAnchoredNodeSetsItsSizeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);

	// On a point-anchored axis the parent's span is zero, so the offsets ARE the rect and the answer is
	// the same one the old code produced. This pins that half of the contract so the rewrite cannot have
	// silently redefined it.
	Child->SetAnchorOffset(FMargin(-30.0f, -20.0f, -30.0f, -20.0f));

	TestTrue(TEXT("Width is right minus left, negated"), FMath::IsNearlyEqual(Child->GetWidth(), 60.0f, 0.01f));
	TestTrue(TEXT("Height is top minus bottom, negated"), FMath::IsNearlyEqual(Child->GetHeight(), 40.0f, 0.01f));
	TestTrue(TEXT("The delta agrees with the resolved size on a point anchor"),
		FMath::IsNearlyEqual(static_cast<float>(Child->GetSizeDelta().X), 60.0f, 0.01f)
		&& FMath::IsNearlyEqual(static_cast<float>(Child->GetSizeDelta().Y), 40.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamResolvedSizeIsNeverNegativeTest,
	"DreamGUI.Anchors.AWidgetWhoseOffsetsCrossReportsAnEmptyRectRatherThanAnInvertedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamResolvedSizeIsNeverNegativeTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 40.0f, 30.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 10.0f, 10.0f);

	// A stretched child inset by more than the parent is wide enough to hold: the arithmetic wants
	// 40 - 100 - 100. That is an empty rect, not a rect whose left edge is to the right of its right
	// edge -- and GetLocalSpaceLeft/Right multiply the width straight through, so a negative answer
	// swapped the edges and inverted the hit area while GetWorldRectBoundingSphere (W*W + H*H) went on
	// reporting a perfectly healthy radius over it.
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
	Child->SetAnchorOffset(FMargin(100.0f, 100.0f, 100.0f, 100.0f));

	TestTrue(TEXT("The width floors at zero"), Child->GetWidth() >= 0.0f);
	TestTrue(TEXT("The height floors at zero"), Child->GetHeight() >= 0.0f);
	TestTrue(TEXT("The left edge is not to the right of the right edge"),
		Child->GetLocalSpaceLeft() <= Child->GetLocalSpaceRight());
	TestTrue(TEXT("The bottom edge is not above the top edge"),
		Child->GetLocalSpaceBottom() <= Child->GetLocalSpaceTop());

	// An authored negative is floored at the setter as well, so the stored intent and the answer agree.
	UDreamWidget* Other = MakeWidget(TestWorld.World, Root, TEXT("Other"), 20.0f, 20.0f);
	Other->SetWidth(-5.0f);
	Other->SetHeight(-5.0f);
	TestTrue(TEXT("SetWidth floors its argument"), FMath::IsNearlyEqual(Other->GetWidth(), 0.0f, 0.01f));
	TestTrue(TEXT("SetHeight floors its argument"), FMath::IsNearlyEqual(Other->GetHeight(), 0.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutAnimationStartsAtTheSizeItSnapshottedTest,
	"DreamGUI.Layout.ACommonTweenStartsAStretchedChildAtTheSizeTheSnapshotRecordedNotOneParentWider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutAnimationStartsAtTheSizeItSnapshottedTest::RunTest(const FString& Parameters)
{
	using namespace DreamAnchorCacheTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Root = MakeWidget(TestWorld.World, nullptr, TEXT("Root"), 400.0f, 300.0f);
	UDreamWidget* Child = MakeWidget(TestWorld.World, Root, TEXT("Child"), 100.0f, 50.0f);
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);

	// The two halves of the layout animation disagreed about units. SnapshotLayout records
	// Child->GetSize(), a RESOLVED size; OnApplyLayoutResults fed it to
	// SetPositionAndSizeForLayoutAnimation, which assigns straight into AnchorData.SizeDelta. On a
	// point-anchored child the two are the same number and nothing shows. On a stretched one they
	// differ by the parent's span, so the animation's first frame jumped the child to
	// snapshot + parent span and then eased back down from there.
	UDreamLayoutAnimation_CommonTween* Animation = NewObject<UDreamLayoutAnimation_CommonTween>(Root);
	if (!TestNotNull(TEXT("Animation handler created"), Animation))
	{
		return false;
	}

	TArray<FLayoutAnimationSnapshotData> Snapshots;
	FLayoutAnimationSnapshotData Snapshot;
	Snapshot.Widget = Child;
	Snapshot.Position = Child->GetAnchoredPosition();
	Snapshot.Size = FVector2D(180.0, 40.0);//what the child RESOLVED to before the layout moved it
	Snapshots.Add(Snapshot);

	TArray<TWeakObjectPtr<UDreamTweener>> Tweeners;
	// The start state is applied synchronously, before any tweener exists -- which is the frame the
	// jump was visible on. (No game instance here, so the tween manager is absent and no easing is
	// scheduled; the applied start state is the whole observable and is exactly what is under test.)
	Animation->OnApplyLayoutResults(Snapshots, Tweeners);

	TestTrue(TEXT("The animation starts at the width the snapshot recorded"),
		FMath::IsNearlyEqual(Child->GetWidth(), 180.0f, 0.01f));
	TestTrue(TEXT("...and at the height it recorded"),
		FMath::IsNearlyEqual(Child->GetHeight(), 40.0f, 0.01f));

	Root->DestroyWidget();
	return true;
}

#endif
