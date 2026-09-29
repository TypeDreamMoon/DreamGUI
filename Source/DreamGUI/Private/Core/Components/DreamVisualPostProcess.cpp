// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/DreamUIWorldContext.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIGeometry.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Rendering/Texture2DResource.h"



UDreamVisualPostProcess::UDreamVisualPostProcess(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	VisualType = EDreamVisualType::PostProcess;
	Geometry = TSharedPtr<FDreamUIGeometry>(new FDreamUIGeometry);

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

void UDreamVisualPostProcess::BeginPlay()
{
	Super::BeginPlay();

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
}

void UDreamVisualPostProcess::BeginDestroy()
{
	// Hand this visual's reference to the render thread instead of deleting through it. The mesh
	// section and any update command still in flight hold their own references, so the proxy dies
	// when the last of them lets go -- and because every one of those is released on the render
	// thread, the destructor (which touches render resources) always runs there.
	FDreamVisualPostProcessRenderProxy::ReleaseOnRenderThread(MoveTemp(RenderProxy));
	Super::BeginDestroy();
}

void UDreamVisualPostProcess::OnUnregister()
{
	Super::OnUnregister();
	OnRenderTargetChanged.Broadcast(nullptr);
}

void UDreamVisualPostProcess::PostLoad()
{
	Super::PostLoad();
	// An older build kept the output render target it made for itself in OutputRenderTarget, the author's
	// property, and saved it there. One outered to this visual is that, never an author's asset: it goes,
	// and the visual makes its own again, in AutoOutputRenderTarget.
	if (OutputRenderTarget != nullptr && OutputRenderTarget->GetOuter() == this)
	{
		OutputRenderTarget = nullptr;
	}
}

#if WITH_EDITOR
void UDreamVisualPostProcess::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	bUVChanged = true;
	bLocalVertexPositionChanged = true;
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (RenderType == EDreamBackgroundBlurRenderType::RenderTarget)
	{
		UpdateRenderTarget();
	}
	else
	{
		OnRenderTargetChanged.Broadcast(nullptr);
	}
	
	SendMaskTextureToRenderProxy();
	SendRenderTargetToRenderProxy();
}
bool UDreamVisualPostProcess::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();
	}
	return Super::CanEditChange(InProperty);
}
#endif


void UDreamVisualPostProcess::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
    if (InPivotChange || InWidthChange || InHeightChange)
    {
	    MarkVertexPositionDirty();
    }
	if (InWidthChange || InHeightChange)
	{
		UpdateRenderTarget();
	}
}
void UDreamVisualPostProcess::OnTransformChanged(bool InPositionChanged, bool InScaleChanged)
{
	Super::OnTransformChanged(InPositionChanged, InScaleChanged);
	UpdateRenderTarget();
}

void UDreamVisualPostProcess::MarkVertexPositionDirty()
{
	bLocalVertexPositionChanged = true;
	GetWidget()->MarkCanvasUpdate(true);
}
void UDreamVisualPostProcess::MarkUVDirty()
{
	bUVChanged = true;
	GetWidget()->MarkCanvasUpdate(false);
}

void UDreamVisualPostProcess::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	Super::MarkAllDirty();
	SendRenderTargetToRenderProxy();
}

