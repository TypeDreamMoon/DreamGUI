// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamVisualEmpty.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "DreamTweenerSequence.h"
#include "DreamScopedGameInstanceWorld.h"

/*
 * Tweens of one property replacing each other (UDreamTweener::SetExclusive), which the property verbs -- a widget's
 * RenderOpacityTo, a visual's ColorTo -- turn on. Two fades on one widget used to run side by side, and whichever ended
 * last won: a tooltip told to fade out while its fade in still ran stayed lit. Every tween here is on Manual tick, so
 * ManualTick is the only clock.
 */
namespace DreamTweenReplacementTestLocal
{
	struct FTweenedFloat
	{
		TSharedRef<float> Value = MakeShared<float>(0.0f);

		FDreamTweenFloatGetterFunction Getter() const
		{
			const TSharedRef<float> SharedValue = Value;
			return FDreamTweenFloatGetterFunction::CreateLambda([SharedValue] { return *SharedValue; });
		}
		FDreamTweenFloatSetterFunction Setter() const
		{
			const TSharedRef<float> SharedValue = Value;
			return FDreamTweenFloatSetterFunction::CreateLambda([SharedValue](float InValue) { *SharedValue = InValue; });
		}
	};

	/** A linear tween of InValue to InEnd, a property tween of InTarget's bit InChannels. */
	UDreamTweener* MakeVerbTween(UObject* InContext, const FTweenedFloat& InValue, float InEnd, float InDuration,
		const UObject* InTarget, uint64 InChannels, float InDelay = 0.0f)
	{
		UDreamTweener* Tween = UDreamTweenManager::To(InContext, InValue.Getter(), InValue.Setter(), InEnd, InDuration);
		if (Tween != nullptr)
		{
			Tween->SetEase(EDreamTweenEase::Linear)->SetTickType(EDreamTweenTickType::Manual)->SetDelay(InDelay);
			Tween->SetExclusive(InTarget, InChannels);
		}
		return Tween;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenReplacementLaterStartWinsTest,
	"DreamGUI.Tween.Replacement.ALaterTweenOfTheSamePropertyStopsTheEarlierWhenItStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenReplacementLaterStartWinsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenReplacementTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	UDreamVisualEmpty* Target = NewObject<UDreamVisualEmpty>(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager) || !TestNotNull(TEXT("and a target"), Target))
	{
		return false;
	}

	// A fade in, then at once a fade out of the same property: the fade out stops the fade in when it starts.
	FTweenedFloat Opacity;
	UDreamTweener* FadeIn = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 1.0f, Target, 1);
	UDreamTweener* FadeOut = MakeVerbTween(TestWorld.World, Opacity, 0.0f, 1.0f, Target, 1);
	Manager->ManualTick(0.1f);
	TestTrue(TEXT("The earlier tween was stopped"), FadeIn->IsMarkedToKill());
	TestTrue(TEXT("and the later runs"), Manager->IsTweening(FadeOut) && !FadeOut->IsMarkedToKill());
	Manager->ManualTick(2.0f);
	TestNearlyEqual(TEXT("The property ends where the later one sends it"), *Opacity.Value, 0.0f, 0.001f);

	// Another property of the same target is left alone.
	FTweenedFloat Scale;
	FTweenedFloat Angle;
	UDreamTweener* ScaleTween = MakeVerbTween(TestWorld.World, Scale, 2.0f, 1.0f, Target, 2);
	UDreamTweener* AngleTween = MakeVerbTween(TestWorld.World, Angle, 90.0f, 1.0f, Target, 4);
	Manager->ManualTick(0.1f);
	TestFalse(TEXT("A tween of another property is not stopped"), ScaleTween->IsMarkedToKill());
	TestFalse(TEXT("nor does it stop the other"), AngleTween->IsMarkedToKill());

