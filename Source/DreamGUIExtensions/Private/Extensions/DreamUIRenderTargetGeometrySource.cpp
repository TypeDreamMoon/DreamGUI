// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Utils/DreamUIUtils.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "DreamTweenBPLibrary.h"
#include "Materials/MaterialRenderProxy.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "RayTracingInstance.h"
#include "RayTracingGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamWidgetPresenterComponentBase.h"

#define LOCTEXT_NAMESPACE "DreamGUIRenderTargetGeometrySource"

#define MIN_SEG 4
#define MAX_SEG 32

class FDreamUIRenderTargetGeometrySourceMeshProxySection
{
public:
	/** Vertex buffer for this section */
	FStaticMeshVertexBuffers VertexBuffers;
	/** Index buffer for this section */
	FDynamicMeshIndexBuffer16 IndexBuffer;
	/** Vertex factory for this section */
	FLocalVertexFactory VertexFactory;

#if RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif

	FDreamUIRenderTargetGeometrySourceMeshProxySection(ERHIFeatureLevel::Type InFeatureLevel)
		: VertexFactory(InFeatureLevel, "FDreamUIRenderTargetGeometrySourceMeshProxySection")
	{}
};

class FDreamUIRenderTargetGeometrySource_SceneProxy : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	FDreamUIRenderTargetGeometrySource_SceneProxy(UDreamUIRenderTargetGeometrySource* Component)
		: FPrimitiveSceneProxy(Component)
		, GeometryMode(Component->GetGeometryMode())
	{
		// Everything the draw needs of the component is read here, on the game thread. The proxy used to keep the
		// render target and the material instance themselves and read them in GetDynamicMeshElements, on the render
		// thread or one of its tasks, whatever the game thread had done to them since. A material instance's render
		// proxy lives as long as the instance, which the component holds past this proxy; and whether there is a
		// target to show is decided now, as it was on every draw before -- a new target rebuilds this proxy.
		const UMaterialInstanceDynamic* MaterialInstance = Component->GetMaterialInstance();
		SurfaceMaterialProxy = MaterialInstance->GetRenderProxy();
		const UTextureRenderTarget2D* Target = Component->GetRenderTarget();
		bHasRenderTarget = Target != nullptr && Target->GetResource() != nullptr;
		MaterialRelevance = MaterialInstance->GetRelevance_Concurrent(GetScene().GetShaderPlatform());

		Section = new FDreamUIRenderTargetGeometrySourceMeshProxySection(GetScene().GetFeatureLevel());

		// Copy index buffer
		Section->IndexBuffer.Indices = Component->Triangles;

		Section->VertexBuffers.InitFromDynamicVertex(&Section->VertexFactory, Component->Vertices, 4);

		// Enqueue initialization of render resource
		BeginInitResource(&Section->VertexBuffers.PositionVertexBuffer);
		BeginInitResource(&Section->VertexBuffers.StaticMeshVertexBuffer);
		BeginInitResource(&Section->VertexBuffers.ColorVertexBuffer);
		BeginInitResource(&Section->IndexBuffer);
		BeginInitResource(&Section->VertexFactory);

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			ENQUEUE_RENDER_COMMAND(InitProceduralMeshRayTracingGeometry)(
				[this, DebugName = Component->GetFName()](FRHICommandListImmediate& RHICmdList)
				{
					FRayTracingGeometryInitializer Initializer;
					Initializer.DebugName = DebugName;
					Initializer.IndexBuffer = nullptr;
					Initializer.TotalPrimitiveCount = 0;
					Initializer.GeometryType = RTGT_Triangles;
					Initializer.bFastBuild = true;
					Initializer.bAllowUpdate = false;

					Section->RayTracingGeometry.SetInitializer(Initializer);
					Section->RayTracingGeometry.InitResource(RHICmdList);

					Initializer = Section->RayTracingGeometry.GetInitializer();
					Initializer.IndexBuffer = Section->IndexBuffer.IndexBufferRHI;
					Initializer.TotalPrimitiveCount = Section->IndexBuffer.Indices.Num() / 3;

					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = Section->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
					Segment.NumPrimitives = Initializer.TotalPrimitiveCount;
					Segment.MaxVertices = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices();
					Initializer.Segments.Add(Segment);
					Section->RayTracingGeometry.SetInitializer(MoveTemp(Initializer));

					//#dxr_todo: add support for segments?

					Section->RayTracingGeometry.UpdateRHI(RHICmdList);
				});
		}
#endif
	}

	virtual ~FDreamUIRenderTargetGeometrySource_SceneProxy()
	{
		if (Section != nullptr)
		{
			Section->VertexBuffers.PositionVertexBuffer.ReleaseResource();
			Section->VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
			Section->VertexBuffers.ColorVertexBuffer.ReleaseResource();
			Section->IndexBuffer.ReleaseResource();
			Section->VertexFactory.ReleaseResource();

#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				Section->RayTracingGeometry.ReleaseResource();
			}
#endif

			delete Section;
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateSection_RenderThread(FRHICommandListBase& RHICmdList, FDynamicMeshVertex* MeshVertexData, int32 NumVerts, uint16* MeshIndexData, int32 NumIndex)
	{
		check(IsInRenderingThread());

		// Iterate through vertex data, copying in new info
		for (int32 i = 0; i < NumVerts; i++)
		{
			auto& Vertex = MeshVertexData[i];

			Section->VertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector3f(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector3f());
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TextureCoordinate[0]);
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 1, Vertex.TextureCoordinate[1]);
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 2, Vertex.TextureCoordinate[2]);
			Section->VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, 3, Vertex.TextureCoordinate[3]);
			Section->VertexBuffers.ColorVertexBuffer.VertexColor(i) = Vertex.Color;
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

		// Lock index buffer. NumIndex counts indices, and each is two bytes: locking and copying NumIndex bytes
		// wrote only the first half of the buffer.
		const uint32 IndexBytes = static_cast<uint32>(NumIndex) * sizeof(uint16);
		auto IndexBufferData = RHICmdList.LockBuffer(Section->IndexBuffer.IndexBufferRHI, 0, IndexBytes, RLM_WriteOnly);
		FMemory::Memcpy(IndexBufferData, (void*)MeshIndexData, IndexBytes);
		RHICmdList.UnlockBuffer(Section->IndexBuffer.IndexBufferRHI);

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			Section->RayTracingGeometry.ReleaseResource();

			FRayTracingGeometryInitializer Initializer;
			Initializer.IndexBuffer = Section->IndexBuffer.IndexBufferRHI;
			Initializer.TotalPrimitiveCount = Section->IndexBuffer.Indices.Num() / 3;
			Initializer.GeometryType = RTGT_Triangles;
			Initializer.bFastBuild = true;
			Initializer.bAllowUpdate = false;

			Section->RayTracingGeometry.SetInitializer(Initializer);
			Section->RayTracingGeometry.InitResource(RHICmdList);

			Initializer = Section->RayTracingGeometry.GetInitializer();
			FRayTracingGeometrySegment Segment;
			Segment.VertexBuffer = Section->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
			Segment.NumPrimitives = Initializer.TotalPrimitiveCount;
			Segment.MaxVertices = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices();
			Initializer.Segments.Add(Segment);
			Section->RayTracingGeometry.SetInitializer(MoveTemp(Initializer));

			Section->RayTracingGeometry.UpdateRHI(RHICmdList);
		}
