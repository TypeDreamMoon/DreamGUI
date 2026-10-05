// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ViewModel/DreamViewModel.h"
#include "DreamViewModelTestTypes.generated.h"

/**
 * The view models the MVVM tests (plan 17) bind to, shared by the parser, compiler, run-time and list tests.
 *
 * Class and member NAMES are the contract: test .dui sources spell them (`DreamTestPlayerVM Player`, `Player.Health`),
 * so renaming one silently changes what a test checks. C++ setters broadcast through DREAM_VM_SET, the way a project's
 * C++ view model would; a test that wants a change nobody announces writes the member directly.
 *
 * Every member a graph must reach is BlueprintReadWrite / BlueprintReadOnly or BlueprintCallable / BlueprintPure, because
 * the compiler lowers `Player.Health` into a Blueprint function that reads it.
 */

/** What `Player.Stats` holds: the second hop of a member path. */
UCLASS(BlueprintType, NotBlueprintable, Transient, HideDropdown)
class UDreamTestStatsVM : public UDreamViewModel
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	FText ClassName;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	int32 Rank = 1;

	void SetClassName(const FText& InClassName) { DREAM_VM_SET(ClassName, InClassName); }
	void SetRank(int32 InRank) { DREAM_VM_SET(Rank, InRank); }
};

/** One row of `Player.Items`: what a `for` / `each` copy shows, and what `-> Item.Use()` calls. */
UCLASS(BlueprintType, NotBlueprintable, Transient, HideDropdown)
class UDreamTestItemVM : public UDreamViewModel
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	FText Name;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	FText CountText;

	/** Not FieldNotify: a member nothing announces, for the copy that has to be told by a refresh instead. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FText Untracked;

	/** How many times Use / UseWithValue ran: what a per-item route test asserts. */
	UPROPERTY(BlueprintReadOnly, Category = "Test")
	int32 UseCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Test")
	float LastValue = 0.f;

	/** `-> Item.Use()` -- takes nothing. */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void Use() { ++UseCount; }

	/** `-> Item.UseWithValue` on a slider's OnValueChanged(float Value) -- takes what the event sends. */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void UseWithValue(float Value) { ++UseCount; LastValue = Value; }

	void SetName(const FText& InName) { DREAM_VM_SET(Name, InName); }
	void SetCountText(const FText& InCountText) { DREAM_VM_SET(CountText, InCountText); }
};

/**
 * The root view model of most tests: plain fields, a nested view model, a list, a read-only field, a field nobody
 * announces, a FieldNotify function, a setter `<->` must prefer, and counters for the calls a route makes.
 */
UCLASS(BlueprintType, NotBlueprintable, Transient, HideDropdown)
class UDreamTestPlayerVM : public UDreamViewModel
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	FText Name;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	float Health = 100.f;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	int32 Gold = 0;

	/** Always the display form of Gold: what a view model offers instead of a converter. */
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	FText GoldText;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	bool bIsDead = false;

	/** `<-> Player.Volume` -- has SetVolume, which the generated setter must prefer to a raw write. */
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	float Volume = 0.5f;

	/** `<-> Player.Brightness` -- has no SetBrightness: written directly, then broadcast on this object. */
	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	float Brightness = 0.5f;

	/** Read-only to graphs and no SetLevel: `<-> Player.Level` is TwoWayTargetReadOnly. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Test")
	int32 Level = 1;

	/** Readable, never announced: a binding through it must poll. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	float Untracked = 0.f;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	TObjectPtr<UDreamTestStatsVM> Stats = nullptr;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Category = "Test")
	TArray<TObjectPtr<UDreamTestItemVM>> Items;

	UPROPERTY(BlueprintReadOnly, Category = "Test")
	int32 ApplyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Test")
	int32 SetVolumeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Test")
	float LastHealAmount = 0.f;

	/** A FieldNotify FUNCTION: announced by SetHealth, so `Percent <- Player.GetHealthPercent()` subscribes. */
	UFUNCTION(BlueprintPure, FieldNotify, Category = "Test")
	float GetHealthPercent() const { return Health / 100.f; }

	/** A pure function WITH an argument: a binding calling it is never complete, and polls. */
	UFUNCTION(BlueprintPure, Category = "Test")
	FText FormatGold(int32 InGold) const { return FText::AsNumber(InGold); }

	/** `-> Player.Apply()` */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void Apply() { ++ApplyCount; }

	/** `-> Player.Heal(25)` */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void Heal(float Amount) { LastHealAmount = Amount; SetHealth(Health + Amount); }

	/** The setter `<-> Player.Volume` writes back through. */
	UFUNCTION(BlueprintCallable, Category = "Test")
	void SetVolume(float InVolume) { ++SetVolumeCount; DREAM_VM_SET(Volume, InVolume); }

	void SetName(const FText& InName) { DREAM_VM_SET(Name, InName); }
	void SetHealth(float InHealth)
	{
		if (DREAM_VM_SET(Health, InHealth))
		{
			BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::GetHealthPercent);
		}
	}
	void SetGold(int32 InGold)
	{
		if (DREAM_VM_SET(Gold, InGold))
		{
			DREAM_VM_SET(GoldText, FText::AsNumber(InGold));
		}
	}
	void SetIsDead(bool bInIsDead) { DREAM_VM_SET(bIsDead, bInIsDead); }
	void SetLevel(int32 InLevel) { DREAM_VM_SET(Level, InLevel); }
	void SetStats(UDreamTestStatsVM* InStats) { DREAM_VM_SET(Stats, InStats); }
	void SetItems(const TArray<TObjectPtr<UDreamTestItemVM>>& InItems)
	{
		Items = InItems;
		BroadcastFieldValueChanged(ThisClass::FFieldNotificationClassDescriptor::Items);
	}
};

/** An object that is not a view model and announces nothing: bindings through it work, and poll. */
UCLASS(BlueprintType, NotBlueprintable, Transient, HideDropdown)
class UDreamTestPlainModel : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FText Name;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	float Value = 0.f;
};
