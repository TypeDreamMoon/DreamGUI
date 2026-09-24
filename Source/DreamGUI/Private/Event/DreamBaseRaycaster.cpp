// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamBaseRaycaster.h"
#include "DreamGUI.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Components/PrimitiveComponent.h"
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
	// What a world hit MEANS here, which is the question that kept this unimplemented.
	//
	// The engine's trace answers with primitives, and a widget's events go to its behaviours: a wall has
	// no widget and never will. What a world hit is, first, is an OCCLUDER. A hit with no widget still
	// carries a distance, and the input module sorts every raycaster's hits by distance: a world hit in
	// front of a world-space panel therefore wins, the panel gets its Exit, and the click that would
	// have gone through the wall does not land -- "the pointer is blocked" is exactly what a trigger
	// volume in front of a UI is for.
	//
	// And the hit is on something, which is the other half: the actor behind it is told about the
	// pointer through the same pointer interfaces widgets' behaviours implement -- on the actor itself
	// and on its components, as LGUI dispatched to a hit component's actor -- which is the road a
	// render-target surface's UDreamUIRenderTargetInteraction is reached by. The hit struct has no field
	// for the primitive, so it is remembered here and handed out by GetWorldHitComponent. A wall
	// implements none of the interfaces, and for a wall nothing changes.
	//
	// The rest of the pipeline had to learn that Widget can be null; see UDreamPointerInputModule::
	// LineTrace, which treats a widgetless hit as a blocker instead of dereferencing it, and
	// ProcessPointerEvent, which dispatches it to the actor.
	OutHitResultArray.Reset();
	// Forgotten before anything else, so a trace that finds nothing -- or never gets to run -- cannot
	// leave an earlier trace's primitive answering for a hit it did not make.
	LastWorldHitComponent.Reset();
	UWorld* World = GetWorld();
	if (World == nullptr)return;
	if (!GenerateRay(InPointerEventData, OutRayOrigin, OutRayDirection, OutRayEnd, CurrentRayLength))
	{
		return;//no ray source configured; GenerateRay has already said so
	}
	CurrentRayOrigin = OutRayOrigin;
	CurrentRayDirection = OutRayDirection;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DreamUIRaycastWorld), true);
	QueryParams.bReturnFaceIndex = InRequireFaceIndex;
	// The actor the raycaster rides on is the one holding the ray source -- a motion controller, a
	// camera rig -- and tracing into itself would block every pointer at zero distance.
	if (const AActor* IgnoredOwner = GetOwner())
	{
		QueryParams.AddIgnoredActor(IgnoredOwner);
	}

	// SINGLE, not multi, and the difference is the definition of an occluder.
	//
	// LineTraceMultiByChannel hands back every TOUCH along the ray plus the first thing that blocks --
	// so a multi trace would report an overlap-only trigger volume as something the pointer cannot pass
	// through, which is the opposite of what overlap means. What occludes a pointer is the nearest
	// BLOCKING hit, and that is exactly what a single trace answers with. Anything behind it is behind a
	// wall; the input module only ever reads the nearest hit anyway.
	FHitResult WorldHit;
	if (!World->LineTraceSingleByChannel(WorldHit, OutRayOrigin, OutRayEnd,
		UEngineTypes::ConvertToCollisionChannel(InTraceChannel), QueryParams))
	{
		return;//nothing solid in the way, so the UI behind is reachable
	}

	FDreamUIHitResult& Result = OutHitResultArray.AddDefaulted_GetRef();
	Result.Distance = WorldHit.Distance;
	Result.Time = WorldHit.Time;
	Result.Location = WorldHit.Location;
	Result.ImpactPoint = WorldHit.ImpactPoint;
	Result.Normal = WorldHit.ImpactNormal;
	Result.TraceStart = WorldHit.TraceStart;
	Result.TraceEnd = WorldHit.TraceEnd;
	Result.FaceIndex = InRequireFaceIndex ? WorldHit.FaceIndex : -1;
	Result.Widget = nullptr;//there is no widget behind a world primitive, and that is the point

	LastWorldHitComponent = WorldHit.GetComponent();
	LastWorldHitDistance = Result.Distance;
	LastWorldHitLocation = Result.Location;
}

UPrimitiveComponent* UDreamBaseRaycaster::GetWorldHitComponent(const FDreamUIHitResult& InHit) const
{
	// Explicitly null, not merely null now: a hit on a widget that has been destroyed since is still a
	// widget's hit, never a world one. RaycastWorld writes nullptr into the ones it makes.
	if (!InHit.Widget.IsExplicitlyNull())
	{
		return nullptr;
	}
	// Exact on purpose. Between RaycastWorld and here the hit is only ever copied -- sorted, put in a
	// container -- never recomputed, so its distance and location are the bits RaycastWorld wrote.
	if (InHit.Distance != LastWorldHitDistance || InHit.Location != LastWorldHitLocation)
	{
		return nullptr;
	}
	return LastWorldHitComponent.Get();
}

void UDreamBaseRaycaster::SetPointerID(int32 Value)
{
	PointerID = Value;
}