#endif
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh)
		{
			return;
		}
#if WITH_EDITOR
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* ParentMaterialProxy = nullptr;
		if (bWireframe)
		{
			ParentMaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			ParentMaterialProxy = SurfaceMaterialProxy;
		}
#else
		const bool bWireframe = false;
		FMaterialRenderProxy* ParentMaterialProxy = SurfaceMaterialProxy;
#endif

		if (bHasRenderTarget)
		{
			{
				switch (GeometryMode)
				{
				case EDreamUIRenderTargetGeometryMode::Plane:
				case EDreamUIRenderTargetGeometryMode::Cylinder:
				{
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FDynamicMeshBuilder MeshBuilder(Views[ViewIndex]->GetFeatureLevel());

						if (VisibilityMap & (1 << ViewIndex))
						{
							const FSceneView* View = Views[ViewIndex];
							// Draw the mesh.
							FMeshBatch& Mesh = Collector.AllocateMesh();
							FMeshBatchElement& BatchElement = Mesh.Elements[0];
							BatchElement.IndexBuffer = &Section->IndexBuffer;
							Mesh.bWireframe = bWireframe;
							Mesh.VertexFactory = &Section->VertexFactory;
							Mesh.MaterialRenderProxy = ParentMaterialProxy;

							bool bHasPrecomputedVolumetricLightmap;
							FMatrix PreviousLocalToWorld;
							int32 SingleCaptureIndex;
							bool bOutputVelocity;
							GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
							bOutputVelocity |= AlwaysHasVelocity();

							FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
							DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity, GetCustomPrimitiveData());
							BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

							BatchElement.FirstIndex = 0;
							BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
							BatchElement.MinVertexIndex = 0;
							BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
							Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
							Mesh.Type = PT_TriangleList;
							Mesh.DepthPriorityGroup = SDPG_World;
							Mesh.bCanApplyViewModeOverrides = false;
							Collector.AddMesh(ViewIndex, Mesh);
						}
					}
				}
					break;
				}
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		bool bVisible = true;

		FPrimitiveViewRelevance Result;

		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		Result.bDrawRelevance = IsShown(View) && bVisible && View->Family->EngineShowFlags.WidgetComponents;
		Result.bDynamicRelevance = true;
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		Result.bEditorPrimitiveRelevance = false;
		Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}

	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		bDynamic = false;
		bRelevant = false;
		bLightMapped = false;
		bShadowMapped = false;
	}


#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return true; }

	virtual bool HasRayTracingRepresentation() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override final
	{
		TConstArrayView<const FSceneView*> Views = Collector.GetViews();
		const uint32 VisibilityMap = Collector.GetVisibilityMap();

		// RT geometry will be generated based on first active view and then reused for all other views
		// TODO: Expose a way for developers to control whether to reuse RT geometry or create one per-view
		const int32 FirstActiveViewIndex = FMath::CountTrailingZeros(VisibilityMap);
		checkf(Views.IsValidIndex(FirstActiveViewIndex), TEXT("There should be at least one active view when calling GetDynamicRayTracingInstances(...)."));

		const FSceneView* FirstActiveView = Views[FirstActiveViewIndex];
		
		if (Section != nullptr)
		{
			FMaterialRenderProxy* MaterialProxy = SurfaceMaterialProxy;

			if (Section->RayTracingGeometry.IsValid())
			{
				check(Section->RayTracingGeometry.GetInitializer().IndexBuffer.IsValid());

				FRayTracingInstance RayTracingInstance;
				RayTracingInstance.Geometry = &Section->RayTracingGeometry;
				RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

				uint32 SectionIdx = 0;
				FMeshBatch MeshBatch;

				MeshBatch.VertexFactory = &Section->VertexFactory;
				MeshBatch.SegmentIndex = 0;
				MeshBatch.MaterialRenderProxy = SurfaceMaterialProxy;
				MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
				MeshBatch.Type = PT_TriangleList;
				MeshBatch.DepthPriorityGroup = SDPG_World;
				MeshBatch.bCanApplyViewModeOverrides = false;
				MeshBatch.CastRayTracedShadow = IsShadowCast(FirstActiveView);

				FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
				BatchElement.IndexBuffer = &Section->IndexBuffer;

				bool bHasPrecomputedVolumetricLightmap;
				FMatrix PreviousLocalToWorld;
				int32 SingleCaptureIndex;
				bool bOutputVelocity;
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
				bOutputVelocity |= AlwaysHasVelocity();

				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, bOutputVelocity, GetCustomPrimitiveData());
				BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = Section->IndexBuffer.Indices.Num() / 3;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = Section->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;

				RayTracingInstance.Materials.Add(MeshBatch);
				
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if ((VisibilityMap & (1 << ViewIndex)) == 0)
					{
						continue;
					}

					Collector.AddRayTracingInstance(ViewIndex, RayTracingInstance);
				}
			}
		}
	}

#endif

private:
	/** The component's material instance, as its render proxy: see the constructor. */
	FMaterialRenderProxy* SurfaceMaterialProxy = nullptr;
	bool bHasRenderTarget = false;
	EDreamUIRenderTargetGeometryMode GeometryMode = EDreamUIRenderTargetGeometryMode::Plane;
	FDreamUIRenderTargetGeometrySourceMeshProxySection* Section = nullptr;

	FMaterialRelevance MaterialRelevance;
};


#define PARAMETER_NAME_MAINTEXTURE "MainTexture"


UDreamUIRenderTargetGeometrySource::UDreamUIRenderTargetGeometrySource()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	TargetWidgetPresenter = FDreamUIComponentReference(UDreamWidgetPresenterComponentBase::StaticClass());
}

void UDreamUIRenderTargetGeometrySource::BeginPlay()
{
	Super::BeginPlay();
	BeginCheckRenderTarget();
}
void UDreamUIRenderTargetGeometrySource::EndPlay(EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
	EndCheckRenderTarget();
}

