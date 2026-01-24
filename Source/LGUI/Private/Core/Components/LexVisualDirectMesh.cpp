// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexVisualDirectMesh.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUIMesh/LexUIMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/LexUIDrawCall.h"

ULexVisualDirectMesh::ULexVisualDirectMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bLocalVertexPositionChanged = true;
	VisualType = ELexVisualType::DirectMesh;
}

void ULexVisualDirectMesh::BeginPlay()
{
	Super::BeginPlay();
	bLocalVertexPositionChanged = true;
}

#if WITH_EDITOR
void ULexVisualDirectMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ULexVisualDirectMesh::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	Super::MarkAllDirty();
}

void ULexVisualDirectMesh::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	MarkVertexPositionDirty();
}

void ULexVisualDirectMesh::MarkVertexPositionDirty()
{
	bLocalVertexPositionChanged = true;
	GetWidget()->MarkCanvasUpdate(false, false, false);//since DirectMeshRenderable will always take a drawcall, we don't need to rebuild drawcall on it
}
void ULexVisualDirectMesh::UpdateGeometry()
{
	Super::UpdateGeometry();
}


TWeakPtr<FLexUIRenderSection> ULexVisualDirectMesh::GetMeshSection()const
{
	//@todo
	// if (DrawCall.IsValid())
	// {
	// 	return DrawCall->DrawCallRenderSection;
	// }
	return nullptr;
}
TWeakObjectPtr<ULexUIMeshComponent> ULexVisualDirectMesh::GetUIMesh()const
{
	//@todo
	// if (DrawCall.IsValid())
	// {
	// 	return DrawCall->DrawCallMesh;
	// }
	return nullptr;
}
void ULexVisualDirectMesh::ClearMeshData()
{
	GetWidget()->MarkCanvasUpdate(false, false, true);
}
void ULexVisualDirectMesh::OnMeshDataReady()
{

}

bool ULexVisualDirectMesh::LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	if (RaycastType == ELexVisualRaycastType::Rect)
	{
		return Super::LineTraceUI(OutHit, Start, End);
	}
	else if (RaycastType == ELexVisualRaycastType::Mesh)
	{
		//@todo
		// if (!DrawCall.IsValid())return false;
		// if (!DrawCall->DrawCallRenderSection.IsValid())return false;

		auto Widget = GetWidget();
		auto inverseTf = Widget->GetComponentTransform().Inverse();
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
				// auto MeshSection = (FLexUIRenderSection_Mesh*)DrawCall->DrawCallRenderSection.Pin().Get();
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
