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
	virtual FDreamVisualPostProcessRenderProxy* DreamUI_GetPostProcessElement(FDreamUIRenderSectionProxy* SectionPtr)const = 0;
};
