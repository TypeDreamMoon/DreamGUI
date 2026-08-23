// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Core/DreamUIRender/IDreamUIRendererPrimitive.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Engine/Engine.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneProxy.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamVisualPostProcessRenderProxy.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
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
	/** Built-in shader parameters; when enabled the material is not used by DreamGUI's renderer. */
	FDreamUIBuiltInDrawParams BuiltIn;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	FDreamUIMeshVertexBuffer DreamUIVertexBuffers;
	/** Index buffer for this section */
	FDreamUIMeshIndexBuffer IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;

	bool bShouldKeepDataWhenDisable = false;
	uint32 ValidVerticesCount = 0;
	uint32 NumPrimitives = 0;

	FDreamUISectionProxy_Mesh(ERHIFeatureLevel::Type InFeatureLevel, bool InShouldKeepDataWhenDisable)
		: VertexFactory(InFeatureLevel, "FDreamUISectionProxy_Mesh")
	{
		Type = EDreamUIRenderSectionProxyType::Mesh;
		bShouldKeepDataWhenDisable = InShouldKeepDataWhenDisable;
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

	virtual void Disable() override
	{
		if (!bShouldKeepDataWhenDisable)
		{
			Material = nullptr;
			BuiltIn = FDreamUIBuiltInDrawParams();
			bCanRender = false;
		}
	}
};
struct FDreamUIRenderSectionProxy_PostProcess : public FDreamUIRenderSectionProxy
{
	FDreamUIRenderSectionProxy_PostProcess()
	{
		Type = EDreamUIRenderSectionProxyType::PostProcess;
	}

	FDreamVisualPostProcessRenderProxy* PostProcessRenderProxy = nullptr;

	virtual void Disable() override
	{
		PostProcessRenderProxy = nullptr;
		bCanRender = false;
	}
};
struct FDreamUIRenderSectionProxy_ChildCanvas : public FDreamUIRenderSectionProxy
{
	FDreamUIRenderSectionProxy_ChildCanvas()
	{
		Type = EDreamUIRenderSectionProxyType::ChildCanvas;
	}

	FPrimitiveComponentId PrimitiveComponentID;
	FDreamUIRenderSceneProxy* ChildCanvasSceneProxy = nullptr;

	virtual void Disable() override
	{
		PrimitiveComponentID = FPrimitiveComponentId();
		ChildCanvasSceneProxy = nullptr;
		bCanRender = false;
	}
};

DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIRenderSceneProxyReleaseDelegate, class FDreamUIRenderSceneProxy*);

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh CreateRenderSection"), STAT_CreateRenderSection, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUIMesh UpdateMeshSection_RT"), STAT_UpdateMeshSectionRT, STATGROUP_DreamGUI);

/** DreamUI render scene proxy */
class FDreamUIRenderSceneProxy : public FPrimitiveSceneProxy, public IDreamUIRendererPrimitive
{
public:
	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	FDreamUIRenderSceneProxy(UDreamUIMeshComponent* InComponent, UDreamCanvas* InCanvasPtr, bool InIsRenderCanvas)
		: FPrimitiveSceneProxy(InComponent)
		, MaterialRelevance(InComponent->GetMaterialRelevance(GetScene().GetShaderPlatform()))
		, RenderPriority(InComponent->TranslucencySortPriority)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateRenderSection);
#if !UE_BUILD_SHIPPING
		static int DebugNameSuffix = 0;
		DebugName = FString::Printf(TEXT("%s_SceneProxy_%d"), *InCanvasPtr->GetWidget()->GetDisplayName(), DebugNameSuffix++);
