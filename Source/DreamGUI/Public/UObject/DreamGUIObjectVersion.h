// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/** Custom serialization version for DreamGUI assets. Append new values before LatestVersion. */
struct DREAMGUI_API FDreamGUIObjectVersion
{
	enum Type
	{
		BeforeCustomVersionWasAdded = 0,
		/** SDF fonts carry SdfSource; assets from before it take the class default, the outline field (the editor warns once per such asset). */
		SdfSourceOnFont,
		/** Bold is a field dilation; fonts still on the embolden-era BoldRatio default (0.08) move to 0.04. */
		BoldAsDilation,
		/**
		 * The authored canvas size became runtime data rather than editor-only designer data. It was a
		 * field on the prefab asset when this entry was added; the hierarchy is a class now, so the
		 * compiler writes it to UDreamWidgetGeneratedClass::DesignSize and nothing reads a prefab.
		 */
		PrefabCanvasSizeOnAsset,
		/**
		 * A FreeType font's fallbacks are FDreamUIFontFallback entries (ranges, cultures, scale, preference). A font saved
		 * before has its FallbackFontArray moved into Fallbacks on load, in order and with default settings.
		 */
		FontFallbackEntries,
		/**
		 * Emoji data is keyed by the whole sequence (FDreamUIFontEmojiKey::Sequence). A key saved before has its Sequence
		 * filled with {EmojiCode} on load; it hashes and compares the same either way.
		 */
		EmojiKeyBySequence,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FDreamGUIObjectVersion() {}
};
