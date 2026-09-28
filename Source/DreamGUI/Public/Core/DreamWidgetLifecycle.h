// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Where a widget is in its life: one value in place of the two flags that used to stand for it,
 * registered and begun play. Being parked -- waiting for a parent -- is not one of the states: it is a
 * question of where the widget hangs rather than how far it has come, and a parked widget is
 * registered.
 *
 * The steps between the states are fixed, and each one is idempotent:
 *
 *     Register     Constructed -> Registered     nothing once Registered or later; an error once Destroyed
 *     BeginPlay    Registered  -> BegunPlay      nothing in any other state
 *     EndPlay      BegunPlay   -> Registered     nothing in any other state
 *     Unregister   Registered  -> Constructed    nothing in any other state
 *     Destroy      any         -> Destroyed      first taking the exit steps still owed: EndPlay, then Unregister
 *
 * Parents register before their children and unregister before them too, and EndPlay finishes for a whole
 * tree before any of it unregisters. A widget may still be moved between parents while it unregisters or
 * ends play; what decides its next state is the step asked of it, never where it happens to hang.
 *
 * UDreamWidget keeps one (GetLifecycle) and takes each step through the call of the same name:
 * OnRegister, BeginPlay, EndPlay, OnUnregister, DestroyWidget.
 */
enum class EDreamWidgetLifecycle : uint8
{
	Constructed,
	Registered,
	BegunPlay,
	Destroyed,
};

/** The steps of the table above. */
enum class EDreamWidgetLifecycleStep : uint8
{
	Register,
	BeginPlay,
	EndPlay,
	Unregister,
	Destroy,
};

namespace DreamUI
{
	/**
	 * The state InStep leaves a widget in that was InFrom -- InFrom itself where the step does nothing. A
	 * Destroy lands on Destroyed from anywhere; the exit steps it owes on the way are the caller's to take.
	 */
	constexpr EDreamWidgetLifecycle NextLifecycle(EDreamWidgetLifecycle InFrom, EDreamWidgetLifecycleStep InStep)
	{
		if (InFrom == EDreamWidgetLifecycle::Destroyed || InStep == EDreamWidgetLifecycleStep::Destroy)
		{
			return EDreamWidgetLifecycle::Destroyed;
		}
		switch (InStep)
		{
		case EDreamWidgetLifecycleStep::Register:
			return InFrom == EDreamWidgetLifecycle::Constructed ? EDreamWidgetLifecycle::Registered : InFrom;
		case EDreamWidgetLifecycleStep::BeginPlay:
			return InFrom == EDreamWidgetLifecycle::Registered ? EDreamWidgetLifecycle::BegunPlay : InFrom;
		case EDreamWidgetLifecycleStep::EndPlay:
			return InFrom == EDreamWidgetLifecycle::BegunPlay ? EDreamWidgetLifecycle::Registered : InFrom;
		case EDreamWidgetLifecycleStep::Unregister:
			return InFrom == EDreamWidgetLifecycle::Registered ? EDreamWidgetLifecycle::Constructed : InFrom;
		default:
			return InFrom;
		}
	}

	/** Whether asking InStep of InFrom is a mistake to report rather than a no-op: registering a destroyed widget. */
	constexpr bool IsLifecycleMistake(EDreamWidgetLifecycle InFrom, EDreamWidgetLifecycleStep InStep)
	{
		return InFrom == EDreamWidgetLifecycle::Destroyed && InStep == EDreamWidgetLifecycleStep::Register;
	}
}

inline const TCHAR* LexToString(EDreamWidgetLifecycle InLifecycle)
{
	switch (InLifecycle)
	{
	case EDreamWidgetLifecycle::Constructed: return TEXT("Constructed");
	case EDreamWidgetLifecycle::Registered: return TEXT("Registered");
	case EDreamWidgetLifecycle::BegunPlay: return TEXT("BegunPlay");
	case EDreamWidgetLifecycle::Destroyed: return TEXT("Destroyed");
	default: return TEXT("Unknown");
	}
}
