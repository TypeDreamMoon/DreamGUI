// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexBaseRaycaster.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexVisual.h"
#include "Engine/SceneCapture2D.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "Engine/World.h"

ULexBaseRaycaster::ULexBaseRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
	TraceChannel = ETraceTypeQuery::TraceTypeQuery3;
}

void ULexBaseRaycaster::BeginPlay()
{
	Super::BeginPlay();
	ClickThresholdSquare = ClickThreshold * ClickThreshold;
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

bool ULexBaseRaycaster::ShouldStartDrag_HoldToDrag(ULexPointerEventData* InPointerEventData)
{
	if (bHoldToDrag)
	{
		return GetWorld()->TimeSeconds - InPointerEventData->pressTime > HoldToDragTime;
	}
	return false;
}

bool ULexBaseRaycaster::RaycastUI(ULexPointerEventData* InPointerEventData, const TArray<ELexRenderMode>& InRenderModeArray, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)
{
	OutHoverArray.Reset();
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection))
	{
		CurrentRayOrigin = OutRayOrigin;
		CurrentRayDirection = OutRayDirection;
		//UI element need to check if hit visible
		MultiHitResult.Reset();
		OutRayEnd = OutRayDirection * RayLength + OutRayOrigin;

		if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
		{
#if 0
			// use ParallelFor to speed up the hit process. ParallelFor should be safe because it blocks current thread
			FCriticalSection Mutex;
			if (InRenderModeArray.Num() == 1)//most case
			{
				auto& AllCanvasArray = LexUIManager->GetCanvasArray(InRenderModeArray[0]);
				ParallelFor(AllCanvasArray.Num(), [&AllCanvasArray, &Mutex, &OutRayOrigin, &OutRayEnd, this](int32 CanvasIndex) {
					auto& CanvasItem = AllCanvasArray[CanvasIndex];
					if (ShouldSkipCanvas(CanvasItem.Get()))return;
					if (CanvasItem->GetTraceChannel() != TraceChannel)return;
					auto& VisualWidgetArray = CanvasItem->GetVisualWidgetArray();
					ParallelFor(VisualWidgetArray.Num(), [&VisualWidgetArray, &Mutex, &OutRayOrigin, &OutRayEnd, CanvasItem, this](int32 index) {
						auto VisualWidget = VisualWidgetArray[index];
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
								MultiHitResult.Add(ThisHit);
								Mutex.Unlock();
							}
						}
						});
					});
			}
			else if (InRenderModeArray.Num() > 1)//only WorldSpace may use two render mode
			{
				ParallelFor(InRenderModeArray.Num(), [&InRenderModeArray, &Mutex, &OutRayOrigin, &OutRayEnd, LexUIManager, this](int32 RenderModeIndex) {
					auto RenderMode = InRenderModeArray[RenderModeIndex];
					auto& AllCanvasArray = LexUIManager->GetCanvasArray(RenderMode);
					ParallelFor(AllCanvasArray.Num(), [&AllCanvasArray, &Mutex, &OutRayOrigin, &OutRayEnd, this](int32 CanvasIndex) {
						auto& CanvasItem = AllCanvasArray[CanvasIndex];
						if (ShouldSkipCanvas(CanvasItem.Get()))return;
						if (CanvasItem->GetTraceChannel() != TraceChannel)return;
						auto& AllUIItemArray = CanvasItem->GetVisualWidgetArray();
						ParallelFor(AllUIItemArray.Num(), [&AllUIItemArray, &Mutex, &OutRayOrigin, &OutRayEnd, CanvasItem, this](int32 index) {
							auto uiItem = AllUIItemArray[index];
							FHitResult thisHit;
							thisHit.FaceIndex = INDEX_NONE;
							if (
								uiItem->GetRaycastableInHierarchy()
								&& uiItem->GetWidgetActiveInHierarchy()
								&& uiItem->GetVisual()
								&& uiItem->GetVisual()->GetRaycastTarget()
								&& uiItem->GetVisual()->LineTraceUI(thisHit, OutRayOrigin, OutRayEnd)
								)
							{
								if (uiItem->IsPointVisibleOnClip(thisHit.Location))
								{
									Mutex.Lock();
									MultiHitResult.Add(thisHit);
									Mutex.Unlock();
								}
							}
							});
						});
					});
			}
