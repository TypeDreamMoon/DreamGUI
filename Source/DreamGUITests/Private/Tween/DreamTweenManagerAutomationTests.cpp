// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Curves/CurveFloat.h"
#include "DreamTweenBPLibrary.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "DreamTweenerSequence.h"
#include "DreamTweenerSpring.h"
#include "Engine/World.h"
#include "DreamScopedGameInstanceWorld.h"

/*
 * The tween manager's side of a tween's life: when it is let go of, what a callback may still do to it on the
 * way, and how often the manager is stepped. The tweener tests next door step a tween by hand; these go through
 * a real manager -- a game instance's subsystem -- with every tween on Manual tick, so ManualTick is the only
 * clock and each step is exactly the delta handed to it.
 */
namespace DreamTweenManagerTestLocal
{
	/**
	 * A float a tween drives, and how often it was written. Shared rather than on the stack: tearing the world
	 * down kills whatever is still in the manager, and nothing a late setter writes can then land in a frame
	 * that has gone.
	 */
	struct FTweenedFloat
	{
		TSharedRef<float> Value = MakeShared<float>(0.0f);
		TSharedRef<int32> Writes = MakeShared<int32>(0);

		FDreamTweenFloatGetterFunction Getter() const
		{
			const TSharedRef<float> SharedValue = Value;
			return FDreamTweenFloatGetterFunction::CreateLambda([SharedValue] { return *SharedValue; });
		}
		FDreamTweenFloatSetterFunction Setter() const
		{
			const TSharedRef<float> SharedValue = Value;
			const TSharedRef<int32> SharedWrites = Writes;
			return FDreamTweenFloatSetterFunction::CreateLambda([SharedValue, SharedWrites](float InValue)
			{
				*SharedValue = InValue;
				++*SharedWrites;
			});
		}
	};

	/** A linear float tween from 0, stepped only by ManualTick. */
	UDreamTweener* MakeManualTween(UObject* InContext, const FTweenedFloat& InValue, float InEndValue, float InDuration)
	{
		UDreamTweener* Tween = UDreamTweenManager::To(InContext, InValue.Getter(), InValue.Setter(), InEndValue, InDuration);
		if (Tween != nullptr)
		{
			Tween->SetEase(EDreamTweenEase::Linear)->SetTickType(EDreamTweenTickType::Manual);
		}
		return Tween;
	}

