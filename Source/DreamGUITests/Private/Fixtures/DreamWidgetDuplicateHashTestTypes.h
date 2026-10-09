// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamWidgetDuplicateHashTestTypes.generated.h"

/** The widget's identity participates in equality and hashing, even when wrapped in a struct. */
USTRUCT()
struct FDreamWidgetDuplicateHashTestKey
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UDreamWidget> Widget = nullptr;

	bool operator==(const FDreamWidgetDuplicateHashTestKey& Other) const { return Widget == Other.Widget; }
	friend uint32 GetTypeHash(const FDreamWidgetDuplicateHashTestKey& Key) { return GetTypeHash(Key.Widget); }
};

UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamWidgetDuplicateHashTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TMap<TObjectPtr<UDreamWidget>, TObjectPtr<UDreamWidget>> DirectMap;

	UPROPERTY()
	TMap<FDreamWidgetDuplicateHashTestKey, TObjectPtr<UDreamWidget>> StructMap;

	UPROPERTY()
	TSet<TObjectPtr<UDreamWidget>> DirectSet;

	UPROPERTY()
	TSet<FDreamWidgetDuplicateHashTestKey> StructSet;

	UPROPERTY()
	FDreamWidgetDuplicateHashTestKey OrdinaryStruct;

	UPROPERTY()
	TObjectPtr<UDreamWidget> FollowingReference = nullptr;
};
