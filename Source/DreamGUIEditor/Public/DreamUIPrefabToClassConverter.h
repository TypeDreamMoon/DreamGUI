// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamUIPrefab;
class UDreamWidgetBlueprint;
class UDreamWidget;
class UWorld;

/**
 * Turns a prefab asset into a hierarchy class.
 *
 * A library rather than a commandlet, so the conversion can be tested without running one. The
 * commandlet is a driver over this; nothing that decides anything lives in it.
 *
 * The prefab and the class models have to be alive at the same time for this to work, which is why
 * it comes before the call sites are switched rather than after: switching first would strand every
 * existing asset with no way across.
 */
namespace DreamUIPrefabToClass
{
	struct DREAMGUIEDITOR_API FConversionResult
	{
		/** Null when the conversion failed; check Errors. */
		UDreamWidgetBlueprint* Blueprint = nullptr;
		int32 WidgetCount = 0;
		TArray<FString> Warnings;
		TArray<FString> Errors;

		bool IsSuccess() const { return Blueprint != nullptr && Errors.IsEmpty(); }
		FString ToString() const;
	};

	/**
	 * Convert InPrefab into a UDreamWidgetBlueprint at InTargetPackageName and compile it.
	 *
	 * The source prefab is left untouched -- this produces a new asset beside it rather than
	 * rewriting one, so a failed or unfaithful conversion costs nothing and can simply be deleted.
	 */
	DREAMGUIEDITOR_API FConversionResult ConvertPrefab(UDreamUIPrefab* InPrefab, const FString& InTargetPackageName);

	/**
	 * Compare what the prefab loads against what the class builds, node by node.
	 *
	 * This is the acceptance test for a conversion, and it is deliberately not an eyeball check.
	 * Structure is compared by display-name path and widget class; values are compared by
	 * FProperty::Identical over the non-object properties.
	 *
	 * Object-typed properties are compared only for null-ness and class, not identity: the two
	 * hierarchies are different objects by construction, so anything pointing inside one can never
	 * equal its opposite number. That is a real limit -- a reference retargeted to the WRONG widget
	 * inside the tree would pass -- and it is the reason widget references get their own check
	 * through the binding names rather than being trusted to this.
	 *
	 * @return true when the two agree; OutDifferences carries one line per disagreement either way.
	 */
	DREAMGUIEDITOR_API bool VerifyFidelity(UDreamUIPrefab* InPrefab, UClass* InGeneratedClass, UWorld* InWorld, TArray<FString>& OutDifferences);
}