DECLARE_CYCLE_STAT(TEXT("UIPostProcessRenderable UpdateGeometry"), STAT_UIPostProcessRenderableUpdate, STATGROUP_DreamGUI);
void UDreamVisualPostProcess::UpdateGeometry()
{
	SCOPE_CYCLE_COUNTER(STAT_UIPostProcessRenderableUpdate);
	auto Widget = GetWidget();
	auto RenderCanvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	if (!ensureMsgf(RenderCanvas != nullptr, TEXT("%s: asked for geometry with no widget or no canvas to draw in."), *GetPathName()))
	{
		return;
	}

	Super::UpdateGeometry();
	
	if (bLocalVertexPositionChanged || bUVChanged || bColorChanged)
	{
		Geometry->Clear();
		OnUpdateGeometry(false, bLocalVertexPositionChanged, bUVChanged, bColorChanged);
	}
	if (bClipDataPositionChanged)
	{
		UpdateGeometryClipData(*Geometry.Get(), ClipDataStartPosition);
	}
	if (bLocalVertexPositionChanged || bTransformChanged)
	{
		FDreamUIGeometry::TransformVertices(RenderCanvas, this, Geometry.Get());
	}
	if (bLocalVertexPositionChanged || bUVChanged || bColorChanged || bTransformChanged || bClipDataPositionChanged)
	{
		UpdateRegionVertex();
	}

	bLocalVertexPositionChanged = false;
	bUVChanged = false;
	bColorChanged = false;
	bTransformChanged = false;
}
void UDreamVisualPostProcess::OnUpdateGeometry(bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	//simple rect geometry for render from screen image to mesh region and inverse
	{
		auto& Vertices = Geometry->Vertices;
		auto& OriginVertices = Geometry->OriginVertices;
		FDreamUIGeometry::DreamUIGeometrySetArrayNum(Vertices, 4);
		FDreamUIGeometry::DreamUIGeometrySetArrayNum(OriginVertices, 4);
		if (InVertexUVChanged || InVertexPositionChanged || InVertexColorChanged)
		{
			if (InVertexPositionChanged)
			{
				auto Widget = GetSizeSourceWidget();
				//offset and size
				float pivotOffsetX = 0, pivotOffsetY = 0;
				FDreamUIGeometry::CalculatePivotOffset(Widget->GetWidth(), Widget->GetHeight(), FVector2f(Widget->GetPivot()), pivotOffsetX, pivotOffsetY);
				float halfW = Widget->GetWidth() * 0.5f, halfH = Widget->GetHeight() * 0.5f;
				//positions
				float minX = -halfW + pivotOffsetX;
				float minY = -halfH + pivotOffsetY;
				float maxX = halfW + pivotOffsetX;
				float maxY = halfH + pivotOffsetY;
				OriginVertices[0].Position = FVector3f(0, minX, minY);
				OriginVertices[1].Position = FVector3f(0, maxX, minY);
				OriginVertices[2].Position = FVector3f(0, minX, maxY);
				OriginVertices[3].Position = FVector3f(0, maxX, maxY);
				//snap pixel
				if (Widget->GetPixelSnappingInHierarchy())
				{
					FDreamUIGeometry::AdjustPixelPerfectPos(OriginVertices, 0, 4, Widget->GetRenderCanvas(), this);
				}
			}

			if (InVertexUVChanged)
			{
				Vertices[0].TextureCoordinate[0] = FVector2f(0, 1);
				Vertices[1].TextureCoordinate[0] = FVector2f(1, 1);
				Vertices[2].TextureCoordinate[0] = FVector2f(0, 0);
				Vertices[3].TextureCoordinate[0] = FVector2f(1, 0);
			}

			if (InVertexColorChanged)
			{
				FDreamUIGeometry::UpdateUIColor(Geometry.Get(), GetFinalColor());
			}
		}
	}
}