void UDreamUIRenderTargetGeometrySource::BeginCheckRenderTarget()
{
	if (CheckRenderTargetTickTweener.IsValid())return;
	CheckRenderTargetTickTweener = UDreamTweenBPLibrary::UpdateCall(this, [=, WeakThis = TWeakObjectPtr<UDreamUIRenderTargetGeometrySource>(this)](float deltaTime) {
		if (WeakThis.IsValid())
		{
			WeakThis->CheckRenderTargetTick();
		}
		});
	if (CheckRenderTargetTickTweener.IsValid())
	{
		CheckRenderTargetTickTweener->SetAffectByGamePause(false)->SetAffectByTimeDilation(false);
	}
	else
	{
		// No tween manager to poll with: UpdateCall answers null in a world without a game instance.
		// Checked once, now, rather than never -- a target that is already set, which is the ordinary
		// case (the canvas has its texture before anything is shown on it), gets its mesh, bounds,
		// collision and scene proxy here, where it used to get none of them. A target that only turns up
		// later still has no poller in such a world; SetCanvas rebuilds all of it when it is given one.
		CheckRenderTargetTick();
	}
}
void UDreamUIRenderTargetGeometrySource::EndCheckRenderTarget()
{
	if (!CheckRenderTargetTickTweener.IsValid())return;
	UDreamTweenBPLibrary::KillIfIsTweening(this, CheckRenderTargetTickTweener.Get());
	CheckRenderTargetTickTweener.Reset();
}
void UDreamUIRenderTargetGeometrySource::CheckRenderTargetTick()
{
	if (IsValid(GetRenderTarget()))
	{
		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
		MarkRenderStateDirty(); // New section requires recreating scene proxy

		EndCheckRenderTarget();
	}
}

bool UDreamUIRenderTargetGeometrySource::CheckStaticMesh()const
{
	if (StaticMeshComp.IsValid())return true;
	if (GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh)
	{
		if (auto ParentComp = this->GetAttachParent())
		{
			StaticMeshComp = Cast<UStaticMeshComponent>(ParentComp);
			if (StaticMeshComp.IsValid())
			{
				if (MaterialInstance != nullptr)//already called UpdateMaterialInstance, so we need to manually set material to static mesh
				{
					// Only in a game world. In the editor the static mesh component is the user's own, saved with the
					// level, and slot 0 is part of what they authored: the instance put there is transient, so saving
					// wrote null over their override, and nothing put the original back when the mode, the flag or this
					// component changed. A game world's component is a copy, and the instance goes with it.
					if (bOverrideStaticMeshMaterial && DreamUI::IsGameWorld(this))
					{
						//delay call, or the bPostTickComponentUpdate check will break.
						//A frame is long enough for this component to be torn down, so the callback holds a weak
						//reference rather than a bare this. And there is no return value to dereference: DelayFrameCall
						//hands back null wherever there is no game instance to run a tween -- every cook, commandlet and
						//shutdown -- so the pause flag goes in as an argument instead.
						UDreamTweenBPLibrary::DelayFrameCall(this->GetWorld(), 1, [WeakThis = TWeakObjectPtr<const UDreamUIRenderTargetGeometrySource>(this)] {
							if (WeakThis.IsValid() && WeakThis->StaticMeshComp.IsValid())
							{
								WeakThis->StaticMeshComp->SetMaterial(0, WeakThis->MaterialInstance);
							}
							}, false);
					}
				}
				return true;
			}
			else
			{
				auto ErrorMsg = LOCTEXT("StaticMeshComponentNotValid", "StaticMesh component not valid!");
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ErrorMsg.ToString());
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(ErrorMsg, false);
#endif
			}
		}
	}
	return false;
}

FPrimitiveSceneProxy* UDreamUIRenderTargetGeometrySource::CreateSceneProxy()
{
	// Nothing to show -- the canvas went, and no presenter names another -- is nothing to draw, and nothing to
	// warn about either, which GetCanvas would.
	if (!TargetCanvasObject.IsValid() && !TargetWidgetPresenter.IsValidComponentReference())
	{
		return nullptr;
	}
	if (GetCanvas())
	{
		UpdateMaterialInstance();
		if (MaterialInstance != nullptr)
		{
			if (Vertices.Num() > 0 && Triangles.Num() > 0)
			{
#if WITH_EDITOR
				bIsValidSceneProxy = true;
#endif
				return new FDreamUIRenderTargetGeometrySource_SceneProxy(this);
			}
		}
	}

#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		// make something so we can see this component in the editor
		class FWidgetBoxProxy final : public FPrimitiveSceneProxy
		{
		public:
			SIZE_T GetTypeHash() const override
			{
				static size_t UniquePointer;
				return reinterpret_cast<size_t>(&UniquePointer);
			}

			FWidgetBoxProxy(const UDreamUIRenderTargetGeometrySource* InComponent)
				: FPrimitiveSceneProxy(InComponent)
				, BoxExtents(1.f, InComponent->GetRenderTargetSize().X / 2.0f, InComponent->GetRenderTargetSize().Y / 2.0f)
			{
				bWillEverBeLit = false;
			}

			virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_BoxSceneProxy_GetDynamicMeshElements);

				const FMatrix& LocalToWorld = GetLocalToWorld();

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					if (VisibilityMap & (1 << ViewIndex))
					{
						const FSceneView* View = Views[ViewIndex];

						const FLinearColor DrawColor = GetViewSelectionColor(FColor::White, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());

						FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
						DrawOrientedWireBox(PDI, LocalToWorld.GetOrigin(), LocalToWorld.GetScaledAxis(EAxis::X), LocalToWorld.GetScaledAxis(EAxis::Y), LocalToWorld.GetScaledAxis(EAxis::Z), BoxExtents, DrawColor, SDPG_World);
					}
				}
			}

			virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
			{
				FPrimitiveViewRelevance Result;
				if (!View->bIsGameView)
				{
					// Should we draw this because collision drawing is enabled, and we have collision
					const bool bShowForCollision = View->Family->EngineShowFlags.Collision && IsCollisionEnabled();
					Result.bDrawRelevance = IsShown(View) || bShowForCollision;
					Result.bDynamicRelevance = true;
					Result.bShadowRelevance = IsShadowCast(View);
					Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
				}
				return Result;
			}
			virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
			uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

		private:
			const FVector	BoxExtents;
		};

		bIsValidSceneProxy = false;
		return new FWidgetBoxProxy(this);
	}
