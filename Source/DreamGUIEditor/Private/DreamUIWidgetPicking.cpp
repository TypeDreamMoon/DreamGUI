// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIWidgetPicking.h"

#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamCanvas.h"

namespace DreamUIWidgetPicking
{
	bool LineTraceWidgetRect(const UDreamWidget* InWidget, const FVector& InLineStart, const FVector& InLineEnd, FVector& OutWorldLocation)
	{
		if (!IsValid(InWidget))return false;

		// Same construction as UDreamVisual::LineTraceUIRect: inside a perspective scope the widget is
		// drawn somewhere its FTransform does not describe, so the ray has to be brought into the
		// space it is actually drawn in or picks land on the un-foreshortened rect. Outside one this
		// stays the plain transform inverse, because FMatrix and FTransform inversion do not agree
		// bit for bit and the ordinary path should not start moving by a fraction of a pixel.
		const bool bPerspective = InWidget->HasPerspectiveApplied();
		const FMatrix WidgetToWorldMatrix = bPerspective ? InWidget->GetWorldMatrix() : FMatrix::Identity;
		const FMatrix WorldToWidgetMatrix = bPerspective ? WidgetToWorldMatrix.Inverse() : FMatrix::Identity;
		const FTransform InverseTransform = InWidget->GetWorldTransform().Inverse();
		const FVector LocalStart = bPerspective ? FVector(WorldToWidgetMatrix.TransformPosition(InLineStart)) : InverseTransform.TransformPosition(InLineStart);
		const FVector LocalEnd = bPerspective ? FVector(WorldToWidgetMatrix.TransformPosition(InLineEnd)) : InverseTransform.TransformPosition(InLineEnd);

		// start and end must sit on opposite sides of the widget plane
		if (FMath::Sign(LocalStart.X) == FMath::Sign(LocalEnd.X))return false;

		const FVector LocalHit = FMath::LinePlaneIntersection(LocalStart, LocalEnd, FVector::ZeroVector, FVector(1, 0, 0));
		if (LocalHit.Y <= InWidget->GetLocalSpaceLeft() || LocalHit.Y >= InWidget->GetLocalSpaceRight())return false;
		if (LocalHit.Z <= InWidget->GetLocalSpaceBottom() || LocalHit.Z >= InWidget->GetLocalSpaceTop())return false;

		OutWorldLocation = bPerspective
			? FVector(WidgetToWorldMatrix.TransformPosition(LocalHit))
			: InWidget->GetWorldTransform().TransformPosition(LocalHit);
		return true;
	}

	void RaycastWidgetRects(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd, TArray<FDreamUIWidgetPickHit>& OutHits)
	{
		OutHits.Reset();
		if (InWorld == nullptr)return;

		for (UDreamWidget* Widget : InWidgets)
		{
			if (!IsValid(Widget))continue;
			if (Widget->GetWorld() != InWorld)continue;
			// bCacheRenderVisibleInHierarchy is maintained for every widget, mesh or not, and it
			// already folds in bHiddenInDesigner -- so this is the one gate the designer needs.
			// Hit-test visibility is deliberately not consulted: a HitTestInvisible widget is still
			// something you author, and UMG lets you select one too.
			if (!Widget->GetRenderVisibleInHierarchy())continue;

			FVector WorldLocation;
			if (!LineTraceWidgetRect(Widget, InLineStart, InLineEnd, WorldLocation))continue;
			if (!Widget->IsPointVisibleOnClip(WorldLocation))continue;

			FDreamUIWidgetPickHit& Hit = OutHits.AddDefaulted_GetRef();
			Hit.Widget = Widget;
			Hit.WorldLocation = WorldLocation;
			Hit.Distance = FVector::Distance(InLineStart, WorldLocation);
		}

		// Front-to-back, matching RaycastHitUI's order so click-through feels the same as it always
		// did: canvas sort order first, then flatten index, which puts a child ahead of its parent.
		// A widget with no visual has no render canvas of its own, so it inherits the comparison's
		// neutral order and is separated from its siblings by flatten index alone.
		OutHits.Sort([](const FDreamUIWidgetPickHit& A, const FDreamUIWidgetPickHit& B)
		{
			const UDreamCanvas* CanvasA = A.Widget->GetRenderCanvas();
			const UDreamCanvas* CanvasB = B.Widget->GetRenderCanvas();
			const int32 SortA = CanvasA ? CanvasA->GetActualSortOrder() : 0;
			const int32 SortB = CanvasB ? CanvasB->GetActualSortOrder() : 0;
			if (SortA != SortB)return SortA > SortB;
			return A.Widget->GetFlattenHierarchyIndex() > B.Widget->GetFlattenHierarchyIndex();
		});
	}

	UDreamWidget* PickTopmostWidget(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd, int32& InOutCycleIndex)
	{
		TArray<FDreamUIWidgetPickHit> Hits;
		RaycastWidgetRects(InWorld, InWidgets, InLineStart, InLineEnd, Hits);
		if (Hits.IsEmpty())
		{
			InOutCycleIndex = INDEX_NONE;
			return nullptr;
		}
		InOutCycleIndex++;
		if (InOutCycleIndex >= Hits.Num() || InOutCycleIndex < 0)InOutCycleIndex = 0;
		return Hits[InOutCycleIndex].Widget;
	}

	UDreamWidget* ResolveDropContainer(UDreamWidget* InHitWidget)
	{
		for (UDreamWidget* Candidate = InHitWidget; IsValid(Candidate); Candidate = Candidate->GetParent())
		{
			if (Candidate->GetLayoutContainer() != nullptr && Candidate->CanAcceptAdditionalChildren())
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	void CollectPickableWidgets(const UWorld* InWorld, TArray<UDreamWidget*>& OutWidgets)
	{
		OutWidgets.Reset();
		if (InWorld == nullptr)return;
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(const_cast<UWorld*>(InWorld));
		if (Manager == nullptr)return;
		for (const TWeakObjectPtr<UDreamCanvas>& WeakCanvas : Manager->GetAllCanvasArray())
		{
			UDreamCanvas* Canvas = WeakCanvas.Get();
			if (!Canvas || !Canvas->IsRootCanvas())continue;
			UDreamWidget* Root = Canvas->GetWidget();
			if (!IsValid(Root))continue;
			// The root is included. ProcessClick used to collect children only, which is why the
			// prefab root itself could never be selected by clicking it.
			UDreamWidget::CollectChildrenWidgets(Root, OutWidgets, /*IncludeTarget*/true);
		}
	}
}
