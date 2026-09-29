// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/MeshComponent.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "DreamUIRender/DreamUIMeshVertex.h"
#include "DreamUIRender/DreamUIBaseShaders.h"
#include "DreamUIMeshComponent.generated.h"

class FDreamUIDrawCall;
class FDreamUIGeometry;
struct FDreamUIRenderSectionProxy;
struct FDreamUISectionProxy_Mesh;
class FDreamUIMaterialProxy;
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
	/**
	 * Set with Material when DreamGUI answers the material's parameters in its place (r.DreamUI.MaterialWrappers): the
	 * section draws through it, and Material is its source.
	 */
	TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe> MaterialProxy;
	/**
	 * The geometries the vertices here were built from, as the draw call that last set this section up listed them,
	 * and whether normals and tangents went up with them; emptied when anything else writes the vertices. A draw call
	 * built from exactly these, the same way, finds its vertices already here and on the GPU, and takes the section
	 * back without a copy or an upload.
	 */
	TArray<TSharedPtr<const FDreamUIGeometry>> SourceGeometries;
	bool bSourceNormalAndTangent = false;

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

class FDreamUIRenderRoot;
/**
 * A mesh's render root (see DreamUIMeshComponent.cpp): made on the game thread, deleted on the render thread by whichever
 * of the mesh and the scene proxies made for the root lets go of it last.
 */
using FDreamUIRenderRootRef = TSharedPtr<FDreamUIRenderRoot, ESPMode::ThreadSafe>;
DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIMeshRenderRootCreatedDelegate, class UDreamUIMeshComponent*, FDreamUIRenderRoot*);

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
	virtual void OnUnregister() override;
	virtual void BeginDestroy() override;
	/**
	 * On the game thread. The engine runs end-of-frame updates on worker threads unless a component says
	 * otherwise. This one's send the render root its transform, and a mesh registered again makes its root
	 * with its first proxy, from its canvas's settings: both read the mesh and its canvas as the game
	 * thread leaves them.
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
	/**
	 * When InMeshSection holds the layout of InGeometries (FDreamUIDrawCall::GeometryListsShareLayout with the geometries
	 * it was built from): the vertices of each geometry that is not the one it was built from are written into it where
	 * they go, and only those go to the render thread; the section then stands for InGeometries. False, with nothing
	 * written, when it does not hold that layout.
	 */
	bool PatchMeshSection(FDreamUIRenderSection_Mesh* InMeshSection, const TArray<TSharedPtr<const FDreamUIGeometry>>& InGeometries);
	void SetupDirectMeshRenderSection(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, bool bNeedExpandMeshSection, UMaterialInterface* InMaterial);
	void SetDirectMeshRenderSectionMaterial(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, UMaterialInterface* InMaterial);
	void PoolAllRenderSection();
	void SetRenderSectionRenderPriority(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, int32 InSortPriority);
	/** InMaterialProxy, when given, is what the section draws through, InMaterial its source (FDreamUIRenderSection_Mesh::MaterialProxy). */
	void SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial, const TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe>& InMaterialProxy = nullptr);
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

	/** The render thread's side of the sections, while the mesh has one (see EnsureRenderRoot). */
	FDreamUIRenderRoot* GetRenderRoot() const { return RenderRoot.Get(); }
protected:
	/** Both send the render root the transform UE's scene has for the proxy. */
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
public:

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
public:
	/**
	 * After PoolAllRenderSection and before the sections are set up again: every batch-mesh draw call built from
	 * exactly the geometries a pooled section was built from -- the same copies, which are never written -- claims
	 * that section, whose vertices are already here and on the GPU. Then each draw call left claims a pooled section
	 * built from geometries laid out as its own are, to have the vertices that differ written in place
	 * (FDreamUIDrawCall::bPatchClaimedMeshSection). Claimed first, all together, because the pool hands sections out by
	 * size: set up in order, an earlier draw call that changed could take a later one's section, and both would upload.
	 */
	void ClaimPooledMeshSections(TArray<FDreamUIDrawCall>& InOutDrawCalls);
	/**
	 * The geometry lists this mesh's sections were built from, in use or pooled, where their vertices are on the GPU:
	 * what a rebuild may find again, and so need not combine (see FDreamCanvasPreparedDrawCallData).
	 */
	TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> GetMeshSectionGeometryLists() const;
private:
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
		/**
		 * Empty for the whole section. Otherwise VertexBufferData holds only these runs of the section's vertices --
		 * (first vertex, count) -- back to back, and the rest of the section and its indices stay as they are.
		 */
		TArray<TPair<int32, int32>> PatchedRuns;
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
		TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe> MaterialProxy;
	};
	TArray<UpdateMeshSectionMaterialDataStruct> PendingUpdateMeshSectionMaterialDataArray;
	struct UpdateMeshSectionBuiltInDataStruct
	{
		FDreamUIRenderSectionProxy* SectionProxy;
		FDreamUIBuiltInDrawParams Params;
		/** Taken from Params when the command is enqueued, and read by it alone. */
		FDreamUIBuiltInTextures Textures;
	};
	TArray<UpdateMeshSectionBuiltInDataStruct> PendingUpdateMeshSectionBuiltInDataArray;

	friend class FDreamUIRenderSceneProxy;

	/**
	 * Made with the first section set up while the mesh is registered, from the settings it has then; released by
	 * ClearRenderData and when the mesh is unregistered. The scene proxies UE makes for the mesh draw it and hold it too,
	 * so a proxy made again finds the sections where they were.
	 */
	FDreamUIRenderRootRef RenderRoot;
	FDreamUIRenderRoot* EnsureRenderRoot();
	/** Out of the renderer and out of every parent section; the section proxies go with it, and the updates naming them. */
	void ReleaseRenderRoot();
	void PushRenderRootTransform();
	/** Whether the sections carry UE's vertex buffers and vertex factory as well as DreamGUI's own. */
	bool NeedsUERendererSectionData() const;

protected:
	TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> DreamUIRenderer;
	bool bIsDreamUIRenderToWorld = false;//DreamUI renderer render to world or screen
	TWeakObjectPtr<UDreamCanvas> RenderCanvas = nullptr;
	bool bIsSupportUERenderer = true;
	TWeakObjectPtr<UDreamUIMeshComponent> ParentCanvasMeshComp = nullptr;

public:
	/** Broadcast as the mesh makes a render root: a parent canvas's section for this mesh points at the new one. */
	FDreamUIMeshRenderRootCreatedDelegate OnRenderRootCreated;
};


