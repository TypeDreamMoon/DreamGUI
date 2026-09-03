// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DreamUI
{
	/**
	 * Registers Sequencer custom accessors for the widget properties that are vectors
	 * (RenderTranslation, RenderScale, RelativeLocation, RelativeScale, PerspectiveOrigin, and
	 * the presenter's WidgetOffset).
	 *
	 * Every Interp property on UDreamWidget has a native setter, which takes it off Sequencer's
	 * fast path and onto the reflective one: a property lookup by path, a variant-type dispatch,
	 * and a CallSetter through the generated wrapper, per bound object, per frame. A custom
	 * accessor is consulted before the setter test, so it bypasses all of that and goes straight
	 * to the widget's setter -- the same arrangement UMG uses for UWidget::RenderTransform.
	 *
	 * It is an optimization and a bisecting aid, not a correctness fix: -DreamGUINoVectorAccessors
	 * puts the vector tracks back on the reflective path, which is how to tell an engine-side
	 * regression from one of these. (The 2026-09-03 "vector tracks never write on an instanced
	 * widget" was not this path at all; see UDreamWidgetAnimation::PostInitProperties.)
	 * Idempotent; the module registers at engine init and playback calls it again as a guard.
	 */
	DREAMGUI_API void EnsureMovieScenePropertyAccessorsRegistered();
}
