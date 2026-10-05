// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIBindingObserver.h"

#include "INotifyFieldValueChanged.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

/*
 * The state lives in a shared object the delegates reach weakly, not in FImpl itself. A delegate is bound weakly to
 * the LIFETIME OWNER, which covers the owner dying; it does not cover the observer being destroyed while its owner
 * lives on (a widget dropping one observer for another), and a broadcast in that window would call into freed memory.
 * The weak pointer turns that call into nothing. It also lets a report in progress keep the state alive (a pin) while
 * the client it reports to stops, re-aims or destroys the observer.
 *
 * Nothing here iterates a container while calling out. A broadcast handler works out what to re-aim and whom to tell
 * first, re-aims, and then reports from a local list of client ids -- so a client that writes values, broadcasts again,
 * adds a path, calls SetRoot or Stop from inside its report finds every container in a consistent state, and the
 * report loop never touches one of them again. Stop and SetRoot bump a generation, which ends a report loop they
 * interrupted: the paths it was reporting were aimed at objects that are no longer watched, and the caller re-applies
 * its values itself after a SetRoot.
 */

namespace DreamUIBindingObserverLocal
{
	using UE::FieldNotification::FFieldId;

	/** One segment of one path: the object it is read on, as last aimed, and the subscription it holds there. */
	struct FLink
	{
		/** Null past an unset object, or before the observer started. */
		TWeakObjectPtr<UObject> Owner;
		/** 0 when the owner cannot announce this member (not INotifyFieldValueChanged, or not one of its fields). */
		uint32 SubscriptionId = 0;
	};

	struct FPath
	{
		TArray<FName> Segments;
		TArray<int32> Clients;
		/** One per segment. */
		TArray<FLink> Links;
	};

	/** One delegate on one (object, field), shared by every segment of every path that reads that field there. */
	struct FSubscription
	{
		uint32 Id = 0;
		TWeakObjectPtr<UObject> Object;
		FFieldId FieldId;
		FDelegateHandle Handle;
		int32 References = 0;
	};

	bool SameSegments(const TArray<FName>& InA, TConstArrayView<FName> InB)
	{
		if (InA.Num() != InB.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < InA.Num(); ++Index)
		{
			if (InA[Index] != InB[Index])
			{
				return false;
			}
		}
		return true;
	}

	/** Take one delegate back off its object, if the object is still there to take it from. */
	void RemoveDelegate(const FSubscription& InSubscription)
	{
		// Even if it is marked garbage: until it is collected its delegate store is live memory, and a broadcast it makes
		// in that window would still arrive. An unreachable object (a collection in progress) answers null and is skipped
		// -- its store is about to go with it.
		UObject* Object = InSubscription.Object.Get(/*bEvenIfPendingKill*/ true);
		if (INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(Object))
		{
			Notifier->RemoveFieldValueChangedDelegate(InSubscription.FieldId, InSubscription.Handle);
		}
	}

	struct FState : public TSharedFromThis<FState>
	{
		TWeakObjectPtr<UObject> Root;
		TWeakObjectPtr<UObject> LifetimeOwner;
		FDreamUIBindingObserver::FOnClientChanged OnChanged;
		TArray<FPath> Paths;
		TArray<FSubscription> Subscriptions;
		uint32 NextSubscriptionId = 1;
		/** Bumped by Start and Stop; a report loop that sees it move stops reporting. */
		uint32 Generation = 0;
		bool bStarted = false;

		int32 FindSubscriptionIndex(uint32 InId) const
		{
			return Subscriptions.IndexOfByPredicate([InId](const FSubscription& Subscription) { return Subscription.Id == InId; });
		}

		/** A reference on the (InObject, InMember) subscription, placing its delegate first if nobody held one. 0 when it cannot be had. */
		uint32 Acquire(UObject* InObject, FName InMember)
		{
			if (!IsValid(InObject) || InMember.IsNone())
			{
				return 0;
			}
			for (FSubscription& Subscription : Subscriptions)
			{
				if (Subscription.Object.Get() == InObject && Subscription.FieldId.GetName() == InMember)
				{
					++Subscription.References;
					return Subscription.Id;
				}
			}

			INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(InObject);
			if (Notifier == nullptr)
			{
				return 0;
			}
			// Asked of the object's own class, which is what it announces under: a subclass of the declared class may
			// announce more, never less.
			const FFieldId FieldId = DreamUIBindingPath::FindFieldId(InObject->GetClass(), InMember);
			if (!FieldId.IsValid())
			{
				return 0;
			}

			const uint32 Id = NextSubscriptionId++;
			if (NextSubscriptionId == 0)
			{
				NextSubscriptionId = 1;
			}
			const TWeakPtr<FState> WeakState = AsShared();
			auto Handler = [WeakState, Id](UObject* /*InObject*/, FFieldId /*InFieldId*/)
			{
				// Pinned for the length of the handling: the client it reports to may destroy the observer.
				if (const TSharedPtr<FState> Pinned = WeakState.Pin())
				{
					Pinned->HandleChanged(Id);
				}
			};
			INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate;
			if (UObject* Owner = LifetimeOwner.Get())
			{
				Delegate = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(Owner, Handler);
			}
			else
			{
				// No owner named: the weak state alone guards the call.
				Delegate = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(Handler);
			}
			const FDelegateHandle Handle = Notifier->AddFieldValueChangedDelegate(FieldId, MoveTemp(Delegate));
			if (!Handle.IsValid())
			{
				return 0;
			}

			FSubscription& Added = Subscriptions.AddDefaulted_GetRef();
			Added.Id = Id;
			Added.Object = InObject;
			Added.FieldId = FieldId;
			Added.Handle = Handle;
			Added.References = 1;
			return Id;
		}

