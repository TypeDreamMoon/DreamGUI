// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LGUIRenderTargetInteraction.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "LGUI.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Extensions/LGUIRenderTargetGeometrySource.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "Engine/World.h"
#include "Event/LexEventSystem.h"
#include "Event/InputModule/LexPointerInputModule.h"

#define LOCTEXT_NAMESPACE "LGUIRenderTargetInteraction"

ULGUIRenderTargetInteraction::ULGUIRenderTargetInteraction()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void ULGUIRenderTargetInteraction::BeginPlay()
{
	Super::BeginPlay();
	PointerEventData = NewObject<ULexPointerEventData>(this);
	PointerEventData->PointerID = -1;//make it -1, different from LGUIEventSystem created
}

void ULGUIRenderTargetInteraction::OnRegister()
{
	Super::OnRegister();
}

void ULGUIRenderTargetInteraction::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(LineTraceSource))
	{
		LineTraceSource = GetOwner()->FindComponentByInterface(ULGUIRenderTargetInteractionSourceInterface::StaticClass());
		if (!IsValid(LineTraceSource))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d InteractionSource is not valid! LGUIRenderTargetInteraction need a valid component which inherit %s on the same actor!")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ULGUIRenderTargetInteractionSourceInterface::StaticClass()->GetName()));
			return;
		}
	}
	if (!TargetCanvas.IsValid())
	{
		TargetCanvas = ILGUIRenderTargetInteractionSourceInterface::Execute_GetTargetCanvas(LineTraceSource);
		if (!TargetCanvas.IsValid())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d TargetCanvas is not valid! LGUIRenderTargetInteraction need to get a vaild LGUICanvas from InteractionSource!")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
	}
	if (!InputPointerEventData.IsValid())
		return;

	FLexUIHitResult hitResultContainer;
	bool lineTraceHitSomething = LineTrace(hitResultContainer);
	bool resultHitSomething = false;
	FHitResult hitResult;
	ULexPointerInputModule::ProcessPointerEvent(nullptr, PointerEventData, lineTraceHitSomething, hitResultContainer, resultHitSomething, hitResult);
}

void ULGUIRenderTargetInteraction::ActivateRaycaster()
{
	//skip Activate && Deactivate, because ULGUIRenderTargetInteraction will process input and interaction by itself
}
void ULGUIRenderTargetInteraction::DeactivateRaycaster()
{
	
}

bool ULGUIRenderTargetInteraction::LineTrace(FLexUIHitResult& OutHitResult)
{
	if (InputPointerEventData->Raycaster == nullptr)return false;
	auto RayOrigin = InputPointerEventData->Raycaster->GetRayOrigin();
	auto RayDirection = InputPointerEventData->Raycaster->GetRayDirection();

	auto RayEnd = RayOrigin + RayDirection * RayLength;

	FVector2D HitUV;
	if (ILGUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(LineTraceSource, InputPointerEventData->FaceIndex, InputPointerEventData->WorldPoint, RayOrigin, RayEnd, HitUV))
	{
		auto ViewProjectionMatrix = TargetCanvas->GetViewProjectionMatrix();
		FVector2D mousePos01 = HitUV;
		PointerEventData->PointerPosition = FVector(mousePos01 * TargetCanvas->GetViewportSize(), 0);

		FVector OutRayOrigin, OutRayDirection;
		ULexScreenSpaceRaycaster::DeprojectViewPointToWorld(ViewProjectionMatrix, mousePos01, OutRayOrigin, OutRayDirection);

		TArray<FHitResult> HitResultArray;
		this->Raycast(PointerEventData, OutRayOrigin, OutRayDirection, RayEnd, HitResultArray);
		if (HitResultArray.Num() > 0)
		{
			FLexUIHitResult LexHitResult;
			LexHitResult.HitResult = HitResultArray[0];
			LexHitResult.EventFireType = this->GetEventFireType();
			LexHitResult.Raycaster = this;
			LexHitResult.RayOrigin = OutRayOrigin;
			LexHitResult.RayDirection = OutRayDirection;
			LexHitResult.RayEnd = RayEnd;
			for (auto& HitItem : HitResultArray)
			{
				LexHitResult.HoverArray.Add(HitItem.Component.Get());
			}
			OutHitResult = LexHitResult;
		}

		return true;
	}
	return false;
}

bool ULGUIRenderTargetInteraction::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
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
void ULGUIRenderTargetInteraction::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FHitResult>& OutHitResultArray)
{
	return Super::RaycastUI(InPointerEventData, TargetCanvas.Get(), TOptional<ETraceTypeQuery>(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}


bool ULGUIRenderTargetInteraction::OnPointerEnter_Implementation(ULexPointerEventData* eventData)
{
	InputPointerEventData = eventData;
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerExit_Implementation(ULexPointerEventData* eventData)
{
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerDown_Implementation(ULexPointerEventData* eventData)
{
	PointerEventData->PressPointerPosition = PointerEventData->PointerPosition;
	PointerEventData->PressTime = GetWorld()->TimeSeconds;
	PointerEventData->bNowIsTriggerPressed = true;
	PointerEventData->MouseButtonType = eventData->MouseButtonType;
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerUp_Implementation(ULexPointerEventData* eventData)
{
	PointerEventData->ReleaseTime = GetWorld()->TimeSeconds;
	PointerEventData->bNowIsTriggerPressed = false;
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerScroll_Implementation(ULexPointerEventData* eventData)
{
	auto inAxisValue = eventData->ScrollAxisValue;
	if (IsValid(PointerEventData->EnterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || PointerEventData->ScrollAxisValue != inAxisValue)
		{
			PointerEventData->ScrollAxisValue = inAxisValue;
			ULexEventSystem::ExecuteEvent_OnPointerScroll(PointerEventData->EnterComponent, PointerEventData, PointerEventData->EnterComponentEventFireType, true);
		}
	}
	return bAllowEventBubbleUp;
}

#undef LOCTEXT_NAMESPACE