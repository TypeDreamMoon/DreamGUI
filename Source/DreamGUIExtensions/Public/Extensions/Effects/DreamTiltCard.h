// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamTiltCard.generated.h"

/**
 * Tilts the widget towards the pointer in 3D, on a spring: the edge under the pointer dips away, it grows a little while the
 * pointer is over it, and swings back flat when the pointer leaves. Through the widget's render transform, so layout is left
 * as it is; put it inside a perspective scope (UDreamWidget's Perspective, on a parent) for the tilt to foreshorten.
 *
 * The tilt it is at (GetTilt) is what a holo foil on the same widget (UDreamHoloFoil) can take its angle of view from.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "Dream Tilt Card")
class DREAMGUIEXTENSIONS_API UDreamTiltCard : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UDreamTiltCard();

	/** Where it tilts to when it does not follow the pointer: -1..1 across and up its rect, (0, 0) flat. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tilt Card")
	void SetTargetTilt(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tilt Card")
	void SetFollowPointer(bool Value);
	/** The tilt it is at now, -1..1 across and up: the spring's, not the pointer's. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Tilt Card")
	FVector2D GetTilt() const { return Tilt; }

	/** The spring stepped by InDeltaTime and the render transform set: what every frame does, for a test. */
	void Step(float InDeltaTime);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card")
	bool bFollowPointer = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", meta = (EditCondition = "!bFollowPointer"))
	FVector2D TargetTilt = FVector2D::ZeroVector;
	/** The most it tilts, degrees, with the pointer at an edge. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", meta = (ClampMin = "0", UIMax = "45"))
	float MaxAngle = 12.0f;
	/** How much it grows while the pointer is over it. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", meta = (ClampMin = "0.5", UIMax = "1.5"))
	float HoverScale = 1.04f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", meta = (ClampMin = "1", UIMax = "1000"))
	float Stiffness = 180.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", meta = (ClampMin = "0", UIMax = "60"))
	float Damping = 16.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", AdvancedDisplay)
	int32 UserIndex = 0;
	UPROPERTY(EditAnywhere, Category = "DreamGUI|Tilt Card", AdvancedDisplay)
	int32 PointerId = 0;

private:
	FVector2D Tilt = FVector2D::ZeroVector;
	FVector2D TiltVelocity = FVector2D::ZeroVector;
	float Scale = 1.0f;
	float ScaleVelocity = 0.0f;
	bool bWarnedNoPerspective = false;
};
