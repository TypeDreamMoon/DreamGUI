// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/MeshComponent.h"
#include "Core/DreamUIMeshIndex.h"
#include "Core/DreamUIMeshVertex.h"
#include "DreamUIMeshComponent.generated.h"

class FDreamUIDrawCall;
struct FDreamUIRenderSectionProxy;
struct FDreamUISectionProxy_Mesh;
struct FDreamUIRenderSectionProxy_PostProcess;
struct FDreamUIRenderSectionProxy_ChildCanvas;

#define DEBUG_PRINT_MESH_MEMORY 0

enum class EDreamUIRenderSectionType :uint8
{
	Mesh, DirectMesh, PostProcess, ChildCanvas,
};
struct FDreamUIRenderSection
{
	FDreamUIRenderSection(){};
	virtual ~FDreamUIRenderSection(){}
	EDreamUIRenderSectionType Type = EDreamUIRenderSectionType::Mesh;
	int RenderPriority = 0;
	FDreamUIRenderSectionProxy* RenderProxy = nullptr;
	FBox BoundingBox = FBox(EForceInit::ForceInit);//world space bounding box

	virtual void ClearBeforePool() = 0;
};
struct FDreamUIRenderSection_Mesh : public FDreamUIRenderSection
{
	FDreamUIRenderSection_Mesh() 
	{
		Type = EDreamUIRenderSectionType::Mesh; 
	}
	virtual ~FDreamUIRenderSection_Mesh()override{}

	TArray<FDreamUIMeshIndex> TriangleIndices;
	TArray<FDreamUIMeshVertex> Vertices;
	int32 ValidVerticesNum = 0;
	int32 ValidTriangleIndicesNum = 0;

	UMaterialInterface* Material = nullptr;

	void Reset()
	{
		Vertices.Reset();
		TriangleIndices.Reset();
	}
	virtual void ClearBeforePool() override;
};
struct FDreamUIRenderSection_DirectMesh : public FDreamUIRenderSection_Mesh
{
	FDreamUIRenderSection_DirectMesh()
	{
		Type = EDreamUIRenderSectionType::DirectMesh;
	}
	virtual ~FDreamUIRenderSection_DirectMesh()override{}

	TWeakObjectPtr<class UDreamVisualDirectMesh> DirectMeshVisualObject = nullptr;
};
struct FDreamUIRenderSection_PostProcess : public FDreamUIRenderSection
{
	FDreamUIRenderSection_PostProcess()
	{
		Type = EDreamUIRenderSectionType::PostProcess;
	}
	virtual ~FDreamUIRenderSection_PostProcess()override{}

	TWeakObjectPtr<class UDreamVisualPostProcess> PostProcessVisualObject = nullptr;

	virtual void ClearBeforePool() override;
};
struct FDreamUIRenderSection_ChildCanvas : public FDreamUIRenderSection
{
	FDreamUIRenderSection_ChildCanvas()
	{
		Type = EDreamUIRenderSectionType::ChildCanvas;
	}
	virtual ~FDreamUIRenderSection_ChildCanvas()override{}

	TWeakObjectPtr<class UDreamUIMeshComponent> ChildCanvasMeshComponent = nullptr;

	virtual void ClearBeforePool() override;
};

class FDreamUIRenderer;
class IDreamUIRendererPrimitive;
class UDreamCanvas;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIMeshSceneProxyCreateDeleteDelegate, class UDreamUIMeshComponent*, class FDreamUIRenderSceneProxy*);

//DreamUI render mesh
//@todo: split this class to: one for UE renderer && one for DreamUI renderer, will it be more efficient?
UCLASS(ClassGroup = (DreamGUI))
class DREAMGUI_API UDreamUIMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UDreamUIMeshComponent();
	virtual void PostInitProperties() override;
private:
	void UpdateMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection, bool InRequireNormalAndTangent);
	void ExpandMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection);
public:
	TSharedPtr<FDreamUIRenderSection> SetupRenderSection(EDreamUIRenderSectionType InType, FDreamUIDrawCall* InDrawCallData);
	void UpdateMeshSection(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, FDreamUIDrawCall* InDrawCallData);
	void SetupDirectMeshRenderSection(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, bool bNeedExpandMeshSection, UMaterialInterface* InMaterial);
	void SetDirectMeshRenderSectionMaterial(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, UMaterialInterface* InMaterial);
	void PoolAllRenderSection();
	void SetRenderSectionRenderPriority(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, int32 InSortPriority);
	void SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial);

	void Init(UDreamCanvas* InCanvas);
	void SetSupportDreamUIRenderer(bool InSupportOrNot, TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> InDreamUIRenderer, bool InIsRenderToWorld);
	void SetSupportUERenderer(bool InSupportOrNot);
	void ClearRenderData();
	void FlushRenderCommand();

	void SetUITranslucentSortPriority(int32 NewTranslucentSortPriority);

	void VerifyMaterials();
	void SetParentCanvasMeshComp(UDreamUIMeshComponent* InParentCanvasMeshComp);
	void ClearParentCanvasMeshComp(UDreamUIMeshComponent* InParentCanvasMeshComp);

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();
	void UpdateChildCanvasSectionBox();
private:
	TArray<TSharedPtr<FDreamUIRenderSection>> RenderSectionArray;
	TDoubleLinkedList<TSharedPtr<FDreamUIRenderSection>> RenderSectionPool;
	struct FMeshRenderSectionPool
	{
		FMeshRenderSectionPool() = default;
		FMeshRenderSectionPool(const FMeshRenderSectionPool& Other)
		{
		}
		TDoubleLinkedList<TSharedPtr<FDreamUIRenderSection_Mesh>> RenderSections;
	};
	TArray<FMeshRenderSectionPool> RenderSectionMesh_CascadePool;//for mesh section pool, sorted by vertex buffer size, to prevent memory waste of big vertex and index buffer
	TDoubleLinkedList<TSharedPtr<FDreamUIRenderSection_Mesh>>& GetRenderSectionMeshPool(int32 InNumVertices);
#if DEBUG_PRINT_MESH_MEMORY
	int ExpandMeshSectionCount = 0;
#endif
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	struct UpdateMeshSectionDataStruct
	{
		TArray<FDreamUIMeshVertex> VertexBufferData;
		int32 NumVerts;
		int32 NumTriangles;
		TArray<FDreamUIMeshIndex> IndexBufferData;
		bool RequireNormalAndTangent;
		FDreamUISectionProxy_Mesh* Section;
	};
	TArray<UpdateMeshSectionDataStruct> PendingUpdateMeshSectionDataArray;
	struct UpdateRenderSectionPriority
	{
		FDreamUIRenderSectionProxy* SectionProxy;
		int RenderPriority;
	};
	TArray<UpdateRenderSectionPriority> PendingUpdateRenderSectionPriorityArray;
	struct UpdateMeshSectionMaterialDataStruct
	{
		FDreamUIRenderSectionProxy* SectionProxy;
		UMaterialInterface* Material;
	};
	TArray<UpdateMeshSectionMaterialDataStruct> PendingUpdateMeshSectionMaterialDataArray;

	friend class FDreamUIRenderSceneProxy;

protected:
	TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> DreamUIRenderer;
	bool bIsDreamUIRenderToWorld = false;//DreamUI renderer render to world or screen
	TWeakObjectPtr<UDreamCanvas> RenderCanvas = nullptr;
	bool bIsSupportUERenderer = true;
	TWeakObjectPtr<UDreamUIMeshComponent> ParentCanvasMeshComp = nullptr;

public:
	FDreamUIMeshSceneProxyCreateDeleteDelegate OnSceneProxyCreated;
};


