// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;

/**
 * Where an angle dial (DreamGUI_Editor="AngleWheel") finds the style's GlobalLightAngle when its effect uses the global
 * light. In a widget's details the effect sits inside its style, and the dial finds it by walking up the handles; a view
 * that shows an effect apart from its style -- a dialog editing a working copy, the global light on another page -- adds
 * a resolver here for as long as it is open, and the dial asks it when the walk finds nothing.
 */
namespace DreamAngleGlobalLight
{
	/** Given the angle row's handle, the GlobalLightAngle handle it turns, or null when the resolver does not know the angle. */
	using FResolver = TFunction<TSharedPtr<IPropertyHandle>(const TSharedRef<IPropertyHandle>& /*Angle*/)>;

	/** Adds InResolver; the id returned removes it. */
	DREAMGUIEDITOR_API int32 AddResolver(FResolver InResolver);
	DREAMGUIEDITOR_API void RemoveResolver(int32 InId);

	/** The GlobalLightAngle InAngle turns when its effect uses the global light: up its handles, then by the resolvers. */
	DREAMGUIEDITOR_API TSharedPtr<IPropertyHandle> Find(const TSharedRef<IPropertyHandle>& InAngle);
}