void UDreamVisualPostProcess::UpdateRegionVertex()
{
	if (RenderScreenToMeshRegionVertexArray.Num() == 0)
	{
		//full screen vertex position
		RenderScreenToMeshRegionVertexArray =
		{
			FDreamUIPostProcessCopyMeshRegionVertex(FVector3f(-1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FDreamUIPostProcessCopyMeshRegionVertex(FVector3f(1, -1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FDreamUIPostProcessCopyMeshRegionVertex(FVector3f(-1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f)),
			FDreamUIPostProcessCopyMeshRegionVertex(FVector3f(1, 1, 0), FVector3f(0.0f, 0.0f, 0.0f))
		};
	}

	auto& Vertices = Geometry->Vertices;
	for (int i = 0; i < 4; i++)
	{
		auto& copyVert = RenderScreenToMeshRegionVertexArray[i];
		copyVert.LocalPosition = Vertices[i].Position;
	}
	
	constexpr int VertexBufferSize = 4;
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

void UDreamVisualPostProcess::UpdateGeometryClipData(FDreamUIGeometry& InMesh, int InDataStartPosition)
{
	auto& vertices = InMesh.Vertices;
	for (int i = 0; i < vertices.Num(); i++)
	{
		vertices[i].TextureCoordinate[1].X = InDataStartPosition;
	}
}

void UDreamVisualPostProcess::SendRegionVertexDataToRenderProxy()
{
	auto Widget = GetSizeSourceWidget();
	auto RenderCanvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	if (RenderProxy.IsValid() && RenderCanvas)
	{
		FDreamUIPostProcessCommonParams Params;
		Params.MeshRegionToScreenVertices = this->RenderMeshRegionToScreenVertexArray;
		Params.ScreenToMeshRegionVertices = this->RenderScreenToMeshRegionVertexArray;
		Params.RectSize = FVector2f(Widget->GetWidth(), Widget->GetHeight());
		Params.ObjectToWorldMatrix = FMatrix44f(RenderCanvas->GetWidget()->GetWorldTransform().ToMatrixWithScale());
		Params.bUseFullSize = bUseFullSize;
		// RGB tints the captured background; the visual's own alpha is left to the effect (background blur reads
		// it as blur strength), so TintStrength travels in the alpha slot instead.
		{
			const FLinearColor LinearTint = FLinearColor(this->GetColor());
			Params.TintColor = FVector4f(LinearTint.R, LinearTint.G, LinearTint.B,
				FMath::Clamp(this->TintStrength, 0.0f, 1.0f));
			Params.TintMode = (int32)this->TintMode;
		}
		{
			Params.BoundingBox = FBox(EForceInit::ForceInit);
			FVector2D Min, Max;
			this->GetGeometryBoundsInLocalSpace(Min, Max);
			auto WorldMin = this->GetWidget()->GetWorldTransform().TransformPosition(FVector(0, Min.X, Min.Y));
			auto WorldMax = this->GetWidget()->GetWorldTransform().TransformPosition(FVector(0, Max.X, Max.Y));
			Params.BoundingBox += WorldMin;
			Params.BoundingBox += WorldMax;
		}
		auto ClipDataTex = this->GetClipDataTexture();
		if (IsValid(ClipDataTex))
		{
			Params.ClipDataTexture = ClipDataTex;
		}
		FDreamVisualPostProcessRenderProxy::SetCommonParams_GameThread(RenderProxy, MoveTemp(Params));
	}
}

void UDreamVisualPostProcess::SetMaskTexture(UTexture2D* Value)
{
	if (MaskTexture != Value)
	{
		MaskTexture = Value;
		SendMaskTextureToRenderProxy();

		bLocalVertexPositionChanged = true;
		bUVChanged = true;
		bColorChanged = true;
		GetWidget()->MarkCanvasUpdate(true);
	}
}
void UDreamVisualPostProcess::SetMaskTextureUVRect(const FVector4& Value)
{
	if (MaskTextureUVRect != Value)
	{
		MaskTextureUVRect = Value;

		bUVChanged = true;
		GetWidget()->MarkCanvasUpdate(false);
	}
}

void UDreamVisualPostProcess::SetTintMode(EDreamPostProcessTintMode Value)
{
	if (TintMode != Value)
	{
		TintMode = Value;
		GetWidget()->MarkCanvasUpdate(false);
		SendRegionVertexDataToRenderProxy();
	}
}

void UDreamVisualPostProcess::SetTintStrength(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(TintStrength, Value))
	{
		TintStrength = Value;
		GetWidget()->MarkCanvasUpdate(false);
		SendRegionVertexDataToRenderProxy();
	}
}

void UDreamVisualPostProcess::SetRenderType(EDreamBackgroundBlurRenderType Value)
{
	if (RenderType != Value)
	{
		RenderType = Value;
		GetWidget()->MarkCanvasUpdate(false);
		SendRenderTargetToRenderProxy();
	}
}

void UDreamVisualPostProcess::SetUseFullSize(bool Value)
{
	if (bUseFullSize != Value)
	{
		bUseFullSize = Value;
		MarkVertexPositionDirty();
	}
}

void UDreamVisualPostProcess::SendMaskTextureToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		FTexture2DResource* MaskTextureResource = nullptr;
		if (IsValid(this->MaskTexture) && this->MaskTexture->GetResource() != nullptr)
		{
			MaskTextureResource = (FTexture2DResource*)this->MaskTexture->GetResource();
		}
		FDreamVisualPostProcessRenderProxy::SetMaskTexture_GameThread(RenderProxy, MaskTextureResource);
	}
}

void UDreamVisualPostProcess::SendRenderTargetToRenderProxy()
{
	if (RenderProxy.IsValid())
	{
		UTextureRenderTarget2D* Target = nullptr;
		if (!bUseFullSize && RenderType == EDreamBackgroundBlurRenderType::RenderTarget && IsValid(GetOutputRenderTarget()))
		{
			Target = GetOutputRenderTarget();
		}
		FDreamVisualPostProcessRenderProxy::SetRenderTarget_GameThread(RenderProxy, Target);
	}
}

bool UDreamVisualPostProcess::HaveValidData()const
{
	return Geometry->Vertices.Num() > 0;
}

bool UDreamVisualPostProcess::LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	if (RaycastType == EDreamVisualRaycastType::Rect)
	{
		return Super::LineTraceUI(OutHit, Start, End);
	}
	else if (RaycastType == EDreamVisualRaycastType::Mesh)
	{
		return LineTraceUIGeometry(Geometry.Get(), OutHit, Start, End);
	}
	else
	{
		return LineTraceUICustom(OutHit, Start, End);
	}
}

UDreamWidget* UDreamVisualPostProcess::GetSizeSourceWidget()const
{
	auto Widget = GetWidget();
	if (bUseFullSize && Widget != nullptr)
	{
		if (auto RenderCanvas = Widget->GetRenderCanvas())
		{
			if (auto RootCanvas = RenderCanvas->GetRootCanvas())
			{
				if (auto RootWidget = RootCanvas->GetWidget())
				{
					return RootWidget;
				}
			}
		}
	}
	return Widget;
}

void UDreamVisualPostProcess::GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const
{
	auto SizeWidget = GetSizeSourceWidget();
	if (SizeWidget == nullptr || SizeWidget == GetWidget())
	{
		Super::GetGeometryBoundsInLocalSpace(OutMinPoint, OutMaxPoint);
		return;
	}
	// Same rect OnUpdateGeometry builds the quad from, expressed the same way, so the bounds and the
	// vertices cannot drift apart.
	float PivotOffsetX = 0, PivotOffsetY = 0;
	FDreamUIGeometry::CalculatePivotOffset(SizeWidget->GetWidth(), SizeWidget->GetHeight(), FVector2f(SizeWidget->GetPivot()), PivotOffsetX, PivotOffsetY);
	const float HalfW = SizeWidget->GetWidth() * 0.5f;
	const float HalfH = SizeWidget->GetHeight() * 0.5f;
	OutMinPoint = FVector2D(-HalfW + PivotOffsetX, -HalfH + PivotOffsetY);
	OutMaxPoint = FVector2D(HalfW + PivotOffsetX, HalfH + PivotOffsetY);
}

void UDreamVisualPostProcess::GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const
{
	FVector2D MinPoint2D, MaxPoint2D;
	GetGeometryBoundsInLocalSpace(MinPoint2D, MaxPoint2D);
	//same depth convention as UDreamVisual::GetGeometryBounds3DInLocalSpace
	OutMinPoint = FVector(0.1f, MinPoint2D.X, MinPoint2D.Y);
	OutMaxPoint = FVector(0.1f, MaxPoint2D.X, MaxPoint2D.Y);
}

void UDreamVisualPostProcess::UpdateRenderTarget()
{
	if (RenderType != EDreamBackgroundBlurRenderType::RenderTarget)return;
	auto Widget = GetWidget();
	FIntPoint DesiredRenderTargetSize(Widget->GetWidth(), Widget->GetHeight());
	static const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DesiredRenderTargetSize.X <= 0 || DesiredRenderTargetSize.Y <= 0)
	{
		return;
	}
	DesiredRenderTargetSize.X = FMath::Min(DesiredRenderTargetSize.X, MaxAllowedDrawSize);
	DesiredRenderTargetSize.Y = FMath::Min(DesiredRenderTargetSize.Y, MaxAllowedDrawSize);

	if (OutputRenderTarget == nullptr && AutoOutputRenderTarget == nullptr)
	{
		// Made here and held apart from the assigned one, so never saved, duplicated or copied: a copy of
		// this visual makes its own. A render target assigned from outside keeps whatever flags its owner
		// gave it.
		AutoOutputRenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, DreamUI::RuntimeObjectFlags);
		AutoOutputRenderTarget->AddressX = TextureAddress::TA_Clamp;
		AutoOutputRenderTarget->AddressY = TextureAddress::TA_Clamp;
		AutoOutputRenderTarget->ClearColor = FLinearColor::Transparent;
		AutoOutputRenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
		SendRenderTargetToRenderProxy();
		OnRenderTargetChanged.Broadcast(AutoOutputRenderTarget);
	}
	else
	{
		UTextureRenderTarget2D* Target = GetOutputRenderTarget();
		if (Target->SizeX != DesiredRenderTargetSize.X || Target->SizeY != DesiredRenderTargetSize.Y)
		{
			Target->ClearColor = FLinearColor::Transparent;
			Target->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate();
#if WITH_EDITOR
			DreamUI::ModifyIfKeptByUndo(*Target);
#endif
			SendRenderTargetToRenderProxy();
		}
	}

#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))
	{
		UTextureRenderTarget2D* Target = GetOutputRenderTarget();
		if (!Target->GameThread_GetRenderTargetResource())
		{
			Target->InitCustomFormat(Target->SizeX, Target->SizeY, EPixelFormat::PF_B8G8R8A8, false);
			SendRenderTargetToRenderProxy();
		}
	}
#endif
}


