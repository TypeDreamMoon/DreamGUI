// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "DreamUIValueFormatTestTypes.generated.h"

/**
 * One reflected property per branch of the short-form table, and one per near miss.
 *
 * The table is keyed on FStructProperty::Struct, so it can only be tested through real properties
 * that UHT produced: a UScriptStruct on its own says nothing about what a property of that type
 * looks like, and hand-building an FProperty would test the fixture instead of the reflection the
 * builder will actually be handed. Reaching into unrelated classes for a property of each type
 * works too, but then a rename somewhere else breaks this file with a message about the wrong thing.
 *
 * The near misses carry as much weight as the hits, and each one is a specific wrong implementation:
 * FVector4 is what a name-prefix match lets through (every FVector* shares the first seven
 * characters), FIntPoint is what a "N numeric fields, must be a tuple" match lets through, and
 * Scalar/Label are there because the caller asks HasShortForm about every property it meets, not
 * only struct ones. FVector used to be the prefix near-miss; it graduated to a real entry when
 * RelativeScale needed a spelling, and FVector4 inherited the role.
 */
USTRUCT()
struct FDreamUIValueFormatFixture
{
	GENERATED_BODY()

	UPROPERTY()
	FVector2D Vector2D = FVector2D::ZeroVector;

	/** The single-precision variant. A different UScriptStruct, the same text, half the digits. */
	UPROPERTY()
	FVector2f Vector2f = FVector2f::ZeroVector;

	UPROPERTY()
	FLinearColor LinearColor = FLinearColor::White;

	UPROPERTY()
	FColor Color = FColor::White;

	UPROPERTY()
	FMargin Margin;

	UPROPERTY()
	FVector Vector = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotator = FRotator::ZeroRotator;

	/** The near miss FVector used to be: shares the name prefix, has no short form. */
	UPROPERTY()
	FVector4 Vector4 = FVector4(0, 0, 0, 0);

	UPROPERTY()
	FIntPoint IntPoint = FIntPoint::ZeroValue;

	UPROPERTY()
	float Scalar = 0.0f;

	UPROPERTY()
	FString Label;
};
