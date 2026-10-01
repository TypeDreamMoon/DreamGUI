// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneManagement.h"
#include "MeshBatch.h"
#include "RHIResources.h"
#include "GlobalShader.h"
#include "DreamUIRender/DreamUIBaseShaders.h"

class FDreamUIRenderer;
class FSceneViewFamily;
class FDreamVisualPostProcessRenderProxy;

/**
 * Shared ownership of a post-process render proxy.
 *
 * Three parties hold one: the visual on the game thread, the mesh section on the render thread, and
 * every render command still in flight. They are torn down independently -- the visual's BeginDestroy
 * does not wait for the canvas to pool its sections -- so the proxy outlives whichever of them lets go
 * first, and the last reference (always released on the render thread) destroys it. A raw pointer here
 * is what let the visual delete a proxy the section was still reading.
 *
 * Declared alongside the forward declaration rather than pulled in from the proxy header, which
 * includes this one.
 */
using FDreamVisualPostProcessRenderProxyPtr = TSharedPtr<FDreamVisualPostProcessRenderProxy, ESPMode::ThreadSafe>;

/**
 * What a draw of one mesh section binds that the section keeps from frame to frame -- its vertex and index buffers and
 * its built-in parameters -- made again whenever any of it changes, and never changed after. The section keeps the
 * newest; a batch collected from it holds the one it was collected with, for as long as its pass may run. A batch used
 * to hold each buffer, texture and sampler itself: nine references taken and given back for every panel of a world of
 * them, every frame, most of them on the textures and samplers every panel shares.
 */
class FDreamUISectionDrawState : public FRefCountedObject
{
public:
	FBufferRHIRef VertexBufferRHI;
	/**
	 * The index buffer to draw with. Mesh.Elements[0].IndexBuffer points into the section proxy that owns it, which a
	 * pass must not reach through: the batch is collected when the pass is recorded, and the pass may run after the
	 * proxy is gone.
	 */
	FBufferRHIRef IndexBufferRHI;
	/**
	 * Whether or not the built-in UI shader draws a batch of the section, its widget data and render layer table are what
	 * either vertex shader places a render layer's vertices on the canvas through before LocalToWorld
	 * (DreamUIRenderLayer.ush): the primitive stays the canvas, so that everything read in its space -- a material's
	 * LocalPosition, the clip rects -- stays in canvas space.
	 */
	FDreamUIBuiltInDrawParams BuiltIn;
};

struct FDreamUIMeshBatchContainer
{
	FMeshBatch Mesh;
	/** See FDreamUISectionDrawState: one reference for the section's buffers and built-in parameters. */
	TRefCountPtr<const FDreamUISectionDrawState> State;
	int32 NumVerts = 0;
	/** Drawn with the built-in UI shader instead of Mesh.MaterialRenderProxy: the section's say, in any view but a wireframe. */
	bool bBuiltIn = false;
	/** Primitive transform, for the built-in path (the material path reads it from the primitive uniform buffer). */
	FMatrix LocalToWorld = FMatrix::Identity;
	/**
	 * The primitive uniform buffer Mesh.Elements[0].PrimitiveUniformBufferResource points at, when it is one its primitive
	 * keeps from frame to frame rather than one made for the frame: held for as long as the batch is, so that a pass run
	 * after the primitive is gone still reads it.
	 */
	TSharedPtr<const FRenderResource, ESPMode::ThreadSafe> PrimitiveUniformBufferHold;

	FDreamUIMeshBatchContainer() {}

	const FDreamUIBuiltInDrawParams& GetBuiltIn() const { return State->BuiltIn; }
	FRHIBuffer* GetVertexBuffer() const { return State->VertexBufferRHI; }
	FRHIBuffer* GetIndexBuffer() const { return State->IndexBufferRHI; }
};

enum class EDreamUIRendererPrimitiveType :uint8
{
	Mesh,
	PostProcess,
};

struct FDreamUIRenderSectionProxy;
struct FDreamUIPrimitiveSectionDataContainer
{
	FDreamUIRenderSectionProxy* SectionPointer = nullptr;
};
struct FDreamUIPrimitiveDataContainer
{
	class IDreamUIRendererPrimitive* Primitive = nullptr;
	EDreamUIRendererPrimitiveType Type;
	/** A few held in place: collected for every primitive every frame, which for a wall of world panels was an allocation each. */
	TArray<FDreamUIPrimitiveSectionDataContainer, TInlineAllocator<4>> Sections;
};
/** What a primitive collects in a frame (IDreamUIRendererPrimitive::DreamUI_CollectRenderData): mostly one run, held in place. */
using FDreamUIPrimitiveDataArray = TArray<FDreamUIPrimitiveDataContainer, TInlineAllocator<1>>;

class IDreamUIRendererPrimitive
{
public:
	virtual ~IDreamUIRendererPrimitive() {}

#if !UE_BUILD_SHIPPING
	FString DebugName = TEXT("DebugNameNone");
#endif
	virtual bool DreamUI_CanRender() const = 0;
	virtual int DreamUI_GetRenderPriority() const = 0;
	/** For world space renderer to tell visibility, e.g. SceneCapture2D */
	virtual FPrimitiveComponentId DreamUI_GetPrimitiveComponentId() const = 0;
	virtual FVector3f DreamUI_GetWorldPositionForSortTranslucent()const = 0;
	virtual FBoxSphereBounds DreamUI_GetWorldBounds()const = 0;

	virtual void DreamUI_CollectRenderData(FDreamUIPrimitiveDataArray& OutRenderData) = 0;
	virtual void DreamUI_GetMeshElements(const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector, const FDreamUIPrimitiveDataContainer& PrimitiveData, TArray<FDreamUIMeshBatchContainer>& ResultArray) = 0;
	/** Returns a reference, not a borrow: the caller keeps the proxy alive for as long as it renders with it. */
	virtual FDreamVisualPostProcessRenderProxyPtr DreamUI_GetPostProcessElement(FDreamUIRenderSectionProxy* SectionPtr)const = 0;
};
