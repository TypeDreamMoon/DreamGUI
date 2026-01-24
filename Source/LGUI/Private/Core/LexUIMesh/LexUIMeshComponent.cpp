// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIMesh/LexUIMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/BodySetup.h"
#include "Containers/ResourceArray.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Core/LexUIRender/ILexUIRendererPrimitive.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Engine/Engine.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneProxy.h"
#include "ToolMenusEditor.h"
#include "Core/LexUIDrawCall.h"
#include "Core/LexVisualPostProcessRenderProxy.h"
#include "Core/Components/LexVisualPostProcess.h"


#define LOCTEXT_NAMESPACE "LexUIMeshComponent"

class FLexUIMeshVertexResourceArray : public FResourceArrayInterface
{
public:
	FLexUIMeshVertexResourceArray(void* InData, uint32 InSize)
		:Data(InData)
		,Size(InSize)
	{

	}
	virtual const void* GetResourceData() const override { return Data; }
	virtual uint32 GetResourceDataSize() const override { return Size; }
	virtual void Discard() override { }
	virtual bool IsStatic() const override { return false; }
	virtual bool GetAllowCPUAccess() const override { return false; }
	virtual void SetAllowCPUAccess(bool bInNeedsCPUAccess) override { }
private: 
	void* Data;
	uint32 Size;
};
class FLexUIVertexBuffer : public FVertexBuffer
{
public:
	TArray<FLexUIMeshVertex> Vertices;
	virtual void InitRHI(FRHICommandListBase& RHICmdList)override
	{
		const uint32 SizeInBytes = Vertices.Num() * sizeof(FLexUIMeshVertex);

		FLexUIMeshVertexResourceArray ResourceArray(Vertices.GetData(), SizeInBytes);
		FRHIResourceCreateInfo CreateInfo(TEXT("LexUIVertexBuffer"), &ResourceArray);
		VertexBufferRHI = RHICmdList.CreateVertexBuffer(SizeInBytes, BUF_Dynamic, CreateInfo);
	}
};


struct FLexUIRenderSectionProxy
{
	virtual ~FLexUIRenderSectionProxy()
	{

	}

	ELexUIRenderSectionType Type;

	/** Sort order */
	int SectionRenderPriority = 0;
	bool bCanRender = true;

	virtual void Disable() = 0;
};
/** Class representing a single section of the LexUI mesh */
struct FLexUIMeshSectionProxy : public FLexUIRenderSectionProxy
{
	/** Material applied to this section */
	UMaterialInterface* Material = nullptr;
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	FLexUIVertexBuffer LexUIVertexBuffers;
	/** Index buffer for this section */
	FLexUIMeshIndexBuffer IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;

	uint32 ValidVerticesCount = 0;
	uint32 NumPrimitives = 0;

	FLexUIMeshSectionProxy(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FLexUIMeshProxySection")
	{
		Type = ELexUIRenderSectionType::Mesh;
	}
	~FLexUIMeshSectionProxy()
	{
		IndexBuffer.ReleaseResource();
		LexUIVertexBuffers.ReleaseResource();
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

	void InitFromLexUIVertexData(TArray<FLexUIMeshVertex>& Vertices)
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
		ENQUEUE_RENDER_COMMAND(FLexUIRenderSceneProxy_InitFromLexUIVertexData)(
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
		Material = nullptr;
		bCanRender = false;
	}
};
struct FLexUIPostProcessSectionProxy : public FLexUIRenderSectionProxy
{
	FLexUIPostProcessSectionProxy()
	{
		Type = ELexUIRenderSectionType::PostProcess;
	}

	FLexVisualPostProcessRenderProxy* PostProcessRenderProxy = nullptr;

	virtual void Disable() override
	{
		PostProcessRenderProxy = nullptr;
		bCanRender = false;
	}
};
struct FLexUIChildCanvasSectionProxy : public FLexUIRenderSectionProxy
{
	FLexUIChildCanvasSectionProxy()
	{
		Type = ELexUIRenderSectionType::ChildCanvas;
	}

	FPrimitiveComponentId PrimitiveComponentID;
	FLexUIRenderSceneProxy* ChildCanvasSceneProxy = nullptr;

	virtual void Disable() override
	{
		PrimitiveComponentID = FPrimitiveComponentId();
		ChildCanvasSceneProxy = nullptr;
		bCanRender = false;
	}
};

DECLARE_CYCLE_STAT(TEXT("LexUIMesh CreateRenderSection"), STAT_CreateRenderSection, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("LexUIMesh UpdateMeshSection_RT"), STAT_UpdateMeshSectionRT, STATGROUP_LGUI);
/** LexUI render scene proxy */
class FLexUIRenderSceneProxy : public FPrimitiveSceneProxy, public ILexUIRendererPrimitive
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	FLexUIRenderSceneProxy(ULexUIMeshComponent* InComponent, ULexCanvas* InCanvasPtr, int32 InCanvasSortOrder, FLexUIRenderSceneProxy* InParentSceneProxy)
		: FPrimitiveSceneProxy(InComponent)
		, MaterialRelevance(InComponent->GetMaterialRelevance(GetScene().GetFeatureLevel()))
		, RenderPriority(InComponent->TranslucencySortPriority)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateRenderSection);
#if !UE_BUILD_SHIPPING
		DebugName = FName(FString::Printf(TEXT("%s_SceneProxy_%d"), *InComponent->GetName(), DebugNameIndex++));
#endif
		LexUIRenderer = InComponent->LexUIRenderer;
		RenderCanvasPtr = InCanvasPtr;
		CanvasLastRenderTime = &RenderCanvasPtr->LastRenderTime;
		bIsLexUIRenderToWorld = InComponent->bIsLexUIRenderToWorld;
		ParentSceneProxy = InParentSceneProxy;
		if (LexUIRenderer.IsValid())
		{
			auto TempRenderer = LexUIRenderer;
			auto SceneProxy = this;
			auto IsRenderToWorld = bIsLexUIRenderToWorld;
			ENQUEUE_RENDER_COMMAND(FLexUIRenderSceneProxy_AddPrimitive)(
				[TempRenderer, SceneProxy, InCanvasPtr, InCanvasSortOrder, IsRenderToWorld](FRHICommandListImmediate& RHICmdList)
				{
					if (TempRenderer.IsValid())
					{
						if (IsRenderToWorld)
						{
							TempRenderer.Pin()->AddWorldSpacePrimitive_RenderThread(InCanvasPtr, SceneProxy);
						}
						else
						{
							TempRenderer.Pin()->AddScreenSpacePrimitive_RenderThread(SceneProxy);
						}
					}
				}
			);
			bIsSupportLexUIRenderer = true;
		}
		bIsSupportUERenderer = InComponent->bIsSupportUERenderer;