	// The same property of another target is left alone too.
	UDreamVisualEmpty* Other = NewObject<UDreamVisualEmpty>(TestWorld.World);
	FTweenedFloat OtherOpacity;
	UDreamTweener* Mine = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 1.0f, Target, 1);
	UDreamTweener* Theirs = MakeVerbTween(TestWorld.World, OtherOpacity, 1.0f, 1.0f, Other, 1);
	Manager->ManualTick(0.1f);
	TestFalse(TEXT("Another target's tween of the same property is not stopped"), Mine->IsMarkedToKill() || Theirs->IsMarkedToKill());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenReplacementDelayedLaterTest,
	"DreamGUI.Tween.Replacement.AFadeOutQueuedWithADelayStillFollowsTheFadeIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenReplacementDelayedLaterTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenReplacementTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	UDreamVisualEmpty* Target = NewObject<UDreamVisualEmpty>(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager) || !TestNotNull(TEXT("and a target"), Target))
	{
		return false;
	}

	// A fade in now and a fade out two seconds later, asked for in that order: the fade in starting does not stop the
	// fade out waiting its turn, and the fade out starting takes over.
	FTweenedFloat Opacity;
	UDreamTweener* FadeIn = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 0.5f, Target, 1);
	UDreamTweener* FadeOut = MakeVerbTween(TestWorld.World, Opacity, 0.0f, 0.5f, Target, 1, /*Delay*/ 2.0f);
	Manager->ManualTick(0.1f);
	TestFalse(TEXT("A later tween that has not started is left to start"), FadeOut->IsMarkedToKill());
	Manager->ManualTick(0.5f);
	TestNearlyEqual(TEXT("The fade in runs to its end"), *Opacity.Value, 1.0f, 0.001f);
	Manager->ManualTick(1.5f);
	Manager->ManualTick(1.0f);
	TestNearlyEqual(TEXT("and the fade out follows it"), *Opacity.Value, 0.0f, 0.001f);

	// Asked for the other way round -- the delayed one first -- the one asked for last wins, as a command does.
	UDreamTweener* LateFadeOut = MakeVerbTween(TestWorld.World, Opacity, 0.0f, 0.5f, Target, 1, /*Delay*/ 2.0f);
	UDreamTweener* NowFadeIn = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 0.5f, Target, 1);
	Manager->ManualTick(0.1f);
	TestTrue(TEXT("An earlier tween still waiting is stopped by a later one that starts"), LateFadeOut->IsMarkedToKill());
	TestFalse(TEXT("which runs"), NowFadeIn->IsMarkedToKill());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTweenReplacementExemptionsTest,
	"DreamGUI.Tween.Replacement.SequencesKeptTweensAndOptOutsAreNotKilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenReplacementExemptionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenReplacementTestLocal;
	DreamTests::FScopedGameInstanceWorld TestWorld;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(TestWorld.World);
	UDreamVisualEmpty* Target = NewObject<UDreamVisualEmpty>(TestWorld.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager) || !TestNotNull(TEXT("and a target"), Target))
	{
		return false;
	}

	// A sequence of a fade in and a fade out of one property: the second tween does not stop the first, and a tween
	// started beside the sequence does not stop either -- the sequence schedules its own.
	{
		FTweenedFloat Opacity;
		UDreamTweenerSequence* Sequence = UDreamTweenManager::CreateSequence(TestWorld.World);
		if (!TestNotNull(TEXT("A sequence is made"), Sequence))
		{
			return false;
		}
		Sequence->SetTickType(EDreamTweenTickType::Manual);
		UDreamTweener* In = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 0.5f, Target, 1);
		Sequence->Append(TestWorld.World, In);
		UDreamTweener* Out = MakeVerbTween(TestWorld.World, Opacity, 0.0f, 0.5f, Target, 1);
		Sequence->Append(TestWorld.World, Out);
		FTweenedFloat Unrelated;
		UDreamTweener* Beside = MakeVerbTween(TestWorld.World, Unrelated, 1.0f, 0.5f, Target, 1);
		Manager->ManualTick(0.25f);
		TestFalse(TEXT("A tween in a sequence is not stopped by a tween beside it"), In->IsMarkedToKill());
		TestFalse(TEXT("nor does it stop that tween"), Beside->IsMarkedToKill());
		Manager->ManualTick(0.5f);
		TestFalse(TEXT("The sequence's second tween did not stop its first"), In->IsMarkedToKill());
		Manager->ManualTick(1.0f);
		TestNearlyEqual(TEXT("The sequence ran both"), *Opacity.Value, 0.0f, 0.001f);
	}

	// A tween kept to be played again is paused, not killed: its handle still restarts it.
	{
		FTweenedFloat Opacity;
		UDreamTweener* Kept = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 1.0f, Target, 1);
		Kept->SetAutoKill(false);
		UDreamTweener* Later = MakeVerbTween(TestWorld.World, Opacity, 0.0f, 1.0f, Target, 1);
		Manager->ManualTick(0.1f);
		TestFalse(TEXT("A kept tween is not killed"), Kept->IsMarkedToKill());
		TestTrue(TEXT("it is still the manager's"), Manager->IsTweening(Kept));
		Later->Kill();
		Kept->Restart();
		Manager->ManualTick(2.0f);
		TestNearlyEqual(TEXT("and restarting it plays it"), *Opacity.Value, 1.0f, 0.001f);
		Kept->Kill();
	}

	// SetReplacesEarlierTweens(false): the tween neither stops the others nor is stopped by them.
	{
		FTweenedFloat Opacity;
		FTweenedFloat Glow;
		UDreamTweener* OptedOut = MakeVerbTween(TestWorld.World, Glow, 1.0f, 1.0f, Target, 1);
		OptedOut->SetReplacesEarlierTweens(false);
		UDreamTweener* Ordinary = MakeVerbTween(TestWorld.World, Opacity, 1.0f, 1.0f, Target, 1);
		Manager->ManualTick(0.1f);
		TestFalse(TEXT("A tween that opted out is not stopped"), OptedOut->IsMarkedToKill());
		UDreamTweener* OptedOutLater = MakeVerbTween(TestWorld.World, Glow, 0.0f, 1.0f, Target, 1);
		OptedOutLater->SetReplacesEarlierTweens(false);
		Manager->ManualTick(0.1f);
		TestFalse(TEXT("and one that opted out stops nothing"), Ordinary->IsMarkedToKill());
	}
	return true;
}

#endif
