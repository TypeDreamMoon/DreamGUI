// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/ActorComponent/UIPostProcessRenderable.h"
#include "LGUI.h"
#include "Core/ActorComponent/LGUICanvas.h"
#include "Core/UIGeometry.h"
#include "Core/UIPostProcessRenderProxy.h"

UUIPostProcessRenderable::UUIPostProcessRenderable(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	UIRenderableType = EUIRenderableType::UIPostProcessRenderable;
	geometry = TSharedPtr<UIGeometry>(new UIGeometry);

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

void UUIPostProcessRenderable::BeginPlay()
{
	Super::BeginPlay();

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

void UUIPostProcessRenderable::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );
}


#if WITH_EDITOR
void UUIPostProcessRenderable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bUVChanged = true;
	bLocalVertexPositionChanged = true;
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	SendMaskTextureToRenderProxy();
}
bool UUIPostProcessRenderable::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();
	}
	return Super::CanEditChange(InProperty);
}
#endif

void UUIPostProcessRenderable::OnUnregister()
{
	Super::OnUnregister();
}

void UUIPostProcessRenderable::OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange, bool InDiscardCache)
{
    Super::OnAnchorChange(InPivotChange, InWidthChange, InHeightChange, InDiscardCache);
    if (InPivotChange || InWidthChange || InHeightChange)
    {
	    MarkVertexPositionDirty();
    }
}

void UUIPostProcessRenderable::MarkVertexPositionDirty()
{
	bLocalVertexPositionChanged = true;
	MarkCanvasUpdate(false, true, false);
}
void UUIPostProcessRenderable::MarkUVDirty()
{
	bUVChanged = true;
	MarkCanvasUpdate(false, false, false);
}

void UUIPostProcessRenderable::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	Super::MarkAllDirty();
}

DECLARE_CYCLE_STAT(TEXT("UIPostProcessRenderable UpdateGeometry"), STAT_UIPostProcessRenderableUpdate, STATGROUP_LGUI);
void UUIPostProcessRenderable::UpdateGeometry()
{
	SCOPE_CYCLE_COUNTER(STAT_UIPostProcessRenderableUpdate);
	if (GetIsUIActiveInHierarchy() == false)return;
	if (!RenderCanvas.IsValid())return;

	Super::UpdateGeometry();
	if (!drawcall.IsValid()//not add to render yet
		)
	{
		geometry->Clear();
		OnUpdateGeometry(true, true, true, true);
		UIGeometry::TransformVertices(RenderCanvas.Get(), this, geometry.Get());

		UpdateRegionVertex();
	}
	else//if geometry is created, update data
	{
		if (bLocalVertexPositionChanged || bUVChanged || bColorChanged)
		{
			geometry->Clear();
			OnUpdateGeometry(false, bLocalVertexPositionChanged, bUVChanged, bColorChanged);
		}
		if (bClipDataChanged)
		{
			OnUpdateGeometryClipData(*geometry.Get(), bClipDataChanged);
		}
		if (bLocalVertexPositionChanged || bTransformChanged)
		{
			UIGeometry::TransformVertices(RenderCanvas.Get(), this, geometry.Get());
		}
		if (bLocalVertexPositionChanged || bUVChanged || bColorChanged || bTransformChanged || bClipDataChanged)
		{
			UpdateRegionVertex();
		}
	}

	bLocalVertexPositionChanged = false;
	bUVChanged = false;
	bColorChanged = false;
	bTransformChanged = false;
}
void UUIPostProcessRenderable::OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	//simple rect geometry for render from screen image to mesh region and inverse
	{
		auto& vertices = geometry->vertices;
		auto& originVertices = geometry->originVertices;
		UIGeometry::LGUIGeometrySetArrayNum(vertices, 4);
		UIGeometry::LGUIGeometrySetArrayNum(originVertices, 4);
		if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
		{
			if (InVertexPositionChanged)
			{
				//offset and size
				float pivotOffsetX = 0, pivotOffsetY = 0;
				UIGeometry::CalculatePivotOffset(this->GetWidth(), this->GetHeight(), FVector2f(this->GetPivot()), pivotOffsetX, pivotOffsetY);
				float halfW = this->GetWidth() * 0.5f, halfH = this->GetHeight() * 0.5f;
				//positions
				float minX = -halfW + pivotOffsetX;
				float minY = -halfH + pivotOffsetY;
				float maxX = halfW + pivotOffsetX;
				float maxY = halfH + pivotOffsetY;
				originVertices[0].Position = FVector3f(0, minX, minY);
				originVertices[1].Position = FVector3f(0, maxX, minY);
				originVertices[2].Position = FVector3f(0, minX, maxY);
				originVertices[3].Position = FVector3f(0, maxX, maxY);
				//snap pixel
				if (RenderCanvas->GetActualPixelPerfect())
				{
					UIGeometry::AdjustPixelPerfectPos(originVertices, 0, 4, RenderCanvas.Get(), this);
				}
			}

			if (InVertexUVChanged)
			{
				vertices[0].TextureCoordinate[0] = FVector2f(0, 1);
				vertices[1].TextureCoordinate[0] = FVector2f(1, 1);
				vertices[2].TextureCoordinate[0] = FVector2f(0, 0);
				vertices[3].TextureCoordinate[0] = FVector2f(1, 0);
			}

			if (InVertexColorChanged)
			{
				UIGeometry::UpdateUIColor(geometry.Get(), GetFinalColor());
			}
		}
	}
}

