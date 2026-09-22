// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Event/DreamUIEventDelegate.h"
#include "Framework/Application/SlateApplication.h"
#include "PlayTween/DreamUIPlayTween.h"
#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "PlayTween/DreamUIPlayTweenSequenceComponent.h"
#include "PlayTween/DreamUIPlayTween_Params.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/*
 * PlayTween is the UI-facing wrapper over DreamTween: an authored block of timing that a designer
 * fills in on a component, and one Start() that turns it into a live tweener. Almost nothing about
 * it is arithmetic -- the interpolation lives in the tweener -- so what these tests hold onto is the
 * TIMELINE: when the first update arrives relative to the delay, how many cycle completions a loop
 * count is worth, which direction a yoyo's second pass runs, and what a pause or a stop does to a
 * tween already in flight. Those are the things a UI author notices and a compiler never does.
 *
 * Two facts shape the fixture:
 *
 * The tween manager is a GAME-INSTANCE subsystem. Reached through UGameplayStatics, it is simply
 * absent in an editor test, and UDreamTweenManager::To then returns null so Start() is a silent
 * no-op. That state is real -- a widget built in an authoring tree behaves exactly this way -- and
 * gets a test of its own. Everything else needs a game instance, which the fixture below builds.
 *
 * The tweener is driven here by ToNext rather than by the manager's tick. UDreamUIPlayTween never
 * sets a tick type, so its tweener sits in the DuringPhysics group, which only a real world tick
 * reaches; moving it to Manual to make ManualTick work would mean asserting about a configuration
 * that never ships. ToNext is the same entry point the manager's own tick calls, one frame at a
 * time, with the frame length chosen by the test instead of by the clock.
 *
 * A play tween's AUTHORED events -- the FDreamUIEventDelegate ones a designer fills in, as opposed to
 * the C++ and Blueprint multicasts beside them -- do nothing at all until something is bound to them,
 * and what they do when something IS bound is the interesting part: the event checks the value it
 * fires against its declared parameter type and, on a mismatch, logs and skips the call rather than
 * failing loudly. Nothing short of a real binding can tell the two outcomes apart, so the fixture
 * builds one by reflection, the way the details panel does.
 */
namespace DreamPlayTweenTestLocal
{
	/**
	 * A game world with a game instance behind it, which is the only way to make a tween manager
	 * exist outside a running game. The world is created the way the rest of this suite creates one;
	 * the game instance is attached to it directly rather than through the engine's world-context
	 * list, so nothing global is left changed if a test fails partway.
	 */
	struct FScopedTweenWorld
	{
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;

		FScopedTweenWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			// UGameInstance::Init reaches for the Slate application to register a console listener.
			// Failing that assertion would take the whole run down, so a headless host gets a null
			// manager and a clean test failure instead of a crash.
			if (World != nullptr && FSlateApplication::IsInitialized())
			{
				GameInstance = NewObject<UGameInstance>(World);
				World->SetGameInstance(GameInstance);
				GameInstance->Init();
			}
		}

		~FScopedTweenWorld()
		{
			if (GameInstance != nullptr)
			{
				GameInstance->Shutdown();
			}
			if (World != nullptr)
			{
				World->SetGameInstance(nullptr);
				World->DestroyWorld(false);
			}
		}

