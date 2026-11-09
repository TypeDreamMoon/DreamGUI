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
 * gradient has a provider of its own -- "Gradient.BodyGradient", "Gradient.BorderGradient" -- so the body's and the
 * border's are two kinds of handles, the one shown chosen as any is (FDreamDesignerHandles::SetActive, Tab in the viewport).
 */
namespace DreamDesignerGradientHandles
{
	DREAMGUIEDITOR_API extern const FName KindName;
	/** The kind's factory: a provider for a rect block's BodyGradient or BorderGradient, none for anything else. */
	DREAMGUIEDITOR_API FDreamDesignerHandleKindFactory MakeKindFactory();
	/** The name a slot's provider goes by: "Gradient.BodyGradient" or "Gradient.BorderGradient". */
	DREAMGUIEDITOR_API FName GetProviderName(EDreamRectBlockGradientSlot InSlot);
}
