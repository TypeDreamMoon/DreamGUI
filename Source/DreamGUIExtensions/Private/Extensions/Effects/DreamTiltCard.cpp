// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/Effects/DreamTiltCard.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "DreamPointerOnPlane.h"

UDreamTiltCard::UDreamTiltCard()
{
	DeclareTransformChangedUnused(UDreamTiltCard::StaticClass());
}

void UDreamTiltCard::SetTargetTilt(FVector2D Value)
{
	TargetTilt = FVector2D(FMath::Clamp(Value.X, -1.0, 1.0), FMath::Clamp(Value.Y, -1.0, 1.0));
}

void UDreamTiltCard::SetFollowPointer(bool Value)
{
	bFollowPointer = Value;
}

void UDreamTiltCard::SetEnabled(bool Value)
{
	if (bEnabled != Value)
	{
		bEnabled = Value;
		if (!bEnabled)
		{
			Step(0.0f);
		}
	}
}

void UDreamTiltCard::OnRegister()
{
	Super::OnRegister();
	Tilt = TiltVelocity = FVector2D::ZeroVector;
	Scale = 1.0f;
	ScaleVelocity = 0.0f;
	AppliedTilt = FRotator::ZeroRotator;
	AppliedScale = 1.0f;
}

void UDreamTiltCard::OnUnregister()
{
	if (UDreamWidget* Widget = GetWidget())
	{
		// Only its own share comes off: the rotation and scale the card has of its own stay.
		const FRotator Current = Widget->GetRenderRotation();
		Widget->SetRenderRotation(FRotator(Current.Pitch - AppliedTilt.Pitch, Current.Yaw - AppliedTilt.Yaw, Current.Roll));
		const FVector CurrentScale = Widget->GetRenderScale();
		const double Undo = AppliedScale > UE_KINDA_SMALL_NUMBER ? 1.0 / AppliedScale : 1.0;
		Widget->SetRenderScale(FVector(CurrentScale.X, CurrentScale.Y * Undo, CurrentScale.Z * Undo));
		AppliedTilt = FRotator::ZeroRotator;
		AppliedScale = 1.0f;
	}
	Super::OnUnregister();
}

void UDreamTiltCard::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Step(DeltaTime);
}

void UDreamTiltCard::Step(float InDeltaTime)
{
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return;
	}
	FVector2D Target = bEnabled ? TargetTilt : FVector2D::ZeroVector;
	bool bHover = bEnabled && !bFollowPointer && !TargetTilt.IsNearlyZero();
	if (bEnabled && bFollowPointer)
	{
		// Measured on the plane the card is laid out on, not the one it is tilted to: its own tilt must not move the pointer.
		Target = FVector2D::ZeroVector;
		bHover = false;
		const UDreamWidget* Parent = Widget->GetParent();
		FVector Local;
		bool bFound = false;
		if (Parent != nullptr)
		{
			FVector InParent;
			bFound = DreamPointerOnPlane::Find(Parent, UserIndex, PointerId, InParent);
			Local = bFound ? Widget->GetLocalTransform().InverseTransformPosition(InParent) : FVector::ZeroVector;
		}
		else
		{
			bFound = DreamPointerOnPlane::Find(Widget, UserIndex, PointerId, Local);
		}
		if (bFound)
		{
			const FVector2D Centre = Widget->GetLocalSpaceCenter();
			const FVector2D Half(FMath::Max(Widget->GetWidth() * 0.5, 1.0), FMath::Max(Widget->GetHeight() * 0.5, 1.0));
			const FVector2D Offset((Local.Y - Centre.X) / Half.X, (Local.Z - Centre.Y) / Half.Y);
			if (FMath::Abs(Offset.X) <= 1.0 && FMath::Abs(Offset.Y) <= 1.0)
			{
				Target = Offset;
				bHover = true;
			}
		}
	}
	// A damped spring, in steps no longer than a 120th of a second so that a long frame does not throw it.
	float Remaining = FMath::Clamp(InDeltaTime, 0.0f, 0.25f);
	const float ScaleTarget = bHover ? HoverScale : 1.0f;
	while (Remaining > 0.0f)
	{
		const float Dt = FMath::Min(Remaining, 1.0f / 120.0f);
		Remaining -= Dt;
		const FVector2D Acceleration = (Target - Tilt) * Stiffness - TiltVelocity * Damping;
		TiltVelocity += Acceleration * Dt;
		Tilt += TiltVelocity * Dt;
		const float ScaleAcceleration = (ScaleTarget - Scale) * Stiffness - ScaleVelocity * Damping;
		ScaleVelocity += ScaleAcceleration * Dt;
		Scale += ScaleVelocity * Dt;
	}
	if (InDeltaTime <= 0.0f)
	{
		Tilt = Target;
		Scale = ScaleTarget;
		TiltVelocity = FVector2D::ZeroVector;
		ScaleVelocity = 0.0f;
	}
	// The card turns to face the pointer: the edge under it dips away from the viewer, the same way on either axis. A positive
	// yaw brings the right edge towards -X, the viewer's side, and a positive pitch the top edge, so both are the tilt negated.
	// On top of the card's own rotation and scale: last step's share is taken off what the widget has now, which also keeps
	// whatever else has turned or scaled it since, and this step's put on.
	const FRotator NewTilt(static_cast<float>(-Tilt.Y * MaxAngle), static_cast<float>(-Tilt.X * MaxAngle), 0.0f);
	const FRotator Current = Widget->GetRenderRotation();
	Widget->SetRenderRotation(FRotator(Current.Pitch - AppliedTilt.Pitch + NewTilt.Pitch, Current.Yaw - AppliedTilt.Yaw + NewTilt.Yaw, Current.Roll));
	AppliedTilt = NewTilt;
	const FVector CurrentScale = Widget->GetRenderScale();
	const double ScaleChange = (AppliedScale > UE_KINDA_SMALL_NUMBER ? 1.0 / AppliedScale : 1.0) * Scale;
	Widget->SetRenderScale(FVector(CurrentScale.X, CurrentScale.Y * ScaleChange, CurrentScale.Z * ScaleChange));
	AppliedScale = Scale;
	if (!bWarnedNoPerspective && !Widget->HasInheritedPerspective() && !Tilt.IsNearlyZero(0.01))
	{
		bWarnedNoPerspective = true;
		UE_LOG(DreamGUI, Log, TEXT("Tilt card '%s' is in no perspective scope: it turns without foreshortening. Turn Perspective on on a parent."), *Widget->GetDisplayName());
	}
}
