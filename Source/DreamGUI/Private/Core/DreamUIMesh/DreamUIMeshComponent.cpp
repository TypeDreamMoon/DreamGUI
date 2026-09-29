// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DreamUIRender/DreamUIMaterialProxy.h"
#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "DreamUIRender/IDreamUIRendererPrimitive.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/Engine.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneProxy.h"
#include "SceneView.h"
#include "Core/DreamUIDrawCall.h"
#include "DreamUIRender/DreamVisualPostProcessRenderProxy.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamUIRuntimeObject.h"
#include "RHIResourceUtils.h"


#define LOCTEXT_NAMESPACE "DreamUIMeshComponent"


enum class EDreamUIRenderSectionProxyType :uint8
{
	Mesh, PostProcess, ChildCanvas,
};
struct FDreamUIRenderSectionProxy
{
	virtual ~FDreamUIRenderSectionProxy() 
	{

	}

	EDreamUIRenderSectionProxyType Type;

	/** Sort order */
	int SectionRenderPriority = 0;
	bool bCanRender = true;

	virtual void Disable() = 0;
};
/** Class representing a single section of the DreamUI mesh */
struct FDreamUISectionProxy_Mesh : public FDreamUIRenderSectionProxy
{
	/** Material applied to this section */
	UMaterialInterface* Material = nullptr;
	/** What the section draws through instead of Material's own render proxy, when DreamGUI answers its parameters. */
	TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe> MaterialProxy;
	FMaterialRenderProxy* GetMaterialRenderProxy() const
	{
		if (MaterialProxy.IsValid())
		{
			return MaterialProxy.Get();
		}
		return Material != nullptr ? Material->GetRenderProxy() : nullptr;
	}
	/** Built-in shader parameters; when enabled the material is not used by DreamGUI's renderer. */
	FDreamUIBuiltInDrawParams BuiltIn;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	FDreamUIMeshVertexBuffer DreamUIVertexBuffers;
	/** Index buffer for this section */
	FDreamUIMeshIndexBuffer IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;

	uint32 ValidVerticesCount = 0;
	uint32 NumPrimitives = 0;

	FDreamUISectionProxy_Mesh(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FDreamUISectionProxy_Mesh")
	{
		Type = EDreamUIRenderSectionProxyType::Mesh;
	}
	virtual ~FDreamUISectionProxy_Mesh()override
	{
		IndexBuffer.ReleaseResource();
		DreamUIVertexBuffers.ReleaseResource();
		VertexBuffers.PositionVertexBuffer.ReleaseResource();
		VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		VertexBuffers.ColorVertexBuffer.ReleaseResource();
		VertexFactory.ReleaseResource();
	}

	static inline void InitOrUpdateResource(FRHICommandListImmediate& RHICmdList, FRenderResource* Resource)
	{
		if (!Resource->IsInitialized())
		{
			Resource->InitResource(RHICmdList);
		}
		else
		{
			Resource->UpdateRHI(RHICmdList);
		}
	}

	void InitFromDreamUIVertexData(TArray<FDreamUIMeshVertex>& Vertices)
	{
		auto LightMapIndex = 0;
		VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		if (Vertices.Num())
		{
			VertexBuffers.PositionVertexBuffer.Init(Vertices.Num());
			VertexBuffers.StaticMeshVertexBuffer.Init(Vertices.Num(), LEXUI_VERTEX_TEXCOORDINATE_COUNT);
			VertexBuffers.ColorVertexBuffer.Init(Vertices.Num());

			for (int32 i = 0; i < Vertices.Num(); i++)
			{
				const auto& Vertex = Vertices[i];

				VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
				VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
				for (uint32 j = 0; j < LEXUI_VERTEX_TEXCOORDINATE_COUNT; j++)
				{
					VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, j, Vertex.TextureCoordinate[j]);
				}
				VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
			}
		}
		else
		{
			VertexBuffers.PositionVertexBuffer.Init(1);
			VertexBuffers.StaticMeshVertexBuffer.Init(1, 1);
			VertexBuffers.ColorVertexBuffer.Init(1);

			VertexBuffers.PositionVertexBuffer.VertexPosition(0) = FVector3f(0, 0, 0);
			VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(0, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
			VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2f(0, 0));
			VertexBuffers.ColorVertexBuffer.VertexColor(0) = FColor(1, 1, 1, 1);
			LightMapIndex = 0;
		}

		FStaticMeshVertexBuffers* Self = &VertexBuffers;
		FLocalVertexFactory* VertexFactoryPtr = &VertexFactory;
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_InitFromDreamUIVertexData)(
			[VertexFactoryPtr, Self, LightMapIndex](FRHICommandListImmediate& RHICmdList)
			{
				InitOrUpdateResource(RHICmdList, &Self->PositionVertexBuffer);
				InitOrUpdateResource(RHICmdList, &Self->StaticMeshVertexBuffer);
				InitOrUpdateResource(RHICmdList, &Self->ColorVertexBuffer);

				FLocalVertexFactory::FDataType Data;
				Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactoryPtr, Data);
				Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactoryPtr, Data);
				Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactoryPtr, Data);
				Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactoryPtr, Data, LightMapIndex);
				Self->ColorVertexBuffer.BindColorVertexBuffer(VertexFactoryPtr, Data);
				VertexFactoryPtr->SetData(RHICmdList, Data);

				InitOrUpdateResource(RHICmdList, VertexFactoryPtr);
			});
	}

	/**
	 * Pooled: not drawn until a draw call takes the section back. The vertex and index buffers stay, which
	 * a direct mesh's section relies on -- its visual resends only geometry that changed -- but the
	 * material goes: it belongs to the visual, and a visual hidden or destroyed while its section waits in
	 * the pool can be collected, material and all. A direct mesh's section used to keep both, and went on
	 * drawing the old mesh through a material that might no longer exist.
	 */
	virtual void Disable() override
	{
		Material = nullptr;
		BuiltIn = FDreamUIBuiltInDrawParams();
		bCanRender = false;
	}
};
struct FDreamUIRenderSectionProxy_PostProcess : public FDreamUIRenderSectionProxy
{
	FDreamUIRenderSectionProxy_PostProcess()
	{
		Type = EDreamUIRenderSectionProxyType::PostProcess;
	}

	/**
	 * Shared with the visual that produced it. This section and the visual are torn down on their own
	 * schedules -- the visual's BeginDestroy runs long before the canvas pools its sections -- so the
	 * section holds a reference of its own and the proxy survives until whichever of them is last.
	 */
	FDreamVisualPostProcessRenderProxyPtr PostProcessRenderProxy;

	virtual void Disable() override
	{
		PostProcessRenderProxy.Reset();
		bCanRender = false;
	}
};
class FDreamUIRenderRoot;
struct FDreamUIRenderSectionProxy_ChildCanvas : public FDreamUIRenderSectionProxy
{
	FDreamUIRenderSectionProxy_ChildCanvas()
	{
		Type = EDreamUIRenderSectionProxyType::ChildCanvas;
	}

	FPrimitiveComponentId PrimitiveComponentID;
	FDreamUIRenderRoot* ChildCanvasRoot = nullptr;

	virtual void Disable() override
	{
		PrimitiveComponentID = FPrimitiveComponentId();
		ChildCanvasRoot = nullptr;
		bCanRender = false;
	}
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIRenderRootReleaseDelegate, class FDreamUIRenderRoot*);

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh CreateRenderSection"), STAT_CreateRenderSection, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUIMesh UpdateMeshSection_RT"), STAT_UpdateMeshSectionRT, STATGROUP_DreamGUI);

class FDreamUIRenderSceneProxy;

/**
 * DreamGUI's side of a canvas mesh on the render thread: its sections in draw order -- their buffers, their materials,
 * the post-process and child canvas sections -- the commands that write them, and the renderer's primitive interface
 * over them. The mesh makes one with its first section and keeps it until ClearRenderData or until the mesh is
 * unregistered, whatever becomes of its scene proxies in between: a proxy made for the root draws the same sections
 * through UE's renderer and holds the root while it lives, so a proxy the engine makes again finds the sections as they
 * were instead of building them again. The root is drawn only while a proxy made for it lives (AttachOwner_RenderThread),
 * at the transform the mesh sends it, which is the one UE's scene has for that proxy.
 */
