// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexVisualBatchMesh.h"
#include "LGUI.h"
#include "Core/LexUIClipData.h"
#include "Core/Components/LexCanvas.h"
#include "Utils/LexUIUtils.h"
#include "GeometryModifier/LexVisualBatchMeshModifierBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Core/LexUIDrawCall.h"
#include "Core/LGUIManager.h"

DECLARE_CYCLE_STAT(TEXT("UIBatchMeshRenderable GeometryModifier"), STAT_ApplyModifier, STATGROUP_LGUI);

ULexVisualBatchMesh::ULexVisualBatchMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	VisualType = ELexVisualType::BatchMesh;
	UIGeometry = TSharedPtr<FLexUIGeometry>(new FLexUIGeometry);

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	bTriangleChanged = true;
}

void ULexVisualBatchMesh::BeginPlay()
{
	Super::BeginPlay();

	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	bTriangleChanged = true;

	for (auto Item : MeshModifierArray)
	{
		Item->BeginPlay();
	}
}
void ULexVisualBatchMesh::EndPlay()
{
	Super::EndPlay();

	for (auto Item : MeshModifierArray)
	{
		Item->EndPlay();
	}
}

#if WITH_EDITOR
void ULexVisualBatchMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{

	}
}
#endif

void ULexVisualBatchMesh::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	if (InPivotChange || InWidthChange || InHeightChange)
    {
        MarkVertexPositionDirty();
    }
}

void ULexVisualBatchMesh::MarkVerticesDirty()
{
	bTriangleChanged = true;
	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	bColorChanged = true;
	GetWidget()->MarkCanvasUpdate(false, false, false, true);
}

void ULexVisualBatchMesh::MarkVerticesDirty(bool InTriangleDirty, bool InVertexPositionDirty, bool InVertexUVDirty, bool InVertexColorDirty)
{
	bTriangleChanged = bTriangleChanged || InTriangleDirty;
	bLocalVertexPositionChanged = bLocalVertexPositionChanged || InVertexPositionDirty;
	bUVChanged = bUVChanged || InVertexUVDirty;
	bColorChanged = bColorChanged || InVertexColorDirty;
	GetWidget()->MarkCanvasUpdate(false, bLocalVertexPositionChanged, false);
}

void ULexVisualBatchMesh::MarkVertexPositionDirty()
{
	MarkVerticesDirty(false, true, false, false);
}
void ULexVisualBatchMesh::MarkUVDirty()
{
	MarkVerticesDirty(false, false, true, false);
}

void ULexVisualBatchMesh::MarkTextureDirty()
{
	if (DrawCall.IsValid())
	{
		UIGeometry->Texture = GetTextureToCreateGeometry();
		DrawCall->bTextureChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, false, false);
}
void ULexVisualBatchMesh::MarkMaterialDirty()
{
	if (DrawCall.IsValid())
	{
		UIGeometry->Material = GetMaterialToCreateGeometry();
		DrawCall->bMaterialChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, false, false);
}

void ULexVisualBatchMesh::MarkAllDirty()
{
	bLocalVertexPositionChanged = true;
	bUVChanged = true;
	bTriangleChanged = true;
	if (DrawCall.IsValid())
	{
		UIGeometry->Texture = GetTextureToCreateGeometry();
		DrawCall->bTextureChanged = true;

		UIGeometry->Material = GetMaterialToCreateGeometry();
		DrawCall->bMaterialChanged = true;
	}
	GetWidget()->MarkCanvasUpdate(true, false, false);
	Super::MarkAllDirty();
}

UMaterialInstanceDynamic* ULexVisualBatchMesh::GetMaterialInstanceDynamic()const
{
	if (DrawCall.IsValid() && DrawCall->RenderMaterial.IsValid() && DrawCall->bMaterialContainsLexUIParameter)
	{
		return (UMaterialInstanceDynamic*)DrawCall->RenderMaterial.Get();
	}
	return nullptr;
}