	/** A counter a callback bumps, shared for the same reason as FTweenedFloat. */
	TFunction<void()> CountInto(const TSharedRef<int32>& InCounter)
	{
		return [InCounter] { ++*InCounter; };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerRetiredHandleTest,
	"DreamGUI.Tween.Manager.ARetiredTweenIsAnInvalidHandleThatNothingCanCompleteAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerRetiredHandleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}
	FTweenedFloat Value;
	UDreamTweener* Tween = MakeManualTween(TestWorld.World, Value, 1.0f, 0.5f);
	if (!TestNotNull(TEXT("A tween is made"), Tween))
	{
		return false;
	}
	const TSharedRef<int32> Completes = MakeShared<int32>(0);
	const TSharedRef<int32> Kills = MakeShared<int32>(0);
	Tween->OnComplete(CountInto(Completes));
	Tween->OnKill(CountInto(Kills));

	Manager->ManualTick(0.3f);
	Manager->ManualTick(0.3f);
	TestEqual(TEXT("It ran to its end"), *Value.Value, 1.0f, 0.001f);
	TestEqual(TEXT("and completed once"), *Completes, 1);
	TestFalse(TEXT("The manager has let it go"), Manager->IsTweening(Tween));
	// Held here as a raw pointer, the way a Blueprint variable or a member holds a finished tween. It used to be
	// ConditionalBeginDestroy'd under the holder and still answer as valid.
	TestFalse(TEXT("and the handle reads as gone"), IsValid(Tween));

	// Something else has moved the value on since; the retired tween must not throw it back.
	*Value.Value = 0.25f;
	const int32 WritesBefore = *Value.Writes;
	Tween->ForceComplete();
	TestEqual(TEXT("ForceComplete on it writes nothing"), *Value.Writes, WritesBefore);
	TestEqual(TEXT("so the value is still the one set since"), *Value.Value, 0.25f);
	TestEqual(TEXT("and it completes nothing a second time"), *Completes, 1);
	Tween->Kill(true);
	TestEqual(TEXT("Kill(true) on it completes nothing"), *Completes, 1);
	TestEqual(TEXT("and announces no kill"), *Kills, 0);
	AddExpectedMessagePlain(TEXT("nothing to restart"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Tween->Restart();
	Tween->Goto(0.1f);
	TestEqual(TEXT("Restart and Goto on it move nothing either"), *Value.Writes, WritesBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerRestartFromCompletionTest,
	"DreamGUI.Tween.Manager.RestartFromATweensOwnCompletionKeepsItRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerRestartFromCompletionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}

	for (const bool bAutoKill : { true, false })
	{
		const TCHAR* Kind = bAutoKill ? TEXT("auto-killed") : TEXT("held");
		FTweenedFloat Value;
		UDreamTweener* Tween = MakeManualTween(TestWorld.World, Value, 1.0f, 0.5f);
		if (!TestNotNull(TEXT("A tween is made"), Tween))
		{
			return false;
		}
		Tween->SetAutoKill(bAutoKill);
		// Restarts itself once, the ordinary "play it again" from a completion handler.
		const TSharedRef<int32> Restarts = MakeShared<int32>(0);
		Tween->OnComplete(TFunction<void()>([Restarts, Tween]
		{
			if (*Restarts == 0)
			{
				++*Restarts;
				Tween->Restart();
			}
		}));

		Manager->ManualTick(0.3f);
		Manager->ManualTick(0.3f);
		TestEqual(FString::Printf(TEXT("%s: the handler restarted it"), Kind), *Restarts, 1);
		// The step went on to report "finished" after the handler ran, and the manager retired the tween it had
		// just restarted -- or, held, paused it again -- leaving the value at the start for good.
		TestTrue(FString::Printf(TEXT("%s: it is still in the manager"), Kind), Manager->IsTweening(Tween));
		TestEqual(FString::Printf(TEXT("%s: back at its start"), Kind), *Value.Value, 0.0f, 0.001f);
		Manager->ManualTick(0.25f);
		TestEqual(FString::Printf(TEXT("%s: and running again from there"), Kind), *Value.Value, 0.5f, 0.001f);
		Manager->ManualTick(0.3f);
		TestEqual(FString::Printf(TEXT("%s: to its end"), Kind), *Value.Value, 1.0f, 0.001f);
		TestEqual(FString::Printf(TEXT("%s: without restarting a second time"), Kind), *Restarts, 1);
		if (!bAutoKill)
		{
			Tween->Kill();
			Manager->ManualTick(0.1f);
		}
		TestFalse(FString::Printf(TEXT("%s: and then it is over"), Kind), Manager->IsTweening(Tween));
	}

	// The same from KillAllTweens, which takes the list out before it kills: a tween its own completion handler
	// restarts goes back into the list instead of being dropped with the rest.
	FTweenedFloat Survivor;
	UDreamTweener* Restarted = MakeManualTween(TestWorld.World, Survivor, 1.0f, 0.5f);
	FTweenedFloat Victim;
	UDreamTweener* Killed = MakeManualTween(TestWorld.World, Victim, 1.0f, 0.5f);
	if (!TestNotNull(TEXT("Two tweens are made"), Restarted) || !TestNotNull(TEXT("..."), Killed))
	{
		return false;
	}
	Restarted->OnComplete(TFunction<void()>([Restarted] { Restarted->Restart(); }));
	Manager->ManualTick(0.1f);
	Manager->KillAllTweens(true);
	TestTrue(TEXT("The tween its handler restarted is still in the manager"), Manager->IsTweening(Restarted));
	TestFalse(TEXT("the other one is gone"), Manager->IsTweening(Killed));
	TestFalse(TEXT("and its handle with it"), IsValid(Killed));
	Restarted->Kill();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerGotoTest,
	"DreamGUI.Tween.Manager.GotoTheEndCompletesOnceAndGotoZeroShowsTheStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerGotoTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}

	FTweenedFloat Value;
	UDreamTweener* Tween = MakeManualTween(TestWorld.World, Value, 1.0f, 1.0f);
	if (!TestNotNull(TEXT("A tween is made"), Tween))
	{
		return false;
	}
	const TSharedRef<int32> Completes = MakeShared<int32>(0);
	Tween->OnComplete(CountInto(Completes));
	Manager->ManualTick(0.3f);
	Tween->Goto(1.0f);
	TestEqual(TEXT("Goto the end lands on the end value"), *Value.Value, 1.0f, 0.001f);
	TestEqual(TEXT("and completes the tween"), *Completes, 1);
	// The clock Goto left behind read as the start of a second cycle: the next step replayed the animation from
	// near its start and completed it again.
	Manager->ManualTick(0.1f);
	TestEqual(TEXT("The next step does not replay it"), *Value.Value, 1.0f, 0.001f);
	TestEqual(TEXT("or complete it a second time"), *Completes, 1);
	TestFalse(TEXT("it is retired instead"), Manager->IsTweening(Tween));

	FTweenedFloat Scrubbed;
	UDreamTweener* Paused = MakeManualTween(TestWorld.World, Scrubbed, 1.0f, 1.0f);
	if (!TestNotNull(TEXT("A second tween is made"), Paused))
	{
		return false;
	}
	Manager->ManualTick(0.5f);
	TestEqual(TEXT("Halfway"), *Scrubbed.Value, 0.5f, 0.001f);
	Paused->Pause();
	// A clock exactly at the end of the (zero) delay counts as still waiting, so this used to apply nothing and a
	// paused tween scrubbed back to its start went on showing wherever it had been.
	Paused->Goto(0.0f);
	TestEqual(TEXT("Goto 0 shows the start"), *Scrubbed.Value, 0.0f, 0.001f);
	Manager->ManualTick(0.5f);
	TestEqual(TEXT("and a paused tween stays there"), *Scrubbed.Value, 0.0f, 0.001f);
	Paused->Resume();
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("resumed, it runs on from the start"), *Scrubbed.Value, 0.25f, 0.001f);
	Paused->Kill();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerSpringRetargetTest,
	"DreamGUI.Tween.Manager.SetTargetOnASpringAtRestWakesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerSpringRetargetTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}
	FTweenedFloat Value;
	// Critically damped, so it settles well inside the five seconds stepped below.
	UDreamTweenerSpring* Spring = UDreamTweenManager::SpringTo(TestWorld.World, Value.Getter(), Value.Setter(), 100.0f, FDreamSpringParams(1.0f, 100.0f, 20.0f));
	if (!TestNotNull(TEXT("A spring is made"), Spring))
	{
		return false;
	}
	Spring->SetTickType(EDreamTweenTickType::Manual);
	const auto StepFiveSeconds = [Manager]()
	{
		for (int32 Frame = 0; Frame < 300; ++Frame)
		{
			Manager->ManualTick(1.0f / 60.0f);
		}
	};

	StepFiveSeconds();
	TestEqual(TEXT("The spring comes to rest at its target"), *Value.Value, 100.0f, 0.05f);
	// It used to be retired at rest, and "SetTarget at any time" then moved nothing.
	TestTrue(TEXT("At rest it is held, not retired"), Manager->IsTweening(Spring));
	Spring->SetTarget(200.0f);
	StepFiveSeconds();
	TestEqual(TEXT("A new target wakes it and it goes there"), *Value.Value, 200.0f, 0.05f);
	TestTrue(TEXT("and it is held at rest again"), Manager->IsTweening(Spring));

	// Asked to retire at rest, it does.
	FTweenedFloat Other;
	UDreamTweenerSpring* Retiring = UDreamTweenManager::SpringTo(TestWorld.World, Other.Getter(), Other.Setter(), 50.0f, FDreamSpringParams(1.0f, 100.0f, 20.0f));
	if (!TestNotNull(TEXT("A second spring is made"), Retiring))
	{
		return false;
	}
	Retiring->SetTickType(EDreamTweenTickType::Manual);
	Retiring->SetAutoKill(true);
	StepFiveSeconds();
	TestEqual(TEXT("A spring told to retire at rest still reaches its target"), *Other.Value, 50.0f, 0.05f);
	TestFalse(TEXT("and is then let go of"), Manager->IsTweening(Retiring));
	Spring->Kill();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerOwnerGoneTest,
	"DreamGUI.Tween.Manager.ATweenWhoseOwnerIsDestroyedEndsWithoutCallingIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerOwnerGoneTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}

	// The widget case: an endless colour yoyo on a widget's visual, and the widget destroyed under it.
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(TEXT("Pulsing"));
		Widget->OnRegister();
		UDreamVisualEmpty* Visual = Widget->CreateNewVisual<UDreamVisualEmpty>();
		UDreamTweener* Pulse = Visual != nullptr ? Visual->ColorTo(FColor::Red, 0.5f) : nullptr;
		if (!TestNotNull(TEXT("The visual's colour tween is made"), Pulse))
		{
			return false;
		}
		Pulse->SetTickType(EDreamTweenTickType::Manual)->SetLoop(EDreamTweenLoop::Yoyo, -1);
		Manager->ManualTick(0.1f);
		TestTrue(TEXT("The pulse runs"), Manager->IsTweening(Pulse));
		Widget->DestroyWidget();
		Manager->ManualTick(0.1f);
		TestFalse(TEXT("Once its widget is destroyed the endless pulse is over"), Manager->IsTweening(Pulse));
	}

	// The same for an owner of any kind, destroyed alone: an actor's Destroy marks the actor, not the tweens made
	// on it, and such a tween went on stepping -- and calling back into its owner's remains -- until the map changed.
	UCurveFloat* Owner = NewObject<UCurveFloat>(TestWorld.World);
	FTweenedFloat Value;
	UDreamTweener* Tween = MakeManualTween(Owner, Value, 1.0f, 1.0f);
	if (!TestNotNull(TEXT("A tween made on the owner"), Tween))
	{
		return false;
	}
	Tween->SetLoop(EDreamTweenLoop::Yoyo, -1);
	const TSharedRef<int32> Completes = MakeShared<int32>(0);
	const TSharedRef<int32> Kills = MakeShared<int32>(0);
	const TSharedRef<int32> Updates = MakeShared<int32>(0);
	Tween->OnComplete(CountInto(Completes));
	Tween->OnKill(CountInto(Kills));
	Tween->OnUpdate(TFunction<void(float)>([Updates](float) { ++*Updates; }));
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("It runs"), *Value.Value, 0.25f, 0.001f);
	const int32 WritesBefore = *Value.Writes;
	const int32 UpdatesBefore = *Updates;

	Owner->MarkAsGarbage();
	Manager->ManualTick(0.25f);
	TestFalse(TEXT("With its owner gone the tween is over"), Manager->IsTweening(Tween));
	TestEqual(TEXT("it wrote nothing more"), *Value.Writes, WritesBefore);
	TestEqual(TEXT("reported no more progress"), *Updates, UpdatesBefore);
	TestEqual(TEXT("and called neither its completion"), *Completes, 0);
	TestEqual(TEXT("nor its kill handlers into what is left"), *Kills, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerOncePerFrameTest,
	"DreamGUI.Tween.Manager.ASecondTickHelperInTheSameFrameDoesNotDoubleTheSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerOncePerFrameTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UWorld* World = TestWorld.World;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}
	// Every tick type but Manual reads the world's own clock, so that is what the frame is made of.
	World->DeltaTimeSeconds = 0.1f;
	World->DeltaRealTimeSeconds = 0.1f;
	FTweenedFloat Value;
	UDreamTweener* Tween = UDreamTweenManager::To(World, Value.Getter(), Value.Setter(), 1.0f, 1.0f);
	if (!TestNotNull(TEXT("A tween on the default tick group"), Tween))
	{
		return false;
	}
	Tween->SetEase(EDreamTweenEase::Linear);

	// Two helpers driving the one manager: every game world of a game instance spawns one, so a second game
	// world sharing it -- a game preview, say -- is two helpers ticking the same manager each frame.
	ADreamTweenTickHelperActor* First = World->SpawnActor<ADreamTweenTickHelperActor>();
	ADreamTweenTickHelperActor* Second = World->SpawnActor<ADreamTweenTickHelperActor>();
	if (!TestNotNull(TEXT("Two tick helpers"), First) || !TestNotNull(TEXT("..."), Second))
	{
		return false;
	}
	First->Target = Manager;
	Second->Target = Manager;

	First->Tick(0.1f);
	Second->Tick(0.1f);
	TestEqual(TEXT("Two helpers in one frame step the tween by one frame, not two"), *Value.Value, 0.1f, 0.001f);
	// The next engine frame: the group is due again, whichever helper asks first.
	++GFrameCounter;
	Second->Tick(0.1f);
	First->Tick(0.1f);
	TestEqual(TEXT("and once more the next frame"), *Value.Value, 0.2f, 0.001f);
	// Tick itself is not deduplicated: a caller pumping frames of its own (the test driver does) steps every time.
	Manager->Tick(EDreamTweenTickType::DuringPhysics, 0.1f);
	TestEqual(TEXT("Tick steps whenever it is called"), *Value.Value, 0.3f, 0.001f);
	Tween->Kill();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenManagerSmallFixesTest,
	"DreamGUI.Tween.Manager.RepeatCallOfNoRepeatsCallsNothingAndASequenceTakesItsChildFromTheManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenManagerSmallFixesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenManagerTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager))
	{
		return false;
	}

	// RepeatCall with a count of 0 set the same single cycle a count of 1 does, and called the function once.
	const TSharedRef<int32> Calls = MakeShared<int32>(0);
	UDreamTweener* None = UDreamTweenBPLibrary::RepeatCall(TestWorld.World, CountInto(Calls), 0.0f, 0.1f, 0);
	UDreamTweener* Once = UDreamTweenBPLibrary::RepeatCall(TestWorld.World, CountInto(Calls), 0.0f, 0.1f, 1);
	if (!TestNotNull(TEXT("A repeat of none still hands a tween back"), None) || !TestNotNull(TEXT("..."), Once))
	{
		return false;
	}
	None->SetTickType(EDreamTweenTickType::Manual);
	Once->SetTickType(EDreamTweenTickType::Manual);
	Manager->ManualTick(0.2f);
	Manager->ManualTick(0.2f);
	TestEqual(TEXT("Only the repeat of one called its function"), *Calls, 1);

	// A child handed to a sequence with no context of the caller's -- C++ building one by hand passes none -- used
	// to stay in the manager's list as well, stepped by both.
	FTweenedFloat Value;
	UDreamTweener* Child = MakeManualTween(TestWorld.World, Value, 1.0f, 1.0f);
	UDreamTweenerSequence* Sequence = UDreamTweenManager::CreateSequence(TestWorld.World);
	if (!TestNotNull(TEXT("A child"), Child) || !TestNotNull(TEXT("and a sequence"), Sequence))
	{
		return false;
	}
	Sequence->SetTickType(EDreamTweenTickType::Manual);
	TestTrue(TEXT("The child starts out in the manager"), Manager->IsTweening(Child));
	Sequence->Append(nullptr, Child);
	TestFalse(TEXT("and the sequence takes it out"), Manager->IsTweening(Child));
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("the sequence alone steps it"), *Value.Value, 0.25f, 0.001f);

	// A negative or not-a-number step moves nothing.
	Manager->ManualTick(-1.0f);
	TestEqual(TEXT("A negative step moves nothing back"), *Value.Value, 0.25f, 0.001f);
	Sequence->Kill();
	return true;
}

#endif
