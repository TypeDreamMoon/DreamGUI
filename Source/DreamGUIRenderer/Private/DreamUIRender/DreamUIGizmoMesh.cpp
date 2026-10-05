// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIRender/DreamUIGizmoMesh.h"

#include "RenderingThread.h"
#include "RenderResource.h"
#include "DreamUIRender/DreamUIRenderer.h"

TSharedRef<FDreamUIGizmoMesh> FDreamUIGizmoMesh::Create(const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType)
{
	TSharedRef<FDreamUIGizmoMesh> Mesh = MakeShared<FDreamUIGizmoMesh>(FPrivateToken(), InVertexArray, InIndexArray, InPrimitiveType);
	// Held, not pointed into: see the declaration.
	ENQUEUE_RENDER_COMMAND(FDreamUIGizmoMesh_InitResources)(
		[Mesh](FRHICommandListImmediate& RHICmdList)
		{
			Mesh->IndexBuffer.InitResource(RHICmdList);
			Mesh->VertexBuffer.InitResource(RHICmdList);
		});
	return Mesh;
}

FDreamUIGizmoMesh::FDreamUIGizmoMesh(FPrivateToken, const TArray<FDreamUIMeshVertex>& InVertexArray, const TArray<FDreamUIMeshIndex>& InIndexArray, EDreamUIGizmoMeshPrimitiveType InPrimitiveType)
{
	PrimitiveType = InPrimitiveType;

	VertexBuffer.bAutoClearVerticesAfterInitRHI = false;
	auto& Vertices = VertexBuffer.Vertices;
	Vertices.SetNumUninitialized(InVertexArray.Num());
	FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FDreamUIMeshVertex));
	auto& Indices = IndexBuffer.Indices;
	Indices.SetNumUninitialized(InIndexArray.Num());
	FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FDreamUIMeshIndex));
}

FDreamUIGizmoMesh::~FDreamUIGizmoMesh()
{
	/**
	 * This runs wherever the last shared pointer to the mesh is dropped, always after the buffers were made: the command
	 * that makes them holds the mesh (Create), and so does every command that makes them again. On the game thread the
	 * render thread may still be drawing it, and the buffers are members, which cannot be handed to a deferred release and
	 * left to outlive the object: the release is enqueued and waited for. Anywhere else it is the render thread's side
	 * letting go -- the render thread, or a render task deleting the renderer that held the frame's gizmos -- where a flush
	 * is not allowed (it is the game thread's) and not needed: nothing that still draws the mesh holds it, and a draw
	 * already recorded keeps the RHI buffers it names alive by itself.
	 */
	if (IsInGameThread())
	{
		ReleaseResourceAndFlush(&IndexBuffer);
		ReleaseResourceAndFlush(&VertexBuffer);
	}
	else
	{
		IndexBuffer.ReleaseResource();
		VertexBuffer.ReleaseResource();
	}
}

void FDreamUIGizmoMesh::UpdateVertices(TArray<FDreamUIMeshVertex> InVertexArray)
{
	if (VertexBuffer.Vertices.Num() != InVertexArray.Num())
	{
		auto& Vertices = VertexBuffer.Vertices;
		Vertices.SetNumUninitialized(InVertexArray.Num());
		FMemory::Memcpy(Vertices.GetData(), InVertexArray.GetData(), InVertexArray.Num() * sizeof(FDreamUIMeshVertex));
		// The vertex buffer made again, on the render thread, which may be drawing the old one until then, by a command
		// that holds the mesh as Create's does. (It is the vertex buffer: re-initializing the index buffer instead left
		// VertexBufferRHI null, and the next same-count update locked nothing.)
		ENQUEUE_RENDER_COMMAND(FDreamUIGizmoMesh_RemakeVertexBuffer)(
			[Self = SharedThis(this)](FRHICommandListImmediate& RHICmdList)
			{
				Self->VertexBuffer.ReleaseResource();
				Self->VertexBuffer.InitResource(RHICmdList);
			});
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
		auto& Indices = IndexBuffer.Indices;
		Indices.SetNumUninitialized(InIndexArray.Num());
		FMemory::Memcpy(Indices.GetData(), InIndexArray.GetData(), InIndexArray.Num() * sizeof(FDreamUIMeshIndex));
		// As in UpdateVertices.
		ENQUEUE_RENDER_COMMAND(FDreamUIGizmoMesh_RemakeIndexBuffer)(
			[Self = SharedThis(this)](FRHICommandListImmediate& RHICmdList)
			{
				Self->IndexBuffer.ReleaseResource();
				Self->IndexBuffer.InitResource(RHICmdList);
			});
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