void ULexVisualBatchMesh::GeometryModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	int count = MeshModifierArray.Num();
	if (count > 0)
	{
		for (int i = 0; i < count; i++)
		{
			auto modifierComp = MeshModifierArray[i];
			if (modifierComp->GetEnable())
			{
				bool TempTriangleIndices = false, TempVertexPosition = false, TempUV = false, TempColor = false;
				modifierComp->ModifierWillChangeVertexData(TempTriangleIndices, TempVertexPosition, TempUV, TempColor);
				if (TempTriangleIndices)OutTriangleIndices = true;
				if (TempVertexPosition)OutVertexPosition = true;
				if (TempUV)OutUV = true;
				if (TempColor)OutColor = true;
			}
		}
	}
}

void ULexVisualBatchMesh::ApplyGeometryModifier(bool triangleChanged, bool uvChanged, bool colorChanged, bool vertexPositionChanged)
{
	SCOPE_CYCLE_COUNTER(STAT_ApplyModifier);

	int count = MeshModifierArray.Num();
	if (count > 0)
	{
		for (int i = 0; i < count; i++)
		{
			auto modifierComp = MeshModifierArray[i];
			if (modifierComp->GetEnable())
			{
				modifierComp->ModifyUIGeometry(*(UIGeometry.Get()), triangleChanged, uvChanged, colorChanged, vertexPositionChanged);
			}
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("LexVisualBatchMesh UpdateGeometry"), STAT_UpdateGeometry, STATGROUP_LGUI);
void ULexVisualBatchMesh::UpdateGeometry()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateGeometry);

	Super::UpdateGeometry();

	auto Widget = this->GetWidget();
	check(Widget);
	auto Canvas = Widget->GetRenderCanvas();
	check(Canvas);

	OnBeforeCreateOrUpdateGeometry();
	if (!DrawCall.IsValid()//not add to render yet
		)
	{
		UIGeometry->Clear();
		UIGeometry->Texture = GetTextureToCreateGeometry();
		UIGeometry->Material = GetMaterialToCreateGeometry();
		OnUpdateGeometry(*(UIGeometry.Get()), true, true, true, true);
		OnUpdateGeometryClipData(*(UIGeometry.Get()), true);
		ApplyGeometryModifier(true, true, true, true);
		CalculateLocalBounds();//CalculateLocalBounds must stay before TransformVertices, because TransformVertices will also cache bounds for Canvas to check 2d overlap.

		//it is ok to use AsyncTask here, because we can make sure it completes in current frame
		Canvas->IncreaseThreadProcessingGeometry();
		AsyncTask(ENamedThreads::Type::AnyBackgroundHiPriTask, [this, Canvas]()
		{
			FLexUIGeometry::TransformVertices(Canvas, this, this->UIGeometry.Get());
			Canvas->DecreaseThreadProcessingGeometry();
		});
	}
	else//if geometry is created, update data
	{
		//when use pixel-perfect, the pixel-perfect calculation will take consider transform matrix, so we need to recalculate geometry if pixel-perfect & bTransformChanged
		bool pixelPerfect = this->GetShouldAffectByPixelSnapping() && Widget->GetPixelSnappingInHierarchy();
		bool pixelPerfectAffectTransform = pixelPerfect && bTransformChanged;
		if (bTriangleChanged || bLocalVertexPositionChanged || pixelPerfectAffectTransform || bColorChanged || bUVChanged)
		{
			UIGeometry->Clear();
			//check if GeometryModifier will affect vertex data, if so we need to update these data in OnUpdateGeometry
			{
				bool TempTriangleIndices = false, TempVertexPosition = false, TempUV = false, TempColor = false;
				GeometryModifierWillChangeVertexData(TempTriangleIndices, TempVertexPosition, TempUV, TempColor);
				if (TempTriangleIndices)bTriangleChanged = true;
				if (TempVertexPosition)bLocalVertexPositionChanged = true;
				if (TempUV)bUVChanged = true;
				if (TempColor)bColorChanged = true;
			}
			OnUpdateGeometry(*(UIGeometry.Get()), bTriangleChanged, bLocalVertexPositionChanged || pixelPerfectAffectTransform, bUVChanged, bColorChanged);
			ApplyGeometryModifier(bTriangleChanged, bUVChanged, bColorChanged, bLocalVertexPositionChanged);
			DrawCall->bNeedToUpdateVertex = true;
			if (bLocalVertexPositionChanged || pixelPerfectAffectTransform)//pixelPerfect is affected by transform, and can affect localVertex calculation
			{
				CalculateLocalBounds();//CalculateLocalBounds must stay before TransformVertices, because TransformVertices will also cache bounds for Canvas to check 2d overlap.
			}
		}
		if (bClipDataChanged)
		{
			OnUpdateGeometryClipData(*(UIGeometry.Get()), true);
			DrawCall->bNeedToUpdateVertex = true;
		}
		if (bLocalVertexPositionChanged || bTransformChanged)
		{
			//it is ok to use AsyncTask here, because we can make sure it completes in current frame
			Canvas->IncreaseThreadProcessingGeometry();
			AsyncTask(ENamedThreads::Type::AnyBackgroundHiPriTask, [this, Canvas]()
			{
				FLexUIGeometry::TransformVertices(Canvas, this, this->UIGeometry.Get());
				Canvas->DecreaseThreadProcessingGeometry();
			});
			DrawCall->bNeedToUpdateVertex = true;
		}
	}
	if (UIGeometry->OriginVertices.Num() >= LEXUI_MAX_VERTEX_COUNT)
	{
		auto errorMsg = FText::Format(NSLOCTEXT("UIBatchMeshRenderable", "TooManyTrianglesInSingleUIElement", "{0} Too many vertex ({1}) in single UI element: {2}")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__))
			, UIGeometry->OriginVertices.Num()
#if WITH_EDITOR
			, FText::FromString(Widget->GetOwner()->GetActorLabel())
#else
			, FText::FromString(this->GetPathName())
#endif
		);
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errorMsg, 10);
#endif
		UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg.ToString());
	}

	bTriangleChanged = false;
	bLocalVertexPositionChanged = false;
	bUVChanged = false;
	bColorChanged = false;
	bTransformChanged = false;
	bClipDataChanged = false;
}

