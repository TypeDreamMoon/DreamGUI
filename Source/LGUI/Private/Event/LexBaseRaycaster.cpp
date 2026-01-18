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
	TArray<FHitResult>& OutHitResultArray)
{
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	{
		CurrentRayOrigin = OutRayOrigin;
		CurrentRayDirection = OutRayDirection;
		
		struct LOCAL
		{
			static void CollectCanvas(ULexCanvas* InCanvas, TArray<ULexCanvas*>& OutCanvasArray)
			{
				OutCanvasArray.Add(InCanvas);
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					CollectCanvas(Child.Get(), OutCanvasArray);
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
			static void ForeachCanvas(ULexCanvas* InCanvas, ETraceTypeQuery InTraceChannel, TFunctionRef<void(ULexCanvas*)> InFunction)
			{
				if (InTraceChannel == InCanvas->GetTraceChannel())
				{
					InFunction(InCanvas);
				}
				for (auto& Child : InCanvas->GetChildrenCanvasArray())
				{
					ForeachCanvas(Child.Get(), InTraceChannel, InFunction);
				}
			}
		};
#if 1// use ParallelFor to speed up the hit process, should be ok because it blocks game thread and we use thread lock
		TArray<ULexCanvas*> CanvasArray;
		LOCAL::CollectCanvas(InRootCanvas, CanvasArray);
		FCriticalSection Mutex;
		ParallelFor(CanvasArray.Num(), [&CanvasArray, &Mutex, &OutHitResultArray, OutRayOrigin, OutRayEnd](int32 Index)
		{
			auto& VisualWidgetArray = CanvasArray[Index]->GetVisualWidgetArray();
			for (auto& VisualWidget : VisualWidgetArray)
			{
				if (!IsValid(VisualWidget))continue;

				FHitResult ThisHit;
				ThisHit.FaceIndex = INDEX_NONE;
				if (
					VisualWidget->GetRaycastableInHierarchy()
					&& VisualWidget->GetWidgetActiveInHierarchy()
					&& VisualWidget->GetVisual()
					&& VisualWidget->GetVisual()->GetRaycastTarget()
					&& VisualWidget->GetVisual()->LineTraceUI(ThisHit, OutRayOrigin, OutRayEnd)
					)
				{
					if (VisualWidget->IsPointVisibleOnClip(ThisHit.Location))
					{
						Mutex.Lock();
						OutHitResultArray.Add(ThisHit);
						Mutex.Unlock();
					}
				}
			}
		});
#elif
		auto TraceFunction = [&](ULexCanvas* InCanvas)
		{
			auto& VisualWidgetArray = InCanvas->GetVisualWidgetArray();
			for (auto& VisualWidget : VisualWidgetArray)
			{
				if (!IsValid(VisualWidget))continue;

				FHitResult ThisHit;
				ThisHit.FaceIndex = INDEX_NONE;
				if (
					VisualWidget->GetRaycastableInHierarchy()
					&& VisualWidget->GetWidgetActiveInHierarchy()
					&& VisualWidget->GetVisual()
					&& VisualWidget->GetVisual()->GetRaycastTarget()
					&& VisualWidget->GetVisual()->LineTraceUI(ThisHit, OutRayOrigin, OutRayEnd)
					)
				{
					if (VisualWidget->IsPointVisibleOnClip(ThisHit.Location))
					{
						OutHitResultArray.Add(ThisHit);
					}
				}
			}
		};
		if (InOptionalTraceChannel.IsSet())
		{
			LOCAL::ForeachCanvas(InRootCanvas, InOptionalTraceChannel.GetValue(), TraceFunction);
		}
		else
		{
			LOCAL::ForeachCanvas(InRootCanvas, TraceFunction);
		}
#endif
		
		if (OutHitResultArray.Num() > 0)
		{
			OutHitResultArray.Sort([](const FHitResult& A, const FHitResult& B)
			{
				auto AWidget = (ULexWidget*)(A.Component.Get());
				auto BWidget = (ULexWidget*)(B.Component.Get());
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

void ULexBaseRaycaster::RaycastWorld(ULexPointerEventData* InPointerEventData, bool InRequireFaceIndex, ETraceTypeQuery InTraceChannel, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FHitResult>& OutHitResultArray)
{
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	{
		CurrentRayOrigin = OutRayOrigin;
		CurrentRayDirection = OutRayDirection;
		
		FCollisionQueryParams queryParams = FCollisionQueryParams::DefaultQueryParam;
		queryParams.bReturnFaceIndex = InRequireFaceIndex;
		this->GetWorld()->LineTraceMultiByChannel(OutHitResultArray, OutRayOrigin, OutRayEnd, UEngineTypes::ConvertToCollisionChannel(InTraceChannel), queryParams);
	}
}

void ULexBaseRaycaster::SetPointerID(int32 Value)
{
	PointerID = Value;
}
