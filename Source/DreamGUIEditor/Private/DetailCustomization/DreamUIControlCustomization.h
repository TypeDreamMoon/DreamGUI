// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IPropertyHandle;

/**
 * The details panel for every native control, registered once against UDreamUIControl.
 *
 * One customization for all twenty-odd of them, and that is possible because they are all the same
 * shape: each declares exactly one property called `Style`, holding a struct whose fields come in
 * pairs -- a `bOverride_X` bit and the X it gates. The engine already draws the pair as ONE row (the
 * bits carry InlineEditConditionToggle, so the checkbox rides the value's row rather than taking a
 * row of its own); what it does not do is give those rows any structure, so a ring menu's style
 * arrived as thirty rows in one flat list with geometry, colours, text and the open animation
 * interleaved in declaration order.
 *
 * So this walks the style struct and re-files its rows into groups taken from the field's OWN
 * category metadata -- the part after the pipe in "Ring Menu Style|Geometry". Nothing is invented:
 * the grouping a style author already wrote down is the grouping the panel shows, which means a new
 * style field lands in the right group by being declared in it, and a struct with no pipes in its
 * categories is left exactly as it was.
 *
 * WHAT IT DELIBERATELY DOES NOT DO: hide a value whose override bit is off. Unticked means "take the
 * project sheet's value", and the number still shown is the one this instance WOULD push -- hiding
 * it would hide the answer to "what would happen if I ticked this". The engine greys it, which says
 * "not in effect" without pretending it does not exist. UMG's own style structs read the same way.
 */
class FDreamUIControlCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/**
	 * The group a style field belongs in: what follows the last pipe in its Category, or none.
	 *
	 * Returns false for a field whose category has no pipe, which is the signal to leave that field
	 * where the default layout put it -- a struct nobody grouped is not a struct to invent groups for.
	 */
	static bool ResolveGroupName(const TSharedPtr<IPropertyHandle>& InChild, FString& OutGroupName);
};