#endif
		DreamUIRenderer = InComponent->DreamUIRenderer;
		RenderCanvasPtr = InCanvasPtr;
		bIsDreamUIRenderToWorld = InComponent->bIsDreamUIRenderToWorld;
		bIsRenderCanvas = InIsRenderCanvas;
		if (DreamUIRenderer.IsValid())
		{
			auto TempRenderer = DreamUIRenderer;
			auto SceneProxy = this;
			auto IsRenderToWorld = bIsDreamUIRenderToWorld;
			auto BlendDepth = InCanvasPtr->GetActualBlendDepth();
			auto DepthFade = InCanvasPtr->GetActualDepthFade();
			ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_AddPrimitive)(
				[TempRenderer, SceneProxy, InCanvasPtr, BlendDepth, DepthFade, IsRenderToWorld](FRHICommandListImmediate& RHICmdList)
				{
					if (TempRenderer.IsValid())
					{
						if (IsRenderToWorld)
						{
							TempRenderer.Pin()->AddWorldSpacePrimitive_RenderThread(InCanvasPtr, BlendDepth, DepthFade, SceneProxy);
						}
						else
						{
							TempRenderer.Pin()->AddScreenSpacePrimitive_RenderThread(SceneProxy);
						}
					}
				}
			);
			bIsSupportDreamUIRenderer = true;
		}
		bIsSupportUERenderer = InComponent->bIsSupportUERenderer;

		auto& SrcSections = InComponent->RenderSectionArray;
		SectionArray.SetNumZeroed(SrcSections.Num());
		for (int SectionIndex = 0; SectionIndex < SrcSections.Num(); SectionIndex++)
		{
			auto Section = CreateSectionData(SrcSections[SectionIndex].Get());
			SectionArray[SectionIndex] = Section;
		}
		bNeedToSortRenderSections = true;
	}

	void AddSectionData(FDreamUIRenderSection* SrcSection)
	{
		auto Section = CreateSectionData(SrcSection);
		check (Section);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_AddSectionData)(
			[this, Section](FRHICommandListImmediate& RHICmdList)
			{
				SectionArray.Add(Section);
			}
		);
		bNeedToSortRenderSections = true;
	}

	void RecreateSectionData(FDreamUIRenderSection* InSrcSection)
	{
		auto OldSection = InSrcSection->RenderProxy;
		auto NewSection = CreateSectionData(InSrcSection);
		check(NewSection);
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_ReplaceSectionData)(
			[this, OldSection, NewSection](FRHICommandListImmediate& RHICmdList) {
				auto SectionIndex = SectionArray.IndexOfByKey(OldSection);
				SectionArray[SectionIndex] = NewSection;
				delete OldSection;
			});
	}
	void UpdatePostProcessSection(FDreamUIRenderSection_PostProcess* InSrcSection, FDreamVisualPostProcessRenderProxy* InRenderProxy)
	{
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_ReplaceSectionData)(
			[this, InSrcSection, InRenderProxy](FRHICommandListImmediate& RHICmdList) {
				auto PostProcessRenderProxy = static_cast<FDreamUIRenderSectionProxy_PostProcess*>(InSrcSection->RenderProxy);
				PostProcessRenderProxy->PostProcessRenderProxy = InRenderProxy;
				PostProcessRenderProxy->bCanRender = true;
			});
	}
	void UpdateChildCanvasSection(FDreamUIRenderSection_ChildCanvas* InSrcSection, UDreamUIMeshComponent* InComp)
	{
		ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_ReplaceSectionData)(
			[this, InSrcSection, CompID = InComp->GetPrimitiveSceneId(), SceneProxy = InComp->SceneProxy](FRHICommandListImmediate& RHICmdList) {
				auto ChildCanvasRenderProxy = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(InSrcSection->RenderProxy);
				ChildCanvasRenderProxy->PrimitiveComponentID = CompID;
				if (SceneProxy != nullptr)
				{
					ChildCanvasRenderProxy->ChildCanvasSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
				}
				ChildCanvasRenderProxy->bCanRender = true;
			});
	}

	FDreamUIRenderSectionProxy* CreateSectionData(FDreamUIRenderSection* InSrcSection)
	{
		switch (InSrcSection->Type)
		{
		case EDreamUIRenderSectionType::Mesh:
		case EDreamUIRenderSectionType::DirectMesh:
			{
				auto SrcSection = static_cast<FDreamUIRenderSection_Mesh*>(InSrcSection);
				if (SrcSection->Vertices.Num() == 0 || SrcSection->TriangleIndices.Num() == 0)
				{
					SrcSection->RenderProxy = nullptr;
					check(0);
					return nullptr;
				}
				auto NewSectionProxy = new FDreamUISectionProxy_Mesh(GetScene().GetFeatureLevel()
					, InSrcSection->Type == EDreamUIRenderSectionType::DirectMesh//direct mesh should not clear data when pooling
					);
				// vertex and index buffer
				auto& Indices = NewSectionProxy->IndexBuffer.Indices;
				Indices.SetNumUninitialized(SrcSection->TriangleIndices.Num());
				FMemory::Memcpy(Indices.GetData(), SrcSection->TriangleIndices.GetData(), SrcSection->TriangleIndices.Num() * sizeof(FDreamUIMeshIndex));

				auto& SrcVertices = SrcSection->Vertices;
				
				NewSectionProxy->ValidVerticesCount = SrcSection->ValidVerticesNum;
				NewSectionProxy->NumPrimitives = SrcSection->ValidTriangleIndicesNum / 3;
				if (bIsSupportDreamUIRenderer)
				{
					auto& Vertices = NewSectionProxy->DreamUIVertexBuffers.Vertices;
					Vertices.SetNumUninitialized(SrcVertices.Num());
					FMemory::Memcpy(Vertices.GetData(), SrcVertices.GetData(), SrcVertices.Num() * sizeof(FDreamUIMeshVertex));
					
					// Enqueue initialization of render resource
					BeginInitResource(&NewSectionProxy->IndexBuffer);
					BeginInitResource(&NewSectionProxy->DreamUIVertexBuffers);
				}
				if (bIsSupportUERenderer)
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
				NewSectionProxy->BuiltIn = SrcSection->BuiltIn;
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
				NewSectionProxy->PostProcessRenderProxy = SrcSection->PostProcessVisualObject->GetRenderProxy();

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
				NewSectionProxy->PrimitiveComponentID = ChildCanvasMeshItem->GetPrimitiveSceneId();
				if (ChildCanvasMeshItem->SceneProxy != nullptr)
				{
					auto ChildSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(ChildCanvasMeshItem->SceneProxy);
					NewSectionProxy->ChildCanvasSceneProxy = ChildSceneProxy;
					ChildSceneProxy->OnRelease.AddRaw(this, &FDreamUIRenderSceneProxy::ClearChildCanvasSectionData_RenderThread);
				}

				// Copy info
				NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
				SrcSection->RenderProxy = NewSectionProxy;

				return NewSectionProxy;
			}
		}
		check(0);
		return nullptr;
	}
	void SetChildCanvasSectionData_RenderThread(FPrimitiveComponentId CompID, FDreamUIRenderSceneProxy* SceneProxy)
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
					ChildCanvasSection->ChildCanvasSceneProxy = SceneProxy;
					ChildCanvasSection->ChildCanvasSceneProxy->OnRelease.AddRaw(this, &FDreamUIRenderSceneProxy::ClearChildCanvasSectionData_RenderThread);
				}
			}
		}
	}
	void ClearChildCanvasSectionData_RenderThread(FDreamUIRenderSceneProxy* SceneProxy)
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			if (Section == nullptr)continue;
			if (Section->Type == EDreamUIRenderSectionProxyType::ChildCanvas)
			{
				auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(Section);
				if (ChildCanvasSection->ChildCanvasSceneProxy == SceneProxy)//child could already get new proxy, so need to check it
				{
					ChildCanvasSection->ChildCanvasSceneProxy->OnRelease.RemoveAll(this);
					ChildCanvasSection->ChildCanvasSceneProxy = nullptr;
					return;
				}
			}
		}
	}
	
	void PoolAllSectionData_RenderThread()
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			Section->Disable();
		}
	}

	void SetRenderPriority_RenderThread(int32 NewPriority)
	{
		RenderPriority = NewPriority;
	}
	void SetMeshSectionMaterial_RenderThread(FDreamUIRenderSectionProxy* Section, UMaterialInterface* Material)
	{
		(static_cast<FDreamUISectionProxy_Mesh*>(Section))->Material = Material;
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

	void DetachChildCanvasSection_RenderThread(FDreamUIRenderSectionProxy* Section)
	{
		if (Section == nullptr || Section->Type != EDreamUIRenderSectionProxyType::ChildCanvas)
		{
			return;
		}
		auto ChildCanvasSection = static_cast<FDreamUIRenderSectionProxy_ChildCanvas*>(Section);
		if (ChildCanvasSection->ChildCanvasSceneProxy != nullptr)
		{
			ChildCanvasSection->ChildCanvasSceneProxy->OnRelease.RemoveAll(this);
		}
		ChildCanvasSection->ChildCanvasSceneProxy = nullptr;
	}

	virtual ~FDreamUIRenderSceneProxy()override
	{
		OnRelease.Broadcast(this);
#if !UE_BUILD_SHIPPING
		DebugName = FString::Printf(TEXT("%s_Deleted"), *DebugName);
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
				MeshMemorySize += MeshSection->IndexBuffer.Indices.Num() * sizeof(FDreamUIMeshIndexBufferType);
			}
		}
		return MeshMemorySize;
	}