#else
			for (auto& InRenderMode : InRenderModeArray)
			{
				auto& AllCanvasArray = LexUIManager->GetCanvasArray(InRenderMode);
				for (auto& CanvasItem : AllCanvasArray)
				{
					if (ShouldSkipCanvas(CanvasItem.Get()))continue;
					auto& VisualWidgetArray = CanvasItem->GetVisualWidgetArray();
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
								MultiHitResult.Add(ThisHit);
							}
						}
					}
				}
			}
#endif
		}
		else
		{
			return false;
		}
		
		int HitCount = MultiHitResult.Num();
		if (HitCount > 0)
		{
			MultiHitResult.Sort([](const FHitResult& A, const FHitResult& B)
				{
					auto AUIItem = (ULexWidget*)(A.Component.Get());
					auto BUIItem = (ULexWidget*)(B.Component.Get());
					if (AUIItem != nullptr && BUIItem != nullptr)
					{
						auto ACanvasSortOrder = AUIItem->GetRenderCanvas()->GetActualSortOrder();
						auto BCanvasSortOrder = BUIItem->GetRenderCanvas()->GetActualSortOrder();
						if (AUIItem->GetRenderCanvas() != BUIItem->GetRenderCanvas() && ACanvasSortOrder != BCanvasSortOrder)//not in same sort order
						{
							return ACanvasSortOrder > BCanvasSortOrder;
						}
						else//same Canvas, sort on item's hierarchy order
						{
							return AUIItem->GetFlattenHierarchyIndex() > BUIItem->GetFlattenHierarchyIndex();
						}
					}
					return true;
				});

			//consider UI may not visible or not allow interaction, so we cannot take first one as result, we need to check from start
			bool bHaveValidHitResult = false;
			for (int i = 0; i < HitCount; i++)
			{
				auto HitResult = MultiHitResult[i];
				if (!bHaveValidHitResult)
				{
					OutHitResult = HitResult;
					bHaveValidHitResult = true;
				}
				OutHoverArray.Add(HitResult.Component.Get());
			}
			if (bHaveValidHitResult)
				return true;
		}
	}
	return false;
}

bool ULexBaseRaycaster::RaycastWorld(bool InRequireFaceIndex, ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, FHitResult& OutHitResult, TArray<USceneComponent*>& OutHoverArray)
{
	OutHoverArray.Reset();
	if (GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection))
	{
		MultiHitResult.Reset();
		OutRayEnd = OutRayDirection * RayLength + OutRayOrigin;

		FCollisionQueryParams queryParams = FCollisionQueryParams::DefaultQueryParam;
		queryParams.bReturnFaceIndex = InRequireFaceIndex;
		this->GetWorld()->LineTraceMultiByChannel(MultiHitResult, OutRayOrigin, OutRayEnd, UEngineTypes::ConvertToCollisionChannel(TraceChannel), queryParams);

		int hitCount = MultiHitResult.Num();
		if (hitCount > 0)
		{
			//this->GetWorld()->LineTraceMultiByChannel() result is sorted
			//multiWorldHitResult.Sort([](const FHitResult& A, const FHitResult& B)//sort on depth
			//{
			//	return A.Distance < B.Distance;
			//});
			OutHitResult = MultiHitResult[0];
			for (auto& item : MultiHitResult)
			{
				OutHoverArray.Add(item.Component.Get());
			}
			return true;
		}
	}
	return false;
}

void ULexBaseRaycaster::SetPointerID(int32 value)
{
	PointerID = value;
}
void ULexBaseRaycaster::SetDepth(int32 value)
{
	Depth = value;
}
void ULexBaseRaycaster::SetRayLength(float value)
{
	RayLength = value;
}
void ULexBaseRaycaster::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> value)
{
	TraceChannel = value;
}
void ULexBaseRaycaster::SetClickThreshold(float Value)
{
	ClickThreshold = Value;
	ClickThresholdSquare = ClickThreshold * ClickThreshold;
}
void ULexBaseRaycaster::SetHoldToDrag(bool Value)
{
	bHoldToDrag = Value;
}
void ULexBaseRaycaster::SetHoldToDragTime(float Value)
{
	HoldToDragTime = Value;
}