		auto& SrcSections = InComponent->RenderSectionArray;
		SectionArray.SetNumZeroed(SrcSections.Num());
		for (int SectionIndex = 0; SectionIndex < SrcSections.Num(); SectionIndex++)
		{
			SectionArray[SectionIndex] = CreateSectionData(SrcSections[SectionIndex].Get());
		}
		bNeedToSortRenderSections = true;
	}

	void AddSectionData(FLexUIRenderSection* SrcSection)
	{
		auto Section = CreateSectionData(SrcSection);
		if (Section != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(FLexUIRenderSceneProxy_AddSectionData)(
				[this, Section](FRHICommandListImmediate& RHICmdList)
				{
					SectionArray.Add(Section);
				}
			);
			bNeedToSortRenderSections = true;
		}
	}

	void RecreateSectionData(FLexUIRenderSection* InSrcSection)
	{
		auto OldSection = InSrcSection->RenderProxy;
		auto NewSection = CreateSectionData(InSrcSection);
		ENQUEUE_RENDER_COMMAND(FLGUIRenderSceneProxy_ReplaceSectionData)(
			[this, OldSection, NewSection](FRHICommandListImmediate& RHICmdList) {
				auto SectionIndex = SectionArray.IndexOfByKey(OldSection);
				SectionArray[SectionIndex] = NewSection;
				delete OldSection;
			});
	}
	void UpdatePostProcessSection(FLexUIRenderSection_PostProcess* InSrcSection, FLexVisualPostProcessRenderProxy* InRenderProxy)
	{
		ENQUEUE_RENDER_COMMAND(FLGUIRenderSceneProxy_ReplaceSectionData)(
			[this, InSrcSection, InRenderProxy](FRHICommandListImmediate& RHICmdList) {
				auto PostProcessRenderProxy = (FLexUIPostProcessSectionProxy*)InSrcSection->RenderProxy;
				PostProcessRenderProxy->PostProcessRenderProxy = InRenderProxy;
				PostProcessRenderProxy->bCanRender = true;
			});
	}
	void UpdateChildCanvasSection(FLexUIRenderSection_ChildCanvas* InSrcSection, ULexUIMeshComponent* InComp)
	{
		ENQUEUE_RENDER_COMMAND(FLGUIRenderSceneProxy_ReplaceSectionData)(
			[this, InSrcSection, CompID = InComp->GetPrimitiveSceneId(), SceneProxy = InComp->SceneProxy](FRHICommandListImmediate& RHICmdList) {
				auto ChildCanvasRenderProxy = (FLexUIChildCanvasSectionProxy*)InSrcSection->RenderProxy;
				ChildCanvasRenderProxy->PrimitiveComponentID = CompID;
				if (SceneProxy != nullptr)
				{
					ChildCanvasRenderProxy->ChildCanvasSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
					ChildCanvasRenderProxy->ChildCanvasSceneProxy->ParentSceneProxy = this;
				}
				ChildCanvasRenderProxy->bCanRender = true;
			});
	}

	FLexUIRenderSectionProxy* CreateSectionData(FLexUIRenderSection* InSrcSection)
	{
		switch (InSrcSection->Type)
		{
		case ELexUIRenderSectionType::Mesh:
			{
				auto SrcSection = (FLexUIRenderSection_Mesh*)InSrcSection;
				if (SrcSection->vertices.Num() == 0 || SrcSection->triangleIndices.Num() == 0)
				{
					SrcSection->RenderProxy = nullptr;
					return nullptr;
				}
				auto NewSectionProxy = new FLexUIMeshSectionProxy(GetScene().GetFeatureLevel());
				// vertex and index buffer
				const auto& SrcVertices = SrcSection->vertices;
				int NumVerts = SrcVertices.Num();
				NewSectionProxy->ValidVerticesCount = NumVerts;
				NewSectionProxy->NumPrimitives = SrcSection->triangleIndices.Num() / 3;
				if (bIsSupportLexUIRenderer)
				{
					auto& LexUIVertices = NewSectionProxy->LexUIVertexBuffers.Vertices;
					LexUIVertices.SetNumUninitialized(NumVerts);
					FMemory::Memcpy(LexUIVertices.GetData(), SrcVertices.GetData(), NumVerts * sizeof(FLexUIMeshVertex));
					NewSectionProxy->IndexBuffer.Indices = SrcSection->triangleIndices;

					// Enqueue initialization of render resource
					BeginInitResource(&NewSectionProxy->IndexBuffer);
					BeginInitResource(&NewSectionProxy->LexUIVertexBuffers);
				}
				if (bIsSupportUERenderer)
				{
					NewSectionProxy->IndexBuffer.Indices = SrcSection->triangleIndices;
					NewSectionProxy->InitFromLexUIVertexData(SrcSection->vertices);

					// Enqueue initialization of render resource
					BeginInitResource(&NewSectionProxy->VertexBuffers.PositionVertexBuffer);
					BeginInitResource(&NewSectionProxy->VertexBuffers.StaticMeshVertexBuffer);
					BeginInitResource(&NewSectionProxy->VertexBuffers.ColorVertexBuffer);
					BeginInitResource(&NewSectionProxy->IndexBuffer);
					BeginInitResource(&NewSectionProxy->VertexFactory);
				}

				// Grab material
				NewSectionProxy->Material = SrcSection->material;
				if (NewSectionProxy->Material == NULL)
				{
					NewSectionProxy->Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}

				// Copy info
				NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
				SrcSection->RenderProxy = NewSectionProxy;

				return NewSectionProxy;
			}
			break;
		case ELexUIRenderSectionType::PostProcess:
			{
				auto SrcSection = (FLexUIRenderSection_PostProcess*)InSrcSection;
				auto NewSectionProxy = new FLexUIPostProcessSectionProxy();
				NewSectionProxy->PostProcessRenderProxy = SrcSection->PostProcessVisualObject->GetRenderProxy();

				// Copy info
				NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
				SrcSection->RenderProxy = NewSectionProxy;

				return NewSectionProxy;
			}
			break;
		case ELexUIRenderSectionType::ChildCanvas:
			{
				auto SrcSection = (FLexUIRenderSection_ChildCanvas*)InSrcSection;
				auto NewSectionProxy = new FLexUIChildCanvasSectionProxy();
				auto& ChildCanvasMeshItem = SrcSection->ChildCanvasMeshComponent;
				NewSectionProxy->PrimitiveComponentID = ChildCanvasMeshItem->GetPrimitiveSceneId();
				if (ChildCanvasMeshItem->SceneProxy != nullptr)
				{
					NewSectionProxy->ChildCanvasSceneProxy = (FLexUIRenderSceneProxy*)ChildCanvasMeshItem->SceneProxy;
					NewSectionProxy->ChildCanvasSceneProxy->ParentSceneProxy = this;
				}

				// Copy info
				NewSectionProxy->SectionRenderPriority = SrcSection->RenderPriority;
				SrcSection->RenderProxy = NewSectionProxy;

				return NewSectionProxy;
			}
			break;
		}
		check(0);
		return nullptr;
	}
	void SetChildCanvasSectionData_RenderThread(FPrimitiveComponentId CompID, FLexUIRenderSceneProxy* SceneProxy)
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			if (Section == nullptr)continue;
			if (Section->Type == ELexUIRenderSectionType::ChildCanvas)
			{
				auto ChildCanvasSection = (FLexUIChildCanvasSectionProxy*)Section;
				if (ChildCanvasSection->PrimitiveComponentID == CompID
					&& (ChildCanvasSection->ChildCanvasSceneProxy == nullptr || ChildCanvasSection->ChildCanvasSceneProxy->ParentSceneProxy == this)//check this because ParentSceneProxy could be a new one
					)
				{
					ChildCanvasSection->ChildCanvasSceneProxy = SceneProxy;
				}
			}
		}
	}
	void ClearChildCanvasSectionData_RenderThread(FLexUIRenderSceneProxy* SceneProxy)
	{
		for (int i = 0; i < SectionArray.Num(); i++)
		{
			auto Section = SectionArray[i];
			if (Section == nullptr)continue;
			if (Section->Type == ELexUIRenderSectionType::ChildCanvas)
			{
				auto ChildCanvasSection = (FLexUIChildCanvasSectionProxy*)Section;
				if (ChildCanvasSection->ChildCanvasSceneProxy == SceneProxy)//child could already get new proxy, so need to check it
				{
					ChildCanvasSection->ChildCanvasSceneProxy = nullptr;
					return;
				}
			}
		}
	}

	void EnableSectionData_RenderThread(FLexUIRenderSectionProxy* Section)
	{
		Section->bCanRender = true;
	}
	void DisableSectionData_RenderThread(FLexUIRenderSectionProxy* Section)
	{
		Section->Disable();
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
	void SetMeshSectionMaterial_RenderThread(FLexUIRenderSectionProxy* Section, UMaterialInterface* Material)
	{
		((FLexUIMeshSectionProxy*)Section)->Material = Material;
	}

	void SetRenderSectionRenderPriority_RenderThread(FLexUIRenderSectionProxy* Section, int32 NewPriority)
	{
		Section->SectionRenderPriority = NewPriority;
		bNeedToSortRenderSections = true;
	}

	void SortMeshSectionRenderPriority_RenderThread()
	{
		Algo::Sort(SectionArray, [](const FLexUIRenderSectionProxy* A, const FLexUIRenderSectionProxy* B) {
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

	virtual ~FLexUIRenderSceneProxy()
	{
		for(auto Section : SectionArray)
		{
			if (Section != nullptr)
			{
				switch (Section->Type)
				{
				case ELexUIRenderSectionType::ChildCanvas:
					auto ChildCanvasSection = (FLexUIChildCanvasSectionProxy*)Section;
					if (ChildCanvasSection->ChildCanvasSceneProxy)
					{
						if (ChildCanvasSection->ChildCanvasSceneProxy->ParentSceneProxy == this)//child canvas's ParentSceneProxy could already be new one, so check it
						{
							ChildCanvasSection->ChildCanvasSceneProxy->ParentSceneProxy = nullptr;
						}
					}
					break;
				}
				delete Section;
			}
		}
		SectionArray.Empty();
		if (ParentSceneProxy)
		{
			ParentSceneProxy->ClearChildCanvasSectionData_RenderThread(this);
		}
		if (LexUIRenderer.IsValid())
		{
			if (bIsLexUIRenderToWorld)
			{
				LexUIRenderer.Pin()->RemoveWorldSpacePrimitive_RenderThread(RenderCanvasPtr, this);
			}
			else
			{
				LexUIRenderer.Pin()->RemoveScreenSpacePrimitive_RenderThread(this);
			}
			LexUIRenderer.Reset();
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FRHICommandListImmediate& RHICmdList
		, FLexUIMeshVertex* MeshVertexData, const int32& NumVerts
		, FLexUIMeshIndexBufferType* MeshIndexData, const uint32& IndexDataLength, const int32& NumTriangles
		, bool RequireNormalAndTangent
		, FLexUIMeshSectionProxy* Section)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionRT);

		check(IsInRenderingThread());

		// Check it references a valid section
		check(Section != nullptr);
		Section->ValidVerticesCount = NumVerts;
		Section->bCanRender = true;
		//vertex buffer
		if (bIsSupportLexUIRenderer)
		{
			uint32 VertexDataLength = NumVerts * sizeof(FLexUIMeshVertex);
			void* VertexBufferData = RHICmdList.LockBuffer(Section->LexUIVertexBuffers.VertexBufferRHI, 0, VertexDataLength, RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, MeshVertexData, VertexDataLength);
			RHICmdList.UnlockBuffer(Section->LexUIVertexBuffers.VertexBufferRHI);
		}
		if(bIsSupportUERenderer)
		{
#if 0//looks like these code is not necessary
			for (int i = 0; i < NumVerts; i++)
			{
				auto& LexUIVert = MeshVertexData[i];
				Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = LexUIVert.Position;
				Section->VertexBuffers.ColorVertexBuffer.VertexColor(i) = LexUIVert.Color;
				if (RequireNormalAndTangent)
					Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, LexUIVert.TangentX.ToFVector3f(), LexUIVert.GetTangentY(), LexUIVert.TangentZ.ToFVector3f());
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, LexUIVert.TextureCoordinate[0]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 1, LexUIVert.TextureCoordinate[1]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 2, LexUIVert.TextureCoordinate[2]);
				Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 3, LexUIVert.TextureCoordinate[3]);
			}
#endif

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
		// Lock index buffer
		auto IndexBufferData = RHICmdList.LockBuffer(Section->IndexBuffer.IndexBufferRHI, 0, IndexDataLength, RLM_WriteOnly);
		FMemory::Memcpy(IndexBufferData, (void*)MeshIndexData, IndexDataLength);
		RHICmdList.UnlockBuffer(Section->IndexBuffer.IndexBufferRHI);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!bIsSupportUERenderer) return;
		if (ParentSceneProxy != nullptr && !bIsRenderFromParent)return;
		if (bNeedToSortRenderSections)
		{
			auto LexUIMeshSceneProxy = const_cast<FLexUIRenderSceneProxy*>(this);
			LexUIMeshSceneProxy->bNeedToSortRenderSections = false;
			LexUIMeshSceneProxy->SortMeshSectionRenderPriority_RenderThread();
		}
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = NULL;
		if (bWireframe)
		{
			WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
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
			case ELexUIRenderSectionType::Mesh:
			{
				auto Section = (FLexUIMeshSectionProxy*)RenderSection;
				FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

				// For each view..
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];
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
			case ELexUIRenderSectionType::PostProcess:
				break;
			case ELexUIRenderSectionType::ChildCanvas:
			{
				auto Section = (FLexUIChildCanvasSectionProxy*)RenderSection;
				auto ChildSceneProxy = Section->ChildCanvasSceneProxy;
				if (ChildSceneProxy != nullptr)
				{
					ChildSceneProxy->bIsRenderFromParent = true;
					ChildSceneProxy->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector);
					ChildSceneProxy->bIsRenderFromParent = false;
				}
			}
			break;
			}
		}
	}

	//begin ILexUIRendererPrimitive interface
	virtual FVector3f GetWorldPositionForSortTranslucent()const override 
	{
		return (FVector3f)(GetLocalToWorld().GetOrigin()); 
	}
	virtual void CollectRenderData(TArray<FLexUIPrimitiveDataContainer>& OutRenderData, float CurrentWorldTime) override
	{
		if (ParentSceneProxy != nullptr)return;
		CollectRenderData_Implement(OutRenderData, CurrentWorldTime);
	}
	virtual void GetMeshElements(const FSceneViewFamily& ViewFamily, FMeshElementCollector* Collector, const FLexUIPrimitiveDataContainer& PrimitiveData, TArray<FLexUIMeshBatchContainer>& ResultArray) override
	{
		if (!bIsSupportLexUIRenderer)return;
		// Set up wireframe material (if needed)
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		FMaterialRenderProxy* WireframeMaterialInstance = NULL;
		if (bWireframe)
		{
			WireframeMaterialInstance = GEngine->WireframeMaterial->GetRenderProxy();
		}

		for (int i = 0; i < PrimitiveData.Sections.Num(); i++)
		{
			auto SectionData = PrimitiveData.Sections[i];
			auto RenderSection = (FLexUIRenderSectionProxy*)SectionData.SectionPointer;

			auto Section = (FLexUIMeshSectionProxy*)RenderSection;
			FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialInstance : Section->Material->GetRenderProxy();

			// Draw the mesh.
			FMeshBatch Mesh;
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &Section->IndexBuffer;
			BatchElement.PrimitiveIdMode = PrimID_ForceZero;
			Mesh.bWireframe = bWireframe;
			Mesh.MaterialRenderProxy = MaterialProxy;

			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector->AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(Collector->GetRHICommandList(), GetLocalToWorld(), GetLocalToWorld(), GetBounds(), GetLocalBounds(), false, false, false);
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

			FLexUIMeshBatchContainer MeshBatchContainer;
			MeshBatchContainer.Mesh = Mesh;
			MeshBatchContainer.VertexBufferRHI = Section->LexUIVertexBuffers.VertexBufferRHI;
			MeshBatchContainer.NumVerts = Section->ValidVerticesCount;
			ResultArray.Add(MeshBatchContainer);
		}
	}

	virtual FLexVisualPostProcessRenderProxy* GetPostProcessElement(const void* SectionPtr)const override
	{
		auto RenderSection = (FLexUIRenderSectionProxy*)SectionPtr;
		check(RenderSection->Type == ELexUIRenderSectionType::PostProcess);
		return ((FLexUIPostProcessSectionProxy*)RenderSection)->PostProcessRenderProxy;
	}
	virtual int GetRenderPriority()const override
	{
		return RenderPriority;
	}
	virtual bool CanRender()const override
	{
		return ParentSceneProxy == nullptr && SectionArray.Num() > 0;
	}
	virtual FPrimitiveComponentId GetPrimitiveComponentId() const override 
	{
		return FPrimitiveSceneProxy::GetPrimitiveComponentId();
	}
	virtual FBoxSphereBounds GetWorldBounds()const override { return FPrimitiveSceneProxy::GetBounds(); }
	//end ILexUIRendererPrimitive interface
	void CollectRenderData_Implement(TArray<FLexUIPrimitiveDataContainer>& OutRenderDataArray, float CurrentWorldTime)
	{
		if (SectionArray.Num() <= 0)return;
		if (bNeedToSortRenderSections)
		{
			bNeedToSortRenderSections = false;
			this->SortMeshSectionRenderPriority_RenderThread();
		}
		*CanvasLastRenderTime = CurrentWorldTime;

		if (SectionArray[0] == nullptr)return;
		auto PrevRenderSectionType = SectionArray[0]->Type;
		auto PrevPrimitiveType = PrevRenderSectionType == ELexUIRenderSectionType::PostProcess ? ELexUIRendererPrimitiveType::PostProcess : ELexUIRendererPrimitiveType::Mesh;
		FLexUIPrimitiveDataContainer CurrentRenderData;
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
				CurrentRenderData = FLexUIPrimitiveDataContainer();
				CurrentRenderData.Primitive = this;
				auto ItemPrimitiveType = RenderSection->Type == ELexUIRenderSectionType::PostProcess ? ELexUIRendererPrimitiveType::PostProcess : ELexUIRendererPrimitiveType::Mesh;
				CurrentRenderData.Type = ItemPrimitiveType;
			}

			switch (RenderSection->Type)
			{
			case ELexUIRenderSectionType::Mesh:
				{
					FLexUIPrimitiveSectionDataContainer SectionData;
					SectionData.SectionPointer = RenderSection;
					CurrentRenderData.Sections.Add(SectionData);
				}
				break;
			case ELexUIRenderSectionType::PostProcess:
				{
					auto Section = (FLexUIPostProcessSectionProxy*)RenderSection;
					if (Section->PostProcessRenderProxy->CanRender())
					{
						FLexUIPrimitiveSectionDataContainer SectionData;
						SectionData.SectionPointer = RenderSection;
						CurrentRenderData.Sections.Add(SectionData);
					}
				}
				break;
			case ELexUIRenderSectionType::ChildCanvas:
				{
					auto Section = (FLexUIChildCanvasSectionProxy*)RenderSection;
					auto ChildSceneProxy = Section->ChildCanvasSceneProxy;
					if (ChildSceneProxy != nullptr)
					{
						ChildSceneProxy->CollectRenderData_Implement(OutRenderDataArray, CurrentWorldTime);
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
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const
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

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}

	void SetParentSceneProxy_RenderThread(FLexUIRenderSceneProxy* InParentSceneProxy)
	{
		ParentSceneProxy = InParentSceneProxy;
	}
private:
	TArray<FLexUIRenderSectionProxy*> SectionArray;

	FMaterialRelevance MaterialRelevance;
	int32 RenderPriority = 0;
	TWeakPtr<FLexUIRenderer, ESPMode::ThreadSafe> LexUIRenderer;
	bool bIsSupportLexUIRenderer = false;
	bool bIsSupportUERenderer = true;
	bool bIsLexUIRenderToWorld = false;
	bool bNeedToSortRenderSections = true;
	ULexCanvas* RenderCanvasPtr = nullptr;
#if !UE_BUILD_SHIPPING
	FName DebugName;
	static uint32 DebugNameIndex;
#endif
	bool bIsRenderFromParent = false;
	/** If have parent then render in parent */
	FLexUIRenderSceneProxy* ParentSceneProxy = nullptr;
	/**
	 * This is a pointer to LexCanvas's LastRenderTime.
	 * Why it is safe to use? Check PrimitiveSceneInfo.h OwnerLastRenderTime
	 */
	float* CanvasLastRenderTime = nullptr;
};
#if !UE_BUILD_SHIPPING
uint32 FLexUIRenderSceneProxy::DebugNameIndex = 0;
#endif



void FLexUIRenderSection_Mesh::UpdateSectionBox(const FTransform& LocalToWorld)
{
	BoundingBox = BoundingBox.TransformBy(LocalToWorld);
}

void FLexUIRenderSection_Mesh::ClearBeforePool()
{
	material = nullptr;
}

void FLexUIRenderSection_PostProcess::UpdateSectionBox(const FTransform& LocalToWorld)
{
	BoundingBox = BoundingBox.TransformBy(LocalToWorld);
}
void FLexUIRenderSection_PostProcess::ClearBeforePool()
{
	PostProcessVisualObject = nullptr;
}
void FLexUIRenderSection_ChildCanvas::UpdateSectionBox(const FTransform& LocalToWorld)
{
	BoundingBox = ChildCanvasMeshComponent->Bounds.GetBox();
}

void FLexUIRenderSection_ChildCanvas::ClearBeforePool()
{
	ChildCanvasMeshComponent = nullptr;
}


ULexUIMeshComponent::ULexUIMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	this->bCanEverAffectNavigation = false;
}

