// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/InputModule/DreamPointerInputModule.h"

#define LOCTEXT_NAMESPACE "DreamGUIRenderTargetInteraction"

UDreamUIRenderTargetInteraction::UDreamUIRenderTargetInteraction()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UDreamUIRenderTargetInteraction::BeginPlay()
{
	Super::BeginPlay();
	PointerEventData = NewObject<UDreamPointerEventData>(this);
	PointerEventData->PointerID = -1;//make it -1, different from DreamGUIEventSystem created
}

void UDreamUIRenderTargetInteraction::OnRegister()
{
	Super::OnRegister();
}

void UDreamUIRenderTargetInteraction::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(LineTraceSource))
	{
		LineTraceSource = GetOwner()->FindComponentByInterface(UDreamUIRenderTargetInteractionSourceInterface::StaticClass());
		if (!IsValid(LineTraceSource))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d InteractionSource is not valid! DreamGUIRenderTargetInteraction need a valid component which inherit %s on the same actor!")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(UDreamUIRenderTargetInteractionSourceInterface::StaticClass()->GetName()));
			return;
		}
	}
	if (!TargetCanvas.IsValid())
	{
		TargetCanvas = IDreamUIRenderTargetInteractionSourceInterface::Execute_GetTargetCanvas(LineTraceSource);
		if (!TargetCanvas.IsValid())
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d TargetCanvas is not valid! DreamGUIRenderTargetInteraction need to get a vaild DreamGUICanvas from InteractionSource!")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
	}
	if (!InputPointerEventData.IsValid())
		return;

	FDreamUIHitResultContainer hitResultContainer;
	bool lineTraceHitSomething = LineTrace(hitResultContainer);
	bool resultHitSomething = false;
	FDreamUIHitResult hitResult;
	UDreamPointerInputModule::ProcessPointerEvent(nullptr, PointerEventData, lineTraceHitSomething, hitResultContainer, resultHitSomething, hitResult);
}

void UDreamUIRenderTargetInteraction::ActivateRaycaster()
{
	//skip Activate && Deactivate, because UDreamGUIRenderTargetInteraction will process input and interaction by itself
}
void UDreamUIRenderTargetInteraction::DeactivateRaycaster()
{
	
}

bool UDreamUIRenderTargetInteraction::LineTrace(FDreamUIHitResultContainer& OutHitResult)
{
	if (InputPointerEventData->Raycaster == nullptr)return false;
	auto RayOrigin = InputPointerEventData->Raycaster->GetRayOrigin();
	auto RayDirection = InputPointerEventData->Raycaster->GetRayDirection();

	auto RayEnd = RayOrigin + RayDirection * RayLength;

	FVector2D HitUV;
	if (IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(LineTraceSource, InputPointerEventData->FaceIndex, InputPointerEventData->WorldPoint, RayOrigin, RayEnd, HitUV))
	{
		auto ViewProjectionMatrix = TargetCanvas->GetViewProjectionMatrix();
		FVector2D mousePos01 = HitUV;
		PointerEventData->PointerPosition = FVector(mousePos01 * TargetCanvas->GetViewportSize(), 0);

		FVector OutRayOrigin, OutRayDirection;
		UDreamScreenSpaceRaycaster::DeprojectViewPointToWorld(ViewProjectionMatrix, mousePos01, OutRayOrigin, OutRayDirection);

		TArray<FDreamUIHitResult> HitResultArray;
		this->Raycast(PointerEventData, OutRayOrigin, OutRayDirection, RayEnd, HitResultArray);
		if (HitResultArray.Num() > 0)
		{
			FDreamUIHitResultContainer DreamHitResult;
			DreamHitResult.HitResult = HitResultArray[0];
			DreamHitResult.Raycaster = this;
			DreamHitResult.RayOrigin = OutRayOrigin;
			DreamHitResult.RayDirection = OutRayDirection;
			DreamHitResult.RayEnd = RayEnd;
			for (auto& HitItem : HitResultArray)
			{
				DreamHitResult.HoverArray.Add(HitItem.Widget.Get());
			}
			OutHitResult = DreamHitResult;
		}

		return true;
	}
	return false;
}

bool UDreamUIRenderTargetInteraction::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	if (bHoldToDrag)
	{
		if (GetWorld()->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	FVector2D mousePos = FVector2D(InPointerEventData->PointerPosition);
	FVector2D pressMousePos = FVector2D(InPointerEventData->PressPointerPosition);
	return FVector2D::DistSquared(pressMousePos, mousePos) > DragThresholdSquare;
}
void UDreamUIRenderTargetInteraction::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	return Super::RaycastUI(InPointerEventData, TargetCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}


bool UDreamUIRenderTargetInteraction::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	InputPointerEventData = EventData;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	PointerEventData->PressPointerPosition = PointerEventData->PointerPosition;
	PointerEventData->PressTime = GetWorld()->TimeSeconds;
	PointerEventData->bNowIsTriggerPressed = true;
	PointerEventData->MouseButtonType = EventData->MouseButtonType;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	PointerEventData->ReleaseTime = GetWorld()->TimeSeconds;
	PointerEventData->bNowIsTriggerPressed = false;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	auto inAxisValue = EventData->ScrollAxisValue;
	if (IsValid(PointerEventData->EnterWidget))
	{
		if (inAxisValue != FVector2D::ZeroVector || PointerEventData->ScrollAxisValue != inAxisValue)
		{
			PointerEventData->ScrollAxisValue = inAxisValue;
			UDreamEventSystem::ExecuteEvent_OnPointerScroll(PointerEventData->EnterWidget, PointerEventData, true);
		}
	}
	return bAllowEventBubbleUp;
}

#undef LOCTEXT_NAMESPACE