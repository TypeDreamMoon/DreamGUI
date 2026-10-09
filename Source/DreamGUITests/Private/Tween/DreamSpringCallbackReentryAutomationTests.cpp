// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "DreamScopedGameInstanceWorld.h"
#include "DreamTweenManager.h"
#include "DreamTweenerSpring.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSpringCancellationCallbackTest,
	"DreamGUI.Tween.Spring.Reentry.CancellationStopsWritesAndLaterNotificationsInTheSameStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpringCancellationCallbackTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameInstanceWorld World;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager)) return false;
	const TArray<FName> Phases = { TEXT("Getter"), TEXT("CycleStart"), TEXT("Start"), TEXT("Setter"), TEXT("Update"), TEXT("CycleComplete"), TEXT("Complete") };
	for (const FName Phase : Phases)
	{
		const TSharedRef<TArray<FName>> Trace = MakeShared<TArray<FName>>();
		const TSharedRef<TWeakObjectPtr<UDreamTweenerSpring>> Handle = MakeShared<TWeakObjectPtr<UDreamTweenerSpring>>();
		const auto Record = [Trace, Handle, Phase](FName Event)
		{
			Trace->Add(Event);
			if (Event == Phase && Handle->IsValid()) Handle->Get()->Kill(false);
		};
		UDreamTweenerSpring* Spring = UDreamTweenManager::SpringTo(World.World,
			FDreamTweenFloatGetterFunction::CreateLambda([Record] { Record(TEXT("Getter")); return 0.0f; }),
			FDreamTweenFloatSetterFunction::CreateLambda([Record](float) { Record(TEXT("Setter")); }),
			0.0f, FDreamSpringParams(1.0f, 100.0f, 20.0f));
		*Handle = Spring;
		Spring->SetTickType(EDreamTweenTickType::Manual);
		Spring->OnCycleStart(TFunction<void()>([Record] { Record(TEXT("CycleStart")); }));
		Spring->OnStart(TFunction<void()>([Record] { Record(TEXT("Start")); }));
		Spring->OnUpdate(TFunction<void(float)>([Record](float) { Record(TEXT("Update")); }));
		Spring->OnCycleComplete(TFunction<void()>([Record] { Record(TEXT("CycleComplete")); }));
		Spring->OnComplete(TFunction<void()>([Record] { Record(TEXT("Complete")); }));
		Manager->ManualTick(0.1f);
		const int32 ExpectedCount = Phases.IndexOfByKey(Phase) + 1;
		TestEqual(FString::Printf(TEXT("Cancellation from %s stops the event trace"), *Phase.ToString()), Trace->Num(), ExpectedCount);
		TestFalse(TEXT("A cancelled spring is retired in this manager tick"), Manager->IsTweening(Spring));
		const int32 Calls = Trace->Num();
		Manager->ManualTick(0.1f);
		TestEqual(TEXT("The retired spring emits nothing on the next tick"), Trace->Num(), Calls);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSpringRestartCallbackTest,
	"DreamGUI.Tween.Spring.Reentry.CallbackRestartsSurviveTheOldStepWithEitherAutoKillSetting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpringRestartCallbackTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameInstanceWorld World;
	UDreamTweenManager* Manager = UDreamTweenManager::GetDreamTweenInstance(World.World);
	if (!TestNotNull(TEXT("The world has a tween manager"), Manager)) return false;
	for (bool bAutoKill : { false, true })
	{
		for (int32 Phase = 0; Phase < 3; ++Phase)
		{
			UDreamTweenerSpring* Spring = UDreamTweenManager::SpringTo(World.World,
				FDreamTweenFloatGetterFunction::CreateLambda([] { return 0.0f; }),
				FDreamTweenFloatSetterFunction(), 0.0f, FDreamSpringParams(1.0f, 100.0f, 20.0f));
			Spring->SetTickType(EDreamTweenTickType::Manual)->SetAutoKill(bAutoKill);
			const TSharedRef<int32> Starts = MakeShared<int32>(0);
			const TSharedRef<int32> Completes = MakeShared<int32>(0);
			const TSharedRef<bool> Restarted = MakeShared<bool>(false);
			const auto RestartOnce = [Spring, Restarted]
			{
				if (!*Restarted) { *Restarted = true; static_cast<UDreamTweener*>(Spring)->Restart(); }
			};
			Spring->OnStart(TFunction<void()>([Starts] { ++*Starts; }));
			Spring->OnComplete(TFunction<void()>([Completes] { ++*Completes; }));
			if (Phase == 0) Spring->OnUpdate(TFunction<void(float)>([RestartOnce](float) { RestartOnce(); }));
			else if (Phase == 1) Spring->OnCycleComplete(TFunction<void()>(RestartOnce));
			else Spring->OnComplete(TFunction<void()>(RestartOnce));
			Manager->ManualTick(0.1f);
			TestTrue(TEXT("Restart preserves manager membership"), Manager->IsTweening(Spring));
			TestEqual(TEXT("Restart preserves its reset clock"), Spring->GetElapsedTime(), 0.0f);
			TestEqual(TEXT("The old step emits no later completion after an early restart"), *Completes, Phase == 2 ? 1 : 0);
			Manager->ManualTick(0.1f);
			TestEqual(TEXT("The restarted spring really starts again"), *Starts, 2);
			TestEqual(TEXT("The restarted spring completes on its fresh run"), *Completes, Phase == 2 ? 2 : 1);
			TestEqual(TEXT("The second run follows the configured auto-kill"), Manager->IsTweening(Spring), !bAutoKill);
			Spring->Kill();
			Manager->ManualTick(0.0f);
		}
	}
	return true;
}

#endif
