// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class IDreamDesignerHandleProvider;

/** A rect block's two gradients. */
enum class EDreamRectBlockGradientSlot : uint8
{
	Body,
	Border,
};

/** The viewport handles of a rect block's gradients (Designer/DreamDesignerHandles.h), which the editor module registers. */
namespace DreamDesignerGradientHandles
{
	DREAMGUIEDITOR_API extern const FName ProviderName;
	DREAMGUIEDITOR_API TSharedRef<IDreamDesignerHandleProvider> MakeProvider();

	/**
	 * Which gradient the handles edit while a rect block has both on: the body's until the border's is asked for (its row's
	 * button in the details panel). One that is off gives way to the other.
	 */
	DREAMGUIEDITOR_API void SetPreferredSlot(EDreamRectBlockGradientSlot InSlot);
	DREAMGUIEDITOR_API EDreamRectBlockGradientSlot GetPreferredSlot();
}