bool ULexVisualBatchMesh::LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	switch (RaycastType)
	{
	default:
	case ELexVisualRaycastType::Rect:
		return LineTraceUIRect(OutHit, Start, End);
		break;
	case ELexVisualRaycastType::Mesh:
		return LineTraceUIGeometry(UIGeometry.Get(), OutHit, Start, End);
		break;
	case ELexVisualRaycastType::VisiblePixel:
		return LineTraceVisiblePixel(VisiblePixelThreshold, OutHit, Start, End);
		break;
	case ELexVisualRaycastType::Custom:
		return LineTraceUICustom(OutHit, Start, End);
		break;
	}
}
bool ULexVisualBatchMesh::LineTraceVisiblePixel(float InAlphaThreshold, FHitResult& OutHit, const FVector& Start, const FVector& End)const
{
	auto Widget = this->GetWidget();
	const auto InverseTf = Widget->GetComponentTransform().Inverse();
	const auto LocalSpaceRayOrigin = InverseTf.TransformPosition(Start);
	const auto LocalSpaceRayEnd = InverseTf.TransformPosition(End);

	//DrawDebugLine(this->GetWorld(), Start, End, FColor::Red, false, 5.0f);//just for test
	//check Line-Plane intersection first, then check Line-Triangle
	//start and end point must be different side of X plane
	if (FMath::Sign(LocalSpaceRayOrigin.X) != FMath::Sign(LocalSpaceRayEnd.X))
	{
		//triangle hit test
		//triangle hit test
		auto& originVertices = UIGeometry->OriginVertices;
		auto& vertices = UIGeometry->Vertices;
		auto& triangleIndices = UIGeometry->Triangles;
		const int triangleCount = triangleIndices.Num() / 3;
		int index = 0;
		for (int i = 0; i < triangleCount; i++)
		{
			auto vertIndex0 = triangleIndices[index++];
			auto vertIndex1 = triangleIndices[index++];
			auto vertIndex2 = triangleIndices[index++];
			auto point0 = (FVector)(originVertices[vertIndex0].Position);
			auto point1 = (FVector)(originVertices[vertIndex1].Position);
			auto point2 = (FVector)(originVertices[vertIndex2].Position);
			FVector OutHitPoint, OutHitNormal;
			if (FMath::SegmentTriangleIntersection(LocalSpaceRayOrigin, LocalSpaceRayEnd, point0, point1, point2, OutHitPoint, OutHitNormal))
			{
				OutHit.TraceStart = Start;
				OutHit.TraceEnd = End;
				OutHit.Component = (UPrimitiveComponent*)Widget;//acturally this convert is incorrect, but I need this pointer
				OutHit.Location = Widget->GetComponentTransform().TransformPosition(OutHitPoint);
				OutHit.Normal = Widget->GetComponentTransform().TransformVector(OutHitNormal);
				OutHit.Normal.Normalize();
				OutHit.Distance = FVector::Distance(Start, OutHit.Location);
				OutHit.ImpactPoint = OutHit.Location;
				OutHit.ImpactNormal = OutHit.Normal;

				auto baryCentric = FMath::ComputeBaryCentric2D(OutHitPoint, point0, point1, point2);
				auto& uv0 = vertices[vertIndex0].TextureCoordinate[0];
				auto& uv1 = vertices[vertIndex1].TextureCoordinate[0];
				auto& uv2 = vertices[vertIndex2].TextureCoordinate[0];
				auto uv = FVector2D(baryCentric.X * uv0 + baryCentric.Y * uv1 + baryCentric.Z * uv2);
				//get pixel
				FColor Pixel;
				if (ReadPixelFromMainTexture(uv, Pixel))
				{
					auto AlphaValue = Pixel.A;
					auto AlphaValue01 = FLexUIUtils::Color255To1_Table[AlphaValue];
					if (AlphaValue01 > InAlphaThreshold)
					{
						return true;
					}
					else
					{
						return false;
					}
				}
				else
				{
					return true;
				}
			}
		}
	}
	return false;
}