class FDreamUIRenderRoot : public IDreamUIRendererPrimitive
{
public:
	/** Where the sections are drawn: what UE's scene has for the mesh's proxy. */
	struct FTransformData
	{
		FMatrix LocalToWorld = FMatrix::Identity;
		FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInitToZero);
		FBoxSphereBounds LocalBounds = FBoxSphereBounds(ForceInitToZero);
	};
	/** What the mesh knows of itself and its canvas when it makes the root. */
	struct FSettings
	{
		ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
		TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> Renderer;
		bool bRenderToWorld = false;
		bool bIsRenderCanvas = false;
		/** The sections carry UE's vertex buffers and vertex factory as well: see UDreamUIMeshComponent::NeedsUERendererSectionData. */
		bool bNeedsUERendererSectionData = true;
		int32 RenderPriority = 0;
		/** The canvas, as the renderer tells world-space canvases apart: compared, never dereferenced. */
		FObjectKey CanvasKey;
		float BlendDepth = 0.0f;
		int32 DepthFade = 0;
		FPrimitiveComponentId PrimitiveComponentId;
		FTransformData Transform;
#if !UE_BUILD_SHIPPING
		FString DebugName;
#endif
	};

	/** Deleted on the render thread, by whichever of the mesh and the proxies made for the root lets go of it last. */
	static FDreamUIRenderRootRef Make(const FSettings& InSettings)
	{
		return FDreamUIRenderRootRef(new FDreamUIRenderRoot(InSettings), [](FDreamUIRenderRoot* InRoot)
			{
				if (IsInRenderingThread())
				{
					delete InRoot;
				}
				else
				{
					ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_Delete)(
						[InRoot](FRHICommandListImmediate& RHICmdList)
						{
							delete InRoot;
						});
				}
			});
	}

	virtual ~FDreamUIRenderRoot() override
	{
		Release_RenderThread();
	}

	/** Game thread, once the sections the mesh already had are in: into the DreamUI renderer's lists, when there is one. */
	void Register()
	{
		if (!bIsSupportDreamUIRenderer)
		{
			return;
		}
		//the renderer only ever compares the canvas key, never dereferences it, and an FObjectKey stays
		//distinct from a later object that happens to reuse the same address
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_AddPrimitive)(
			[WeakRenderer = DreamUIRenderer, Primitive = this, Key = CanvasKey, Blend = BlendDepth, Fade = DepthFade, bToWorld = bIsDreamUIRenderToWorld](FRHICommandListImmediate& RHICmdList)
			{
				if (WeakRenderer.IsValid())
				{
					if (bToWorld)
					{
						WeakRenderer.Pin()->AddWorldSpacePrimitive_RenderThread(Key, Blend, Fade, Primitive);
					}
					else
					{
						WeakRenderer.Pin()->AddScreenSpacePrimitive_RenderThread(Primitive);
					}
				}
			}
		);
	}

	/**
	 * Render thread: out of every parent section that holds it and out of the renderer's lists, its sections deleted --
	 * what the mesh does when it lets the root go. A proxy that still holds it draws nothing from then on.
	 */
	void Release_RenderThread()
	{
		if (bReleased)
		{
			return;
		}
		bReleased = true;
		OnRelease.Broadcast(this);
#if !UE_BUILD_SHIPPING
		DebugName = FString::Printf(TEXT("%s_Released"), *DebugName);
#endif
		for(auto Section : SectionArray)
		{
			if (Section != nullptr)
			{
				DetachChildCanvasSection_RenderThread(Section);
				delete Section;
			}
		}
		SectionArray.Empty();
		if (DreamUIRenderer.IsValid())
		{
			if (bIsDreamUIRenderToWorld)
			{
				DreamUIRenderer.Pin()->RemoveWorldSpacePrimitive_RenderThread(this);
			}
			else
			{
				DreamUIRenderer.Pin()->RemoveScreenSpacePrimitive_RenderThread(this);
			}
			DreamUIRenderer.Reset();
		}
	}

	/**
	 * Render thread, before the scene adds the proxy made for this root: the root is drawn while that proxy lives, on its
	 * own or only through a parent's section (a child canvas) as bInIsRenderCanvas says -- decided as the proxy is made,
	 * as it was when the proxy made the root.
	 */
	void AttachOwner_RenderThread(FDreamUIRenderSceneProxy* InOwner, bool bInIsRenderCanvas)
	{
		Owner = InOwner;
		bIsRenderCanvas = bInIsRenderCanvas;
	}
	/** Render thread, as a proxy made for this root is deleted: one made after it may have taken its place already. */
	void DetachOwner_RenderThread(const FDreamUIRenderSceneProxy* InOwner)
	{
		if (Owner == InOwner)
		{
			Owner = nullptr;
		}
	}
	void SetTransform_RenderThread(const FTransformData& InTransform)
	{
		Transform = InTransform;
		bDeterminantNegative = InTransform.LocalToWorld.Determinant() < 0.0f;
	}

	/** The proxy last made for this root, while it lives. */
	FDreamUIRenderSceneProxy* GetOwner() const { return Owner; }
	const TArray<FDreamUIRenderSectionProxy*>& GetSections() const { return SectionArray; }

	/**
	 * Game thread, before Register: a section proxy for each section the mesh had before it had this root. A child canvas
	 * section is hooked into its child's root on the render thread, whose that root's delegate is.
	 */
	void CreateSections(const TArray<TSharedPtr<FDreamUIRenderSection>>& InSrcSections)
	{
		SectionArray.SetNumZeroed(InSrcSections.Num());
		for (int SectionIndex = 0; SectionIndex < InSrcSections.Num(); SectionIndex++)
		{
			auto Section = CreateSectionData(InSrcSections[SectionIndex].Get());
			SectionArray[SectionIndex] = Section;
		}
		bNeedToSortRenderSections = true;
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_HookChildCanvasSections)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				for (FDreamUIRenderSectionProxy* Section : SectionArray)
				{
					HookChildCanvasSection_RenderThread(Section);
				}
			});
	}

	void AddSectionData(FDreamUIRenderSection* SrcSection)
	{
		auto Section = CreateSectionData(SrcSection);
		check (Section);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_AddSectionData)(
			[this, Section](FRHICommandListImmediate& RHICmdList)
			{
				SectionArray.Add(Section);
				HookChildCanvasSection_RenderThread(Section);
			}
		);
		bNeedToSortRenderSections = true;
	}

	void RecreateSectionData(FDreamUIRenderSection* InSrcSection)
	{
		auto OldSection = InSrcSection->RenderProxy;
		auto NewSection = CreateSectionData(InSrcSection);
		check(NewSection);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_ReplaceSectionData)(
			[this, OldSection, NewSection](FRHICommandListImmediate& RHICmdList) {
				const auto SectionIndex = SectionArray.IndexOfByKey(OldSection);
				if (SectionIndex == INDEX_NONE)
				{
					/**
					 * The old proxy is not in the array: it was never added (the section was created
					 * after this proxy, and its AddSectionData command has not run yet), or a pool pass
					 * took it out. SectionArray[INDEX_NONE] = NewSection wrote one slot before the
					 * array. Add the replacement instead, which is what an absent old one means.
					 */
					SectionArray.Add(NewSection);
				}
				else
				{
					SectionArray[SectionIndex] = NewSection;
				}
				delete OldSection;
			});
	}
	/**
	 * Takes the section proxy already resolved on the game thread, not the render section that owns it.
	 * FDreamUIRenderSection_PostProcess is game-thread state -- the canvas pools it and hands it to the
	 * next draw call -- so reading its RenderProxy field from inside the command would be a cross-thread
	 * read of something that may have been repointed by then. Every other deferred section update in
	 * this file passes the resolved proxy the same way (see UpdateRenderSectionPriority and friends).
	 */
	void UpdatePostProcessSection(FDreamUIRenderSectionProxy* InSectionProxy, FDreamVisualPostProcessRenderProxyPtr InRenderProxy)
	{
		check(InSectionProxy != nullptr && InSectionProxy->Type == EDreamUIRenderSectionProxyType::PostProcess);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_ReplaceSectionData)(
			[SectionProxy = static_cast<FDreamUIRenderSectionProxy_PostProcess*>(InSectionProxy)
				, InRenderProxy = MoveTemp(InRenderProxy)](FRHICommandListImmediate& RHICmdList) {
				SectionProxy->PostProcessRenderProxy = InRenderProxy;
				SectionProxy->bCanRender = true;
			});
	}
	/**
	 * Same rule as UpdatePostProcessSection above: the section proxy is resolved on the game thread and
	 * captured, so the command never reads FDreamUIRenderSection_ChildCanvas -- which the canvas pools
	 * and repoints on its own schedule. The child's id and root are game-thread snapshots too.
	 */
	void UpdateChildCanvasSection(FDreamUIRenderSectionProxy* InSectionProxy, FPrimitiveComponentId InChildComponentId, FDreamUIRenderRoot* InChildRoot)
	{
		check(InSectionProxy != nullptr && InSectionProxy->Type == EDreamUIRenderSectionProxyType::ChildCanvas);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_ReplaceSectionData)(
			[this, SectionProxy = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(InSectionProxy)
				, CompID = InChildComponentId, ChildRoot = InChildRoot](FRHICommandListImmediate& RHICmdList) {
				SectionProxy->PrimitiveComponentID = CompID;
				if (ChildRoot != nullptr)
				{
					LinkChildCanvas_RenderThread(SectionProxy, ChildRoot);
				}
				SectionProxy->bCanRender = true;
			});
	}

	/** Game thread: the section proxy for InSrcSection, with its buffers' initialization enqueued. */
	FDreamUIRenderSectionProxy* CreateSectionData(FDreamUIRenderSection* InSrcSection);

	void SetChildCanvasSectionData_RenderThread(FPrimitiveComponentId CompID, FDreamUIRenderRoot* ChildRoot)
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			if (Section == nullptr)continue;
			if (Section->Type == EDreamUIRenderSectionProxyType::ChildCanvas)
			{
				auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(Section);
				if (ChildCanvasSection->PrimitiveComponentID == CompID
					)
				{
					LinkChildCanvas_RenderThread(ChildCanvasSection, ChildRoot);
				}
			}
		}
	}
	void ClearChildCanvasSectionData_RenderThread(FDreamUIRenderRoot* ChildRoot)
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			if (Section == nullptr)continue;
			if (Section->Type == EDreamUIRenderSectionProxyType::ChildCanvas)
			{
				auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(Section);
				if (ChildCanvasSection->ChildCanvasRoot == ChildRoot)//child could already get new proxy, so need to check it
				{
					ChildCanvasSection->ChildCanvasRoot->OnRelease.RemoveAll(this);
					ChildCanvasSection->ChildCanvasRoot = nullptr;
					return;
				}
			}
		}
	}

	void PoolAllSectionData_RenderThread()
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			// Null entries are normal here -- CreateSectionData returns one for an empty mesh section, and
			// every other walk of this array already skips them. This is where a post-process section lets
			// go of its half of the render proxy, so it is not a loop to crash in.
			if (auto Section = SectionArray[i])
			{
				// Unhook from the child canvas's root first. Disable forgets the child's root, and with it
				// the only way to take this root's callback back off the child's OnRelease -- which would
				// then call into this root when the child is released, after this one may be gone.
				DetachChildCanvasSection_RenderThread(Section);
				Section->Disable();
			}
		}
	}

	void SetRenderPriority_RenderThread(int32 NewPriority)
	{
		RenderPriority = NewPriority;
	}
	void SetMeshSectionMaterial_RenderThread(FDreamUIRenderSectionProxy* Section, UMaterialInterface* Material, const TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe>& MaterialProxy = nullptr)
	{
		auto MeshSection = static_cast<FDreamUISectionProxy_Mesh*>(Section);
		MeshSection->Material = Material;
		MeshSection->MaterialProxy = MaterialProxy;
	}
	/** A pooled mesh section taken back by a draw call without new geometry: drawn again, with InMaterial. */
	void EnableMeshSection_RenderThread(FDreamUIRenderSectionProxy* Section, UMaterialInterface* Material)
	{
		auto MeshSection = static_cast<FDreamUISectionProxy_Mesh*>(Section);
		MeshSection->Material = Material;
		MeshSection->bCanRender = true;
	}
	void SetMeshSectionBuiltIn_RenderThread(FDreamUIRenderSectionProxy* Section, const FDreamUIBuiltInDrawParams& Params)
	{
		(static_cast<FDreamUISectionProxy_Mesh*>(Section))->BuiltIn = Params;
	}

	void SetRenderSectionRenderPriority_RenderThread(FDreamUIRenderSectionProxy* Section, int32 NewPriority)
	{
		Section->SectionRenderPriority = NewPriority;
		bNeedToSortRenderSections = true;
	}

	void SortMeshSectionRenderPriority_RenderThread()
	{
		Algo::Sort(SectionArray, [](const FDreamUIRenderSectionProxy* A, const FDreamUIRenderSectionProxy* B) {
			if (A != nullptr && B != nullptr)
			{
				return A->SectionRenderPriority < B->SectionRenderPriority;
			}
			else if (A == nullptr)
			{
				return false;
			}
			else if (B == nullptr)
			{
				return true;
			}
			return false;
			});
	}
	/** The sections in draw order, sorted first if a priority changed since they last were. */
	void SortIfAsked_RenderThread()
	{
		if (bNeedToSortRenderSections)
		{
			bNeedToSortRenderSections = false;
			SortMeshSectionRenderPriority_RenderThread();
		}
	}

	void DetachChildCanvasSection_RenderThread(FDreamUIRenderSectionProxy* Section)
	{
		if (Section == nullptr || Section->Type != EDreamUIRenderSectionProxyType::ChildCanvas)
		{
			return;
		}
		auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(Section);
		if (ChildCanvasSection->ChildCanvasRoot != nullptr)
		{
			ChildCanvasSection->ChildCanvasRoot->OnRelease.RemoveAll(this);
		}
		ChildCanvasSection->ChildCanvasRoot = nullptr;
	}
	/** Render thread: a child canvas section drawing InChildRoot, hooked into its release. */
	void LinkChildCanvas_RenderThread(FDreamUIRenderSectionProxy_ChildCanvas* InSection, FDreamUIRenderRoot* InChildRoot)
	{
		if (InSection->ChildCanvasRoot == InChildRoot)
		{
			return;
		}
		if (InSection->ChildCanvasRoot != nullptr)
		{
			InSection->ChildCanvasRoot->OnRelease.RemoveAll(this);
		}
		InSection->ChildCanvasRoot = InChildRoot;
		if (InChildRoot != nullptr)
		{
			InChildRoot->OnRelease.AddRaw(this, &FDreamUIRenderRoot::ClearChildCanvasSectionData_RenderThread);
		}
	}
	/** Render thread: a section the game thread made and that just joined the root, hooked into the release of the child root it names. */
	void HookChildCanvasSection_RenderThread(FDreamUIRenderSectionProxy* InSection)
	{
		if (InSection != nullptr && InSection->Type == EDreamUIRenderSectionProxyType::ChildCanvas)
		{
			auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(InSection);
			if (ChildCanvasSection->ChildCanvasRoot != nullptr)
			{
				ChildCanvasSection->ChildCanvasRoot->OnRelease.AddRaw(this, &FDreamUIRenderRoot::ClearChildCanvasSectionData_RenderThread);
			}
		}
	}

#if DEBUG_PRINT_MESH_MEMORY
	uint32 CalculateMeshMemorySize_RT()
	{
		MeshMemorySize = sizeof(FDreamUISectionProxy_Mesh);
		MaxVertexBufferSize = 0;
		for (auto Section : SectionArray)
		{
			if (Section->Type == EDreamUIRenderSectionProxyType::Mesh)
			{
				auto MeshSection = static_cast<FDreamUISectionProxy_Mesh*>(Section);
				auto VertexBufferSize = MeshSection->DreamUIVertexBuffers.Vertices.Num() * sizeof(FDreamUIMeshVertexBuffer);
				if (MaxVertexBufferSize < VertexBufferSize) MaxVertexBufferSize = VertexBufferSize;
				MeshMemorySize += VertexBufferSize;
				MeshMemorySize += MeshSection->IndexBuffer.Indices.Num() * sizeof(FDreamUIMeshIndex);
			}
		}
		return MeshMemorySize;
	}
	uint32 GetMeshMemorySize()const { return MeshMemorySize; }
	uint32 GetMaxVertexBufferSize()const{return MaxVertexBufferSize;}
