// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIMesh/LexUIGizmoMesh.h"

#include "Core/LexUIRender/LexUIRenderer.h"

FLexUIGizmoMesh::FLexUIGizmoMesh(const TArray<FLexUIMeshVertex>& InVertexArray, const TArray<FLexUIMeshIndex>& InIndexArray, ELexUIGizmoMeshPrimitiveType InPrimitiveType)
{
	PrimitiveType = InPrimitiveType;

	VertexBuffer.bAutoClearVerticesAfterInitRHI = false;
	auto& Vertices = VertexBuffer.Vertices;
	Vertices.SetNumUninitialized(InVertexArray.Num());
	FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FLexUIMeshVertex));
	auto& Indices = IndexBuffer.Indices;
	Indices.SetNumUninitialized(InIndexArray.Num());
	FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FLexUIMeshIndex));

	// Enqueue initialization of render resource
	BeginInitResource(&IndexBuffer);
	BeginInitResource(&VertexBuffer);
}

FLexUIGizmoMesh::~FLexUIGizmoMesh()
{
	IndexBuffer.ReleaseResource();
	VertexBuffer.ReleaseResource();
}

void FLexUIGizmoMesh::UpdateVertices(TArray<FLexUIMeshVertex> InVertexArray)
{
	if (VertexBuffer.Vertices.Num() != InVertexArray.Num())
	{
		VertexBuffer.ReleaseResource();
		auto& Vertices = VertexBuffer.Vertices;
		Vertices.SetNumUninitialized(InVertexArray.Num());
		FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FLexUIMeshVertex));
		BeginInitResource(&IndexBuffer);
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(FLexUIMeshUpdate)(
		[this, InVertexArray = MoveTemp(InVertexArray)](FRHICommandListImmediate& RHICmdList)
		{
			uint32 VertexDataLength = InVertexArray.Num() * sizeof(FLexUIMeshVertex);
			void* VertexBufferData = RHICmdList.LockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexDataLength, RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, InVertexArray.GetData(), VertexDataLength);
			RHICmdList.UnlockBuffer(VertexBuffer.VertexBufferRHI);
		});
	}
}

void FLexUIGizmoMesh::UpdateIndices(TArray<FLexUIMeshIndex> InIndexArray)
{
	if (IndexBuffer.Indices.Num() != InIndexArray.Num())
	{
		IndexBuffer.ReleaseResource();
		auto& Indices = IndexBuffer.Indices;
		Indices.SetNumUninitialized(InIndexArray.Num());
		FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FLexUIMeshIndex));
		BeginInitResource(&IndexBuffer);
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(FLexUIMeshUpdate)(
		[this, InIndexArray = MoveTemp(InIndexArray)](FRHICommandListImmediate& RHICmdList)
		{
			uint32 IndicesDataLength = InIndexArray.Num() * sizeof(FLexUIMeshIndex);
			auto IndexBufferData = RHICmdList.LockBuffer(IndexBuffer.IndexBufferRHI, 0, IndicesDataLength, RLM_WriteOnly);
			FMemory::Memcpy(IndexBufferData, InIndexArray.GetData(), IndicesDataLength);
			RHICmdList.UnlockBuffer(IndexBuffer.IndexBufferRHI);
		});
	}
}

void FLexUIGizmoMesh::SetColor(const FColor& InColor)
{
	for (auto& Vertex : VertexBuffer.Vertices)
	{
		Vertex.Color = InColor;
	}
	UpdateVertices(VertexBuffer.Vertices);
}

void FLexUIGizmoMesh::UpdateLocalBounds()
{
	FBox Box;
	for (const auto& Vertex : VertexBuffer.Vertices)
	{
		Box += FVector(Vertex.Position);
	}
	LocalBounds = FBoxSphereBounds(Box);
}

void FLexUIGizmoMesh::Render(TSharedPtr<FLexUIRenderer> LexUIRenderer, bool ScreenSpaceOrWorldSpace)
{
#if WITH_EDITOR
	if (ScreenSpaceOrWorldSpace)
	{
		LexUIRenderer->AddScreenSpaceGizmoMesh(SharedThis(this));
	}
	else
	{
		LexUIRenderer->AddWorldSpaceGizmoMesh(SharedThis(this));
	}
#endif
}
