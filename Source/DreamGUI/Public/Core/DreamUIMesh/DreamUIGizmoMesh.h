// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Core/DreamUIMeshIndex.h"
#include "Core/DreamUIMeshVertex.h"

enum class EDreamUIGizmoMeshPrimitiveType
{
	Line, Triangle,
};

class DREAMGUI_API FDreamUIGizmoMesh : public TSharedFromThis<FDreamUIGizmoMesh>
{
public:
	FDreamUIGizmoMesh(){}
	FDreamUIGizmoMesh(const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType);
	~FDreamUIGizmoMesh();

	void UpdateVertices(TArray<FDreamUIMeshVertex> InVertexArray);
	void UpdateIndices(TArray<FDreamUIMeshIndex> InIndexArray);
	void SetColor(const FColor& InColor);
	void UpdateLocalBounds();
	void Render(TSharedPtr<class FDreamUIRenderer> DreamUIRenderer, bool ScreenSpaceOrWorldSpace);
	
	TStrongObjectPtr<UMaterialInterface> Material = nullptr;
	FMatrix LocalToWorldMatrix = FMatrix::Identity;
	FBoxSphereBounds LocalBounds = FBoxSphereBounds(EForceInit::ForceInit);
	EDreamUIGizmoMeshPrimitiveType GetPrimitiveType()const { return PrimitiveType; }
	const FDreamUIMeshVertexBuffer& GetVertexBuffer() { return VertexBuffer; }
	uint32 GetNumVertices()const { return VertexBuffer.Vertices.Num(); }
	const FDreamUIMeshIndexBuffer& GetIndexBuffer() { return IndexBuffer; }
private:
	EDreamUIGizmoMeshPrimitiveType PrimitiveType = EDreamUIGizmoMeshPrimitiveType::Triangle;
	FDreamUIMeshVertexBuffer VertexBuffer;
	/** Index buffer for this section */
	FDreamUIMeshIndexBuffer IndexBuffer;
};