TSharedPtr<FLexUIRenderSection> ULexUIMeshComponent::SetupRenderSection(ELexUIRenderSectionType InType, FLexUIDrawCall* InDrawCallData)
{
	auto GetRenderSectionFromPool = [&]()
	{
		for (int i = 0; i < RenderSectionPool.Num(); i++)
		{
			auto RenderSection = RenderSectionPool[i];
			if (RenderSection->Type == InType)
			{
				RenderSectionPool.RemoveAt(i);
				return RenderSection;
			}
		}
		return TSharedPtr<FLexUIRenderSection>(nullptr);
	};

	TSharedPtr<FLexUIRenderSection> RenderSection = GetRenderSectionFromPool();
	if (!RenderSection)
	{
		switch (InType)
		{
		case ELexUIRenderSectionType::Mesh:
			RenderSection = MakeShared<FLexUIRenderSection_Mesh>();
			break;
		case ELexUIRenderSectionType::PostProcess:
			RenderSection = MakeShared<FLexUIRenderSection_PostProcess>();
			break;
		case ELexUIRenderSectionType::ChildCanvas:
			RenderSection = MakeShared<FLexUIRenderSection_ChildCanvas>();
			break;
		}
	}
	
	switch (InType)
	{
	case ELexUIRenderSectionType::Mesh:
		{
			auto MeshSectionPtr = (FLexUIRenderSection_Mesh*)RenderSection.Get();
			bool bNeedExpandMeshSection = MeshSectionPtr->vertices.Num() < InDrawCallData->CombinedBatchMeshGeometryVertices.Num()
            				|| MeshSectionPtr->triangleIndices.Num() < InDrawCallData->CombinedBatchMeshGeometryTriangles.Num();
			MeshSectionPtr->vertices = InDrawCallData->CombinedBatchMeshGeometryVertices;
			MeshSectionPtr->triangleIndices = InDrawCallData->CombinedBatchMeshGeometryTriangles;
			MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds;
			if (MeshSectionPtr->RenderProxy)//if we have valid render-proxy then recreate date or update data
			{
				if (bNeedExpandMeshSection)
				{
					ExpandMeshSectionRenderData(RenderSection);
				}
				else
				{
					UpdateMeshSectionRenderData(RenderSection, RenderCanvas->GetActualRequireNormalAndTangent());
				}
			}
			else//no valid render-proxy, because it is newly created
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;
					ThisSceneProxy->AddSectionData(MeshSectionPtr);
				}
			}
		}
		break;
	case ELexUIRenderSectionType::PostProcess:
		{
			auto PostProcessSectionPtr = (FLexUIRenderSection_PostProcess*)RenderSection.Get();
			PostProcessSectionPtr->PostProcessVisualObject = InDrawCallData->PostProcessVisualObject;
			auto PostProcessVisualObject = InDrawCallData->PostProcessVisualObject;
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
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
					auto RenderProxy = PostProcessSectionPtr->PostProcessVisualObject->GetRenderProxy();
					ThisSceneProxy->UpdatePostProcessSection(PostProcessSectionPtr, RenderProxy);
				}
			}
			else
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;
					ThisSceneProxy->AddSectionData(PostProcessSectionPtr);
				}
			}
		}
		break;
	case ELexUIRenderSectionType::ChildCanvas:
		{
			auto ChildCanvasSectionPtr = (FLexUIRenderSection_ChildCanvas*)RenderSection.Get();
			ChildCanvasSectionPtr->ChildCanvasMeshComponent = InDrawCallData->ChildCanvas->GetUIMesh();
			ChildCanvasSectionPtr->ChildCanvasMeshComponent->SetParentCanvasMeshComp(InDrawCallData->ChildCanvas->GetUIMesh());
			auto ChildCanvasMeshComp = ChildCanvasSectionPtr->ChildCanvasMeshComponent;
			ChildCanvasMeshComp->OnSceneProxyCreated.AddWeakLambda(this, [this](ULexUIMeshComponent* InMesh, FLexUIRenderSceneProxy* InSceneProxy) {
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
					ENQUEUE_RENDER_COMMAND(FLexUIRenderSceneProxy_ReassignChildCanvasSectionData)(
						[ThisSceneProxy, CompID = InMesh->GetPrimitiveSceneId(), InSceneProxy](FRHICommandListImmediate& RHICmdList) {
							ThisSceneProxy->SetChildCanvasSectionData_RenderThread(CompID, InSceneProxy);
						});
				}
				});
			if (ChildCanvasSectionPtr->RenderProxy)
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;//SceneProxy could change before the RENDER_COMMAND execute, so do necessary check in SetChildCanvasSectionData_RenderThread
					ThisSceneProxy->UpdateChildCanvasSection(ChildCanvasSectionPtr, InDrawCallData->ChildCanvas->GetUIMesh());
				}
			}
			else
			{
				if (this->SceneProxy != nullptr)
				{
					auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;
					ThisSceneProxy->AddSectionData(ChildCanvasSectionPtr);
				}
			}
		}
		break;
	}

	RenderSectionArray.Add(RenderSection);
	return RenderSection;
}

