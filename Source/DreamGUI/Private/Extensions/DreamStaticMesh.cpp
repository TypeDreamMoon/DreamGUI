// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "StaticMeshResources.h"
#include "Rendering/ColorVertexBuffer.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "UIStaticMesh"

static void StaticMeshToDreamUIMeshRenderData(const UStaticMesh* DataSource, TArray<FDreamUIStaticMeshVertex>& OutVerts, TArray<uint32>& OutIndexes)
{
	const FStaticMeshLODResources& LOD = DataSource->GetRenderData()->LODResources[0];
	const int32 NumSections = LOD.Sections.Num();
	if (NumSections > 1)
	{
		auto WarningText = FText::Format(LOCTEXT("StaticMeshHasMultipleSections", "StaticMesh {0} has {1} sections. UIStaticMesh expects a static mesh with 1 section."), FText::FromString(DataSource->GetName()), NumSections);
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(WarningText, false, 10);
#endif
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *WarningText.ToString());
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
			auto WarningText = FText::Format(LOCTEXT("StaticMeshHasTooManyUVSets", "StaticMesh {0} has {1} UV sets; DreamGUI vertex data supports at most {2}."), FText::FromString(DataSource->GetName()), TexCoordsPerVertex, MAX_SUPPORTED_UV_SETS);
#if WITH_EDITOR
			FDreamUIUtils::EditorNotification(WarningText, false, 10);
#endif
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *WarningText.ToString());
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

			OutVerts.Add(FDreamUIStaticMeshVertex(
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



const TArray<FDreamUIStaticMeshVertex>& UDreamUIStaticMeshCacheData::GetVertexData() const
{
	return VertexData;
}

const TArray<uint32>& UDreamUIStaticMeshCacheData::GetIndexData() const
{
	return IndexData;
}

UMaterialInterface* UDreamUIStaticMeshCacheData::GetMaterial() const
{
	return Material;
}

void UDreamUIStaticMeshCacheData::EnsureValidData()
{
#if WITH_EDITORONLY_DATA
	if (IsValid(MeshAsset))
	{
		InitFromStaticMesh(MeshAsset);
	}
#endif
}

#include "UObject/ObjectSaveContext.h"
#include "Core/DreamUIWidgetRegistry.h"
void UDreamUIStaticMeshCacheData::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
}
#if WITH_EDITOR
void UDreamUIStaticMeshCacheData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (
			PropName == GET_MEMBER_NAME_CHECKED(UDreamUIStaticMeshCacheData, MeshAsset)
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

void UDreamUIStaticMeshCacheData::InitFromStaticMesh(const UStaticMesh* InSourceMesh)
{
	if (SourceMaterial != InSourceMesh->GetMaterial(0))
	{
		SourceMaterial = InSourceMesh->GetMaterial(0);
		Material = SourceMaterial;
	}

	ensureMsgf(Material != nullptr, TEXT("[%s].%d Expected %s to have a material assigned."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSourceMesh->GetFullName());

	StaticMeshToDreamUIMeshRenderData(InSourceMesh, VertexData, IndexData);
	MeshBounds.Init();
	for (const auto& Vert : VertexData)
	{
		MeshBounds += Vert.Position;
	}
	OnMeshDataChange.Broadcast();
}
void UDreamUIStaticMeshCacheData::ClearMeshData()
{
	VertexData.Empty();
	IndexData.Empty();
}
#endif



UDreamStaticMesh::UDreamStaticMesh(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
}

#define ONE_DIVIDE_255 0.0039215686274509803921568627451f

void UDreamStaticMesh::UpdateGeometry()
{
#if WITH_EDITOR
	if (IsValid(MeshCache))
	{
		if (!OnMeshDataChangeDelegateHandle.IsValid())
		{
			OnMeshDataChangeDelegateHandle = MeshCache->OnMeshDataChange.AddUObject(this, &UDreamStaticMesh::OnStaticMeshDataChange);
		}
	}
#endif
}
void UDreamStaticMesh::CreateGeometry()
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
		case EDreamStaticMeshVertexColorType::MultiplyWithUIColor:
			{
				vert.Color = sourceVert.Color;
				auto uiFinalColor = GetFinalColor();
				vert.Color.R = (uint8)((float)vert.Color.R * uiFinalColor.R * ONE_DIVIDE_255);
				vert.Color.G = (uint8)((float)vert.Color.G * uiFinalColor.G * ONE_DIVIDE_255);
				vert.Color.B = (uint8)((float)vert.Color.B * uiFinalColor.B * ONE_DIVIDE_255);
				vert.Color.A = (uint8)((float)vert.Color.A * uiFinalColor.A * ONE_DIVIDE_255);
			}
			break;
		case EDreamStaticMeshVertexColorType::NotAffectByUIColor:
			{
				vert.Color = sourceVert.Color;
			}
			break;
		case EDreamStaticMeshVertexColorType::ReplaceByUIColor:
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
void UDreamStaticMesh::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	auto PropName = PropertyAboutToChange->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UDreamStaticMesh, MeshCache))
	{
		if (IsValid(MeshCache) && OnMeshDataChangeDelegateHandle.IsValid())
		{
			MeshCache->OnMeshDataChange.Remove(OnMeshDataChangeDelegateHandle);
			OnMeshDataChangeDelegateHandle.Reset();
		}
	}
}
void UDreamStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropName = Property->GetFName();
		if (
			PropName == GET_MEMBER_NAME_CHECKED(UDreamStaticMesh, MeshCache)
			|| PropName == GET_MEMBER_NAME_CHECKED(UDreamStaticMesh, VertexColorType)
			)
		{
			if (IsValid(MeshCache))
			{
				if (!OnMeshDataChangeDelegateHandle.IsValid())
				{
					OnMeshDataChangeDelegateHandle = MeshCache->OnMeshDataChange.AddUObject(this, &UDreamStaticMesh::OnStaticMeshDataChange);
				}
				GetWidget()->MarkCanvasUpdate(true);
			}
			else
			{
				ClearMeshData();
			}
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(UDreamStaticMesh, ReplaceMaterial))
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

