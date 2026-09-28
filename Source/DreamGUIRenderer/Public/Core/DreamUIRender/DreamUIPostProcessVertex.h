// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
// FVertexDeclarationRHIRef is TRefCountPtr<FRHIVertexDeclaration>, and this class's implicit
// operator= instantiates it -- which needs the pointee complete, not forward-declared. Inside a
// unity blob some neighbour always had it.
#include "RHIResources.h"

struct DREAMGUIRENDERER_API FDreamUIPostProcessVertex
{
	FVector3f Position;
	FVector2f TextureCoordinate0;
	FVector2f TextureCoordinate1;

	FDreamUIPostProcessVertex(FVector3f InPosition, FVector2f InTextureCoordinate0)
	{
		Position = InPosition;
		TextureCoordinate0 = InTextureCoordinate0;
	}
	FDreamUIPostProcessVertex(FVector3f InPosition, FVector2f InTextureCoordinate0, FVector2f InTextureCoordinate1)
	{
		Position = InPosition;
		TextureCoordinate0 = InTextureCoordinate0;
		TextureCoordinate1 = InTextureCoordinate1;
	}
};

class DREAMGUIRENDERER_API FDreamUIPostProcessVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};
DREAMGUIRENDERER_API FVertexDeclarationRHIRef& GetDreamUIPostProcessVertexDeclaration();




struct DREAMGUIRENDERER_API FDreamUIPostProcessCopyMeshRegionVertex
{
	FVector3f ScreenPosition;
	FVector3f LocalPosition;

	FDreamUIPostProcessCopyMeshRegionVertex(FVector3f InScreenPosition, FVector3f InLocalPosition)
	{
		ScreenPosition = InScreenPosition;
		LocalPosition = InLocalPosition;
	}
};

class DREAMGUIRENDERER_API FDreamUIPostProcessCopyMeshRegionVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};
DREAMGUIRENDERER_API FVertexDeclarationRHIRef& GetDreamUIPostProcessCopyMeshRegionVertexDeclaration();