void ULexUIMeshComponent::UpdateMeshSection(int Index, FLexUIDrawCall* InDrawCallData)
{
	auto& RenderSection = RenderSectionArray[Index];
	
	auto MeshSectionPtr = (FLexUIRenderSection_Mesh*)RenderSection.Get();
	if (MeshSectionPtr->RenderProxy)//if we have valid render-proxy then recreate date or update data
	{
		FMemory::Memcpy(MeshSectionPtr->vertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.GetData(), InDrawCallData->CombinedBatchMeshGeometryVertices.Num() * sizeof(FLexUIMeshVertex));
		FMemory::Memcpy(MeshSectionPtr->triangleIndices.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.GetData(), InDrawCallData->CombinedBatchMeshGeometryTriangles.Num() * sizeof(FLexUIMeshIndexBufferType));
		UpdateMeshSectionRenderData(RenderSection, RenderCanvas->GetActualRequireNormalAndTangent());
	}
	else//no valid render-proxy, because it is newly created
	{
		MeshSectionPtr->vertices = InDrawCallData->CombinedBatchMeshGeometryVertices;
		MeshSectionPtr->triangleIndices = InDrawCallData->CombinedBatchMeshGeometryTriangles;
		MeshSectionPtr->BoundingBox = InDrawCallData->CombinedBounds;
		if (this->SceneProxy != nullptr)
		{
			auto ThisSceneProxy = (FLexUIRenderSceneProxy*)this->SceneProxy;
			ThisSceneProxy->AddSectionData(MeshSectionPtr);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("LexUIMesh UpdateMeshSection_GT"), STAT_UpdateMeshSectionGT, STATGROUP_LGUI);
void ULexUIMeshComponent::UpdateMeshSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection, bool InRequireNormalAndTangent)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateMeshSectionGT);
	InRenderSection->UpdateSectionBox(GetComponentTransform());
	if (SceneProxy)
	{
		check(InRenderSection->Type == ELexUIRenderSectionType::Mesh);
		auto MeshSection = (FLexUIRenderSection_Mesh*)InRenderSection.Get();

		struct UpdateMeshSectionDataStruct
		{
			TArray<FLexUIMeshVertex> VertexBufferData;
			int32 NumVerts;
			int32 NumTriangles;
			TArray<FLexUIMeshIndexBufferType> IndexBufferData;
			uint32 IndexBufferDataLength;
			bool RequireNormalAndTangent;
			FLexUIMeshSectionProxy* Section;
			FLexUIRenderSceneProxy* SceneProxy;
		};
		UpdateMeshSectionDataStruct* UpdateData = new UpdateMeshSectionDataStruct();
		UpdateData->Section = (FLexUIMeshSectionProxy*)MeshSection->RenderProxy;
		//vertex data
		const int32 NumVerts = MeshSection->vertices.Num();
		UpdateData->VertexBufferData.AddUninitialized(NumVerts);
		FMemory::Memcpy(UpdateData->VertexBufferData.GetData(), MeshSection->vertices.GetData(), NumVerts * sizeof(FLexUIMeshVertex));
		UpdateData->NumVerts = NumVerts;
		UpdateData->SceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
		const int32 NumIndices = MeshSection->triangleIndices.Num();
		const uint32 IndexBufferDataLength = NumIndices * sizeof(FLexUIMeshIndexBufferType);
		UpdateData->IndexBufferData.AddUninitialized(NumIndices);
		UpdateData->NumTriangles = NumIndices / 3;
		FMemory::Memcpy(UpdateData->IndexBufferData.GetData(), MeshSection->triangleIndices.GetData(), IndexBufferDataLength);
		UpdateData->IndexBufferDataLength = IndexBufferDataLength;
		UpdateData->RequireNormalAndTangent = InRequireNormalAndTangent;
		//update data
		ENQUEUE_RENDER_COMMAND(FLexUIMeshUpdate)(
			[UpdateData](FRHICommandListImmediate& RHICmdList)
			{
				UpdateData->SceneProxy->UpdateSection_RenderThread(
					RHICmdList
					, UpdateData->VertexBufferData.GetData()
					, UpdateData->NumVerts
					, UpdateData->IndexBufferData.GetData()
					, UpdateData->IndexBufferDataLength
					, UpdateData->NumTriangles
					, UpdateData->RequireNormalAndTangent
					, UpdateData->Section
				);
				delete UpdateData;
			});
	}
}

