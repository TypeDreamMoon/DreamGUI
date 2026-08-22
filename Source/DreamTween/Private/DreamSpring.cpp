// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamSpring.h"

float FDreamSpringParams::DampingRatio() const
{
	const float Denominator = 2.0f * FMath::Sqrt(FMath::Max(Stiffness * Mass, KINDA_SMALL_NUMBER));
	return Damping / Denominator;
}

bool FDreamSpringState::IsAtRest(const FDreamSpringParams& Params) const
{
	return FMath::Abs(Value - Target) <= Params.RestDistance && FMath::Abs(Velocity) <= Params.RestVelocity;
}

bool FDreamSpring::Step(const FDreamSpringParams& Params, FDreamSpringState& State, float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return !State.IsAtRest(Params);
	}
	if (State.IsAtRest(Params))
	{
		State.Settle();
		return false;
	}

	// Solve x'' + 2 zeta w0 x' + w0^2 x = 0 for the displacement x = Value - Target, with the current
	// displacement and velocity as initial conditions, and read it off at t = DeltaTime.
	const double Mass = FMath::Max((double)Params.Mass, (double)KINDA_SMALL_NUMBER);
	const double Stiffness = FMath::Max((double)Params.Stiffness, (double)KINDA_SMALL_NUMBER);
	const double W0 = FMath::Sqrt(Stiffness / Mass);
	const double Zeta = (double)Params.Damping / (2.0 * FMath::Sqrt(Stiffness * Mass));
	const double X0 = (double)State.Value - (double)State.Target;
	const double V0 = State.Velocity;
	const double T = DeltaTime;

	double X, V;
	if (Zeta < 1.0 - 1e-6)
	{
		// Under-damped: a decaying oscillation.
		const double Wd = W0 * FMath::Sqrt(1.0 - Zeta * Zeta);
		const double Decay = FMath::Exp(-Zeta * W0 * T);
		const double C2 = (V0 + Zeta * W0 * X0) / Wd;
		const double Cos = FMath::Cos(Wd * T), Sin = FMath::Sin(Wd * T);
		X = Decay * (X0 * Cos + C2 * Sin);
		V = Decay * ((C2 * Wd - Zeta * W0 * X0) * Cos - (X0 * Wd + Zeta * W0 * C2) * Sin);
	}
	else if (Zeta < 1.0 + 1e-6)
	{
		// Critically damped: the fastest settle with no overshoot.
		const double Decay = FMath::Exp(-W0 * T);
		const double C2 = V0 + W0 * X0;
		X = Decay * (X0 + C2 * T);
		V = Decay * (C2 - W0 * (X0 + C2 * T));
	}
	else
	{
		// Over-damped: two decaying exponentials.
		const double Root = W0 * FMath::Sqrt(Zeta * Zeta - 1.0);
		const double R1 = -Zeta * W0 + Root;
		const double R2 = -Zeta * W0 - Root;
		const double A = (V0 - R2 * X0) / (R1 - R2);
		const double B = X0 - A;
		const double E1 = FMath::Exp(R1 * T), E2 = FMath::Exp(R2 * T);
		X = A * E1 + B * E2;
		V = A * R1 * E1 + B * R2 * E2;
	}

	State.Value = (float)((double)State.Target + X);
	State.Velocity = (float)V;
	if (State.IsAtRest(Params))
	{
		State.Settle();
		return false;
	}
	return true;
}

bool FDreamSpring::Step(const FDreamSpringParams& Params, FDreamSpringState* States, int32 Count, float DeltaTime)
{
	bool bMoving = false;
	for (int32 i = 0; i < Count; i++)
	{
		bMoving |= Step(Params, States[i], DeltaTime);
	}
	return bMoving;
}

bool UDreamSpringLibrary::StepSpring(const FDreamSpringParams& Params, FDreamSpringState& State, float DeltaTime)
{
	return FDreamSpring::Step(Params, State, DeltaTime);
}

bool UDreamSpringLibrary::IsSpringAtRest(const FDreamSpringParams& Params, const FDreamSpringState& State)
{
	return State.IsAtRest(Params);
}
