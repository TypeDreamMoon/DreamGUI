// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamPointerEventData.h"
#include "DreamUINestedSurface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUINestedSurface : public UInterface
{
	GENERATED_BODY()
};

/**
 * A component that shows a canvas of its own on the actor it sits on -- a render target drawn onto a mesh --
 * and lets pointers reach into it.
 *
 * When a pointer's ray lands on such an actor, the input system asks the surface where on its own canvas the
 * ray lands, and a widget there is what the pointer is over: the one state machine, dispatch and broadcast
 * serve it as they serve any widget, and every pointer keeps its own state inside the surface as outside it.
 * A surface used to run a pipeline of its own with one synthesised pointer for every pointer and player, no
 * broadcasts and no double click.
 */
class DREAMGUIINPUT_API IDreamUINestedSurface
{
	GENERATED_BODY()
public:
	/**
	 * Carry InOuterHit -- a world hit on this surface's actor -- into the surface's own canvas.
	 * @param InPointer		The pointer whose ray it is.
	 * @param OutInnerHit	The hit on the surface's canvas: its ray the canvas's own, its raycaster the surface.
	 * @return	True when the ray reaches the surface's canvas at all. OutInnerHit then names a widget when it
	 *			lands on one; when it lands on nothing the world hit stands, and the actor is what the pointer is over.
	 */
	virtual bool ResolveNestedHit(const FDreamUIHitResultContainer& InOuterHit, UDreamPointerEventData* InPointer,
		FDreamUIHitResultContainer& OutInnerHit) = 0;
};
