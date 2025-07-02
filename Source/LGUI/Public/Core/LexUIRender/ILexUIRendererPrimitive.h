// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneManagement.h"
#include "MeshBatch.h"
#include "RHIResources.h"
#include "GlobalShader.h"
#include "SceneTextures.h"

class FLexUIRenderer;
class FSceneViewFamily;
class FLexVisualPostProcessRenderProxy;
enum class ELGUICanvasDepthMode :uint8;

struct FLexUIMeshBatchContainer
{
	FMeshBatch Mesh;
	FBufferRHIRef VertexBufferRHI;
	int32 NumVerts = 0;

	FLexUIMeshBatchContainer() {}
};

enum class ELexUIRendererPrimitiveType :uint8
{
	Mesh,
	PostProcess,
};

struct FLexUIPrimitiveSectionDataContainer
{
	void* SectionPointer = nullptr;
};
struct FLexUIPrimitiveDataContainer
{
	class ILexUIRendererPrimitive* Primitive = nullptr;
	ELexUIRendererPrimitiveType Type;
	TArray<FLexUIPrimitiveSectionDataContainer> Sections;
};

class ILexUIRendererPrimitive
{
public:
	virtual ~ILexUIRendererPrimitive() {}

	virtual bool CanRender() const = 0;
	virtual int GetRenderPriority() const = 0;
	/** For world space renderer to tell visibility, eg SceneCapture2D */
	virtual FPrimitiveComponentId GetPrimitiveComponentId() const = 0;
	virtual FVector3f GetWorldPositionForSortTranslucent()const = 0;
	virtual FBoxSphereBounds GetWorldBounds()const = 0;

	virtual void CollectRenderData(TArray<FLexUIPrimitiveDataContainer>& OutRenderData, float CurrentWorldTime) = 0;
	virtual void GetMeshElements(const FSceneViewFamily& ViewFamily, FMeshElementCollector* Collector, const FLexUIPrimitiveDataContainer& PrimitiveData, TArray<FLexUIMeshBatchContainer>& ResultArray) = 0;
	virtual FLexVisualPostProcessRenderProxy* GetPostProcessElement(const void* SectionPtr)const = 0;
};
