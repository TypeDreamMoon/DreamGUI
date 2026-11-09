// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Designer/DreamDesignerHandles.h"
#include "Templates/SharedPointer.h"

/** A rect block's two gradients. */
enum class EDreamRectBlockGradientSlot : uint8
{
	Body,
	Border,
};

/**
 * The viewport handles of a rect block's gradients (Designer/DreamDesignerHandles.h): the kind its BodyGradient and
 * BorderGradient are tagged with, meta=(DreamGUI_Handle="Gradient"), which the editor module registers. Each tagged
 * gradient has a provider of its own, named "Gradient.BodyGradient" or "Gradient.BorderGradient".
 */
namespace DreamDesignerGradientHandles
{
	DREAMGUIEDITOR_API extern const FName KindName;
	/** The kind's factory: a provider for a rect block's BodyGradient or BorderGradient, none for anything else. */
	DREAMGUIEDITOR_API FDreamDesignerHandleKindFactory MakeKindFactory();

	/**
	 * Which gradient the handles edit while a rect block has both on: the body's until the border's is asked for (its row's
	 * button in the details panel). One that is off gives way to the other.
	 */
	DREAMGUIEDITOR_API void SetPreferredSlot(EDreamRectBlockGradientSlot InSlot);
	DREAMGUIEDITOR_API EDreamRectBlockGradientSlot GetPreferredSlot();
}
