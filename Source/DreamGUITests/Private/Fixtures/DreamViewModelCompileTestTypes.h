// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "ViewModel/DreamViewModel.h"
#include "DreamViewModelCompileTestTypes.generated.h"

/**
 * What the view model COMPILE tests (plan 17, the compiler's lane) need beside the shared view models in
 * DreamViewModelTestTypes.h: members a graph cannot reach, an event of each kind a route may leave from, and a widget
 * parent with handlers. Class and member names are spelled by the tests' .dui sources (reflected names drop the U).
 */

/** A single-cast event carrying a value: it holds one listener, which `OnPicked = Handler` sets. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamVMCompileTestPicked, float, Value);

/** A multicast event carrying a value: `OnValue -> Player.SetVolume` passes it on. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamVMCompileTestValueChanged, float, Value);

/**
 * A view model whose members no graph can reach. `Hidden.Secret` is MemberPathNotFound (a reflected property that is
 * not BlueprintVisible -- what an UnrealSharp [UProperty] without BlueprintReadOnly is), `-> Hidden.Poke()` and
 * `-> Hidden.Peek()` are RouteMemberFunctionNotFound (not BlueprintCallable; BlueprintPure, which an event has nothing
 * to call for).
 */
UCLASS(BlueprintType, NotBlueprintable, Transient, HideDropdown)
class UDreamVMCompileTestHiddenVM : public UDreamViewModel
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float Secret = 0.f;

	UFUNCTION(Category = "Test")
	void Poke() {}

	UFUNCTION(BlueprintPure, Category = "Test")
	int32 Peek() const { return 0; }
};

/** A behaviour with one event of each delegate kind that carries a value, and a way to fire each. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamVMCompileTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	/** Single-cast: `OnPicked = Player.SetVolume`, `OnPicked = HandlePicked`. */
	UPROPERTY()
	FDreamVMCompileTestPicked OnPicked;

	/** Multicast: `OnValue -> Player.SetVolume`. */
	UPROPERTY(BlueprintAssignable, Category = "Test")
	FDreamVMCompileTestValueChanged OnValue;

	void Pick(float InValue) { OnPicked.ExecuteIfBound(InValue); }
	void FireValue(float InValue) { OnValue.Broadcast(InValue); }
};

/** A user widget parent with handlers a single-cast route can name, one of them the wrong shape on purpose. */
UCLASS(NotBlueprintType, HideDropdown)
class UDreamVMCompileTestWidget : public UDreamTextUserWidget
{
	GENERATED_BODY()

public:
	/** OnPicked's shape. */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void HandlePicked(float Value) { ++PickedCount; LastPicked = Value; }

	/** Not OnPicked's shape: routing OnPicked to it is EventHandlerSignatureMismatch. */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void HandleWrongShape(int32 Value) { ++PickedCount; }

	/** A pure source, as UDreamTextUserWidgetBindingBase offers: `!IsBusy()` reads {IsBusy}. */
	UFUNCTION(BlueprintPure, Category = "Test")
	bool IsBusy() const { return false; }

	int32 PickedCount = 0;
	float LastPicked = 0.f;
};
