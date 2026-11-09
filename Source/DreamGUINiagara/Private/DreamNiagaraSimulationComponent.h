// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraComponent.h"
#include "DreamNiagaraSimulationComponent.generated.h"

/**
 * What a NiagaraSystem visual simulates its system with: a Niagara component the scene is given no proxy for, so nothing of
 * it is ever drawn in the world. DreamGUI draws its particles, on the canvas.
 *
 * Turning a plain component's rendering off does not keep it out of the picture: Niagara hands that flag to the component's
 * proxy only as the component ticks, and the proxy made when the component is activated draws the frame before -- a burst's
 * whole system, lit, in the world behind the UI.
 */
UCLASS(Transient, NotBlueprintable, NotBlueprintType, HideDropdown)
class UDreamNiagaraSimulationComponent : public UNiagaraComponent
{
	GENERATED_BODY()

public:
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }
};
