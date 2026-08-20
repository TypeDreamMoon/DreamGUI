// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIMeshVertex.h"
#include "RHIResourceUtils.h"
#include "RHI.h"


void FDreamUIMeshVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint32 Stride = sizeof(FDreamUIMeshVertex);
	uint16 Index = 0;
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, Position), VET_Float3, Index++, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, Color), VET_Color, Index++, Stride));
	for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
	{
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TextureCoordinate) + i * 8, VET_Float2, Index++, Stride));
	}
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TangentX), VET_PackedNormal, Index++, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TangentZ), VET_PackedNormal, Index++, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FDreamUIMeshVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FDreamUIMeshVertexDeclaration> GDreamUIVertexDeclaration;
FVertexDeclarationRHIRef& GetDreamUIMeshVertexDeclaration()
{
	return GDreamUIVertexDeclaration.VertexDeclarationRHI;
}

void FDreamUIMeshVertexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	VertexBufferRHI = UE::RHIResourceUtils::CreateVertexBufferFromArray(
		RHICmdList, TEXT("DreamUIVertexBuffer"), EBufferUsageFlags::Dynamic, MakeConstArrayView(Vertices)
		);
	if (bAutoClearVerticesAfterInitRHI)
	{
		Vertices.Empty();
	}
}
