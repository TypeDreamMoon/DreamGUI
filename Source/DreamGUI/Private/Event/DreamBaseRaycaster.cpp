// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamBaseRaycaster.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Engine/World.h"

namespace DreamBaseRaycasterLocal
{
	/**
	 * Could this ray reach this visual at all? A coarse reject, run before the exact hit test.
	 *
	 * Every active raycaster walks EVERY visual under the root canvas on every pointer update, and the
	 * exact test is not cheap: UDreamVisual::LineTraceUIRect inverts the widget's FTransform -- a
	 * quaternion conjugate and three reciprocals -- then transforms two points before it has anything
	 * to compare. A segment-to-point distance is a dot product and a subtraction, and on a real screen
	 * almost every widget is nowhere near the cursor, so almost every visual can be answered with it.
	 *
	 * IT RETURNS FALSE ONLY WHEN THE MISS IS PROVEN. Everything it cannot prove comes back true and
	 * takes the old path unchanged -- a custom raycast shape, a mesh whose vertices are not bound by
	 * the rect, a widget under a perspective scope, a rect with no measured size. The filter is
	 * allowed to cost time; it is not allowed to lose a hit.
	 */
	FORCEINLINE bool CouldRayReachVisual(const UDreamVisual* InVisual, const UDreamWidget* InWidget, const FVector& InRayOrigin, const FVector& InRayEnd)
	{
		// Whether the hit shape is inside the rect is the VISUAL's question (raycast type, custom
		// raycast object); where that rect is in the world is the WIDGET's. Neither knows the other's
		// half, so both are asked.
		if (!InVisual->GetHitGeometryFitsWidgetRect())
		{
			return true;
		}
		FVector Center;
		double Radius = 0.0;
		if (!InWidget->GetWorldRectBoundingSphere(Center, Radius))
		{
			return true;
		}
		// A SEGMENT, matching how the exact test reads the same two points: it requires the widget's
		// plane to be crossed BETWEEN start and end, so a widget past the raycaster's reach is already
		// a miss there and clamping the parameter here reproduces that for free.
		const FVector Segment = InRayEnd - InRayOrigin;
		const FVector ToCenter = Center - InRayOrigin;
		const double SegmentLengthSquared = Segment.SizeSquared();
		const double T = SegmentLengthSquared > 0.0
			? FMath::Clamp(FVector::DotProduct(ToCenter, Segment) / SegmentLengthSquared, 0.0, 1.0)
			: 0.0;
		const double DistanceSquared = (ToCenter - Segment * T).SizeSquared();
		// Negated so a NaN anywhere in the ray answers "could reach" and defers to the exact test,
		// rather than quietly culling the widget.
		return !(DistanceSquared > Radius * Radius);
	}
}

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
	if (DreamUI::IsGameWorld(this))
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
				&& DreamBaseRaycasterLocal::CouldRayReachVisual(Visual, Widget, OutRayOrigin, OutRayEnd)
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
					// Ordered last of the cheap tests and first of the expensive ones: the three above
					// are field reads, this one is arithmetic on a cached sphere, and LineTraceUI below
					// inverts a transform.
					&& DreamBaseRaycasterLocal::CouldRayReachVisual(Visual, Widget, OutRayOrigin, OutRayEnd)
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
	// Not implemented, and the commented-out body below cannot simply be uncommented: it hands
	// OutHitResultArray straight to LineTraceMultiByChannel, which fills TArray<FHitResult> -- while
	// this is TArray<FDreamUIHitResult>, a different struct whose payload is a UDreamWidget the
	// engine trace has no way to produce. Everything downstream (Widget, GetInteractableInHierarchy,
	// canvas sort order) is defined only for widgets, so a world-object hit path is a design job,
	// not a revert. Until someone does it, this degrades to "hit nothing" instead of taking the
	// process down: UDreamWorldSpaceRaycasterForWorldTrigger is a placeable component and reaching
	// here needed nothing more than adding it to an actor.
	ensureMsgf(false, TEXT("UDreamBaseRaycaster::RaycastWorld is not implemented; world-trigger raycasting will find nothing."));
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