#endif

	/** The UE renderer's copy of one vertex, in the section's split buffers. */
	static void SetUERendererVertex(FDreamUISectionProxy_Mesh* Section, int32 Index, const FDreamUIMeshVertex& DreamUIVert, bool RequireNormalAndTangent)
	{
		Section->VertexBuffers.PositionVertexBuffer.VertexPosition(Index) = DreamUIVert.Position;
		Section->VertexBuffers.ColorVertexBuffer.VertexColor(Index) = DreamUIVert.Color;
		if (RequireNormalAndTangent)
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(Index, DreamUIVert.TangentX.ToFVector3f(), DreamUIVert.GetTangentY(), DreamUIVert.TangentZ.ToFVector3f());
		Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 0, DreamUIVert.TextureCoordinate[0]);
		Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 1, DreamUIVert.TextureCoordinate[1]);
		Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 2, DreamUIVert.TextureCoordinate[2]);
		Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(Index, 3, DreamUIVert.TextureCoordinate[3]);
	}

	/** The UE renderer's split buffers, sent up whole from their copies here. */
	static void UploadUERendererVertexBuffers_RenderThread(FRHICommandListImmediate& RHICmdList, FDreamUISectionProxy_Mesh* Section, bool RequireNormalAndTangent)
	{
		{
			auto& VertexBuffer = Section->VertexBuffers.PositionVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
		}

		{
			auto& VertexBuffer = Section->VertexBuffers.ColorVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
		}

		if (RequireNormalAndTangent)
		{
			auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTangentSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
			RHICmdList.UnlockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
		}

		{
			auto& VertexBuffer = Section->VertexBuffers.StaticMeshVertexBuffer;
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
			RHICmdList.UnlockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
		}
	}

	/**
	 * Called on the render thread to write runs of a section's vertices -- RunVertices holds them back to back, in the
	 * order of Runs, each (first vertex, count) -- leaving the rest of the section and its indices as they are.
	 */
	void PatchSection_RenderThread(FRHICommandListImmediate& RHICmdList
		, const TArray<FDreamUIMeshVertex>& RunVertices, const TArray<TPair<int32, int32>>& Runs
		, bool RequireNormalAndTangent
		, FDreamUISectionProxy_Mesh* Section)const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_PatchSection_RenderThread);
		check(IsInRenderingThread());
		check(Section != nullptr);
		if (bIsSupportDreamUIRenderer)
		{
			// A dynamic buffer cannot be written in part -- locking one hands out new memory -- so the runs go into the
			// copy it was last filled from, and all of that goes up again.
			TArray<FDreamUIMeshVertex>& Held = Section->DreamUIVertexBuffers.Vertices;
			check(Held.Num() >= static_cast<int32>(Section->ValidVerticesCount));
			int32 Read = 0;
			for (const TPair<int32, int32>& Run : Runs)
			{
				check(Run.Key >= 0 && Run.Key + Run.Value <= Held.Num());
				FMemory::Memcpy(Held.GetData() + Run.Key, RunVertices.GetData() + Read, Run.Value * sizeof(FDreamUIMeshVertex));
				Read += Run.Value;
			}
			const uint32 VertexDataLength = Section->ValidVerticesCount * sizeof(FDreamUIMeshVertex);
			void* VertexBufferData = RHICmdList.LockBuffer(Section->DreamUIVertexBuffers.VertexBufferRHI, 0, VertexDataLength, RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, Held.GetData(), VertexDataLength);
			RHICmdList.UnlockBuffer(Section->DreamUIVertexBuffers.VertexBufferRHI);
		}
		if (bNeedsUERendererSectionData)
		{
			int32 Read = 0;
			for (const TPair<int32, int32>& Run : Runs)
			{
				for (int32 Offset = 0; Offset < Run.Value; ++Offset)
				{
					SetUERendererVertex(Section, Run.Key + Offset, RunVertices[Read++], RequireNormalAndTangent);
				}
			}
			UploadUERendererVertexBuffers_RenderThread(RHICmdList, Section, RequireNormalAndTangent);
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FRHICommandListImmediate& RHICmdList
		, TArray<FDreamUIMeshVertex>& MeshVertexData, const int32& NumVerts
		, const TArray<FDreamUIMeshIndex>& MeshIndexData, const int32& NumTriangles
		, bool RequireNormalAndTangent
		, FDreamUISectionProxy_Mesh* Section)const
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionRT);
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_UpdateSection_RenderThread);

		check(IsInRenderingThread());

		// Check it references a valid section
		check(Section != nullptr);
		Section->ValidVerticesCount = NumVerts;
		Section->bCanRender = true;
		//vertex buffer
		if (bIsSupportDreamUIRenderer)
		{
			uint32 VertexDataLength = NumVerts * sizeof(FDreamUIMeshVertex);
			void* VertexBufferData = RHICmdList.LockBuffer(Section->DreamUIVertexBuffers.VertexBufferRHI, 0, VertexDataLength, RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, MeshVertexData.GetData(), VertexDataLength);
			RHICmdList.UnlockBuffer(Section->DreamUIVertexBuffers.VertexBufferRHI);
		}
		if(bNeedsUERendererSectionData)
		{
			for (int i = 0; i < NumVerts; i++)
			{
				SetUERendererVertex(Section, i, MeshVertexData[i], RequireNormalAndTangent);
			}
			UploadUERendererVertexBuffers_RenderThread(RHICmdList, Section, RequireNormalAndTangent);
		}

		Section->NumPrimitives = NumTriangles;
		uint32 IndicesDataLength = NumTriangles * 3 * sizeof(FDreamUIMeshIndex);
		// Lock index buffer
		auto IndexBufferData = RHICmdList.LockBuffer(Section->IndexBuffer.IndexBufferRHI, 0, IndicesDataLength, RLM_WriteOnly);
		FMemory::Memcpy(IndexBufferData, MeshIndexData.GetData(), IndicesDataLength);
		RHICmdList.UnlockBuffer(Section->IndexBuffer.IndexBufferRHI);

		// What the DreamUI vertex buffer holds now, kept for a patch to write its runs into (PatchSection_RenderThread).
		// Taken whole when the copy kept is no longer; the copy kept stays the size the buffer was made at otherwise.
		if (bIsSupportDreamUIRenderer)
		{
			TArray<FDreamUIMeshVertex>& Held = Section->DreamUIVertexBuffers.Vertices;
			if (Held.Num() <= MeshVertexData.Num())
			{
				Held = MoveTemp(MeshVertexData);
			}
			else
			{
				FMemory::Memcpy(Held.GetData(), MeshVertexData.GetData(), NumVerts * sizeof(FDreamUIMeshVertex));
			}
		}
	}

	//begin IDreamUIRendererPrimitive interface
	virtual FVector3f DreamUI_GetWorldPositionForSortTranslucent()const override
	{
		return FVector3f(Transform.LocalToWorld.GetOrigin());
	}
	virtual void DreamUI_CollectRenderData(TArray<FDreamUIPrimitiveDataContainer>& OutRenderData) override
	{
#if DEBUG_PRINT_MESH_MEMORY
		CalculateMeshMemorySize_RT();
#endif
		CollectRenderData_Implement(OutRenderData);
	}
	virtual void DreamUI_GetMeshElements(const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector, const FDreamUIPrimitiveDataContainer& PrimitiveData, TArray<FDreamUIMeshBatchContainer>& ResultArray) override
	{
		if (!bIsSupportDreamUIRenderer)return;
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FMaterialRenderProxy* WireframeMaterialInstance = nullptr;
		if (bWireframe)
		{
			WireframeMaterialInstance = GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr;
		}
		// The renderer draws a wireframe in the wireframe view mode, and over a lit view with mesh edges shown.
		const bool bAnyWireframePass = ViewFamily.ViewMode == VMI_Wireframe || ViewFamily.EngineShowFlags.MeshEdges;
		const FMatrix& LocalToWorld = Transform.LocalToWorld;

		for (int i = 0; i < PrimitiveData.Sections.Num(); i++)
		{
			auto SectionData = PrimitiveData.Sections[i];
			auto RenderSection = SectionData.SectionPointer;

			auto Section = static_cast<FDreamUISectionProxy_Mesh*>(RenderSection);
			FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->GetMaterialRenderProxy();
			if (MaterialProxy == nullptr && !Section->BuiltIn.bEnabled)
			{
				continue;//nothing to draw it with
			}

			// Draw the mesh.
			FMeshBatch Mesh;
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &Section->IndexBuffer;
			BatchElement.PrimitiveIdMode = PrimID_ForceZero;
			Mesh.bWireframe = bWireframe;
			Mesh.MaterialRenderProxy = MaterialProxy;

			// The built-in shader takes the transform from the batch and reads no primitive uniform buffer; a material reads
			// one, and so does the wireframe.
			if (bWireframe || bAnyWireframePass || !Section->BuiltIn.bEnabled)
			{
				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), LocalToWorld, LocalToWorld, Transform.Bounds, Transform.LocalBounds, false, false, false);
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
			}

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = Section->NumPrimitives;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = Section->ValidVerticesCount - 1;
			Mesh.ReverseCulling = bDeterminantNegative;
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;

			FDreamUIMeshBatchContainer MeshBatchContainer;
			MeshBatchContainer.Mesh = Mesh;
			MeshBatchContainer.VertexBufferRHI = Section->DreamUIVertexBuffers.VertexBufferRHI;
			MeshBatchContainer.IndexBufferRHI = Section->IndexBuffer.IndexBufferRHI;
			MeshBatchContainer.NumVerts = Section->ValidVerticesCount;
			MeshBatchContainer.BuiltIn = bWireframe ? FDreamUIBuiltInDrawParams() : Section->BuiltIn;
			MeshBatchContainer.LocalToWorld = LocalToWorld;
			ResultArray.Add(MeshBatchContainer);
		}
	}

	virtual FDreamVisualPostProcessRenderProxyPtr DreamUI_GetPostProcessElement(FDreamUIRenderSectionProxy* SectionPtr)const override
	{
		check(SectionPtr->Type == EDreamUIRenderSectionProxyType::PostProcess);
		return (static_cast<FDreamUIRenderSectionProxy_PostProcess*>(SectionPtr))->PostProcessRenderProxy;
	}
	virtual int DreamUI_GetRenderPriority()const override
	{
		return RenderPriority;
	}
	virtual bool DreamUI_CanRender()const override
	{
		return Owner != nullptr && bIsRenderCanvas && SectionArray.Num() > 0;
	}
	virtual FPrimitiveComponentId DreamUI_GetPrimitiveComponentId() const override
	{
		return PrimitiveComponentId;
	}
	virtual FBoxSphereBounds DreamUI_GetWorldBounds()const override { return Transform.Bounds; }
	//end IDreamUIRendererPrimitive interface
	void CollectRenderData_Implement(TArray<FDreamUIPrimitiveDataContainer>& OutRenderDataArray)
	{
		if (SectionArray.Num() <= 0)return;
		SortIfAsked_RenderThread();

		if (SectionArray[0] == nullptr)return;
		auto PrevRenderSectionType = SectionArray[0]->Type;
		auto PrevPrimitiveType = PrevRenderSectionType == EDreamUIRenderSectionProxyType::PostProcess ? EDreamUIRendererPrimitiveType::PostProcess : EDreamUIRendererPrimitiveType::Mesh;
		FDreamUIPrimitiveDataContainer CurrentRenderData;
		CurrentRenderData.Primitive = this;
		CurrentRenderData.Type = PrevPrimitiveType;
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto RenderSection = SectionArray[i];
			if (RenderSection == nullptr)continue;
			if (!RenderSection->bCanRender)continue;
			if (RenderSection->Type != PrevRenderSectionType)//render section type change, collect prev data
			{
				if (CurrentRenderData.Sections.Num() > 0)
				{
					OutRenderDataArray.Add(CurrentRenderData);
				}
				PrevRenderSectionType = RenderSection->Type;
				CurrentRenderData = FDreamUIPrimitiveDataContainer();
				CurrentRenderData.Primitive = this;
				auto ItemPrimitiveType = RenderSection->Type == EDreamUIRenderSectionProxyType::PostProcess ? EDreamUIRendererPrimitiveType::PostProcess : EDreamUIRendererPrimitiveType::Mesh;
				CurrentRenderData.Type = ItemPrimitiveType;
			}

			switch (RenderSection->Type)
			{
			case EDreamUIRenderSectionProxyType::Mesh:
				{
					FDreamUIPrimitiveSectionDataContainer SectionData;
					SectionData.SectionPointer = RenderSection;
					CurrentRenderData.Sections.Add(SectionData);
				}
				break;
			case EDreamUIRenderSectionProxyType::PostProcess:
				{
					auto Section = static_cast<FDreamUIRenderSectionProxy_PostProcess*>(RenderSection);
					if (Section->PostProcessRenderProxy.IsValid() && Section->PostProcessRenderProxy->CanRender())
					{
						FDreamUIPrimitiveSectionDataContainer SectionData;
						SectionData.SectionPointer = RenderSection;
						CurrentRenderData.Sections.Add(SectionData);
					}
				}
				break;
			case EDreamUIRenderSectionProxyType::ChildCanvas:
				{
					auto Section = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(RenderSection);
					auto ChildRoot = Section->ChildCanvasRoot;
					//drawn while a proxy made for it lives, like a root of its own (DreamUI_CanRender)
					if (ChildRoot != nullptr && ChildRoot->GetOwner() != nullptr)
					{
						ChildRoot->CollectRenderData_Implement(OutRenderDataArray);
					}
				}
				break;
			}
		}
		if (CurrentRenderData.Sections.Num() > 0)
		{
			OutRenderDataArray.Add(CurrentRenderData);
		}
	}

private:
	explicit FDreamUIRenderRoot(const FSettings& InSettings)
		: Transform(InSettings.Transform)
		, bDeterminantNegative(InSettings.Transform.LocalToWorld.Determinant() < 0.0f)
		, PrimitiveComponentId(InSettings.PrimitiveComponentId)
		, CanvasKey(InSettings.CanvasKey)
		, BlendDepth(InSettings.BlendDepth)
		, DepthFade(InSettings.DepthFade)
		, FeatureLevel(InSettings.FeatureLevel)
		, RenderPriority(InSettings.RenderPriority)
		, DreamUIRenderer(InSettings.Renderer)
		, bIsSupportDreamUIRenderer(InSettings.Renderer.IsValid())
		, bIsDreamUIRenderToWorld(InSettings.bRenderToWorld)
		, bIsRenderCanvas(InSettings.bIsRenderCanvas)
		, bNeedsUERendererSectionData(InSettings.bNeedsUERendererSectionData)
	{
#if !UE_BUILD_SHIPPING
		DebugName = InSettings.DebugName;
#endif
	}

	/** Tells the roots whose sections hold this one as a child that it is going. */
	FDreamUIRenderRootReleaseDelegate OnRelease;
	/** Render thread: the proxy last made for this root, while it lives. */
	FDreamUIRenderSceneProxy* Owner = nullptr;
	FTransformData Transform;
	bool bDeterminantNegative = false;
	FPrimitiveComponentId PrimitiveComponentId;
	FObjectKey CanvasKey;
	float BlendDepth = 0.0f;
	int32 DepthFade = 0;
	bool bReleased = false;
	ERHIFeatureLevel::Type FeatureLevel;
	TArray<FDreamUIRenderSectionProxy*> SectionArray;
#if DEBUG_PRINT_MESH_MEMORY
	uint32 MeshMemorySize = 0;
	uint32 MaxVertexBufferSize = 0;
#endif
	int32 RenderPriority = 0;
	TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> DreamUIRenderer;
	bool bIsSupportDreamUIRenderer = false;
	bool bIsDreamUIRenderToWorld = false;
	bool bNeedToSortRenderSections = true;
	bool bIsRenderCanvas = false;
	bool bNeedsUERendererSectionData = true;
};

