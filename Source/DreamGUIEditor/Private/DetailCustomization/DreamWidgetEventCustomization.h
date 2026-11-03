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
class UK2Node_CustomEvent;

/**
 * The two writes behind the Events rows, kept out of the customization so a script or a test makes them exactly as a
 * click does. A route is named by the compiler's variable name of the widget, which of its objects carries the event,
 * and the event's name.
 *
 * On a text-authored class a route is its `Event -> Handler` line: the compile rebuilds the Blueprint's route list from
 * the `.dui`, so the line is what is written (in the node's own `+` block for a behaviour the node writes, on the
 * node's line otherwise), inside the same transaction as the handler, and the compile reads it back.
 */
namespace DreamWidgetEventRoutes
{
	/** The route the Blueprint holds for this event on this object, or null. */
	DREAMGUIEDITOR_API const FDreamWidgetEventBinding* FindRoute(const UDreamWidgetBlueprint* InBlueprint, FName InWidgetVariableName,
		EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex, FName InEventName);

	/**
	 * A handler for this event and the route that names it, made as one gesture: a custom event in the event graph with
	 * the event's own signature, and the route. On a text-authored class the line is checked before anything is made.
	 * Returns the handler, or null when nothing was made (and said why). A route that already exists is not doubled.
	 */
	DREAMGUIEDITOR_API UK2Node_CustomEvent* AddRouteWithHandler(UDreamWidgetBlueprint* InBlueprint, FName InWidgetVariableName,
		EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex, FName InEventName);

	/** Take the route out -- the list entry, or the `.dui` line. The handler stays in the event graph. False when nothing was removed. */
	DREAMGUIEDITOR_API bool RemoveRoute(UDreamWidgetBlueprint* InBlueprint, FName InWidgetVariableName,
		EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex, FName InEventName);
}

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
	void AddEventRow(class IDetailCategoryBuilder& InCategory, const FProperty* InDelegate,
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
	/**
	 * Whether FindRoute finds one, answered from a cache that lasts a frame. Every row's switcher and remove button ask on
	 * every frame, and each ask scans the Blueprint's routes; the first of a frame does the scan. A click re-resolves with
	 * FindRoute itself, so only the icon can be a frame behind.
	 */
	bool HasRoute(FName InEventName) const;
	mutable TMap<FName, bool> HasRouteCache;
	mutable uint64 HasRouteCacheFrame = MAX_uint64;

	/** Switch the editor to its Graph mode and put the caret on the handler. */
	static void FocusHandler(FDreamWidgetBlueprintEditor& InEditor, const UObject* InHandler);

	FRouteContext Context;
	/** The class the delegates were enumerated from -- the widget's, or the behaviour's. */
	TWeakObjectPtr<const UClass> SubjectClass;
};
