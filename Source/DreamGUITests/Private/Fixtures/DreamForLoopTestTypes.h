// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamTextUserWidget.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "DreamForLoopTestTypes.generated.h"

/**
 * One item of a `for` source, as a game would hand one over: a plain object whose members the item bindings read
 * by reflection. A text and a number, because those are the two shapes the bindings convert differently -- a value
 * copied whole, and one that goes through the numeric widening.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamForLoopTestItem : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FText Label;

	UPROPERTY()
	float Amount = 1.0f;
};

/**
 * The widget a `for` belongs to: both source shapes over one array. Options is the variable form
 * (`for Option in Options`), GetOptions the function form (`for Option in GetOptions()`), and a test switches
 * between them by what the binding names, never by having two lists to keep in step.
 *
 * Blueprintable through its parent, so the compile tests can derive a .dui class from it; the runtime tests use it
 * as it is, natively.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamForLoopTestHost : public UDreamTextUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TArray<TObjectPtr<UObject>> Options;

	UFUNCTION(BlueprintPure, Category = "Test")
	TArray<UObject*> GetOptions() const
	{
		TArray<UObject*> Result;
		Result.Reserve(Options.Num());
		for (const TObjectPtr<UObject>& Option : Options)
		{
			Result.Add(Option.Get());
		}
		return Result;
	}
};

/**
 * A component repeated by a `for`, standing in for what a .dui `props` block compiles into: a property with NO
 * setter (Caption -- there is no SetCaption, as there is none for a Blueprint variable), and a function the
 * component's own bindings read it through. That pair is what proves an item write lands on a copy and that the
 * copy's own bindings then show it.
 *
 * InitializedCount is deliberately not reflected. A copy is made from an initialized template, and a reflected
 * counter would come across with everything else already at one; a plain member starts at zero in every new object,
 * so a copy that reads one was initialized itself, exactly once.
 */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamForLoopTestRow : public UDreamTextUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FText Caption;

	UFUNCTION(BlueprintPure, Category = "Test")
	FText GetCaptionText() const { return Caption; }

	int32 InitializedCount = 0;

	virtual void NativeOnInitialized() override
	{
		++InitializedCount;
		Super::NativeOnInitialized();
	}
};