/** DreamUI render scene proxy: UE's renderer's side of a canvas mesh, drawing the sections of the root it holds. */
class FDreamUIRenderSceneProxy : public FPrimitiveSceneProxy
{
public:
	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	FDreamUIRenderSceneProxy(UDreamUIMeshComponent* InComponent, const FDreamUIRenderRootRef& InRoot, bool InIsRenderCanvas)
		: FPrimitiveSceneProxy(InComponent)
		, Root(InRoot)
		, MaterialRelevance(InComponent->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateRenderSection);
		bIsSupportUERenderer = InComponent->bIsSupportUERenderer;
#if WITH_EDITOR
		//Only the level editor's world. A game world has hit proxies compiled out of it, and an editor
		//PREVIEW world (the widget designer, thumbnail capture) does its own picking against the widget
		//tree rather than against the viewport's hit proxy map.
		bDrawForEditorHitProxies = !bIsSupportUERenderer && DreamUI::GetWorldType(InComponent) == EWorldType::Editor;
		if (bDrawForEditorHitProxies)
		{
			//Resolved here rather than where it is used: a proxy is built on the game thread, while
			//GetDynamicMeshElements runs on the rendering thread or on one of its gather tasks, which is
			//no place to reach through a UObject. The default material outlives every scene proxy -- the
			//engine holds it for its own lifetime -- so a bare pointer to its render proxy is enough.
			EditorHitProxyFallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		}
#endif
		// Before the scene adds this proxy, which is enqueued after it: the root is drawn from then on, at this proxy's
		// transform, on its own or only through its parent's section as InIsRenderCanvas says.
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_AttachOwner)(
			[RootPtr = Root.Get(), Proxy = this, InIsRenderCanvas](FRHICommandListImmediate& RHICmdList)
			{
				RootPtr->AttachOwner_RenderThread(Proxy, InIsRenderCanvas);
			});
	}

	virtual ~FDreamUIRenderSceneProxy() override
	{
		Root->DetachOwner_RenderThread(this);
	}

	FDreamUIRenderRoot* GetRoot() const { return Root.Get(); }

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!bIsSupportUERenderer)
		{
#if WITH_EDITOR
			/**
			 * The DreamUI renderer draws this canvas from a view extension, which never touches the
			 * primitive pipeline, so a hit proxy render of the level viewport finds nothing where the UI
			 * is and the cursor falls through to whatever is behind it. Emitting the same geometry into
			 * the hit proxy view family -- and into no other -- gives the viewport a surface to test the
			 * cursor against. It is not a second rendering of the UI: that family draws depth and hit
			 * proxy ids into an offscreen target the editor only ever reads pixels back from.
			 */
			//Built-in sections carry no material because DreamGUI's own renderer shades them, so without
			//the stand-in there is nothing to draw them with and nothing worth emitting.
			if (bDrawForEditorHitProxies && EditorHitProxyFallbackMaterial != nullptr
				&& ViewFamily.EngineShowFlags.HitProxies && Root->DreamUI_CanRender())
			{
				GetMeshElements_UERenderer(Views, ViewFamily, VisibilityMap, Collector, EditorHitProxyFallbackMaterial);
			}
#endif
			return;
		}
		if (!Root->DreamUI_CanRender())return;
		GetMeshElements_UERenderer(Views, ViewFamily, VisibilityMap, Collector);
	}
	/**
	 * InFallbackMaterial is set only by the editor-only hit proxy path above: it stands in for the
	 * sections DreamGUI's own renderer would have shaded, and it doubles as that path's marker, because
	 * picking wants neither of the two things a real draw does. It must not consume the root's sort
	 * request -- the DreamUI renderer reads the same flag when it collects its render data, and draw
	 * order means nothing to a depth-tested pass that writes ids -- and it must not draw wireframe, which
	 * would leave only the edges of the UI pickable in a wireframe viewport.
	 */
	void GetMeshElements_UERenderer(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, FMaterialRenderProxy* InFallbackMaterial = nullptr) const
	{
		if (InFallbackMaterial == nullptr)
		{
			Root->SortIfAsked_RenderThread();
		}
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe && InFallbackMaterial == nullptr;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
		if (bWireframe)
		{
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
				FLinearColor(0, 0.5f, 1.f)
			);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

		const TArray<FDreamUIRenderSectionProxy*>& SectionArray = Root->GetSections();
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto RenderSection = SectionArray[i];
			if (RenderSection == nullptr)continue;
			if (!RenderSection->bCanRender)continue;

			switch (RenderSection->Type)
			{
			case EDreamUIRenderSectionProxyType::Mesh:
			{
				auto Section = static_cast<FDreamUISectionProxy_Mesh*>(RenderSection);
				FMaterialRenderProxy* SectionMaterialProxy = Section->GetMaterialRenderProxy();
				if (!bWireframe && SectionMaterialProxy == nullptr && InFallbackMaterial == nullptr)
				{
					break;//built-in sections are drawn by the DreamUI renderer only
				}
				FMaterialRenderProxy* MaterialProxy = bWireframe
					? static_cast<FMaterialRenderProxy*>(WireframeMaterialInstance)
					: (SectionMaterialProxy != nullptr ? SectionMaterialProxy : InFallbackMaterial);

				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						// Draw the mesh.
						FMeshBatch& Mesh = Collector.AllocateMesh();
						FMeshBatchElement& BatchElement = Mesh.Elements[0];
						BatchElement.IndexBuffer = &Section->IndexBuffer;
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = &Section->VertexFactory;
						Mesh.MaterialRenderProxy = MaterialProxy;

						bool bHasPrecomputedVolumetricLightmap;
						FMatrix PreviousLocalToWorld;
						int32 SingleCaptureIndex;
						bool bOutputVelocity;
						GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

						FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
						DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity);
						BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

						BatchElement.FirstIndex = 0;
						BatchElement.NumPrimitives = Section->NumPrimitives;
						BatchElement.MinVertexIndex = 0;
						BatchElement.MaxVertexIndex = Section->ValidVerticesCount - 1;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = false;
						Collector.AddMesh(ViewIndex, Mesh);
					}
				}
			}
			break;
			case EDreamUIRenderSectionProxyType::PostProcess:
				break;
			case EDreamUIRenderSectionProxyType::ChildCanvas:
			{
				auto Section = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(RenderSection);
				auto ChildRoot = Section->ChildCanvasRoot;
				if (ChildRoot != nullptr && ChildRoot->GetOwner() != nullptr)
				{
					//A nested canvas has a scene proxy of its own but is gathered through this one, so its
					//geometry ends up under this primitive's hit proxy -- which is the actor, the depth the
					//viewport selects at.
					ChildRoot->GetOwner()->GetMeshElements_UERenderer(Views, ViewFamily, VisibilityMap, Collector, InFallbackMaterial);
				}
			}
			break;
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		if (bIsSupportUERenderer)
		{
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			Result.bStaticRelevance = false;
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		}
		else
		{
			Result.bDrawRelevance = false;
			Result.bShadowRelevance = false;
			Result.bDynamicRelevance = false;
			Result.bStaticRelevance = false;
			Result.bRenderInMainPass = false;
			Result.bUsesLightingChannels = false;
			Result.bRenderCustomDepth = false;
#if WITH_EDITOR
			/**
			 * See GetDynamicMeshElements: in a hit proxy view family this primitive exists so the level
			 * viewport has something to pick. bRenderInMainPass has to be claimed as well, even though
			 * nothing of this is displayed, because the renderer reaches every pass mask -- hit proxies
			 * included -- only through the main pass bit.
			 */
			if (bDrawForEditorHitProxies && View->Family->EngineShowFlags.HitProxies)
			{
				Result.bDrawRelevance = IsShown(View);
				Result.bDynamicRelevance = true;
				Result.bRenderInMainPass = ShouldRenderInMainPass();
			}
#endif
		}
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return bIsSupportUERenderer && !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override
	{
		return(sizeof(*this) + GetAllocatedSize());
	}
private:
	/** Held as long as the proxy lives: the render thread may still gather this proxy's elements after the mesh let the root go. */
	FDreamUIRenderRootRef Root;
	FMaterialRelevance MaterialRelevance;
	bool bIsSupportUERenderer = true;
#if WITH_EDITOR
	/** Set for a canvas that only DreamGUI's renderer draws, in the level editor's world. */
	bool bDrawForEditorHitProxies = false;
	/**
	 * Stands in for the sections DreamGUI's own renderer shades, so they can be picked: the hit proxy
	 * pass only needs a material that compiles a hit proxy shader, and the default surface material is
	 * the one exempt from the proxy's used-material verification.
	 */
	FMaterialRenderProxy* EditorHitProxyFallbackMaterial = nullptr;
#endif
};

FDreamUIRenderSectionProxy* FDreamUIRenderRoot::CreateSectionData(FDreamUIRenderSection* InSrcSection)
{
	switch (InSrcSection->Type)
	{
	case EDreamUIRenderSectionType::Mesh:
	case EDreamUIRenderSectionType::DirectMesh:
		{
			auto SrcSection = static_cast<FDreamUIRenderSection_Mesh*>(InSrcSection);
			if (SrcSection->Vertices.Num() == 0 || SrcSection->TriangleIndices.Num() == 0)
			{
				// An empty mesh section is a state the walkers of SectionArray already expect --
				// see PoolAllSectionData_RenderThread -- and `return nullptr` is this branch
				// saying so. check(0) contradicted it: the same state crashed a Development or
				// Test package and returned quietly in Shipping, where DO_CHECK is 0.
				SrcSection->RenderProxy = nullptr;
				return nullptr;
			}
			auto NewSectionProxy = new FDreamUISectionProxy_Mesh(FeatureLevel);
			// vertex and index buffer
			auto& Indices = NewSectionProxy->IndexBuffer.Indices;
			Indices.SetNumUninitialized(SrcSection->TriangleIndices.Num());
			FMemory::Memcpy(Indices.GetData(), SrcSection->TriangleIndices.GetData(), SrcSection->TriangleIndices.Num() * sizeof(FDreamUIMeshIndex));

			auto& SrcVertices = SrcSection->Vertices;

			NewSectionProxy->ValidVerticesCount = SrcSection->ValidVerticesNum;
			NewSectionProxy->NumPrimitives = SrcSection->ValidTriangleIndicesNum / 3;
			if (bIsSupportDreamUIRenderer)
			{
				// Kept after the buffer is made, as what it holds: see PatchSection_RenderThread.
				NewSectionProxy->DreamUIVertexBuffers.bAutoClearVerticesAfterInitRHI = false;
				auto& Vertices = NewSectionProxy->DreamUIVertexBuffers.Vertices;
				Vertices.SetNumUninitialized(SrcVertices.Num());
				FMemory::Memcpy(Vertices.GetData(), SrcVertices.GetData(), SrcVertices.Num() * sizeof(FDreamUIMeshVertex));

				// Enqueue initialization of render resource
				BeginInitResource(&NewSectionProxy->IndexBuffer);
				BeginInitResource(&NewSectionProxy->DreamUIVertexBuffers);
			}
			if (bNeedsUERendererSectionData)
			{
				NewSectionProxy->InitFromDreamUIVertexData(SrcVertices);

				// Enqueue initialization of render resource
				BeginInitResource(&NewSectionProxy->VertexBuffers.PositionVertexBuffer);
				BeginInitResource(&NewSectionProxy->VertexBuffers.StaticMeshVertexBuffer);
				BeginInitResource(&NewSectionProxy->VertexBuffers.ColorVertexBuffer);
				BeginInitResource(&NewSectionProxy->IndexBuffer);
				BeginInitResource(&NewSectionProxy->VertexFactory);
			}

			// Grab material
			NewSectionProxy->Material = SrcSection->Material;
			NewSectionProxy->MaterialProxy = SrcSection->MaterialProxy;
			NewSectionProxy->BuiltIn = SrcSection->BuiltIn;
			if (NewSectionProxy->BuiltIn.bEnabled)
			{
				// The references a built-in draw binds are taken on the render thread, by a command enqueued
				// while the textures are alive. It runs before the proxy is first drawn: anything that draws it
				// is enqueued after this.
				ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_ResolveBuiltIn)(
					[NewSectionProxy, Textures = NewSectionProxy->BuiltIn.GetTexturesForRenderCommand()](FRHICommandListImmediate& RHICmdList)
					{
						NewSectionProxy->BuiltIn.ResolveTextures_RenderThread(Textures);
					});
			}
			if (NewSectionProxy->Material == nullptr)
			{
				NewSectionProxy->Material = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			// Copy info
			NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
			SrcSection->RenderProxy = NewSectionProxy;

			return NewSectionProxy;
		}
	case EDreamUIRenderSectionType::PostProcess:
		{
			auto SrcSection = static_cast<FDreamUIRenderSection_PostProcess*>(InSrcSection);
			auto NewSectionProxy = new FDreamUIRenderSectionProxy_PostProcess();
			// The visual is held weakly and can already be gone -- it is destroyed on its own
			// schedule, while the section only goes away on the canvas's next pool pass. A stale weak
			// pointer is therefore normal, and dereferencing it is a crash; the section simply gets no
			// proxy and cannot render. (The proxy itself is safe once obtained: it is shared.)
			if (SrcSection->PostProcessVisualObject.IsValid())
			{
				NewSectionProxy->PostProcessRenderProxy = SrcSection->PostProcessVisualObject->GetRenderProxy();
			}

			// Copy info
			NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
			SrcSection->RenderProxy = NewSectionProxy;

			return NewSectionProxy;
		}
	case EDreamUIRenderSectionType::ChildCanvas:
		{
			auto SrcSection = static_cast<FDreamUIRenderSection_ChildCanvas*>(InSrcSection);
			auto NewSectionProxy = new FDreamUIRenderSectionProxy_ChildCanvas();
			auto& ChildCanvasMeshItem = SrcSection->ChildCanvasMeshComponent;
			//the same reasoning as the post-process branch above: the mesh is held weakly and can
			//already be gone. The section still gets a proxy -- it just has no child to draw until
			//SetChildCanvasSectionData_RenderThread fills one in.
			if (ChildCanvasMeshItem.IsValid())
			{
				NewSectionProxy->PrimitiveComponentID = ChildCanvasMeshItem->GetPrimitiveSceneId();
				// Hooked into the child root's release on the render thread, as the section joins this root: the child root's
				// delegate is the render thread's to touch.
				NewSectionProxy->ChildCanvasRoot = ChildCanvasMeshItem->GetRenderRoot();
			}

			// Copy info
			NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
			SrcSection->RenderProxy = NewSectionProxy;

			return NewSectionProxy;
		}
	}
	// A section type this switch does not know about. The `return nullptr` is the recoverable
	// answer -- one section does not get a proxy -- and check(0) disagreed with it in exactly
	// the configurations that ship to players: a crash in Development and Test, a quiet return
	// in Shipping. Logged instead, so it is findable rather than silent.
	UE_LOG(DreamGUI, Error, TEXT("[%s].%d Unhandled render section type %d; that section will not be drawn."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, static_cast<int32>(InSrcSection->Type));
	return nullptr;
}



void FDreamUIRenderSection_Mesh::ClearBeforePool()
{
	Material = nullptr;
	MaterialProxy.Reset();
	BuiltIn = FDreamUIBuiltInDrawParams();
}

void FDreamUIRenderSection_PostProcess::ClearBeforePool()
{
	PostProcessVisualObject = nullptr;
}

void FDreamUIRenderSection_ChildCanvas::ClearBeforePool()
{
	ChildCanvasMeshComponent = nullptr;
}


UDreamUIMeshComponent::UDreamUIMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	this->bCanEverAffectNavigation = false;
}

void UDreamUIMeshComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamUIMeshComponent::NeutralizeIfOrphan()
{
	if (IsTemplate() || HasAnyFlags(RF_Transient))
	{
		return;
	}
	// Not destroyed here: PostLoad, PostEditImport and OnRegister are no place to destroy a component.
	// Without materials and sections it draws nothing, and transient it is gone at the next save.
	OverrideMaterials.Reset();
	SetFlags(DreamUI::RuntimeObjectFlags);
	UE_LOG(DreamGUI, Warning, TEXT("%s: neutralized an orphaned canvas mesh (a copy of another panel's mesh, pasted or saved by an older build); it draws nothing and will not be saved, copied or duplicated again."), *GetPathName());
}

