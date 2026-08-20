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
	GetWidget()->MarkCanvasUpdate(true);
	if (Mesh.IsValid() && MeshSection.IsValid())
	{
		//@todo: maybe we should remove render section
	}
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
		//@todo
		// if (!DrawCall.IsValid())return false;
		// if (!DrawCall->DrawCallRenderSection.IsValid())return false;

		auto Widget = GetWidget();
		auto inverseTf = Widget->GetWorldTransform().Inverse();
		auto localSpaceRayOrigin = inverseTf.TransformPosition(Start);
		auto localSpaceRayEnd = inverseTf.TransformPosition(End);

		//DrawDebugLine(this->GetWorld(), Start, End, FColor::Red, false, 5.0f);//just for test
		//check Line-Plane intersection first, then check Line-Triangle
		//start and end point must be different side of X plane
		if (FMath::Sign(localSpaceRayOrigin.X) != FMath::Sign(localSpaceRayEnd.X))
		{
			auto IntersectionPoint = FMath::LinePlaneIntersection(localSpaceRayOrigin, localSpaceRayEnd, FVector::ZeroVector, FVector(1, 0, 0));
			//hit point inside rect area
			if (IntersectionPoint.Y > Widget->GetLocalSpaceLeft() && IntersectionPoint.Y < Widget->GetLocalSpaceRight() && IntersectionPoint.Z > Widget->GetLocalSpaceBottom() && IntersectionPoint.Z < Widget->GetLocalSpaceTop())
			{
				//@todo
				//triangle hit test
				// auto MeshSection = (FDreamUIRenderSection_Mesh*)DrawCall->DrawCallRenderSection.Pin().Get();
				// auto& vertices = MeshSection->vertices;
				// auto& triangleIndices = MeshSection->triangleIndices;
				// int triangleCount = triangleIndices.Num() / 3;
				// int index = 0;
				// for (int i = 0; i < triangleCount; i++)
				// {
				// 	auto point0 = (FVector)(vertices[triangleIndices[index++]].Position);
				// 	auto point1 = (FVector)(vertices[triangleIndices[index++]].Position);
				// 	auto point2 = (FVector)(vertices[triangleIndices[index++]].Position);
				// 	FVector HitPoint, HitNormal;
				// 	if (FMath::SegmentTriangleIntersection(localSpaceRayOrigin, localSpaceRayEnd, point0, point1, point2, HitPoint, HitNormal))
				// 	{
				// 		OutHit.TraceStart = Start;
				// 		OutHit.TraceEnd = End;
				// 		OutHit.Component = (UPrimitiveComponent*)Widget;//acturally this convert is incorrect, but I need this pointer
				// 		OutHit.Location = Widget->GetComponentTransform().TransformPosition(HitPoint);
				// 		OutHit.Normal = Widget->GetComponentTransform().TransformVector(HitNormal);
				// 		OutHit.Normal.Normalize();
				// 		OutHit.Distance = FVector::Distance(Start, OutHit.Location);
				// 		OutHit.ImpactPoint = OutHit.Location;
				// 		OutHit.ImpactNormal = OutHit.Normal;
				// 		return true;
				// 	}
				// }
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
