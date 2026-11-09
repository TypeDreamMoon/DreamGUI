// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Workflow/DreamUILayerStyleSnippet.h"

class FDreamUIWidgetContextMenuExtensibilityManager;
class FDreamWidgetBlueprintEditor;
class UDreamWidget;

/**
 * Copy Layer Style and Paste Layer Style, an image editor's pair, on the designer's widgets.
 *
 * The clipboard holds the copied widget's look as lines of `.dui` (FDreamUILayerStyleSnippet), so it carries across
 * Blueprints, across editor sessions, and into a `.dui` opened in a text editor; and a paste reads lines written by hand the
 * same way. A paste writes onto the AUTHORING tree -- each target's own layer style, added when it has none; its element's
 * blend mode and its opacity; its clip and mask -- reports what it wrote to the write-back, and makes one structural
 * notification, which on a `.dui`-backed Blueprint writes the lines into the file before the compile reads it back. All of it,
 * for every selected widget, is one transaction: one undo.
 *
 * The parts a paste writes are the ones the clipboard carries; what Copy puts there is the layer style, and the blend mode
 * and the mask when the menu's Copy Includes says so (remembered per user, GetCopyParts).
 */
namespace DreamUILayerStyleClipboard
{
	/**
	 * A clipboard of the editor's own, in the desktop's place for Copy, ReadClipboard and Paste while it lives: a test's, so
	 * that it neither waits on nor disturbs the clipboard every program on the desktop shares. One at a time.
	 */
	struct DREAMGUIEDITOR_API FScopedClipboard
	{
		FScopedClipboard();
		~FScopedClipboard();
		FScopedClipboard(const FScopedClipboard&) = delete;
		FScopedClipboard& operator=(const FScopedClipboard&) = delete;

		/** What it holds. */
		FString Text;
	};

	/** The parts Copy Layer Style takes: the style always, the blending and the mask as the user last chose. */
	DREAMGUIEDITOR_API EDreamUIStyleParts GetCopyParts();
	DREAMGUIEDITOR_API void SetCopyParts(EDreamUIStyleParts InParts);

	/**
	 * InWidget's look (its InParts) onto the system clipboard, checked by reading it back (another program can hold the
	 * clipboard open for a moment). False when it has none of them, or the clipboard would not take it.
	 */
	DREAMGUIEDITOR_API bool Copy(const UDreamWidget* InWidget, EDreamUIStyleParts InParts);

	/**
	 * What the clipboard holds, when it holds a layer style. Read again only when the clipboard's text changed. A read while
	 * another program holds the clipboard open finds nothing: bInPatient asks again for a moment first (Paste does; a menu,
	 * which asks on every frame, does not).
	 */
	DREAMGUIEDITOR_API bool ReadClipboard(FDreamUILayerStyleSnippet& OutSnippet, bool bInPatient = false);

	/**
	 * InSnippet's parts that InParts selects, written onto the authoring templates of InPreviewWidgets (the designer's
	 * selection), in one transaction named InTransactionName. Returns how many widgets took it; 0 leaves no transaction.
	 *
	 * What the preset library's Apply goes through too, so a preset applied and a style pasted are the same edit.
	 */
	DREAMGUIEDITOR_API int32 Apply(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets,
		const FDreamUILayerStyleSnippet& InSnippet, EDreamUIStyleParts InParts, const FText& InTransactionName);

	/** Paste Layer Style: the clipboard's snippet, every part it carries, onto InPreviewWidgets. */
	DREAMGUIEDITOR_API int32 Paste(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets);

	/** The Layer Style section of the designer's widget menu (hierarchy and viewport), on the editor module's extension point. */
	void RegisterMenuExtension(FDreamUIWidgetContextMenuExtensibilityManager& InManager);
	void UnregisterMenuExtension(FDreamUIWidgetContextMenuExtensibilityManager& InManager);
}
