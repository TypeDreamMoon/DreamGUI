// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DreamTweener.h"
#include "DreamTweenerSequence.h"
#include "Tweener/DreamTweenerFloat.h"
#include "Tweener/DreamTweenerInteger.h"
#include "Tweener/DreamTweenerQuaternion.h"
#include "UObject/Package.h"

/*
 * The tweener's own clock: restarting, seeking, killing, and the arithmetic each value type does at
 * a cycle boundary. None of this needs a world or a manager -- a tweener is stepped by handing it an
 * elapsed time -- which is exactly why none of it had ever been covered.
 */
namespace DreamTweenerTestLocal
{
	UDreamTweenerFloat* MakeFloatTween(float& InOutValue, float InEndValue, float InDuration)
	{
		UDreamTweenerFloat* Tween = NewObject<UDreamTweenerFloat>(GetTransientPackage());
		Tween->SetInitialValue(
			FDreamTweenFloatGetterFunction::CreateLambda([&InOutValue] { return InOutValue; }),
			FDreamTweenFloatSetterFunction::CreateLambda([&InOutValue](float NewValue) { InOutValue = NewValue; }),
			InEndValue, InDuration);
		return Tween;
	}

	UDreamTweenerInteger* MakeIntTween(int32& InOutValue, int32 InEndValue, float InDuration)
	{
		UDreamTweenerInteger* Tween = NewObject<UDreamTweenerInteger>(GetTransientPackage());
		Tween->SetInitialValue(
			FDreamTweenIntGetterFunction::CreateLambda([&InOutValue] { return static_cast<int>(InOutValue); }),
			FDreamTweenIntSetterFunction::CreateLambda([&InOutValue](int NewValue) { InOutValue = NewValue; }),
			InEndValue, InDuration);
		return Tween;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenIntegerRestartTest,
	"DreamGUI.Tween.Tweener.AnIntegerTweenRestartedRunsTheSameNumbersItRanTheFirstTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenIntegerRestartTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	int32 Value = 0;
	UDreamTweenerInteger* Tween = MakeIntTween(Value, 100, 1.0f);

	Tween->ToNextWithElapsedTime(0.5f);
	const int32 HalfwayFirstTime = Value;
	TestTrue(TEXT("the tween moved the value on its way through"), HalfwayFirstTime > 0 && HalfwayFirstTime < 100);

	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("it finished on the end value"), Value, 100);

