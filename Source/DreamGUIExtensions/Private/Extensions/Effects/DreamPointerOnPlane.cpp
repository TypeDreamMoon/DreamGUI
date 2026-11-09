// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPointerOnPlane.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamBaseRaycaster.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"

bool DreamPointerOnPlane::Find(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId, FVector& OutLocal)
{
	if (InWidget == nullptr)
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InWidget);
	UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
	UDreamPointerEventData* Pointer = User != nullptr ? User->FindPointerEventData(InPointerId) : nullptr;
	if (Pointer == nullptr || Pointer->Raycaster == nullptr)
	{
		return false;
	}
	FVector Origin, Direction, End;
	float Length = 0.0f;
	if (!Pointer->Raycaster->GenerateRay(Pointer, Origin, Direction, End, Length))
	{
		return false;
	}
	// The widget's plane is x = 0 in its own space.
	const FMatrix ToLocal = InWidget->GetInverseWorldMatrix();
	const FVector LocalOrigin = ToLocal.TransformPosition(Origin);
	const FVector LocalDirection = ToLocal.TransformVector(Direction);
	if (FMath::Abs(LocalDirection.X) < UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const double T = -LocalOrigin.X / LocalDirection.X;
	if (T < 0.0)
	{
		return false;
	}
	OutLocal = LocalOrigin + LocalDirection * T;
	return true;
}
