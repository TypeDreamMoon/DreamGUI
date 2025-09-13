// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexPointerEventData.h"
#include "Event/LexBaseRaycaster.h"
#include "LGUI.h"
#include "GameFramework/Actor.h"
#include "Core/Components/LexWidget.h"

void ULexPointerEventData::SetHighlightedComponentForNavigation(USceneComponent* InComp)
{
	this->highlightComponentForNavigation = InComp;
	this->navigateTickTime = 0;//trigger on next navigation process
}

bool ULexPointerEventData::IsPointerOverUI()
{
	if (this->enterComponentStack.Num() > 0)
	{
		auto firstEnterComp = this->enterComponentStack[0];
		if (auto UIItem = Cast<ULexWidget>(firstEnterComp))
		{
			return true;
		}
	}
	return false;
}

FVector ULexPointerEventData::GetWorldPointInPlane()const
{
	if (IsValid(pressRaycaster))
	{
		return FMath::LinePlaneIntersection(pressRaycaster->GetRayOrigin(), pressRaycaster->GetRayOrigin() + pressRaycaster->GetRayDirection() * pressRaycaster->GetRayLength(), pressWorldPoint, pressWorldNormal);
	}
	else
	{
		return worldPoint;
	}
}
FVector ULexPointerEventData::GetLocalPointInPlane()const
{
	return pressWorldToLocalTransform.TransformPosition(GetWorldPointInPlane());
}
FVector ULexPointerEventData::GetWorldPointSpherical()const
{
	if (IsValid(pressRaycaster))
	{
		return pressRaycaster->GetRayOrigin() + pressRaycaster->GetRayDirection() * pressDistance;
	}
	else
	{
		return worldPoint;
	}
}
FVector ULexPointerEventData::GetDragRayOrigin()const
{
	if (IsValid(pressRaycaster))
	{
		return pressRaycaster->GetRayOrigin();
	}
	else
	{
		return FVector::ZeroVector;
	}
}
FVector ULexPointerEventData::GetDragRayDirection()const
{
	if (IsValid(pressRaycaster))
	{
		return pressRaycaster->GetRayDirection();
	}
	else
	{
		return FVector(1, 0, 0);
	}
}
FVector ULexPointerEventData::GetCumulativeMoveDelta()const
{
	return GetWorldPointSpherical() - pressWorldPoint;
}

FString ULexPointerEventData::ToString()const
{
	FString result;
	if (IsValid(enterComponent))
	{
		result += FString::Printf(TEXT("\n		enterComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (enterComponent->GetOwner()->GetActorLabel()),
#else
			* (enterComponent->GetOwner()->GetName()),
#endif
			* (enterComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		enterComponent is null");
	}
	if (enterComponentStack.Num() > 0)
	{
		result += FString::Printf(TEXT("\n		enterActorStack count:%d"), enterComponentStack.Num());
	}
	else
	{
		result += TEXT("\n		enterActorStack empty");
	}
	if (IsValid(dragComponent))
	{
		result += FString::Printf(TEXT("\n		dragComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (dragComponent->GetOwner()->GetActorLabel()),
#else
			* (dragComponent->GetOwner()->GetName()),
#endif
			* (dragComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		dragComponent is null");
	}
	result += FString::Printf(TEXT("\n		worldPoint:%s"), *(worldPoint.ToString()));
	result += FString::Printf(TEXT("\n		moveDelta:%s"), *(worldNormal.ToString()));

	result += FString::Printf(TEXT("\n		scrollAxisValue:%s"), *scrollAxisValue.ToString());

	result += FString::Printf(TEXT("\n		raycaster:%s"), *(IsValid(raycaster) ? raycaster->GetName() : TEXT("null")));
	switch (mouseButtonType)
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

	result += FString::Printf(TEXT("\n		pressComponent:%s"), *(IsValid(pressComponent) ? pressComponent->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		pressWorldPoint:%s"), *(pressWorldPoint.ToString()));
	result += FString::Printf(TEXT("\n		pressWorldNormal:%s"), *(pressWorldNormal.ToString()));
	result += FString::Printf(TEXT("\n		pressDistance:%f"), pressDistance);
	result += FString::Printf(TEXT("\n		pressRayOrigin:%s"), *(pressRayOrigin.ToString()));
	result += FString::Printf(TEXT("\n		pressRayDirection:%s"), *(pressRayDirection.ToString()));
	result += FString::Printf(TEXT("\n		pressRaycaster:%s"), *(IsValid(pressRaycaster) ? pressRaycaster->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		clickTime:%f"), clickTime);
	result += FString::Printf(TEXT("\n		pressTime:%f"), pressTime);

	result += FString::Printf(TEXT("\n		isDragging:%s"), isDragging ? TEXT("true") : TEXT("false"));
	result += FString::Printf(TEXT("\n		dragComponent:%s"), *(IsValid(dragComponent) ? dragComponent->GetName() : TEXT("null")));

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



	if (IsValid(pressComponent))
	{
		result += FString::Printf(TEXT("\n		pressHitComponent actor:%s, comp:%s"),
#if WITH_EDITOR
			* (pressComponent->GetOwner()->GetActorLabel()),
#else
			* (pressComponent->GetOwner()->GetName()),
#endif
			* (pressComponent->GetPathName()));
	}
	else
	{
		result += TEXT("\n		pressHitComponent is null");
	}
	result += FString::Printf(TEXT("\n		pressWorldPoint:%s"), *(pressWorldPoint.ToString()));
	result += FString::Printf(TEXT("\n		pressWorldNormal:%s"), *(pressWorldNormal.ToString()));
	result += FString::Printf(TEXT("\n		pressDistance:%f"), pressDistance);
	result += FString::Printf(TEXT("\n		pressRayOrigin:%s"), *(pressRayOrigin.ToString()));
	result += FString::Printf(TEXT("\n		pressRayDirection:%s"), *(pressRayDirection.ToString()));
	result += FString::Printf(TEXT("\n		pressRaycaster:%s"), *(IsValid(pressRaycaster) ? pressRaycaster->GetName() : TEXT("null")));
	result += FString::Printf(TEXT("\n		pressTime:%f"), clickTime);

	return result;
}
