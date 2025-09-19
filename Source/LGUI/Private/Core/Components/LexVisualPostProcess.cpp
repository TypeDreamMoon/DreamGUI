// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexVisualPostProcess.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUIGeometry.h"
#include "Core/LexVisualPostProcessRenderProxy.h"

ULexVisualPostProcess::ULexVisualPostProcess(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	VisualType = ELexVisualType::PostProcess;
	geometry = TSharedPtr<FLexUIGeometry>(new FLexUIGeometry);

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

void ULexVisualPostProcess::BeginPlay()
{
	Super::BeginPlay();

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

#if WITH_EDITOR
void ULexVisualPostProcess::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bUVChanged = true;
	bLocalVertexPositionChanged = true;
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	SendMaskTextureToRenderProxy();
}
bool ULexVisualPostProcess::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();
	}
	return Super::CanEditChange(InProperty);
}
#endif


void ULexVisualPostProcess::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
    if (InPivotChange || InWidthChange || InHeightChange)
    {
	    MarkVertexPositionDirty();
    }
}

void ULexVisualPostProcess::MarkVertexPositionDirty()
{
	bLocalVertexPositionChanged = true;
	GetWidget()->MarkCanvasUpdate(false, true, false);
}
void ULexVisualPostProcess::MarkUVDirty()
{
	bUVChanged = true;
	GetWidget()->MarkCanvasUpdate(false, false, false);
}

void ULexVisualPostProcess::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	Super::MarkAllDirty();
}

DECLARE_CYCLE_STAT(TEXT("UIPostProcessRenderable UpdateGeometry"), STAT_UIPostProcessRenderableUpdate, STATGROUP_LGUI);
void ULexVisualPostProcess::UpdateGeometry()
{
	SCOPE_CYCLE_COUNTER(STAT_UIPostProcessRenderableUpdate);
	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	check(RenderCanvas);

	Super::UpdateGeometry();
	if (!DrawCall.IsValid()//not add to render yet
		)
	{
		geometry->Clear();
		OnUpdateGeometry(true, true, true, true);
		FLexUIGeometry::TransformVertices(RenderCanvas, this, geometry.Get());

		UpdateRegionVertex();
	}
	else//if geometry is created, update data
	{
		if (bLocalVertexPositionChanged || bUVChanged || bColorChanged)
		{
			geometry->Clear();
			OnUpdateGeometry(false, bLocalVertexPositionChanged, bUVChanged, bColorChanged);
		}
		if (bClipDataPositionChanged)
		{
			UpdateGeometryClipData(*geometry.Get(), ClipDataStartPosition);
		}
		if (bLocalVertexPositionChanged || bTransformChanged)
		{
			FLexUIGeometry::TransformVertices(RenderCanvas, this, geometry.Get());
		}
		if (bLocalVertexPositionChanged || bUVChanged || bColorChanged || bTransformChanged || bClipDataPositionChanged)
		{
			UpdateRegionVertex();
		}
	}

	bLocalVertexPositionChanged = false;
	bUVChanged = false;
	bColorChanged = false;
	bTransformChanged = false;
}
void ULexVisualPostProcess::OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	//simple rect geometry for render from screen image to mesh region and inverse
	{
		auto& vertices = geometry->Vertices;
		auto& originVertices = geometry->OriginVertices;
		FLexUIGeometry::LexUIGeometrySetArrayNum(vertices, 4);
		FLexUIGeometry::LexUIGeometrySetArrayNum(originVertices, 4);
		if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
		{
			if (InVertexPositionChanged)
			{
				auto Widget = GetWidget();
				//offset and size
				float pivotOffsetX = 0, pivotOffsetY = 0;
				FLexUIGeometry::CalculatePivotOffset(Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), pivotOffsetX, pivotOffsetY);
				float halfW = Widget->GetWidth() * 0.5f, halfH = Widget->GetHeight() * 0.5f;
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
				if (Widget->GetPixelSnappingInHierarchy())
				{
					FLexUIGeometry::AdjustPixelPerfectPos(originVertices, 0, 4, Widget->GetRenderCanvas(), this);
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
				FLexUIGeometry::UpdateUIColor(geometry.Get(), GetFinalColor());
			}
		}
	}
}

