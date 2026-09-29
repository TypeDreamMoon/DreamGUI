// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamCanvas.h"   // EDreamCanvasScaleMode

/**
 * The small vocabulary every part of the driver shares: how a rig is built, and the words input
 * steps use that the runtime has no type for. Kept apart from the rig and the sequence so a piece
 * that only needs a word -- a game-host adapter, a PIE rig -- does not pull in either of them.
 */

/** Where the rig's input enters the pipeline. */
enum class EDreamRigInputHost : uint8
{
	/** Pointer, buttons, wheel, navigation and keys go straight into UDreamDriverInputModule and the edited text field (today's rig). */
	ModuleOnly,
	/** A real ADreamStandaloneInputEventSystemActor whose module is a UDreamDriverInputModule; buttons, wheel, navigation, touch and keys go through the PlayerController's input stack. */
	StandaloneActor,
	/** The same with ADreamEnhancedInputEventSystemActor and Enhanced Input mapping contexts. */
	EnhancedActor,
};

/** One phase of a finger's contact. */
enum class EDreamDriverTouchPhase : uint8
{
	Began,
	Moved,
	Ended,
};

/** How a rig is built. Every default reproduces today's rig except bWithGameInstance. */
struct FDreamRigOptions
{
	FIntPoint ViewportSize = FIntPoint(1280, 720);
	/** The world belongs to a UGameInstance, so GameInstance subsystems (the tween manager) exist and the pump ticks them. false = a bare UWorld::CreateWorld world, exactly as before. */
	bool bWithGameInstance = true;
	/** Root canvas scaling. Unset = leave the canvas's own default. */
	TOptional<EDreamCanvasScaleMode> CanvasScaleMode;
	FVector2D ReferenceResolution = FVector2D(1280.0, 720.0);
	float MatchFromWidthToHeight = 1.0f;
	EDreamRigInputHost InputHost = EDreamRigInputHost::ModuleOnly;
};
