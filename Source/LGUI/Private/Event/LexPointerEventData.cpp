// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexPointerEventData.h"
#include "Event/LexBaseRaycaster.h"
#include "LGUI.h"
#include "GameFramework/Actor.h"
#include "Core/Components/LexWidget.h"

void ULexPointerEventData::SetHighlightedComponentForNavigation(USceneComponent* InComp)
{
	this->HighlightComponentForNavigation = InComp;
	this->NavigateTickTime = 0;//trigger on next navigation process
}

bool ULexPointerEventData::IsPointerOverUI()
{
	if (this->EnterComponentStack.Num() > 0)
	{
		auto firstEnterComp = this->EnterComponentStack[0];
		if (auto UIItem = Cast<ULexWidget>(firstEnterComp))
		{
			return true;
		}
	}
	return false;
}

FVector ULexPointerEventData::GetWorldPointInPlane()const
{
	if (IsValid(PressRaycaster))
	{
		return FMath::RayPlaneIntersection(PressRaycaster->GetRayOrigin(), PressRaycaster->GetRayDirection(), FPlane(PressWorldPoint, PressWorldNormal));
	}
	else
	{
		return WorldPoint;
	}
}
FVector ULexPointerEventData::GetLocalPointInPlane()const
{
	return PressWorldToLocalTransform.TransformPosition(GetWorldPointInPlane());
}
FVector ULexPointerEventData::GetWorldPointSpherical()const
{
	if (IsValid(PressRaycaster))
	{
		return PressRaycaster->GetRayOrigin() + PressRaycaster->GetRayDirection() * PressDistance;
	}
	else
	{
		return WorldPoint;
	}
}
FVector ULexPointerEventData::GetDragRayOrigin()const
{
	if (IsValid(PressRaycaster))
	{
		return PressRaycaster->GetRayOrigin();
	}
	else
	{
		return FVector::ZeroVector;
	}
}
FVector ULexPointerEventData::GetDragRayDirection()const
{
	if (IsValid(PressRaycaster))
	{
		return PressRaycaster->GetRayDirection();
	}
	else
	{
		return FVector(1, 0, 0);
	}
}
FVector ULexPointerEventData::GetCumulativeMoveDelta()const
{
	return GetWorldPointSpherical() - PressWorldPoint;
}

FString ULexPointerEventData::ToString()const
{
	FString result;
	if (IsValid(EnterComponent))
	{
		result += FString::Printf(TEXT("\n		enterComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (EnterComponent->GetOwner()->GetActorLabel()),
#else
			* (enterComponent->GetOwner()->GetName()),
#endif
			* (EnterComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		enterComponent is null");
	}
	if (EnterComponentStack.Num() > 0)
	{
		result += FString::Printf(TEXT("\n		enterActorStack count:%d"), EnterComponentStack.Num());
	}
	else
	{
		result += TEXT("\n		enterActorStack empty");
	}
	if (IsValid(DragComponent))
	{
		result += FString::Printf(TEXT("\n		dragComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (DragComponent->GetOwner()->GetActorLabel()),
#else
			* (dragComponent->GetOwner()->GetName()),
#endif
			* (DragComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		dragComponent is null");
	}
	result += FString::Printf(TEXT("\n		worldPoint:%s"), *(WorldPoint.ToString()));
	result += FString::Printf(TEXT("\n		moveDelta:%s"), *(WorldNormal.ToString()));

	result += FString::Printf(TEXT("\n		scrollAxisValue:%s"), *ScrollAxisValue.ToString());

	result += FString::Printf(TEXT("\n		raycaster:%s"), *(IsValid(Raycaster) ? Raycaster->GetName() : TEXT("null")));
	switch (MouseButtonType)
	{
	case ELexUIMouseButtonType::Left:
		result += TEXT("\n		mouseButtonType:Left");
		break;
	case ELexUIMouseButtonType::Middle:
		result += TEXT("\n		mouseButtonType:Middle");
		break;
	case ELexUIMouseButtonType::Right:
		result += TEXT("\n		mouseButtonType:Right");
		break;
	}

	result += FString::Printf(TEXT("\n		pressComponent:%s"), *(IsValid(PressComponent) ? PressComponent->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		pressWorldPoint:%s"), *(PressWorldPoint.ToString()));
	result += FString::Printf(TEXT("\n		pressWorldNormal:%s"), *(PressWorldNormal.ToString()));
	result += FString::Printf(TEXT("\n		pressDistance:%f"), PressDistance);
	result += FString::Printf(TEXT("\n		pressRayOrigin:%s"), *(PressRayOrigin.ToString()));
	result += FString::Printf(TEXT("\n		pressRayDirection:%s"), *(PressRayDirection.ToString()));
	result += FString::Printf(TEXT("\n		pressRaycaster:%s"), *(IsValid(PressRaycaster) ? PressRaycaster->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		clickTime:%f"), ClickTime);
	result += FString::Printf(TEXT("\n		pressTime:%f"), PressTime);

	result += FString::Printf(TEXT("\n		isDragging:%s"), bIsDragging ? TEXT("true") : TEXT("false"));
	result += FString::Printf(TEXT("\n		dragComponent:%s"), *(IsValid(DragComponent) ? DragComponent->GetName() : TEXT("null")));

	switch (EventType)
	{
	case ELexUIPointerEventType::Click:
		result += TEXT("\n		eventType:Click");
		break;
	case ELexUIPointerEventType::Enter:
		result += TEXT("\n		eventType:Enter");
		break;
	case ELexUIPointerEventType::Exit:
		result += TEXT("\n		eventType:Exit");
		break;
	case ELexUIPointerEventType::Down:
		result += TEXT("\n		eventType:Down");
		break;
	case ELexUIPointerEventType::Up:
		result += TEXT("\n		eventType:Up");
		break;
	case ELexUIPointerEventType::BeginDrag:
		result += TEXT("\n		eventType:BeginDrag");
		break;
	case ELexUIPointerEventType::Drag:
		result += TEXT("\n		eventType:Drag");
		break;
	case ELexUIPointerEventType::EndDrag:
		result += TEXT("\n		eventType:EndDrag");
		break;
	case ELexUIPointerEventType::Scroll:
		result += TEXT("\n		eventType:Scroll");
		break;
	case ELexUIPointerEventType::DragDrop:
		result += TEXT("\n		eventType:DragDrop");
		break;
	case ELexUIPointerEventType::Select:
		result += TEXT("\n		eventType:Select");
		break;
	case ELexUIPointerEventType::Deselect:
		result += TEXT("\n		eventType:Deselect");
		break;
	}



	if (IsValid(PressComponent))
	{
		result += FString::Printf(TEXT("\n		pressHitComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (PressComponent->GetOwner()->GetActorLabel()),
#else
			* (pressComponent->GetOwner()->GetName()),
#endif
			* (PressComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		pressHitComponent is null");
	}
	result += FString::Printf(TEXT("\n		pressWorldPoint:%s"), *(PressWorldPoint.ToString()));
	result += FString::Printf(TEXT("\n		pressWorldNormal:%s"), *(PressWorldNormal.ToString()));
	result += FString::Printf(TEXT("\n		pressDistance:%f"), PressDistance);
	result += FString::Printf(TEXT("\n		pressRayOrigin:%s"), *(PressRayOrigin.ToString()));
	result += FString::Printf(TEXT("\n		pressRayDirection:%s"), *(PressRayDirection.ToString()));
	result += FString::Printf(TEXT("\n		pressRaycaster:%s"), *(IsValid(PressRaycaster) ? PressRaycaster->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		pressTime:%f"), ClickTime);

	return result;
}
