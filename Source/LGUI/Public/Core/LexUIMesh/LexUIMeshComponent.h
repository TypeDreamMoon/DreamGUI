// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/MeshComponent.h"
#include "Core/LexUIMeshIndex.h"
#include "Core/LexUIMeshVertex.h"
#include "LexUIMeshComponent.generated.h"

class FLexUIDrawCall;
struct FLexUIRenderSectionProxy;
struct FLexUIMeshSectionProxy;
struct FLexUIPostProcessSectionProxy;
struct FLexUIChildCanvasSectionProxy;

enum class ELexUIRenderSectionType :uint8
{
	Mesh, PostProcess, ChildCanvas,
};
struct FLexUIRenderSection
{
	FLexUIRenderSection(){};
	virtual ~FLexUIRenderSection(){}
	ELexUIRenderSectionType Type = ELexUIRenderSectionType::Mesh;
	int RenderPriority = 0;
	FLexUIRenderSectionProxy* RenderProxy = nullptr;
	FBox BoundingBox = FBox(EForceInit::ForceInit);//world space bounding box

	virtual void UpdateSectionBox(const FTransform& LocalToWorld) = 0;
	virtual void ClearBeforePool() = 0;
};
struct FLexUIRenderSection_Mesh : public FLexUIRenderSection
{
	FLexUIRenderSection_Mesh() 
	{
		Type = ELexUIRenderSectionType::Mesh; 
	}
	virtual ~FLexUIRenderSection_Mesh()override{}

	TArray<FLexUIMeshIndexBufferType> triangleIndices;
	TArray<FLexUIMeshVertex> vertices;

	UMaterialInterface* material = nullptr;

	void Reset()
	{
		vertices.Reset();
		triangleIndices.Reset();
	}
	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
	virtual void ClearBeforePool() override;
};
struct FLexUIRenderSection_PostProcess : public FLexUIRenderSection
{
	FLexUIRenderSection_PostProcess()
	{
		Type = ELexUIRenderSectionType::PostProcess;
	}
	virtual ~FLexUIRenderSection_PostProcess()override{}

	TWeakObjectPtr<class ULexVisualPostProcess> PostProcessVisualObject = nullptr;

	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
	virtual void ClearBeforePool() override;
};
struct FLexUIRenderSection_ChildCanvas : public FLexUIRenderSection
{
	FLexUIRenderSection_ChildCanvas()
	{
		Type = ELexUIRenderSectionType::ChildCanvas;
	}
	virtual ~FLexUIRenderSection_ChildCanvas()override{}

	TWeakObjectPtr<class ULexUIMeshComponent> ChildCanvasMeshComponent = nullptr;

	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
	virtual void ClearBeforePool() override;
};

class FLexUIRenderer;
class ILexUIRendererPrimitive;
class ULexCanvas;

DECLARE_MULTICAST_DELEGATE_TwoParams(FLexUIMeshSceneProxyCreateDeleteDelegate, class ULexUIMeshComponent*, class FLexUIRenderSceneProxy*);

//LexUI render mesh
//@todo: split this class to: one for UE renderer && one for LexUI renderer, will it be more efficient?
UCLASS(ClassGroup = (LGUI))
class LGUI_API ULexUIMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	ULexUIMeshComponent();
private:
	void UpdateMeshSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection, bool InRequireNormalAndTangent);
	void ExpandMeshSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection);
public:
	TSharedPtr<FLexUIRenderSection> SetupRenderSection(ELexUIRenderSectionType InType, FLexUIDrawCall* InDrawCallData);
	void UpdateMeshSection(int Index, FLexUIDrawCall* InDrawCallData);
	void PoolRenderSection(TSharedPtr<FLexUIRenderSection> InRenderSection);
	void PoolAllRenderSection();
	void SetRenderSectionRenderPriority(int32 InSectionIndex, int32 InSortPriority);
	void SetMeshSectionMaterial(int32 InSectionIndex, UMaterialInterface* InMaterial);

	void SetRenderCanvas(ULexCanvas* InCanvas);
	void SetSupportLexUIRenderer(bool InSupportOrNot, TWeakPtr<FLexUIRenderer, ESPMode::ThreadSafe> InLexUIRenderer, bool InIsRenderToWorld);
	void SetSupportUERenderer(bool InSupportOrNot);
	void ClearRenderData();

	void SetUITranslucentSortPriority(int32 NewTranslucentSortPriority);

	void VerifyMaterials();
	void SetParentCanvasMeshComp(ULexUIMeshComponent* InMesh);
	void ClearParentCanvasMeshComp(ULexUIMeshComponent* InMesh);

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
	TArray<TSharedPtr<FLexUIRenderSection>> RenderSectionArray;
	TArray<TSharedPtr<FLexUIRenderSection>> RenderSectionPool;
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	friend class FLexUIRenderSceneProxy;

protected:
	TWeakPtr<FLexUIRenderer, ESPMode::ThreadSafe> LexUIRenderer;
	bool bIsLexUIRenderToWorld = false;//LexUI renderer render to world or screen
	TWeakObjectPtr<ULexCanvas> RenderCanvas = nullptr;
	bool bIsSupportUERenderer = true;
	TWeakObjectPtr<ULexUIMeshComponent> ParentCanvasMeshComp = nullptr;

public:
	FLexUIMeshSceneProxyCreateDeleteDelegate OnSceneProxyCreated;
};


