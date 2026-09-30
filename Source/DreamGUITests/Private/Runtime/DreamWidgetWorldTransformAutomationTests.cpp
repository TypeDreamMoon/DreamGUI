// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Components/SceneComponent.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

#include "DreamWorldRaycastTestTypes.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A widget's world transform is composed when it is read, and what a move is announced to hears it once a frame.
 *
 * A write marks the widget and its subtree stale; GetWorldTransform composes a stale widget from its chain when it is
 * asked. What a move used to set off at every write, for every descendant -- the canvas marked, the visual told, the
 * OnTransformChanged listeners called -- happens once, at the UI manager's flush: after the layout pass, and again at
 * the end of the frame. r.DreamUI.DeferTransformNotifications 0 announces every write on the spot instead, as every
 * write used to be; the transforms read are the same either way, which is the first thing pinned here.
 *
 * The flush also chooses what a moved widget's canvas and visual are told, by the render-layer rules: a render layer
 * keeps the geometry of the widgets in it relative to itself, so when it moves as a whole its sections move and nothing
 * in it is transformed again. Those rules are checked through the walk every flush takes, handed which widgets to treat
 * as layers, so that they are pinned whichever widgets a canvas goes on to make layers of.
 */
namespace DreamWidgetWorldTransformTestLocal
{
	using DreamTests::Lifecycle::FScopedConsoleVariable;
	using DreamTests::Lifecycle::FScopedWorld;

	const TCHAR* const DeferSwitch = TEXT("r.DreamUI.DeferTransformNotifications");

