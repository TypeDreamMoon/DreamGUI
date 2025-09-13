// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LGUIRenderTargetInteraction.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "LGUI.h"
#include "Event/LexWorldSpaceRaycaster.h"
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
	PointerEventData->pointerID = -1;//make it -1, different from LGUIEventSystem created
	RenderModeArray = { ELexRenderMode::RenderTarget };
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

bool ULGUIRenderTargetInteraction::LineTrace(FLexUIHitResult& hitResult)
{
	if (InputPointerEventData->raycaster == nullptr)return false;
	auto RayOrigin = InputPointerEventData->raycaster->GetRayOrigin();
	auto RayDirection = InputPointerEventData->raycaster->GetRayDirection();

	auto RayEnd = RayOrigin + RayDirection * RayLength;

	FVector2D HitUV;
	if (ILGUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(LineTraceSource, InputPointerEventData->faceIndex, InputPointerEventData->worldPoint, RayOrigin, RayEnd, HitUV))
	{
		auto ViewProjectionMatrix = TargetCanvas->GetViewProjectionMatrix();
		FVector2D mousePos01 = HitUV;
		PointerEventData->pointerPosition = FVector(mousePos01 * TargetCanvas->GetViewportSize(), 0);

		FVector OutRayOrigin, OutRayDirection;
		ULexScreenSpaceRaycaster::DeprojectViewPointToWorld(ViewProjectionMatrix, mousePos01, OutRayOrigin, OutRayDirection);

		FHitResult hitResultItem;
		TArray<USceneComponent*> hoverArray;//temp array for store hover components
		if (this->Raycast(PointerEventData, OutRayOrigin, OutRayDirection, RayEnd, hitResultItem, hoverArray))
		{
			FLexUIHitResult LGUIHitResult;
			LGUIHitResult.hitResult = hitResultItem;
			LGUIHitResult.eventFireType = this->GetEventFireType();
			LGUIHitResult.raycaster = this;
			LGUIHitResult.rayOrigin = OutRayOrigin;
			LGUIHitResult.rayDirection = OutRayDirection;
			LGUIHitResult.rayEnd = RayEnd;
			LGUIHitResult.hoverArray = hoverArray;

			hitResult = LGUIHitResult;
		}

		return true;
	}
	return false;
}

bool ULGUIRenderTargetInteraction::ShouldSkipCanvas(class ULexCanvas* UICanvas)
{
	if (TargetCanvas.IsValid())
	{
		return TargetCanvas->GetRenderTarget() != UICanvas->GetRootRenderTarget();
	}
	return true;
}
bool ULGUIRenderTargetInteraction::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (ShouldStartDrag_HoldToDrag(InPointerEventData))return true;
	FVector2D mousePos = FVector2D(InPointerEventData->pointerPosition);
	FVector2D pressMousePos = FVector2D(InPointerEventData->pressPointerPosition);
	return FVector2D::DistSquared(pressMousePos, mousePos) > ClickThresholdSquare;
}
bool ULGUIRenderTargetInteraction::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)
{
	return Super::RaycastUI(InPointerEventData, RenderModeArray, OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResult, OutHoverArray);
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
	PointerEventData->pressPointerPosition = PointerEventData->pointerPosition;
	PointerEventData->pressTime = GetWorld()->TimeSeconds;
	PointerEventData->nowIsTriggerPressed = true;
	PointerEventData->mouseButtonType = eventData->mouseButtonType;
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerUp_Implementation(ULexPointerEventData* eventData)
{
	PointerEventData->releaseTime = GetWorld()->TimeSeconds;
	PointerEventData->nowIsTriggerPressed = false;
	return bAllowEventBubbleUp;
}
bool ULGUIRenderTargetInteraction::OnPointerScroll_Implementation(ULexPointerEventData* eventData)
{
	auto inAxisValue = eventData->scrollAxisValue;
	if (IsValid(PointerEventData->enterComponent))
	{
		if (inAxisValue != FVector2D::ZeroVector || PointerEventData->scrollAxisValue != inAxisValue)
		{
			PointerEventData->scrollAxisValue = inAxisValue;
			ULexEventSystem::ExecuteEvent_OnPointerScroll(PointerEventData->enterComponent, PointerEventData, PointerEventData->enterComponentEventFireType, true);
		}
	}
	return bAllowEventBubbleUp;
}

#undef LOCTEXT_NAMESPACE