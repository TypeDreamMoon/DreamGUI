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
		/** SDF fonts carry SdfSource; assets from before it keep the bitmap-derived field. */
		SdfSourceOnFont,
		/** Bold is a field dilation; fonts still on the embolden-era BoldRatio default (0.08) move to 0.04. */
		BoldAsDilation,
		/**
		 * The authored canvas size became runtime data rather than editor-only designer data. It was a
		 * field on the prefab asset when this entry was added; the hierarchy is a class now, so the
		 * compiler writes it to UDreamWidgetGeneratedClass::DesignSize and nothing reads a prefab.
		 */
		PrefabCanvasSizeOnAsset,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FDreamGUIObjectVersion() {}
};