void ULexVisualPostProcess::UpdateRegionVertex()
{
	if (RenderScreenToMeshRegionVertexArray.Num() == 0)
	{
		//full screen vertex position
		RenderScreenToMeshRegionVertexArray =
		{
			FLexUIPostProcessCopyMeshRegionVertex(FVector3f(-1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLexUIPostProcessCopyMeshRegionVertex(FVector3f(1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLexUIPostProcessCopyMeshRegionVertex(FVector3f(-1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FLexUIPostProcessCopyMeshRegionVertex(FVector3f(1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f))
		};
	}

	auto& Vertices = geometry->Vertices;
	for (int i = 0; i < 4; i++)
	{
		auto& copyVert = RenderScreenToMeshRegionVertexArray[i];
		copyVert.LocalPosition = Vertices[i].Position;
	}
	
	const int VertexBufferSize = 4;
	if (RenderMeshRegionToScreenVertexArray.Num() != VertexBufferSize)
	{
		RenderMeshRegionToScreenVertexArray.SetNumZeroed(VertexBufferSize);
	}

	for (int i = 0; i < VertexBufferSize; i++)
	{
		auto& copyVert = RenderMeshRegionToScreenVertexArray[i];
		copyVert.Position = Vertices[i].Position;
		copyVert.TextureCoordinate0 = Vertices[i].TextureCoordinate[0];
		copyVert.TextureCoordinate1 = Vertices[i].TextureCoordinate[1];
	}

	SendRegionVertexDataToRenderProxy();
}

void ULexVisualPostProcess::SendRegionVertexDataToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto Widget = GetWidget();
		auto TempRenderProxy = RenderProxy.Get();
		struct FUIPostProcess_SendRegionVertexDataToRenderProxy
		{
			TArray<FLexUIPostProcessCopyMeshRegionVertex> renderScreenToMeshRegionVertexArray;
			TArray<FLexUIPostProcessVertex> renderMeshRegionToScreenVertexArray;
			FVector2f RectSize;
			FMatrix44f objectToWorldMatrix;
			FTexture2DDynamicResource* ClipDataTexture = nullptr;
		};
		auto updateData = new FUIPostProcess_SendRegionVertexDataToRenderProxy();
		updateData->renderMeshRegionToScreenVertexArray = this->RenderMeshRegionToScreenVertexArray;
		updateData->renderScreenToMeshRegionVertexArray = this->RenderScreenToMeshRegionVertexArray;
		updateData->RectSize = FVector2f(Widget->GetWidth(), Widget->GetHeight());
		updateData->objectToWorldMatrix = FMatrix44f(Widget->GetRenderCanvas()->GetLexWidget()->GetComponentTransform().ToMatrixWithScale());
		auto ClipDataTex = this->GetClipDataTexture();
		if (IsValid(ClipDataTex) && ClipDataTex->GetResource() != nullptr)
		{
			updateData->ClipDataTexture = (FTexture2DDynamicResource*)ClipDataTex->GetResource();
		}
		ENQUEUE_RENDER_COMMAND(FUIPostProcess_UpdateData)
			([TempRenderProxy, updateData](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->RenderScreenToMeshRegionVertexArray = updateData->renderScreenToMeshRegionVertexArray;
					TempRenderProxy->RenderMeshRegionToScreenVertexArray = updateData->renderMeshRegionToScreenVertexArray;
					TempRenderProxy->RectSize = updateData->RectSize;
					TempRenderProxy->ObjectToWorldMatrix = updateData->objectToWorldMatrix;
					TempRenderProxy->ClipDataTexture = updateData->ClipDataTexture;
					delete updateData;
				});
	}
}

void ULexVisualPostProcess::SetMaskTexture(UTexture2D* newValue)
{
	if (MaskTexture != newValue)
	{
		MaskTexture = newValue;
		SendMaskTextureToRenderProxy();

		bLocalVertexPositionChanged = true;
		bUVChanged = true;
		bColorChanged = true;
		GetWidget()->MarkCanvasUpdate(false, true, false);
	}
}
void ULexVisualPostProcess::SetMaskTextureUVRect(const FVector4& value)
{
	if (MaskTextureUVRect != value)
	{
		MaskTextureUVRect = value;

		bUVChanged = true;
		GetWidget()->MarkCanvasUpdate(false, false, false);
	}
}

void ULexVisualPostProcess::SendMaskTextureToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		auto TempRenderProxy = RenderProxy.Get();
		FTexture2DResource* MaskTextureResource = nullptr;
		if (IsValid(this->MaskTexture) && this->MaskTexture->GetResource() != nullptr)
		{
			MaskTextureResource = (FTexture2DResource*)this->MaskTexture->GetResource();
		}
		ENQUEUE_RENDER_COMMAND(FUIPostProcess_UpdateMaskTexture)
			([TempRenderProxy, MaskTextureResource](FRHICommandListImmediate& RHICmdList)
				{
					TempRenderProxy->MaskTexture = MaskTextureResource;
				});
	}
}

bool ULexVisualPostProcess::IsRenderProxyValid()const
{
	return RenderProxy.IsValid();
}

bool ULexVisualPostProcess::HaveValidData()const
{
	return geometry->Vertices.Num() > 0;
}

bool ULexVisualPostProcess::LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	if (RaycastType == ELexVisualRaycastType::Rect)
	{
		return Super::LineTraceUI(OutHit, Start, End);
	}
	else if (RaycastType == ELexVisualRaycastType::Mesh)
	{
		return LineTraceUIGeometry(geometry.Get(), OutHit, Start, End);
	}
	else
	{
		return LineTraceUICustom(OutHit, Start, End);
	}
}