	/** A widget of InWorld, registered, under InParent when there is one. */
	UDreamWidget* MakeWidget(UWorld* InWorld, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InSize = FVector2D(100.0, 100.0))
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(static_cast<float>(InSize.X));
		Widget->SetHeight(static_cast<float>(InSize.Y));
		Widget->OnRegister();
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
		}
		return Widget;
	}

	/** The root of a world-space canvas standing at the world origin, registered in InWorld. */
	UDreamWidget* MakeCanvasRoot(UWorld* InWorld, UDreamCanvas*& OutCanvas)
	{
		UDreamWidget* Root = MakeWidget(InWorld, TEXT("Root"), nullptr, FVector2D(400.0, 400.0));
		OutCanvas = Root->AddComponent<UDreamCanvas>();
		if (OutCanvas != nullptr)
		{
			OutCanvas->SetRenderMode(EDreamRenderMode::WorldSpace);
		}
		return Root;
	}

	/** An actor in InWorld whose root is a plain scene component standing at InLocation: a host for a panel. */
	USceneComponent* MakeHost(UWorld* InWorld, const FVector& InLocation)
	{
		AActor* Actor = InWorld->SpawnActor<AActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}
		USceneComponent* Host = NewObject<USceneComponent>(Actor, TEXT("Host"));
		Actor->SetRootComponent(Host);
		Host->RegisterComponent();
		Host->SetWorldLocation(InLocation);
		return Host;
	}

	/**
	 * The world transform InWidget's chain composes to now, composed in the order GetWorldTransform composes it -- its
	 * own over its parent's, a root's over its scene component's -- so that a transform composed from the same inputs
	 * is the same to the last bit, and a stale one is not.
	 */
	FTransform ComposeFromChain(const UDreamWidget* InWidget)
	{
		const FTransform Local = InWidget->GetRenderLocalTransform();
		if (const UDreamWidget* ParentWidget = InWidget->GetParent())
		{
			return Local * ComposeFromChain(ParentWidget);
		}
		if (const USceneComponent* Host = InWidget->GetAttachedRootSceneComponent())
		{
			return Local * Host->GetComponentTransform();
		}
		return Local;
	}

	/**
	 * How often each widget's OnTransformChanged listeners were called. Declared before the world it counts in, so it
	 * outlives the widgets; what is still alive when it goes stops being counted.
	 */
	struct FTransformEventCounts
	{
		~FTransformEventCounts()
		{
			for (const TPair<TWeakObjectPtr<UDreamWidget>, FDelegateHandle>& Binding : Bindings)
			{
				if (UDreamWidget* Widget = Binding.Key.Get())
				{
					Widget->GetTransformChangedEvent().Remove(Binding.Value);
				}
			}
		}
		void Listen(UDreamWidget* InWidget)
		{
			const FDelegateHandle Handle = InWidget->GetTransformChangedEvent().AddLambda([this, InWidget]()
			{
				++Counts.FindOrAdd(InWidget);
			});
			Bindings.Add(TPair<TWeakObjectPtr<UDreamWidget>, FDelegateHandle>(InWidget, Handle));
		}
		int32 Of(const UDreamWidget* InWidget) const
		{
			const int32* Count = Counts.Find(InWidget);
			return Count != nullptr ? *Count : 0;
		}
		void Reset()
		{
			Counts.Reset();
		}
	private:
		TMap<const UDreamWidget*, int32> Counts;
		TArray<TPair<TWeakObjectPtr<UDreamWidget>, FDelegateHandle>> Bindings;
	};

	bool AnyPending(const TArray<UDreamWidget*>& InWidgets)
	{
		for (const UDreamWidget* Widget : InWidgets)
		{
			if (Widget->IsTransformChangePending())
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Two canvases and the widgets a scripted run of moves goes through: a world-space panel standing on a scene
	 * component, and a screen-space root whose canvas scaler resizes it. Every widget is registered, so its moves wait
	 * for the flush whenever the switch says so.
	 *
	 *   WorldRoot (world-space canvas, on Host)
	 *     Panel
	 *       Button
	 *         Label
	 *         Icon
	 *       Other
	 *   ScreenRoot (screen-space canvas)
	 *     Corner (anchored to the top-right corner)
	 *     Bar (stretched along the top)
	 *       BarIcon (stretched over the bar, turned about its bottom-left corner)
	 */
	struct FScriptScene
	{
		FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		USceneComponent* Host = nullptr;
		UDreamCanvas* ScreenCanvas = nullptr;
		UDreamWidget* WorldRoot = nullptr;
		UDreamWidget* Panel = nullptr;
		UDreamWidget* Button = nullptr;
		UDreamWidget* Label = nullptr;
		UDreamWidget* Icon = nullptr;
		UDreamWidget* Other = nullptr;
		UDreamWidget* ScreenRoot = nullptr;
		UDreamWidget* Corner = nullptr;
		UDreamWidget* Bar = nullptr;
		UDreamWidget* BarIcon = nullptr;

		bool Build(FAutomationTestBase& InTest)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			if (!InTest.TestNotNull(TEXT("A game world with a UI manager"), Manager))
			{
				return false;
			}
			Host = MakeHost(World.World, FVector(500.0, 0.0, 0.0));
			if (!InTest.TestNotNull(TEXT("A scene component for the panel to stand on"), Host))
			{
				return false;
			}
			WorldRoot = MakeWidget(World.World, TEXT("WorldRoot"), nullptr, FVector2D(800.0, 600.0));
			UDreamCanvas* WorldCanvas = WorldRoot->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("A canvas on the world-space root"), WorldCanvas))
			{
				return false;
			}
			WorldCanvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			WorldCanvas->AttachToSceneComponent(Host);
			Panel = MakeWidget(World.World, TEXT("Panel"), WorldRoot, FVector2D(400.0, 300.0));
			Button = MakeWidget(World.World, TEXT("Button"), Panel, FVector2D(120.0, 40.0));
			Label = MakeWidget(World.World, TEXT("Label"), Button, FVector2D(80.0, 20.0));
			Icon = MakeWidget(World.World, TEXT("Icon"), Button, FVector2D(20.0, 20.0));
			Other = MakeWidget(World.World, TEXT("Other"), Panel, FVector2D(60.0, 60.0));
			Label->SetAnchoredPosition(FVector2D(10.0, 0.0));
			Icon->SetAnchoredPosition(FVector2D(-45.0, 0.0));
			Other->SetAnchoredPosition(FVector2D(120.0, -90.0));

			ScreenRoot = MakeWidget(World.World, TEXT("ScreenRoot"), nullptr);
			ScreenCanvas = ScreenRoot->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("A canvas on the screen-space root"), ScreenCanvas))
			{
				return false;
			}
			// The viewport after the render mode, the order the driver's rig uses: setting the mode applies the
			// headless world's two-pixel fallback, and the substitute is what undoes it.
			ScreenCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
			ScreenCanvas->SetViewportSizeOverride(FIntPoint(1280, 720));
			Corner = MakeWidget(World.World, TEXT("Corner"), ScreenRoot, FVector2D(100.0, 50.0));
			Corner->SetAnchorMin(FVector2D(1.0, 1.0));
			Corner->SetAnchorMax(FVector2D(1.0, 1.0));
			Corner->SetPivot(FVector2D(1.0, 1.0));
			Corner->SetAnchoredPosition(FVector2D(-20.0, -20.0));
			Bar = MakeWidget(World.World, TEXT("Bar"), ScreenRoot, FVector2D(100.0, 40.0));
			Bar->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(1.0, 1.0), false, false);
			Bar->SetPivot(FVector2D(0.5, 1.0));
			Bar->SetHeight(40.0f);
			BarIcon = MakeWidget(World.World, TEXT("BarIcon"), Bar);
			BarIcon->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
			// Turned about a corner, so that the bar growing moves the point it turns about.
			BarIcon->SetRenderTransformPivot(FVector2D(0.0, 0.0));
			BarIcon->SetRenderRotation(FRotator(0.0, 0.0, 15.0));
			return true;
		}

		TArray<UDreamWidget*> Widgets() const
		{
			return { WorldRoot, Panel, Button, Label, Icon, Other, ScreenRoot, Corner, Bar, BarIcon };
		}
	};

	/** One move of the script, and whether it leaves a change for the flush to announce while the switch defers. */
	struct FScriptStep
	{
		const TCHAR* What;
		TFunction<void(FScriptScene&)> Apply;
		bool bLeavesPendingWhenDeferred = true;
	};

	TArray<FScriptStep> MakeScript()
	{
		TArray<FScriptStep> Script;
		Script.Add({ TEXT("the panel's relative location"), [](FScriptScene& S) { S.Panel->SetRelativeLocation(FVector(0.0, 50.0, -30.0)); } });
		Script.Add({ TEXT("the panel's relative rotation"), [](FScriptScene& S) { S.Panel->SetRelativeRotation(FRotator(0.0, 0.0, 10.0).Quaternion()); } });
		Script.Add({ TEXT("the panel's relative scale"), [](FScriptScene& S) { S.Panel->SetRelativeScale(FVector(1.0, 1.25, 0.8)); } });
		Script.Add({ TEXT("the button's render translation"), [](FScriptScene& S) { S.Button->SetRenderTranslation(FVector(0.0, 15.0, 5.0)); } });
		Script.Add({ TEXT("the button's render rotation"), [](FScriptScene& S) { S.Button->SetRenderRotation(FRotator(0.0, 0.0, 30.0)); } });
		Script.Add({ TEXT("the button's render scale"), [](FScriptScene& S) { S.Button->SetRenderScale(FVector(1.0, 1.1, 0.9)); } });
		Script.Add({ TEXT("the button's render pivot"), [](FScriptScene& S) { S.Button->SetRenderTransformPivot(FVector2D(0.0, 0.0)); } });
		// The render transform turns about a corner now, so the width moves the point it turns about.
		Script.Add({ TEXT("the button's width"), [](FScriptScene& S) { S.Button->SetWidth(160.0f); } });
		Script.Add({ TEXT("the button's anchored position"), [](FScriptScene& S) { S.Button->SetAnchoredPosition(FVector2D(-40.0, 60.0)); } });
		Script.Add({ TEXT("the label moved to another parent, keeping its relative transform"), [](FScriptScene& S) { S.Label->SetParent(S.Other, false); } });
		Script.Add({ TEXT("the label moved back, keeping its world transform"), [](FScriptScene& S) { S.Label->SetParent(S.Button, true); } });
		Script.Add({ TEXT("the host moving"), [](FScriptScene& S) { S.Host->SetWorldLocation(FVector(650.0, -40.0, 25.0)); } });
		Script.Add({ TEXT("the host turning"), [](FScriptScene& S) { S.Host->SetWorldRotation(FRotator(0.0, 20.0, 0.0)); } });
		Script.Add({ TEXT("the canvas scaler resizing the screen root"), [](FScriptScene& S) { S.ScreenCanvas->SetViewportSizeOverride(FIntPoint(1920, 1080)); } });
		Script.Add({ TEXT("three writes to the button in a row"), [](FScriptScene& S)
		{
			S.Button->SetRenderRotation(FRotator(0.0, 0.0, 45.0));
			S.Button->SetRenderRotation(FRotator(0.0, 0.0, 60.0));
			S.Button->SetRenderTranslation(FVector(0.0, -10.0, 20.0));
		} });
		Script.Add({ TEXT("a frame of the UI manager"), [](FScriptScene& S)
		{
			++GFrameCounter;
			S.Manager->Tick(1.0f / 30.0f);
			S.Manager->SubmitCanvasDrawCall();
		}, false });
		Script.Add({ TEXT("a move after the frame, announced at its end"), [](FScriptScene& S)
		{
			S.Panel->SetRelativeLocation(FVector(0.0, -25.0, 40.0));
			S.Manager->OnWorldPreSendAllEndOfFrameUpdates(S.World.World);
		}, false });
		return Script;
	}

	/**
	 * The script with the switch at InDefer, the world transform of every widget read after every move and checked
	 * against its chain. False when the scene could not be built.
	 */
	bool RunScript(FAutomationTestBase& InTest, int32 InDefer, TArray<TArray<FTransform>>& OutReads)
	{
		const FScopedConsoleVariable Defer(DeferSwitch, InDefer);
		FScriptScene Scene;
		if (!Scene.Build(InTest))
		{
			return false;
		}
		// Settled first, so every step starts from a scene with nothing waiting.
		Scene.Manager->FlushTransformChanges();
		const TArray<UDreamWidget*> Widgets = Scene.Widgets();
		for (const FScriptStep& Step : MakeScript())
		{
			Step.Apply(Scene);
			if (InDefer != 0)
			{
				InTest.TestEqual(FString::Printf(TEXT("With the switch on, after %s, a change waits for the flush"), Step.What),
					AnyPending(Widgets), Step.bLeavesPendingWhenDeferred);
			}
			else
			{
				InTest.TestFalse(FString::Printf(TEXT("With the switch off, %s is announced before the setter returns"), Step.What),
					AnyPending(Widgets));
			}
			TArray<FTransform>& Reads = OutReads.AddDefaulted_GetRef();
			for (const UDreamWidget* Widget : Widgets)
			{
				const FTransform Read = Widget->GetWorldTransform();
				Reads.Add(Read);
				InTest.TestTrue(FString::Printf(TEXT("With the switch at %d, after %s, %s stands where its chain puts it"),
					InDefer, Step.What, *Widget->GetDisplayName()), Read.Equals(ComposeFromChain(Widget), 0.0));
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformParityTest,
	"DreamGUI.Widget.WorldTransform.AReadAfterAnyMoveIsTheSameWhetherTheMoveIsAnnouncedAtOnceOrAtTheFlush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformParityTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	// Every kind of move there is -- relative, render, a size under a render transform, anchors, reparenting both
	// ways, the host moving and turning, the canvas scaler, several writes at once, a frame, a move after the frame --
	// read after each, with the moves announced at the flush and with them announced at every write.
	TArray<TArray<FTransform>> Deferred;
	TArray<TArray<FTransform>> AtOnce;
	if (!RunScript(*this, 1, Deferred) || !RunScript(*this, 0, AtOnce))
	{
		return false;
	}
	if (!TestEqual(TEXT("Both runs read after every move"), Deferred.Num(), AtOnce.Num()))
	{
		return false;
	}
	const TArray<FScriptStep> Script = MakeScript();
	for (int32 Step = 0; Step < Deferred.Num(); ++Step)
	{
		if (!TestEqual(TEXT("Both runs read every widget"), Deferred[Step].Num(), AtOnce[Step].Num()))
		{
			return false;
		}
		for (int32 Widget = 0; Widget < Deferred[Step].Num(); ++Widget)
		{
			TestTrue(FString::Printf(TEXT("After %s, widget %d reads the same whether the move waits for the flush or not"),
				Script[Step].What, Widget), Deferred[Step][Widget].Equals(AtOnce[Step][Widget], 0.0));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformCoalescingTest,
	"DreamGUI.Widget.WorldTransform.ThreeMovesInAFrameComposeEachWidgetOnceAndAnnounceItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformCoalescingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	FTransformEventCounts Events;
	const FScopedConsoleVariable Defer(DeferSwitch, 1);
	FScopedWorld World(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
	{
		return false;
	}
	UDreamCanvas* Canvas = nullptr;
	UDreamWidget* Root = MakeCanvasRoot(World.World, Canvas);
	UDreamWidget* Moved = MakeWidget(World.World, TEXT("Moved"), Root);
	UDreamWidget* FirstChild = MakeWidget(World.World, TEXT("FirstChild"), Moved);
	UDreamWidget* SecondChild = MakeWidget(World.World, TEXT("SecondChild"), Moved);
	UDreamWidget* Grandchild = MakeWidget(World.World, TEXT("Grandchild"), SecondChild);
	const TArray<UDreamWidget*> Subtree = { Moved, FirstChild, SecondChild, Grandchild };
	for (UDreamWidget* Widget : Subtree)
	{
		Events.Listen(Widget);
	}
	Manager->FlushTransformChanges();
	Events.Reset();

	// An animation keying three channels of one widget writes it three times a frame.
	const uint64 ComposedBefore = UDreamWidget::GetWorldTransformComputeCount();
	Moved->SetRenderRotation(FRotator(0.0, 0.0, 10.0));
	Moved->SetRenderRotation(FRotator(0.0, 0.0, 20.0));
	Moved->SetRenderTranslation(FVector(0.0, 5.0, 5.0));
	TestEqual(TEXT("Nothing is composed while the widget is written"),
		UDreamWidget::GetWorldTransformComputeCount() - ComposedBefore, (uint64)0);
	for (const UDreamWidget* Widget : Subtree)
	{
		TestTrue(FString::Printf(TEXT("%s is stale after the writes"), *Widget->GetDisplayName()), Widget->IsWorldTransformDirty());
		TestTrue(FString::Printf(TEXT("%s waits for the flush"), *Widget->GetDisplayName()), Widget->IsTransformChangePending());
		TestEqual(FString::Printf(TEXT("%s has heard nothing yet"), *Widget->GetDisplayName()), Events.Of(Widget), 0);
	}

	Manager->FlushTransformChanges();
	TestEqual(TEXT("The flush composes each widget of the subtree once"),
		UDreamWidget::GetWorldTransformComputeCount() - ComposedBefore, (uint64)Subtree.Num());
	for (const UDreamWidget* Widget : Subtree)
	{
		TestEqual(FString::Printf(TEXT("%s hears the three writes as one move"), *Widget->GetDisplayName()), Events.Of(Widget), 1);
		TestFalse(FString::Printf(TEXT("%s is current after the flush"), *Widget->GetDisplayName()), Widget->IsWorldTransformDirty());
		TestFalse(FString::Printf(TEXT("%s has nothing left to announce"), *Widget->GetDisplayName()), Widget->IsTransformChangePending());
		TestTrue(FString::Printf(TEXT("%s stands where the last write put it"), *Widget->GetDisplayName()),
			Widget->GetWorldTransform().Equals(ComposeFromChain(Widget), 0.0));
	}

	// Nothing moved since: a second flush has nothing to say, and nothing is composed again.
	const uint64 ComposedAfter = UDreamWidget::GetWorldTransformComputeCount();
	Manager->FlushTransformChanges();
	TestEqual(TEXT("A flush with nothing pending composes nothing"), UDreamWidget::GetWorldTransformComputeCount() - ComposedAfter, (uint64)0);
	TestEqual(TEXT("... and announces nothing"), Events.Of(Moved), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformEventTimingTest,
	"DreamGUI.Widget.WorldTransform.ListenersHearAMoveAtTheFlushOrAtTheWriteAndAParentsListenerSeesItsChildMoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformEventTimingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	for (const int32 DeferValue : { 1, 0 })
	{
		const bool bDeferred = DeferValue != 0;
		FTransformEventCounts Events;
		FVector ChildSeenByParent = FVector::ZeroVector;
		FDelegateHandle ParentListener;
		const FScopedConsoleVariable Defer(DeferSwitch, DeferValue);
		FScopedWorld World(EWorldType::Game);
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
		if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
		{
			return false;
		}
		UDreamCanvas* Canvas = nullptr;
		UDreamWidget* Root = MakeCanvasRoot(World.World, Canvas);
		UDreamWidget* Parent = MakeWidget(World.World, TEXT("Parent"), Root);
		UDreamWidget* Child = MakeWidget(World.World, TEXT("Child"), Parent);
		Child->SetAnchoredPosition(FVector2D(30.0, 10.0));
		Events.Listen(Parent);
		Events.Listen(Child);
		// What a listener on a parent reads of a child: it used to be told before the child had been recomputed.
		ParentListener = Parent->GetTransformChangedEvent().AddLambda([&ChildSeenByParent, Child]()
		{
			ChildSeenByParent = Child->GetWorldLocation();
		});
		Manager->FlushTransformChanges();
		Events.Reset();

		const TCHAR* const Mode = bDeferred ? TEXT("at the flush") : TEXT("at the write");
		Parent->SetRelativeLocation(FVector(0.0, 100.0, -50.0));
		if (bDeferred)
		{
			TestEqual(TEXT("With the switch on, the parent hears nothing at the write"), Events.Of(Parent), 0);
			TestEqual(TEXT("... and neither does the child"), Events.Of(Child), 0);
			Manager->FlushTransformChanges();
		}
		TestEqual(FString::Printf(TEXT("The parent hears the move once, %s"), Mode), Events.Of(Parent), 1);
		TestEqual(FString::Printf(TEXT("The child hears it once too, %s"), Mode), Events.Of(Child), 1);
		TestTrue(FString::Printf(TEXT("A listener on the parent, called %s, sees the child where the move put it"), Mode),
			ChildSeenByParent.Equals(Child->GetWorldLocation(), 0.0)
			&& Child->GetWorldLocation().Equals(FVector(0.0, 130.0, -40.0), 0.001));

		// A second write in the same frame.
		Parent->SetRelativeLocation(FVector(0.0, 120.0, -50.0));
		if (bDeferred)
		{
			// After the tick: what the world's end-of-frame updates flush.
			TestEqual(TEXT("With the switch on, a move after the flush waits for the end of the frame"), Events.Of(Parent), 1);
			Manager->OnWorldPreSendAllEndOfFrameUpdates(World.World);
			TestEqual(TEXT("... which announces it"), Events.Of(Parent), 2);
		}
		else
		{
			TestEqual(TEXT("With the switch off, every write is heard as it happens"), Events.Of(Parent), 2);
			Manager->FlushTransformChanges();
			TestEqual(TEXT("... and a flush has nothing more to say"), Events.Of(Parent), 2);
		}
		TestEqual(FString::Printf(TEXT("The child hears the second move once, %s"), Mode), Events.Of(Child), 2);
		TestTrue(TEXT("... and the parent's listener sees it moved again"), ChildSeenByParent.Equals(Child->GetWorldLocation(), 0.0));
		Parent->GetTransformChangedEvent().Remove(ParentListener);
	}

	// A tree no world holds has no manager to wait for: every move is heard on the spot, whatever the switch says.
	FTransformEventCounts LooseEvents;
	const FScopedConsoleVariable Defer(DeferSwitch, 1);
	UDreamWidget* Loose = NewObject<UDreamWidget>(GetTransientPackage());
	UDreamWidget* LooseChild = NewObject<UDreamWidget>(GetTransientPackage());
	LooseChild->SetParent(Loose, false);
	LooseEvents.Listen(Loose);
	LooseEvents.Listen(LooseChild);
	Loose->SetRelativeLocation(FVector(0.0, 40.0, 0.0));
	TestEqual(TEXT("A widget no manager knows hears its move at the write"), LooseEvents.Of(Loose), 1);
	TestEqual(TEXT("... and so does its child"), LooseEvents.Of(LooseChild), 1);
	TestFalse(TEXT("... and nothing is left waiting"), Loose->IsTransformChangePending() || LooseChild->IsTransformChangePending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformRaycastTest,
	"DreamGUI.Widget.WorldTransform.ARaycastRightAfterAMoveHitsWhereTheWidgetNowStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformRaycastTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	// The input tick traces before the UI manager ticks, so between a move and the flush the hit test is on its own:
	// the transform it reads and the bounding sphere it culls by have to be the moved ones already.
	const FScopedConsoleVariable Defer(DeferSwitch, 1);
	FScopedWorld World(EWorldType::Game);
	USceneComponent* Host = MakeHost(World.World, FVector(300.0, 0.0, 0.0));
	UDreamWidget* Panel = UDreamUIBPLibrary::ConstructWidget(World.World, TEXT("Panel"), nullptr);
	if (!TestNotNull(TEXT("A host"), Host) || !TestNotNull(TEXT("A panel"), Panel))
	{
		return false;
	}
	Panel->SetWidth(400.0f);
	Panel->SetHeight(400.0f);
	UDreamCanvas* Canvas = Panel->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("A canvas on the panel"), Canvas))
	{
		return false;
	}
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	UDreamUIBPLibrary::AttachWidgetToSceneComponent(Panel, Host);
	// The button is the one thing on the panel a ray can hit, off to the side of the ray to begin with.
	UDreamWidget* Button = UDreamUIBPLibrary::ConstructWidget(World.World, TEXT("Button"), nullptr);
	if (!TestNotNull(TEXT("A button"), Button) || !TestTrue(TEXT("The button hangs on the panel"), Button->TrySetParent(Panel, false)))
	{
		return false;
	}
	Button->SetWidth(40.0f);
	Button->SetHeight(40.0f);
	Button->SetAnchoredPosition(FVector2D(120.0, 0.0));
	Button->CreateNewVisual<UDreamVisualEmpty>();

	UDreamWorldSpaceRaycasterFixedRay* Raycaster = nullptr;
	if (AActor* RaycasterHost = World.World->SpawnActor<AActor>())
	{
		Raycaster = NewObject<UDreamWorldSpaceRaycasterFixedRay>(RaycasterHost);
		Raycaster->RegisterComponent();
	}
	UDreamPointerEventData* Pointer = NewObject<UDreamPointerEventData>();
	if (!TestNotNull(TEXT("A raycaster down the world X axis"), Raycaster) || !TestNotNull(TEXT("A pointer"), Pointer))
	{
		return false;
	}
	auto Trace = [Raycaster, Pointer]()
	{
		FVector RayOrigin = FVector::ZeroVector, RayDirection = FVector::ZeroVector, RayEnd = FVector::ZeroVector;
		TArray<FDreamUIHitResult> Hits;
		Raycaster->Raycast(Pointer, RayOrigin, RayDirection, RayEnd, Hits);
		return Hits;
	};
	TArray<FDreamUIHitResult> Hits = Trace();
	TestEqual(TEXT("Beside the ray, the button is not hit"), Hits.Num(), 0);

	// Moved onto the ray, and traced before anything flushes.
	Button->SetRenderTranslation(FVector(0.0, -120.0, 0.0));
	TestTrue(TEXT("The move waits for the flush"), Button->IsTransformChangePending());
	Hits = Trace();
	if (TestEqual(TEXT("On the ray, the button is hit before the flush"), Hits.Num(), 1))
	{
		TestEqual(TEXT("... it is the button"), Hits[0].Widget.Get(), Button);
		TestTrue(TEXT("... at the panel's distance"), FMath::IsNearlyEqual(Hits[0].Distance, 300.0f, 1.0f));
	}
	TestTrue(TEXT("Tracing composed the moved transform without announcing it"),
		!Button->IsWorldTransformDirty() && Button->IsTransformChangePending());

	// The scene component the panel stands on moving takes the button off the ray again, the same way.
	Host->SetWorldLocation(FVector(300.0, 500.0, 0.0));
	Hits = Trace();
	TestEqual(TEXT("With its host moved aside, the panel's button is missed before the flush"), Hits.Num(), 0);
	Host->SetWorldLocation(FVector(300.0, 0.0, 0.0));
	Hits = Trace();
	TestEqual(TEXT("... and hit again once the host is back"), Hits.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformRenderLayerRulesTest,
	"DreamGUI.Widget.WorldTransform.AFlushTellsEachCanvasWhatMovedByTheRenderLayerRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformRenderLayerRulesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	using ENotice = EDreamTransformChangeNotice;
	struct FNotice
	{
		const UDreamWidget* Widget;
		ENotice Notice;
	};
	FTransformEventCounts Events;
	const FScopedConsoleVariable Defer(DeferSwitch, 1);
	FScopedWorld World(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
	{
		return false;
	}
	//   Root (a canvas's own widget)
	//     Outside
	//       Layer (treated as a render layer)
	//         Inner
	//           Leaf
	//           Nested (treated as a render layer)
	//             NestedLeaf
	UDreamCanvas* Canvas = nullptr;
	UDreamWidget* Root = MakeCanvasRoot(World.World, Canvas);
	UDreamWidget* Outside = MakeWidget(World.World, TEXT("Outside"), Root);
	UDreamWidget* Layer = MakeWidget(World.World, TEXT("Layer"), Outside);
	UDreamWidget* Inner = MakeWidget(World.World, TEXT("Inner"), Layer);
	UDreamWidget* Leaf = MakeWidget(World.World, TEXT("Leaf"), Inner);
	UDreamWidget* Nested = MakeWidget(World.World, TEXT("Nested"), Inner);
	UDreamWidget* NestedLeaf = MakeWidget(World.World, TEXT("NestedLeaf"), Nested);
	const TArray<UDreamWidget*> All = { Root, Outside, Layer, Inner, Leaf, Nested, NestedLeaf };
	for (UDreamWidget* Widget : All)
	{
		Events.Listen(Widget);
	}
	Manager->FlushTransformChanges();

	TArray<FNotice> Told;
	const auto AsLayers = [Layer, Nested](const UDreamWidget& InWidget) { return &InWidget == Layer || &InWidget == Nested; };
	const auto AsTheyAre = [](const UDreamWidget& InWidget) { return InWidget.IsRenderLayer(); };
	const auto Record = [&Told](UDreamWidget& InWidget, ENotice InNotice) { Told.Add({ &InWidget, InNotice }); };
	const auto NoticeName = [](ENotice InNotice)
	{
		switch (InNotice)
		{
		case ENotice::Canvas: return TEXT("Canvas");
		case ENotice::RenderLayer: return TEXT("RenderLayer");
		case ENotice::Moved: return TEXT("Moved");
		case ENotice::InsideMovedLayer: return TEXT("InsideMovedLayer");
		}
		return TEXT("?");
	};
	/**
	 * One case: InMove marks some widgets, the walk runs from InFrom with InIsLayer, and exactly InExpected is told --
	 * each widget once. Every widget the walk visits hears the move, whatever it is told. Nothing is left pending.
	 */
	const auto Case = [&](const TCHAR* InWhat, TFunctionRef<void()> InMove, UDreamWidget* InFrom,
		TFunctionRef<bool(const UDreamWidget&)> InIsLayer, const TArray<FNotice>& InExpected)
	{
		Told.Reset();
		Events.Reset();
		InMove();
		UDreamWidget::FlushTransformChangesFrom(InFrom, InIsLayer, Record);
		TestEqual(FString::Printf(TEXT("%s: as many widgets are told as expected"), InWhat), Told.Num(), InExpected.Num());
		for (const FNotice& Expected : InExpected)
		{
			const FNotice* Actual = Told.FindByPredicate([&Expected](const FNotice& InTold) { return InTold.Widget == Expected.Widget; });
			if (TestNotNull(FString::Printf(TEXT("%s: %s is told"), InWhat, *Expected.Widget->GetDisplayName()), Actual))
			{
				TestEqual(FString::Printf(TEXT("%s: %s is told %s"), InWhat, *Expected.Widget->GetDisplayName(), NoticeName(Expected.Notice)),
					FString(NoticeName(Actual->Notice)), FString(NoticeName(Expected.Notice)));
			}
			// Listeners hear a move whatever the canvas is told: the world transform changed either way.
			TestEqual(FString::Printf(TEXT("%s: %s hears the move once"), InWhat, *Expected.Widget->GetDisplayName()),
				Events.Of(Expected.Widget), 1);
		}
		TestFalse(FString::Printf(TEXT("%s: nothing is left pending"), InWhat), AnyPending(All));
	};

	// A layer turning moves as a whole: its sections, and those of the layer nested in it, move; nothing in either is
	// transformed again.
	Case(TEXT("A layer turning"), [Layer]() { Layer->SetRenderRotation(FRotator(0.0, 0.0, 10.0)); }, Layer, AsLayers,
		{ { Layer, ENotice::RenderLayer }, { Inner, ENotice::InsideMovedLayer }, { Leaf, ENotice::InsideMovedLayer },
		  { Nested, ENotice::RenderLayer }, { NestedLeaf, ENotice::InsideMovedLayer } });
	// A widget inside a layer moving moves relative to it: it and what is under it are drawn again, except what a
	// nested layer keeps relative to itself.
	Case(TEXT("A widget inside a layer turning"), [Inner]() { Inner->SetRenderRotation(FRotator(0.0, 0.0, 10.0)); }, Inner, AsLayers,
		{ { Inner, ENotice::Moved }, { Leaf, ENotice::Moved }, { Nested, ENotice::RenderLayer }, { NestedLeaf, ENotice::InsideMovedLayer } });
	// A widget in no layer moving: it is drawn again, and the layers below it move as wholes.
	Case(TEXT("A widget in no layer moving"), [Outside]() { Outside->SetRelativeLocation(FVector(0.0, 12.0, 0.0)); }, Outside, AsLayers,
		{ { Outside, ENotice::Moved }, { Layer, ENotice::RenderLayer }, { Inner, ENotice::InsideMovedLayer },
		  { Leaf, ENotice::InsideMovedLayer }, { Nested, ENotice::RenderLayer }, { NestedLeaf, ENotice::InsideMovedLayer } });
	// Two moves in one frame: the leaf moved on its own as well as with its layer, so it is drawn again.
	Case(TEXT("A layer and a widget inside it both moving"),
		[Layer, Leaf]() { Layer->SetRenderRotation(FRotator(0.0, 0.0, 20.0)); Leaf->SetRenderTranslation(FVector(0.0, 3.0, 0.0)); }, Layer, AsLayers,
		{ { Layer, ENotice::RenderLayer }, { Inner, ENotice::InsideMovedLayer }, { Leaf, ENotice::Moved },
		  { Nested, ENotice::RenderLayer }, { NestedLeaf, ENotice::InsideMovedLayer } });
	// The canvas's own widget keeps what it has always been told.
	Case(TEXT("The canvas's own widget moving"), [Root]() { Root->SetRelativeLocation(FVector(0.0, 0.0, 7.0)); }, Root, AsLayers,
		{ { Root, ENotice::Canvas }, { Outside, ENotice::Moved }, { Layer, ENotice::RenderLayer }, { Inner, ENotice::InsideMovedLayer },
		  { Leaf, ENotice::InsideMovedLayer }, { Nested, ENotice::RenderLayer }, { NestedLeaf, ENotice::InsideMovedLayer } });
	// A widget in a nested layer moving is drawn again, and nothing else is told.
	Case(TEXT("A widget inside a nested layer moving"), [NestedLeaf]() { NestedLeaf->SetRenderScale(FVector(1.0, 1.5, 1.5)); }, NestedLeaf, AsLayers,
		{ { NestedLeaf, ENotice::Moved } });

	// With layers as the canvas makes them -- none, on a canvas the engine's renderer draws -- every widget of a moved
	// subtree is drawn again, as every moved widget always was.
	Case(TEXT("A widget moving on a canvas without layers"), [Outside]() { Outside->SetRelativeLocation(FVector(0.0, 0.0, 0.0)); }, Outside, AsTheyAre,
		{ { Outside, ENotice::Moved }, { Layer, ENotice::Moved }, { Inner, ENotice::Moved }, { Leaf, ENotice::Moved },
		  { Nested, ENotice::Moved }, { NestedLeaf, ENotice::Moved } });
	Case(TEXT("The canvas's own widget moving on a canvas without layers"), [Root]() { Root->SetRelativeLocation(FVector(0.0, 0.0, 0.0)); }, Root, AsTheyAre,
		{ { Root, ENotice::Canvas }, { Outside, ENotice::Moved }, { Layer, ENotice::Moved }, { Inner, ENotice::Moved },
		  { Leaf, ENotice::Moved }, { Nested, ENotice::Moved }, { NestedLeaf, ENotice::Moved } });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTransformRunawayListenerTest,
	"DreamGUI.Widget.WorldTransform.AListenerThatAlwaysMovesItsWidgetAgainIsCutOffWithAWarning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTransformRunawayListenerTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetWorldTransformTestLocal;
	int32 Calls = 0;
	const FScopedConsoleVariable Defer(DeferSwitch, 1);
	FScopedWorld World(EWorldType::Game);
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
	if (!TestNotNull(TEXT("A game world with a UI manager"), Manager))
	{
		return false;
	}
	UDreamCanvas* Canvas = nullptr;
	UDreamWidget* Root = MakeCanvasRoot(World.World, Canvas);
	UDreamWidget* Restless = MakeWidget(World.World, TEXT("Restless"), Root);
	Manager->FlushTransformChanges();
	// Every move it hears, it answers with another. Each answer is heard in a further pass of the flush.
	const FDelegateHandle Listener = Restless->GetTransformChangedEvent().AddLambda([&Calls, Restless]()
	{
		++Calls;
		Restless->SetRenderTranslation(FVector(0.0, static_cast<double>(Calls), 0.0));
	});

	AddExpectedMessagePlain(TEXT("did not settle"), ELogVerbosity::Warning);
	Restless->SetRenderTranslation(FVector(0.0, -1.0, 0.0));
	Manager->FlushTransformChanges();
	TestEqual(TEXT("The flush gives up after as many passes as the layout loop would"), Calls, 32);
	TestTrue(TEXT("... and leaves the last answer for the next flush"), Restless->IsTransformChangePending());
	TestTrue(TEXT("... while the transform read is still the latest"), Restless->GetWorldTransform().Equals(ComposeFromChain(Restless), 0.0));

	// Quiet again, the next flush settles in one pass.
	Restless->GetTransformChangedEvent().Remove(Listener);
	Manager->FlushTransformChanges();
	TestFalse(TEXT("Without the listener, the next flush settles"), Restless->IsTransformChangePending());
	TestEqual(TEXT("... having called nobody"), Calls, 32);
	return true;
}

#endif
