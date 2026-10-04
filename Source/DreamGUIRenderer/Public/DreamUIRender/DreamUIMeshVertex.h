// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "PackedNormal.h"
#include "RenderMath.h"

#define LEXUI_VERTEX_TEXCOORDINATE_COUNT 4
/**
 * The vertex attribute FDreamUIMeshVertex::UV4 is declared as. The attributes go Position (0), Color (1), the
 * TextureCoordinate channels (2 to 5), TangentX (6), TangentZ (7), then UV4: last, so that no attribute before it moved.
 */
#define LEXUI_VERTEX_UV4_ATTRIBUTE 8
/** Every UV channel a vertex carries, UV4 included: what the UE renderer's split vertex buffers are made with. */
#define LEXUI_VERTEX_UV_CHANNEL_COUNT (LEXUI_VERTEX_TEXCOORDINATE_COUNT + 1)

struct DREAMGUIRENDERER_API FDreamUIMeshVertex
{
	FDreamUIMeshVertex(){}
	FDreamUIMeshVertex(const FVector3f& InPosition):
		Position(InPosition),
		Color(FColor(255, 255, 255)),
		TangentX(FVector3f(1, 0, 0)),
		TangentZ(FVector3f(0, 0, 1))
	{
		// basis determinant default to +1.0
		TangentZ.Vector.W = 127;

		for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
		{
			TextureCoordinate[i] = FVector2f::ZeroVector;
		}
	}
	FDreamUIMeshVertex(const FVector3f& InPosition, const FColor& InColor):
		Position(InPosition),
		Color(InColor),
		TangentX(FVector3f(1, 0, 0)),
		TangentZ(FVector3f(0, 0, 1))
	{
		// basis determinant default to +1.0
		TangentZ.Vector.W = 127;

		for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
		{
			TextureCoordinate[i] = FVector2f::ZeroVector;
		}
	}
	FDreamUIMeshVertex(
		const FVector3f& InPosition
		, const FVector3f& InTangentX
		, const FVector3f& InTangentZ
		, const FColor& InColor
		, const FVector2f& InUV0
		, const FVector2f& InUV1
		, const FVector2f& InUV2
		, const FVector2f& InUV3
	)
	{
		Position = InPosition;
		TangentX = InTangentX;
		TangentZ = InTangentZ;
		Color = InColor;
		TextureCoordinate[0] = InUV0;
		TextureCoordinate[1] = InUV1;
		TextureCoordinate[2] = InUV2;
		TextureCoordinate[3] = InUV3;
	}

	FVector3f Position;
	FColor Color;
	FVector2f TextureCoordinate[LEXUI_VERTEX_TEXCOORDINATE_COUNT];
	FPackedNormal TangentX;
	FPackedNormal TangentZ;
	/**
	 * A painted glyph quad's place in its gradient's boxes (FDreamTextPaints, DreamTextQuadCode's slots): x from the box's
	 * left edge (0) to its right (1), y from its top (0) down to its bottom (1). (0, 0) on everything else, which nothing
	 * reads -- every constructor zeroes it, and whatever grows a vertex array without constructing writes it. Vertex
	 * attribute LEXUI_VERTEX_UV4_ATTRIBUTE; TexCoord(4) to a material.
	 */
	FVector2f UV4 = FVector2f(0.0f, 0.0f);

	FVector3f GetTangentY() const
	{
		return FVector3f(GenerateYAxis(TangentX, TangentZ));
	};
};
static_assert(sizeof(FDreamUIMeshVertex) == 64, "FDreamUIMeshVertex is 64 bytes: the vertex declaration, the uploads and the byte compares of geometry assume no padding");

class DREAMGUIRENDERER_API FDreamUIMeshVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList)override;
	virtual void ReleaseRHI()override;
	/**
	 * The elements the declaration is made of, one per field of FDreamUIMeshVertex in stream 0, each at the attribute
	 * index the vertex shaders read it as (UV4 at LEXUI_VERTEX_UV4_ATTRIBUTE). No RHI work: InitRHI makes the declaration
	 * from these.
	 */
	static void MakeElements(FVertexDeclarationElementList& OutElements);
};
DREAMGUIRENDERER_API FVertexDeclarationRHIRef& GetDreamUIMeshVertexDeclaration();

class DREAMGUIRENDERER_API FDreamUIMeshVertexBuffer : public FVertexBuffer
{
public:
	bool bAutoClearVerticesAfterInitRHI = true;//clear Vertices after InitRHI
	TArray<FDreamUIMeshVertex> Vertices;
	virtual void InitRHI(FRHICommandListBase& RHICmdList)override;
};

