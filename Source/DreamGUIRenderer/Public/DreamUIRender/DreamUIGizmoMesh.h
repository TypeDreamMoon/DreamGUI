// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIMeshIndex.h"
#include "DreamUIRender/DreamUIMeshVertex.h"

enum class EDreamUIGizmoMeshPrimitiveType
{
	Line, Triangle,
};

class DREAMGUIRENDERER_API FDreamUIGizmoMesh : public TSharedFromThis<FDreamUIGizmoMesh>
{
	/** Only Create makes a mesh. */
	struct FPrivateToken { explicit FPrivateToken() = default; };
public:
	/**
	 * Game thread: a mesh, its buffers' initialization sent to the render thread by a command that holds the mesh until it
	 * has run. The mesh can be let go of last on the render thread, ahead of that command: a world's renderer deleted
	 * there by a command it sent earlier takes the gizmos added to it since along. The initialization used to hold only a
	 * pointer into the mesh, and then ran on freed memory and left the engine's list of render resources naming it, which
	 * the RHI walks as the editor closes ("Resource->GetListIndex() == Index").
	 */
	static TSharedRef<FDreamUIGizmoMesh> Create(const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType);
	FDreamUIGizmoMesh(FPrivateToken, const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType);
	~FDreamUIGizmoMesh();

	void UpdateVertices(TArray<FDreamUIMeshVertex> InVertexArray);
	void UpdateIndices(TArray<FDreamUIMeshIndex> InIndexArray);
	void SetColor(const FColor& InColor);
	void UpdateLocalBounds();
	void Render(TSharedPtr<class FDreamUIRenderer> DreamUIRenderer, bool ScreenSpaceOrWorldSpace);
	
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