#endif

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FRHICommandListImmediate& RHICmdList
		, const TArray<FDreamUIMeshVertex>& MeshVertexData, const int32& NumVerts
		, const TArray<FDreamUIMeshIndex>& MeshIndexData, const int32& NumTriangles
		, bool RequireNormalAndTangent
		, FDreamUISectionProxy_Mesh* Section)const
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionRT);

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
		if(bIsSupportUERenderer)
		{
			for (int i = 0; i < NumVerts; i++)
			{
				auto& DreamUIVert = MeshVertexData[i];
				Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = DreamUIVert.Position;
				Section->VertexBuffers.ColorVertexBuffer.VertexColor(i) = DreamUIVert.Color;
				if (RequireNormalAndTangent)
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, DreamUIVert.TangentX.ToFVector3f(), DreamUIVert.GetTangentY(), DreamUIVert.TangentZ.ToFVector3f());
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, DreamUIVert.TextureCoordinate[0]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 1, DreamUIVert.TextureCoordinate[1]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 2, DreamUIVert.TextureCoordinate[2]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 3, DreamUIVert.TextureCoordinate[3]);
			}

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

		Section->NumPrimitives = NumTriangles;
		uint32 IndicesDataLength = NumTriangles * 3 * sizeof(FDreamUIMeshIndex);
		// Lock index buffer
		auto IndexBufferData = RHICmdList.LockBuffer(Section->IndexBuffer.IndexBufferRHI, 0, IndicesDataLength, RLM_WriteOnly);
		FMemory::Memcpy(IndexBufferData, MeshIndexData.GetData(), IndicesDataLength);
		RHICmdList.UnlockBuffer(Section->IndexBuffer.IndexBufferRHI);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!bIsSupportUERenderer) return;
		if (!DreamUI_CanRender())return;
		GetMeshElements_UERenderer(Views, ViewFamily, VisibilityMap, Collector);
	}
	void GetMeshElements_UERenderer(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
	{
		if (bNeedToSortRenderSections)
		{
			auto DreamUIMeshSceneProxy = const_cast<FDreamUIRenderSceneProxy*>(this);
			DreamUIMeshSceneProxy->bNeedToSortRenderSections = false;
			DreamUIMeshSceneProxy->SortMeshSectionRenderPriority_RenderThread();
		}
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
		if (bWireframe)
		{
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
				FLinearColor(0, 0.5f, 1.f)
			);

			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
		}

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
				if (!bWireframe && Section->Material == nullptr)
				{
					break;//built-in sections are drawn by the DreamUI renderer only
				}
				FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

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
				auto ChildSceneProxy = Section->ChildCanvasSceneProxy;
				if (ChildSceneProxy != nullptr)
				{
					ChildSceneProxy->GetMeshElements_UERenderer(Views, ViewFamily, VisibilityMap, Collector);
				}
			}
			break;
			}
		}
	}

	//begin IDreamUIRendererPrimitive interface
	virtual FVector3f DreamUI_GetWorldPositionForSortTranslucent()const override 
	{
		return FVector3f(GetLocalToWorld().GetOrigin()); 
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
			WireframeMaterialInstance = GEngine->WireframeMaterial->GetRenderProxy();
		}

		for (int i = 0; i < PrimitiveData.Sections.Num(); i++)
		{
			auto SectionData = PrimitiveData.Sections[i];
			auto RenderSection = SectionData.SectionPointer;

			auto Section = static_cast<FDreamUISectionProxy_Mesh*>(RenderSection);
			FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : (Section->Material ? Section->Material->GetRenderProxy() : nullptr);
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

			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), GetLocalToWorld(), GetBounds(), GetLocalBounds(), false, false, false);
			BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
			//BatchElement.PrimitiveUniformBuffer = CreatePrimitiveUniformBufferImmediate(GetLocalToWorld(), GetBounds(), GetLocalBounds(), false, UseEditorDepthTest());

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = Section->NumPrimitives;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = Section->ValidVerticesCount - 1;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;

			FDreamUIMeshBatchContainer MeshBatchContainer;
			MeshBatchContainer.Mesh = Mesh;
			MeshBatchContainer.VertexBufferRHI = Section->DreamUIVertexBuffers.VertexBufferRHI;
			MeshBatchContainer.NumVerts = Section->ValidVerticesCount;
			MeshBatchContainer.BuiltIn = bWireframe ? FDreamUIBuiltInDrawParams() : Section->BuiltIn;
			MeshBatchContainer.LocalToWorld = GetLocalToWorld();
			ResultArray.Add(MeshBatchContainer);
		}
	}

	virtual FDreamVisualPostProcessRenderProxy* DreamUI_GetPostProcessElement(FDreamUIRenderSectionProxy* SectionPtr)const override
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
		return bIsRenderCanvas && SectionArray.Num() > 0;
	}
	virtual FPrimitiveComponentId DreamUI_GetPrimitiveComponentId() const override 
	{
		return FPrimitiveSceneProxy::GetPrimitiveComponentId();
	}
	virtual FBoxSphereBounds DreamUI_GetWorldBounds()const override { return FPrimitiveSceneProxy::GetBounds(); }
	//end IDreamUIRendererPrimitive interface
	void CollectRenderData_Implement(TArray<FDreamUIPrimitiveDataContainer>& OutRenderDataArray)
	{
		if (SectionArray.Num() <= 0)return;
		if (bNeedToSortRenderSections)
		{
			bNeedToSortRenderSections = false;
			this->SortMeshSectionRenderPriority_RenderThread();
		}

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
					if (Section->PostProcessRenderProxy->CanRender())
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
					auto ChildSceneProxy = Section->ChildCanvasSceneProxy;
					if (ChildSceneProxy != nullptr)
					{
						ChildSceneProxy->CollectRenderData_Implement(OutRenderDataArray);
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
#if DEBUG_PRINT_MESH_MEMORY
	uint32 GetMeshMemorySize()const { return MeshMemorySize; }
	uint32 GetMaxVertexBufferSize()const{return MaxVertexBufferSize;}
#endif
private:
	TArray<FDreamUIRenderSectionProxy*> SectionArray;
#if DEBUG_PRINT_MESH_MEMORY
	uint32 MeshMemorySize = 0;
	uint32 MaxVertexBufferSize = 0;
#endif
	FMaterialRelevance MaterialRelevance;
	int32 RenderPriority = 0;
	TWeakPtr<FDreamUIRenderer, ESPMode::ThreadSafe> DreamUIRenderer;
	bool bIsSupportDreamUIRenderer = false;
	bool bIsSupportUERenderer = true;
	bool bIsDreamUIRenderToWorld = false;
	bool bNeedToSortRenderSections = true;
	bool bIsRenderCanvas = false;
	TWeakObjectPtr<UDreamCanvas> RenderCanvasPtr = nullptr;
	FDreamUIRenderSceneProxyReleaseDelegate OnRelease;
};



void FDreamUIRenderSection_Mesh::ClearBeforePool()
{
	Material = nullptr;
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

TSharedPtr<FDreamUIRenderSection> UDreamUIMeshComponent::SetupRenderSection(EDreamUIRenderSectionType InType, FDreamUIDrawCall* InDrawCallData)
{
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
	switch (InType)
	{
	case EDreamUIRenderSectionType::Mesh:
		RenderSection = GetMeshRenderSectionFromPool(InDrawCallData->CombinedBatchMeshGeometryVertices.Num());
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
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);
					ThisSceneProxy->AddSectionData(MeshSectionPtr);
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
		}
		break;
	case EDreamUIRenderSectionType::PostProcess:
		{
			auto PostProcessSectionPtr = static_cast<FDreamUIRenderSection_PostProcess*>(RenderSection.Get());
			auto PostProcessVisualObject = InDrawCallData->PostProcessVisualObject;
			PostProcessSectionPtr->PostProcessVisualObject = PostProcessVisualObject;
			auto BoundingBox = FBox(EForceInit::ForceInit);
			FVector Min, Max;
			PostProcessVisualObject->GetGeometryBounds3DInLocalSpace(Min, Max);
			BoundingBox += Min;
			BoundingBox += Max;
			PostProcessSectionPtr->BoundingBox = BoundingBox;
			if (PostProcessSectionPtr->RenderProxy)//if we have valid render-proxy then update data
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
					auto RenderProxy = PostProcessSectionPtr->PostProcessVisualObject->GetRenderProxy();
					ThisSceneProxy->UpdatePostProcessSection(PostProcessSectionPtr, RenderProxy);
				}
			}
			else
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);
					ThisSceneProxy->AddSectionData(PostProcessSectionPtr);
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
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
					ThisSceneProxy->UpdateChildCanvasSection(ChildCanvasSectionPtr, InDrawCallData->ChildCanvas->GetUIMesh());
				}
			}
			else
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);
					ThisSceneProxy->AddSectionData(ChildCanvasSectionPtr);
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
		if (this->SceneProxy != nullptr)
		{
			auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);
			ThisSceneProxy->AddSectionData(MeshSectionPtr);
		}
	}
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
		if (this->SceneProxy != nullptr)
		{
			auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(this->SceneProxy);
			ThisSceneProxy->AddSectionData(InDirectMeshSection);
		}
	}

	SetDirectMeshRenderSectionMaterial(InDirectMeshSection, InMaterial);
}

