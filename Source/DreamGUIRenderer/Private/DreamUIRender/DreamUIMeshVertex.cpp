// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIRender/DreamUIMeshVertex.h"
#include "RHIResourceUtils.h"
#include "RHI.h"


void FDreamUIMeshVertexDeclaration::MakeElements(FVertexDeclarationElementList& OutElements)
{
	OutElements.Reset();
	uint32 Stride = sizeof(FDreamUIMeshVertex);
	uint16 Index = 0;
	OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, Position), VET_Float3, Index++, Stride));
	OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, Color), VET_Color, Index++, Stride));
	for (int i = 0; i < LEXUI_VERTEX_TEXCOORDINATE_COUNT; i++)
	{
		OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TextureCoordinate) + i * 8, VET_Float2, Index++, Stride));
	}
	OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TangentX), VET_PackedNormal, Index++, Stride));
	OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, TangentZ), VET_PackedNormal, Index++, Stride));
	// Last, after the tangents, so that every attribute a shader read before kept its number: ATTRIBUTE8 to both vertex
	// shaders (DreamUIBase.usf, DreamUIShader.usf).
	OutElements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIMeshVertex, UV4), VET_Float2, LEXUI_VERTEX_UV4_ATTRIBUTE, Stride));
}

void FDreamUIMeshVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	MakeElements(Elements);
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