		/** Drop one reference; the last one takes the delegate off. Removed from the array before the object is called. */
		void Release(uint32 InId)
		{
			if (InId == 0)
			{
				return;
			}
			const int32 Index = FindSubscriptionIndex(InId);
			if (Index == INDEX_NONE)
			{
				return;
			}
			if (--Subscriptions[Index].References > 0)
			{
				return;
			}
			const FSubscription Released = Subscriptions[Index];
			Subscriptions.RemoveAtSwap(Index);
			RemoveDelegate(Released);
		}

		/**
		 * Aim segments InFromSegment.. of one path, InOwner holding segment InFromSegment: release what they held,
		 * subscribe where they now land, walking the object members in between. Indexes the path afresh after every
		 * call out, so nothing here holds a reference into Paths across one.
		 */
		void AimFrom(int32 InPathIndex, int32 InFromSegment, UObject* InOwner)
		{
			if (!Paths.IsValidIndex(InPathIndex))
			{
				return;
			}
			UObject* Current = IsValid(InOwner) ? InOwner : nullptr;
			const int32 Num = Paths[InPathIndex].Segments.Num();
			for (int32 Segment = InFromSegment; Segment < Num; ++Segment)
			{
				const uint32 Previous = Paths[InPathIndex].Links[Segment].SubscriptionId;
				const FName Member = Paths[InPathIndex].Segments[Segment];
				// Acquire before Release, so a segment that lands on the subscription it already held keeps the same
				// delegate instead of taking it off and putting it straight back.
				const uint32 Acquired = Current != nullptr ? Acquire(Current, Member) : 0;
				FLink& Link = Paths[InPathIndex].Links[Segment];
				Link.Owner = Current;
				Link.SubscriptionId = Acquired;
				Release(Previous);
				if (Segment + 1 < Num)
				{
					Current = Current != nullptr ? DreamUIBindingPath::ReadObjectMember(Current, Member) : nullptr;
				}
			}
		}

		void AddPath(TConstArrayView<FName> InSegments, int32 InClientId)
		{
			if (InSegments.Num() == 0)
			{
				// Reads nothing that could change: nothing to watch.
				return;
			}
			for (FPath& Path : Paths)
			{
				if (SameSegments(Path.Segments, InSegments))
				{
					Path.Clients.AddUnique(InClientId);
					return;
				}
			}
			FPath& Added = Paths.AddDefaulted_GetRef();
			Added.Segments = TArray<FName>(InSegments.GetData(), InSegments.Num());
			Added.Clients.Add(InClientId);
			Added.Links.SetNum(InSegments.Num());
			if (bStarted)
			{
				AimFrom(Paths.Num() - 1, 0, Root.Get());
			}
		}

		void Start()
		{
			if (bStarted)
			{
				return;
			}
			bStarted = true;
			++Generation;
			UObject* RootObject = Root.Get();
			for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
			{
				AimFrom(PathIndex, 0, RootObject);
			}
		}

		void Stop()
		{
			bStarted = false;
			++Generation;
			for (FPath& Path : Paths)
			{
				for (FLink& Link : Path.Links)
				{
					Link = FLink();
				}
			}
			// Moved out first: removing a delegate is a call into the object, and the array must already be in its final
			// state should anything come back in.
			const TArray<FSubscription> Released = MoveTemp(Subscriptions);
			Subscriptions.Reset();
			for (const FSubscription& Subscription : Released)
			{
				RemoveDelegate(Subscription);
			}
		}

