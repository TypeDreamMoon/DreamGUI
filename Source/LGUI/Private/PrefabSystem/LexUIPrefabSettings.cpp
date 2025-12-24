// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabSettings.h"

#if WITH_EDITOR
void ULexUIPrefabSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool ULexUIPrefabSettings::GetLogPrefabLoadTime()
{
	return GetDefault<ULexUIPrefabSettings>()->bLogPrefabLoadTime;
}
