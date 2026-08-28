// Copyright 2019-Present DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertyHandle.h"
#include "Styling/SlateTypes.h"

/**
 * Reading a property handle across a selection that does not agree.
 *
 * IPropertyHandle::GetValue leaves its out-param UNTOUCHED when the objects hold different values:
 * it returns MultipleValues and writes nothing. A customization that declares a bare local and reads
 * it afterwards is therefore reading whatever was on the stack, and that value goes on to decide
 * which rows the panel builds, which box draws checked, or -- worst -- what gets written back into
 * the asset. It looks correct in every single-selection test, because with one object the read
 * always succeeds.
 *
 * These are the three honest readings. Which one a site wants is a real decision, not a formality:
 *
 *   ValueOr        the panel has to pick something; say what it picks
 *   CheckedIfEqual a box over a disagreeing selection is Undetermined, not a coin flip
 *   AllEqual       "hide this row" / "write this across" may only act on unanimity
 */
namespace DreamDetailsMultiSelect
{
	/** The shared value, or InFallback when the selection disagrees (or the handle is unusable). */
	template<typename T>
	T ValueOr(const TSharedPtr<IPropertyHandle>& InHandle, const T& InFallback)
	{
		T Value{};
		if (!InHandle.IsValid() || InHandle->GetValue(Value) != FPropertyAccess::Success)
		{
			return InFallback;
		}
		return Value;
	}

	/** Checked / Unchecked on unanimity, Undetermined otherwise -- which is what the box should draw. */
	template<typename T>
	ECheckBoxState CheckedIfEqual(const TSharedPtr<IPropertyHandle>& InHandle, const T& InExpected)
	{
		T Value{};
		if (!InHandle.IsValid() || InHandle->GetValue(Value) != FPropertyAccess::Success)
		{
			return ECheckBoxState::Undetermined;
		}
		return Value == InExpected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	/** True only when every object in the selection agrees on InExpected. Disagreement is false. */
	template<typename T>
	bool AllEqual(const TSharedPtr<IPropertyHandle>& InHandle, const T& InExpected)
	{
		T Value{};
		return InHandle.IsValid()
			&& InHandle->GetValue(Value) == FPropertyAccess::Success
			&& Value == InExpected;
	}
}