void ULexUIMeshComponent::ExpandMeshSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection)
{
	if (SceneProxy)
	{
		check(InRenderSection->Type == ELexUIRenderSectionType::Mesh);
		auto ThisSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
		if (InRenderSection->RenderProxy == nullptr)
		{
			ThisSceneProxy->CreateSectionData(InRenderSection.Get());
		}
		else
		{
			ThisSceneProxy->RecreateSectionData(InRenderSection.Get());
		}
	}
}

void ULexUIMeshComponent::PoolRenderSection(TSharedPtr<FLexUIRenderSection> InRenderSection)
{
	if (SceneProxy)
	{
		if (auto RenderSectionProxy = InRenderSection->RenderProxy)
		{
			auto LexUIMeshSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
			ENQUEUE_RENDER_COMMAND(FLexUIMeshSectionProxy_PoolAllSectionData)(
					[LexUIMeshSceneProxy, RenderSectionProxy](FRHICommandListImmediate& RHICmdList) {
						LexUIMeshSceneProxy->DisableSectionData_RenderThread(RenderSectionProxy);
					});
		}
	}

	if (InRenderSection->Type == ELexUIRenderSectionType::ChildCanvas)
	{
		auto ChildCanvasSection = (FLexUIRenderSection_ChildCanvas*)InRenderSection.Get();
		ChildCanvasSection->ChildCanvasMeshComponent->ClearParentCanvasMeshComp(this);
	}

	RenderSectionArray.Remove(InRenderSection);
	InRenderSection->ClearBeforePool();
	RenderSectionPool.Add(InRenderSection);
}

