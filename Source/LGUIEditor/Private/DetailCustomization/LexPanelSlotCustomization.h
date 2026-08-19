// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ULexLayoutContainer;
class IDetailCategoryBuilder;

/**
 * Slot rows, built into the owning widget's details panel. A ULexPanelSlot is never itself an object
 * being customized - it is reached through the child widget that owns it - so this is a row builder
 * rather than an IDetailCustomization, and registering it as one would only produce a layout nothing
 * ever asks for.
 */
class FLexPanelSlotCustomization
{
public:
	static void AddSlotProperties(IDetailCategoryBuilder& Category, const TArray<UObject*>& SlotObjects, const ULexLayoutContainer* ParentLayout);
	/**
	 * ZOrder is shown only under the panels that read it. Everywhere else it is inert, and a row for it
	 * would promise a paint order the panel never applies.
	 */
	static bool ShouldShowZOrder(const ULexLayoutContainer* ParentLayout);
};
