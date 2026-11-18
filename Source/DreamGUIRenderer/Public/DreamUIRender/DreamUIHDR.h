// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FRenderTarget;
class FGlobalShaderMap;

namespace DreamUIHDR
{
    enum class EEncoding : uint8 { None, PQ, ScRGB };
    /** Display-referred working space: linear sRGB, with 1.0 at the UI's paper white. */
    struct FOutput
    {
        EEncoding Encoding=EEncoding::None;
        float PaperWhiteNits=203.f;
        bool IsActive() const { return Encoding!=EEncoding::None; }
    };
    /** Only active display HDR formats; floating-point capture/EXR targets keep their existing semantics. */
    DREAMGUIRENDERER_API FOutput FromTarget(const FRenderTarget& Target);
    /** Equal-sized textures. Decode the scene before UI drawing; encode after blending and MSAA resolve.
     * HDR10 uses PQ/Rec2020, scRGB uses linear sRGB at the native 80-nit reference. Alpha is preserved. */
    DREAMGUIRENDERER_API void AddTransferPass(FRDGBuilder& GraphBuilder,const FGlobalShaderMap* ShaderMap,
        FRDGTextureRef Source,FRDGTextureRef Destination,const FIntRect& Rect,const FOutput& Output,bool ToWorking);
}