void ULexUIMeshComponent::PoolAllRenderSection()
{
	auto LexUIMeshSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
	for (auto& RenderSection : RenderSectionArray)
	{
		if (LexUIMeshSceneProxy)
		{
			ENQUEUE_RENDER_COMMAND(FLexUIMeshSectionProxy_PoolAllSectionData)(
				[LexUIMeshSceneProxy](FRHICommandListImmediate& RHICmdList) {
					LexUIMeshSceneProxy->PoolAllSectionData_RenderThread();
				});
		}

		if (RenderSection->Type == ELexUIRenderSectionType::ChildCanvas)
		{
			auto ChildCanvasSection = (FLexUIRenderSection_ChildCanvas*)RenderSection.Get();
			ChildCanvasSection->ChildCanvasMeshComponent->ClearParentCanvasMeshComp(this);
		}

		RenderSection->ClearBeforePool();
	}
	RenderSectionPool.Append(RenderSectionArray);
	RenderSectionArray.Reset();
}

void ULexUIMeshComponent::SetRenderSectionRenderPriority(int32 InSectionIndex, int32 InSortPriority)
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	RenderSection->RenderPriority = InSortPriority;
	if (SceneProxy)
	{
		auto LexUIMeshSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
		auto RenderProxy = RenderSection->RenderProxy;
		if (RenderProxy != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(FLexUIMeshSectionProxy_SetMeshSectionRenderPriority)(
				[LexUIMeshSceneProxy, RenderProxy, InSortPriority](FRHICommandListImmediate& RHICmdList) {
					LexUIMeshSceneProxy->SetRenderSectionRenderPriority_RenderThread(RenderProxy, InSortPriority);
				});
		}
	}
}

