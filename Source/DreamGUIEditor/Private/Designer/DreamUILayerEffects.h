// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UDreamWidget;
struct FSlateBrush;

/** What a layer effect does to its widget, which is the mark the hierarchy shows for it. */
enum class EDreamUILayerEffectKind : uint8
{
	Style,
	Group,
	Mask,
	Blend,
	Effect,
	Warp,
	Particles,
	Count,
};

/**
 * One layer effect on a widget, as the editor shows it: in the details panel's layer section, and as a mark on the
 * widget's hierarchy row.
 *
 * Object is what holds the effect -- a component, the widget's visual, or the widget itself for what is a property of
 * the widget (its mask, clipping to the layer below). SwitchPath is the property that turns the effect off and on,
 * dotted into structs, on Object; empty when it has none, because the effect IS its being there (a blend mode, a mask).
 */
struct DREAMGUIEDITOR_API FDreamUILayerEffect
{
	EDreamUILayerEffectKind Kind = EDreamUILayerEffectKind::Effect;
	TWeakObjectPtr<UObject> Object;
	FText Name;
	/** One line on what it is set to, for a tooltip. */
	FText Detail;
	FString SwitchPath;
	/** Why the switch is there but cannot be used now, or empty. */
	FText SwitchUnavailable;
};

namespace DreamUILayerEffects
{
	/** Every layer effect on InWidget, in the order its component list and its visual hold them. */
	DREAMGUIEDITOR_API void Collect(const UDreamWidget* InWidget, TArray<FDreamUILayerEffect>& OutEffects);

	DREAMGUIEDITOR_API bool HasSwitch(const FDreamUILayerEffect& InEffect);
	DREAMGUIEDITOR_API bool IsOn(const FDreamUILayerEffect& InEffect);
	/**
	 * Turn the effect off or on as its details row would: one undoable step, carried from the preview to the template the
	 * way the designer carries a details edit, so that it is saved and written to the widget's .dui file.
	 */
	DREAMGUIEDITOR_API bool SetOn(const FDreamUILayerEffect& InEffect, bool bInOn);

	DREAMGUIEDITOR_API const FSlateBrush* KindIcon(EDreamUILayerEffectKind InKind);
	DREAMGUIEDITOR_API FText KindName(EDreamUILayerEffectKind InKind);

	/**
	 * Write InValueText to the property at InPath (dots into structs) of InObject the way a property row does: the open
	 * designer told before and after, the edit notifications around the write, and the top-level property written through
	 * its setter. InObject is a designer's preview object; outside a designer it is simply edited. One undo step.
	 */
	DREAMGUIEDITOR_API bool EditAsDetailsPanel(UObject* InObject, const FString& InPath, const FString& InValueText, FString* OutWhyNot = nullptr);
	/** The current value at InPath, as text, or an unset value when there is no such property. */
	DREAMGUIEDITOR_API TOptional<FString> ReadProperty(const UObject* InObject, const FString& InPath);
}