void ULexVisualBatchMesh::CalculateLocalBounds()
{
	auto& originVertices = UIGeometry->OriginVertices;
	float horizontalMin = MAX_flt, horizontalMax = -MAX_flt;
	float verticalMin = MAX_flt, verticalMax = -MAX_flt;
	float forwardMin = MAX_flt, forwardMax = -MAX_flt;
	if (originVertices.Num() == 0)
	{
		horizontalMin = horizontalMax = verticalMin = verticalMax = 0;
		forwardMin = forwardMax = 0;
	}
	else
	{
		for (auto& Vert : originVertices)
		{
			auto& VertPos = Vert.Position;
			if (VertPos.Y < horizontalMin)
			{
				horizontalMin = VertPos.Y;
			}
			if (VertPos.Y > horizontalMax)
			{
				horizontalMax = VertPos.Y;
			}
			if (VertPos.Z < verticalMin)
			{
				verticalMin = VertPos.Z;
			}
			if (VertPos.Z > verticalMax)
			{
				verticalMax = VertPos.Z;
			}
			if (VertPos.X < forwardMin)
			{
				forwardMin = VertPos.X;
			}
			if (VertPos.X > forwardMax)
			{
				forwardMax = VertPos.X;
			}
		}
	}
	this->LocalMinPoint3D = FVector(forwardMin, horizontalMin, verticalMin);
	this->LocalMaxPoint3D = FVector(forwardMax, horizontalMax, verticalMax);
}

