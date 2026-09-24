// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIWorldContext.h"
#include "DreamGUI.h"
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

namespace DreamUIRenderTargetInteractionLocal
{
	/**
	 * The pointer clock (UDreamEventSystem::GetPointerClockSeconds), or zero when there is no world to
	 * ask.
	 *
	 * The same clock the outer pointer's own stamps are on, so the pointer this component synthesises
	 * for the UI on the texture is timed exactly like the one that hit the surface -- a press held on
	 * the surface in a paused game is as old as it really is. Zero is not a plausible timestamp so
	 * much as a harmless one: the readers of these stamps (the base class's hold-to-drag test) decline
	 * to measure at all when the world they would measure against is missing, so a stamp taken without
	 * a world is never subtracted from anything. What matters here is that a press arriving outside a
	 * world writes a defined value instead of dereferencing null.
	 */
	double PointerClockSeconds(const UObject* InObject)
	{
		return UDreamEventSystem::GetPointerClockSeconds(InObject);
	}
}

UDreamPointerEventData* UDreamUIRenderTargetInteraction::EnsurePointerEventData()
{
	if (!IsValid(PointerEventData))
	{
		PointerEventData = NewObject<UDreamPointerEventData>(this);
		PointerEventData->PointerID = -1;//make it -1, different from DreamGUIEventSystem created
	}
	return PointerEventData;
}

void UDreamUIRenderTargetInteraction::BeginPlay()
{
	Super::BeginPlay();
	// Still built here, so the ordinary case pays for it once at a moment nobody is waiting on
	// input. EnsurePointerEventData exists for the cases that get here first, not instead of this.
	EnsurePointerEventData();
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

	// LineTrace writes through PointerEventData before it reads anything back, and this tick is
	// enabled from the constructor rather than from BeginPlay, so the same ordering question the
	// pointer handlers have applies here too.
	EnsurePointerEventData();

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
	// The pointer feeding this component describes the surface only while it is ON the surface -- or
	// holding a press it began here, which the UI on the texture is owed the release of. Past its Exit
	// its ray and hit point are about whatever it is over now, a panel or a wall or a screen widget,
	// and reading a UV off those put the inner pointer at a meaningless spot on the texture: it went on
	// hovering the UI drawn there after the pointer had left. Reporting no hit instead is what gives the
	// inner pointer its own Exit, through ProcessPointerEvent's no-hit branch.
	if (!bInputPointerOverSurface && !(IsValid(PointerEventData) && PointerEventData->bNowIsTriggerPressed))return false;
	if (InputPointerEventData->Raycaster == nullptr)return false;
	auto RayOrigin = InputPointerEventData->Raycaster->GetRayOrigin();
	auto RayDirection = InputPointerEventData->Raycaster->GetRayDirection();

	// The WORLD ray's end, for the trace against the surface.
	auto RayEnd = RayOrigin + RayDirection * RayLength;

	FVector2D HitUV;
	if (IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(LineTraceSource, InputPointerEventData->FaceIndex, InputPointerEventData->WorldPoint, RayOrigin, RayEnd, HitUV))
	{
		auto ViewProjectionMatrix = TargetCanvas->GetViewProjectionMatrix();
		FVector2D mousePos01 = HitUV;
		PointerEventData->PointerPosition = FVector(mousePos01 * TargetCanvas->GetViewportSize(), 0);

		FVector OutRayOrigin, OutRayDirection;
		UDreamScreenSpaceRaycaster::DeprojectViewPointToWorld(ViewProjectionMatrix, mousePos01, OutRayOrigin, OutRayDirection);

		// The CANVAS ray's own end. This used to be the world ray's end point, which made the canvas
		// segment run from the canvas's eye toward wherever the world ray was going: the canvas ray
		// bent toward the world ray's direction, invisible for a point straight ahead of both cameras
		// and off by more the further from the middle of the surface the pointer was.
		FVector CanvasRayEnd = OutRayOrigin + OutRayDirection * RayLength;

		TArray<FDreamUIHitResult> HitResultArray;
		this->Raycast(PointerEventData, OutRayOrigin, OutRayDirection, CanvasRayEnd, HitResultArray);
		if (HitResultArray.Num() > 0)
		{
			FDreamUIHitResultContainer DreamHitResult;
			DreamHitResult.HitResult = HitResultArray[0];
			DreamHitResult.Raycaster = this;
			DreamHitResult.RayOrigin = OutRayOrigin;
			DreamHitResult.RayDirection = OutRayDirection;
			DreamHitResult.RayEnd = CanvasRayEnd;
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

void UDreamUIRenderTargetInteraction::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	return Super::RaycastUI(InPointerEventData, TargetCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}


bool UDreamUIRenderTargetInteraction::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	InputPointerEventData = EventData;
	bInputPointerOverSurface = true;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	// Only the pointer this component follows can take it off the surface; another pointer leaving
	// says nothing about the one it is reading. The pointer itself is kept: a press it began here is
	// still owed its release (see LineTrace).
	if (EventData == InputPointerEventData.Get())
	{
		bInputPointerOverSurface = false;
	}
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	using namespace DreamUIRenderTargetInteractionLocal;
	UDreamPointerEventData* Synthesised = EnsurePointerEventData();
	Synthesised->PressPointerPosition = Synthesised->PointerPosition;
	Synthesised->PressTime = PointerClockSeconds(this);
	Synthesised->bNowIsTriggerPressed = true;
	Synthesised->MouseButtonType = EventData->MouseButtonType;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	// Pressed as the press it is -- see the declaration.
	return IDreamPointerDownUpInterface::Execute_OnPointerDown(this, EventData);
}
bool UDreamUIRenderTargetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	using namespace DreamUIRenderTargetInteractionLocal;
	UDreamPointerEventData* Synthesised = EnsurePointerEventData();
	Synthesised->ReleaseTime = PointerClockSeconds(this);
	Synthesised->bNowIsTriggerPressed = false;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	auto inAxisValue = EventData->ScrollAxisValue;
	UDreamPointerEventData* Synthesised = EnsurePointerEventData();
	if (IsValid(Synthesised->EnterWidget))
	{
		if (inAxisValue != FVector2D::ZeroVector || Synthesised->ScrollAxisValue != inAxisValue)
		{
			Synthesised->ScrollAxisValue = inAxisValue;
			UDreamEventSystem::ExecuteEvent_OnPointerScroll(Synthesised->EnterWidget, Synthesised, true);
		}
	}
	return bAllowEventBubbleUp;
}

#undef LOCTEXT_NAMESPACE