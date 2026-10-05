// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationId.h"
#include "Templates/UniquePtr.h"

/**
 * Reading a dotted member path off live objects: the run-time half of `Player.Stats.Title`.
 *
 * A path is a list of member names starting on a root object. Every segment but the last names an object-typed
 * member (an FObjectPropertyBase) of the object reached so far; the last names whatever the path reads -- a property
 * of any type, or a no-argument function. The compiler checked the shape against the declared classes; a miss here
 * means the class moved underneath a compiled path, and is answered with null rather than a guess.
 */
namespace DreamUIBindingPath
{
	/** The object an object-typed property InMember holds on InOwner. Null when not set, or not an object property. */
	DREAMGUI_API UObject* ReadObjectMember(const UObject* InOwner, FName InMember);

	/**
	 * The object that owns the path's last member: InRoot walked through InSegments[0 .. Num-2]. A one-segment path's
	 * owner is InRoot itself. Null when any object along the way is not set.
	 */
	DREAMGUI_API UObject* ResolveOwner(UObject* InRoot, TConstArrayView<FName> InSegments);

	/** True when every object the path passes through before its last member is set right now. */
	DREAMGUI_API bool IsComplete(UObject* InRoot, TConstArrayView<FName> InSegments);

	/**
	 * Every element of the object array InMember supplies on InOwner: an array property of objects, or -- bIsFunction --
	 * a no-argument function returning one. Empty on any mismatch. The one implementation the `for` and `each`
	 * adapters fetch their items through.
	 */
	DREAMGUI_API void ReadObjectArray(UObject* InOwner, FName InMember, bool bIsFunction, TArray<UObject*>& OutItems);

	/**
	 * The FieldNotify id of InMember on objects of InClass, or an invalid id when such objects cannot announce it:
	 * InClass does not implement INotifyFieldValueChanged, or its descriptor has no field of that name (a FieldNotify
	 * property or a FieldNotify function). Asked of the class, through its default object, rather than of an instance:
	 * a binding's subscribe-or-poll ruling is made once from declared types and does not flip with whichever object a
	 * path happens to hold.
	 */
	DREAMGUI_API UE::FieldNotification::FFieldId FindFieldId(const UClass* InClass, FName InMember);

	/**
	 * The class InMember of InClass holds, statically: an object property's PropertyClass, or a no-argument function's
	 * object return type. Null when the member is not object-typed or does not exist.
	 */
	DREAMGUI_API UClass* FindMemberClass(const UClass* InClass, FName InMember);

	/**
	 * Whether every member along InSegments, starting on InRootClass, can be announced: FindFieldId at each hop, on the
	 * class the previous hop declares. False otherwise, with OutWhyNot -- a sentence fragment naming the hop that cannot
	 * ("'Health' on PlayerVM is not FieldNotify") -- which is what DreamUI.Binding.Dump prints for a polled binding.
	 */
	DREAMGUI_API bool CanNotifyAlong(const UClass* InRootClass, TConstArrayView<FName> InSegments, FString* OutWhyNot = nullptr);
}

/**
 * Watches member paths that start on one root object, and reports -- by client id -- when anything along one changes.
 *
 * For a path `A.B.C` it subscribes to A on the root; while A holds an object, to B on that object; while B holds one,
 * to C on that. A change of A re-aims everything after it (the old objects are unsubscribed, the new ones subscribed)
 * and reports every client of a path through A; a change of C only reports. One delegate per (object, field) however
 * many paths and clients share it. An object that does not implement INotifyFieldValueChanged, or a field it does not
 * announce, is simply not subscribed: whether that is acceptable is the caller's ruling (DreamUIBindingPath::
 * CanNotifyAlong), made before it chose to watch rather than poll.
 *
 * Users: UDreamUserWidget (root = the widget, clients = its property bindings and its loop sources), UDreamUIForAdapter
 * and the list views' UDreamUIEachAdapter (root = one item, clients = the entry bindings of the copy or cell showing
 * it). Not a UObject; held by value or TUniquePtr inside the UObject that uses it, which it names as its lifetime owner.
 */
class DREAMGUI_API FDreamUIBindingObserver
{
public:
	DECLARE_DELEGATE_OneParam(FOnClientChanged, int32 /*InClientId*/);

	FDreamUIBindingObserver();
	~FDreamUIBindingObserver();
	FDreamUIBindingObserver(const FDreamUIBindingObserver&) = delete;
	FDreamUIBindingObserver& operator=(const FDreamUIBindingObserver&) = delete;

	/**
	 * InRoot is where every path starts. InLifetimeOwner is the UObject this observer lives inside; every delegate it
	 * places is bound weakly to that object, so a broadcast that arrives after the owner died is dropped instead of
	 * calling into freed memory. Usually the same object as InRoot (a widget watching itself); an adapter watching an
	 * item passes itself. Call once, before AddPath.
	 */
	void Initialize(UObject* InRoot, UObject* InLifetimeOwner, FOnClientChanged InOnChanged);

	/**
	 * Watch root.InSegments[0].InSegments[1]…, reporting InClientId. A client may watch several paths, and several
	 * clients one path. Takes effect at the next Start (or right away when already started).
	 */
	void AddPath(TConstArrayView<FName> InSegments, int32 InClientId);

	/** Subscribe along every path as far as the objects currently reach. Calling it again while started does nothing. */
	void Start();

	/** Take every delegate this observer placed, on any object, back off. Paths and clients are kept. The destructor calls it. */
	void Stop();

	/**
	 * Re-aim at another root -- a `for` copy handed another item, a list cell recycled for another row: Stop, swap,
	 * and Start again if it was started. Paths and clients are kept. Does not report: the caller re-applies its values
	 * itself, once, rather than once per path.
	 */
	void SetRoot(UObject* InRoot);

	UObject* GetRoot() const;
	bool IsStarted() const;

	/** How many (object, field) subscriptions are live. Diagnostics and tests. */
	int32 GetSubscriptionCount() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