void UDreamUIMeshComponent::PostLoad()
{
	Super::PostLoad();
	NeutralizeIfOrphan();
}

#if WITH_EDITOR
void UDreamUIMeshComponent::PostEditImport()
{
	Super::PostEditImport();
	NeutralizeIfOrphan();
}
#endif

void UDreamUIMeshComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (bDuplicateForPIE)
	{
		DreamUI::ReportCopiedIntoPlaySession(*this);
	}
}

void UDreamUIMeshComponent::OnRegister()
{
	// Before the base registers it: a pasted component registers straight after the paste, and nothing
	// should reach the scene still holding another panel's materials.
	NeutralizeIfOrphan();
	Super::OnRegister();
}

void UDreamUIMeshComponent::OnUnregister()
{
	Super::OnUnregister();
	// UnregisterComponent destroyed the render state before this, and the proxy holds the root until the render thread
	// deletes it: what goes now is the root's place in the renderer and in its parent's section, and its sections.
	ReleaseRenderRoot();
}

void UDreamUIMeshComponent::BeginDestroy()
{
	ReleaseRenderRoot();
	Super::BeginDestroy();
}

void UDreamUIMeshComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	PushRenderRootTransform();
}

void UDreamUIMeshComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();
	PushRenderRootTransform();
}

namespace DreamUIMeshComponentLocal
{
	/** What UE's scene has for the mesh's proxy: FScene::UpdatePrimitiveTransform takes the same three. */
	FDreamUIRenderRoot::FTransformData RenderRootTransformOf(const UDreamUIMeshComponent& InMesh)
	{
		FDreamUIRenderRoot::FTransformData RootTransform;
		RootTransform.LocalToWorld = InMesh.GetRenderMatrix();
		RootTransform.Bounds = InMesh.Bounds;
		RootTransform.LocalBounds = InMesh.GetLocalBounds();
		return RootTransform;
	}
}

void UDreamUIMeshComponent::PushRenderRootTransform()
{
	if (!RenderRoot.IsValid())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_SetTransform)(
		[Root = RenderRoot.Get(), RootTransform = DreamUIMeshComponentLocal::RenderRootTransformOf(*this)](FRHICommandListImmediate& RHICmdList)
		{
			Root->SetTransform_RenderThread(RootTransform);
		});
}

bool UDreamUIMeshComponent::NeedsUERendererSectionData() const
{
	/**
	 * Without them a section can only be drawn by DreamGUI's own renderer, which reads its own buffer and
	 * never asks the vertex factory for a stream, so the pair is left uninitialized to save the memory and
	 * the per-update copy. The editor's hit proxy geometry for a canvas only DreamGUI's renderer draws
	 * (FDreamUIRenderSceneProxy::GetDynamicMeshElements) does go through the primitive pipeline, and so
	 * needs them in the level editor's world.
	 */
#if WITH_EDITOR
	return bIsSupportUERenderer || DreamUI::GetWorldType(this) == EWorldType::Editor;
#else
	return bIsSupportUERenderer;
#endif
}

FDreamUIRenderRoot* UDreamUIMeshComponent::EnsureRenderRoot()
{
	if (RenderRoot.IsValid())
	{
		return RenderRoot.Get();
	}
	UWorld* World = GetWorld();
	if (!IsRegistered() || World == nullptr || World->Scene == nullptr || !RenderCanvas.IsValid())
	{
		return nullptr;
	}
	FDreamUIRenderRoot::FSettings Settings;
	Settings.FeatureLevel = World->Scene->GetFeatureLevel();
	Settings.Renderer = DreamUIRenderer;
	Settings.bRenderToWorld = bIsDreamUIRenderToWorld;
	Settings.bIsRenderCanvas = !ParentCanvasMeshComp.IsValid();
	Settings.bNeedsUERendererSectionData = NeedsUERendererSectionData();
	Settings.RenderPriority = TranslucencySortPriority;
	Settings.CanvasKey = FObjectKey(RenderCanvas.Get());
	Settings.BlendDepth = RenderCanvas->GetActualBlendDepth();
	Settings.DepthFade = RenderCanvas->GetActualDepthFade();
	Settings.PrimitiveComponentId = GetPrimitiveSceneId();
	Settings.Transform = DreamUIMeshComponentLocal::RenderRootTransformOf(*this);
#if !UE_BUILD_SHIPPING
	static int32 DebugNameSuffix = 0;
	Settings.DebugName = FString::Printf(TEXT("%s_RenderRoot_%d"), *RenderCanvas->GetWidget()->GetDisplayName(), DebugNameSuffix++);
#endif
	RenderRoot = FDreamUIRenderRoot::Make(Settings);
	// What was pooled for a root before this one has no proxy in it.
	RenderSectionPool.Empty();
	for (auto& RenderSectionPoolItem : RenderSectionMesh_CascadePool)
	{
		RenderSectionPoolItem.RenderSections.Empty();
	}
	RenderRoot->CreateSections(RenderSectionArray);
	RenderRoot->Register();
	OnRenderRootCreated.Broadcast(this, RenderRoot.Get());
	return RenderRoot.Get();
}

void UDreamUIMeshComponent::ReleaseRenderRoot()
{
	if (!RenderRoot.IsValid())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_Release)(
		[Root = RenderRoot.Get()](FRHICommandListImmediate& RHICmdList)
		{
			Root->Release_RenderThread();
		});
	// When no proxy holds the root, this was the last reference, and the deletion follows the release.
	RenderRoot.Reset();
	// Every section proxy was the root's, and so were the pooled sections' and the ones the waiting updates name.
	for (const TSharedPtr<FDreamUIRenderSection>& Section : RenderSectionArray)
	{
		Section->RenderProxy = nullptr;
	}
	RenderSectionPool.Empty();
	for (auto& RenderSectionPoolItem : RenderSectionMesh_CascadePool)
	{
		RenderSectionPoolItem.RenderSections.Empty();
	}
	PendingUpdateMeshSectionDataArray.Reset();
	PendingUpdateRenderSectionPriorityArray.Reset();
	PendingUpdateMeshSectionMaterialDataArray.Reset();
	PendingUpdateMeshSectionBuiltInDataArray.Reset();
}

TSharedPtr<FDreamUIRenderSection> UDreamUIMeshComponent::SetupRenderSection(EDreamUIRenderSectionType InType, FDreamUIDrawCall* InDrawCallData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_SetupRenderSection);
	// A section set up goes straight into the root, which is made with the first one.
	EnsureRenderRoot();
	auto GetMeshRenderSectionFromPool = [&](int32 NumVertices)
	{
		auto& RenderSections = GetRenderSectionMeshPool(NumVertices);
		if (RenderSections.Num() == 0)
		{
			return TSharedPtr<FDreamUIRenderSection_Mesh>();
		}
		auto HeadNode = RenderSections.GetHead();
		auto RenderSection = HeadNode->GetValue();
		RenderSections.RemoveNode(HeadNode);
		return RenderSection;
	};
	auto GetRenderSectionFromPool = [&]()
	{
		for (auto Node = RenderSectionPool.GetHead(); Node != nullptr; Node = Node->GetNextNode() )
		{
			auto RenderSection = Node->GetValue();
			if (RenderSection->Type == InType)
			{
				RenderSectionPool.RemoveNode(Node);
				return RenderSection;
			}
		}
		return TSharedPtr<FDreamUIRenderSection>();
	};
	auto GetDirectMeshRenderSectionFromPool = [&](const UDreamVisualDirectMesh* DirectMesh)
	{
		for (auto Node = RenderSectionPool.GetHead(); Node != nullptr; Node = Node->GetNextNode() )
		{
			auto RenderSection = Node->GetValue();
			if (RenderSection->Type == EDreamUIRenderSectionType::DirectMesh)
			{
				auto DirectMeshRenderSection = static_cast<FDreamUIRenderSection_DirectMesh*>(RenderSection.Get());
				if (DirectMeshRenderSection->DirectMeshVisualObject == DirectMesh//keep reference of DirectMeshVisualObject
					|| DirectMeshRenderSection->DirectMeshVisualObject == nullptr//if old one is deleted then we can use it
					)
				{
					RenderSectionPool.RemoveNode(Node);
					return RenderSection;
				}
			}
		}
		return TSharedPtr<FDreamUIRenderSection>();
	};

	TSharedPtr<FDreamUIRenderSection> RenderSection;
	bool bMeshSectionHoldsItsGeometry = false;
	switch (InType)
	{
	case EDreamUIRenderSectionType::Mesh:
		// Claimed by ClaimPooledMeshSections: its vertices are this draw call's already, here and on the GPU.
		bMeshSectionHoldsItsGeometry = InDrawCallData->ClaimedMeshSection.IsValid();
		if (bMeshSectionHoldsItsGeometry)
		{
			RenderSection = MoveTemp(InDrawCallData->ClaimedMeshSection);
		}
		else
		{
			// Not taken back after all: its vertices are combined now if the batching left them for that.
			InDrawCallData->CombineIfPending();
			RenderSection = GetMeshRenderSectionFromPool(InDrawCallData->CombinedBatchMeshGeometryVertices.Num());
		}
		break;
	case EDreamUIRenderSectionType::DirectMesh:
		RenderSection = GetDirectMeshRenderSectionFromPool(InDrawCallData->DirectMeshVisualObject.Get());
		break;
	case EDreamUIRenderSectionType::PostProcess:
	case EDreamUIRenderSectionType::ChildCanvas:
		RenderSection = GetRenderSectionFromPool();
		break;
	}
	if (!RenderSection)
	{
		switch (InType)
		{
		case EDreamUIRenderSectionType::Mesh:
			RenderSection = MakeShared<FDreamUIRenderSection_Mesh>();
			break;
		case EDreamUIRenderSectionType::PostProcess:
			RenderSection = MakeShared<FDreamUIRenderSection_PostProcess>();
			break;
		case EDreamUIRenderSectionType::ChildCanvas:
			RenderSection = MakeShared<FDreamUIRenderSection_ChildCanvas>();
			break;
		case EDreamUIRenderSectionType::DirectMesh:
			RenderSection = MakeShared<FDreamUIRenderSection_DirectMesh>();
			break;
		}
	}
	
	switch (InType)
	{
	case EDreamUIRenderSectionType::Mesh:
		{
			auto MeshSectionPtr = static_cast<FDreamUIRenderSection_Mesh*>(RenderSection.Get());
			if (bMeshSectionHoldsItsGeometry)
			{
				// Taken back. Pooling switched it off on the render thread and dropped its material: it is switched on
				// again here, and UpdateDrawCallMaterial sends the material after, as for any section. Taken back for
				// geometries laid out as its own are, it has the vertices of those that differ written in place.
				MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds.TransformBy(GetComponentTransform());
				FDreamUIRenderRoot* Root = RenderRoot.Get();
				ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_EnableMeshSection)(
					[Root, SectionProxy = MeshSectionPtr->RenderProxy, Material = MeshSectionPtr->Material](FRHICommandListImmediate& RHICmdList) {
						Root->EnableMeshSection_RenderThread(SectionProxy, Material);
					});
				if (InDrawCallData->bPatchClaimedMeshSection)
				{
					InDrawCallData->bPatchClaimedMeshSection = false;
					// Claimed for this very layout (ClaimPooledMeshSections), so it takes the vertices.
					ensure(PatchMeshSection(MeshSectionPtr, InDrawCallData->BatchMeshGeometryArray));
				}
				else
				{
					DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SectionReuses, 1);
				}
				break;
			}
			// What the vertices below are built from, for the next rebuild to recognise.
			MeshSectionPtr->SourceGeometries = InDrawCallData->BatchMeshGeometryArray;
			MeshSectionPtr->bSourceNormalAndTangent = RenderCanvas->GetActualRequireNormalAndTangent();
			bool bNeedExpandMeshSection = false;
			if (MeshSectionPtr->Vertices.Num() < InDrawCallData->CombinedBatchMeshGeometryVertices.Num())
			{
				MeshSectionPtr->Vertices.SetNumUninitialized(InDrawCallData->CombinedBatchMeshGeometryVertices.Num());
				bNeedExpandMeshSection = true;
			}
			MeshSectionPtr->ValidVerticesNum = InDrawCallData->CombinedBatchMeshGeometryVertices.Num();
			FMemory::Memcpy(MeshSectionPtr->Vertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.Num() * sizeof(FDreamUIMeshVertex));
			if (MeshSectionPtr->TriangleIndices.Num() < InDrawCallData->CombinedBatchMeshGeometryTriangles.Num())
			{
				MeshSectionPtr->TriangleIndices.SetNumUninitialized(InDrawCallData->CombinedBatchMeshGeometryTriangles.Num());
				bNeedExpandMeshSection = true;
			}
			MeshSectionPtr->ValidTriangleIndicesNum = InDrawCallData->CombinedBatchMeshGeometryTriangles.Num();
			FMemory::Memcpy(MeshSectionPtr->TriangleIndices.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.Num() * sizeof(FDreamUIMeshIndex));
			MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds.TransformBy(GetComponentTransform());
			if (MeshSectionPtr->RenderProxy)//if we have valid render-proxy then recreate data or update data
			{
				if (bNeedExpandMeshSection)
				{
#if DEBUG_PRINT_MESH_MEMORY
					ExpandMeshSectionCount++;
#endif
					ExpandMeshSectionRenderData(MeshSectionPtr);
				}
				else
				{
					UpdateMeshSectionRenderData(MeshSectionPtr, RenderCanvas->GetActualRequireNormalAndTangent());
				}
			}
			else//no valid render-proxy, because it is newly created
			{
				if (RenderRoot.IsValid())
				{
					FDreamUIRenderRoot* Root = RenderRoot.Get();
					Root->AddSectionData(MeshSectionPtr);
				}
			}
		}
		break;
	case EDreamUIRenderSectionType::DirectMesh:
		{
			auto DirectMeshSectionPtr = StaticCastSharedPtr<FDreamUIRenderSection_DirectMesh>(RenderSection);
			auto DirectMeshVisualObject = InDrawCallData->DirectMeshVisualObject;
			DirectMeshSectionPtr->DirectMeshVisualObject = DirectMeshVisualObject;
			auto BoundingBox = FBox(EForceInit::ForceInit);
			FVector Min, Max;
			DirectMeshVisualObject->GetGeometryBounds3DInLocalSpace(Min, Max);
			BoundingBox += Min;
			BoundingBox += Max;
			DirectMeshSectionPtr->BoundingBox = BoundingBox;
			DirectMeshVisualObject->OnSupplyMeshSection(this, DirectMeshSectionPtr);
			// Taken back from the pool: pooling disabled the section on the render thread and dropped its
			// material, and the visual resends only what changed. What it did not resend is switched back
			// on here, with the material the section has now.
			if (RenderRoot.IsValid() && DirectMeshSectionPtr->RenderProxy != nullptr)
			{
				FDreamUIRenderRoot* Root = RenderRoot.Get();
				ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_EnableDirectMeshSection)(
					[Root, SectionProxy = DirectMeshSectionPtr->RenderProxy, Material = DirectMeshSectionPtr->Material](FRHICommandListImmediate& RHICmdList) {
						Root->EnableMeshSection_RenderThread(SectionProxy, Material);
					});
			}
		}
		break;
	case EDreamUIRenderSectionType::PostProcess:
		{
			auto PostProcessSectionPtr = static_cast<FDreamUIRenderSection_PostProcess*>(RenderSection.Get());
			auto PostProcessVisualObject = InDrawCallData->PostProcessVisualObject;
			PostProcessSectionPtr->PostProcessVisualObject = PostProcessVisualObject;
			// Weak, and destroyed independently of the section it feeds: nothing below survives a stale one.
			// Its render proxy is shared, so what the section takes below stays valid on its own.
			if (!PostProcessVisualObject.IsValid())
			{
				break;
			}
			auto BoundingBox = FBox(EForceInit::ForceInit);
			FVector Min, Max;
			PostProcessVisualObject->GetGeometryBounds3DInLocalSpace(Min, Max);
			BoundingBox += Min;
			BoundingBox += Max;
			PostProcessSectionPtr->BoundingBox = BoundingBox;
			if (PostProcessSectionPtr->RenderProxy)//if we have valid render-proxy then update data
			{
				if (RenderRoot.IsValid())
				{
					FDreamUIRenderRoot* Root = RenderRoot.Get();
					auto RenderProxy = PostProcessSectionPtr->PostProcessVisualObject->GetRenderProxy();
					Root->UpdatePostProcessSection(PostProcessSectionPtr->RenderProxy, RenderProxy);
				}
			}
			else
			{
				if (RenderRoot.IsValid())
				{
					FDreamUIRenderRoot* Root = RenderRoot.Get();
					Root->AddSectionData(PostProcessSectionPtr);
				}
			}
		}
		break;
	case EDreamUIRenderSectionType::ChildCanvas:
		{
			auto ChildCanvasSectionPtr = static_cast<FDreamUIRenderSection_ChildCanvas*>(RenderSection.Get());
			ChildCanvasSectionPtr->ChildCanvasMeshComponent = InDrawCallData->ChildCanvas->GetUIMesh();
			// The parent of the child's mesh is THIS mesh (the one hosting the section). It was set to the
			// child's own mesh, which parented every child canvas to itself — the pooled-section teardown
			// (ClearParentCanvasMeshComp(this)) and proxy-recreated re-hookup both assume `this`.
			ChildCanvasSectionPtr->ChildCanvasMeshComponent->SetParentCanvasMeshComp(this);
			if (ChildCanvasSectionPtr->RenderProxy)
			{
				if (RenderRoot.IsValid())
				{
					FDreamUIRenderRoot* Root = RenderRoot.Get();
					UDreamUIMeshComponent* ChildMesh = InDrawCallData->ChildCanvas->GetUIMesh();
					Root->UpdateChildCanvasSection(ChildCanvasSectionPtr->RenderProxy, ChildMesh->GetPrimitiveSceneId(), ChildMesh->GetRenderRoot());
				}
			}
			else
			{
				if (RenderRoot.IsValid())
				{
					FDreamUIRenderRoot* Root = RenderRoot.Get();
					Root->AddSectionData(ChildCanvasSectionPtr);
				}
			}
		}
		break;
	}

	RenderSectionArray.Add(RenderSection);
	return RenderSection;
}

