// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class IDetailLayoutBuilder;
class IPropertyUtilities;
class UDreamWidget;

/**
 * The Layer category at the top of every widget's details: what a layer is in an image editor, in one place.
 *
 * Opacity, blend mode, clipping to the layer below, the mask, the layer style and the layer group each live on a
 * different object -- the widget, its visual, a component -- and each used to be found where that object put it: the
 * mask under Appearance > Clipping behind a Clipping switch, the blend mode in the visual's own rows, the style and the
 * group only by selecting the component in the list. Here they are rows of one category, and every one writes through a
 * property handle or the designer's own add, so it is mirrored to the template and written to a .dui like any other edit.
 *
 * Below them, the effects on the widget, each with an eye that turns it off and on and a name that opens its own
 * properties.
 */
namespace DreamWidgetLayerSection
{
	/** The Layer category for InWidgets, the details panel's selection. */
	DREAMGUIEDITOR_API void Build(IDetailLayoutBuilder& InDetailBuilder, const TArray<TWeakObjectPtr<UDreamWidget>>& InWidgets, TSharedPtr<IPropertyUtilities> InUtilities);

	/** Whether a widget's Visual category leaves InPropertyName to the Layer category. */
	DREAMGUIEDITOR_API bool ShowsVisualProperty(FName InPropertyName);
}
