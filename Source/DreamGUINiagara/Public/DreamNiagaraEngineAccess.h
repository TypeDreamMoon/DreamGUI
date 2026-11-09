// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamNiagaraParticles.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "UObject/WeakObjectPtr.h"

class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraRendererProperties;
class UNiagaraSystem;
class UTexture;

/** What DreamGUI makes of a renderer. */
enum class EDreamNiagaraRendererKind : uint8
{
	Sprite,
	Ribbon,
	/** Not drawn: a mesh, light or component renderer, or any renderer of an emitter that is not simulated on the CPU. */
	Unsupported,
};

/** One renderer of a system's, as DreamGUI draws it -- or why it does not. Made from the asset alone. */
struct FDreamNiagaraRendererInfo
{
	int32 EmitterIndex = INDEX_NONE;
	FName EmitterName;
	/** Among its emitter's renderers. */
	int32 RendererIndex = INDEX_NONE;
	/** The renderer's class, for a report. */
	FString RendererType;
	EDreamNiagaraRendererKind Kind = EDreamNiagaraRendererKind::Unsupported;
	/** What keeps it from being drawn (an unsupported renderer), or what of it is not drawn as Niagara draws it. */
	TArray<FText> Notes;
	bool bLocalSpace = false;
	/** The renderer's own sort order: lower is drawn first, as Niagara orders renderers. */
	int32 SortOrderHint = 0;
	/** The renderer's material, and what the built-in shader takes from it: its texture and its blend. */
	TWeakObjectPtr<UMaterialInterface> Material;
	TWeakObjectPtr<UTexture> InferredTexture;
	EDreamUIBlendMode InferredBlendMode = EDreamUIBlendMode::Alpha;
	FDreamNiagaraSpriteSettings Sprite;
	FDreamNiagaraRibbonSettings Ribbon;
	TWeakObjectPtr<const UNiagaraRendererProperties> Properties;

	bool IsDrawn() const { return Kind != EDreamNiagaraRendererKind::Unsupported; }
};

/** The types a user parameter DreamGUI writes may be declared as. */
enum class EDreamNiagaraParameterType : uint8
{
	None,
	Float,
	Int,
	Bool,
	Vector2,
	Vector3,
	Position,
	Vector4,
	Color,
};

/**
 * Every call DreamGUI makes into Niagara past its components' public interface: the system's emitters and renderers, the
 * instance's tick and its particle data. Kept here, together, so that an engine version that moves them is answered here.
 */
namespace DreamNiagaraEngine
{
	/** Each renderer of each emitter, drawn or not, in the order Niagara keeps them. */
	DREAMGUINIAGARA_API void DescribeSystem(const UNiagaraSystem* InSystem, TArray<FDreamNiagaraRendererInfo>& OutRenderers);

	/**
	 * What the built-in shader takes from a particle material: the texture parameter named Texture, else the first texture
	 * the material samples; Additive for an additive material, Multiply for a modulating one, Alpha for the rest.
	 */
	DREAMGUINIAGARA_API void InferShading(const UMaterialInterface* InMaterial, UTexture*& OutTexture, EDreamUIBlendMode& OutBlendMode);

	/** The system and its scripts are compiled and can run. */
	DREAMGUINIAGARA_API bool IsReadyToRun(const UNiagaraSystem* InSystem);

	/** Waits for the component's instance to finish this frame's tick, wherever it ran, so that its particle data is final. */
	DREAMGUINIAGARA_API void FinishTick(UNiagaraComponent* InComponent);

	/** The particles InRenderer draws of InComponent's instance, added to OutParticles; how many were added. After FinishTick. */
	DREAMGUINIAGARA_API int32 ReadSprites(UNiagaraComponent* InComponent, const FDreamNiagaraRendererInfo& InRenderer, TArray<FDreamNiagaraSpriteParticle>& OutParticles);

	/**
	 * The points InRenderer draws of InComponent's instance, added to OutParticles. InKeySalt is mixed into every ribbon key, so
	 * that the ribbons of two instances read into one array stay apart.
	 */
	DREAMGUINIAGARA_API int32 ReadRibbons(UNiagaraComponent* InComponent, const FDreamNiagaraRendererInfo& InRenderer, int64 InKeySalt, TArray<FDreamNiagaraRibbonParticle>& OutParticles);

	/** The type InSystem declares the user parameter InName as (without the "User." namespace), None when it declares no such parameter. */
	DREAMGUINIAGARA_API EDreamNiagaraParameterType FindUserParameter(const UNiagaraSystem* InSystem, FName InName);

	/** How many particles InComponent's instance holds for every emitter, for a test or a report. */
	DREAMGUINIAGARA_API int32 CountParticles(UNiagaraComponent* InComponent);
}