void ULexUIMeshComponent::SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial)
{
	auto RenderSection = RenderSectionArray[InSectionIndex];
	check(RenderSection->Type == ELexUIRenderSectionType::Mesh);
	((FLexUIRenderSection_Mesh*)RenderSection.Get())->material = InMaterial;
	if (SceneProxy)
	{
		auto LexUIMeshSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
		auto RenderProxy = RenderSection->RenderProxy;
		if (RenderProxy != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(FLexUIMeshSectionProxy_SetMeshSectionMaterial)(
				[LexUIMeshSceneProxy, RenderProxy, InMaterial](FRHICommandListImmediate& RHICmdList) {
					LexUIMeshSceneProxy->SetMeshSectionMaterial_RenderThread(RenderProxy, InMaterial);
				});
		}
	}
}

void ULexUIMeshComponent::VerifyMaterials()
{
	this->EmptyOverrideMaterials();

	int MatIndex = 0;
	for (auto& RenderSectionItem : RenderSectionArray)
	{
		switch (RenderSectionItem->Type)
		{
		case ELexUIRenderSectionType::Mesh:
		{
			auto MeshSection = (FLexUIRenderSection_Mesh*)RenderSectionItem.Get();
			this->SetMaterial(MatIndex++, MeshSection->material);
		}
		break;
		case ELexUIRenderSectionType::ChildCanvas:
		{
			auto ChildCanvasSection = (FLexUIRenderSection_ChildCanvas*)RenderSectionItem.Get();
			for (auto ChildMat : ChildCanvasSection->ChildCanvasMeshComponent->OverrideMaterials)
			{
				this->SetMaterial(MatIndex++, ChildMat);
			}
		}
		break;
		}
	}
}

void ULexUIMeshComponent::SetParentCanvasMeshComp(ULexUIMeshComponent* InMesh)
{
	if (ParentCanvasMeshComp != InMesh)
	{
		ParentCanvasMeshComp = InMesh;
	}
}
void ULexUIMeshComponent::ClearParentCanvasMeshComp(ULexUIMeshComponent* InMesh)
{
	if (ParentCanvasMeshComp == InMesh)//check, incase parent already change
	{
		ParentCanvasMeshComp = nullptr;
	}
}

