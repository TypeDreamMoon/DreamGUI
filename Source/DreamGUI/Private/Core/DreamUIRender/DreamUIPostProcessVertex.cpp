// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamUIRender/DreamUIPostProcessVertex.h"
#include "RHI.h"


void FDreamUIPostProcessVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint16 Stride = sizeof(FDreamUIPostProcessVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIPostProcessVertex, Position), VET_Float3, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIPostProcessVertex, TextureCoordinate0), VET_Float2, 1, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIPostProcessVertex, TextureCoordinate1), VET_Float2, 2, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FDreamUIPostProcessVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FDreamUIPostProcessVertexDeclaration> GDreamGUIPostProcessVertexDeclaration;
FVertexDeclarationRHIRef& GetDreamUIPostProcessVertexDeclaration()
{
	return GDreamGUIPostProcessVertexDeclaration.VertexDeclarationRHI;
}




void FDreamUIPostProcessCopyMeshRegionVertexDeclaration::InitRHI(FRHICommandListBase& RHICmdList)
{
	FVertexDeclarationElementList Elements;
	uint16 Stride = sizeof(FDreamUIPostProcessCopyMeshRegionVertex);
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIPostProcessCopyMeshRegionVertex, ScreenPosition), VET_Float3, 0, Stride));
	Elements.Add(FVertexElement(0, STRUCT_OFFSET(FDreamUIPostProcessCopyMeshRegionVertex, LocalPosition), VET_Float3, 1, Stride));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
}
void FDreamUIPostProcessCopyMeshRegionVertexDeclaration::ReleaseRHI()
{
	VertexDeclarationRHI.SafeRelease();
}
TGlobalResource<FDreamUIPostProcessCopyMeshRegionVertexDeclaration> GDreamGUIPostProcessCopyMeshRegionVertexDeclaration;
FVertexDeclarationRHIRef& GetDreamUIPostProcessCopyMeshRegionVertexDeclaration()
{
	return GDreamGUIPostProcessCopyMeshRegionVertexDeclaration.VertexDeclarationRHI;
}