void UDreamUIMeshComponent::SetDirectMeshRenderSectionMaterial(FDreamUIRenderSection_DirectMesh* InDirectMeshSection, UMaterialInterface* InMaterial)
{
	InDirectMeshSection->Material = InMaterial;
	if (SceneProxy)
	{
		if (InDirectMeshSection->RenderProxy)
		{
			UpdateMeshSectionMaterialDataStruct UpdateData;
			UpdateData.SectionProxy = InDirectMeshSection->RenderProxy;
			UpdateData.Material = InMaterial;

			auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
				[DreamUIMeshSceneProxy, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					DreamUIMeshSceneProxy->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material);
				});
		}
	}
}

#define LATE_FLUSH_RENDER_CMD 1
DECLARE_CYCLE_STAT(TEXT("DreamUIMesh UpdateMeshSection_GT"), STAT_UpdateMeshSectionGT, STATGROUP_DreamGUI);
void UDreamUIMeshComponent::UpdateMeshSectionRenderData(FDreamUIRenderSection_Mesh* InMeshSection, bool InRequireNormalAndTangent)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionGT);
	if (SceneProxy)
	{
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
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
			[DreamUIMeshSceneProxy, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList)
			{
				DreamUIMeshSceneProxy->UpdateSection_RenderThread(
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
	if (SceneProxy)
	{
		auto ThisSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ThisSceneProxy->RecreateSectionData(InMeshSection);
	}
}

void UDreamUIMeshComponent::PoolAllRenderSection()
{
	if (SceneProxy)
	{
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_PoolAllSectionData)(
			[DreamUIMeshSceneProxy](FRHICommandListImmediate& RHICmdList) {
				DreamUIMeshSceneProxy->PoolAllSectionData_RenderThread();
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
				ChildCanvasMeshCom->OnSceneProxyCreated.RemoveAll(this);
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
	if (SceneProxy)
	{
		if (RenderSection->RenderProxy)
		{
			UpdateRenderSectionPriority UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.RenderPriority = InSortPriority;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateRenderSectionPriorityArray.Add(MoveTemp(UpdateData));
#else
			auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionRenderPriority)(
				[DreamUIMeshSceneProxy, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					DreamUIMeshSceneProxy->SetRenderSectionRenderPriority_RenderThread(UpdateData.SectionProxy, UpdateData.RenderPriority);
				});
#endif
		}
	}
}

void UDreamUIMeshComponent::SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial)
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	check(RenderSection->Type == EDreamUIRenderSectionType::Mesh);
	(static_cast<FDreamUIRenderSection_Mesh*>(RenderSection.Get()))->Material = InMaterial;
	if (SceneProxy)
	{
		if (RenderSection->RenderProxy)
		{
			UpdateMeshSectionMaterialDataStruct UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.Material = InMaterial;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateMeshSectionMaterialDataArray.Add(MoveTemp(UpdateData));
#else
			auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
				[DreamUIMeshSceneProxy, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					DreamUIMeshSceneProxy->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material);
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
	if (SceneProxy)
	{
		if (RenderSection->RenderProxy)
		{
			UpdateMeshSectionBuiltInDataStruct UpdateData;
			UpdateData.SectionProxy = RenderSection->RenderProxy;
			UpdateData.Params = InParams;
#if LATE_FLUSH_RENDER_CMD
			PendingUpdateMeshSectionBuiltInDataArray.Add(MoveTemp(UpdateData));
#else
			auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
			ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionBuiltIn)(
				[DreamUIMeshSceneProxy, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList) {
					DreamUIMeshSceneProxy->SetMeshSectionBuiltIn_RenderThread(UpdateData.SectionProxy, UpdateData.Params);
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
			{
				auto MeshSection = static_cast<FDreamUIRenderSection_Mesh*>(RenderSectionItem.Get());
				SetMaterialForUI(MatIndex++, MeshSection->Material);
			}
			break;
		case EDreamUIRenderSectionType::ChildCanvas:
			{
				auto ChildCanvasSection = static_cast<FDreamUIRenderSection_ChildCanvas*>(RenderSectionItem.Get());
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
			ChildCanvasMeshCom->OnSceneProxyCreated.RemoveAll(ParentCanvasMeshComp.Get());
		}
		
		ParentCanvasMeshComp = InParentCanvasMeshComp;

		ChildCanvasMeshCom->OnSceneProxyCreated.AddWeakLambda(InParentCanvasMeshComp, [InParentCanvasMeshComp](UDreamUIMeshComponent* InChildMeshComp, FDreamUIRenderSceneProxy* InSceneProxy) {
			if (InParentCanvasMeshComp->SceneProxy != nullptr)
			{
				auto ParentSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(InParentCanvasMeshComp->SceneProxy);//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
				ENQUEUE_RENDER_COMMAND(FDreamUIRenderSceneProxy_ReassignChildCanvasSectionData)(
					[ParentSceneProxy, CompID = InChildMeshComp->GetPrimitiveSceneId(), InSceneProxy](FRHICommandListImmediate& RHICmdList) {
						ParentSceneProxy->SetChildCanvasSectionData_RenderThread(CompID, InSceneProxy);
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
			ChildCanvasMeshCom->OnSceneProxyCreated.RemoveAll(ParentCanvasMeshComp.Get());
		}
		ParentCanvasMeshComp = nullptr;
	}
}

void UDreamUIMeshComponent::SetUITranslucentSortPriority(int32 NewTranslucentSortPriority)
{
	UPrimitiveComponent::SetTranslucentSortPriority(NewTranslucentSortPriority);
	if (SceneProxy)
	{
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMesh_SetUITranslucentSortPriority)(
			[DreamUIMeshSceneProxy, NewTranslucentSortPriority](FRHICommandListImmediate& RHICmdList)
		{
			DreamUIMeshSceneProxy->SetRenderPriority_RenderThread(NewTranslucentSortPriority);
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
	if (SceneProxy)
	{
		auto Proxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
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

struct FDreamUIPrimitiveComponentIdTemporaryModifier
{
	UDreamUIMeshComponent* Comp = nullptr;
	FPrimitiveComponentId OriginId;
	FDreamUIPrimitiveComponentIdTemporaryModifier(UDreamUIMeshComponent* InComp, FPrimitiveComponentId InNewId)
	{
		Comp = InComp;
		OriginId = Comp->GetPrimitiveSceneId();
		Comp->GetPrimitiveSceneId() = InNewId;
	}
	~FDreamUIPrimitiveComponentIdTemporaryModifier()
	{
		Comp->GetPrimitiveSceneId() = OriginId;
	}
};

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh CreateSceneProxy"), STAT_DreamUIMesh_CreateSceneProxy, STATGROUP_DreamGUI);
FPrimitiveSceneProxy* UDreamUIMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUIMesh_CreateSceneProxy);
	//clear section data
	RenderSectionPool.Empty();
	for (auto& RenderSectionPoolItem : RenderSectionMesh_CascadePool)
	{
		RenderSectionPoolItem.RenderSections.Empty();
	}

	FDreamUIRenderSceneProxy* Proxy = nullptr;
	if (RenderSectionArray.Num() > 0)
	{
		Proxy = new FDreamUIRenderSceneProxy(this, RenderCanvas.Get()
			, !ParentCanvasMeshComp.IsValid()//child canvas is render by it's parent
			);
		OnSceneProxyCreated.Broadcast(this, Proxy);
	}
	return Proxy;
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
	MarkRenderStateDirty();//mark dirty to recreate SceneProxy
	RenderSectionArray.Empty();
	RenderSectionPool.Empty();
	for (auto& RenderSectionPoolItem : RenderSectionMesh_CascadePool)
	{
		RenderSectionPoolItem.RenderSections.Empty();
	}
	OnSceneProxyCreated.Clear();
	ParentCanvasMeshComp = nullptr;
	DreamUIRenderer = nullptr;
}

DECLARE_CYCLE_STAT(TEXT("DreamUIMesh FlushRenderCommand"), STAT_DreamUIMesh_FlushRenderCommand, STATGROUP_DreamGUI);
void UDreamUIMeshComponent::FlushRenderCommand()
{
	SCOPE_CYCLE_COUNTER(STAT_DreamUIMesh_FlushRenderCommand)
	if (PendingUpdateMeshSectionDataArray.Num() > 0)
	{
		//update data
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
			[DreamUIMeshSceneProxy, PendingUpdateMeshSectionDataArray = MoveTemp(PendingUpdateMeshSectionDataArray)](FRHICommandListImmediate& RHICmdList)
			{
				for (auto& UpdateData : PendingUpdateMeshSectionDataArray)
				{
					DreamUIMeshSceneProxy->UpdateSection_RenderThread(
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
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionRenderPriority)(
			[DreamUIMeshSceneProxy, PendingUpdateRenderSectionPriorityArray = MoveTemp(PendingUpdateRenderSectionPriorityArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateRenderSectionPriorityArray)
				{
					DreamUIMeshSceneProxy->SetRenderSectionRenderPriority_RenderThread(UpdateData.SectionProxy, UpdateData.RenderPriority);
				}
			});
	}
	if (PendingUpdateMeshSectionMaterialDataArray.Num() > 0)
	{
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionMaterial)(
			[DreamUIMeshSceneProxy, PendingUpdateMeshSectionMaterialDataArray = MoveTemp(PendingUpdateMeshSectionMaterialDataArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateMeshSectionMaterialDataArray)
				{
					DreamUIMeshSceneProxy->SetMeshSectionMaterial_RenderThread(UpdateData.SectionProxy, UpdateData.Material);
				}
			});
	}
	if (PendingUpdateMeshSectionBuiltInDataArray.Num() > 0)
	{
		auto DreamUIMeshSceneProxy = static_cast<FDreamUIRenderSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshSectionProxy_SetMeshSectionBuiltIn)(
			[DreamUIMeshSceneProxy, PendingUpdateMeshSectionBuiltInDataArray = MoveTemp(PendingUpdateMeshSectionBuiltInDataArray)](FRHICommandListImmediate& RHICmdList) {
				for (auto& UpdateData : PendingUpdateMeshSectionBuiltInDataArray)
				{
					DreamUIMeshSceneProxy->SetMeshSectionBuiltIn_RenderThread(UpdateData.SectionProxy, UpdateData.Params);
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
			Result += ChildCanvasSection->ChildCanvasMeshComponent->GetNumMaterials();
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
