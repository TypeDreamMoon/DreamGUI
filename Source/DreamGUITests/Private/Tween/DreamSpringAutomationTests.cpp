// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DreamSpring.h"

/*
 * The spring's closed-form step: it must settle, behave as its damping ratio says, give the same
 * trajectory whatever the frame length, and keep its velocity across a retarget.
 */
namespace DreamSpringTestLocal
{
	/** Run until rest or the time budget runs out; returns the time it took (or -1). */
	float RunToRest(const FDreamSpringParams& Params, FDreamSpringState& State, float Dt, float MaxTime, float* OutPeak = nullptr)
	{
		float Peak = State.Value;
		for (float T = 0.0f; T < MaxTime; T += Dt)
		{
			if (!FDreamSpring::Step(Params, State, Dt))
			{
				if (OutPeak)*OutPeak = Peak;
				return T + Dt;
			}
			Peak = FMath::Max(Peak, State.Value);
		}
		if (OutPeak)*OutPeak = Peak;
		return -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpringSettlesTest,
	"DreamGUI.Tween.Spring.SettlesAccordingToDamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpringSettlesTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpringTestLocal;

	// Critically damped: reaches the target without ever passing it.
	{
		FDreamSpringParams Params(1.0f, 100.0f, 20.0f);
		TestEqual(TEXT("critical damping ratio"), Params.DampingRatio(), 1.0f, 0.001f);
		FDreamSpringState State;
		State.Value = 0.0f;
		State.Target = 100.0f;
		float Peak = 0.0f;
		const float Time = RunToRest(Params, State, 1.0f / 60.0f, 10.0f, &Peak);
		TestTrue(TEXT("critical settles"), Time > 0.0f && Time < 3.0f);
		TestEqual(TEXT("critical lands on the target"), State.Value, 100.0f, 0.05f);
		TestTrue(TEXT("critical never overshoots"), Peak <= 100.0f + 0.01f);
	}
	// Under-damped: overshoots, then settles.
	{
		FDreamSpringParams Params(1.0f, 100.0f, 4.0f);
		TestTrue(TEXT("under-damped ratio < 1"), Params.DampingRatio() < 1.0f);
		FDreamSpringState State;
		State.Target = 100.0f;
		float Peak = 0.0f;
		const float Time = RunToRest(Params, State, 1.0f / 60.0f, 20.0f, &Peak);
		TestTrue(TEXT("under-damped settles"), Time > 0.0f);
		TestTrue(TEXT("under-damped overshoots"), Peak > 105.0f);
		TestEqual(TEXT("under-damped lands on the target"), State.Value, 100.0f, 0.05f);
	}
	// Over-damped: slow, no overshoot.
	{
		FDreamSpringParams Params(1.0f, 100.0f, 40.0f);
		TestTrue(TEXT("over-damped ratio > 1"), Params.DampingRatio() > 1.0f);
		FDreamSpringState State;
		State.Target = 100.0f;
		float Peak = 0.0f;
		const float Time = RunToRest(Params, State, 1.0f / 60.0f, 30.0f, &Peak);
		TestTrue(TEXT("over-damped settles"), Time > 0.0f);
		TestTrue(TEXT("over-damped never overshoots"), Peak <= 100.0f + 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpringStepInvarianceTest,
	"DreamGUI.Tween.Spring.TrajectoryDoesNotDependOnFrameLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpringStepInvarianceTest::RunTest(const FString& Parameters)
{
	// The closed form makes one 0.5s step equal to thirty 1/60s steps (up to float noise), which an
	// explicit integrator would not manage -- and which is what keeps a hitching frame from
	// launching the value.
	FDreamSpringParams Params(1.0f, 120.0f, 6.0f);
	Params.RestDistance = 0.0f;
	Params.RestVelocity = 0.0f;
	FDreamSpringState Fine, Coarse;
	Fine.Target = Coarse.Target = 50.0f;
	Fine.Velocity = Coarse.Velocity = -30.0f;
	for (int32 i = 0; i < 30; i++)
	{
		FDreamSpring::Step(Params, Fine, 0.5f / 30.0f);
	}
	FDreamSpring::Step(Params, Coarse, 0.5f);
	TestEqual(TEXT("value after 0.5s"), Coarse.Value, Fine.Value, 0.01f);
	TestEqual(TEXT("velocity after 0.5s"), Coarse.Velocity, Fine.Velocity, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpringRetargetTest,
	"DreamGUI.Tween.Spring.RetargetKeepsVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpringRetargetTest::RunTest(const FString& Parameters)
{
	FDreamSpringParams Params(1.0f, 100.0f, 10.0f);
	FDreamSpringState State;
	State.Target = 100.0f;
	for (int32 i = 0; i < 6; i++)
	{
		FDreamSpring::Step(Params, State, 1.0f / 60.0f);
	}
	const float VelocityBefore = State.Velocity;
	TestTrue(TEXT("moving toward the first target"), VelocityBefore > 0.0f);
	// Move the goal further: the value keeps going at the same speed this frame instead of restarting.
	State.Target = 200.0f;
	const float ValueBefore = State.Value;
	FDreamSpring::Step(Params, State, 1.0f / 60.0f);
	TestTrue(TEXT("still moving forward"), State.Value > ValueBefore);
	TestTrue(TEXT("velocity is continuous"), FMath::Abs(State.Velocity - VelocityBefore) < VelocityBefore * 0.5f);
	// And a rest spring that is retargeted starts moving again.
	FDreamSpringState Rest;
	Rest.Target = 0.0f;
	TestFalse(TEXT("at rest does not move"), FDreamSpring::Step(Params, Rest, 1.0f / 60.0f));
	Rest.Target = 10.0f;
	TestTrue(TEXT("retargeted rest spring moves"), FDreamSpring::Step(Params, Rest, 1.0f / 60.0f));
	return true;
}

#endif
