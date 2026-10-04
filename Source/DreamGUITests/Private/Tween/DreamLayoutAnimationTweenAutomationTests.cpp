// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Tweener/DreamTweenerFloat.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#include "Driver/DreamDriverRig.h"

/*
 * The tweens a layout animation starts, as tweens: whom they hold, when they end, and which clock they run on.
 * The layout tests check where the children land; these check that the tweens carrying them there can neither
 * outlive the child they write to nor the container that started them, and that they run on the screen's clock
 * the way every other widget tween does.
 */
namespace DreamLayoutAnimationTweenTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * The tweens an animation handler has running: their outer is the handler. A layout pass that runs again
	 * kills the run before it, so only the live ones are the current animation.
	 */
	TArray<UDreamTweener*> TweensRunningFor(UObject* InHandler)
	{
		TArray<UObject*> Inner;
		GetObjectsWithOuter(InHandler, Inner, EGetObjectsFlags::None);
		TArray<UDreamTweener*> Tweens;
		for (UObject* Object : Inner)
		{
			UDreamTweener* Tween = Cast<UDreamTweener>(Object);
			if (IsValid(Tween) && !Tween->IsMarkedToKill())
			{
				Tweens.Add(Tween);
			}
		}
		return Tweens;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutAnimationChildCollectedTest,
	"DreamGUI.Tween.LayoutAnimation.AChildDestroyedMidAnimationIsNeverWrittenToOnceItIsCollected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutAnimationChildCollectedTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutAnimationTweenTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has a tween manager"), Manager))
	{
		return false;
	}
	UDreamWidget* Parent = Rig.MakeWidget(TEXT("Parent"), nullptr, FVector2D(400.0, 300.0));
	UDreamWidget* Child = Rig.MakeWidget(TEXT("Child"), Parent, FVector2D(100.0, 50.0));
	if (!TestNotNull(TEXT("A parent"), Parent) || !TestNotNull(TEXT("and a child"), Child))
	{
		return false;
	}

	// The scenario the anchor-cache test plays, without its manual kill before the child goes.
	UDreamLayoutAnimation_CommonTween* Animation = NewObject<UDreamLayoutAnimation_CommonTween>(Parent);
	TArray<FLayoutAnimationSnapshotData> Snapshots;
	FLayoutAnimationSnapshotData Snapshot;
	Snapshot.Widget = Child;
	Snapshot.Position = Child->GetAnchoredPosition();
	Snapshot.Size = FVector2D(180.0, 40.0);
	Snapshots.Add(Snapshot);
	TArray<TWeakObjectPtr<UDreamTweener>> Tweeners;
	Animation->OnApplyLayoutResults(Snapshots, Tweeners);
	if (!TestEqual(TEXT("One tween carries the child"), Tweeners.Num(), 1))
	{
		return false;
	}
	UDreamTweenerFloat* Tween = Cast<UDreamTweenerFloat>(Tweeners[0].Get());
	if (!TestNotNull(TEXT("and it is a float tween"), Tween))
	{
		return false;
	}
	// Stepped below by hand rather than by the rig's frames.
	Tween->SetTickType(EDreamTweenTickType::Manual);
	TestTrue(TEXT("While the child lives the setter reaches it"), Tween->setter.IsBound());

	const TWeakObjectPtr<UDreamWidget> WeakChild(Child);
	Child->DestroyWidget();
	// The test's own copy of the snapshot names the child too; let go of it before the collection, as the
	// container lets go of its own once the animation has started.
	Snapshots.Reset();
	Snapshot.Widget = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	Child = nullptr;
	TestFalse(TEXT("The destroyed child has been collected"), WeakChild.IsValid());

	// The setter used to capture the snapshot by value -- a pointer to the child the collector cannot see -- so it
	// stayed bound, and the next step wrote the child's rect into freed memory. Bound weakly to the child, it is
	// unbound now and the step writes nothing.
	TestFalse(TEXT("With the child gone the setter no longer reaches it"), Tween->setter.IsBound());
	Manager->ManualTick(0.1f);
	TestTrue(TEXT("and the tween steps on harmlessly"), IsValid(Tween));
	Tween->Kill();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayoutAnimationContainerUnregisterTest,
	"DreamGUI.Tween.LayoutAnimation.AContainerLeavingItsWidgetKillsTheAnimationItStarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayoutAnimationContainerUnregisterTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayoutAnimationTweenTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has a tween manager"), Manager))
	{
		return false;
	}
	UDreamWidget* Box = Rig.MakeWidget(TEXT("Box"), nullptr, FVector2D(400.0, 300.0));
	UDreamLayoutContainerOverlay* Overlay = Box != nullptr ? Box->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>() : nullptr;
	if (!TestNotNull(TEXT("A box laid out as an overlay"), Overlay))
	{
		return false;
	}
	Overlay->SetUseAnimation(true);
	UDreamLayoutAnimation_CommonTween* Animation = Overlay->CreateNewLayoutAnimation<UDreamLayoutAnimation_CommonTween>();
	UDreamWidget* Item = Rig.MakeWidget(TEXT("Item"), Box, FVector2D(100.0, 50.0));
	if (!TestNotNull(TEXT("with an animation"), Animation) || !TestNotNull(TEXT("and an item in it"), Item))
	{
		return false;
	}
	// The layout pass the item's arrival asks for animates it (the animation lasts 0.3 s; this is two frames).
	Rig.PumpFrames(2);
	const TArray<UDreamTweener*> Tweens = TweensRunningFor(Animation);
	if (!TestTrue(TEXT("The layout pass started the animation"), Tweens.Num() > 0))
	{
		return false;
	}
	bool bAllRunning = true;
	for (UDreamTweener* Tween : Tweens)
	{
		bAllRunning &= Manager->IsTweening(Tween);
	}
	TestTrue(TEXT("and it is still running"), bAllRunning);

	// On the screen's clock, as every other widget tween is: the item is on the rig's screen-space canvas, and a
	// pause menu's list laid out while the game is paused has to animate rather than wait for the game to resume.
	const UDreamUISettings* Settings = GetDefault<UDreamUISettings>();
	if (TestTrue(TEXT("The item is screen-space overlay UI"), Item->IsScreenSpaceOverlayUI()))
	{
		for (UDreamTweener* Tween : Tweens)
		{
			TestEqual(TEXT("Its tween pauses as screen-space UI is set to"), Tween->GetAffectByGamePause(), Settings->bScreenSpaceUIAffectByGamePause);
			TestEqual(TEXT("and follows time dilation as screen-space UI is set to"), Tween->GetAffectByTimeDilation(), Settings->bScreenSpaceUIAffectByTimeDilation);
		}
	}

	// A new container: the old one is unregistered from the box while its animation is in flight. Nothing used to
	// end that animation -- SnapshotLayout only kills the previous run when the next one starts.
	Box->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
	for (UDreamTweener* Tween : Tweens)
	{
		TestTrue(TEXT("Unregistering the container killed its animation"), Tween->IsMarkedToKill());
	}
	Rig.PumpFrames(1);
	for (UDreamTweener* Tween : Tweens)
	{
		TestFalse(TEXT("and the manager let it go"), Manager->IsTweening(Tween));
	}
	return true;
}

#endif
