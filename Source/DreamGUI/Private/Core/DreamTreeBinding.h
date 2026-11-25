// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UDreamUserWidget;
class UDreamWidget;
class UDreamWidgetTree;
class FObjectPropertyBase;

// The class is held by its live instance. Runtime classes must not be reinstanced mid-job.
struct FDreamTreeBinding
{
	TWeakObjectPtr<UDreamUserWidget> Host;
	TWeakObjectPtr<UDreamWidgetTree> Tree;
	TArray<TWeakObjectPtr<UDreamWidget>> Widgets;
	TMap<FName, FObjectPropertyBase*> Properties;
	TWeakObjectPtr<UDreamWidgetTree> Archetype;
	int32 Stage = 0;
	int32 Index = 0;
	void BindAnimations(const UDreamWidget* InWidget);
};
