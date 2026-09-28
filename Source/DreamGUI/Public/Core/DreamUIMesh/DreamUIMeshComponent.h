// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/MeshComponent.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "DreamUIRender/DreamUIMeshVertex.h"
#include "DreamUIRender/DreamUIBaseShaders.h"
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
struct DREAMGUI_API FDreamUIRenderSection_Mesh : public FDreamUIRenderSection
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
	/** Set instead of a material when DreamGUI's own renderer draws this section with its built-in shader. */
	FDreamUIBuiltInDrawParams BuiltIn;

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
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
#endif
	virtual void OnRegister() override;
	/**
	 * On the game thread. The engine runs end-of-frame updates on worker threads unless a component says
	 * otherwise, and this one's read and write its canvas -- the proxy's construction can even set the
	 * root canvas -- and hand a child canvas's sections to the parent component's SceneProxy through a
	 * render command. Off the game thread that is a data race, and when parent and child rebuild in the
	 * same frame the command can reach a parent proxy that has already been deleted.
	 */
	virtual bool RequiresGameThreadEndOfFrameUpdates() const override { return true; }
	/**
	 * A play session's copy of the world never holds one of these: see DreamUI::ReportCopiedIntoPlaySession.
	 * The bool overload, the one UPrimitiveComponent overrides and so the one that hides the other.
	 */
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
private:
	/**
	 * A canvas mesh is only ever made by UDreamCanvas::CheckUIMesh, and always transient. One that is not
	 * came from a paste -- an import gives what it creates the pasted actor's flags -- or from a map saved
	 * before the canvas marked its mesh text-export transient. It has no canvas and draws nothing, but its
	 * materials name another panel's tree, and a play-in-editor duplication carries every non-transient
	 * component of an actor, so it would clone that tree. This makes such a mesh inert and keeps it from
	 * being saved, copied or duplicated again.
	 */
	void NeutralizeIfOrphan();
	void UpdateMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection, bool InRequireNormalAndTangent);
	void ExpandMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection);
	/**
	 * Point this frame's not-yet-flushed updates at a replacement section proxy (or drop them if there
	 * is none). Needed because a section proxy can be destroyed, from a render command enqueued
	 * immediately, while updates naming it are still waiting for FlushRenderCommand.
	 */
	void RetargetPendingRenderCommands(FDreamUIRenderSectionProxy* InOldSectionProxy, FDreamUIRenderSectionProxy* InNewSectionProxy);
public:
	TSharedPtr<FDreamUIRenderSection> SetupRenderSection(EDreamUIRenderSectionType InType, FDreamUIDrawCall* InDrawCallData);
	void UpdateMeshSection(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, FDreamUIDrawCall* InDrawCallData);
	void SetupDirectMeshRenderSection(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, bool bNeedExpandMeshSection, UMaterialInterface* InMaterial);
	void SetDirectMeshRenderSectionMaterial(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, UMaterialInterface* InMaterial);
	void PoolAllRenderSection();
	void SetRenderSectionRenderPriority(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, int32 InSortPriority);
	void SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial);
	/** Draw the section with the built-in UI shader (no material). Pass a disabled params struct to go back to the material. */
	void SetMeshSectionBuiltIn(int32 InSectionIndex, const FDreamUIBuiltInDrawParams& InParams);
	bool IsMeshSectionBuiltIn(int32 InSectionIndex) const;

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
	struct UpdateMeshSectionBuiltInDataStruct
	{
		FDreamUIRenderSectionProxy* SectionProxy;
		FDreamUIBuiltInDrawParams Params;
	};
	TArray<UpdateMeshSectionBuiltInDataStruct> PendingUpdateMeshSectionBuiltInDataArray;

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