void UDreamUIMeshComponent::UpdateMeshSection(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, FDreamUIDrawCall* InDrawCallData)
{
	// Addressed by handle, not by draw-call index: skipped draw-calls have no section, so index-based
	// addressing hit the wrong section — including reinterpreting a ChildCanvas section as a mesh.
	if (!InRenderSection.IsValid() || InRenderSection->Type != EDreamUIRenderSectionType::Mesh)
	{
		return;
	}
	auto MeshSectionPtr = static_cast<FDreamUIRenderSection_Mesh*>(InRenderSection.Get());
	// Laid out as the geometries the section was built from: only the vertices of those that changed are written, here
	// and on the GPU.
	if (PatchMeshSection(MeshSectionPtr, InDrawCallData->BatchMeshGeometryArray))
	{
		MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds.TransformBy(GetComponentTransform());
		return;
	}
	// Every vertex, from the combined buffer, made now if the batching left it to be made.
	InDrawCallData->CombineIfPending();
	// The refreshed copies' vertices go in now, with the indices the section already holds: it stands for those copies
	// while their triangles are the ones it holds, and for nothing a rebuild could claim otherwise.
	if (InDrawCallData->bTrianglesAsBuilt)
	{
		MeshSectionPtr->SourceGeometries = InDrawCallData->BatchMeshGeometryArray;
		MeshSectionPtr->bSourceNormalAndTangent = RenderCanvas->GetActualRequireNormalAndTangent();
	}
	else
	{
		MeshSectionPtr->SourceGeometries.Reset();
	}
	if (MeshSectionPtr->RenderProxy)//if we have valid render-proxy then recreate or update data
	{
		MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds.TransformBy(GetComponentTransform());
		FMemory::Memcpy(MeshSectionPtr->Vertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.Num() * sizeof(FDreamUIMeshVertex));
		FMemory::Memcpy(MeshSectionPtr->TriangleIndices.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.Num() * sizeof(FDreamUIMeshIndex));
		UpdateMeshSectionRenderData(MeshSectionPtr, RenderCanvas->GetActualRequireNormalAndTangent());
	}
	else//no valid render-proxy, because it is newly created
	{
		MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds.TransformBy(GetComponentTransform());
		FMemory::Memcpy(MeshSectionPtr->Vertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.Num() * sizeof(FDreamUIMeshVertex));
		FMemory::Memcpy(MeshSectionPtr->TriangleIndices.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.Num() * sizeof(FDreamUIMeshIndex));
		if (RenderRoot.IsValid())
		{
			FDreamUIRenderRoot* Root = RenderRoot.Get();
			Root->AddSectionData(MeshSectionPtr);
		}
	}
}

bool UDreamUIMeshComponent::PatchMeshSection(FDreamUIRenderSection_Mesh* InMeshSection, const TArray<TSharedPtr<const FDreamUIGeometry>>& InGeometries)
{
	if (!RenderRoot.IsValid() || InMeshSection == nullptr || InMeshSection->RenderProxy == nullptr || !RenderCanvas.IsValid()
		|| InMeshSection->bSourceNormalAndTangent != RenderCanvas->GetActualRequireNormalAndTangent()
		|| !FDreamUIDrawCall::GeometryListsShareLayout(InMeshSection->SourceGeometries, InGeometries))
	{
		return false;
	}
	// Each geometry's vertices follow the one before's, leaving out those with no triangles unless there is only one
	// (FDreamUIDrawCall::ApplyBatchMeshGeometryToCombined). The layout is the section's, so they fill it exactly.
	const bool bSkipTriangleless = InGeometries.Num() != 1;
	int32 Total = 0;
	for (const TSharedPtr<const FDreamUIGeometry>& Geometry : InGeometries)
	{
		if (!(bSkipTriangleless && Geometry->Triangles.Num() <= 0))
		{
			Total += Geometry->Vertices.Num();
		}
	}
	if (Total != InMeshSection->ValidVerticesNum || InMeshSection->Vertices.Num() < Total)
	{
		return false;
	}
	UpdateMeshSectionDataStruct UpdateData;
	int32 First = 0;
	for (int32 Index = 0; Index < InGeometries.Num(); ++Index)
	{
		const FDreamUIGeometry& Geometry = *InGeometries[Index];
		if (bSkipTriangleless && Geometry.Triangles.Num() <= 0)
		{
			continue;
		}
		const int32 Count = Geometry.Vertices.Num();
		if (Count > 0 && InGeometries[Index] != InMeshSection->SourceGeometries[Index])
		{
			FMemory::Memcpy(InMeshSection->Vertices.GetData() + First, Geometry.Vertices.GetData(), Count * sizeof(FDreamUIMeshVertex));
			if (UpdateData.PatchedRuns.Num() > 0 && UpdateData.PatchedRuns.Last().Key + UpdateData.PatchedRuns.Last().Value == First)
			{
				UpdateData.PatchedRuns.Last().Value += Count;
			}
			else
			{
				UpdateData.PatchedRuns.Emplace(First, Count);
			}
			UpdateData.VertexBufferData.Append(Geometry.Vertices.GetData(), Count);
		}
		First += Count;
	}
	InMeshSection->SourceGeometries = InGeometries;
	if (UpdateData.PatchedRuns.Num() == 0)
	{
		return true;
	}
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SectionPatches, 1);
	DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::UploadedBytes, static_cast<int64>(UpdateData.VertexBufferData.Num()) * sizeof(FDreamUIMeshVertex));
	UpdateData.Section = static_cast<FDreamUISectionProxy_Mesh*>(InMeshSection->RenderProxy);
	UpdateData.NumVerts = InMeshSection->ValidVerticesNum;
	UpdateData.NumTriangles = InMeshSection->ValidTriangleIndicesNum / 3;
	UpdateData.RequireNormalAndTangent = InMeshSection->bSourceNormalAndTangent;
	// With the frame's other section updates, in order: a patch must not overtake an update it follows.
	PendingUpdateMeshSectionDataArray.Add(MoveTemp(UpdateData));
	return true;
}

void UDreamUIMeshComponent::SetupDirectMeshRenderSection(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, bool bNeedExpandMeshSection, UMaterialInterface* InMaterial)
{
	InDirectMeshSection->BoundingBox = InDirectMeshSection->BoundingBox.TransformBy(GetComponentTransform());
	
	if (InDirectMeshSection->RenderProxy)//if we have valid render-proxy then recreate data or update data
	{
		if (bNeedExpandMeshSection)
		{
			ExpandMeshSectionRenderData(InDirectMeshSection);
		}
		else
		{
			UpdateMeshSectionRenderData(InDirectMeshSection, RenderCanvas->GetActualRequireNormalAndTangent());
		}
	}
	else//no valid render-proxy, because it is newly created
	{
		if (RenderRoot.IsValid())
		{
			FDreamUIRenderRoot* Root = RenderRoot.Get();
			Root->AddSectionData(InDirectMeshSection);
		}
	}

	SetDirectMeshRenderSectionMaterial(InDirectMeshSection, InMaterial);
}

void UDreamUIMeshComponent::SetDirectMeshRenderSectionMaterial(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, UMaterialInterface* InMaterial)
{
	InDirectMeshSection->Material = InMaterial;
	if (RenderRoot.IsValid())
	{
		if (InDirectMeshSection->RenderProxy)
		{
			UpdateMeshSectionMaterialDataStruct UpdateData;
			UpdateData.SectionProxy = InDirectMeshSection->RenderProxy;
			UpdateData.Material = InMaterial;

			FDreamUIRenderRoot* Root = RenderRoot.Get();
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
				[Root, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					Root->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material);
				});
		}
	}
}

#define LATE_FLUSH_RENDER_CMD 1
DECLARE_CYCLE_STAT(TEXT("DreamUIMesh UpdateMeshSection_GT"), STAT_UpdateMeshSectionGT, STATGROUP_DreamGUI);
void UDreamUIMeshComponent::UpdateMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection, bool InRequireNormalAndTangent)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionGT);
	if (RenderRoot.IsValid())
	{
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::SectionUploads, 1);
		DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::UploadedBytes,
			static_cast<int64>(InMeshSection->ValidVerticesNum) * sizeof(FDreamUIMeshVertex) + static_cast<int64>(InMeshSection->ValidTriangleIndicesNum) * sizeof(FDreamUIMeshIndex));
		UpdateMeshSectionDataStruct UpdateData;
		UpdateData.Section = static_cast<FDreamUISectionProxy_Mesh*>(InMeshSection->RenderProxy);
		//vertex data
		const int32 NumVerts = InMeshSection->ValidVerticesNum;
		UpdateData.VertexBufferData.AddUninitialized(NumVerts);
		FMemory::Memcpy(UpdateData.VertexBufferData.GetData(), InMeshSection->Vertices.GetData(), NumVerts * sizeof(FDreamUIMeshVertex));
		UpdateData.NumVerts = NumVerts;
		const int32 NumIndices = InMeshSection->ValidTriangleIndicesNum;
		UpdateData.IndexBufferData.AddUninitialized(NumIndices);
		UpdateData.NumTriangles = NumIndices / 3;
		FMemory::Memcpy(UpdateData.IndexBufferData.GetData(), InMeshSection->TriangleIndices.GetData(), NumIndices * sizeof(FDreamUIMeshIndex));
		UpdateData.RequireNormalAndTangent = InRequireNormalAndTangent;
		//update data
#if LATE_FLUSH_RENDER_CMD
		PendingUpdateMeshSectionDataArray.Add(MoveTemp(UpdateData));
#else
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
			[Root, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList)
			{
				Root->UpdateSection_RenderThread(
					RHICmdList
					, UpdateData.VertexBufferData.GetData()
					, UpdateData.NumVerts
					, UpdateData.IndexBufferData.GetData()
					, UpdateData.NumTriangles
					, UpdateData.RequireNormalAndTangent
					, UpdateData.Section
				);
			});
