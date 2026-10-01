// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIRender/DreamUIBaseShaders.h"

#include "Engine/Texture.h"
#include "RenderingThread.h"
#include "TextureResource.h"

FDreamUIBuiltInTextures FDreamUIBuiltInDrawParams::GetTexturesForRenderCommand() const
{
	// Called where the game thread's objects hold still -- the game thread itself, or the end-of-frame update that
	// builds a proxy -- which is what resolving a weak pointer asks for.
	FDreamUIBuiltInTextures Textures;
	Textures.Main = MainTexture.Get();
	Textures.Font = FontTexture.Get();
	Textures.WidgetData = WidgetDataTexture.Get();
	Textures.ClipData = ClipDataTexture.Get();
	Textures.RenderLayerTable = RenderLayerTable.Get();
	Textures.RectBlockData = RectBlockData.Get();
	return Textures;
}

void FDreamUIBuiltInDrawParams::ResolveTextures_RenderThread(const FDreamUIBuiltInTextures& InTextures)
{
	check(IsInRenderingThread());
	// A texture's reference is made and released on the render thread, and its resource is the render thread's view
	// of it: both are read here, where they are coherent, and never on the game thread.
	auto Reference = [](const UTexture* InTexture) -> FTextureReferenceRHIRef
	{
		return InTexture != nullptr ? InTexture->TextureReference.TextureReferenceRHI : FTextureReferenceRHIRef();
	};
	auto Sampler = [](const UTexture* InTexture) -> FSamplerStateRHIRef
	{
		const FTextureResource* Resource = InTexture != nullptr ? InTexture->GetResource() : nullptr;
		return Resource != nullptr ? Resource->SamplerStateRHI : FSamplerStateRHIRef();
	};
	MainTextureRHI = Reference(InTextures.Main);
	MainSamplerRHI = Sampler(InTextures.Main);
	FontTextureRHI = Reference(InTextures.Font);
	FontSamplerRHI = Sampler(InTextures.Font);
	WidgetDataTextureRHI = Reference(InTextures.WidgetData);
	ClipDataTextureRHI = Reference(InTextures.ClipData);
	RenderLayerTableRHI = Reference(InTextures.RenderLayerTable);
	RectBlockDataRHI = Reference(InTextures.RectBlockData);
}

IMPLEMENT_GLOBAL_SHADER(FDreamUIBaseVS, "/Plugin/DreamGUI/Private/DreamUIBase.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDreamUIBasePS, "/Plugin/DreamGUI/Private/DreamUIBase.usf", "MainPS", SF_Pixel);
