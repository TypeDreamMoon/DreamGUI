// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamTweenManager.h"
#include "DreamTweenerSequence.h"
#include "Tweener/DreamTweenerFloat.h"
#include "DreamScopedGameInstanceWorld.h"

namespace DreamTweenExplicitReentryTestLocal
{
	struct FState
	{
		float Value = 0.0f;
		int32 Writes = 0;
		int32 Starts = 0;
		int32 Cycles = 0;
		int32 Updates = 0;
		int32 Completes = 0;
		bool bArmed = false;
		TWeakObjectPtr<UDreamTweener> Tween;
		TWeakObjectPtr<UDreamTweenerSequence> Sequence;
		TArray<float> Progress;
	};

	UDreamTweener* MakeTween(UObject* Context, const TSharedRef<FState>& State,
		const TFunction<void()>& GetterAction = {}, const TFunction<void(float)>& SetterAction = {})
	{
		UDreamTweener* Tween = UDreamTweenManager::To(Context,
			FDreamTweenFloatGetterFunction::CreateLambda([State, GetterAction]
			{
				if (GetterAction)GetterAction();
				return State->Value;
			}),
			FDreamTweenFloatSetterFunction::CreateLambda([State, SetterAction](float Value)
			{
				State->Value = Value;
				++State->Writes;
				if (SetterAction)SetterAction(Value);
			}), 10.0f, 1.0f);
		if (Tween != nullptr)
		{
			State->Tween = Tween;
			Tween->SetEase(EDreamTweenEase::Linear)->SetTickType(EDreamTweenTickType::Manual)->SetAutoKill(false);
			Tween->OnStart(TFunction<void()>([State] { ++State->Starts; }));
			Tween->OnCycleStart(TFunction<void()>([State] { ++State->Cycles; }));
			Tween->OnUpdate(TFunction<void(float)>([State](float Progress)
			{
				++State->Updates;
				State->Progress.Add(Progress);
			}));
			Tween->OnComplete(TFunction<void()>([State] { ++State->Completes; }));
		}
		return Tween;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenGetterCancellationTest,
	"DreamGUI.Tween.ExplicitReentry.AGetterCancellationDoesNotStartTheCancelledTween",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenGetterCancellationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenExplicitReentryTestLocal;
	DreamTests::FScopedGameInstanceWorld World;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
	if (!TestNotNull(TEXT("the world has a manager"), Manager))return false;
	const TSharedRef<FState> State = MakeShared<FState>();
	UDreamTweener* Tween = MakeTween(World.World, State, [State] { State->Tween->Kill(false); });
	if (!TestNotNull(TEXT("the value tween was created"), Tween))return false;
	Manager->ManualTick(0.25f);
	TestEqual(TEXT("a getter that cancelled owes no cycle-start event"), State->Cycles, 0);
	TestEqual(TEXT("a getter that cancelled owes no start event"), State->Starts, 0);
	TestEqual(TEXT("a getter that cancelled writes no value"), State->Writes, 0);
	TestEqual(TEXT("a getter that cancelled owes no update"), State->Updates, 0);
	TestEqual(TEXT("a getter that cancelled owes no completion"), State->Completes, 0);
	TestFalse(TEXT("the manager retires the cancelled tween"), Manager->IsTweening(Tween));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenForceCompleteReentryTest,
	"DreamGUI.Tween.ExplicitReentry.AForceCompletionRestartFromSetterOrUpdateOwnsTheNewRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenForceCompleteReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenExplicitReentryTestLocal;
	for (bool bFromSetter : { false, true })
	{
		DreamTests::FScopedGameInstanceWorld World;
		UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
		if (!TestNotNull(TEXT("the world has a manager"), Manager))return false;
		const TSharedRef<FState> State = MakeShared<FState>();
		const TFunction<void()> RestartOnce = [State]
		{
			if (State->bArmed)
			{
				State->bArmed = false;
				State->Tween->Restart();
			}
		};
		UDreamTweener* Tween = MakeTween(World.World, State, {}, [bFromSetter, RestartOnce](float)
		{
			if (bFromSetter)RestartOnce();
		});
		if (!TestNotNull(TEXT("the value tween was created"), Tween))return false;
		if (!bFromSetter)Tween->OnUpdate(TFunction<void(float)>([RestartOnce](float) { RestartOnce(); }));
		Manager->ManualTick(0.25f);
		State->bArmed = true;
		const int32 UpdatesBefore = State->Updates;
		Tween->ForceComplete();
		TestFalse(TEXT("the restarted force-completed tween is alive"), Tween->IsMarkedToKill());
		TestEqual(TEXT("the restart restores its value"), State->Value, 0.0f, 0.001f);
		TestEqual(TEXT("the superseded force-complete emits no completion"), State->Completes, 0);
		TestEqual(TEXT("a setter takeover suppresses the old update"), State->Updates, UpdatesBefore + (bFromSetter ? 0 : 1));
		Manager->ManualTick(0.25f);
		TestEqual(TEXT("the restarted run starts again"), State->Starts, 2);
		TestEqual(TEXT("the restarted run interpolates from its original origin"), State->Value, 2.5f, 0.001f);
		Manager->ManualTick(0.75f);
		TestEqual(TEXT("only the new run reaches completion"), State->Completes, 1);
		Tween->Kill(false);
		Manager->ManualTick(0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenGotoZeroReentryTest,
	"DreamGUI.Tween.ExplicitReentry.AGotoZeroSetterRedirectDoesNotEmitTheOldProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenGotoZeroReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenExplicitReentryTestLocal;
	DreamTests::FScopedGameInstanceWorld World;
	const TSharedRef<FState> State = MakeShared<FState>();
	UDreamTweener* Tween = MakeTween(World.World, State, {}, [State](float)
	{
		if (State->bArmed)
		{
			State->bArmed = false;
			State->Tween->Goto(0.75f);
		}
	});
	if (!TestNotNull(TEXT("the value tween was created"), Tween))return false;
	Tween->Goto(0.5f);
	State->Progress.Reset();
	State->bArmed = true;
	Tween->Goto(0.0f);
	TestEqual(TEXT("the new seek owns the value"), State->Value, 7.5f, 0.001f);
	TestEqual(TEXT("only the new seek owns an update"), State->Progress.Num(), 1);
	if (!State->Progress.IsEmpty())TestEqual(TEXT("the last update describes the current seek"), State->Progress.Last(), 0.75f, 0.001f);
	Tween->Kill(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenSequenceRewindReentryTest,
	"DreamGUI.Tween.Sequence.ARewindSetterCannotOverwriteTheNewerSequenceSeek",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceRewindReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenExplicitReentryTestLocal;
	for (bool bNested : { false, true })
	for (bool bRestart : { false, true })
	{
		DreamTests::FScopedGameInstanceWorld World;
		UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
		if (!TestNotNull(TEXT("the world has a manager"), Manager))return false;
		const TSharedRef<FState> State = MakeShared<FState>();
		UDreamTweener* Child = MakeTween(World.World, State, {}, [State](float)
		{
			if (State->bArmed)
			{
				State->bArmed = false;
				static_cast<UDreamTweener*>(State->Sequence.Get())->Goto(0.75f);
			}
		});
		UDreamTweenerSequence* Sequence = UDreamTweenManager::CreateSequence(World.World);
		if (!TestNotNull(TEXT("the child exists"), Child) || !TestNotNull(TEXT("the sequence exists"), Sequence))return false;
		State->Sequence = Sequence;
		Sequence->SetTickType(EDreamTweenTickType::Manual)->SetEase(EDreamTweenEase::Linear)->SetAutoKill(false);
		UDreamTweener* DirectChild = Child;
		if (bNested)
		{
			UDreamTweenerSequence* Nested = UDreamTweenManager::CreateSequence(World.World);
			if (!TestNotNull(TEXT("the nested sequence exists"), Nested))return false;
			Nested->SetEase(EDreamTweenEase::Linear)->SetAutoKill(false);
			Nested->Append(nullptr, Child);
			DirectChild = Nested;
		}
		Sequence->Append(nullptr, DirectChild);
		Manager->ManualTick(0.5f);
		State->bArmed = true;
		if (bRestart)static_cast<UDreamTweener*>(Sequence)->Restart();
		else static_cast<UDreamTweener*>(Sequence)->Goto(0.25f);
		TestEqual(TEXT("the setter's newer seek owns the value"), State->Value, 7.5f, 0.001f);
		TestEqual(TEXT("the setter's newer seek owns the sequence clock"), Sequence->GetProgress(), 0.75f, 0.001f);
		TestEqual(TEXT("the setter's newer seek owns the direct child clock"), DirectChild->GetProgress(), 0.75f, 0.001f);
		TestEqual(TEXT("an old nested rewind cannot zero the leaf's new clock"), Child->GetProgress(), 0.75f, 0.001f);
		Manager->ManualTick(0.05f);
		TestEqual(TEXT("the newer seek continues from its own position"), State->Value, 8.0f, 0.001f);
		Sequence->Kill(false);
		Manager->ManualTick(0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamTweenSequenceLoopResetReentryTest,
	"DreamGUI.Tween.Sequence.ALoopResetSetterCannotOverwriteTheNewerSequenceSeek",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTweenSequenceLoopResetReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamTweenExplicitReentryTestLocal;
	for (bool bNested : { false, true })
	for (EDreamTweenLoop Loop : { EDreamTweenLoop::Restart, EDreamTweenLoop::Incremental })
	{
		DreamTests::FScopedGameInstanceWorld World;
		UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
		if (!TestNotNull(TEXT("the world has a manager"), Manager))return false;
		const TSharedRef<FState> State = MakeShared<FState>();
		UDreamTweener* Child = MakeTween(World.World, State, {}, [State](float)
		{
			// The leaf's completion precedes the parent's loop reset. Seek from the reset's setter,
			// rather than from the end-value setter that completed the leaf a few calls earlier.
			if (State->bArmed && State->Completes > 0)
			{
				State->bArmed = false;
				static_cast<UDreamTweener*>(State->Sequence.Get())->Goto(0.75f);
			}
		});
		UDreamTweenerSequence* Sequence = UDreamTweenManager::CreateSequence(World.World);
		if (!TestNotNull(TEXT("the child exists"), Child) || !TestNotNull(TEXT("the looping sequence exists"), Sequence))return false;
		State->Sequence = Sequence;
		Sequence->SetTickType(EDreamTweenTickType::Manual)->SetEase(EDreamTweenEase::Linear)->SetAutoKill(false)->SetLoop(Loop, 2);
		UDreamTweener* DirectChild = Child;
		if (bNested)
		{
			UDreamTweenerSequence* Nested = UDreamTweenManager::CreateSequence(World.World);
			if (!TestNotNull(TEXT("the nested sequence exists"), Nested))return false;
			Nested->SetEase(EDreamTweenEase::Linear)->SetAutoKill(false);
			Nested->Append(nullptr, Child);
			DirectChild = Nested;
		}
		Sequence->Append(nullptr, DirectChild);
		Manager->ManualTick(0.25f);
		State->bArmed = true;
		Manager->ManualTick(0.75f);
		TestFalse(TEXT("the natural loop reset actually reached the redirecting setter"), State->bArmed);
		TestEqual(TEXT("the reset setter's newer seek owns the value"), State->Value, 7.5f, 0.001f);
		TestEqual(TEXT("the reset setter's newer seek owns the parent clock"), Sequence->GetProgress(), 0.75f, 0.001f);
		TestEqual(TEXT("the reset setter's newer seek owns the direct child clock"), DirectChild->GetProgress(), 0.75f, 0.001f);
		TestEqual(TEXT("an old nested loop reset cannot zero the leaf's new clock"), Child->GetProgress(), 0.75f, 0.001f);
		Manager->ManualTick(0.05f);
		TestEqual(TEXT("the redirected loop continues from its own position"), State->Value, 8.0f, 0.001f);
		Sequence->Kill(false);
		Manager->ManualTick(0.0f);
	}
	return true;
}

#endif
