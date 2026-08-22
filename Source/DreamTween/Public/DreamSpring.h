// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamSpring.generated.h"

/**
 * A damped spring, the way UI motion libraries (CSS spring(), react-spring, Apple's) describe it:
 * mass, stiffness, damping. There is no duration; the value moves toward its target and settles
 * when both the distance and the velocity are under the rest thresholds. Because the state keeps
 * its velocity, retargeting mid-flight is continuous -- the reason lyric players use springs.
 */
USTRUCT(BlueprintType)
struct DREAMTWEEN_API FDreamSpringParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0001"))
	float Mass = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0001"))
	float Stiffness = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0"))
	float Damping = 10.0f;
	/** Distance to the target under which the spring may come to rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0"))
	float RestDistance = 0.01f;
	/** Speed under which the spring may come to rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0"))
	float RestVelocity = 0.01f;

	FDreamSpringParams() {}
	FDreamSpringParams(float InMass, float InStiffness, float InDamping)
		: Mass(InMass), Stiffness(InStiffness), Damping(InDamping) {}

	/** Damping ratio: <1 bounces, 1 is critical, >1 creeps. */
	float DampingRatio() const;
};

USTRUCT(BlueprintType)
struct DREAMTWEEN_API FDreamSpringState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float Value = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float Velocity = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float Target = 0.0f;

	bool IsAtRest(const FDreamSpringParams& Params) const;
	/** Snap to the target and stop. */
	void Settle() { Value = Target; Velocity = 0.0f; }
};

struct DREAMTWEEN_API FDreamSpring
{
	/**
	 * Advance the state by DeltaTime with the closed-form solution of the damped harmonic
	 * oscillator, so any step size is exact and stable (no explicit integration to blow up on a
	 * long frame). Returns true while the spring is still moving.
	 */
	static bool Step(const FDreamSpringParams& Params, FDreamSpringState& State, float DeltaTime);

	/** Step a spring per component; every component shares the parameters and the rest test is joint. */
	static bool Step(const FDreamSpringParams& Params, FDreamSpringState* States, int32 Count, float DeltaTime);
};

UCLASS()
class DREAMTWEEN_API UDreamSpringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Advance a spring by DeltaTime; returns true while it is still moving. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween|Spring")
	static bool StepSpring(const FDreamSpringParams& Params, UPARAM(ref) FDreamSpringState& State, float DeltaTime);
	UFUNCTION(BlueprintPure, Category = "DreamTween|Spring")
	static bool IsSpringAtRest(const FDreamSpringParams& Params, const FDreamSpringState& State);
};