void UDreamStaticMesh::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamStaticMesh::OnStaticMeshDataChange()
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

void UDreamStaticMesh::OnSupplyMeshSection(TWeakObjectPtr<UDreamUIMeshComponent> InMesh, TWeakPtr<FDreamUIRenderSection_DirectMesh> InSection)
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

bool UDreamStaticMesh::HaveValidData()const
{
	if (IsValid(MeshCache))
	{
		return MeshCache->GetVertexData().Num() > 0 && MeshCache->GetIndexData().Num() > 0;
	}
	return false;
}

/*
 * MeshCache is null on a freshly added static mesh and stays null until somebody picks an asset,
 * which is a normal state and not an error -- HaveValidData three lines above says as much by
 * guarding the very same pointer. This getter did not, and it is the one on the Blueprint side of
 * the wall: GetRenderMaterial and GetOrCreateDynamicMaterialInstance are both BlueprintCallable and
 * both come through here, so asking an unconfigured mesh what it is painted with took the process
 * down rather than answering "nothing yet".
 *
 * Null is the honest answer and the callers already expect it: GetOrCreateDynamicMaterialInstance
 * has a !MaterialInstance branch that logs and returns null, which was unreachable until now.
 */
UMaterialInterface* UDreamStaticMesh::GetMaterial()const
{
	if (IsValid(ReplaceMaterial))
	{
		return ReplaceMaterial;
	}
	if (IsValid(MeshCache))
	{
		return MeshCache->GetMaterial();
	}
	return nullptr;
}

UMaterialInstanceDynamic* UDreamStaticMesh::GetOrCreateDynamicMaterialInstance()
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
		UE_LOG(DreamGUI, Warning, TEXT("[UUIStaticMesh::GetOrCreateDynamicMaterialInstance]Material is invalid on %s."), *GetPathName());
	}

	return MID;
}

void UDreamStaticMesh::SetMesh(UDreamUIStaticMeshCacheData* Value)
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

void UDreamStaticMesh::SetReplaceMaterial(UMaterialInterface* Value)
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

void UDreamStaticMesh::SetVertexColorType(EDreamStaticMeshVertexColorType Value)
{
	if (VertexColorType != Value)
	{
		VertexColorType = Value;
		MarkColorDirty();
	}
}

/*
 * A mesh really does have a natural size, and unusually for this plugin it is already in the right
 * units: CreateGeometry feeds each source vertex position straight through the item-to-canvas
 * transform, so the mesh's own coordinates ARE this widget's local space. Nothing has to be guessed
 * about scale.
 *
 * The flattening is the part to get right. A UI element's local space is the YZ plane -- see
 * UDreamVisual::GetGeometryBounds3DInLocalSpace, which writes (depth, x, y) -- so the mesh's Y
 * extent is the UI width and its Z extent is the UI height, and the X extent is depth that a 2D
 * layout has no slot for and should not be shown.
 *
 * MeshBounds is filled once, in the editor, when the cache is built from the source StaticMesh, and
 * serialised from there; reading it is a field access, which is what the measure contract requires.
 * A cache saved before it existed deserialises with IsValid clear, and that is the case that must
 * abstain rather than report the zero box -- an old asset is missing an answer, not asserting one.
 */
FVector2f UDreamStaticMesh::MeasureMeshBounds() const
{
	if (!IsValid(MeshCache))
	{
		return FVector2f(-1.0f, -1.0f);
	}
	const FBox& Bounds = MeshCache->GetMeshBounds();
	if (Bounds.IsValid == 0)
	{
		return FVector2f(-1.0f, -1.0f);
	}
	const FVector Extent = Bounds.Max - Bounds.Min;
	// A flat mesh drawn edge-on has genuinely zero extent on one axis, and zero is a claim this
	// component is in no position to make on the strength of one degenerate mesh.
	return FVector2f(Extent.Y > 0.0 ? static_cast<float>(Extent.Y) : -1.0f,
		Extent.Z > 0.0 ? static_cast<float>(Extent.Z) : -1.0f);
}

float UDreamStaticMesh::GetPreferredWidth() const
{
	return MeasureMeshBounds().X;
}

float UDreamStaticMesh::GetPreferredHeight() const
{
	return MeasureMeshBounds().Y;
}

#undef LOCTEXT_NAMESPACE


DECLARE_DREAM_GUI_VISUAL("StaticMesh", UDreamStaticMesh)
