// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/LexUIRender/LexUIVertex.h"
#include "RHI.h"


void FLexUIPostProcessVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint16 Stride = sizeof(FLexUIPostProcessVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIPostProcessVertex, Position), VET_Float3, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIPostProcessVertex, TextureCoordinate0), VET_Float2, 1, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIPostProcessVertex, TextureCoordinate1), VET_Float2, 2, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FLexUIPostProcessVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FLexUIPostProcessVertexDeclaration> GLGUIPostProcessVertexDeclaration;
FVertexDeclarationRHIRef& GetLexUIPostProcessVertexDeclaration()
{
	return GLGUIPostProcessVertexDeclaration.VertexDeclarationRHI;
}




void FLexUIPostProcessCopyMeshRegionVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint16 Stride = sizeof(FLexUIPostProcessCopyMeshRegionVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIPostProcessCopyMeshRegionVertex, ScreenPosition), VET_Float3, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIPostProcessCopyMeshRegionVertex, LocalPosition), VET_Float3, 1, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FLexUIPostProcessCopyMeshRegionVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FLexUIPostProcessCopyMeshRegionVertexDeclaration> GLGUIPostProcessCopyMeshRegionVertexDeclaration;
FVertexDeclarationRHIRef& GetLexUIPostProcessCopyMeshRegionVertexDeclaration()
{
	return GLGUIPostProcessCopyMeshRegionVertexDeclaration.VertexDeclarationRHI;
}



void FLexUIHelperLineVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint16 Stride = sizeof(FLexUIHelperLineVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIHelperLineVertex, Position), VET_Float3, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLexUIHelperLineVertex, Color), VET_Color, 1, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FLexUIHelperLineVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FLexUIHelperLineVertexDeclaration> GLexUIHelperLineVertexDeclaration;
FVertexDeclarationRHIRef& GetLexUIHelperLineVertexDeclaration()
{
	return GLexUIHelperLineVertexDeclaration.VertexDeclarationRHI;
}
