// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamBaseRaycaster.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Engine/World.h"

UDreamBaseRaycaster::UDreamBaseRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void UDreamBaseRaycaster::BeginPlay()
{
	Super::BeginPlay();
}

void UDreamBaseRaycaster::Activate(bool bReset)
{
	Super::Activate(bReset);
	if (this->GetWorld() == nullptr)return;
#if WITH_EDITOR
	if (this->GetWorld()->IsGameWorld())
#endif
	{
		ActivateRaycaster();
	}
}
void UDreamBaseRaycaster::Deactivate()
{
	Super::Deactivate();
	DeactivateRaycaster();
}
void UDreamBaseRaycaster::ActivateRaycaster()
{
	UDreamUIManagerWorldSubsystem::AddRaycaster(this);
}
void UDreamBaseRaycaster::DeactivateRaycaster()
{
	UDreamUIManagerWorldSubsystem::RemoveRaycaster(this);
}
void UDreamBaseRaycaster::OnUnregister()
{
	Super::OnUnregister();
	DeactivateRaycaster();
}

void UDreamBaseRaycaster::RaycastUI(UDreamPointerEventData* InPointerEventData, UDreamCanvas* InRootCanvas,
	FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd,
	TArray<FDreamUIHitResult>& OutHitResultArray)
{
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	{
		CurrentRayOrigin = OutRayOrigin;
		CurrentRayDirection = OutRayDirection;
		
		struct LOCAL
		{
			static void CollectVisualWidget(UDreamCanvas* InCanvas, TArray<UDreamVisual*>& OutVisualArray)
			{
				OutVisualArray.Append(InCanvas->GetVisualArray());
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					CollectVisualWidget(Child.Get(), OutVisualArray);
				}
			}
			static void ForeachCanvas(UDreamCanvas* InCanvas, TFunctionRef<void(UDreamCanvas*)> InFunction)
			{
				InFunction(InCanvas);
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					ForeachCanvas(Child.Get(), InFunction);
				}
			}
		};
#if 0// use ParallelFor to speed up the hit process, should be ok because it blocks game thread and we use thread lock
		TArray<UDreamVisual*> VisualArray;
		LOCAL::CollectVisualWidget(InRootCanvas, VisualArray);
		FCriticalSection Mutex;
		ParallelFor(VisualArray.Num(), [&VisualArray, &Mutex, &OutHitResultArray, OutRayOrigin, OutRayEnd](int32 Index)
		{
			auto& Visual = VisualArray[Index];
			auto Widget = Visual->GetWidget();
			FDreamUIHitResult ThisHit;
			ThisHit.FaceIndex = INDEX_NONE;
			if (
				Widget->GetRaycastableInHierarchy()
				&& Widget->GetHitTestVisibleInHierarchy()
				&& Visual->GetRaycastTarget()
				&& Visual->LineTraceUI(ThisHit, OutRayOrigin, OutRayEnd)
				)
			{
				if (Widget->IsPointVisibleOnClip(ThisHit.Location))
				{
					Mutex.Lock();
					OutHitResultArray.Add(ThisHit);
					Mutex.Unlock();
				}
			}
		});
#else
		auto TraceFunction = [&](UDreamCanvas* InCanvas)
		{
			auto& VisualArray = InCanvas->GetVisualArray();
			for (auto& Visual : VisualArray)
			{
				auto Widget = Visual->GetWidget();
				FDreamUIHitResult ThisHit;
				ThisHit.FaceIndex = INDEX_NONE;
				if (
					Widget->GetRaycastableInHierarchy()
					&& Widget->GetHitTestVisibleInHierarchy()
					&& Visual->GetRaycastTarget()
					&& Visual->LineTraceUI(ThisHit, OutRayOrigin, OutRayEnd)
					)
				{
					if (Widget->IsPointVisibleOnClip(ThisHit.Location))
					{
						OutHitResultArray.Add(ThisHit);
					}
				}
			}
		};
		LOCAL::ForeachCanvas(InRootCanvas, TraceFunction);
#endif
		
		if (OutHitResultArray.Num() > 0)
		{
			OutHitResultArray.Sort([](const FDreamUIHitResult& A, const FDreamUIHitResult& B)
			{
				auto AWidget = A.Widget.Get();
				auto BWidget = B.Widget.Get();
				if (AWidget != nullptr && BWidget != nullptr)
				{
					auto ACanvasSortOrder = AWidget->GetRenderCanvas()->GetActualSortOrder();
					auto BCanvasSortOrder = BWidget->GetRenderCanvas()->GetActualSortOrder();
					if (AWidget->GetRenderCanvas() != BWidget->GetRenderCanvas() && ACanvasSortOrder != BCanvasSortOrder)//not in same sort order
					{
						return ACanvasSortOrder > BCanvasSortOrder;
					}
					else//same Canvas, sort on item's hierarchy order
					{
						return AWidget->GetFlattenHierarchyIndex() > BWidget->GetFlattenHierarchyIndex();
					}
				}
				return true;
			});
		}
	}
}

void UDreamBaseRaycaster::RaycastWorld(UDreamPointerEventData* InPointerEventData, bool InRequireFaceIndex, ETraceTypeQuery InTraceChannel, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	check(0);
	// if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	// {
	// 	CurrentRayOrigin = OutRayOrigin;
	// 	CurrentRayDirection = OutRayDirection;
	// 	
	// 	FCollisionQueryParams queryParams = FCollisionQueryParams::DefaultQueryParam;
	// 	queryParams.bReturnFaceIndex = InRequireFaceIndex;
	// 	this->GetWorld()->LineTraceMultiByChannel(OutHitResultArray, OutRayOrigin, OutRayEnd, UEngineTypes::ConvertToCollisionChannel(InTraceChannel), queryParams);
	// }
}

void UDreamBaseRaycaster::SetPointerID(int32 Value)
{
	PointerID = Value;
}
