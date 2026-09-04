// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"

#include "Core/DreamUIRender/DreamUIRenderer.h"

FDreamUIGizmoMesh::FDreamUIGizmoMesh(const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType)
{
	PrimitiveType = InPrimitiveType;

	VertexBuffer.bAutoClearVerticesAfterInitRHI = false;
	auto& Vertices = VertexBuffer.Vertices;
	Vertices.SetNumUninitialized(InVertexArray.Num());
	FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FDreamUIMeshVertex));
	auto& Indices = IndexBuffer.Indices;
	Indices.SetNumUninitialized(InIndexArray.Num());
	FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FDreamUIMeshIndex));

	// Enqueue initialization of render resource
	BeginInitResource(&IndexBuffer);
	BeginInitResource(&VertexBuffer);
}

FDreamUIGizmoMesh::~FDreamUIGizmoMesh()
{
	IndexBuffer.ReleaseResource();
	VertexBuffer.ReleaseResource();
}

void FDreamUIGizmoMesh::UpdateVertices(TArray<FDreamUIMeshVertex> InVertexArray)
{
	if (VertexBuffer.Vertices.Num() != InVertexArray.Num())
	{
		VertexBuffer.ReleaseResource();
		auto& Vertices = VertexBuffer.Vertices;
		Vertices.SetNumUninitialized(InVertexArray.Num());
		FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FDreamUIMeshVertex));
		// The buffer that was just released and refilled is the vertex one; re-initializing the index
		// buffer instead left VertexBufferRHI null, and the next same-count update locked nothing.
		BeginInitResource(&VertexBuffer);
	}
	else
	{
		// Keep the mesh alive until the command has run: it is owned by shared pointers, and a bare
		// `this` outlived nothing.
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
		[Self = SharedThis(this), InVertexArray = MoveTemp(InVertexArray)](FRHICommandListImmediate& RHICmdList)
		{
			uint32 VertexDataLength = InVertexArray.Num() * sizeof(FDreamUIMeshVertex);
			void* VertexBufferData = RHICmdList.LockBuffer(Self->VertexBuffer.VertexBufferRHI, 0, VertexDataLength, RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, InVertexArray.GetData(), VertexDataLength);
			RHICmdList.UnlockBuffer(Self->VertexBuffer.VertexBufferRHI);
		});
	}
}

void FDreamUIGizmoMesh::UpdateIndices(TArray<FDreamUIMeshIndex> InIndexArray)
{
	if (IndexBuffer.Indices.Num() != InIndexArray.Num())
	{
		IndexBuffer.ReleaseResource();
		auto& Indices = IndexBuffer.Indices;
		Indices.SetNumUninitialized(InIndexArray.Num());
		FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FDreamUIMeshIndex));
		BeginInitResource(&IndexBuffer);
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(FDreamUIMeshUpdate)(
		[Self = SharedThis(this), InIndexArray = MoveTemp(InIndexArray)](FRHICommandListImmediate& RHICmdList)
		{
			uint32 IndicesDataLength = InIndexArray.Num() * sizeof(FDreamUIMeshIndex);
			auto IndexBufferData = RHICmdList.LockBuffer(Self->IndexBuffer.IndexBufferRHI, 0, IndicesDataLength, RLM_WriteOnly);
			FMemory::Memcpy(IndexBufferData, InIndexArray.GetData(), IndicesDataLength);
			RHICmdList.UnlockBuffer(Self->IndexBuffer.IndexBufferRHI);
		});
	}
}

void FDreamUIGizmoMesh::SetColor(const FColor& InColor)
{
	for (auto& Vertex : VertexBuffer.Vertices)
	{
		Vertex.Color = InColor;
	}
	UpdateVertices(VertexBuffer.Vertices);
}

void FDreamUIGizmoMesh::UpdateLocalBounds()
{
	FBox Box;
	for (const auto& Vertex : VertexBuffer.Vertices)
	{
		Box += FVector(Vertex.Position);
	}
	LocalBounds = FBoxSphereBounds(Box);
}

void FDreamUIGizmoMesh::Render(TSharedPtr<FDreamUIRenderer> DreamUIRenderer, bool ScreenSpaceOrWorldSpace)
{
#if WITH_EDITOR
	if (ScreenSpaceOrWorldSpace)
	{
		DreamUIRenderer->AddScreenSpaceGizmoMesh(SharedThis(this));
	}
	else
	{
		DreamUIRenderer->AddWorldSpaceGizmoMesh(SharedThis(this));
	}
#endif
}