#endif
	return nullptr;
}
FBoxSphereBounds UDreamUIRenderTargetGeometrySource::CalcBounds(const FTransform& LocalToWorld) const
{
	auto RenderTargetSize = GetRenderTargetSize();
	const float Width = ComputeComponentWidth();
	const float Height = ComputeComponentHeight();
	const float Thickness = ComputeComponentThickness();
	// A negative arc bends the surface the other way (UpdateMeshData lays the vertices on -X), so the box sits on
	// that side; its extent stays positive either way.
	const float ThicknessSide = (GeometryMode == EDreamUIRenderTargetGeometryMode::Cylinder && CylinderArcAngle < 0.0f) ? -1.0f : 1.0f;
	const FVector Origin = FVector(
		ThicknessSide * Thickness * 0.5f,
		Width * (0.5f - Pivot.X),
		Height * (0.5f - Pivot.Y));
	auto BoxExtent = FVector(Thickness * 0.5f, Width * 0.5f, Height * 0.5f);

	FBoxSphereBounds NewBounds(Origin, BoxExtent, RenderTargetSize.Size() / 2.0f);
	NewBounds = NewBounds.TransformBy(LocalToWorld);

	NewBounds.BoxExtent *= BoundsScale;
	NewBounds.SphereRadius *= BoundsScale;

	return NewBounds;
}
UBodySetup* UDreamUIRenderTargetGeometrySource::GetBodySetup()
{
	UpdateBodySetup(false);
	return BodySetup;
}
FCollisionShape UDreamUIRenderTargetGeometrySource::GetCollisionShape(float Inflation) const
{
	auto RenderTargetSize = GetRenderTargetSize();

	FVector BoxHalfExtent = (FVector(0.01f, RenderTargetSize.X * 0.5f, RenderTargetSize.Y * 0.5f) * GetComponentTransform().GetScale3D()) + Inflation;

	if (Inflation < 0.0f)
	{
		// Don't shrink below zero size.
		BoxHalfExtent = BoxHalfExtent.ComponentMax(FVector::ZeroVector);
	}

	return FCollisionShape::MakeBox(BoxHalfExtent);
}
void UDreamUIRenderTargetGeometrySource::OnRegister()
{
	Super::OnRegister();
	if (this->GetWorld() != nullptr)
	{
		if (GetMaterial(0) == nullptr)
		{
			if (auto PresetMaterial = GetPresetMaterial())
			{
				SetMaterial(0, PresetMaterial);
				this->MarkPackageDirty();
			}
		}
#if WITH_EDITOR
		if (!DreamUI::IsGameWorld(this))//only do it in Editor world, because Game world can do it by BeginCheckRenderTarget
		{
			UpdateMeshData();
		}
#endif
	}
}
void UDreamUIRenderTargetGeometrySource::OnUnregister()
{
	Super::OnUnregister();
}
void UDreamUIRenderTargetGeometrySource::DestroyComponent(bool bPromoteChildren)
{
	Super::DestroyComponent(bPromoteChildren);
}
UMaterialInterface* UDreamUIRenderTargetGeometrySource::GetMaterial(int32 MaterialIndex) const
{
	return Super::GetMaterial(MaterialIndex);
}
void UDreamUIRenderTargetGeometrySource::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	Super::SetMaterial(ElementIndex, Material);
	MaterialInstance = nullptr;
#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))
	{
	}
	else
#endif
		UpdateMaterialInstance();
}

void UDreamUIRenderTargetGeometrySource::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (MaterialInstance)
	{
		OutMaterials.AddUnique(MaterialInstance);
	}
}

int32 UDreamUIRenderTargetGeometrySource::GetNumMaterials() const
{
	return FMath::Max<int32>(OverrideMaterials.Num(), 1);
}

bool UDreamUIRenderTargetGeometrySource::GetTriMeshSizeEstimates(struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const
{
	if (!GetRenderTarget())return false;
	if (GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh)return true;
	if (Vertices.Num() == 0 || Triangles.Num() == 0)return true;
	OutTriMeshEstimates.VerticeCount = Vertices.Num();
	return true;
}
bool UDreamUIRenderTargetGeometrySource::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	auto RenderTarget = GetRenderTarget();
	if (!RenderTarget)return false;

	switch (GeometryMode)
	{
	case EDreamUIRenderTargetGeometryMode::Plane:
	case EDreamUIRenderTargetGeometryMode::Cylinder:
	{
		// See if we should copy UVs
		bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults;
		if (bCopyUVs)
		{
			CollisionData->UVs.AddZeroed(1); // only one UV channel
		}

		CollisionData->Vertices.Reserve(Vertices.Num());
		if (bCopyUVs)
		{
			CollisionData->UVs[0].Reserve(Vertices.Num());
		}
		for (int i = 0; i < Vertices.Num(); i++)
		{
			auto& Vert = Vertices[i];
			CollisionData->Vertices.Add(Vert.Position);
			if (bCopyUVs)
			{
				CollisionData->UVs[0].Add(FVector2D(Vert.TextureCoordinate[0]));
			}
		}
		CollisionData->Indices.Reserve(Triangles.Num());
		for (int i = 0; i < Triangles.Num(); i+=3)
		{
			FTriIndices Triangle;

			Triangle.v0 = Triangles[i];
			Triangle.v1 = Triangles[i + 1];
			Triangle.v2 = Triangles[i + 2];
			CollisionData->Indices.Add(Triangle);
		}

		CollisionData->bFlipNormals = true;
		CollisionData->bDeformableMesh = true;
		CollisionData->bFastCook = true;
		return true;
	}
	break;
	case EDreamUIRenderTargetGeometryMode::StaticMesh:
	{
		return false;
	}
	break;
	}
	return false;
}
bool UDreamUIRenderTargetGeometrySource::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
	if (!GetRenderTarget())return false;
	if (GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh)return false;
	if (Vertices.Num() == 0 || Triangles.Num() == 0)return false;
	return true;
}
bool UDreamUIRenderTargetGeometrySource::WantsNegXTriMesh() 
{
	return false; 
}