void ULexVisualBatchMesh::GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const
{
	OutMinPoint = FVector2D(this->LocalMinPoint3D.Y, this->LocalMinPoint3D.Z);
	OutMaxPoint = FVector2D(this->LocalMaxPoint3D.Y, this->LocalMaxPoint3D.Z);
}

void ULexVisualBatchMesh::GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const
{
	OutMinPoint = this->LocalMinPoint3D;
	OutMaxPoint = this->LocalMaxPoint3D;
}

UTexture* ULexVisualBatchMesh::GetTextureToCreateGeometry()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveGetTextureToCreateGeometry();
	}
	return nullptr;
}

UMaterialInterface* ULexVisualBatchMesh::GetMaterialToCreateGeometry()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveGetMaterialToCreateGeometry();
	}
	return nullptr;
}

void ULexVisualBatchMesh::OnBeforeCreateOrUpdateGeometry()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnBeforeCreateOrUpdateGeometry();
	}
}

DECLARE_CYCLE_STAT(TEXT("UIBatchMeshRenderable Blueprint.OnFillMesh"), STAT_BatchGeometryRenderable_OnFillMesh, STATGROUP_LGUI);
void ULexVisualBatchMesh::OnUpdateGeometry(FLexUIGeometry& InGeo, bool InTriangleChanged, bool InVertexPositionChanged, bool InVertexUVChanged, bool InVertexColorChanged)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		if (!IsValid(GeometryHelper))
		{
			GeometryHelper = NewObject<ULexUIGeometryHelper>(this);
		}
		GeometryHelper->UIGeo = &InGeo;
		SCOPE_CYCLE_COUNTER(STAT_BatchGeometryRenderable_OnFillMesh);
		ReceiveOnUpdateGeometry(GeometryHelper, InTriangleChanged, InVertexPositionChanged, InVertexUVChanged, InVertexColorChanged);
	}
}

void ULexVisualBatchMesh::OnUpdateGeometryClipData(FLexUIGeometry& InMesh, bool InClipDataStartPositionChanged)
{
	//clip data
	if (InClipDataStartPositionChanged)
	{
		auto& vertices = InMesh.Vertices;
		auto clipDataStartPos = GetClipDataStartPosition();
		for (int i = 0; i < vertices.Num(); i++)
		{
			vertices[i].TextureCoordinate[1].X = clipDataStartPos;
		}
	}
}