void ULexUIMeshComponent::SetUITranslucentSortPriority(int32 NewTranslucentSortPriority)
{
	UPrimitiveComponent::SetTranslucentSortPriority(NewTranslucentSortPriority);
	if (SceneProxy)
	{
		auto LexUIMeshSceneProxy = (FLexUIRenderSceneProxy*)SceneProxy;
		ENQUEUE_RENDER_COMMAND(FLexUIMesh_SetUITranslucentSortPriority)(
			[LexUIMeshSceneProxy, NewTranslucentSortPriority](FRHICommandListImmediate& RHICmdList)
		{
			LexUIMeshSceneProxy->SetRenderPriority_RenderThread(NewTranslucentSortPriority);
		}
		);
	}
}

void ULexUIMeshComponent::UpdateChildCanvasSectionBox()
{
	struct LOCAL
	{
		static void UpdateChildCanvasSectionBox_Recursive(const TArray<TSharedPtr<FLexUIRenderSection>>& InRenderSections)
		{
			for (auto& RenderSectionItem : InRenderSections)
			{
				if (RenderSectionItem->Type == ELexUIRenderSectionType::ChildCanvas)
				{
					auto ChildCanvasSection = (FLexUIRenderSection_ChildCanvas*)RenderSectionItem.Get();
					if (ChildCanvasSection->ChildCanvasMeshComponent != nullptr)
					{
						UpdateChildCanvasSectionBox_Recursive(ChildCanvasSection->ChildCanvasMeshComponent->RenderSectionArray);
						ChildCanvasSection->UpdateSectionBox(ChildCanvasSection->ChildCanvasMeshComponent->GetComponentToWorld());
					}
				}
			}
		}
	};
	LOCAL::UpdateChildCanvasSectionBox_Recursive(RenderSectionArray);
}

void ULexUIMeshComponent::UpdateLocalBounds()
{
	UpdateBounds();// Update global bounds		
	MarkRenderTransformDirty();// Need to send to render thread
}

struct FLexUIPrimitiveComponentIdTemporaryModifier
{
	ULexUIMeshComponent* Comp = nullptr;
	FPrimitiveComponentId OriginId;
	FLexUIPrimitiveComponentIdTemporaryModifier(ULexUIMeshComponent* InComp, FPrimitiveComponentId InNewId)
	{
		Comp = InComp;
		OriginId = Comp->GetPrimitiveSceneId();
		Comp->GetPrimitiveSceneId() = InNewId;
	}
	~FLexUIPrimitiveComponentIdTemporaryModifier()
	{
		Comp->GetPrimitiveSceneId() = OriginId;
	}
};

DECLARE_CYCLE_STAT(TEXT("LexUIMesh CreateSceneProxy"), STAT_LexUIMesh_CreateSceneProxy, STATGROUP_LGUI);
FPrimitiveSceneProxy* ULexUIMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_LexUIMesh_CreateSceneProxy);
	//clear section data
	RenderSectionPool.Reset();

	FLexUIRenderSceneProxy* Proxy = NULL;
	if (RenderSectionArray.Num() > 0)
	{
		//change component id to RootCanvasUIMesh's component id, so when check visibility it will use RootCanvas's id
		{
			//turns out not work as I want, so comment the codes
			//auto RootCanvasUIMesh = RenderCanvas->GetRootCanvas()->GetUIMesh();
			//FLexUIPrimitiveComponentIdTemporaryModifier TempModifier(this, RootCanvasUIMesh->ComponentId);
			Proxy = new FLexUIRenderSceneProxy(this, RenderCanvas.Get(), RenderCanvas->GetActualSortOrder(), ParentCanvasMeshComp.IsValid() ? (FLexUIRenderSceneProxy*)ParentCanvasMeshComp->SceneProxy : nullptr);
		}
		OnSceneProxyCreated.Broadcast(this, Proxy);
	}
	return Proxy;
}

void ULexUIMeshComponent::SetRenderCanvas(ULexCanvas* InCanvas)
{
	RenderCanvas = InCanvas;
}
void ULexUIMeshComponent::SetSupportLexUIRenderer(bool InSupportOrNot, TWeakPtr<FLexUIRenderer, ESPMode::ThreadSafe> InLexUIRenderer, bool InIsRenderToWorld)
{
	if (InSupportOrNot)
	{
		LexUIRenderer = InLexUIRenderer;
		bIsLexUIRenderToWorld = InIsRenderToWorld;
	}
	else
	{
		LexUIRenderer.Reset();
	}
}

void ULexUIMeshComponent::SetSupportUERenderer(bool InSupportOrNot)
{
	bIsSupportUERenderer = InSupportOrNot;
}
void ULexUIMeshComponent::ClearRenderData()
{
	MarkRenderStateDirty();//mark dirty to recreate SceneProxy
	RenderSectionArray.Reset();
	RenderSectionArray.Reset();
	OnSceneProxyCreated.Clear();
	LexUIRenderer = nullptr;
}

int32 ULexUIMeshComponent::GetNumMaterials() const
{
	int Result = 0;
	for (auto& RenderSectionItem : RenderSectionArray)
	{
		switch (RenderSectionItem->Type)
		{
		case ELexUIRenderSectionType::Mesh:
			Result++;
			break;
		case ELexUIRenderSectionType::ChildCanvas:
			auto ChildCanvasSection = (FLexUIRenderSection_ChildCanvas*)RenderSectionItem.Get();
			Result += ChildCanvasSection->ChildCanvasMeshComponent->GetNumMaterials();
			break;
		}
	}
	return Result;
}

FBoxSphereBounds ULexUIMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
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
		case ELexUIRenderSectionType::Mesh:
		{
			ResultBox += RenderSection->BoundingBox;
		}
		break;
		case ELexUIRenderSectionType::PostProcess:
		{
			if (LexUIRenderer.IsValid())
			{
				ResultBox += RenderSection->BoundingBox;
			}
		}
		break;
		case ELexUIRenderSectionType::ChildCanvas:
		{
			ResultBox += RenderSection->BoundingBox;
		}
		break;
		}
	}

	return FBoxSphereBounds(ResultBox);
}
#undef LOCTEXT_NAMESPACE
