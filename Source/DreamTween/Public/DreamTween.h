// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"
DECLARE_LOG_CATEGORY_EXTERN(DreamTween, Log, All);
DECLARE_STATS_GROUP(TEXT("DreamTween"), STATGROUP_DreamTween, STATCAT_Advanced);
class FDreamTweenModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};