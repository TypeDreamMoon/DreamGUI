// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class SWidget;
class UDreamWidget;

namespace DreamTests
{
	class FDreamDesignerDriver;

	/**
	 * Where things are in a designer's own Slate panels: the hierarchy tree, the details panel, the palette and the
	 * viewport's toolbar. Each answers a widget only once Slate has painted it -- a row of a virtualised tree that has not
	 * been scrolled to, or a panel in a tab that was never laid out, has no place for a pointer to be -- so a caller waits
	 * on the answer rather than on a number of frames.
	 *
	 * Pointer and key input into what these find goes through DreamSlatePanel, which uses Slate's own input functions.
	 */
	namespace DreamDesignerPanels
	{
		/** Whether InWidget has been arranged and painted with a size, which is when a pointer can be aimed at it. */
		bool IsPainted(const TSharedPtr<SWidget>& InWidget);

		/**
		 * The hierarchy tree's row showing InWidget -- a template or its preview, which share a display name
		 * (SDreamWidgetEditorHierarchyViewItem::GetItemText) -- or null while there is none painted.
		 */
		TSharedPtr<SWidget> HierarchyRowFor(const FDreamDesignerDriver& InDriver, const UDreamWidget* InWidget);

		/** The details panel's search box, or null while there is none painted. */
		TSharedPtr<SWidget> DetailsSearchBox(const FDreamDesignerDriver& InDriver);
		/**
		 * The widget of type InTypeNamePart ("SSpinBox", "SCheckBox") on the details panel's property row labelled
		 * InLabel, or null while there is none painted. A details view row is an SDetailSingleItemRow holding the name and
		 * the value; the details tree is virtualised, so a row that is out of view is not there to find -- filter the
		 * panel through its search box first.
		 */
		TSharedPtr<SWidget> DetailsValueWidget(const FDreamDesignerDriver& InDriver, const FString& InLabel, const TCHAR* InTypeNamePart);

		/** The palette's search box, or null while there is none painted. */
		TSharedPtr<SWidget> PaletteSearchBox(const FDreamDesignerDriver& InDriver);
		/**
		 * The palette's row named InDisplayName -- the name an author reads on it ("Button", "Vertical Box") -- or null
		 * while there is none painted. A group that is folded holds its rows unpainted; the palette's search unfolds every
		 * group that has a match (SDreamWidgetPalette::RefreshRootItems).
		 */
		TSharedPtr<SWidget> PaletteRowFor(const FDreamDesignerDriver& InDriver, const FString& InDisplayName);
	}
}
