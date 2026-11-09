// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamNiagaraSystem;
class UMaterial;
class UMaterialInterface;

/** What a conversion made, and what of the particle material it could not make the UI's. */
struct FDreamNiagaraMaterialConversion
{
	/** The UI material, or null when nothing was made (Error says why). */
	UMaterialInterface* Result = nullptr;
	UMaterialInterface* Source = nullptr;
	/** Each node replaced, as "ParticleColor -> vertex colour". */
	TArray<FString> Replaced;
	/** Each node left as it was, with why: it reads what a UI vertex does not carry, so it draws with a default. */
	TArray<FString> Unconverted;
	FString Error;
};

/**
 * A particle material made a DreamGUI UI material: the graph copied, the particle's inputs read from where DreamGUINiagara
 * writes them (DreamNiagaraVertexLayout.h) -- Particle Color from the vertex colour and its scale, Particle SubUV from the two
 * frames and their blend, Dynamic Parameter from the spare texture coordinates, texture coordinate 1 from UV2 -- and the
 * opacity clipped and masked by DreamGUI's clip (MF_DreamWidgetProperty into MF_DreamUI_Clip). Surface, unlit, two-sided,
 * the source's blend kept (an opaque one becomes translucent). A material instance is converted by converting its parent
 * material and parenting a copy of the instance to it.
 */
class DREAMGUINIAGARAEDITOR_API FDreamNiagaraMaterialConverter
{
public:
	/** Beside the source, named after it with _DreamUI. */
	static FString DefaultPackageName(const UMaterialInterface* InParticleMaterial);

	/** InParticleMaterial converted into a new asset in package InPackageName, which an existing one there is replaced by. Not saved. */
	static FDreamNiagaraMaterialConversion Convert(UMaterialInterface* InParticleMaterial, const FString& InPackageName);

	/**
	 * Every particle material of InVisual's drawn renderers converted beside its source (once each), saved when bInSave, and
	 * put into InVisual's material map; the visual's default shading is then Material.
	 */
	static TArray<FDreamNiagaraMaterialConversion> ConvertForVisual(UDreamNiagaraSystem* InVisual, bool bInSave);

	/** Saves InAsset's package to its file. */
	static bool Save(UObject* InAsset);
};