void UDreamUIRenderTargetGeometrySource::UpdateCollision()
{
	UpdateBodySetup(true);
	RecreatePhysicsState();
}
void UDreamUIRenderTargetGeometrySource::UpdateMeshData()
{
	auto PrevNumVerts = Vertices.Num();
	auto PrevNumIndex = Triangles.Num();
	Vertices.Reset();
	Triangles.Reset();

	auto RenderTarget = GetRenderTarget();
	if (!RenderTarget)
	{
		if (PrevNumVerts != 0 || PrevNumIndex != 0)
		{
			MarkRenderStateDirty();
		}
		return;
	}
	
	switch (GeometryMode)
	{
	case EDreamUIRenderTargetGeometryMode::Plane:
	{
		float U = -RenderTarget->SizeX * Pivot.X;
		float V = -RenderTarget->SizeY * Pivot.Y;
		float UL = RenderTarget->SizeX * (1.0f - Pivot.X);
		float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

		Vertices.Reserve(4);
		Triangles.Reserve(6);

		auto Vert = FDynamicMeshVertex();
		Vert.TangentX = FVector(0, -1, 0);
		Vert.TangentZ = FVector(1, 0, 0);
		Vert.Color = FColor::White;

		Vert.Position = FVector3f(0, U, V);
		Vert.TextureCoordinate[0] = FVector2f(0, 1);
		Vertices.Add(Vert);
		Vert.Position = FVector3f(0, UL, V);
		Vert.TextureCoordinate[0] = FVector2f(1, 1);
		Vertices.Add(Vert);
		Vert.Position = FVector3f(0, U, VL);
		Vert.TextureCoordinate[0] = FVector2f(0, 0);
		Vertices.Add(Vert);
		Vert.Position = FVector3f(0, UL, VL);
		Vert.TextureCoordinate[0] = FVector2f(1, 0);
		Vertices.Add(Vert);

		Triangles.Add(0);
		Triangles.Add(3);
		Triangles.Add(2);
		Triangles.Add(0);
		Triangles.Add(1);
		Triangles.Add(3);
	}
	break;
	case EDreamUIRenderTargetGeometryMode::Cylinder:
	{
		auto ArcAngle = FMath::Max(FMath::DegreesToRadians(FMath::Abs(GetCylinderArcAngle())), 0.01f);
		auto ArcAngleSign = FMath::Sign(GetCylinderArcAngle());

		const int32 NumSegments = FMath::Lerp(MIN_SEG, MAX_SEG, ArcAngle / PI);

		const float Radius = RenderTarget->SizeX / ArcAngle;
		const float Apothem = Radius * FMath::Cos(0.5f * ArcAngle);
		const float ChordLength = 2.0f * Radius * FMath::Sin(0.5f * ArcAngle);
		const float HalfChordLength = ChordLength * 0.5f;

		const float PivotOffsetX = ChordLength * (0.5 - Pivot.X);
		const float V = -RenderTarget->SizeY * Pivot.Y;
		const float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

		//@todo: normal and tangent calculate wrong
		Vertices.Reserve(2 + NumSegments * 2);
		Triangles.Reserve(6 * NumSegments);
		const float RadiansPerStep = ArcAngle / NumSegments;
		float Angle = -ArcAngle * 0.5f;
		const FVector3f CenterPoint = FVector3f(0, HalfChordLength + PivotOffsetX, V);
		auto Vert = FDynamicMeshVertex();
		Vert.Color = FColor::White;
		Vert.Position = FVector3f(0, Radius * FMath::Sin(Angle) + PivotOffsetX, V);
		auto TangentX2D = FVector2f(CenterPoint) - FVector2f(Vert.Position);
		TangentX2D.Normalize();
		auto TangentZ = FVector3f(TangentX2D, 0);
		auto TangentY = FVector3f(0, 0, 1);
		auto TangentX = FVector3f::CrossProduct(TangentY, TangentZ);
		Vert.SetTangents(TangentX, TangentY, TangentZ);
		Vert.TextureCoordinate[0] = FVector2f(0, 1);
		Vertices.Add(Vert);
		Vert.Position.Z = VL;
		Vert.TextureCoordinate[0] = FVector2f(0, 0);
		Vertices.Add(Vert);

		float UVInterval = 1.0f / NumSegments;
		float UVX = 0;
		int32 TriangleIndex = 0;
		for (int32 Segment = 0; Segment < NumSegments; Segment++)
		{
			Angle += RadiansPerStep;
			UVX += UVInterval;

			Vert.Position = FVector3f(ArcAngleSign * (Radius * FMath::Cos(Angle) - Apothem), Radius * FMath::Sin(Angle) + PivotOffsetX, V);
			TangentX2D = FVector2f(Vert.Position) - FVector2f(CenterPoint);
			TangentX2D.Normalize();
			TangentZ = FVector3f(TangentX2D, 0);
			TangentX = FVector3f::CrossProduct(TangentY, TangentZ);
			Vert.SetTangents(TangentX, TangentY, TangentZ);
			Vert.TextureCoordinate[0] = FVector2f(UVX, 1);
			Vertices.Add(Vert);
			Vert.Position.Z = VL;
			Vert.TextureCoordinate[0] = FVector2f(UVX, 0);
			Vertices.Add(Vert);

			Triangles.Add(TriangleIndex);
			Triangles.Add(TriangleIndex + 3);
			Triangles.Add(TriangleIndex + 1);
			Triangles.Add(TriangleIndex);
			Triangles.Add(TriangleIndex + 2);
			Triangles.Add(TriangleIndex + 3);
			TriangleIndex += 2;
		}
	}
	break;
	}

	if (PrevNumVerts == Vertices.Num() && PrevNumIndex == Triangles.Num())//if buffer size not change then we can do update
	{
		if (SceneProxy != nullptr
#if WITH_EDITOR
			&& bIsValidSceneProxy
#endif
			)
		{
			struct FUpdateMeshDataStruct
			{
				TArray<FDynamicMeshVertex> VertexData;
				TArray<uint16> IndexData;
				FDreamUIRenderTargetGeometrySource_SceneProxy* Proxy = nullptr;
			};
			auto UpdateData = new FUpdateMeshDataStruct();
			UpdateData->VertexData = Vertices;
			UpdateData->IndexData = Triangles;
			UpdateData->Proxy = (FDreamUIRenderTargetGeometrySource_SceneProxy*)SceneProxy;
			ENQUEUE_RENDER_COMMAND(FDreamGUIRenderTargetGeometrySourceMeshSectionUpdate)
				([UpdateData](FRHICommandListImmediate& RHICmdList) {
				UpdateData->Proxy->UpdateSection_RenderThread(
					RHICmdList
					, UpdateData->VertexData.GetData()
					, UpdateData->VertexData.Num()
					, UpdateData->IndexData.GetData()
					, UpdateData->IndexData.Num()
				);
				delete UpdateData;
					});
		}
	}
	else
	{
		MarkRenderStateDirty();
	}
}

UDreamCanvas* UDreamUIRenderTargetGeometrySource::GetTargetCanvas_Implementation()const
{
	return GetCanvas();
}
bool UDreamUIRenderTargetGeometrySource::PerformLineTrace_Implementation(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)
{
	return LineTraceHitUV(InHitFaceIndex, InHitPoint, InLineStart, InLineEnd, OutHitUV);
}

#if WITH_EDITOR
bool UDreamUIRenderTargetGeometrySource::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDreamUIRenderTargetGeometrySource, CylinderArcAngle))
		{
			return GeometryMode == EDreamUIRenderTargetGeometryMode::Cylinder;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDreamUIRenderTargetGeometrySource, bOverrideStaticMeshMaterial))
		{
			return GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDreamUIRenderTargetGeometrySource, bEnableInteractOnBackside))
		{
			return GeometryMode != EDreamUIRenderTargetGeometryMode::StaticMesh;
		}
	}

	return Super::CanEditChange(InProperty);
}
void UDreamUIRenderTargetGeometrySource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetName();
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDreamUIRenderTargetGeometrySource, CylinderArcAngle))
		{
			// Signed, with 0 taken as the smallest positive arc: Sign(0) is 0, and an arc of 0 divided the width by it.
			CylinderArcAngle = (CylinderArcAngle < 0.0f ? -1.0f : 1.0f) * FMath::Clamp(FMath::Abs(CylinderArcAngle), 1.0f, 180.0f);
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDreamUIRenderTargetGeometrySource, TargetWidgetPresenter))
		{
			if (!TargetWidgetPresenter.IsValidComponentReference())
			{
				TargetCanvasObject = nullptr;
			}
		}

		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
		MarkRenderStateDirty(); // New section requires recreating scene proxy
	}
}
#endif

