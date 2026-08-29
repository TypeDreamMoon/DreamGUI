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
	FBindingSite ResolveBindingSite(UObject* InObject);

	/** Whether a Bind entry belongs on this row: a designer owns it, and the property has a setter. */
	bool IsBindable(UObject* InObject, const FProperty* InProperty);

	/** The authored binding for this site and property, or null. */
	FDreamWidgetPropertyBinding* FindBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName);

	/** Author one, replacing any binding already on that property. */
	void SetBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName, FName InFunctionName);

	/** Drop it, leaving the property at its authored value. */
	void RemoveBinding(UDreamWidgetBlueprint* InBlueprint, const FBindingSite& InSite, FName InPropertyName);

	/**
	 * Add a function shaped to feed this property, bind it, and open it.
	 *
	 * Returns the graph so a caller can check what was built. InDesigner may be null, which skips
	 * only the opening -- the graph and the binding are the same either way.
	 */
	class UEdGraph* CreateAndBindFunction(FDreamWidgetBlueprintEditor* InDesigner, UDreamWidgetBlueprint* InBlueprint,
		const FBindingSite& InSite, const FProperty* InProperty);

	/** The Bind combo for one property row. Null when the row is not bindable. */
	TSharedPtr<SWidget> MakeBindingWidget(FDreamWidgetBlueprintEditor* InDesigner, UObject* InObject,
		TSharedPtr<IPropertyHandle> InPropertyHandle);
}
