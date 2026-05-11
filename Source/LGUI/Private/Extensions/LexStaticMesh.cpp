// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/LexStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "StaticMeshResources.h"
#include "Rendering/ColorVertexBuffer.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIMesh/LexUIMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "UIStaticMesh"

static void StaticMeshToLexUIMeshRenderData(const UStaticMesh* DataSource, TArray<FLexUIStaticMeshVertex>& OutVerts, TArray<uint32>& OutIndexes)
{
	const FStaticMeshLODResources& LOD = DataSource->GetRenderData()->LODResources[0];
	const int32 NumSections = LOD.Sections.Num();
	if (NumSections > 1)
	{
		auto WarningText = FText::Format(LOCTEXT("StaticMeshHasMultipleSections", "StaticMesh {0} has {1} sections. UIStaticMesh expects a static mesh with 1 section."), FText::FromString(DataSource->GetName()), NumSections);
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(WarningText, false, 10);
#endif
		UE_LOG(LGUI, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *WarningText.ToString());
		//@todo: support multiple sections
	}

	// Populate Vertex Data
	{
		const uint32 NumVerts = LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		OutVerts.Empty();
		OutVerts.Reserve(NumVerts);

		static const int32 MAX_SUPPORTED_UV_SETS = 4;
		const int32 TexCoordsPerVertex = LOD.GetNumTexCoords();
		if (TexCoordsPerVertex > MAX_SUPPORTED_UV_SETS)
		{
			auto WarningText = FText::Format(LOCTEXT("StaticMeshHasTooManyUVSets", "StaticMesh {0} has {1} UV sets; LGUI vertex data supports at most {2}."), FText::FromString(DataSource->GetName()), TexCoordsPerVertex, MAX_SUPPORTED_UV_SETS);
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(WarningText, false, 10);
#endif
			UE_LOG(LGUI, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *WarningText.ToString());
		}

		for (uint32 i = 0; i < NumVerts; ++i)
		{
			// Copy Position
			const FVector3f& Position = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(i);

			// Copy Color
			FColor Color = (LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0) ? LOD.VertexBuffers.ColorVertexBuffer.VertexColor(i) : FColor::White;

			// Copy all the UVs that we have, and as many as we can fit.
			const FVector2f& UV0 = (TexCoordsPerVertex > 0) ? LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 0) : FVector2f(1, 1);

			const FVector2f& UV1 = (TexCoordsPerVertex > 1) ? LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 1) : FVector2f(1, 1);

			const FVector2f& UV2 = (TexCoordsPerVertex > 2) ? LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 2) : FVector2f(1, 1);

			const FVector2f& UV3 = (TexCoordsPerVertex > 3) ? LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 3) : FVector2f(1, 1);

			const FVector3f TangentX = FVector3f(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(i));
			const FVector3f TangentZ = FVector3f(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(i));

			OutVerts.Add(FLexUIStaticMeshVertex(
				FVector(Position),
				FVector(TangentX),
				FVector(TangentZ),
				Color,
				FVector2D(UV0),
				FVector2D(UV1),
				FVector2D(UV2),
				FVector2D(UV3)
			));
		}
	}

	// Populate Index data
	{
		FIndexArrayView SourceIndexes = LOD.IndexBuffer.GetArrayView();
		const int32 NumIndexes = SourceIndexes.Num();
		OutIndexes.Empty();
		OutIndexes.Reserve(NumIndexes);
		for (int32 i = 0; i < NumIndexes; ++i)
		{
			OutIndexes.Add(SourceIndexes[i]);
		}


		// Sort the index buffer such that verts are drawn in Z-order.
		// Assume that all triangles are coplanar with Z == SomeValue.
		ensure(NumIndexes % 3 == 0);
		for (int32 a = 0; a < NumIndexes; a += 3)
		{
			for (int32 b = 0; b < NumIndexes; b += 3)
			{
				const float VertADepth = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(OutIndexes[a]).Z;
				const float VertBDepth = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(OutIndexes[b]).Z;
				if (VertADepth < VertBDepth)
				{
					// Swap the order in which triangles will be drawn
					Swap(OutIndexes[a + 0], OutIndexes[b + 0]);
					Swap(OutIndexes[a + 1], OutIndexes[b + 1]);
					Swap(OutIndexes[a + 2], OutIndexes[b + 2]);
				}
			}
		}
	}
}



const TArray<FLexUIStaticMeshVertex>& ULexUIStaticMeshCacheData::GetVertexData() const
{
	return VertexData;
}

const TArray<uint32>& ULexUIStaticMeshCacheData::GetIndexData() const
{
	return IndexData;
}

UMaterialInterface* ULexUIStaticMeshCacheData::GetMaterial() const
{
	return Material;
}

void ULexUIStaticMeshCacheData::EnsureValidData()
{
#if WITH_EDITORONLY_DATA
	if (IsValid(MeshAsset))
	{
		InitFromStaticMesh(MeshAsset);
	}
#endif
}

