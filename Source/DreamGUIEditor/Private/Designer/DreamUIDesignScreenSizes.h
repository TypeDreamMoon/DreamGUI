// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Common device resolutions shared by the prefab designer's Screen Size picker and the viewport's
 * resolution-guide overlay — the same set UMG's designer surface visualizes.
 */
struct FDreamUIDesignScreenSize
{
	const TCHAR* Label;
	FIntPoint Size;
};

inline TArrayView<const FDreamUIDesignScreenSize> GetDreamUIDesignScreenSizes()
{
	static const FDreamUIDesignScreenSize Sizes[] =
	{
		{ TEXT("1136 x 640 (Phone)"), FIntPoint(1136, 640) },
		{ TEXT("1280 x 720 (720p)"), FIntPoint(1280, 720) },
		{ TEXT("1920 x 1080 (1080p)"), FIntPoint(1920, 1080) },
		{ TEXT("2048 x 1536 (Tablet)"), FIntPoint(2048, 1536) },
		{ TEXT("2560 x 1080 (Ultrawide)"), FIntPoint(2560, 1080) },
		{ TEXT("2560 x 1440 (1440p)"), FIntPoint(2560, 1440) },
		{ TEXT("3440 x 1440 (Ultrawide)"), FIntPoint(3440, 1440) },
		{ TEXT("3840 x 2160 (4K)"), FIntPoint(3840, 2160) },
		{ TEXT("640 x 1136 (Phone Portrait)"), FIntPoint(640, 1136) },
		{ TEXT("720 x 1280 (Portrait)"), FIntPoint(720, 1280) },
		{ TEXT("1080 x 1920 (Portrait)"), FIntPoint(1080, 1920) },
		{ TEXT("1440 x 2560 (Portrait)"), FIntPoint(1440, 2560) },
		{ TEXT("1536 x 2048 (Tablet Portrait)"), FIntPoint(1536, 2048) },
	};
	return Sizes;
}
