// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetPropertyBinding.h"

class FDreamWidgetBlueprintEditor;
class IPropertyHandle;
class SWidget;
class UDreamWidget;
class UDreamWidgetBlueprint;

/**
 * The details panel's Bind entry: authoring the bindings the compiler resolves.
 *
 * Kept out of the extension handler it hangs off because the handler's other job -- the hierarchy
 * picker for widget-reference properties -- shares nothing with this but the slot it draws in.
 */
namespace DreamWidgetPropertyBindingExtension
{
	/** Where a binding authored on InObject would point: the widget, and which of its objects. */
	struct FBindingSite
	{
		UDreamWidget* Widget = nullptr;
		EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;
		int32 BehaviourIndex = INDEX_NONE;
		FName WidgetName;

		bool IsValid() const { return Widget != nullptr && !WidgetName.IsNone(); }
	};

	/**
	 * Work out that site from an object the details panel is showing.
	 *
	 * The panel shows widgets, visuals and behaviours as peers, but only a widget has a name, so an
	 * object that is not one has to be found on the widget that owns it.
	 */
	DREAMGUIEDITOR_API FBindingSite ResolveBindingSite(UObject* InObject);

	/** Whether a Bind entry belongs on this row: a designer owns it, and the property has a setter. */
	DREAMGUIEDITOR_API bool IsBindable(UObject* InObject, const FProperty* InProperty);

	/** The authored binding for this site and property, or null. */
	DREAMGUIEDITOR_API FDreamWidgetPropertyBinding* FindBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName);

	/**
	 * Author one, replacing any binding already on that property.
	 *
	 * On a text-authored class the binding is its `Property <- Function()` line, written into the `.dui` (the property's
	 * own line rewritten when it has one) and read back by the compile that follows. False, said out loud, when the line
	 * cannot be written.
	 */
	DREAMGUIEDITOR_API bool SetBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, FName InFunctionName);

	/** Drop it, leaving the property at its authored value. On a text-authored class, its `<-` (or `<->`) line is taken out. */
	DREAMGUIEDITOR_API bool RemoveBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName);

	/**
	 * Text-authored classes: drop the binding and write a value in its place -- the property's `<-` line rewritten as
	 * `Property = InValueText`, a `.dui` literal (`24`, `"OK"`, `(8, 8)`). What the greyed value row of a bound property
	 * offers instead of an edit the file could not keep.
	 */
	DREAMGUIEDITOR_API bool UnbindAndSetValue(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName,
		const FString& InValueText);

	/** UnbindAndSetValue with the value InValueOwner holds for InProperty now, spelled as the write-back spells it. */
	DREAMGUIEDITOR_API bool UnbindAndKeepValue(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, const FProperty* InProperty,
		const UObject* InValueOwner);

	/**
	 * Whether a two-way binding (`<->`) is offered on InObject's properties: on a text-authored class, for a control that
	 * announces its value through the conventional OnValueChangedBP event -- the one the compiler routes the reverse
	 * half to. A property of anything else would bind one way and spring back.
	 */
	DREAMGUIEDITOR_API bool CanBindTwoWay(UObject* InObject);

	/**
	 * What `InProperty <-> …` can name on this class, for a property of InSubjectClass: its own variables that a graph may
	 * set, and one member deep into an object variable (a viewmodel: `Settings.Volume`) -- read from the skeleton, as of
	 * the last compile, sorted. Of the type the subject's OnValueChangedBP hands its listener, exactly: the reverse half
	 * is a setter taking the variable's type, routed to that event, and a `Number` (a double) cannot take a slider's
	 * float. And one the getter can feed the property from.
	 */
	DREAMGUIEDITOR_API TArray<FString> CollectTwoWayVariables(UDreamWidgetBlueprint* InBlueprint, const UClass* InSubjectClass,
		const FProperty* InProperty);

	/** Text-authored classes: write `Property <-> InVariablePath`, replacing the property's own line when it has one. */
	DREAMGUIEDITOR_API bool SetTwoWayBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName,
		const FString& InVariablePath);

	/**
	 * Add a function shaped to feed this property, bind it, and open it.
	 *
	 * Returns the graph so a caller can check what was built. InDesigner may be null, which skips
	 * only the opening -- the graph and the binding are the same either way. On a text-authored class the `<-` line is
	 * checked BEFORE the function is made, so a refusal leaves no empty function behind.
	 */
	DREAMGUIEDITOR_API class UEdGraph* CreateAndBindFunction(FDreamWidgetBlueprintEditor* InDesigner, UDreamWidgetBlueprint* InBlueprint,
		const FBindingSite& InSite, const FProperty* InProperty);

	/** The Bind combo for one property row. Null when the row is not bindable. */
	TSharedPtr<SWidget> MakeBindingWidget(FDreamWidgetBlueprintEditor* InDesigner, UObject* InObject,
		TSharedPtr<IPropertyHandle> InPropertyHandle);
}
