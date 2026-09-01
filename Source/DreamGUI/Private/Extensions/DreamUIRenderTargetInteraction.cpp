// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIWorldContext.h"
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

namespace DreamUIRenderTargetInteractionLocal
{
	/**
	 * The owning world's clock, or zero when there is no world to ask.
	 *
	 * Zero is not a plausible timestamp so much as a harmless one: the only reader of these stamps
	 * is the base class's hold-to-drag test, which declines to measure a hold at all when the world
	 * it would measure against is missing, so a stamp taken without a world is never subtracted from
	 * anything. What matters here is that a press arriving outside a world writes a defined value
	 * instead of dereferencing null.
	 */
	double WorldTimeSeconds(const UObject* InObject)
	{
		const UWorld* World = DreamUI::GetWorldSafe(InObject);
		return World != nullptr ? World->TimeSeconds : 0.0;
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
	using namespace DreamUIRenderTargetInteractionLocal;
	UDreamPointerEventData* Synthesised = EnsurePointerEventData();
	Synthesised->PressPointerPosition = Synthesised->PointerPosition;
	Synthesised->PressTime = WorldTimeSeconds(this);
	Synthesised->bNowIsTriggerPressed = true;
	Synthesised->MouseButtonType = EventData->MouseButtonType;
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	using namespace DreamUIRenderTargetInteractionLocal;
	UDreamPointerEventData* Synthesised = EnsurePointerEventData();
	Synthesised->ReleaseTime = WorldTimeSeconds(this);
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