#endif
	}
}

void UDreamUIMeshComponent::ExpandMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection)
{
	if (RenderRoot.IsValid())
	{
		/**
		 * RecreateSectionData enqueues the delete of the old section proxy right now, while the
		 * frame's pending updates are not enqueued until FlushRenderCommand at the end of the frame --
		 * so any pending update still addressing the old proxy ran after it had been deleted. Repoint
		 * them at the proxy that replaces it (dropping them would lose this frame's vertex data,
		 * material or priority for that section).
		 */
		FDreamUIRenderSectionProxy* OldSectionProxy = InMeshSection->RenderProxy;
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		Root->RecreateSectionData(InMeshSection);
		if (OldSectionProxy != nullptr && OldSectionProxy != InMeshSection->RenderProxy)
		{
			RetargetPendingRenderCommands(OldSectionProxy, InMeshSection->RenderProxy);
		}
	}
}

void UDreamUIMeshComponent::RetargetPendingRenderCommands(FDreamUIRenderSectionProxy* InOldSectionProxy, FDreamUIRenderSectionProxy* InNewSectionProxy)
{
	if (InNewSectionProxy == nullptr)
	{
		//nothing to point them at: the section has no proxy, so the updates have nowhere to land
		PendingUpdateMeshSectionDataArray.RemoveAll([InOldSectionProxy](const UpdateMeshSectionDataStruct& Item) { return Item.Section == InOldSectionProxy; });
		PendingUpdateRenderSectionPriorityArray.RemoveAll([InOldSectionProxy](const UpdateRenderSectionPriority& Item) { return Item.SectionProxy == InOldSectionProxy; });
		PendingUpdateMeshSectionMaterialDataArray.RemoveAll([InOldSectionProxy](const UpdateMeshSectionMaterialDataStruct& Item) { return Item.SectionProxy == InOldSectionProxy; });
		PendingUpdateMeshSectionBuiltInDataArray.RemoveAll([InOldSectionProxy](const UpdateMeshSectionBuiltInDataStruct& Item) { return Item.SectionProxy == InOldSectionProxy; });
		return;
	}
	for (auto& Item : PendingUpdateMeshSectionDataArray)
	{
		if (Item.Section == InOldSectionProxy)
		{
			Item.Section = static_cast<FDreamUISectionProxy_Mesh*>(InNewSectionProxy);
		}
	}
	for (auto& Item : PendingUpdateRenderSectionPriorityArray)
	{
		if (Item.SectionProxy == InOldSectionProxy)
		{
			Item.SectionProxy = InNewSectionProxy;
		}
	}
	for (auto& Item : PendingUpdateMeshSectionMaterialDataArray)
	{
		if (Item.SectionProxy == InOldSectionProxy)
		{
			Item.SectionProxy = InNewSectionProxy;
		}
	}
	for (auto& Item : PendingUpdateMeshSectionBuiltInDataArray)
	{
		if (Item.SectionProxy == InOldSectionProxy)
		{
			Item.SectionProxy = InNewSectionProxy;
		}
	}
}

void UDreamUIMeshComponent::PoolAllRenderSection()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_PoolAllRenderSection);
	if (RenderRoot.IsValid())
	{
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_PoolAllSectionData)(
			[Root](FRHICommandListImmediate& RHICmdList) {
				Root->PoolAllSectionData_RenderThread();
			});
	}
	for (auto& RenderSection : RenderSectionArray)
	{
		if (RenderSection->Type == EDreamUIRenderSectionType::ChildCanvas)
		{
			auto ChildCanvasSection = static_cast<FDreamUIRenderSection_ChildCanvas*>(RenderSection.Get());
			auto ChildCanvasMeshCom = ChildCanvasSection->ChildCanvasMeshComponent;
			if (ChildCanvasMeshCom.IsValid())
			{
				ChildCanvasMeshCom->ClearParentCanvasMeshComp(this);
				ChildCanvasMeshCom->OnRenderRootCreated.RemoveAll(this);
			}
		}

		RenderSection->ClearBeforePool();
		switch (RenderSection->Type)
		{
		case EDreamUIRenderSectionType::Mesh:
			{
				auto MeshSection = StaticCastSharedPtr<FDreamUIRenderSection_Mesh>(RenderSection);
				int32 NumVertices = MeshSection->Vertices.Num();
				auto& RenderSections = GetRenderSectionMeshPool(NumVertices);
				RenderSections.AddTail(MeshSection);
			}
			break;
		default:
			RenderSectionPool.AddTail(RenderSection);
			break;
		}
	}
	RenderSectionArray.Reset();
}

void UDreamUIMeshComponent::SetRenderSectionRenderPriority(const TSharedPtr<FDreamUIRenderSection>& InRenderSection, int32 InSortPriority)
{
	auto& RenderSection = InRenderSection;
	if (!RenderSection.IsValid())
	{
		return;
	}
	RenderSection->RenderPriority = InSortPriority;
	if (RenderRoot.IsValid())
	{
		if (RenderSection->RenderProxy)
		{
			UpdateRenderSectionPriority UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.RenderPriority = InSortPriority;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateRenderSectionPriorityArray.Add(MoveTemp(UpdateData));
#else
			FDreamUIRenderRoot* Root = RenderRoot.Get();
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionRenderPriority)(
				[Root, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					Root->SetRenderSectionRenderPriority_RenderThread(UpdateData.SectionProxy, UpdateData.RenderPriority);
				});
#endif
		}
	}
}

void UDreamUIMeshComponent::SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial, const TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe>& InMaterialProxy)
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	check(RenderSection->Type == EDreamUIRenderSectionType::Mesh);
	(static_cast<FDreamUIRenderSection_Mesh*>(RenderSection.Get()))->Material = InMaterial;
	(static_cast<FDreamUIRenderSection_Mesh*>(RenderSection.Get()))->MaterialProxy = InMaterialProxy;
	if (RenderRoot.IsValid())
	{
		if (RenderSection->RenderProxy)
		{
			UpdateMeshSectionMaterialDataStruct UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.Material = InMaterial;
			UpdateData.MaterialProxy = InMaterialProxy;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateMeshSectionMaterialDataArray.Add(MoveTemp(UpdateData));
#else
			FDreamUIRenderRoot* Root = RenderRoot.Get();
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
				[Root, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					Root->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material);
				});
#endif
		}
	}
}

void UDreamUIMeshComponent::SetMeshSectionBuiltIn(int32 InSectionIndex, const FDreamUIBuiltInDrawParams& InParams)
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	check(RenderSection->Type == EDreamUIRenderSectionType::Mesh);
	(static_cast<FDreamUIRenderSection_Mesh*>(RenderSection.Get()))->BuiltIn = InParams;
	if (RenderRoot.IsValid())
	{
		if (RenderSection->RenderProxy)
		{
			UpdateMeshSectionBuiltInDataStruct UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.Params = InParams;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateMeshSectionBuiltInDataArray.Add(MoveTemp(UpdateData));
#else
			UpdateData.Textures = UpdateData.Params.GetTexturesForRenderCommand();
			FDreamUIRenderRoot* Root = RenderRoot.Get();
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionBuiltIn)(
				[Root, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					FDreamUIBuiltInDrawParams Params = UpdateData.Params;
					Params.ResolveTextures_RenderThread(UpdateData.Textures);
					Root->SetMeshSectionBuiltIn_RenderThread(UpdateData.SectionProxy, Params);
				});
#endif
		}
	}
}

bool UDreamUIMeshComponent::IsMeshSectionBuiltIn(int32 InSectionIndex) const
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	check(RenderSection->Type == EDreamUIRenderSectionType::Mesh);
	return (static_cast<const FDreamUIRenderSection_Mesh*>(RenderSection.Get()))->BuiltIn.bEnabled;
}

void UDreamUIMeshComponent::VerifyMaterials()
{
#if 1
	if (OverrideMaterials.Num())
	{
		for (int32 MatIndex = 0; MatIndex < OverrideMaterials.Num(); MatIndex++)
		{
			if (UMaterialInterface* MatInterface = OverrideMaterials[MatIndex].Get())
			{
				MatInterface->OnRemovedAsOverride(this);
			}
		}
		// Precache PSOs again
		// PrecachePSOs();
		OverrideMaterials.Reset();
	}
	auto SetMaterialForUI = [=, this](int ElementIndex, UMaterialInterface* Material)
	{
		// Grow the array if the new index is too large
		if (OverrideMaterials.Num() <= ElementIndex)
		{
			OverrideMaterials.AddZeroed(ElementIndex + 1 - OverrideMaterials.Num());
		}

		// Set the material and invalidate things
		OverrideMaterials[ElementIndex] = Material;

		if (Material)
		{
			Material->OnAssignedAsOverride(this);
		}

		// Precache PSOs again
		// PrecachePSOs();

		if (Material)
		{
			Material->AddToCluster(this, true);
		}
	};
	int MatIndex = 0;
	for (auto& RenderSectionItem : RenderSectionArray)
	{
		switch (RenderSectionItem->Type)
		{
		case EDreamUIRenderSectionType::Mesh:
		// A direct mesh's material too: this array is what keeps a section's material from being
		// collected while the section draws with it, and GetNumMaterials already counts it.
		case EDreamUIRenderSectionType::DirectMesh:
			{
				auto MeshSection = static_cast<FDreamUIRenderSection_Mesh*>(RenderSectionItem.Get());
				SetMaterialForUI(MatIndex++, MeshSection->Material);
			}
			break;
		case EDreamUIRenderSectionType::ChildCanvas:
			{
				auto ChildCanvasSection = static_cast<FDreamUIRenderSection_ChildCanvas*>(RenderSectionItem.Get());
				//the child canvas's mesh is held weakly and dies on its own schedule, while this section
				//only goes away on the next pool pass -- a stale weak pointer here is normal
				if (!ChildCanvasSection->ChildCanvasMeshComponent.IsValid())continue;
				for (auto ChildMat : ChildCanvasSection->ChildCanvasMeshComponent->OverrideMaterials)
				{
					SetMaterialForUI(MatIndex++, ChildMat);
				}
			}
			break;
		}
	}
#else
	this->EmptyOverrideMaterials();

	int MatIndex = 0;
	for (auto& RenderSectionItem : RenderSectionArray)
	{
		switch (RenderSectionItem->Type)
		{
		case EDreamUIRenderSectionType::Mesh:
		{
			auto MeshSection = (FDreamUIRenderSection_Mesh*)RenderSectionItem.Get();
			this->SetMaterial(MatIndex++, MeshSection->material);
		}
		break;
		case EDreamUIRenderSectionType::ChildCanvas:
		{
			auto ChildCanvasSection = (FDreamUIRenderSection_ChildCanvas*)RenderSectionItem.Get();
			for (auto ChildMat : ChildCanvasSection->ChildCanvasMeshComponent->OverrideMaterials)
			{
				this->SetMaterial(MatIndex++, ChildMat);
			}
		}
		break;
		}
	}
#endif
}

void UDreamUIMeshComponent::SetParentCanvasMeshComp(UDreamUIMeshComponent* InParentCanvasMeshComp)
{
	if (ParentCanvasMeshComp != InParentCanvasMeshComp)
	{
		auto ChildCanvasMeshCom = this;
		if (ParentCanvasMeshComp != nullptr)
		{
			ChildCanvasMeshCom->OnRenderRootCreated.RemoveAll(ParentCanvasMeshComp.Get());
		}
		
		ParentCanvasMeshComp = InParentCanvasMeshComp;

		ChildCanvasMeshCom->OnRenderRootCreated.AddWeakLambda(InParentCanvasMeshComp, [InParentCanvasMeshComp](UDreamUIMeshComponent* InChildMeshComp, FDreamUIRenderRoot* InChildRoot) {
			if (FDreamUIRenderRoot* ParentRoot = InParentCanvasMeshComp->GetRenderRoot())
			{
				ENQUEUE_RENDER_COMMAND(FDreamUIRenderRoot_ReassignChildCanvasSectionData)(
					[ParentRoot, CompID = InChildMeshComp->GetPrimitiveSceneId(), InChildRoot](FRHICommandListImmediate& RHICmdList) {
						ParentRoot->SetChildCanvasSectionData_RenderThread(CompID, InChildRoot);
					});
			}
			});
	}
}
void UDreamUIMeshComponent::ClearParentCanvasMeshComp(UDreamUIMeshComponent* InParentCanvasMeshComp)
{
	if (ParentCanvasMeshComp == InParentCanvasMeshComp)//check, incase parent already change
	{
		auto ChildCanvasMeshCom = this;
		if (ParentCanvasMeshComp != nullptr)
		{
			ChildCanvasMeshCom->OnRenderRootCreated.RemoveAll(ParentCanvasMeshComp.Get());
		}
		ParentCanvasMeshComp = nullptr;
	}
}

void UDreamUIMeshComponent::SetUITranslucentSortPriority(int32 NewTranslucentSortPriority)
{
	UPrimitiveComponent::SetTranslucentSortPriority(NewTranslucentSortPriority);
	if (RenderRoot.IsValid())
	{
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMesh_SetUITranslucentSortPriority)(
			[Root, NewTranslucentSortPriority](FRHICommandListImmediate& RHICmdList)
		{
			Root->SetRenderPriority_RenderThread(NewTranslucentSortPriority);
		}
		);
	}
}

