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

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FDreamGUIObjectVersion() {}
};
