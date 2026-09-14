// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/Components/DreamWidget.h"

UDreamVisualDirectMesh::UDreamVisualDirectMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bLocalVertexPositionChanged = true;
	VisualType = EDreamVisualType::DirectMesh;
}

void UDreamVisualDirectMesh::BeginPlay()
{
	Super::BeginPlay();
	bLocalVertexPositionChanged = true;
}

void UDreamVisualDirectMesh::BeginDestroy()
{
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UDreamVisualDirectMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamVisualDirectMesh::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	Super::MarkAllDirty();
}

void UDreamVisualDirectMesh::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
}

void UDreamVisualDirectMesh::ClearMeshData()
{
	// Asking for a canvas update IS the release, and there is deliberately no per-section call here.
	// UDreamCanvas::UpdateDrawCallMesh opens by pooling EVERY section
	// (UDreamUIMeshComponent::PoolAllRenderSection) and then hands one back to each draw call it
	// still has; a direct-mesh visual whose HaveValidData() is false produces no draw call
	// (UDreamCanvas::GetRenderDataFromWidgets skips it), so its section stays in the pool and is
	// reused by whoever asks next. Nothing is leaked and nothing stale is drawn.
	// The weak pointers to the mesh and the section are left alone on purpose: a pooled section is
	// handed to whoever asks next and refilled by its supplier, and this element gets its own again
	// through OnSupplyMeshSection the next time it has data to draw.
	GetWidget()->MarkCanvasUpdate(true);
}
void UDreamVisualDirectMesh::OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection)
{
	Mesh = InMesh;
	MeshSection = InSection;
}

bool UDreamVisualDirectMesh::LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	if (RaycastType == EDreamVisualRaycastType::Rect)
	{
		return Super::LineTraceUI(OutHit, Start, End);
	}
	else if (RaycastType == EDreamVisualRaycastType::Mesh)
	{
		auto Widget = GetWidget();
		auto Section = MeshSection.Pin();
		if (!Section.IsValid())
		{
			//nothing has been supplied to trace against yet; the rect is all that is known about this element
			return LineTraceUIRect(OutHit, Start, End);
		}
		auto RenderCanvas = Widget->GetRenderCanvas();
		auto CanvasWidget = RenderCanvas != nullptr ? RenderCanvas->GetWidget() : nullptr;
		if (CanvasWidget == nullptr)
		{
			return LineTraceUIRect(OutHit, Start, End);
		}
		const auto& Vertices = Section->Vertices;
		const auto& TriangleIndices = Section->TriangleIndices;
		const int32 ValidTriangleIndicesNum = FMath::Min(Section->ValidTriangleIndicesNum, TriangleIndices.Num());
		const int32 ValidVerticesNum = FMath::Min(Section->ValidVerticesNum, Vertices.Num());
		if (ValidTriangleIndicesNum < 3 || ValidVerticesNum <= 0)
		{
			return LineTraceUIRect(OutHit, Start, End);
		}

		// Every vertex in a direct-mesh section is stored in CANVAS space -- the supplier transforms
		// by ItemToCanvas before writing (see UDreamStaticMesh::CreateGeometry) -- so the ray is
		// brought into canvas space through the same transform, not into this widget's local space.
		// There is deliberately no plane-crossing or rect pre-filter either: this geometry is the
		// reason DirectMesh exists, it is three-dimensional and it is not bound by the widget rect,
		// which is exactly what GetHitGeometryFitsWidgetRect() answers false to.
		const FTransform& CanvasToWorldTf = CanvasWidget->GetWorldTransform();
		const FVector CanvasSpaceRayStart = CanvasToWorldTf.InverseTransformPosition(Start);
		const FVector CanvasSpaceRayEnd = CanvasToWorldTf.InverseTransformPosition(End);

		const int32 TriangleCount = ValidTriangleIndicesNum / 3;
		int32 Index = 0;
		for (int32 i = 0; i < TriangleCount; i++)
		{
			const int32 VertIndex0 = (int32)TriangleIndices[Index++];
			const int32 VertIndex1 = (int32)TriangleIndices[Index++];
			const int32 VertIndex2 = (int32)TriangleIndices[Index++];
			if (VertIndex0 >= ValidVerticesNum || VertIndex1 >= ValidVerticesNum || VertIndex2 >= ValidVerticesNum)
			{
				continue;
			}
			const FVector Point0 = (FVector)(Vertices[VertIndex0].Position);
			const FVector Point1 = (FVector)(Vertices[VertIndex1].Position);
			const FVector Point2 = (FVector)(Vertices[VertIndex2].Position);
			FVector HitPoint, HitNormal;
			if (FMath::SegmentTriangleIntersection(CanvasSpaceRayStart, CanvasSpaceRayEnd, Point0, Point1, Point2, HitPoint, HitNormal))
			{
				OutHit.TraceStart = Start;
				OutHit.TraceEnd = End;
				OutHit.Widget = Widget;
				OutHit.Location = CanvasToWorldTf.TransformPosition(HitPoint);
				OutHit.Normal = CanvasToWorldTf.TransformVector(HitNormal);
				OutHit.Normal.Normalize();
				OutHit.Distance = FVector::Distance(Start, OutHit.Location);
				OutHit.ImpactPoint = OutHit.Location;
				OutHit.FaceIndex = i;
				return true;
			}
		}
		return false;
	}
	else
	{
		return LineTraceUICustom(OutHit, Start, End);
	}
}

void UDreamVisualDirectMesh::PostFillMeshData()
{
	if (!MeshSection.IsValid())return;
	auto Widget = GetWidget();
	auto Canvas = Widget->GetRenderCanvas();
	if (bWidgetPropertyDataStartPositionChanged)
	{
		bWidgetPropertyDataStartPositionChanged = false;
		UpdateGeometryWidgetPropertyData(MeshSection.Pin()->Vertices, MeshSection.Pin()->ValidVerticesNum, this->WidgetPropertyDataStartPosition);
	}
	if (bWidgetPropertyDataFontMarkDirty)
	{
		bWidgetPropertyDataFontMarkDirty = false;
		FillWidgetPropertyDataForMaterial_InitialMark(Canvas->GetWidgetPropertyDataAsTexture(), 0);
	}
	if (bClipDataPositionChanged)
	{
		bClipDataPositionChanged = false;
		/** Only update the clip data position coordinate. */
		FillWidgetPropertyDataForMaterial_ClipDataCoordinate(Canvas->GetWidgetPropertyDataAsTexture());
	}
	if (bLocalVertexPositionChanged || bTransformChanged)
	{
		if (this->GetRequirePropertiesForMaterial_Size() || this->GetRequirePropertiesForMaterial_CenterPosition())
		{
			FillWidgetPropertyDataForMaterial(this->GetRequirePropertiesForMaterial_Size(), this->GetRequirePropertiesForMaterial_CenterPosition());
		}
	}
}