void ULexUIGeometryHelper::AddVertexSimple(FVector position, FColor color, FVector2D uv0)
{
#if !UE_BUILD_SHIPPING
	if (position.ContainsNaN()
		|| uv0.ContainsNaN()
		)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Vertex data contains NaN!."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	originVertices.Add(FVector3f(position));
	auto& vertices = UIGeo->Vertices;
	FLexUIMeshVertex vert(FVector3f::ZeroVector);
	vert.Color = color;
	vert.TextureCoordinate[0] = FVector2f(uv0);
	vertices.Add(vert);
}
void ULexUIGeometryHelper::AddVertexFull(FVector position, FColor color, FVector2D uv0, FVector2D uv1, FVector2D uv2, FVector2D uv3, FVector normal, FVector tangent)
{
#if !UE_BUILD_SHIPPING
	if (position.ContainsNaN()
		|| normal.ContainsNaN()
		|| tangent.ContainsNaN()
		|| uv0.ContainsNaN()
		|| uv1.ContainsNaN()
		|| uv2.ContainsNaN()
		|| uv3.ContainsNaN()
		)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Vertex data contains NaN!."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	originVertices.Add(FLexUIOriginVertexData((FVector3f)position, (FVector3f)normal, (FVector3f)tangent));
	auto& vertices = UIGeo->Vertices;
	FLexUIMeshVertex vert(FVector3f::ZeroVector);
	vert.Color = color;
	vert.TextureCoordinate[0] = FVector2f(uv0);
	vert.TextureCoordinate[1] = FVector2f(uv1);
	vert.TextureCoordinate[2] = FVector2f(uv2);
	vert.TextureCoordinate[3] = FVector2f(uv3);
	vertices.Add(vert);
}
void ULexUIGeometryHelper::AddVertexStruct(FLexUIGeometryVertex vertex)
{
#if !UE_BUILD_SHIPPING
	if (vertex.position.ContainsNaN()
		|| vertex.normal.ContainsNaN()
		|| vertex.tangent.ContainsNaN()
		|| vertex.uv0.ContainsNaN()
		|| vertex.uv1.ContainsNaN()
		|| vertex.uv2.ContainsNaN()
		|| vertex.uv3.ContainsNaN()
		)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Vertex data contains NaN!."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
#endif
	auto& originVertices = UIGeo->OriginVertices;
	originVertices.Add(FLexUIOriginVertexData((FVector3f)vertex.position, (FVector3f)vertex.normal, (FVector3f)vertex.tangent));
	auto& vertices = UIGeo->Vertices;
	FLexUIMeshVertex vert(FVector3f::ZeroVector);
	vert.Color = vertex.color;
	vert.TextureCoordinate[0] = FVector2f(vertex.uv0);
	vert.TextureCoordinate[1] = FVector2f(vertex.uv1);
	vert.TextureCoordinate[2] = FVector2f(vertex.uv2);
	vert.TextureCoordinate[3] = FVector2f(vertex.uv3);
	vertices.Add(vert);
}
void ULexUIGeometryHelper::AddTriangle(int index0, int index1, int index2)
{
#if !UE_BUILD_SHIPPING
	int vertCount = UIGeo->Vertices.Num();
	if (index0 >= vertCount || index1 >= vertCount || index2 >= vertCount)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Triangle index reference out of vertex range."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
#endif
	auto& triangles = UIGeo->Triangles;
	triangles.Reserve(triangles.Num() + 3);
	triangles.Add(index0);
	triangles.Add(index1);
	triangles.Add(index2);
}
void ULexUIGeometryHelper::SetMesh(const TArray<FLexUIGeometryVertex>& InVertices, const TArray<int>& InIndices)
{
	int vertCount = InVertices.Num();
#if !UE_BUILD_SHIPPING
	for (auto& i : InIndices)
	{
		if (i >= vertCount)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Triangle index reference out of vertex range."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
	}
	if ((InIndices.Num() % 3) != 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Indices count must be multiple of 3."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	for (auto& vertex : InVertices)
	{
		if (vertex.position.ContainsNaN()
			|| vertex.normal.ContainsNaN()
			|| vertex.tangent.ContainsNaN()
			|| vertex.uv0.ContainsNaN()
			|| vertex.uv1.ContainsNaN()
			|| vertex.uv2.ContainsNaN()
			|| vertex.uv3.ContainsNaN()
			)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Vertex data contains NaN!."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			break;
		}
	}
#endif
	auto& triangles = UIGeo->Triangles;
	triangles.SetNumUninitialized(InIndices.Num());
	for (int i = 0; i < InIndices.Num(); i++)
	{
		triangles[i] = InIndices[i];
	}

	auto& vertices = UIGeo->Vertices;
	auto& originVertices = UIGeo->OriginVertices;
	vertices.SetNumUninitialized(vertCount);
	originVertices.SetNumUninitialized(vertCount);

	for (int i = 0; i < vertCount; i++)
	{
		auto& originVert = InVertices[i];
		originVertices[i] = FLexUIOriginVertexData((FVector3f)originVert.position, (FVector3f)originVert.normal, (FVector3f)originVert.tangent);
		auto& vert = vertices[i];
		vert.Color = originVert.color;
		vert.TextureCoordinate[0] = FVector2f(originVert.uv0);
		vert.TextureCoordinate[1] = FVector2f(originVert.uv1);
		vert.TextureCoordinate[2] = FVector2f(originVert.uv2);
		vert.TextureCoordinate[3] = FVector2f(originVert.uv3);
	}
}

