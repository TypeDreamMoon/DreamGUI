// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneManagement.h"
#include "MeshBatch.h"
#include "RHIResources.h"
#include "GlobalShader.h"
#include "Core/DreamUIRender/DreamUIBaseShaders.h"

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

struct FDreamUIMeshBatchContainer
{
	FMeshBatch Mesh;
	FBufferRHIRef VertexBufferRHI;
	int32 NumVerts = 0;
	/** When enabled, the renderer draws this batch with the built-in UI shader instead of Mesh.MaterialRenderProxy. */
	FDreamUIBuiltInDrawParams BuiltIn;
	/** Primitive transform, for the built-in path (the material path reads it from the primitive uniform buffer). */
	FMatrix LocalToWorld = FMatrix::Identity;

	FDreamUIMeshBatchContainer() {}
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
	TArray<FDreamUIPrimitiveSectionDataContainer> Sections;
};

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

	virtual void DreamUI_CollectRenderData(TArray<FDreamUIPrimitiveDataContainer>& OutRenderData) = 0;
	virtual void DreamUI_GetMeshElements(const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector, const FDreamUIPrimitiveDataContainer& PrimitiveData, TArray<FDreamUIMeshBatchContainer>& ResultArray) = 0;
	/** Returns a reference, not a borrow: the caller keeps the proxy alive for as long as it renders with it. */
	virtual FDreamVisualPostProcessRenderProxyPtr DreamUI_GetPostProcessElement(FDreamUIRenderSectionProxy* SectionPtr)const = 0;
};