		UDreamTweenManager* Manager() const
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UDreamTweenManager>() : nullptr;
		}

		/** Outered to the world, because that is the only chain UGameplayStatics can walk to find it. */
		template<typename PlayTweenT>
		PlayTweenT* MakePlayTween() const { return NewObject<PlayTweenT>(World); }
	};

	/** One frame of the tween, at a length the test picks. Returns false once the tween is finished. */
	static bool Advance(UDreamTweener* Tweener, float DeltaTime)
	{
		return Tweener->ToNext(DeltaTime, DeltaTime);
	}

	/** What a play tween reports as it runs, in the order it reports it. */
	struct FTweenLog
	{
		int32 Starts = 0;
		int32 Completes = 0;
		TArray<float> Progress;
		TArray<int32> CycleCounts;

		void Watch(UDreamUIPlayTween* PlayTween)
		{
			PlayTween->OnStartCPP.AddLambda([this] { Starts++; });
			PlayTween->OnCompleteCPP.AddLambda([this] { Completes++; });
			PlayTween->OnUpdateProgressCPP.AddLambda([this](float InProgress) { Progress.Add(InProgress); });
			PlayTween->OnCycleCompleteCPP.AddLambda([this](int32 InCount) { CycleCounts.Add(InCount); });
		}

		float LastProgress() const { return Progress.Num() > 0 ? Progress.Last() : -1.0f; }
	};

	/** Tick a tween until it retires. The frame budget is there so a broken loop fails rather than hangs. */
	static void RunToEnd(UDreamTweener* Tweener, float DeltaTime = 0.25f)
	{
		int32 Frames = 0;
		while (Tweener != nullptr && Advance(Tweener, DeltaTime) && Frames < 64)
		{
			Frames++;
		}
	}

	/**
	 * Fill a sequence's tween list. The array is a protected Instanced property with no setter -- the
	 * details panel is its only author -- so a test writes it the same way the panel does, through
	 * reflection. A null entry is spellable here because it is spellable there: a row exists from the
	 * moment somebody presses "+", and holds nothing until a class is chosen for it.
	 */
	static bool SetSequenceTweens(UDreamUIPlayTweenSequenceComponent* InSequence,
		const TArray<UDreamUIPlayTween*>& InTweens)
	{
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(
			UDreamUIPlayTweenSequenceComponent::StaticClass()->FindPropertyByName(TEXT("PlayTweenArray")));
		if (ArrayProperty == nullptr)
		{
			return false;
		}
		const FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ArrayProperty->Inner);
		if (Inner == nullptr)
		{
			return false;
		}
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(InSequence));
		Helper.EmptyValues();
		Helper.AddValues(InTweens.Num());
		for (int32 Index = 0; Index < InTweens.Num(); Index++)
		{
			Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), InTweens[Index]);
		}
		return true;
	}

	/**
	 * Bind an authored event to a one-argument function on another object, exactly as a designer
	 * picking a function in the details panel would: the event's own binding list is a private
	 * property, so it is filled through reflection rather than through an API that does not exist
	 * outside the editor customization.
	 *
	 * bUseNativeParameter is what makes the binding carry the value the event FIRES with instead of a
	 * value stored on the binding -- which is the whole point here, since what these tests need to
	 * know is what the event handed over.
	 */
	static bool BindAuthoredEvent(UObject* InEventOwner, const TCHAR* InEventName,
		UObject* InTarget, const TCHAR* InFunctionName, EDreamUIEventDelegateParameterType InParamType)
	{
		const FStructProperty* EventProperty =
			CastField<FStructProperty>(InEventOwner->GetClass()->FindPropertyByName(InEventName));
		if (EventProperty == nullptr || EventProperty->Struct != FDreamUIEventDelegate::StaticStruct())
		{
			return false;
		}
		const FArrayProperty* ListProperty = CastField<FArrayProperty>(
			FDreamUIEventDelegate::StaticStruct()->FindPropertyByName(TEXT("EventList")));
		if (ListProperty == nullptr)
		{
			return false;
		}

		void* EventPtr = EventProperty->ContainerPtrToValuePtr<void>(InEventOwner);
		FScriptArrayHelper Helper(ListProperty, ListProperty->ContainerPtrToValuePtr<void>(EventPtr));
		void* BindingPtr = Helper.GetRawPtr(Helper.AddValue());

		UScriptStruct* BindingStruct = FDreamUIEventDelegateData::StaticStruct();
		const FObjectPropertyBase* TargetProperty =
			CastField<FObjectPropertyBase>(BindingStruct->FindPropertyByName(TEXT("TargetObject")));
		const FNameProperty* FunctionProperty =
			CastField<FNameProperty>(BindingStruct->FindPropertyByName(TEXT("FunctionName")));
		const FBoolProperty* NativeProperty =
			CastField<FBoolProperty>(BindingStruct->FindPropertyByName(TEXT("bUseNativeParameter")));
		FProperty* TypeProperty = BindingStruct->FindPropertyByName(TEXT("ParamType"));
		if (TargetProperty == nullptr || FunctionProperty == nullptr
			|| NativeProperty == nullptr || TypeProperty == nullptr)
		{
			return false;
		}

		TargetProperty->SetObjectPropertyValue(
			TargetProperty->ContainerPtrToValuePtr<void>(BindingPtr), InTarget);
		FunctionProperty->SetPropertyValue(
			FunctionProperty->ContainerPtrToValuePtr<void>(BindingPtr), FName(InFunctionName));
		NativeProperty->SetPropertyValue(
			NativeProperty->ContainerPtrToValuePtr<void>(BindingPtr), true);
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(TypeProperty))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(
				EnumProperty->ContainerPtrToValuePtr<void>(BindingPtr), (int64)InParamType);
		}
		else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(TypeProperty))
		{
			NumericProperty->SetIntPropertyValue(
				NumericProperty->ContainerPtrToValuePtr<void>(BindingPtr), (int64)InParamType);
		}
		else
		{
			return false;
		}
		return true;
	}

	/**
	 * A stand-in for whatever a designer would wire an authored event to. Any UObject with a
	 * one-argument BlueprintCallable setter serves -- the event finds the function by name through the
	 * reflection system, so nothing about the class matters beyond the signature -- and a play tween
	 * that is never started happens to expose both a float setter and an integer one whose values read
	 * straight back, which saves declaring a class that would exist only for this file.
	 */
	struct FEventRecorder
	{
		/** A value no tween under test produces, so "never called" reads differently from "called with". */
		static constexpr int32 Nothing = -1;

		UDreamUIPlayTween_Float* Object = nullptr;

		explicit FEventRecorder(const FScopedTweenWorld& InFixture)
			: Object(InFixture.MakePlayTween<UDreamUIPlayTween_Float>())
		{
			Object->SetDuration((float)Nothing);
			Object->SetLoopCount(Nothing);
		}

		float ReceivedFloat() const { return Object->GetDuration(); }
		int32 ReceivedInt() const { return Object->GetLoopCount(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenNoManagerTest,
	"DreamGUI.PlayTween.APlayTweenWithNoTweenManagerReachableStartsNothingAndStopsCleanly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenNoManagerTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A play tween authored into a prefab exists long before anything is playing: in the class
	// defaults, in a designer preview, in an authoring tree with no world at all. Start() has to
	// come back empty-handed in that state rather than half-building a tween nobody will ever tick,
	// and Stop() has to survive being called on the result -- which the sequence component does
	// unconditionally on teardown.
	UDreamUIPlayTween_Float* PlayTween = NewObject<UDreamUIPlayTween_Float>(GetTransientPackage());
	TestNull(TEXT("a fresh play tween holds no tweener"), PlayTween->GetTweener());

	PlayTween->Stop();
	PlayTween->Start();
	TestNull(TEXT("and cannot make one without a tween manager"), PlayTween->GetTweener());
	PlayTween->Stop();

	// The authored settings are still settings: they survive a failed start, so the same object
	// works the moment it is instanced into a real world.
	PlayTween->SetDuration(2.5f);
	PlayTween->SetStartDelay(0.75f);
	PlayTween->SetLoopType(EDreamTweenLoop::Yoyo);
	PlayTween->SetLoopCount(4);
	PlayTween->SetEaseType(EDreamTweenEase::OutBounce);
	PlayTween->SetAffectByGamePause(true);
	PlayTween->SetAffectByTimeDilation(true);
	TestEqual(TEXT("the duration is kept"), PlayTween->GetDuration(), 2.5f);
	TestEqual(TEXT("and the delay"), PlayTween->GetStartDelay(), 0.75f);
	TestEqual(TEXT("and the loop type"), PlayTween->GetLoopType(), EDreamTweenLoop::Yoyo);
	TestEqual(TEXT("and the loop count"), PlayTween->GetLoopCount(), 4);
	TestEqual(TEXT("and the ease"), PlayTween->GetEaseType(), EDreamTweenEase::OutBounce);
	TestTrue(TEXT("and the pause flag"), PlayTween->GetAffectByGamePause());
	TestTrue(TEXT("and the dilation flag"), PlayTween->GetAffectByTimeDilation());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenStartWiringTest,
	"DreamGUI.PlayTween.StartHandsTheAuthoredTimingToTheTweenerItCreates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenStartWiringTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Start() is a transcription: nine authored properties copied onto a freshly made tweener. The
	// two flags below are the ones a UI cares about most -- a menu tween that respects game pause or
	// time dilation freezes with the world, which is right for a gameplay effect and wrong for the
	// pause menu itself, so the defaults are the opposite of the tweener's own.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	TestFalse(TEXT("a play tween ignores game pause by default"), PlayTween->GetAffectByGamePause());
	TestFalse(TEXT("and time dilation"), PlayTween->GetAffectByTimeDilation());

	PlayTween->SetDuration(1.25f);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}
	TestEqual(TEXT("carrying the authored duration"), Tweener->GetDuration(), 1.25f);
	TestFalse(TEXT("and the pause flag, which the tweener defaults the other way"),
		Tweener->GetAffectByGamePause());
	TestFalse(TEXT("and the dilation flag, likewise"), Tweener->GetAffectByTimeDilation());
	TestEqual(TEXT("with the clock still at zero"), Tweener->GetElapsedTime(), 0.0f);
	TestTrue(TEXT("and the manager holding it, which is what makes Stop able to find it"),
		Fixture.Manager()->IsTweening(Tweener));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenStartDelayTest,
	"DreamGUI.PlayTween.TheStartDelayHoldsBackTheFirstUpdateWithoutLosingTheTimePastIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenStartDelayTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A start delay is how a UI staggers a row of elements, so two things have to hold: nothing fires
	// during the delay -- not the start event either, since that is what an author hangs a sound on
	// -- and the tween picks up where the delay ended rather than at zero. Losing the remainder makes
	// every staggered element run a frame short, which reads as jitter across the row.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	PlayTween->SetStartDelay(0.5f);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	TestTrue(TEXT("the tween is alive during the delay"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("but has not started"), Log.Starts, 0);
	TestEqual(TEXT("and has reported no progress"), Log.Progress.Num(), 0);

	// Exactly at the delay is still the delay: the comparison is strictly greater, so a delay of one
	// frame means the tween begins on the frame after it, not on it.
	TestTrue(TEXT("still alive at exactly the delay"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("and still not started"), Log.Starts, 0);

	TestTrue(TEXT("and alive past it"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("now it has started, once"), Log.Starts, 1);
	TestEqual(TEXT("and reported the time past the delay, not the whole elapsed time"),
		Log.LastProgress(), 0.25f);

	TestTrue(TEXT("and it keeps running"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("start does not fire again"), Log.Starts, 1);
	TestEqual(TEXT("and progress advances by the frame"), Log.LastProgress(), 0.5f);
	TestEqual(TEXT("with nothing completed yet"), Log.Completes, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenZeroDurationTest,
	"DreamGUI.PlayTween.AZeroDurationTweenCompletesOnItsFirstTickWithoutDividingByIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenZeroDurationTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A zero duration is what an author leaves behind when a tween is used as a "set it now" step in
	// a sequence, or when a duration is driven from data that came back empty. Progress is reported
	// as elapsed/duration, so the only thing keeping an infinity out of the value the tween applies
	// is that the completion branch is taken FIRST -- and completion has to actually happen, or a
	// zero-length step in a sequence never hands over to the next one.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(0.0f);
	PlayTween->SetLoopType(EDreamTweenLoop::Once);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	TestFalse(TEXT("one frame finishes it"), Advance(Tweener, 0.016f));
	TestEqual(TEXT("it did start"), Log.Starts, 1);
	TestEqual(TEXT("and completed"), Log.Completes, 1);
	TestEqual(TEXT("having reported progress once"), Log.Progress.Num(), 1);
	TestEqual(TEXT("as a finished one, not an infinity"), Log.LastProgress(), 1.0f);
	TestTrue(TEXT("and the number really is finite"), FMath::IsFinite(Log.LastProgress()));

	// The cycle-complete event fires for a non-looping tween too: one cycle is still a cycle. Worth
	// pinning because the sequence component can be configured to advance on cycle completion, and
	// it has to advance past a Once tween as well.
	TestEqual(TEXT("with one cycle reported"), Log.CycleCounts.Num(), 1);
	TestEqual(TEXT("numbered from one"), Log.CycleCounts[0], 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenRestartLoopTest,
	"DreamGUI.PlayTween.ARestartLoopReportsEveryCycleAndCompletesOnTheLastOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenRestartLoopTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A loop count is a count of CYCLES, and the cycle number handed to the event is one-based. Both
	// halves matter to a caller: the sequence component chains on these events, and a UI that shows
	// "3 of 5" reads the number straight out of them. The completion has to land on the last cycle
	// and not one cycle later, or a looping tween outlives its own sequence step.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(0.5f);
	PlayTween->SetLoopType(EDreamTweenLoop::Restart);
	PlayTween->SetLoopCount(3);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	int32 Frames = 0;
	while (Advance(Tweener, 0.25f) && Frames < 32)
	{
		Frames++;
	}
	// Three half-second cycles at a quarter of a second a frame: five frames keep it alive and the
	// sixth is the one that ends it, which is the frame the completion lands on.
	TestEqual(TEXT("the tween survives exactly five frames"), Frames, 5);
	TestEqual(TEXT("started once, not once per cycle"), Log.Starts, 1);
	TestEqual(TEXT("completed once, at the end of the last cycle"), Log.Completes, 1);
	TestEqual(TEXT("with one cycle report per cycle"), Log.CycleCounts.Num(), 3);
	TestEqual(TEXT("counting from one"), Log.CycleCounts[0], 1);
	TestEqual(TEXT("upwards"), Log.CycleCounts[1], 2);
	TestEqual(TEXT("to the loop count"), Log.CycleCounts[2], 3);
	TestEqual(TEXT("and the last progress reported is a full one"), Log.LastProgress(), 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenYoyoTest,
	"DreamGUI.PlayTween.AYoyoLoopRunsItsSecondCycleBackwards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenYoyoTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Yoyo is the difference between a pulse and a sawtooth, and the reversal is invisible from
	// anything but the reported progress: the same clock, the same cycle count, the same completion,
	// with the ramp mirrored. A yoyo that forgot to flip is a Restart loop wearing another name, and
	// it looks like a snap back at each cycle boundary rather than a return.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	PlayTween->SetLoopType(EDreamTweenLoop::Yoyo);
	PlayTween->SetLoopCount(2);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	TestTrue(TEXT("a quarter into the first cycle"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("progress runs forward"), Log.LastProgress(), 0.25f);

	TestTrue(TEXT("and on to the turn"), Advance(Tweener, 0.75f));
	TestEqual(TEXT("which is one whole cycle"), Log.CycleCounts.Num(), 1);
	TestEqual(TEXT("and not the end of the tween"), Log.Completes, 0);

	// The same quarter of a second on the far side of the turn reports the mirrored value.
	TestTrue(TEXT("a quarter into the second cycle"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("progress now runs backwards"), Log.LastProgress(), 0.75f);
	TestTrue(TEXT("and keeps running backwards"), Advance(Tweener, 0.5f));
	TestEqual(TEXT("towards the start"), Log.LastProgress(), 0.25f);

	TestFalse(TEXT("and the second cycle ends the tween"), Advance(Tweener, 0.25f));
	TestEqual(TEXT("with both cycles reported"), Log.CycleCounts.Num(), 2);
	TestEqual(TEXT("the second numbered two"), Log.CycleCounts[1], 2);
	TestEqual(TEXT("and one completion"), Log.Completes, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenPauseTest,
	"DreamGUI.PlayTween.PausingAPlayTweenHoldsItsClockUntilItResumes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenPauseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A paused tween has to keep its place rather than keep counting: the frames that pass while a
	// menu is up must not be spent, or every transition resumes near its end. It also has to stay
	// ALIVE -- returning "finished" while paused would have the manager collect it, and the resume
	// would then have nothing to resume.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	Advance(Tweener, 0.4f);
	TestEqual(TEXT("four tenths in"), Log.LastProgress(), 0.4f);
	const int32 UpdatesBeforePause = Log.Progress.Num();

	Tweener->Pause();
	TestTrue(TEXT("a paused tween is still alive"), Advance(Tweener, 0.4f));
	TestTrue(TEXT("and again"), Advance(Tweener, 0.4f));
	TestEqual(TEXT("but reports nothing while paused"), Log.Progress.Num(), UpdatesBeforePause);
	TestEqual(TEXT("and its clock did not move"), Tweener->GetElapsedTime(), 0.4f);

	Tweener->Resume();
	TestTrue(TEXT("resuming picks the clock back up"), Advance(Tweener, 0.4f));
	TestEqual(TEXT("from where it was paused, not from where it would have been"),
		Log.LastProgress(), 0.8f);
	TestEqual(TEXT("without restarting"), Log.Starts, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenStopTest,
	"DreamGUI.PlayTween.StoppingAPlayTweenEndsItWithoutFiringComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenStopTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Stop means abandoned, not finished. The distinction is the whole reason Stop passes false for
	// "call complete": a sequence chains on OnComplete, so a stop that fired it would hand control to
	// the next step of a sequence the caller just cancelled.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	FTweenLog Log;
	Log.Watch(PlayTween);
	PlayTween->Start();

	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	TestTrue(TEXT("it runs"), Advance(Tweener, 0.4f));
	TestEqual(TEXT("part way"), Log.LastProgress(), 0.4f);

	PlayTween->Stop();
	TestEqual(TEXT("stopping fires no completion"), Log.Completes, 0);
	TestFalse(TEXT("and the next frame retires the tween"), Advance(Tweener, 0.4f));
	TestEqual(TEXT("still with no completion"), Log.Completes, 0);
	TestEqual(TEXT("and no further progress"), Log.LastProgress(), 0.4f);

	// The handle is kept rather than cleared, which is what lets a caller ask what was playing after
	// stopping it -- and what makes a second Stop harmless.
	TestEqual(TEXT("the tweener handle is still there"), PlayTween->GetTweener(), Tweener);
	PlayTween->Stop();
	TestEqual(TEXT("and stopping twice changes nothing"), Log.Completes, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenComponentEmptyTest,
	"DreamGUI.PlayTween.ComponentsWithNothingAssignedPlayAndStopWithoutTouchingAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenComponentEmptyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Both components are added from a component picker, and both are empty for as long as it takes
	// the author to fill them in -- an Instanced object property starts null, and an Instanced array
	// starts as a row of nothing. Play-on-start means that empty state is reached at run time, not
	// only in the designer, so Play and Stop have to be no-ops rather than the first dereference.
	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Animated"));
	if (!TestNotNull(TEXT("the widget was constructed"), Widget))
	{
		return false;
	}

	UDreamUIPlayTweenComponent* Single = Cast<UDreamUIPlayTweenComponent>(
		Widget->AddComponent(UDreamUIPlayTweenComponent::StaticClass()));
	if (!TestNotNull(TEXT("the play tween component was added"), Single))
	{
		Widget->DestroyWidget();
		return false;
	}
	TestNull(TEXT("with no tween assigned yet"), Single->GetPlayTween());
	Single->Play();
	Single->Stop();
	TestNull(TEXT("and playing an empty one assigns nothing"), Single->GetPlayTween());

	UDreamUIPlayTweenSequenceComponent* Sequence = Cast<UDreamUIPlayTweenSequenceComponent>(
		Widget->AddComponent(UDreamUIPlayTweenSequenceComponent::StaticClass()));
	if (!TestNotNull(TEXT("the sequence component was added"), Sequence))
	{
		Widget->DestroyWidget();
		return false;
	}

	// An empty sequence must not report completion either. Firing it would tell whatever chained onto
	// the sequence that a run finished when none ever began.
	int32 SequenceCompletions = 0;
	Sequence->OnCompleteCPP.AddLambda([&SequenceCompletions] { SequenceCompletions++; });
	Sequence->Play();
	Sequence->Stop();
	Sequence->Stop();
	TestEqual(TEXT("an empty sequence reports no completion"), SequenceCompletions, 0);

	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenSequenceChainTest,
	"DreamGUI.PlayTween.ASequenceStartsOnlyItsFirstTweenAndIgnoresASecondPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenSequenceChainTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// The sequence exists to play tweens ONE AT A TIME, so the two things it must not do are start
	// more than the head of the list and restart itself while a run is in flight. The second is the
	// reachable one: Play is a Blueprint entry point, and a button that fires it twice would
	// otherwise reset the index under a chain that is still subscribed to the tween it was on.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(Fixture.World);
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Sequenced"));
	if (!TestNotNull(TEXT("the widget was constructed"), Widget))
	{
		return false;
	}
	UDreamUIPlayTweenSequenceComponent* Sequence = Cast<UDreamUIPlayTweenSequenceComponent>(
		Widget->AddComponent(UDreamUIPlayTweenSequenceComponent::StaticClass()));
	if (!TestNotNull(TEXT("the sequence component was added"), Sequence))
	{
		Widget->DestroyWidget();
		return false;
	}

	UDreamUIPlayTween_Float* First = NewObject<UDreamUIPlayTween_Float>(Sequence);
	UDreamUIPlayTween_Float* Second = NewObject<UDreamUIPlayTween_Float>(Sequence);
	First->SetDuration(1.0f);
	Second->SetDuration(1.0f);

	// There is no Blueprint path to build a sequence at run time, so the list is filled the way the
	// details panel fills it.
	if (!TestTrue(TEXT("the sequence's tween list can be authored"),
		SetSequenceTweens(Sequence, { First, Second })))
	{
		Widget->DestroyWidget();
		return false;
	}

	Sequence->Play();
	TestNotNull(TEXT("the head of the sequence is playing"), First->GetTweener());
	TestNull(TEXT("and the one behind it is not"), Second->GetTweener());

	// A second Play while the first is still running is ignored, rather than rewinding the chain.
	UDreamTweener* FirstTweener = First->GetTweener();
	Sequence->Play();
	TestEqual(TEXT("playing again does not restart the head"), First->GetTweener(), FirstTweener);
	TestNull(TEXT("nor jump ahead"), Second->GetTweener());

	// Running the head to its end hands over to the next one -- the whole point of the component.
	int32 Frames = 0;
	while (Advance(FirstTweener, 0.25f) && Frames < 32)
	{
		Frames++;
	}
	TestNotNull(TEXT("finishing the head starts the next tween"), Second->GetTweener());

	// And once a run has ended, Play is accepted again and starts from the head.
	UDreamTweener* SecondTweener = Second->GetTweener();
	Frames = 0;
	while (SecondTweener != nullptr && Advance(SecondTweener, 0.25f) && Frames < 32)
	{
		Frames++;
	}
	Sequence->Play();
	TestNotEqual(TEXT("a finished sequence can be played again from the head"),
		First->GetTweener(), FirstTweener);

	Sequence->Stop();
	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenParameterTypeTest,
	"DreamGUI.PlayTween.EveryPlayTweenTypeDeclaresAnEventMatchingTheValueItInterpolates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenParameterTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// A DreamUI event carries a declared parameter type, and FireEvent checks the value it is handed
	// against that declaration. A mismatch is not a compile error and not a crash: the event logs and
	// DOES NOT CALL the bound function, so a Blueprint wired to it silently never runs. That failure
	// mode is invisible until somebody binds something, which is why the declaration is worth a test
	// of its own -- a copy-pasted subclass that kept its neighbour's parameter type would look
	// perfect in the details panel and do nothing at run time.
	auto DeclaredType = [this](const UClass* InClass, const TCHAR* InPropertyName)
	{
		const FStructProperty* Property = CastField<FStructProperty>(InClass->FindPropertyByName(InPropertyName));
		if (Property == nullptr || Property->Struct != FDreamUIEventDelegate::StaticStruct())
		{
			AddError(FString::Printf(TEXT("%s has no %s event property"), *InClass->GetName(), InPropertyName));
			return EDreamUIEventDelegateParameterType::None;
		}
		const FDreamUIEventDelegate* Event =
			Property->ContainerPtrToValuePtr<FDreamUIEventDelegate>(InClass->GetDefaultObject());
		return Event->GetSupportParameterType();
	};

	struct FCase
	{
		const UClass* Class;
		EDreamUIEventDelegateParameterType Expected;
		const TCHAR* What;
	};
	const FCase Cases[] = {
		{ UDreamUIPlayTween_Float::StaticClass(), EDreamUIEventDelegateParameterType::Float, TEXT("float") },
		{ UDreamUIPlayTween_Double::StaticClass(), EDreamUIEventDelegateParameterType::Double, TEXT("double") },
		{ UDreamUIPlayTween_Int::StaticClass(), EDreamUIEventDelegateParameterType::Int32, TEXT("int") },
		{ UDreamUIPlayTween_Color::StaticClass(), EDreamUIEventDelegateParameterType::Color, TEXT("colour") },
		{ UDreamUIPlayTween_LinearColor::StaticClass(), EDreamUIEventDelegateParameterType::LinearColor, TEXT("linear colour") },
		{ UDreamUIPlayTween_Quaternion::StaticClass(), EDreamUIEventDelegateParameterType::Quaternion, TEXT("quaternion") },
		{ UDreamUIPlayTween_Rotator::StaticClass(), EDreamUIEventDelegateParameterType::Rotator, TEXT("rotator") },
		{ UDreamUIPlayTween_Vector2::StaticClass(), EDreamUIEventDelegateParameterType::Vector2, TEXT("vector2") },
		{ UDreamUIPlayTween_Vector3::StaticClass(), EDreamUIEventDelegateParameterType::Vector3, TEXT("vector3") },
		{ UDreamUIPlayTween_Vector4::StaticClass(), EDreamUIEventDelegateParameterType::Vector4, TEXT("vector4") },
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(*FString::Printf(TEXT("the %s tween fires a %s"), Case.What, Case.What),
			DeclaredType(Case.Class, TEXT("OnUpdateValue")), Case.Expected);
	}

	// The four events every play tween inherits. OnUpdateProgress is a float because progress is
	// linear on time whatever the ease is, and OnCycleComplete is an integer because it is meant to
	// carry the cycle number -- the same number the C++ and Blueprint multicast events deliver.
	const UClass* Base = UDreamUIPlayTween_Float::StaticClass();
	TestEqual(TEXT("start carries nothing"),
		DeclaredType(Base, TEXT("OnStart")), EDreamUIEventDelegateParameterType::Empty);
	TestEqual(TEXT("complete carries nothing"),
		DeclaredType(Base, TEXT("OnComplete")), EDreamUIEventDelegateParameterType::Empty);
	TestEqual(TEXT("update carries the progress"),
		DeclaredType(Base, TEXT("OnUpdateProgress")), EDreamUIEventDelegateParameterType::Float);
	TestEqual(TEXT("and cycle completion carries the cycle number"),
		DeclaredType(Base, TEXT("OnCycleComplete")), EDreamUIEventDelegateParameterType::Int32);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenEaseTest,
	"DreamGUI.PlayTween.TheAuthoredEaseShapesTheValueWhileProgressStaysLinearOnTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenEaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// The ease is the reason to author a tween instead of a lerp, and it shows up in exactly one place:
	// the VALUE. OnUpdateProgress reports the clock and is deliberately linear whatever the ease, so a
	// tween whose ease had been thrown away would look perfectly healthy from there -- which is how
	// every play tween in the project came to run linearly without anybody noticing. Reading both
	// halves at the same instant is what tells the two apart.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	// InQuad squares its input: half way through the TIME is a quarter of the way through the VALUE,
	// against a half for linear. Far enough apart that no tolerance question arises.
	FEventRecorder EasedValue(Fixture);
	FEventRecorder LinearValue(Fixture);

	UDreamUIPlayTween_Float* Eased = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	Eased->SetDuration(1.0f);
	Eased->SetEaseType(EDreamTweenEase::InQuad);
	UDreamUIPlayTween_Float* Straight = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	Straight->SetDuration(1.0f);
	Straight->SetEaseType(EDreamTweenEase::Linear);
	if (!TestTrue(TEXT("both tweens' value events take a binding"),
		BindAuthoredEvent(Eased, TEXT("OnUpdateValue"), EasedValue.Object, TEXT("SetDuration"),
			EDreamUIEventDelegateParameterType::Float)
		&& BindAuthoredEvent(Straight, TEXT("OnUpdateValue"), LinearValue.Object, TEXT("SetDuration"),
			EDreamUIEventDelegateParameterType::Float)))
	{
		return false;
	}

	FTweenLog EasedLog;
	EasedLog.Watch(Eased);
	Eased->Start();
	Straight->Start();
	if (!TestNotNull(TEXT("the eased tween started"), Eased->GetTweener())
		|| !TestNotNull(TEXT("and the linear one"), Straight->GetTweener()))
	{
		return false;
	}

	Advance(Eased->GetTweener(), 0.5f);
	Advance(Straight->GetTweener(), 0.5f);

	TestEqual(TEXT("half the duration has passed"), EasedLog.LastProgress(), 0.5f);
	TestEqual(TEXT("which puts a linear tween half way along its value"), LinearValue.ReceivedFloat(), 0.5f);
	TestEqual(TEXT("and an InQuad one a quarter of the way"), EasedValue.ReceivedFloat(), 0.25f);
	TestNotEqual(TEXT("so the authored ease really did survive to the value"),
		EasedValue.ReceivedFloat(), LinearValue.ReceivedFloat());

	// The eased value keeps its own shape for the rest of the run rather than only at the sample above.
	Advance(Eased->GetTweener(), 0.25f);
	TestEqual(TEXT("three quarters of the way through the time"), EasedLog.LastProgress(), 0.75f);
	TestEqual(TEXT("is nine sixteenths of the way through an InQuad value"),
		EasedValue.ReceivedFloat(), 0.5625f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenMissingCurveTest,
	"DreamGUI.PlayTween.ACurveFloatEaseWithNoCurveFallsBackToLinearAndComplainsExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenMissingCurveTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// CurveFloat is the one ease whose shape lives outside the enum, so it is the one an author can
	// half-finish: pick it in the details panel, leave the curve empty. Falling back to linear is the
	// documented answer and complaining is the useful one, but the complaint belongs to BUILDING the
	// tween, not to evaluating it -- the decision used to be re-made inside the interpolation function,
	// which meant one warning per frame per tween for as long as anything on screen was animating.
	// Pinning the count at one is the whole point of this test.
	AddExpectedMessagePlain(TEXT("CurveFloat not valid"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);

	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	FEventRecorder Value(Fixture);
	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	PlayTween->SetEaseType(EDreamTweenEase::CurveFloat);
	PlayTween->SetEaseCurve(nullptr);
	if (!TestTrue(TEXT("the value event takes a binding"),
		BindAuthoredEvent(PlayTween, TEXT("OnUpdateValue"), Value.Object, TEXT("SetDuration"),
			EDreamUIEventDelegateParameterType::Float)))
	{
		return false;
	}

	PlayTween->Start();
	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}

	// Three evaluations. Under the per-frame version this was three warnings, and the expectation
	// above is for one.
	Advance(Tweener, 0.25f);
	TestEqual(TEXT("a quarter of the way through the time is a quarter of the way through the value"),
		Value.ReceivedFloat(), 0.25f);
	Advance(Tweener, 0.25f);
	Advance(Tweener, 0.25f);
	TestEqual(TEXT("and three quarters is three quarters, which is what linear means"),
		Value.ReceivedFloat(), 0.75f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenCycleEventParameterTest,
	"DreamGUI.PlayTween.TheCycleCompleteEventCallsItsBindingsWithTheCycleNumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenCycleEventParameterTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// OnCycleComplete is declared to carry an integer, and a DreamUI event checks what it is handed
	// against that declaration before calling anything. Firing it with no value at all therefore did
	// not fail loudly -- it logged a type error and skipped every binding, so a designer's handler
	// simply never ran, and only ever on a looping tween. A binding is the only way to see the
	// difference: with nothing bound the event returns early and both versions look identical.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	FEventRecorder CycleNumber(Fixture);
	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(0.5f);
	PlayTween->SetLoopType(EDreamTweenLoop::Restart);
	PlayTween->SetLoopCount(3);
	if (!TestTrue(TEXT("the cycle event takes a binding"),
		BindAuthoredEvent(PlayTween, TEXT("OnCycleComplete"), CycleNumber.Object, TEXT("SetLoopCount"),
			EDreamUIEventDelegateParameterType::Int32)))
	{
		return false;
	}

	PlayTween->Start();
	UDreamTweener* Tweener = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), Tweener))
	{
		return false;
	}
	TestEqual(TEXT("nothing has been reported before the first cycle ends"),
		CycleNumber.ReceivedInt(), FEventRecorder::Nothing);

	TestTrue(TEXT("the first cycle ends without ending the tween"), Advance(Tweener, 0.5f));
	TestEqual(TEXT("and the binding was called with the cycle number"), CycleNumber.ReceivedInt(), 1);
	TestTrue(TEXT("the second likewise"), Advance(Tweener, 0.5f));
	TestEqual(TEXT("counting upwards"), CycleNumber.ReceivedInt(), 2);
	TestFalse(TEXT("and the third ends the tween"), Advance(Tweener, 0.5f));
	TestEqual(TEXT("having reported the last cycle as well"), CycleNumber.ReceivedInt(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenRestartTest,
	"DreamGUI.PlayTween.StartingAgainRetiresTheTweenTheLastStartLeftRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenRestartTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Start is reachable twice over from a button, a state change, or a sequence replaying, and the
	// play tween keeps ONE handle. A second Start that only overwrote that handle left the first tween
	// alive in the manager, still broadcasting this play tween's events over the top of the new run and
	// beyond the reach of Stop, which can only kill what the handle points at. There is no visible
	// symptom except animation that will not stop and events that arrive twice.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamUIPlayTween_Float* PlayTween = Fixture.MakePlayTween<UDreamUIPlayTween_Float>();
	PlayTween->SetDuration(1.0f);
	FTweenLog Log;
	Log.Watch(PlayTween);

	PlayTween->Start();
	UDreamTweener* First = PlayTween->GetTweener();
	if (!TestNotNull(TEXT("Start creates a tweener"), First))
	{
		return false;
	}
	TestTrue(TEXT("it runs"), Advance(First, 0.4f));
	TestEqual(TEXT("part way"), Log.LastProgress(), 0.4f);

	PlayTween->Start();
	UDreamTweener* Second = PlayTween->GetTweener();
	TestNotEqual(TEXT("starting again builds a second tweener"), Second, First);

	const int32 UpdatesBeforeRestart = Log.Progress.Num();
	TestFalse(TEXT("and retires the first one on its next frame"), Advance(First, 0.4f));
	TestEqual(TEXT("so the abandoned tween reports no further progress"),
		Log.Progress.Num(), UpdatesBeforeRestart);
	TestEqual(TEXT("nor a completion it never reached"), Log.Completes, 0);

	// What is left is one tween, running from the beginning, and reachable by Stop.
	TestTrue(TEXT("the new tween runs"), Advance(Second, 0.4f));
	TestEqual(TEXT("from the beginning, not from where the abandoned one had got to"),
		Log.LastProgress(), 0.4f);
	TestEqual(TEXT("having started over"), Log.Starts, 2);

	PlayTween->Stop();
	TestFalse(TEXT("and Stop reaches it"), Advance(Second, 0.4f));
	TestEqual(TEXT("without a completion, because a stop is not a finish"), Log.Completes, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenSequenceReleaseTest,
	"DreamGUI.PlayTween.AFinishedSequenceLetsGoOfEveryTweenItChainedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenSequenceReleaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// The sequence advances itself by subscribing to whichever completion event the authored flag
	// selects, and it must let go of the same event it took. Removing the handle from the OTHER one
	// removed nothing, silently: the subscription outlived the run, so anybody who later played that
	// tween on its own -- a button, another component, a second sequence -- pushed this sequence's
	// index along and made it announce a completion for a run that was over. Replaying stacked one
	// more of them on each pass.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(Fixture.World);
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Sequenced"));
	if (!TestNotNull(TEXT("the widget was constructed"), Widget))
	{
		return false;
	}
	UDreamUIPlayTweenSequenceComponent* Sequence = Cast<UDreamUIPlayTweenSequenceComponent>(
		Widget->AddComponent(UDreamUIPlayTweenSequenceComponent::StaticClass()));
	if (!TestNotNull(TEXT("the sequence component was added"), Sequence))
	{
		Widget->DestroyWidget();
		return false;
	}

	UDreamUIPlayTween_Float* First = NewObject<UDreamUIPlayTween_Float>(Sequence);
	UDreamUIPlayTween_Float* Second = NewObject<UDreamUIPlayTween_Float>(Sequence);
	First->SetDuration(1.0f);
	Second->SetDuration(1.0f);
	if (!TestTrue(TEXT("the sequence's tween list can be authored"),
		SetSequenceTweens(Sequence, { First, Second })))
	{
		Widget->DestroyWidget();
		return false;
	}

	int32 SequenceCompletions = 0;
	Sequence->OnCompleteCPP.AddLambda([&SequenceCompletions] { SequenceCompletions++; });

	Sequence->Play();
	RunToEnd(First->GetTweener());
	RunToEnd(Second->GetTweener());
	TestEqual(TEXT("running the chain to its end reports one completion"), SequenceCompletions, 1);

	// Now the head is played by somebody who is not the sequence, which is the reachable half of this:
	// a play tween is a shared object and Start is a Blueprint entry point on it.
	UDreamTweener* SecondTweenerFromTheRun = Second->GetTweener();
	First->Start();
	RunToEnd(First->GetTweener());
	TestEqual(TEXT("and finishing a tween the sequence has let go of does not report another"),
		SequenceCompletions, 1);
	TestEqual(TEXT("nor drag the sequence back into the tween behind it"),
		Second->GetTweener(), SecondTweenerFromTheRun);

	// Replaying is the other half: a second run must subscribe once more, not once more ON TOP.
	Sequence->Play();
	RunToEnd(First->GetTweener());
	RunToEnd(Second->GetTweener());
	TestEqual(TEXT("a replayed sequence reports exactly one more completion"), SequenceCompletions, 2);

	First->Start();
	RunToEnd(First->GetTweener());
	TestEqual(TEXT("with no subscription left over from either run"), SequenceCompletions, 2);

	Sequence->Stop();
	Widget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPlayTweenSequenceHolesTest,
	"DreamGUI.PlayTween.ASequenceWithUnfilledRowsPlaysTheTweensItActuallyHas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPlayTweenSequenceHolesTest::RunTest(const FString& Parameters)
{
	using namespace DreamPlayTweenTestLocal;

	// Pressing "+" on an Instanced array gives you a row holding nothing, and the class for it is
	// chosen afterwards -- so a list with holes in it is an ordinary authoring state, not a corrupt
	// asset. Play-on-start is the default, which means that state is reached at RUN time by a prefab
	// somebody was halfway through editing, and the first thing the sequence used to do with it was
	// dereference the hole.
	FScopedTweenWorld Fixture;
	if (!TestNotNull(TEXT("a tween manager exists behind the game instance"), Fixture.Manager()))
	{
		return false;
	}

	UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(Fixture.World);
	UDreamWidget* Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("HalfAuthored"));
	if (!TestNotNull(TEXT("the widget was constructed"), Widget))
	{
		return false;
	}
	UDreamUIPlayTweenSequenceComponent* Sequence = Cast<UDreamUIPlayTweenSequenceComponent>(
		Widget->AddComponent(UDreamUIPlayTweenSequenceComponent::StaticClass()));
	if (!TestNotNull(TEXT("the sequence component was added"), Sequence))
	{
		Widget->DestroyWidget();
		return false;
	}

	UDreamUIPlayTween_Float* First = NewObject<UDreamUIPlayTween_Float>(Sequence);
	UDreamUIPlayTween_Float* Second = NewObject<UDreamUIPlayTween_Float>(Sequence);
	First->SetDuration(1.0f);
	Second->SetDuration(1.0f);

	// Holes in front, between and behind: the leading one is what Play dereferences, the middle one is
	// what the hand-over dereferences, and the trailing one is what makes "past the end" and "past the
	// last real tween" different questions.
	if (!TestTrue(TEXT("a list with holes in it can be authored"),
		SetSequenceTweens(Sequence, { nullptr, First, nullptr, Second, nullptr })))
	{
		Widget->DestroyWidget();
		return false;
	}

	int32 SequenceCompletions = 0;
	Sequence->OnCompleteCPP.AddLambda([&SequenceCompletions] { SequenceCompletions++; });

	Sequence->Play();
	TestNotNull(TEXT("the sequence skips the leading hole and plays the first tween it has"),
		First->GetTweener());
	TestNull(TEXT("still one at a time"), Second->GetTweener());

	RunToEnd(First->GetTweener());
	TestNotNull(TEXT("and steps over the hole between them"), Second->GetTweener());
	TestEqual(TEXT("without having finished yet"), SequenceCompletions, 0);

	RunToEnd(Second->GetTweener());
	TestEqual(TEXT("finishing after the last real tween rather than after the last row"),
		SequenceCompletions, 1);

	// A list that is ALL holes is what a component looks like a second after it was added. It has to
	// play nothing and report nothing -- announcing a completion would tell whatever chained onto this
	// sequence that a run had finished when none ever began.
	const int32 CompletionsBeforeEmptyPlay = SequenceCompletions;
	if (!TestTrue(TEXT("a list of nothing but holes can be authored"),
		SetSequenceTweens(Sequence, { nullptr, nullptr })))
	{
		Widget->DestroyWidget();
		return false;
	}
	Sequence->Play();
	Sequence->Stop();
	TestEqual(TEXT("and playing it does nothing at all"),
		SequenceCompletions, CompletionsBeforeEmptyPlay);

	Widget->DestroyWidget();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