void UDreamUIMeshComponent::UpdateChildCanvasSectionBox()
{
	struct LOCAL
	{
		static void UpdateChildCanvasSectionBox_Recursive(const TArray<TSharedPtr<FDreamUIRenderSection>>& InRenderSections)
		{
			for (auto& RenderSectionItem : InRenderSections)
			{
				if (RenderSectionItem->Type == EDreamUIRenderSectionType::ChildCanvas)
				{
					auto ChildCanvasSection = StaticCastSharedPtr<FDreamUIRenderSection_ChildCanvas>(RenderSectionItem);
					if (ChildCanvasSection->ChildCanvasMeshComponent != nullptr)
					{
						UpdateChildCanvasSectionBox_Recursive(ChildCanvasSection->ChildCanvasMeshComponent->RenderSectionArray);
						ChildCanvasSection->BoundingBox = ChildCanvasSection->ChildCanvasMeshComponent->Bounds.GetBox();//how we can be sure that children canvas bounds is ready? because we update child canvas drawcall before parent
					}
				}
			}
		}
	};
	LOCAL::UpdateChildCanvasSectionBox_Recursive(RenderSectionArray);

#if DEBUG_PRINT_MESH_MEMORY
	if (RenderRoot.IsValid())
	{
		FDreamUIRenderRoot* Proxy = RenderRoot.Get();
		auto MemSize = (double)Proxy->GetMeshMemorySize();
		FString MemSizeStr;
		if (MemSize > 1024 * 1024 * 1024)
		{
			MemSizeStr = FString::Printf(TEXT("%fGB"), MemSize / (1024 * 1024 * 1024));
		}
		else if (MemSize > 1024 * 1024)
		{
			MemSizeStr = FString::Printf(TEXT("%fMB"), MemSize / (1024 * 1024));
		}
		else
		{
			MemSizeStr = FString::Printf(TEXT("%fKB"), MemSize / 1024);
		}
		auto MaxVertexBufferSize = (double)Proxy->GetMaxVertexBufferSize();
		FString MaxVertexBufferSizeStr;
		if (MaxVertexBufferSize > 1024 * 1024)
		{
			MaxVertexBufferSizeStr = FString::Printf(TEXT("%fMB"), MaxVertexBufferSize / (1024 * 1024));
		}
		else if (MaxVertexBufferSize > 1024)
		{
			MaxVertexBufferSizeStr = FString::Printf(TEXT("%fKB"), MaxVertexBufferSize / 1024);
		}
		else
		{
			MaxVertexBufferSizeStr = FString::Printf(TEXT("%fB"), MaxVertexBufferSize);
		}
		auto DebugMsg = FString::Printf(TEXT("RenderProxy:%s UsingSectionCount:%d ExpandMeshSectionCount:%d MeshMemorySize:%s MaxVertexBufferSize:%s"), *Proxy->DebugName, RenderSectionArray.Num(), ExpandMeshSectionCount, *MemSizeStr, *MaxVertexBufferSizeStr);
		UE_LOG(DreamGUI, Error, TEXT("%s"), *DebugMsg);
		if (ExpandMeshSectionCount > 0)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Green, DebugMsg);
		}
		ExpandMeshSectionCount = 0;
	}
#endif
}

void UDreamUIMeshComponent::UpdateLocalBounds() 
{
	UpdateBounds();// Update global bounds		
	MarkRenderTransformDirty();// Need to send to render thread
}

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh CreateSceneProxy"), STAT_DreamUIMesh_CreateSceneProxy, STATGROUP_DreamGUI);
FPrimitiveSceneProxy* UDreamUIMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUIMesh_CreateSceneProxy);
	if (RenderSectionArray.Num() <= 0)
	{
		return nullptr;
	}
	// The root outlives the proxies made for it and is there already, but for a mesh registered again after its root went.
	if (EnsureRenderRoot() == nullptr)
	{
		return nullptr;
	}
	return new FDreamUIRenderSceneProxy(this, RenderRoot
		, !ParentCanvasMeshComp.IsValid()//child canvas is render by it's parent
		);
}

void UDreamUIMeshComponent::Init(UDreamCanvas* InCanvas)
{
	RenderCanvas = InCanvas;
	TArray<int32> VertexBufferRangeSlices = {0, 128, 1024, 8192, 32768, 65535, TNumericLimits<int32>::Max()};
	for (int i = 1; i < VertexBufferRangeSlices.Num(); i++)
	{
		FMeshRenderSectionPool Pool;
		RenderSectionMesh_CascadePool.Add(MoveTemp(Pool));
	}
}
TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> UDreamUIMeshComponent::GetMeshSectionGeometryLists() const
{
	TArray<TArray<TSharedPtr<const FDreamUIGeometry>>> Lists;
	if (!RenderRoot.IsValid())
	{
		return Lists;
	}
	auto AddFrom = [&Lists](const FDreamUIRenderSection* InSection)
	{
		if (InSection != nullptr && InSection->Type == EDreamUIRenderSectionType::Mesh && InSection->RenderProxy != nullptr)
		{
			const FDreamUIRenderSection_Mesh* MeshSection = static_cast<const FDreamUIRenderSection_Mesh*>(InSection);
			if (MeshSection->SourceGeometries.Num() > 0)
			{
				Lists.Add(MeshSection->SourceGeometries);
			}
		}
	};
	for (const TSharedPtr<FDreamUIRenderSection>& Section : RenderSectionArray)
	{
		AddFrom(Section.Get());
	}
	for (const FMeshRenderSectionPool& Pool : RenderSectionMesh_CascadePool)
	{
		for (auto Node = Pool.RenderSections.GetHead(); Node != nullptr; Node = Node->GetNextNode())
		{
			AddFrom(Node->GetValue().Get());
		}
	}
	return Lists;
}

void UDreamUIMeshComponent::ClaimPooledMeshSections(TArray<FDreamUIDrawCall>& InOutDrawCalls)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_ClaimPooledMeshSections);
	// Without a render root no section has anything on the GPU to keep.
	if (!RenderRoot.IsValid() || !RenderCanvas.IsValid())
	{
		return;
	}
	const bool bNormalAndTangent = RenderCanvas->GetActualRequireNormalAndTangent();
	// The sections built from exactly a draw call's geometries first, all of them; then, for the draw calls left, the
	// sections laid out as theirs are. A draw call none of whose geometries changed must not find its section taken by
	// one that changed.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bInPlace = Pass == 1;
		for (FDreamUIDrawCall& DrawCall : InOutDrawCalls)
		{
			if (DrawCall.Type != EDreamUIDrawCallType::BatchMesh || DrawCall.BatchMeshGeometryArray.Num() == 0 || DrawCall.ClaimedMeshSection.IsValid())
			{
				continue;
			}
			for (FMeshRenderSectionPool& Pool : RenderSectionMesh_CascadePool)
			{
				for (auto Node = Pool.RenderSections.GetHead(); Node != nullptr; Node = Node->GetNextNode())
				{
					const TSharedPtr<FDreamUIRenderSection_Mesh>& Candidate = Node->GetValue();
					if (Candidate->RenderProxy != nullptr && Candidate->bSourceNormalAndTangent == bNormalAndTangent
						&& (bInPlace ? FDreamUIDrawCall::GeometryListsShareLayout(Candidate->SourceGeometries, DrawCall.BatchMeshGeometryArray)
							: Candidate->SourceGeometries == DrawCall.BatchMeshGeometryArray))
					{
						DrawCall.ClaimedMeshSection = Candidate;
						DrawCall.bPatchClaimedMeshSection = bInPlace;
						Pool.RenderSections.RemoveNode(Node);
						break;
					}
				}
				if (DrawCall.ClaimedMeshSection.IsValid())
				{
					break;
				}
			}
		}
	}
}

TDoubleLinkedList<TSharedPtr<FDreamUIRenderSection_Mesh>>& UDreamUIMeshComponent::GetRenderSectionMeshPool(int32 InNumVertices)
{
	auto GetRange = [](int32 x)
	{
		if (x <= 127) return 0;
		if (x <= 1023) return 1;
		if (x <= 8191) return 2;
		if (x <= 32767) return 3;
		if (x <= 65535) return 4;
		return 5;
	};
	int32 Index = GetRange(InNumVertices);
	return RenderSectionMesh_CascadePool[Index].RenderSections;
}
void UDreamUIMeshComponent::SetSupportDreamUIRenderer(bool InSupportOrNot, TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> InDreamUIRenderer, bool InIsRenderToWorld)
{
	if (InSupportOrNot)
	{
		DreamUIRenderer = InDreamUIRenderer;
		bIsDreamUIRenderToWorld = InIsRenderToWorld;
	}
	else
	{
		DreamUIRenderer.Reset();
	}
}

void UDreamUIMeshComponent::SetSupportUERenderer(bool InSupportOrNot)
{
	bIsSupportUERenderer = InSupportOrNot;
}
void UDreamUIMeshComponent::ClearRenderData()
{
	ReleaseRenderRoot();
	MarkRenderStateDirty();//mark dirty to recreate SceneProxy
	RenderSectionArray.Empty();
	RenderSectionPool.Empty();
	for (auto& RenderSectionPoolItem : RenderSectionMesh_CascadePool)
	{
		RenderSectionPoolItem.RenderSections.Empty();
	}
	OnRenderRootCreated.Clear();
	ParentCanvasMeshComp = nullptr;
	DreamUIRenderer = nullptr;
}

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh FlushRenderCommand"), STAT_DreamUIMesh_FlushRenderCommand, STATGROUP_DreamGUI);
void UDreamUIMeshComponent::FlushRenderCommand()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUIMesh_FlushRenderCommand)
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_MeshFlushRenderCommand);
	if (!RenderRoot.IsValid())
	{
		/**
		 * No root to send them to. ReleaseRenderRoot drops what was waiting when it lets a root go, and every
		 * collector checks for one, so there is nothing here unless something collected without checking:
		 * the section proxies such updates name went with the root, and the next root builds its sections
		 * from the render sections anyway.
		 */
		PendingUpdateMeshSectionDataArray.Reset();
		PendingUpdateRenderSectionPriorityArray.Reset();
		PendingUpdateMeshSectionMaterialDataArray.Reset();
		PendingUpdateMeshSectionBuiltInDataArray.Reset();
		return;
	}
	if (PendingUpdateMeshSectionDataArray.Num() > 0)
	{
		//update data
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
			[Root, PendingUpdateMeshSectionDataArray = MoveTemp(PendingUpdateMeshSectionDataArray)](FRHICommandListImmediate& RHICmdList) mutable
			{
				for (auto& UpdateData : PendingUpdateMeshSectionDataArray)
				{
					if (UpdateData.PatchedRuns.Num() > 0)
					{
						Root->PatchSection_RenderThread(
							RHICmdList
							, UpdateData.VertexBufferData
							, UpdateData.PatchedRuns
							, UpdateData.RequireNormalAndTangent
							, UpdateData.Section
						);
						continue;
					}
					Root->UpdateSection_RenderThread(
						RHICmdList
						, UpdateData.VertexBufferData
						, UpdateData.NumVerts
						, UpdateData.IndexBufferData
						, UpdateData.NumTriangles
						, UpdateData.RequireNormalAndTangent
						, UpdateData.Section
					);
				}
			});
	}
	if (PendingUpdateRenderSectionPriorityArray.Num() > 0)
	{
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionRenderPriority)(
			[Root, PendingUpdateRenderSectionPriorityArray = MoveTemp(PendingUpdateRenderSectionPriorityArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateRenderSectionPriorityArray)
				{
					Root->SetRenderSectionRenderPriority_RenderThread(UpdateData.SectionProxy, UpdateData.RenderPriority);
				}
			});
	}
	if (PendingUpdateMeshSectionMaterialDataArray.Num() > 0)
	{
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
			[Root, PendingUpdateMeshSectionMaterialDataArray = MoveTemp(PendingUpdateMeshSectionMaterialDataArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateMeshSectionMaterialDataArray)
				{
					Root->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material, UpdateData.MaterialProxy);
				}
			});
	}
	if (PendingUpdateMeshSectionBuiltInDataArray.Num() > 0)
	{
		// Taken now, just before the command that reads them is enqueued: see FDreamUIBuiltInTextures.
		for (auto& UpdateData : PendingUpdateMeshSectionBuiltInDataArray)
		{
			UpdateData.Textures = UpdateData.Params.GetTexturesForRenderCommand();
		}
		FDreamUIRenderRoot* Root = RenderRoot.Get();
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionBuiltIn)(
			[Root, PendingUpdateMeshSectionBuiltInDataArray = MoveTemp(PendingUpdateMeshSectionBuiltInDataArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateMeshSectionBuiltInDataArray)
				{
					FDreamUIBuiltInDrawParams Params = UpdateData.Params;
					Params.ResolveTextures_RenderThread(UpdateData.Textures);
					Root->SetMeshSectionBuiltIn_RenderThread(UpdateData.SectionProxy, Params);
				}
			});
	}
}

int32 UDreamUIMeshComponent::GetNumMaterials() const
{
	int Result = 0;
	for (auto& RenderSectionItem : RenderSectionArray)
	{
		switch (RenderSectionItem->Type)
		{
		case EDreamUIRenderSectionType::Mesh:
		case EDreamUIRenderSectionType::DirectMesh:
			Result++;
			break;
		case EDreamUIRenderSectionType::ChildCanvas:
			auto ChildCanvasSection = static_cast<FDreamUIRenderSection_ChildCanvas*>(RenderSectionItem.Get());
			//weak, and legitimately stale between the child canvas going away and the next pool pass
			if (ChildCanvasSection->ChildCanvasMeshComponent.IsValid())
			{
				Result += ChildCanvasSection->ChildCanvasMeshComponent->GetNumMaterials();
			}
			break;
		}
	}
	return Result;
}

FBoxSphereBounds UDreamUIMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (RenderSectionArray.Num() <= 0)
	{
		return FBoxSphereBounds(EForceInit::ForceInitToZero);
	}

	FBox ResultBox = FBox(EForceInit::ForceInit);
	for (auto& RenderSection : RenderSectionArray)
	{
		switch (RenderSection->Type)
		{
		case EDreamUIRenderSectionType::DirectMesh:
			{
				ResultBox += RenderSection->BoundingBox;
			}
			break;
		case EDreamUIRenderSectionType::Mesh:
			{
				ResultBox += RenderSection->BoundingBox;
			}
			break;
		case EDreamUIRenderSectionType::PostProcess:
			{
				if (DreamUIRenderer.IsValid())
				{
					ResultBox += RenderSection->BoundingBox;
				}
			}
			break;
		case EDreamUIRenderSectionType::ChildCanvas:
			{
				ResultBox += RenderSection->BoundingBox;
			}
			break;
		}
	}

	return FBoxSphereBounds(ResultBox);
}
#undef LOCTEXT_NAMESPACE
