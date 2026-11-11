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
 * FIntVector is what a name-prefix match lets through (it shares "Vector" with every float vector),
 * FIntPoint is what a "N numeric fields, must be a tuple" match lets through, and Scalar/Label are
 * there because the caller asks HasShortForm about every property it meets, not only struct ones.
 * FVector used to be the prefix near-miss; it graduated to a real entry when RelativeScale needed a
 * spelling, and FVector4 inherited the role -- until an image brush's UVRegion (an FVector4f) needed
 * one too, and the float vectors became entries all the way to four. FIntVector holds the role now.
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

	UPROPERTY()
	FVector3f Vector3f = FVector3f::ZeroVector;

	UPROPERTY()
	FVector4 Vector4 = FVector4(0, 0, 0, 0);

	/** The type an image brush's UVRegion is: (min x, min y, max x, max y). */
	UPROPERTY()
	FVector4f Vector4f = FVector4f(0, 0, 0, 0);

	/** The near miss FVector4 used to be: shares the name, holds integers, has no short form. */
	UPROPERTY()
	FIntVector IntVector = FIntVector::ZeroValue;

	UPROPERTY()
	FIntPoint IntPoint = FIntPoint::ZeroValue;

	UPROPERTY()
	float Scalar = 0.0f;

	UPROPERTY()
	FString Label;
};
