// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"

/**
 * Asking about the world a DreamGUI object lives in, when it may not live in one.
 *
 * A widget can genuinely have NO world, and both ways of getting there are ordinary rather than
 * exotic:
 *
 *   - a headless test, which builds a tree under the transient package and never makes a world;
 *   - a Blueprint's AUTHORING tree, whose outer is the Blueprint rather than a world -- every
 *     DreamUI widget class carries one, so this is the state of every widget in the designer that
 *     is not the preview copy.
 *
 * The plugin inherited `GetWorld()->` written as a bare dereference in dozens of places, from a
 * time when a UI component was only ever a component on an actor in a level. Each of them is a
 * crash waiting for the first caller that reaches it from one of the two places above;
 * UDreamRingMenu disabling one of its own parts was simply the first control to do so.
 *
 * WHICH ONES MATTER is a question about the CALL PATH, not about the expression. Most of the naked
 * ones live in input, raycasting and interaction, which cannot run without a world in the first
 * place; a world subsystem's GetWorld is null-free by construction. The hazardous ones are the
 * code a widget reaches while being built, registered, laid out or having its geometry rebuilt --
 * that is what an authoring tree and a headless test both do, and neither has a world.
 *
 * And it is not only `IsGameWorld`. The first pass over this grepped for `IsGameWorld()` and
 * `WorldType` and came back clean, while `GetWorld()->TimeSeconds` sat on the mesh-modifier
 * geometry path in three places. Grep for `GetWorld()->`, then judge by call path.
 *
 * THE RULE, established when the first two were fixed and applied uniformly here: a missing world
 * IS NOT A GAME WORLD, so it takes the same branch edit mode takes. That is the branch that can
 * actually work without a world -- the game branch invariably wants a subsystem, a player, or a
 * tick that a worldless object has no way to reach -- and for state changes it is the branch that
 * delivers, where the game branch would defer to a lifecycle event that is never going to fire.
 *
 * Uniformity is the point. A helper that answers the question is one place to change if the rule
 * ever needs to distinguish "no world" from "editor world"; forty hand-written null checks are not,
 * which is how the plugin came to have forty sites that answer it by crashing.
 */
namespace DreamUI
{
	/**
	 * The world InObject lives in, or null.
	 *
	 * Exists so the null case is spelled once. Callers that only need the question below answered
	 * should ask it instead -- reaching for the world in order to test it is how the bare
	 * dereferences got written.
	 */
	FORCEINLINE const UWorld* GetWorldSafe(const UObject* InObject)
	{
		return InObject != nullptr ? InObject->GetWorld() : nullptr;
	}

	/**
	 * Whether InObject is in a world that is playing -- PIE or a cooked game.
	 *
	 * False for an editor world, an editor preview world, AND for no world at all. See the rule in
	 * this header's comment: "no world" is the edit-mode answer, not a third case for every caller
	 * to handle.
	 */
	FORCEINLINE bool IsGameWorld(const UObject* InObject)
	{
		const UWorld* World = GetWorldSafe(InObject);
		return World != nullptr && World->IsGameWorld();
	}

	/**
	 * InObject's world type, or EWorldType::None when it has no world.
	 *
	 * For the sites that ask something narrower than IsGameWorld -- "is this the editor world, as
	 * opposed to an editor PREVIEW world", which is a distinction IsGameWorld cannot draw. None is
	 * the honest answer for a worldless object and compares equal to no other case, so an existing
	 * `== EWorldType::Editor` test keeps its meaning and stops crashing.
	 */
	FORCEINLINE EWorldType::Type GetWorldType(const UObject* InObject)
	{
		const UWorld* World = GetWorldSafe(InObject);
		return World != nullptr ? World->WorldType.GetValue() : EWorldType::None;
	}
}