void UUIPostProcessRenderable::OnUpdateGeometryClipData(UIGeometry& InMesh, bool InClipDataStartPositionChanged)
{
	//clip data
	if (InClipDataStartPositionChanged)
	{
		auto& vertices = InMesh.vertices;
		auto clipDataStartPos = GetClipDataStartPosition();
		for (int i = 0; i < vertices.Num(); i++)
		{
			vertices[i].TextureCoordinate[1].X = clipDataStartPos;
		}
	}
}

void UUIPostProcessRenderable::UpdateRegionVertex()
{
	if (renderScreenToMeshRegionVertexArray.Num() == 0)
	{
		//full screen vertex position
		renderScreenToMeshRegionVertexArray =
		{
			FLGUIPostProcessCopyMeshRegionVertex(FVector3f(-1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLGUIPostProcessCopyMeshRegionVertex(FVector3f(1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLGUIPostProcessCopyMeshRegionVertex(FVector3f(-1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLGUIPostProcessCopyMeshRegionVertex(FVector3f(1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f))
		};
	}

	auto& Vertices = geometry->vertices;
	for (int i = 0; i < 4; i++)
	{
		auto& copyVert = renderScreenToMeshRegionVertexArray[i];
		copyVert.LocalPosition = Vertices[i].Position;
	}
	
	const int VertexBufferSize = 4;
	if (renderMeshRegionToScreenVertexArray.Num() != VertexBufferSize)
	{
		renderMeshRegionToScreenVertexArray.SetNumZeroed(VertexBufferSize);
	}

	for (int i = 0; i < VertexBufferSize; i++)
	{
		auto& copyVert = renderMeshRegionToScreenVertexArray[i];
		copyVert.Position = Vertices[i].Position;
		copyVert.TextureCoordinate0 = Vertices[i].TextureCoordinate[0];
		copyVert.TextureCoordinate1 = Vertices[i].TextureCoordinate[1];
	}

	SendRegionVertexDataToRenderProxy();
}

void UUIPostProcessRenderable::SendRegionVertexDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto TempRenderProxy = RenderProxy.Get();
		struct FUIPostProcess_SendRegionVertexDataToRenderProxy
		{
			TArray<FLGUIPostProcessCopyMeshRegionVertex> renderScreenToMeshRegionVertexArray;
			TArray<FLGUIPostProcessVertex> renderMeshRegionToScreenVertexArray;
			FVector2f RectSize;
			FMatrix44f objectToWorldMatrix;
			FTexture2DDynamicResource* ClipDataTexture = nullptr;
		};
		auto updateData = new FUIPostProcess_SendRegionVertexDataToRenderProxy();
		updateData->renderMeshRegionToScreenVertexArray = this->renderMeshRegionToScreenVertexArray;
		updateData->renderScreenToMeshRegionVertexArray = this->renderScreenToMeshRegionVertexArray;
		updateData->RectSize = FVector2f(this->GetWidth(), this->GetHeight());
		updateData->objectToWorldMatrix = FMatrix44f(this->RenderCanvas->GetUIItem()->GetComponentTransform().ToMatrixWithScale());
		auto ClipDataTex = this->GetClipDataTexture();
		if (IsValid(ClipDataTex) && ClipDataTex->GetResource() != nullptr)
		{
			updateData->ClipDataTexture = (FTexture2DDynamicResource*)this->GetClipDataTexture()->GetResource();
		}
		ENQUEUE_RENDER_COMMAND(FUIPostProcess_UpdateData)
			([TempRenderProxy, updateData](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->renderScreenToMeshRegionVertexArray = updateData->renderScreenToMeshRegionVertexArray;
					TempRenderProxy->renderMeshRegionToScreenVertexArray = updateData->renderMeshRegionToScreenVertexArray;
					TempRenderProxy->RectSize = updateData->RectSize;
					TempRenderProxy->objectToWorldMatrix = updateData->objectToWorldMatrix;
					TempRenderProxy->ClipDataTexture = updateData->ClipDataTexture;
					delete updateData;
				});
	}
}

void UUIPostProcessRenderable::SetMaskTexture(UTexture2D* newValue)
{
	if (maskTexture != newValue)
	{
		maskTexture = newValue;
		SendMaskTextureToRenderProxy();

		bLocalVertexPositionChanged = true;
		bUVChanged = true;
		bColorChanged = true;
		MarkCanvasUpdate(false, true, false);
	}
}
void UUIPostProcessRenderable::SetMaskTextureUVRect(const FVector4& value)
{
	if (MaskTextureUVRect != value)
	{
		MaskTextureUVRect = value;

		bUVChanged = true;
		MarkCanvasUpdate(false, false, false);
	}
}

void UUIPostProcessRenderable::SendMaskTextureToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto TempRenderProxy = RenderProxy.Get();
		FTexture2DResource* maskTextureResource = nullptr;
		if (IsValid(this->maskTexture) && this->maskTexture->GetResource() != nullptr)
		{
			maskTextureResource = (FTexture2DResource*)this->maskTexture->GetResource();
		}
		ENQUEUE_RENDER_COMMAND(FUIPostProcess_UpdateMaskTexture)
			([TempRenderProxy, maskTextureResource](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->maskTexture = maskTextureResource;
				});
	}
}

bool UUIPostProcessRenderable::IsRenderProxyValid()const
{
	return RenderProxy.IsValid();
}

bool UUIPostProcessRenderable::HaveValidData()const
{
	return geometry->vertices.Num() > 0;
}

bool UUIPostProcessRenderable::LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)
{
	if (RaycastType == EUIRenderableRaycastType::Rect)
	{
		return Super::LineTraceUI(OutHit, Start, End);
	}
	else if (RaycastType == EUIRenderableRaycastType::Mesh)
	{
		return LineTraceUIGeometry(geometry.Get(), OutHit, Start, End);
	}
	else
	{
		return LineTraceUICustom(OutHit, Start, End);
	}
}
