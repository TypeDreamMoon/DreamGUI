// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamUserWidget;
class UDreamWidget;
struct FDreamWidgetEachBinding;

/**
 * What an `each` needs from the list view it fills.
 *
 * The list views belong to the control library, and the core does not name its types; the .dui builder and
 * UDreamUserWidget reach them through this instead. The control library registers its handler with
 * DreamUI::SetEachBindingHandler. Without one, an `each` is refused when the .dui compiles and a compiled
 * one binds nothing.
 */
class DREAMGUI_API IDreamUIEachBindingHandler
{
public:
	virtual ~IDreamUIEachBindingHandler() = default;

	/** Whether InHost carries a list view an `each` can fill. */
	virtual bool HasListView(const UDreamWidget* InHost) const = 0;

	/**
	 * Get InHost's list view and InTemplate ready while a .dui compiles: the template gets the cell marker a
	 * list clones by when it has none, and a view configured for both axes or neither scrolls vertically.
	 * Returns whether the view scrolls vertically, which is how the builder lays out the content it adds.
	 */
	virtual bool PrepareHost(UDreamWidget* InHost, UDreamWidget* InTemplate) const = 0;

	/** Point InHost's list view at InContent, the widget its cells are cloned under. */
	virtual void SetContent(UDreamWidget* InHost, UDreamWidget* InContent) const = 0;

	/**
	 * Bind one `each` of InOwner: aim InHost's list view at this instance's content (when InContent is
	 * given) and template, and feed it from the binding's source. Returns the adapter that feeds it, outered
	 * to InOwner, which has to keep it alive; null when InHost has no list view.
	 */
	virtual UObject* Bind(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding,
		UDreamWidget* InHost, UDreamWidget* InTemplate, UDreamWidget* InContent) const = 0;

	/** Re-read InAdapter's source and update its list. */
	virtual void Refresh(UObject* InAdapter) const = 0;

	/** The binding InAdapter feeds, or null when InAdapter is not one of this handler's. */
	virtual const FDreamWidgetEachBinding* GetBinding(const UObject* InAdapter) const = 0;
};

namespace DreamUI
{
	/** The registered handler, or null. */
	DREAMGUI_API IDreamUIEachBindingHandler* GetEachBindingHandler();

	/** Register InHandler, or with null unregister. The handler must outlive its registration. */
	DREAMGUI_API void SetEachBindingHandler(IDreamUIEachBindingHandler* InHandler);
}
