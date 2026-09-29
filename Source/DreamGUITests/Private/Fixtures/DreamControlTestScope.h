// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/Components/DreamWidget.h"
#include "UObject/StrongObjectPtr.h"

/**
 * A code-built control, owned for the length of one test and torn down when it leaves.
 *
 * A drop-in for TStrongObjectPtr in a control test, and the reason to prefer it: a control whose
 * tree got REGISTERED logs an Error out of UDreamWidget::BeginDestroy when the collector finally
 * reaches it, because an owner was supposed to have destroyed it first. Any control that hosts
 * another control registers -- a nested Initialize() walks the subtree calling OnRegister -- so
 * the scroll box, the list, the tab view and the dialog all do.
 *
 * What makes that worth a type rather than a line at the end of each test: the collection fires
 * BETWEEN tests, so the Error lands on whichever test happens to be running when it does. The
 * measured symptom was an unrelated designer test going red with six errors it had no part in.
 * The test is the owner; this makes it act like one on every exit path, early returns included.
 */
template<class T>
struct TDreamTestControl
{
	explicit TDreamTestControl(T* InControl)
		: Control(InControl)
	{
	}

	~TDreamTestControl()
	{
		if (T* Owned = Control.Get())
		{
			// The control IS the hierarchy root (UDreamUserWidget is a UDreamWidget), so this takes
			// the nested controls with it.
			Owned->DestroyWidget();
		}
	}

	TDreamTestControl(const TDreamTestControl&) = delete;
	TDreamTestControl& operator=(const TDreamTestControl&) = delete;

	T* operator->() const { return Control.Get(); }
	T& operator*() const { return *Control.Get(); }
	T* Get() const { return Control.Get(); }
	bool IsValid() const { return Control.IsValid(); }

private:
	TStrongObjectPtr<T> Control;
};
