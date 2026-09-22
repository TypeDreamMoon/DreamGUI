// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class UDreamWidget;

/**
 * How to find a widget, held apart from the widget that was found.
 *
 * The engine's automation driver keeps this separation for a reason worth copying: an element that
 * remembered only the widget it matched would go stale the moment a list rebuilt its entries or a
 * prefab reloaded, and every test would have to re-find by hand after anything that restructures the
 * tree. Holding the question instead lets the element ask it again.
 */
class IDreamElementLocator
{
public:
	virtual ~IDreamElementLocator() = default;

	/** Append every widget under InRoot that answers this locator. Appends nothing when none do. */
	virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const = 0;

	/** What this locator was looking for, for an error message that has to be readable without the source. */
	virtual FString Describe() const = 0;
};

using FDreamLocatorRef = TSharedRef<IDreamElementLocator>;

/**
 * The locator factory. Named FDreamBy rather than By because the test module links AutomationDriver,
 * whose own locator factory is a class called By.
 *
 * THE SEARCH IS CASE SENSITIVE, throughout. That is not a choice made here: display names are matched
 * by UDreamWidget::FindChildByDisplayName with ESearchCase::CaseSensitive, and a locator that matched
 * case-insensitively would find widgets the production lookup cannot.
 */
class FDreamBy
{
public:
	/**
	 * Every widget anywhere under the root whose display name is exactly InDisplayName.
	 *
	 * The root itself is never a candidate -- this searches children, like the lookup it is built on.
	 * Several widgets may share a name, and all of them come back, in the tree's own sorted child
	 * order; it is Find (which insists on exactly one) that decides whether that is an error.
	 */
	static FDreamLocatorRef Name(const FString& InDisplayName);

	/**
	 * One widget, reached by walking display names from the root: "Menu/Buttons/Play".
	 *
	 * Delegates to UDreamWidget::FindChildByDisplayName, which already understands the separator --
	 * so this walks the same path the production lookup would, rather than a second implementation of
	 * it. That inherits two behaviours worth knowing before writing a path: each segment takes the
	 * FIRST child of that name and there is no backtracking, so a duplicate name partway down can
	 * send the walk into a branch that does not contain the rest of the path; and the leaf segment is
	 * matched against direct children only, never searched for deeper.
	 */
	static FDreamLocatorRef Path(const FString& InPath);

	/** Every widget under the root that is an InClass (or a subclass of one). */
	static FDreamLocatorRef Class(UClass* InClass);

	template<typename T>
	static FDreamLocatorRef Class()
	{
		return Class(T::StaticClass());
	}

	/**
	 * Every widget under the root the predicate says yes to. InDescription is what shows up in a
	 * timeout message, so it is worth writing something a reader can act on.
	 */
	static FDreamLocatorRef Predicate(TFunction<bool(UDreamWidget*)> InPredicate, const FString& InDescription);

	/** A locator that answers with exactly this widget, for handing an already-found widget to the driver. */
	static FDreamLocatorRef Widget(UDreamWidget* InWidget);
};
