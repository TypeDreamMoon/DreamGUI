// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Core/DreamWidgetPropertyBinding.h"

class FDreamWidgetBlueprintEditor;
class UDreamWidget;
class UDreamWidgetBlueprint;
class FMulticastDelegateProperty;

/**
 * UMG's green "+" for the DreamUI designer: one row per BlueprintAssignable event on whatever the
 * details panel has selected, with a button that creates -- or jumps to -- the handler in the owning
 * Blueprint's event graph.
 *
 * ## What a click writes, and why it is not UMG's K2Node_ComponentBoundEvent
 *
 * UMG's button plants a K2Node_ComponentBoundEvent, whose runtime half is a UComponentDelegateBinding
 * the class applies through UBlueprintGeneratedClass::BindDynamicDelegates. UMG calls that in
 * UWidgetBlueprintGeneratedClass::InitializeWidgetStatic; DreamGUI's InitializeWidgetStatic never
 * does, so the node would compile cleanly and then never fire -- the exact silent failure a green "+"
 * exists to prevent. This plugin already has its own channel for the same fact: an
 * FDreamWidgetEventBinding on the Blueprint ("OnClicked -> Handler", the `->` the text pipeline
 * writes), which the compiler validates against the delegate's own signature and
 * UDreamUserWidget::BindEventBindings applies to every instance. The button rides that channel: it
 * creates a custom event with the delegate's signature and records the route, which is byte-for-byte
 * the form a `.dui` author would have written -- and, unlike a component-bound event, it can also
 * reach a delegate on a BEHAVIOUR, which no member variable of the class ever points at.
 *
 * ## Where the rows appear
 *
 * Registered for UDreamUserWidget (every native control -- DreamButton, DreamToggle... -- and every
 * nested widget Blueprint with event dispatchers) and for UDreamUIBehaviour (UIButton, UIToggle,
 * UIEventTrigger and the rest), so the events sit under whichever object the panel is showing, the
 * way this panel already splits their properties. Rows are built only when the selection lives in a
 * designer's preview world: any other host -- the widget inspector, a PIE world -- has no Blueprint
 * in reach, and UMG shows nothing there too.
 */
class FDreamWidgetEventCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/**
	 * Everything a click needs to name a route, resolved once per layout pass.
	 *
	 * Held as names and an index rather than object pointers wherever possible: the preview this
	 * panel shows is rebuilt wholesale on every structural edit, so a cached preview pointer is the
	 * kind of thing that is valid at layout time and stale by the time a button is pressed. The
	 * handlers re-resolve through the editor on every click for the same reason.
	 */
	struct FRouteContext
	{
		TWeakPtr<FDreamWidgetBlueprintEditor> Editor;
		/** The compiler's variable name for the widget: its display name, sanitized. */
		FName WidgetVariableName;
		/** Which object on the widget carries the delegate. */
		EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;
		/** The behaviour's position in the widget's component array, when Target is Behaviour. */
		int32 BehaviourIndex = INDEX_NONE;
	};

	/** Add one row: event icon, display name, and the Add / View switcher button -- UMG's row, verbatim. */
	void AddEventRow(class IDetailCategoryBuilder& InCategory, const FMulticastDelegateProperty* InDelegate,
		bool bInEnabled, const FText& InDisabledReason);

	/** The click. Focuses the existing handler when the route exists, creates both otherwise. */
	FReply HandleAddOrViewEvent(FName InEventName);
	/** Remove the route (the handler node is left where it is; deleting graph nodes is the graph's job). */
	FReply HandleRemoveEvent(FName InEventName);
	/** 0 = View (route exists), 1 = Add. Drives the button's icon, polled by Slate. */
	int32 GetAddOrViewIndex(FName InEventName) const;
	EVisibility GetRemoveVisibility(FName InEventName) const;

	/** The authored route matching this context and event, or null. */
	const FDreamWidgetEventBinding* FindRoute(const UDreamWidgetBlueprint* InBlueprint, FName InEventName) const;

	/** Switch the editor to its Graph mode and put the caret on the handler. */
	static void FocusHandler(FDreamWidgetBlueprintEditor& InEditor, const UObject* InHandler);

	FRouteContext Context;
	/** The class the delegates were enumerated from -- the widget's, or the behaviour's. */
	TWeakObjectPtr<const UClass> SubjectClass;
};