void ULexUIGeometryHelper::AddVertexTriangleStream(const TArray<FLexUIGeometryVertex>& InVertexTriangleStream)
{
#if !UE_BUILD_SHIPPING
	if ((InVertexTriangleStream.Num() % 3) != 0)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Indices count must be multiple of 3."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	for (auto& vertex : InVertexTriangleStream)
	{
		if (vertex.position.ContainsNaN()
			|| vertex.normal.ContainsNaN()
			|| vertex.tangent.ContainsNaN()
			|| vertex.uv0.ContainsNaN()
			|| vertex.uv1.ContainsNaN()
			|| vertex.uv2.ContainsNaN()
			|| vertex.uv3.ContainsNaN()
			)
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Vertex data contains NaN!."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
	}
#endif
	auto& triangles = UIGeo->Triangles;
	auto& vertices = UIGeo->Vertices;
	auto& originVertices = UIGeo->OriginVertices;
	auto vertCount = vertices.Num();
	triangles.Reserve(InVertexTriangleStream.Num());
	for (int i = 0; i < InVertexTriangleStream.Num(); i++)
	{
		triangles.Add(vertCount + i);
	}

	vertices.Reserve(InVertexTriangleStream.Num());
	originVertices.Reserve(InVertexTriangleStream.Num());

	for (int i = 0; i < InVertexTriangleStream.Num(); i++)
	{
		auto& originVert = InVertexTriangleStream[i];
		originVertices.Add(FLexUIOriginVertexData((FVector3f)originVert.position, (FVector3f)originVert.normal, (FVector3f)originVert.tangent));
		FLexUIMeshVertex vert(FVector3f::ZeroVector);
		vert.Color = originVert.color;
		vert.TextureCoordinate[0] = FVector2f(originVert.uv0);
		vert.TextureCoordinate[1] = FVector2f(originVert.uv1);
		vert.TextureCoordinate[2] = FVector2f(originVert.uv2);
		vert.TextureCoordinate[3] = FVector2f(originVert.uv3);
		vertices.Add(vert);
	}
}

void ULexUIGeometryHelper::Clear()
{
	UIGeo->Clear();
}

void ULexUIGeometryHelper::GetVertexTriangleStream(TArray<FLexUIGeometryVertex>& OutVertexTriangleStream)
{
	auto& triangles = UIGeo->Triangles;
	auto& vertices = UIGeo->Vertices;
	auto& originVertices = UIGeo->OriginVertices;
	auto vertCount = vertices.Num();
	OutVertexTriangleStream.Reserve(triangles.Num());
	for (int i = 0; i < triangles.Num(); i++)
	{
		FLexUIGeometryVertex vertex;
		auto vertIndex = triangles[i];
		auto& vert = vertices[vertIndex];
		vertex.uv0 = FVector2D(vert.TextureCoordinate[0]);
		vertex.uv1 = FVector2D(vert.TextureCoordinate[1]);
		vertex.uv2 = FVector2D(vert.TextureCoordinate[2]);
		vertex.uv3 = FVector2D(vert.TextureCoordinate[3]);
		vertex.color = vert.Color;
		auto& originVert = originVertices[vertIndex];
		vertex.position = (FVector)originVert.Position;
		vertex.normal = (FVector)originVert.Normal;
		vertex.tangent = (FVector)originVert.Tangent;
		OutVertexTriangleStream.Add(vertex);
	}
}

FVector2D ULexUIGeometryHelper::CalculatePivotOffset(float InWidth, float InHeight, const FVector2D& InPivot)
{
	FVector2D PivotOffset;
	PivotOffset.X = InWidth * (0.5f - InPivot.X);
	PivotOffset.Y = InHeight * (0.5f - InPivot.Y);
	return PivotOffset;
}
