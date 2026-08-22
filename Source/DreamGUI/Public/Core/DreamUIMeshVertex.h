// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "PackedNormal.h"
#include "RenderMath.h"

#define LEXUI_VERTEX_TEXCOORDINATE_COUNT 4

struct DREAMGUI_API FDreamUIMeshVertex
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

	FVector3f GetTangentY() const
	{
		return FVector3f(GenerateYAxis(TangentX, TangentZ));
	};
};

class DREAMGUI_API FDreamUIMeshVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList)override;
	virtual void ReleaseRHI()override;
};
DREAMGUI_API FVertexDeclarationRHIRef& GetDreamUIMeshVertexDeclaration();

class FDreamUIMeshVertexBuffer : public FVertexBuffer
{
public:
	bool bAutoClearVerticesAfterInitRHI = true;//clear Vertices after InitRHI
	TArray<FDreamUIMeshVertex> Vertices;
	virtual void InitRHI(FRHICommandListBase& RHICmdList)override;
};

