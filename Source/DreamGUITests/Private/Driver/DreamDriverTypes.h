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
	/**
	 * The world's Slate input source (UDreamUIInputSubsystem::SetSlateInputSourceEnabled), the preset actors standing down:
	 * keys reach it as FKeyEvents through FDreamUISlateInputSource::HandleKeyDownEvent and HandleKeyUpEvent, pointers as
	 * FPointerEvents, characters through UDreamUIInputSubsystem::HandleViewportCharacter, as a game viewport's would. A
	 * headless rig gives the source its test mappers (the rig's viewport, keyboard focus on it, Slate user 0 is player 0).
	 */
	SlateSource,
};

/** Whether InHost delivers input through a player controller and one of the preset input actors. */
inline bool IsActorInputHost(EDreamRigInputHost InHost)
{
	return InHost == EDreamRigInputHost::StandaloneActor || InHost == EDreamRigInputHost::EnhancedActor;
}

/** Modifier keys a driver key step holds around its key (FDreamDriverSequence::Key). */
enum class EDreamDriverModifierKeys : uint8
{
	None = 0,
	Shift = 1 << 0,
	Ctrl = 1 << 1,
	Alt = 1 << 2,
	Cmd = 1 << 3,
};
ENUM_CLASS_FLAGS(EDreamDriverModifierKeys);

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