UDreamCanvas* UDreamUIRenderTargetGeometrySource::GetCanvas()const
{
	if (TargetCanvasObject.IsValid())
	{
		return TargetCanvasObject.Get();
	}
	if (!TargetWidgetPresenter.IsValidComponentReference())
	{
		// No presenter named, and no canvas handed over (SetCanvas) or none any more: a surface shown a canvas
		// at run time names no presenter, and one whose canvas went has nothing to show. Neither is a mistake
		// to warn about; a presenter that is named but will not do, below, is.
		return nullptr;
	}
	// Each reason is said once, until a canvas is found again; see bReportedCanvasProblem.
	const auto ReportCanvasProblem = [this](const TCHAR* InProblem)
	{
		if (!bReportedCanvasProblem)
		{
			bReportedCanvasProblem = true;
			UE_LOG(DreamGUI, Warning, TEXT("[UDreamUIRenderTargetGeometrySource::GetCanvas] %s: %s"), *GetPathName(), InProblem);
		}
	};
	auto WidgetPresenter = TargetWidgetPresenter.GetComponent<UDreamWidgetPresenterComponentBase>();
	if (WidgetPresenter == nullptr)
	{
		ReportCanvasProblem(TEXT("TargetWidgetPresenter not valid!"));
		return nullptr;
	}
	auto Canvas = WidgetPresenter->GetLoadedCanvas();
	if (Canvas == nullptr)
	{
		ReportCanvasProblem(TEXT("TargetCanvas not valid!"));
		return nullptr;
	}
	if (!Canvas->IsRootCanvas())
	{
		ReportCanvasProblem(TEXT("TargetCanvas must be a root canvas!"));
		return nullptr;
	}
	if (Canvas->GetRenderMode() != EDreamRenderMode::RenderTarget || !IsValid(Canvas->GetRenderTarget()))
	{
		ReportCanvasProblem(TEXT("TargetCanvas's render mode must be RenderTarget!"));
		return nullptr;
	}
	bReportedCanvasProblem = false;
	TargetCanvasObject = Canvas;
	ListenToCanvas(Canvas);
	return Canvas;
}

void UDreamUIRenderTargetGeometrySource::ListenToCanvas(UDreamCanvas* InCanvas)const
{
	if (InCanvas == nullptr)
	{
		return;
	}
	// From GetCanvas too, which is const and caches what it found; the binding changes nothing a caller sees.
	UDreamUIRenderTargetGeometrySource* MutableThis = const_cast<UDreamUIRenderTargetGeometrySource*>(this);
	InCanvas->GetRenderTargetChangedEvent().RemoveAll(MutableThis);
	InCanvas->GetRenderTargetChangedEvent().AddUObject(MutableThis, &UDreamUIRenderTargetGeometrySource::HandleCanvasRenderTargetChanged);
}

void UDreamUIRenderTargetGeometrySource::HandleCanvasRenderTargetChanged(UTextureRenderTarget2D* InTarget)
{
	// Taken from the event rather than from GetRenderTarget, which a canvas that is going still answers with
	// the target it is letting go of.
	if (InTarget != nullptr)
	{
		if (MaterialInstance != nullptr)
		{
			MaterialInstance->SetTextureParameterValue(PARAMETER_NAME_MAINTEXTURE, InTarget);
		}
		// A new target may be a new size: the quad, its bounds and its collision follow it.
		UpdateMeshData();
		UpdateLocalBounds();
		UpdateCollision();
		MarkRenderStateDirty();
		return;
	}
	// None: let go of the old one now, before the collector can take it. Setting a texture parameter to null
	// does nothing -- UMaterialInstance keeps the texture it had -- so the instance's parameters are cleared
	// instead (the target is the only one it sets), and the render state is rebuilt at once, so the proxy,
	// which holds the target for its draw, lets go of it too. Not inside a collection, which is no time to
	// build render state; a canvas reaped there takes this surface's view of it along.
	if (IsGarbageCollecting())
	{
		return;
	}
	// The canvas is forgotten too: it is still valid while it goes, and anything rebuilt from it -- the render
	// state below, first of all -- would take its target straight back.
	TargetCanvasObject = nullptr;
	if (MaterialInstance != nullptr)
	{
		MaterialInstance->ClearParameterValues();
	}
	if (IsRenderStateCreated())
	{
		RecreateRenderState_Concurrent();
	}
}

void UDreamUIRenderTargetGeometrySource::SetCanvas(UDreamCanvas* Value)
{
	if (TargetCanvasObject.Get() != Value)
	{
		if (UDreamCanvas* Previous = TargetCanvasObject.Get())
		{
			Previous->GetRenderTargetChangedEvent().RemoveAll(this);
		}
		TargetCanvasObject = Value;
		ListenToCanvas(Value);
		BeginCheckRenderTarget();

		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
	}
}

void UDreamUIRenderTargetGeometrySource::SetGeometryMode(EDreamUIRenderTargetGeometryMode Value)
{
	if (GeometryMode != Value)
	{
		GeometryMode = Value;
		
		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
	}
}
void UDreamUIRenderTargetGeometrySource::SetPivot(const FVector2D Value)
{
	if (Pivot != Value)
	{
		Pivot = Value;
		
		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
	}
}
void UDreamUIRenderTargetGeometrySource::SetCylinderArcAngle(float Value)
{
	if (CylinderArcAngle != Value)
	{
		CylinderArcAngle = Value;
		// Signed, with 0 taken as the smallest positive arc: Sign(0) is 0, and the arc of 0 it kept divided the
		// surface's width by zero in the bounds and the body, which then held NaN.
		CylinderArcAngle = (CylinderArcAngle < 0.0f ? -1.0f : 1.0f) * FMath::Clamp(FMath::Abs(CylinderArcAngle), 1.0f, 180.0f);
		
		UpdateMeshData();
		UpdateLocalBounds(); // Update overall bounds
		UpdateCollision(); // Mark collision as dirty
	}
}

void UDreamUIRenderTargetGeometrySource::SetEnableInteractOnBackside(bool Value)
{
	if (bEnableInteractOnBackside != Value)
	{
		bEnableInteractOnBackside = Value;
	}
}
void UDreamUIRenderTargetGeometrySource::SetFlipVerticalOnGLES(bool Value)
{
	if (bFlipVerticalOnGLES != Value)
	{
		// Stores the flag and nothing else. The GLES branch that used to push a FlipY scalar into the
		// material was already `#if PLATFORM_ANDROID && 0` ("UE5.1 don't need this"), so it has not
		// compiled on any platform for several engine versions; kept as dead text it read as a
		// platform path that exists. See the property's own comment.
		bFlipVerticalOnGLES = Value;
	}
}

FIntPoint UDreamUIRenderTargetGeometrySource::GetRenderTargetSize()const
{
	if (auto RenderTarget = GetRenderTarget())
	{
		return FIntPoint(RenderTarget->GetSurfaceWidth(), RenderTarget->GetSurfaceHeight());
	}
	return FIntPoint(2, 2);
}

