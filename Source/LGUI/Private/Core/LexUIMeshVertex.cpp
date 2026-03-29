// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIMeshVertex.h"
#include "RHIResourceUtils.h"
#include "RHI.h"


void FLexUIMeshVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint32 Stride = sizeof(FLexUIMeshVertex);
	uint16 Index = 0;
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIMeshVertex, Position), VET_Float3, Index++, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIMeshVertex, Color), VET_Color, Index++, Stride));
	for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
	{
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIMeshVertex, TextureCoordinate) + i * 8, VET_Float2, Index++, Stride));
	}
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIMeshVertex, TangentX), VET_PackedNormal, Index++, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIMeshVertex, TangentZ), VET_PackedNormal, Index++, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FLexUIMeshVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FLexUIMeshVertexDeclaration> GLexUIVertexDeclaration;
FVertexDeclarationRHIRef& GetLexUIMeshVertexDeclaration()
{
	return GLexUIVertexDeclaration.VertexDeclarationRHI;
}

void FLexUIMeshVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
		RHICmdList, TEXT("LexUIVertexBuffer"), EBufferUsageFlags::Dynamic, MakeConstArrayView(Vertices)
		);
	if (bAutoClearVerticesAfterInitRHI)
	{
		Vertices.Empty();
	}
}
