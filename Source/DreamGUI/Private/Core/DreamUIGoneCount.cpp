// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIGoneCount.h"

#include "DreamGUI.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Object.h"
#include "UObject/UObjectArray.h"
#include <atomic>

int32 GDreamUIVerifyKeptPointers = 0;
static FAutoConsoleVariableRef CVarDreamUIVerifyKeptPointers(
	TEXT("r.DreamUI.VerifyKeptPointers"),
	GDreamUIVerifyKeptPointers,
	TEXT("1: every object DreamGUI keeps while the count of objects gone reads the same -- a widget's render canvas, a canvas's ")
	TEXT("widget and render layers, the UI manager's canvases, an animated property's bound object -- is looked up as well where ")
	TEXT("it is used, and a disagreement is an error and an ensure. For tests: it costs the look-ups the kept pointers save."),
	ECVF_Default);

namespace DreamUIGoneLocal
{
	std::atomic<uint64> Count{ 1 };
	std::atomic<uint64> KeptDisagreements{ 0 };

	/** Every deletion of every object moves the count on: a deleted object's memory may be another object by the next frame. */
	class FDeleteListener final : public FUObjectArray::FUObjectDeleteListener
	{
	public:
		virtual void NotifyUObjectDeleted(const UObjectBase* Object, int32 Index) override
		{
			Count.fetch_add(1, std::memory_order_relaxed);
		}
		virtual void OnUObjectArrayShutdown() override
		{
			GUObjectArray.RemoveUObjectDeleteListener(this);
			bListening = false;
		}
		/** Game thread only. */
		bool bListening = false;
	};
	FDeleteListener DeleteListener;
}

uint64 DreamUIGone::Read()
{
	using namespace DreamUIGoneLocal;
	// Listening from the first read: nothing has kept an object by the count before it.
	if (!DeleteListener.bListening)
	{
		DeleteListener.bListening = true;
		GUObjectArray.AddUObjectDeleteListener(&DeleteListener);
		Count.fetch_add(1, std::memory_order_relaxed);
	}
	return Count.load(std::memory_order_acquire);
}

uint64 DreamUIGone::Peek()
{
	using namespace DreamUIGoneLocal;
	return DeleteListener.bListening ? Count.load(std::memory_order_acquire) : 0;
}

void DreamUIGone::Note()
{
	DreamUIGoneLocal::Count.fetch_add(1, std::memory_order_release);
}

void DreamUIGone::StopListening()
{
	using namespace DreamUIGoneLocal;
	if (DeleteListener.bListening)
	{
		DeleteListener.bListening = false;
		GUObjectArray.RemoveUObjectDeleteListener(&DeleteListener);
	}
	Count.fetch_add(1, std::memory_order_relaxed);
}

void DreamUIGone::CheckKept(const void* Kept, const UObject* LookedUp, const TCHAR* Where)
{
	if (Kept == LookedUp)
	{
		return;
	}
	const uint64 Disagreements = DreamUIGoneLocal::KeptDisagreements.fetch_add(1, std::memory_order_relaxed) + 1;
	// The kept pointer is printed, not followed: one that is not what the look-up finds may be gone.
	UE_LOG(DreamGUI, Error, TEXT("DreamGUI kept %p as %s while the count of objects gone read the same, but its look-up finds %s (%llu disagreement(s) so far)."),
		Kept, Where, LookedUp != nullptr ? *GetPathNameSafe(LookedUp) : TEXT("nothing"), Disagreements);
	ensureMsgf(false, TEXT("A pointer DreamGUI kept disagrees with its look-up: %s. See the log."), Where);
}

uint64 DreamUIGone::GetKeptDisagreements()
{
	return DreamUIGoneLocal::KeptDisagreements.load(std::memory_order_relaxed);
}
