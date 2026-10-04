// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "DreamTweener.h"
#include "DreamTweenerSequence.h"
#include "Tweener/DreamTweenerColor.h"
#include "Tweener/DreamTweenerFloat.h"
#include "Tweener/DreamTweenerInteger.h"
#include "Tweener/DreamTweenerQuaternion.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include <limits>

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

	UDreamTweenerColor* MakeColorTween(FColor& InOutValue, const FColor& InEndValue, float InDuration)
	{
		UDreamTweenerColor* Tween = NewObject<UDreamTweenerColor>(GetTransientPackage());
		Tween->SetInitialValue(
			FDreamTweenColorGetterFunction::CreateLambda([&InOutValue] { return InOutValue; }),
			FDreamTweenColorSetterFunction::CreateLambda([&InOutValue](FColor NewValue) { InOutValue = NewValue; }),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenKillOnceTest,
	"DreamGUI.Tween.Tweener.ATweenIsKilledOnceEvenByKillHandlersThatKillEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenKillOnceTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float ValueA = 0.0f;
	float ValueB = 0.0f;
	UDreamTweenerFloat* TweenA = MakeFloatTween(ValueA, 1.0f, 1.0f);
	UDreamTweenerFloat* TweenB = MakeFloatTween(ValueB, 1.0f, 1.0f);

	// "A's OnKill kills B, B's OnKill kills A": a killed tween stays listed until the next tick, so Kill used to
	// announce itself again on every call and the two handlers bounced until the stack ran out. The cap is
	// only there so the old behaviour fails the counts below instead of taking the run down.
	constexpr int32 Cap = 8;
	int32 KillsA = 0;
	int32 KillsB = 0;
	int32 CompletesA = 0;
	TweenA->OnKill(TFunction<void()>([&KillsA, TweenB] { if (++KillsA < Cap) { TweenB->Kill(); } }));
	TweenB->OnKill(TFunction<void()>([&KillsB, TweenA] { if (++KillsB < Cap) { TweenA->Kill(true); } }));
	TweenA->OnComplete(TFunction<void()>([&CompletesA] { CompletesA++; }));

	TweenA->ToNextWithElapsedTime(0.5f);
	TweenA->Kill();
	TestEqual(TEXT("A's kill is announced once"), KillsA, 1);
	TestEqual(TEXT("so is B's, which A's handler set off"), KillsB, 1);
	TestEqual(TEXT("and B's handler killing A back completes nothing: A was already killed"), CompletesA, 0);

	TweenA->Kill(true);
	TestEqual(TEXT("killing A again announces nothing more"), KillsA, 1);
	TestEqual(TEXT("and completes nothing either"), CompletesA, 0);
	TestFalse(TEXT("and it is finished"), TweenA->ToNext(0.1f, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenEmptyRuntimeCurveTest,
	"DreamGUI.Tween.Tweener.AnEmptyRuntimeCurveEasesLinearlyToTheEndInsteadOfHoldingTheStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenEmptyRuntimeCurveTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	// What a layout animation set to CurveFloat hands over when nobody drew its curve: an FRuntimeFloatCurve with
	// no keys, which evaluates to 0 everywhere -- the animated child stayed at its old rect for good.
	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::CurveFloat);
	AddExpectedMessagePlain(TEXT("with no keys"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Tween->SetRuntimeFloatCurve(FRuntimeFloatCurve());

	Tween->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("halfway is halfway: the linear fallback"), Value, 50.0f, 0.01f);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("and the end value is reached"), Value, 100.0f, 0.01f);

	// A curve that has keys is still the curve: this one ends halfway.
	float Curved = 0.0f;
	UDreamTweenerFloat* CurvedTween = MakeFloatTween(Curved, 100.0f, 1.0f);
	CurvedTween->SetEase(EDreamTweenEase::CurveFloat);
	FRuntimeFloatCurve Keyed;
	Keyed.GetRichCurve()->AddKey(0.0f, 0.0f);
	Keyed.GetRichCurve()->AddKey(1.0f, 0.5f);
	CurvedTween->SetRuntimeFloatCurve(Keyed);
	CurvedTween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("a curve with keys is followed to where it ends"), Curved, 50.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenColorOvershootTest,
	"DreamGUI.Tween.Tweener.AColourTweenThatOvershootsHoldsAtTheEndOfTheRangeInsteadOfWrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenColorOvershootTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	// OutBack peaks at 1.1 a little past halfway: 0 to 255 reaches 280.5 there, which the byte lerp wrapped to
	// 24 -- a white that flashed to near black for a moment.
	FColor Value(0, 0, 0, 0);
	UDreamTweenerColor* Tween = MakeColorTween(Value, FColor(255, 255, 255, 255), 1.0f);
	Tween->SetEase(EDreamTweenEase::OutBack);
	Tween->ToNextWithElapsedTime(0.58f);
	TestTrue(FString::Printf(TEXT("past the end of the range it holds at the top (R = %d)"), Value.R), Value.R >= 250);
	TestTrue(FString::Printf(TEXT("as does every channel (A = %d)"), Value.A), Value.A >= 250);
	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("and it ends on the end value"), Value, FColor(255, 255, 255, 255));

	// And below the bottom: InBack dips under 0 early on, which wrapped up towards 255.
	FColor Dipped(0, 0, 0, 0);
	UDreamTweenerColor* DipTween = MakeColorTween(Dipped, FColor(255, 255, 255, 255), 1.0f);
	DipTween->SetEase(EDreamTweenEase::InBack);
	DipTween->ToNextWithElapsedTime(0.3f);
	TestEqual(FString::Printf(TEXT("below the start of the range it holds at the bottom (R = %d)"), Dipped.R), static_cast<int32>(Dipped.R), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenEaseEndpointsTest,
	"DreamGUI.Tween.Tweener.EveryEaseStartsExactlyAtItsStartAndEndsExactlyAtItsEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenEaseEndpointsTest::RunTest(const FString& Parameters)
{
	// Whatever a curve does in between, f(0) is the start and f(d) the end, or a tween stops short of the value it
	// was sent to. InExpo used to end a thousandth of the change early: a 1920 unit slide two units short.
	const UEnum* EaseEnum = StaticEnum<EDreamTweenEase>();
	const float Change = 100.0f;
	const float Start = 10.0f;
	for (int32 EaseIndex = 0; EaseIndex < static_cast<int32>(EDreamTweenEase::CurveFloat); ++EaseIndex)
	{
		const EDreamTweenEase Ease = static_cast<EDreamTweenEase>(EaseIndex);
		const FString EaseName = EaseEnum != nullptr ? EaseEnum->GetNameStringByValue(EaseIndex) : FString::FromInt(EaseIndex);
		const FDreamTweenFunction Function = UDreamTweener::GetEaseFunction(Ease);
		if (!TestTrue(FString::Printf(TEXT("%s has a function"), *EaseName), Function.IsBound()))
		{
			continue;
		}
		for (const float Duration : { 1.0f, 0.3f, 2.5f })
		{
			TestEqual(FString::Printf(TEXT("%s starts at its start over %.1f s"), *EaseName, Duration),
				Function.Execute(Change, Start, 0.0f, Duration), Start, 0.01f);
			TestEqual(FString::Printf(TEXT("%s ends at its end over %.1f s"), *EaseName, Duration),
				Function.Execute(Change, Start, Duration, Duration), Start + Change, 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenBackwardYoyoEndTest,
	"DreamGUI.Tween.Tweener.ABackwardYoyoCycleEndsAtProgressZeroWhereItsValueIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenBackwardYoyoEndTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetLoop(EDreamTweenLoop::Yoyo, 2);
	TArray<float> Progress;
	Tween->OnUpdate(TFunction<void(float)>([&Progress](float InProgress) { Progress.Add(InProgress); }));

	Tween->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("the forward cycle ends at progress 1"), Progress.Num() > 0 ? Progress.Last() : -1.0f, 1.0f);
	Tween->ToNextWithElapsedTime(1.75f);
	TestEqual(TEXT("three quarters into the backward cycle the progress is down to a quarter"), Progress.Num() > 0 ? Progress.Last() : -1.0f, 0.25f, 0.001f);
	// It used to report 1 here, jumping back to the far end on the frame the value reached the near one.
	Tween->ToNextWithElapsedTime(2.0f);
	TestEqual(TEXT("and the backward cycle ends at progress 0"), Progress.Num() > 0 ? Progress.Last() : -1.0f, 0.0f);
	TestEqual(TEXT("which is where its value is"), Value, 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenTimingValidationTest,
	"DreamGUI.Tween.Tweener.ANegativeOrNotANumberClockSettingNeverRunsTheTweenBackwardsOrForever",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenTimingValidationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;
	const float NotANumber = std::numeric_limits<float>::quiet_NaN();

	float Value = 0.0f;
	UDreamTweenerFloat* Tween = MakeFloatTween(Value, 100.0f, 1.0f);
	Tween->SetEase(EDreamTweenEase::Linear);
	Tween->SetTimeScale(2.0f);
	AddExpectedMessagePlain(TEXT("time scale that is not a number"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Tween->SetTimeScale(NotANumber);
	TestEqual(TEXT("a time scale that is not a number is refused"), Tween->GetTimeScale(), 2.0f);
	AddExpectedMessagePlain(TEXT("negative time scale"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Tween->SetTimeScale(-1.0f);
	TestEqual(TEXT("a negative one holds the tween still instead of running it backwards"), Tween->GetTimeScale(), 0.0f);
	Tween->SetTimeScale(1.0f);

	// A delay that is not a number was never waited out: every comparison against it is false.
	AddExpectedMessagePlain(TEXT("delay that is not a number"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Tween->SetDelay(NotANumber);
	Tween->ToNext(0.5f, 0.5f);
	TestEqual(TEXT("so it starts without one"), Value, 50.0f, 0.01f);
	// And a step that is not a number moves nothing, rather than writing NaN into the value.
	Tween->ToNextWithElapsedTime(NotANumber);
	TestEqual(TEXT("a clock that is not a number leaves the value where it was"), Value, 50.0f, 0.01f);
	TestTrue(TEXT("and it is still a number"), FMath::IsFinite(Value));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenSequenceCollectedChildTest,
	"DreamGUI.Tween.Sequence.AChildCollectedUnderARunningSequenceIsDroppedInsteadOfStepped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceCollectedChildTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	float Kept = 0.0f;
	float Lost = 0.0f;
	UDreamTweenerSequence* Sequence = NewObject<UDreamTweenerSequence>(GetTransientPackage());
	// Rooted, as a sequence held by something persistent is: the collection below has to take the child and leave
	// the sequence, the way streaming out the level the child's target lived in does.
	Sequence->AddToRoot();
	ON_SCOPE_EXIT
	{
		Sequence->RemoveFromRoot();
	};
	UDreamTweenerFloat* KeptChild = MakeFloatTween(Kept, 100.0f, 1.0f);
	KeptChild->SetEase(EDreamTweenEase::Linear);
	UDreamTweenerFloat* LostChild = MakeFloatTween(Lost, 100.0f, 1.0f);
	LostChild->SetEase(EDreamTweenEase::Linear);
	Sequence->Append(nullptr, LostChild);
	Sequence->Join(nullptr, KeptChild);

	Sequence->ToNextWithElapsedTime(0.25f);
	TestEqual(TEXT("both children run"), Kept, 25.0f, 0.01f);

	const TWeakObjectPtr<UDreamTweenerFloat> WeakLost(LostChild);
	LostChild->MarkAsGarbage();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	LostChild = nullptr;
	TestFalse(TEXT("the marked child is gone"), WeakLost.IsValid());

	// The sequence's list held the child in a UPROPERTY, which the collector nulls: every loop over the children
	// dereferenced that null on the next step.
	Sequence->ToNextWithElapsedTime(0.5f);
	TestEqual(TEXT("the sequence steps on without it"), Kept, 50.0f, 0.01f);
	Sequence->ToNextWithElapsedTime(1.0f);
	TestEqual(TEXT("to its end"), Kept, 100.0f, 0.01f);
	// Restart walks and sorts the lists again; reached through the base, where it is public.
	static_cast<UDreamTweener*>(Sequence)->Restart();
	TestEqual(TEXT("and restarting it puts the child it still has back at the start"), Kept, 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenSequenceSelfSeekTest,
	"DreamGUI.Tween.Sequence.ACallbackThatSeeksItsOwnSequenceLeavesTheRestOfThatPassAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceSelfSeekTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenerTestLocal;

	// A plays from 0 to 1, a callback at 1 seeks the sequence (once), B plays from 1 to 2. The seek rebuilt and
	// re-sorted the lists under the pass that fired the callback, which then walked on by index: it moved B to
	// the finished list in the callback's place (B never played), and reached the reset callback again in the
	// same pass -- for ever, had it seeked every time. The counts below are the cap.
	auto BuildSequence = [](float& InOutA, float& InOutB, int32& InOutCalls, int32& InOutCompletes, bool bRestart)
	{
		UDreamTweenerFloat* TweenA = MakeFloatTween(InOutA, 100.0f, 1.0f);
		TweenA->SetEase(EDreamTweenEase::Linear);
		UDreamTweenerFloat* TweenB = MakeFloatTween(InOutB, 100.0f, 1.0f);
		TweenB->SetEase(EDreamTweenEase::Linear);
		UDreamTweenerSequence* Sequence = NewObject<UDreamTweenerSequence>(GetTransientPackage());
		// Goto and Restart are public on the base and reached through it.
		UDreamTweener* SequenceAsTween = Sequence;
		Sequence->Append(nullptr, TweenA);
		Sequence->AppendCallback(TFunction<void()>([&InOutCalls, SequenceAsTween, bRestart]
		{
			if (++InOutCalls == 1)
			{
				if (bRestart)
				{
					SequenceAsTween->Restart();
				}
				else
				{
					SequenceAsTween->Goto(0.5f);
				}
			}
		}));
		Sequence->Append(nullptr, TweenB);
		Sequence->OnComplete(TFunction<void()>([&InOutCompletes] { InOutCompletes++; }));
		return Sequence;
	};

	{
		float A = 0.0f;
		float B = 0.0f;
		int32 Calls = 0;
		int32 Completes = 0;
		UDreamTweenerSequence* Sequence = BuildSequence(A, B, Calls, Completes, false);
		Sequence->ToNextWithElapsedTime(0.6f);
		Sequence->ToNextWithElapsedTime(1.05f);
		TestEqual(TEXT("Goto: the callback fired once in the pass that reached it"), Calls, 1);
		TestEqual(TEXT("Goto: and what it sought stands -- A halfway, not run to its end again"), A, 50.0f, 0.01f);
		TestEqual(TEXT("Goto: B has not begun"), B, 0.0f, 0.01f);
		Sequence->ToNextWithElapsedTime(1.1f);
		Sequence->ToNextWithElapsedTime(2.0f);
		TestEqual(TEXT("Goto: B plays to its end"), B, 100.0f, 0.01f);
		TestEqual(TEXT("Goto: the callback fired once more, on the way past it again"), Calls, 2);
		TestEqual(TEXT("Goto: and the sequence completed once"), Completes, 1);
	}
	{
		float A = 0.0f;
		float B = 0.0f;
		int32 Calls = 0;
		int32 Completes = 0;
		UDreamTweenerSequence* Sequence = BuildSequence(A, B, Calls, Completes, true);
		Sequence->ToNextWithElapsedTime(0.6f);
		Sequence->ToNextWithElapsedTime(1.05f);
		TestEqual(TEXT("Restart: the callback fired once"), Calls, 1);
		TestEqual(TEXT("Restart: A is back at its start"), A, 0.0f, 0.01f);
		Sequence->ToNextWithElapsedTime(0.5f);
		TestEqual(TEXT("Restart: and plays again from there"), A, 50.0f, 0.01f);
		Sequence->ToNextWithElapsedTime(1.05f);
		Sequence->ToNextWithElapsedTime(2.0f);
		TestEqual(TEXT("Restart: B plays to its end"), B, 100.0f, 0.01f);
		TestEqual(TEXT("Restart: the callback fired once per pass over it"), Calls, 2);
		TestEqual(TEXT("Restart: and the sequence completed once"), Completes, 1);
	}
	{
		// A callback that seeks FORWARD past itself reaches itself again in the pass the seek runs, which seeks
		// again; the nesting is refused past a fixed depth, once, with an error.
		float A = 0.0f;
		float B = 0.0f;
		int32 Calls = 0;
		UDreamTweenerFloat* TweenA = MakeFloatTween(A, 100.0f, 1.0f);
		TweenA->SetEase(EDreamTweenEase::Linear);
		UDreamTweenerFloat* TweenB = MakeFloatTween(B, 100.0f, 1.0f);
		TweenB->SetEase(EDreamTweenEase::Linear);
		UDreamTweenerSequence* Sequence = NewObject<UDreamTweenerSequence>(GetTransientPackage());
		UDreamTweener* SequenceAsTween = Sequence;
		Sequence->Append(nullptr, TweenA);
		Sequence->AppendCallback(TFunction<void()>([&Calls, SequenceAsTween]
		{
			// The guard against a broken depth cap: no more than a few dozen, whatever happens.
			if (++Calls < 64)
			{
				SequenceAsTween->Goto(1.5f);
			}
		}));
		Sequence->Append(nullptr, TweenB);
		AddExpectedErrorPlain(TEXT("sent back into itself"), EAutomationExpectedErrorFlags::Contains, 1);
		Sequence->ToNextWithElapsedTime(1.05f);
		TestTrue(FString::Printf(TEXT("the self-seeking callback was stopped after a bounded number of nested seeks (%d)"), Calls), Calls > 1 && Calls < 64);
		TestEqual(TEXT("and the seek that ran last stands: B halfway"), B, 50.0f, 0.01f);
	}
	return true;
}

#endif