	// The restart used to set the start value to GFrameNumber -- copied from the frame tweener, whose
	// "value" IS a frame number -- so the second run began at a six-digit number that differed every
	// time the test was run.
	Tween->Restart();
	Tween->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("halfway through the restarted run is halfway through the original run"), Value, HalfwayFirstTime);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenGotoWithDelayTest,
	"DreamGUI.Tween.Tweener.GotoLandsAtTheTimePointItWasGivenEvenWhenTheTweenHasADelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenGotoWithDelayTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetDelay(2.0f);

	// timePoint is a position in the ANIMATION. Handed straight to the clock, which counts the delay
	// first, it landed at timePoint - delay: here, two seconds before the animation even begins.
	Tween->Goto(0.5f);
	TestEqual(TEXT("halfway through the animation, not halfway through the delay"), Value, 50.0f, 0.01f);

	Tween->Goto(1.0f);
	TestEqual(TEXT("the end of the animation is reachable at all"), Value, 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenRestartStartsAgainTest,
	"DreamGUI.Tween.Tweener.ARestartedTweenStartsAgainInsteadOfSilentlyResuming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenRestartStartsAgainTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);

	int32 StartCount = 0;
	int32 CycleStartCount = 0;
	Tween->OnStart(TFunction<void()>([&StartCount] { StartCount++; }));
	Tween->OnCycleStart(TFunction<void()>([&CycleStartCount] { CycleStartCount++; }));

	Tween->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("starting is announced once"), StartCount, 1);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("it finished on the end value"), Value, 100.0f, 0.01f);

	Tween->Restart();
	// Restarting put the value back at the beginning; the flag that says "already started" used to
	// stay raised, so neither callback ever fired a second time and the start value was never re-read.
	TestEqual(TEXT("the value is back where the tween began"), Value, 0.0f, 0.01f);

	Tween->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("starting again is announced again"), StartCount, 2);
	TestEqual(TEXT("and so is the cycle"), CycleStartCount, 2);
	TestEqual(TEXT("and it runs from the beginning, not from the end"), Value, 50.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenRestartFromCompletionTest,
	"DreamGUI.Tween.Tweener.RestartingFromACompletionHandlerSurvivesTheKillThatDeliveredIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenRestartFromCompletionTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);

	bool bRestartedOnce = false;
	Tween->OnComplete(TFunction<void()>([&bRestartedOnce, Tween]
	{
		if (!bRestartedOnce)
		{
			bRestartedOnce = true;
			Tween->Restart();
		}
	}));

	Tween->ToNextWithElapsedTime(0.5f);
	// Kill used to run the completion handler and THEN raise the flag, so the restart the handler
	// asked for was undone on the line after it -- and nothing anywhere said so.
	Tween->Kill(/*callComplete*/true);

	TestTrue(TEXT("the completion handler ran"), bRestartedOnce);
	TestFalse(TEXT("the tween the handler restarted is not still marked for death"), Tween->IsMarkedToKill());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenEveryListenerIsCalledTest,
	"DreamGUI.Tween.Tweener.EveryListenerBoundToATweenIsCalledAndNotOnlyTheLastOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenEveryListenerIsCalledTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);

	// The first of these two is the shape a system binds for its own bookkeeping (UDreamUIPlayTween
	// binds four such callbacks); the second is an author's. With one delegate per event the author's
	// silently replaced the system's, and the system simply stopped hearing about its own tween.
	int32 SystemCalls = 0;
	int32 AuthorCalls = 0;
	Tween->OnComplete(TFunction<void()>([&SystemCalls] { SystemCalls++; }));
	Tween->OnComplete(TFunction<void()>([&AuthorCalls] { AuthorCalls++; }));

	Tween->ToNextWithElapsedTime(0.5f);
	Tween->ToNextWithElapsedTime(1.0f);

	TestEqual(TEXT("the first listener heard the completion"), SystemCalls, 1);
	TestEqual(TEXT("so did the second"), AuthorCalls, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenQuaternionStaysUnitTest,
	"DreamGUI.Tween.Tweener.AQuaternionTweenKeepsItsEndpointsUnitLengthAcrossLoopsAndRestarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenQuaternionStaysUnitTest::RunTest(const FString& Parameters)
{
	FQuat Value = FQuat::Identity;
	UDreamTweenerQuaternion* Tween = NewObject<UDreamTweenerQuaternion>(GetTransientPackage());
	Tween->SetInitialValue(
		FDreamTweenQuaternionGetterFunction::CreateLambda([&Value] { return Value; }),
		FDreamTweenQuaternionSetterFunction::CreateLambda([&Value](const FQuat& NewValue) { Value = NewValue; }),
		FRotator(0.0f, 170.0f, 0.0f).Quaternion(), 1.0f);
	Tween->SetLoop(EDreamTweenLoop::Incremental, 3);

	Tween->ToNextWithElapsedTime(0.5f);
	// One cycle boundary. Adding and subtracting quaternion COMPONENTS -- which is what the vector
	// tweeners do, and what this was copied from -- leaves something that is no longer a rotation:
	// two near-opposite rotations subtract to nearly zero, and normalising that is a NaN.
	Tween->ToNextWithElapsedTime(1.0f);
	TestTrue(TEXT("the next cycle's start is still a rotation"), Tween->startValue.IsNormalized());
	TestTrue(TEXT("and so is its end"), Tween->endValue.IsNormalized());

	Tween->ToNextWithElapsedTime(1.5f);
	TestTrue(TEXT("the value driven from them is a rotation too"), Value.IsNormalized());

	Tween->Restart();
	TestTrue(TEXT("restoring the original start leaves a rotation"), Tween->startValue.IsNormalized());
	TestTrue(TEXT("and re-applying the same relative turn leaves one"), Tween->endValue.IsNormalized());
	// Restarting with the start value unchanged has to put the end back exactly where it was; only
	// quaternion algebra does that, component arithmetic only manages it about a single axis.
	TestTrue(TEXT("the restarted end is the end the tween was authored with"),
		Tween->endValue.Equals(FRotator(0.0f, 170.0f, 0.0f).Quaternion(), 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenForceCompleteReversedTest,
	"DreamGUI.Tween.Tweener.ForceCompleteOnACycleRunningBackwardsLandsAtTheEndItWasHeadingFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenForceCompleteReversedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetLoop(EDreamTweenLoop::Yoyo, 2);

	Tween->ToNextWithElapsedTime(0.5f);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("the forward cycle ended at the far end"), Value, 100.0f, 0.01f);

	// The second cycle runs backwards, so its end is time zero. Completing it used to jump the value
	// to the duration end -- the end it had just left.
	Tween->ForceComplete();
	TestEqual(TEXT("completing the backward cycle lands where it was going"), Value, 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenSequenceAdoptsRunningTweenTest,
	"DreamGUI.Tween.Sequence.ATweenThatHasAlreadyStartedStillTakesThePositionTheSequenceGivesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceAdoptsRunningTweenTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Child = MakeFloatTween(Value, 100.0f, 1.0f);
	Child->SetEase(EDreamTweenEase::Linear);

	// A tween created by the manager is in its list and ticking from the moment it exists, so a graph
	// that builds its tweens one frame and assembles them the next hands over a tween that has run.
	Child->ToNextWithElapsedTime(0.25f);
	TestTrue(TEXT("the child had begun running on its own"), Value > 0.0f);
	Value = 0.0f;

	UDreamTweenerSequence* Sequence = NewObject<UDreamTweenerSequence>(GetTransientPackage());
	// Positioned with a plain assignment rather than SetDelay, which refuses -- silently, returning
	// the tween -- for anything that has already started. Every such child used to keep delay 0 and
	// play on top of the others at the head of the sequence.
	Sequence->Insert(nullptr, 2.0f, Child);

	Sequence->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("before its position in the sequence the child has not moved"), Value, 0.0f, 0.01f);

	Sequence->ToNextWithElapsedTime(2.5f);
	TestEqual(TEXT("halfway past its position it is halfway through"), Value, 50.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenAutoKillTest,
	"DreamGUI.Tween.Tweener.ATweenToldNotToAutoKillIsStillThereToBeRestarted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenAutoKillTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetAutoKill(false);

	// ToNext rather than ToNextWithElapsedTime: "is this tween still alive" is what ToNext answers,
	// and it is the manager's only cue to retire one.
	TestTrue(TEXT("running"), Tween->ToNext(0.5f, 0.5f));
	TestTrue(TEXT("still running at its end, because it is not to be killed"), Tween->ToNext(0.6f, 0.6f));
	TestEqual(TEXT("and it finished on the end value"), Value, 100.0f, 0.01f);
	TestTrue(TEXT("a held tween reports itself as still there on later ticks too"), Tween->ToNext(1.0f, 1.0f));

	// And it is restartable, which is the whole point of keeping it.
	Tween->Restart();
	TestEqual(TEXT("restarting a held tween puts the value back"), Value, 0.0f, 0.01f);
	Tween->ToNext(0.5f, 0.5f);
	TestEqual(TEXT("and runs it again"), Value, 50.0f, 0.01f);

	// Kill still ends it, whatever auto-kill says.
	Tween->Kill();
	TestFalse(TEXT("killing a held tween ends it"), Tween->ToNext(0.1f, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenFromTest,
	"DreamGUI.Tween.Tweener.AFromTweenRunsFromTheTargetBackToWhereTheValueIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenFromTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 10.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 110.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetFrom();

	Tween->ToNextWithElapsedTime(0.0001f);
	TestEqual(TEXT("it begins at the value it was authored to end at"), Value, 110.0f, 0.05f);
	Tween->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("halfway is halfway back"), Value, 60.0f, 0.01f);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("and it ends where the value was when it started"), Value, 10.0f, 0.01f);

	// A restart must not turn it around again: after the swap, the tween's own start and end ARE the
	// from-configuration, and restoring them is all a restart has to do.
	Tween->Restart();
	TestEqual(TEXT("restarting puts it back at the target end"), Value, 110.0f, 0.01f);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("and it runs back down again, not up"), Value, 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenSpeedBasedTest,
	"DreamGUI.Tween.Tweener.ASpeedBasedTweenTakesLongerTheFurtherItHasToGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSpeedBasedTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	// duration is read as a SPEED: 100 units a second. The near travels 100 units, the far 300, so
	// the far one must take three times as long -- which a plain duration cannot express at all.
	float Near = 0.0f;
	UDreamTweenerFloat* NearTween = MakeFloatTween(Near, 100.0f, 100.0f);
	NearTween->SetEase(EDreamTweenEase::Linear);
	NearTween->SetSpeedBased();
	NearTween->ToNextWithElapsedTime(0.0001f);
	TestEqual(TEXT("a hundred units at a hundred a second is one second"), NearTween->GetDuration(), 1.0f, 0.001f);

	float Far = 0.0f;
	UDreamTweenerFloat* FarTween = MakeFloatTween(Far, 300.0f, 100.0f);
	FarTween->SetEase(EDreamTweenEase::Linear);
	FarTween->SetSpeedBased();
	FarTween->ToNextWithElapsedTime(0.0001f);
	TestEqual(TEXT("three hundred units at the same speed is three seconds"), FarTween->GetDuration(), 3.0f, 0.001f);

	FarTween->ToNextWithElapsedTime(1.5f);
	TestEqual(TEXT("and halfway through those three seconds it is halfway there"), Far, 150.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenTimeScaleTest,
	"DreamGUI.Tween.Tweener.ATimeScaleOfTwoRunsTheTweenTwiceAsFast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenTimeScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetTimeScale(2.0f);

	Tween->ToNext(0.25f, 0.25f);
	TestEqual(TEXT("a quarter of a second at double speed is half the tween"), Value, 50.0f, 0.01f);

	// Changeable mid-flight, unlike the setters that describe the tween's shape.
	Tween->SetTimeScale(0.0f);
	Tween->ToNext(0.25f, 0.25f);
	TestEqual(TEXT("a time scale of zero holds it still"), Value, 50.0f, 0.01f);

	Tween->SetTimeScale(1.0f);
	Tween->ToNext(0.25f, 0.25f);
	TestEqual(TEXT("and putting it back moves it on from where it stopped"), Value, 75.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenSequenceCallbackTest,
	"DreamGUI.Tween.Sequence.ACallbackInsertedIntoASequenceFiresWhenTheSequenceReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceCallbackTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Child = MakeFloatTween(Value, 100.0f, 1.0f);
	Child->SetEase(EDreamTweenEase::Linear);

	UDreamTweenerSequence* Sequence = NewObject<UDreamTweenerSequence>(GetTransientPackage());
	Sequence->Append(nullptr, Child);

	int32 Calls = 0;
	// At the end of what the sequence holds so far -- one second, the child's own length.
	Sequence->AppendCallback(TFunction<void()>([&Calls] { Calls++; }));

	Sequence->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("halfway through the tween the callback has not fired"), Calls, 0);
	Sequence->ToNextWithElapsedTime(1.01f);
	TestEqual(TEXT("past the end of the tween it has"), Calls, 1);
	Sequence->ToNextWithElapsedTime(1.5f);
	TestEqual(TEXT("and only once"), Calls, 1);
	return true;
}

#endif
