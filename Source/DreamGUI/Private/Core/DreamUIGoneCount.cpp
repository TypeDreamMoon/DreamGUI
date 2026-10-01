// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIGoneCount.h"

#include "UObject/UObjectArray.h"
#include <atomic>

namespace DreamUIGoneLocal
{
	std::atomic<uint64> Count{ 1 };

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
