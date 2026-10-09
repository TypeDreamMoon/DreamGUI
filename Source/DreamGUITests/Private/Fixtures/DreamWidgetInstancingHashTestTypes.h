// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamWidgetInstancingHashTestTypes.generated.h"

/** Plain references that must be retargeted without editing hashed widget keys in place. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamWidgetInstancingHashTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<TObjectPtr<UDreamWidget>, TObjectPtr<UDreamWidget>> ReferencesByWidget;

	UPROPERTY()
	TSet<TObjectPtr<UDreamWidget>> WidgetSet;

	UPROPERTY()
	TObjectPtr<UDreamWidget> FollowingReference = nullptr;
};
