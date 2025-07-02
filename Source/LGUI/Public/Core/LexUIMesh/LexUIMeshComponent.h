// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/MeshComponent.h"
#include "Core/LexUIMeshIndex.h"
#include "Core/LexUIMeshVertex.h"
#include "LexUIMeshComponent.generated.h"

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
};
struct FLexUIMeshSection : public FLexUIRenderSection
{
	FLexUIMeshSection() 
	{
		Type = ELexUIRenderSectionType::Mesh; 
	}
	virtual ~FLexUIMeshSection()override{}

	TArray<FLexUIMeshIndexBufferType> triangles;
	TArray<FLexUIMeshVertex> vertices;

	int prevVertexCount = 0;
	int prevIndexCount = 0;

	UMaterialInterface* material = nullptr;

	void Reset()
	{
		vertices.Reset();
		triangles.Reset();
	}
	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
};
struct FLexUIPostProcessSection : public FLexUIRenderSection
{
	FLexUIPostProcessSection()
	{
		Type = ELexUIRenderSectionType::PostProcess;
	}
	virtual ~FLexUIPostProcessSection()override{}

	TWeakObjectPtr<class ULexVisualPostProcess> PostProcessRenderableObject = nullptr;

	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
};
struct FLexUIChildCanvasSection : public FLexUIRenderSection
{
	FLexUIChildCanvasSection()
	{
		Type = ELexUIRenderSectionType::ChildCanvas;
	}
	virtual ~FLexUIChildCanvasSection()override{}

	class ULexUIMeshComponent* ChildCanvasMeshComponent = nullptr;

	virtual void UpdateSectionBox(const FTransform& LocalToWorld) override;
};

class FLexUIRenderer;
class ILexUIRendererPrimitive;
class ULexCanvas;

DECLARE_MULTICAST_DELEGATE_TwoParams(FLexUIMeshSceneProxyCreateDeleteDelegate, class ULexUIMeshComponent*, class FLexUIRenderSceneProxy*);

//LexUI render mesh
//@todo: split this class to: one for UE renderer && one for LexUI renderer, will it be more efficient?
UCLASS(ClassGroup = (LGUI), Blueprintable)
class LGUI_API ULexUIMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	ULexUIMeshComponent();
	void CreateRenderSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection);
	void UpdateMeshSectionRenderData(TSharedPtr<FLexUIRenderSection> InRenderSection, bool InVertexPositionChanged, bool InRequireNormalAndTangent);
	void DeleteRenderSection(TSharedPtr<FLexUIRenderSection> InRenderSection);
	TSharedPtr<FLexUIRenderSection> CreateRenderSection(ELexUIRenderSectionType type);
	void SetRenderSectionRenderPriority(TSharedPtr<FLexUIRenderSection> InRenderSection, int32 InSortPriority);
	void SetMeshSectionMaterial(TSharedPtr<FLexUIRenderSection> InMeshSection, UMaterialInterface* InMaterial);

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
	TArray<TSharedPtr<FLexUIRenderSection>> RenderSections;
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


