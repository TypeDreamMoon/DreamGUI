// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetPropertyBinding.generated.h"

class UDreamWidget;

/**
 * Which object on the widget a binding drives.
 *
 * A widget's interesting properties are mostly not on the widget: Text and FontSize belong to a
 * UDreamText, which is the widget's VISUAL, and a behaviour has its own. The widget is only the
 * thing that has a name, so the name alone does not say what to write to.
 */
UENUM()
enum class EDreamWidgetBindingTarget : uint8
{
	/** The widget itself. */
	Widget,
	/** Its visual -- where Text, FontSize, Sprite and most of what an author wants to bind live. */
	Visual,
	/** One of its behaviours, by position in the widget's component array. */
	Behaviour,
};

/**
 * "Drive this property from this function", resolved at compile time.
 *
 * The counterpart of UMG's FDelegateRuntimeBinding, with one deliberate difference. UMG binds a
 * companion delegate that the widget consults while it draws (FGetText next to Text), which needs
 * every bindable property in the runtime library to carry a delegate twin. Nothing here does, and
 * adding one per property to buy an editor feature is the wrong trade: this evaluates in the other
 * direction instead -- call the function, hand the result to the object's own setter.
 *
 * Going through the SETTER rather than writing the property is what makes that sound. A reflected
 * write lands in memory and nothing repaints; SetText marks what SetText knows to mark. It also
 * settles what "bindable" means without a list to maintain: a property is bindable exactly when its
 * object already exposes a setter for it, which is the same as it being ready to change at runtime.
 *
 * Every name here is resolved and checked by the compiler, so the runtime looks things up but never
 * guesses -- a binding that cannot be honoured is a compile error, not a silent no-op.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetPropertyBinding
{
	GENERATED_BODY()

	/** The widget, by the variable name the compiler gave it -- its display name. */
	UPROPERTY()
	FName WidgetName;

	/** Which object on that widget carries the property. */
	UPROPERTY()
	EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;

	/**
	 * Which behaviour, when Target is Behaviour: its index in the widget's component array.
	 *
	 * By position, because a behaviour is an instanced sub-object and the authored copy and the
	 * instanced one share no name -- the same correspondence the designer's own component editing
	 * runs on.
	 */
	UPROPERTY()
	int32 BehaviourIndex = INDEX_NONE;

	/** The property being driven. */
	UPROPERTY()
	FName PropertyName;

	/** Its setter, resolved by the compiler so the runtime does no name-guessing. */
	UPROPERTY()
	FName SetterName;

	/** A no-argument function on the user widget whose return value feeds the setter. */
	UPROPERTY()
	FName FunctionName;

	bool operator==(const FDreamWidgetPropertyBinding& Other) const
	{
		return WidgetName == Other.WidgetName
			&& Target == Other.Target
			&& BehaviourIndex == Other.BehaviourIndex
			&& PropertyName == Other.PropertyName
			&& SetterName == Other.SetterName
			&& FunctionName == Other.FunctionName;
	}
};

/**
 * The object a binding writes to, given the widget it names.
 *
 * Shared by the compiler and the runtime on purpose: they have to agree about what a binding points
 * at, and two copies of this walk would be two chances to disagree.
 */
DREAMGUI_API UObject* ResolveDreamWidgetBindingTarget(const UDreamWidget* InWidget, EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex);

/** The name a property's setter is spelled with: SetText for Text, SetUseKerning for bUseKerning. */
DREAMGUI_API FName MakeDreamWidgetSetterName(const FProperty* InProperty);

/**
 * That setter, when it exists taking one in-parameter of the property's type -- and so also the
 * answer to "is this property bindable".
 *
 * Shared rather than duplicated because the editor and the compiler have to agree: a panel that
 * offers Bind on a property the compiler then refuses is worse than no panel entry at all.
 */
DREAMGUI_API UFunction* FindDreamWidgetSetterFor(const UClass* InClass, const FProperty* InProperty);