#include "UObject/ObjectSaveContext.h"
void ULexUIStaticMeshCacheData::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
}
#if WITH_EDITOR
void ULexUIStaticMeshCacheData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (
			PropName == GET_MEMBER_NAME_CHECKED(ULexUIStaticMeshCacheData, MeshAsset)
			)
		{
			if (IsValid(MeshAsset))
			{
				EnsureValidData();
			}
			else
			{
				ClearMeshData();
			}
		}
	}
}

void ULexUIStaticMeshCacheData::InitFromStaticMesh(const UStaticMesh* InSourceMesh)
{
	if (SourceMaterial != InSourceMesh->GetMaterial(0))
	{
		SourceMaterial = InSourceMesh->GetMaterial(0);
		Material = SourceMaterial;
	}

	ensureMsgf(Material != nullptr, TEXT("[%s].%d Expected %s to have a material assigned."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSourceMesh->GetFullName());

	StaticMeshToLexUIMeshRenderData(InSourceMesh, VertexData, IndexData);
	MeshBounds.Init();
	for (const auto& Vert : VertexData)
	{
		MeshBounds += Vert.Position;
	}
	OnMeshDataChange.Broadcast();
}
void ULexUIStaticMeshCacheData::ClearMeshData()
{
	VertexData.Empty();
	IndexData.Empty();
}
#endif



ULexStaticMesh::ULexStaticMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

#define ONE_DIVIDE_255 0.0039215686274509803921568627451f

void ULexStaticMesh::UpdateGeometry()
{
#if WITH_EDITOR
	if (IsValid(MeshCache))
	{
		if (!OnMeshDataChangeDelegateHandle.IsValid())
		{
			OnMeshDataChangeDelegateHandle = MeshCache->OnMeshDataChange.AddUObject(this, &ULexStaticMesh::OnStaticMeshDataChange);
		}
	}
#endif
}
void ULexStaticMesh::CreateGeometry()
{
	const auto& SourceVertexData = MeshCache->GetVertexData();
	const auto& SourceIndexData = MeshCache->GetIndexData();
	auto NumVertices = SourceVertexData.Num();
	auto NumIndices = SourceIndexData.Num();
	if (NumVertices <= 0 || NumIndices <= 0)return;

	auto Widget = GetWidget();
	auto RenderCanvas = Widget->GetRenderCanvas();
	FTransform ItemToCanvasTf;
	auto CanvasUIItem = RenderCanvas->GetWidget();
	auto InverseCanvasTf = CanvasUIItem->GetWorldTransform().Inverse();
	const auto& ItemTf = Widget->GetWorldTransform();
	FTransform::Multiply(&ItemToCanvasTf, &ItemTf, &InverseCanvasTf);
	
	bool bNeedExpandMeshSection = false;
	auto MeshSectionPtr = MeshSection.Pin().Get();
	auto& VertexData = MeshSectionPtr->Vertices;

	if (VertexData.Num() < NumVertices)
	{
		VertexData.SetNumUninitialized(NumVertices);
		bNeedExpandMeshSection = true;
	}
	MeshSectionPtr->ValidVerticesNum = NumVertices;
	bool RequireNormalAndTangent = RenderCanvas->GetActualRequireNormalAndTangent();
	auto tempVertexColorType = VertexColorType;

	for (int i = 0; i < NumVertices; i++)
	{
		auto& sourceVert = SourceVertexData[i];
		auto& vert = VertexData[i];
		vert.Position = FVector3f(ItemToCanvasTf.TransformPosition(sourceVert.Position));
		if (RequireNormalAndTangent)
		{
			vert.TangentZ = ItemToCanvasTf.TransformVector(sourceVert.TangentZ);
			vert.TangentZ.Vector.W = -127;
			vert.TangentX = ItemToCanvasTf.TransformVector(sourceVert.TangentX);
		}
		switch (tempVertexColorType)
		{
		case ELexStaticMeshVertexColorType::MultiplyWithUIColor:
			{
				vert.Color = sourceVert.Color;
				auto uiFinalColor = GetFinalColor();
				vert.Color.R = (uint8)((float)vert.Color.R * uiFinalColor.R * ONE_DIVIDE_255);
				vert.Color.G = (uint8)((float)vert.Color.G * uiFinalColor.G * ONE_DIVIDE_255);
				vert.Color.B = (uint8)((float)vert.Color.B * uiFinalColor.B * ONE_DIVIDE_255);
				vert.Color.A = (uint8)((float)vert.Color.A * uiFinalColor.A * ONE_DIVIDE_255);
			}
			break;
		case ELexStaticMeshVertexColorType::NotAffectByUIColor:
			{
				vert.Color = sourceVert.Color;
			}
			break;
		case ELexStaticMeshVertexColorType::ReplaceByUIColor:
			{
				vert.Color = GetFinalColor();
			}
			break;
		}

		vert.TextureCoordinate[0] = FVector2f(sourceVert.UV0);
		vert.TextureCoordinate[1] = FVector2f(sourceVert.UV1);
		vert.TextureCoordinate[2] = FVector2f(sourceVert.UV2);
		vert.TextureCoordinate[3] = FVector2f(sourceVert.UV3);
	}

	auto& IndexData = MeshSectionPtr->TriangleIndices;
	if (IndexData.Num() < NumIndices)
	{
		IndexData.SetNumUninitialized(NumIndices);
		bNeedExpandMeshSection = true;
	}
	MeshSectionPtr->ValidTriangleIndicesNum = NumIndices;
	for (int i = 0; i < NumIndices; i++)
	{
		IndexData[i] = SourceIndexData[i];
	}
	MeshSectionPtr->BoundingBox = MeshCache->GetMeshBounds();

	PostFillMeshData();
	
	Mesh->SetupDirectMeshRenderSection(MeshSectionPtr, bNeedExpandMeshSection, ReplaceMaterial);
}

#if WITH_EDITOR
void ULexStaticMesh::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(ULexStaticMesh, MeshCache))
	{
		if (IsValid(MeshCache) && OnMeshDataChangeDelegateHandle.IsValid())
		{
			MeshCache->OnMeshDataChange.Remove(OnMeshDataChangeDelegateHandle);
			OnMeshDataChangeDelegateHandle.Reset();
		}
	}
}
void ULexStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (
			PropName == GET_MEMBER_NAME_CHECKED(ULexStaticMesh, MeshCache)
			|| PropName == GET_MEMBER_NAME_CHECKED(ULexStaticMesh, VertexColorType)
			)
		{
			if (IsValid(MeshCache))
			{
				if (!OnMeshDataChangeDelegateHandle.IsValid())
				{
					OnMeshDataChangeDelegateHandle = MeshCache->OnMeshDataChange.AddUObject(this, &ULexStaticMesh::OnStaticMeshDataChange);
				}
				GetWidget()->MarkCanvasUpdate(true);
			}
			else
			{
				ClearMeshData();
			}
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(ULexStaticMesh, ReplaceMaterial))
		{
			if (Mesh.IsValid() && MeshSection.IsValid())
			{
				Mesh->SetDirectMeshRenderSectionMaterial(MeshSection.Pin().Get(), ReplaceMaterial);
			}
			else
			{
				GetWidget()->MarkCanvasUpdate(true);
			}
		}
	}
}