		/** The field of subscription InId changed on its object. */
		void HandleChanged(uint32 InId)
		{
			if (!bStarted)
			{
				return;
			}

			// Which paths read through it, each by the first segment that does -- a path that passes the same field of
			// the same object twice (a node that is its own Next) is re-aimed once, from the earlier.
			TArray<TPair<int32, int32>, TInlineAllocator<8>> Affected;
			for (int32 PathIndex = 0; PathIndex < Paths.Num(); ++PathIndex)
			{
				const TArray<FLink>& Links = Paths[PathIndex].Links;
				for (int32 Segment = 0; Segment < Links.Num(); ++Segment)
				{
					if (Links[Segment].SubscriptionId == InId)
					{
						Affected.Emplace(PathIndex, Segment);
						break;
					}
				}
			}
			if (Affected.Num() == 0)
			{
				return;
			}

			// Everything after the changed member now hangs off whatever it holds now.
			for (const TPair<int32, int32>& Pair : Affected)
			{
				if (!Paths.IsValidIndex(Pair.Key) || Pair.Value + 1 >= Paths[Pair.Key].Segments.Num())
				{
					continue;
				}
				UObject* Owner = Paths[Pair.Key].Links[Pair.Value].Owner.Get();
				UObject* Next = Owner != nullptr ? DreamUIBindingPath::ReadObjectMember(Owner, Paths[Pair.Key].Segments[Pair.Value]) : nullptr;
				AimFrom(Pair.Key, Pair.Value + 1, Next);
			}

			// Each client once per change, however many of its paths ran through the field.
			TArray<int32, TInlineAllocator<8>> Clients;
			for (const TPair<int32, int32>& Pair : Affected)
			{
				if (Paths.IsValidIndex(Pair.Key))
				{
					for (const int32 Client : Paths[Pair.Key].Clients)
					{
						Clients.AddUnique(Client);
					}
				}
			}

			const uint32 ReportGeneration = Generation;
			for (const int32 Client : Clients)
			{
				if (!bStarted || Generation != ReportGeneration)
				{
					break;
				}
				OnChanged.ExecuteIfBound(Client);
			}
		}
	};
}

struct FDreamUIBindingObserver::FImpl
{
	TSharedRef<DreamUIBindingObserverLocal::FState> State = MakeShared<DreamUIBindingObserverLocal::FState>();
};

FDreamUIBindingObserver::FDreamUIBindingObserver()
	: Impl(MakeUnique<FImpl>())
{
}

FDreamUIBindingObserver::~FDreamUIBindingObserver()
{
	if (Impl.IsValid())
	{
		// Stopped rather than torn apart: a report that pinned the state may still be on the stack (its client is what
		// destroyed this), and a stopped state ends that report loop and answers every later broadcast with nothing.
		// OnChanged is left bound for the same reason -- unbinding a delegate from inside its own execution is not safe.
		Impl->State->Stop();
	}
}

void FDreamUIBindingObserver::Initialize(UObject* InRoot, UObject* InLifetimeOwner, FOnClientChanged InOnChanged)
{
	Impl->State->Root = InRoot;
	Impl->State->LifetimeOwner = InLifetimeOwner;
	Impl->State->OnChanged = MoveTemp(InOnChanged);
}

void FDreamUIBindingObserver::AddPath(TConstArrayView<FName> InSegments, int32 InClientId)
{
	Impl->State->AddPath(InSegments, InClientId);
}

void FDreamUIBindingObserver::Start()
{
	Impl->State->Start();
}

void FDreamUIBindingObserver::Stop()
{
	Impl->State->Stop();
}

void FDreamUIBindingObserver::SetRoot(UObject* InRoot)
{
	DreamUIBindingObserverLocal::FState& State = Impl->State.Get();
	State.Root = InRoot;
	// Ends a report loop this interrupted, as Stop would: what it was reporting is about the old root.
	++State.Generation;
	if (!State.bStarted)
	{
		return;
	}
	// The end state of Stop-then-Start, reached by re-aiming every path from the root instead: AimFrom takes the new
	// subscription before it lets the old one go, so whatever the old and the new root share -- the same object handed
	// again, an item both copies show -- keeps its delegate rather than losing it and getting a fresh one. A fresh
	// delegate on an object that is broadcasting right now would be called by that same broadcast (the engine runs
	// delegates added mid-broadcast), which would report a change twice.
	UObject* RootObject = State.Root.Get();
	for (int32 PathIndex = 0; PathIndex < State.Paths.Num(); ++PathIndex)
	{
		State.AimFrom(PathIndex, 0, RootObject);
	}
}

UObject* FDreamUIBindingObserver::GetRoot() const
{
	return Impl->State->Root.Get();
}

bool FDreamUIBindingObserver::IsStarted() const
{
	return Impl->State->bStarted;
}

int32 FDreamUIBindingObserver::GetSubscriptionCount() const
{
	return Impl->State->Subscriptions.Num();
}