void UDreamUIRenderTargetGeometrySource::UpdateLocalBounds()
{
	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
}
void UDreamUIRenderTargetGeometrySource::UpdateBodySetup(bool bIsDirty)
{
	if (!GetRenderTarget())
	{
		// No target is no surface, and the body of the last one goes with it. It used to be kept: SetCanvas to a
		// canvas without a target emptied the mesh and left the old body catching world traces, and the
		// nested-surface lookup then handed the cylinder trace a hit on a mesh with no vertices to index.
		BodySetup = nullptr;
		return;
	}
	if (!BodySetup || bIsDirty)
	{
		BodySetup = NewObject<UBodySetup>(this, NAME_None, DreamUI::RuntimeObjectFlags);
		BodySetup->CollisionTraceFlag = CTF_UseDefault;
		BodySetup->AggGeom.BoxElems.Add(FKBoxElem());
		BodySetup->bHasCookedCollisionData = false;

		FKBoxElem* BoxElem = BodySetup->AggGeom.BoxElems.GetData();

		const float Width = ComputeComponentWidth();
		const float Height = ComputeComponentHeight();
		const float Thickness = ComputeComponentThickness();
		const FVector Origin = FVector(.5f,
			Width * (0.5f - Pivot.X),
			Height * (0.5f - Pivot.Y));

		BoxElem->X = Thickness;
		BoxElem->Y = Width;
		BoxElem->Z = Height;

		BoxElem->SetTransform(FTransform::Identity);
		BoxElem->Center = Origin;
	}
}

void UDreamUIRenderTargetGeometrySource::UpdateMaterialInstance()
{
	if (MaterialInstance == nullptr)
	{
		auto SourceMat = GetMaterial(0);
		if (SourceMat == nullptr)
		{
			SourceMat = GetPresetMaterial();
		}
		if (SourceMat)
		{
			MaterialInstance = UMaterialInstanceDynamic::Create(SourceMat, this);
			// In StaticMesh mode this instance goes into the static mesh component's OverrideMaterials,
			// which that component saves, copies and duplicates with everything else. The flags make
			// each of those drop the instance instead of carrying a reference to this component's --
			// and, through the texture it samples, this canvas's -- runtime objects along.
			MaterialInstance->SetFlags(DreamUI::RuntimeObjectFlags);
			// Only in a game world, for the reason given in CheckStaticMesh: in the editor slot 0 of the static mesh
			// is the user's authored material.
			if (GeometryMode == EDreamUIRenderTargetGeometryMode::StaticMesh && bOverrideStaticMeshMaterial && DreamUI::IsGameWorld(this))
			{
				if (CheckStaticMesh())
				{
					//delay call, or the bPostTickComponentUpdate check will break.
					//Weak capture and no dereference of the return value, for the reasons given in CheckStaticMesh.
					UDreamTweenBPLibrary::DelayFrameCall(this, 1, [WeakThis = TWeakObjectPtr<UDreamUIRenderTargetGeometrySource>(this)] {
						if (WeakThis.IsValid() && WeakThis->StaticMeshComp.IsValid())
						{
							WeakThis->StaticMeshComp->SetMaterial(0, WeakThis->MaterialInstance);
						}
						}, false);
				}
			}
		}
	}
	UpdateMaterialInstanceParameters();
}

void UDreamUIRenderTargetGeometrySource::UpdateMaterialInstanceParameters()
{
	if (MaterialInstance)
	{
		// The FlipY scalar that used to follow was `#if PLATFORM_ANDROID && 0` dead text -- see
		// SetFlipVerticalOnGLES.
		if (UTextureRenderTarget2D* Target = GetRenderTarget())
		{
			MaterialInstance->SetTextureParameterValue(PARAMETER_NAME_MAINTEXTURE, Target);
		}
		else
		{
			// A null texture parameter is ignored; cleared, the instance samples its material's own again.
			MaterialInstance->ClearParameterValues();
		}
	}
}
UMaterialInterface* UDreamUIRenderTargetGeometrySource::GetPresetMaterial()const
{
	return UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->RenderTargetMaterial, TEXT("RenderTargetMaterial"));
}

UMaterialInstanceDynamic* UDreamUIRenderTargetGeometrySource::GetMaterialInstance()const
{
	return MaterialInstance;
}

UTextureRenderTarget2D* UDreamUIRenderTargetGeometrySource::GetRenderTarget()const
{
	if (auto Canvas = GetCanvas())
	{
		return Canvas->GetRenderTarget();
	}
	return nullptr;
}

float UDreamUIRenderTargetGeometrySource::ComputeComponentWidth() const
{
	auto RenderTargetSize = GetRenderTargetSize();
	switch (GeometryMode)
	{
	default:
		return 0.0f;
		break;
	case EDreamUIRenderTargetGeometryMode::Plane:
		return RenderTargetSize.X;
		break;

	case EDreamUIRenderTargetGeometryMode::Cylinder:
		// The arc UpdateMeshData builds: its size, never under 0.01 radians. An arc of 0 (Sign(0) kept it so) made
		// this X / 0 * sin(0), NaN, which went on into the bounds and the body.
		const float ArcAngleRadians = FMath::Max(FMath::DegreesToRadians(FMath::Abs(CylinderArcAngle)), 0.01f);
		const float Radius = RenderTargetSize.X / ArcAngleRadians;
		return 2.0f * Radius * FMath::Sin(0.5f * ArcAngleRadians);
		break;
	}
}

float UDreamUIRenderTargetGeometrySource::ComputeComponentHeight() const
{
	auto RenderTargetSize = GetRenderTargetSize();
	switch (GeometryMode)
	{
	default:
		return 0.0f;
		break;
	case EDreamUIRenderTargetGeometryMode::Plane:
	case EDreamUIRenderTargetGeometryMode::Cylinder:
		return RenderTargetSize.Y;
		break;
	}
}

float UDreamUIRenderTargetGeometrySource::ComputeComponentThickness() const
{
	auto RenderTargetSize = GetRenderTargetSize();
	switch (GeometryMode)
	{
	default:
		return 0.0f;
		break;
	case EDreamUIRenderTargetGeometryMode::Plane:
		return 0.00f;
		break;

	case EDreamUIRenderTargetGeometryMode::Cylinder:
		// As in ComputeComponentWidth, and unsigned: a negative arc gave a negative thickness, and an inverted box.
		// Which side the surface bends to is CalcBounds' business.
		const float ArcAngleRadians = FMath::Max(FMath::DegreesToRadians(FMath::Abs(CylinderArcAngle)), 0.01f);
		const float Radius = RenderTargetSize.X / ArcAngleRadians;
		return Radius * (1.0f - FMath::Cos(0.5f * ArcAngleRadians));
		break;
	}
}