void ULexStaticMesh::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexStaticMesh::OnStaticMeshDataChange()
{
	if (Mesh.IsValid() && MeshSection.IsValid())
	{
		if (HaveValidData())
		{
			CreateGeometry();
		}
	}
}
#endif

void ULexStaticMesh::OnSupplyMeshSection(TWeakObjectPtr<ULexUIMeshComponent> InMesh, TWeakPtr<FLexUIRenderSection_DirectMesh> InSection)
{
	Super::OnSupplyMeshSection(InMesh, InSection);
	if (HaveValidData())
	{
		if (bLocalVertexPositionChanged || bTransformChanged || bColorChanged)
		{
			CreateGeometry();
			bLocalVertexPositionChanged = false;
			bTransformChanged = false;
			bColorChanged = false;
		}
	}
}

bool ULexStaticMesh::HaveValidData()const
{
	if (IsValid(MeshCache))
	{
		return MeshCache->GetVertexData().Num() > 0 && MeshCache->GetIndexData().Num() > 0;
	}
	return false;
}

UMaterialInterface* ULexStaticMesh::GetMaterial()const
{
	if (IsValid(ReplaceMaterial))
	{
		return ReplaceMaterial;
	}
	else
	{
		return MeshCache->GetMaterial();
	}
}

UMaterialInstanceDynamic* ULexStaticMesh::GetOrCreateDynamicMaterialInstance()
{
	UMaterialInterface* MaterialInstance = GetMaterial();
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MaterialInstance);

	if (MaterialInstance && !MID)
	{
		// Create and set the dynamic material instance.
		MID = UMaterialInstanceDynamic::Create(MaterialInstance, this);
		SetReplaceMaterial(MID);
	}
	else if (!MaterialInstance)
	{
		UE_LOG(LGUI, Warning, TEXT("[UUIStaticMesh::GetOrCreateDynamicMaterialInstance]Material is invalid on %s."), *GetPathName());
	}

	return MID;
}

void ULexStaticMesh::SetMesh(ULexUIStaticMeshCacheData* Value)
{
	if (MeshCache != Value)
	{
		MeshCache = Value;
		if (HaveValidData())
		{
			if (Mesh.IsValid() && MeshSection.IsValid())
			{
				CreateGeometry();
			}
		}
	}
}

void ULexStaticMesh::SetReplaceMaterial(UMaterialInterface* Value)
{
	if (ReplaceMaterial != Value)
	{
		ReplaceMaterial = Value;
		if (Mesh.IsValid() && MeshSection.IsValid())
		{
			Mesh->SetDirectMeshRenderSectionMaterial(MeshSection.Pin().Get(), ReplaceMaterial);
		}
		else
		{
			GetWidget()->MarkCanvasUpdate(true);
		}
	}
}

void ULexStaticMesh::SetVertexColorType(ELexStaticMeshVertexColorType Value)
{
	if (VertexColorType != Value)
	{
		VertexColorType = Value;
		MarkColorDirty();
	}
}

#undef LOCTEXT_NAMESPACE

