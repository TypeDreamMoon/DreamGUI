// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexBaseRaycaster.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "Engine/World.h"

ULexBaseRaycaster::ULexBaseRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void ULexBaseRaycaster::BeginPlay()
{
	Super::BeginPlay();
}

void ULexBaseRaycaster::Activate(bool bReset)
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
void ULexBaseRaycaster::Deactivate()
{
	Super::Deactivate();
	DeactivateRaycaster();
}
void ULexBaseRaycaster::ActivateRaycaster()
{
	ULexUIManagerWorldSubsystem::AddRaycaster(this);
}
void ULexBaseRaycaster::DeactivateRaycaster()
{
	ULexUIManagerWorldSubsystem::RemoveRaycaster(this);
}
void ULexBaseRaycaster::OnUnregister()
{
	Super::OnUnregister();
	DeactivateRaycaster();
}

void ULexBaseRaycaster::RaycastUI(ULexPointerEventData* InPointerEventData, ULexCanvas* InRootCanvas,
	FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd,
	TArray<FLexUIHitResult>& OutHitResultArray)
{
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	{
		CurrentRayOrigin = OutRayOrigin;
		CurrentRayDirection = OutRayDirection;
		
		struct LOCAL
		{
			static void CollectVisualWidget(ULexCanvas* InCanvas, TArray<ULexVisual*>& OutVisualArray)
			{
				OutVisualArray.Append(InCanvas->GetVisualArray());
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					CollectVisualWidget(Child.Get(), OutVisualArray);
				}
			}
			static void ForeachCanvas(ULexCanvas* InCanvas, TFunctionRef<void(ULexCanvas*)> InFunction)
			{
				InFunction(InCanvas);
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					ForeachCanvas(Child.Get(), InFunction);
				}
			}
		};
#if 0// use ParallelFor to speed up the hit process, should be ok because it blocks game thread and we use thread lock
		TArray<ULexVisual*> VisualArray;
		LOCAL::CollectVisualWidget(InRootCanvas, VisualArray);
		FCriticalSection Mutex;
		ParallelFor(VisualArray.Num(), [&VisualArray, &Mutex, &OutHitResultArray, OutRayOrigin, OutRayEnd](int32 Index)
		{
			auto& Visual = VisualArray[Index];
			auto Widget = Visual->GetWidget();
			FLexUIHitResult ThisHit;
			ThisHit.FaceIndex = INDEX_NONE;
			if (
				Widget->GetRaycastableInHierarchy()
				&& Widget->GetWidgetActiveInHierarchy()
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
		auto TraceFunction = [&](ULexCanvas* InCanvas)
		{
			auto& VisualArray = InCanvas->GetVisualArray();
			for (auto& Visual : VisualArray)
			{
				auto Widget = Visual->GetWidget();
				FLexUIHitResult ThisHit;
				ThisHit.FaceIndex = INDEX_NONE;
				if (
					Widget->GetRaycastableInHierarchy()
					&& Widget->GetWidgetActiveInHierarchy()
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
			OutHitResultArray.Sort([](const FLexUIHitResult& A, const FLexUIHitResult& B)
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

void ULexBaseRaycaster::RaycastWorld(ULexPointerEventData* InPointerEventData, bool InRequireFaceIndex, ETraceTypeQuery InTraceChannel, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FLexUIHitResult>& OutHitResultArray)
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

void ULexBaseRaycaster::SetPointerID(int32 Value)
{
	PointerID = Value;
}