#include "Kismet/GameplayStatics.h"
bool UDreamUIRenderTargetGeometrySource::LineTraceHitUV(const int32& InHitFaceIndex, const FVector& InHitPoint, const FVector& InLineStart, const FVector& InLineEnd, FVector2D& OutHitUV)const
{
	switch (GeometryMode)
	{
	default:
	case EDreamUIRenderTargetGeometryMode::Plane:
	{
		auto InverseTf = GetComponentTransform().Inverse();
		auto LocalHitPoint = InverseTf.TransformPosition(InHitPoint);

		auto RenderTargetSize = this->GetRenderTargetSize();
		OutHitUV.X = LocalHitPoint.Y / RenderTargetSize.X;
		OutHitUV.Y = LocalHitPoint.Z / RenderTargetSize.Y;
		OutHitUV += Pivot;

		return true;
	}
	break;
	case EDreamUIRenderTargetGeometryMode::Cylinder:
	{
		if (InHitFaceIndex >= 0)
		{
			auto InverseTf = GetComponentTransform().Inverse();
			auto LocalHitPoint = InverseTf.TransformPosition(InHitPoint);
			if (Triangles.IsValidIndex(InHitFaceIndex * 3 + 2))
			{
				int32 Index0 = Triangles[InHitFaceIndex * 3 + 0];
				int32 Index1 = Triangles[InHitFaceIndex * 3 + 1];
				int32 Index2 = Triangles[InHitFaceIndex * 3 + 2];
				if (!Vertices.IsValidIndex(Index0) || !Vertices.IsValidIndex(Index1) || !Vertices.IsValidIndex(Index2))
				{
					return false;
				}

				auto Pos0 = (FVector)Vertices[Index0].Position;
				auto Pos1 = (FVector)Vertices[Index1].Position;
				auto Pos2 = (FVector)Vertices[Index2].Position;

				auto UV0 = (FVector2D)Vertices[Index0].TextureCoordinate[0];
				auto UV1 = (FVector2D)Vertices[Index1].TextureCoordinate[0];
				auto UV2 = (FVector2D)Vertices[Index2].TextureCoordinate[0];

				// Transform hit location from world to local space.
				// Find barycentric coords
				auto BaryCoords = FMath::ComputeBaryCentric2D(LocalHitPoint, Pos0, Pos1, Pos2);
				// Use to blend UVs
				OutHitUV = (BaryCoords.X * UV0) + (BaryCoords.Y * UV1) + (BaryCoords.Z * UV2);
				OutHitUV.Y = 1.0f - OutHitUV.Y;
				return true;
			}
		}
		else//don't have valid faceIndex, then do traditional hit test
		{
			auto InverseTf = GetComponentTransform().Inverse();
			auto LocalSpaceRayOrigin = InverseTf.TransformPosition(InLineStart);
			auto LocalSpaceRayEnd = InverseTf.TransformPosition(InLineEnd);

			auto RenderTargetSize = this->GetRenderTargetSize();
			// The arc UpdateMeshData built, segment count included.
			auto ArcAngle = FMath::Max(FMath::DegreesToRadians(FMath::Abs(GetCylinderArcAngle())), 0.01f);

			const int32 NumSegments = FMath::Lerp(MIN_SEG, MAX_SEG, ArcAngle / PI);
			// The mesh this walks is the one built for the current target, and with no target there is none: the
			// world raycaster always arrives here (it passes no face index), so a ray over a surface whose canvas
			// lost its target -- or over the frame mesh of the same actor -- indexed an empty array.
			if (Vertices.Num() < 2 + 2 * NumSegments)
			{
				return false;
			}

			auto Vert0 = Vertices[0];
			auto Vert1 = Vertices[1];
			auto Position0 = (FVector)Vert0.Position;
			auto Position1 = (FVector)Vert1.Position;

			float UVInterval = 1.0f / NumSegments;
			int32 TriangleIndex = 0;

			struct FHitResultContainer
			{
				FVector2D UV;
				FVector HitPoint;
				float DistSquare;
				FMatrix RectMatrix;
			};
			TArray<FHitResultContainer> MultiHitResult;
			MultiHitResult.Reset();
			for (int32 Segment = 0; Segment < NumSegments; Segment++)
			{
				auto Vert2 = Vertices[(Segment + 1) * 2];
				auto Vert3 = Vertices[(Segment + 1) * 2 + 1];
				auto Position2 = (FVector)Vert2.Position;
				auto Position3 = (FVector)Vert3.Position;

				auto Y = Position2 - Position0;
				Y.Normalize();
				auto Z = FVector(0, 0, 1);
				auto X = FVector::CrossProduct(Y, Z);
				X.Normalize();

				auto LocalRectMatrix = FMatrix(X, Y, Z, Position0);
				auto ToLocalRectMatrix = LocalRectMatrix.Inverse();

				auto RectSpaceRayOrigin = (FVector)ToLocalRectMatrix.TransformPosition(LocalSpaceRayOrigin);
				auto RectSpaceRayEnd = (FVector)ToLocalRectMatrix.TransformPosition(LocalSpaceRayEnd);
				// A culled back face must still advance Position0/Position1 for the next mesh segment.
				if (FMath::Sign(RectSpaceRayOrigin.X) != FMath::Sign(RectSpaceRayEnd.X)
					&& (bEnableInteractOnBackside || RectSpaceRayOrigin.X <= 0.0))
				{
					auto HitPoint = FMath::LinePlaneIntersection(RectSpaceRayOrigin, RectSpaceRayEnd, FVector::ZeroVector, FVector(1, 0, 0));
					//hit point inside rect area
					float Left = 0;
					float Right = (Position2 - Position0).Size();
					float Bottom = 0;
					float Top = RenderTargetSize.Y;
					if (HitPoint.Y > Left && HitPoint.Y < Right && HitPoint.Z > Bottom && HitPoint.Z < Top)
					{
						FHitResultContainer HitResult;
						HitResult.UV.X = Segment * UVInterval + UVInterval * HitPoint.Y / Right;
						HitResult.UV.Y = HitPoint.Z / Top;
						HitResult.DistSquare = FVector::DistSquared(RectSpaceRayOrigin, HitPoint);
						HitResult.HitPoint = HitPoint;
						HitResult.RectMatrix = LocalRectMatrix;

						MultiHitResult.Add(HitResult);
					}
				}

				Position0 = Position2;
				Position1 = Position3;
			}
			if (MultiHitResult.Num() > 0)
			{
				MultiHitResult.Sort([](const FHitResultContainer& A, const FHitResultContainer& B) {
					return A.DistSquare < B.DistSquare;
					});
				auto& HitResult = MultiHitResult[0];

				OutHitUV = HitResult.UV;

				return true;
			}
		}
	}
	break;
	case EDreamUIRenderTargetGeometryMode::StaticMesh:
	{
		if (CheckStaticMesh())
		{
			FHitResult HitResult;
			if (InHitFaceIndex >= 0)
			{
				HitResult.Component = StaticMeshComp;
				HitResult.Location = InHitPoint;
				HitResult.FaceIndex = InHitFaceIndex;
			}
			else
			{
				FCollisionQueryParams QueryParams;
				QueryParams.bTraceComplex = true;
				QueryParams.bReturnFaceIndex = true;
				StaticMeshComp->LineTraceComponent(HitResult, InLineStart, InLineEnd, QueryParams);
			}
			if (UGameplayStatics::FindCollisionUV(HitResult, 0, OutHitUV))
			{
				OutHitUV.Y = 1.0f - OutHitUV.Y;
				return true;
			}
		}
	}
	break;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
