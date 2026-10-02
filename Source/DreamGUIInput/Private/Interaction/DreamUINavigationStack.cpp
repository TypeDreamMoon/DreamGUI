// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUINavigationStack.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UISelectable.h"
#include "Interaction/DreamUITextInputTarget.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Engine/World.h"

namespace DreamUINavigationStackLocal
{
	/**
	 * InWidget and every widget above it in play -- registered, in a world that never began play (the designer, a bare
	 * test world). A widget being torn down ends play parents first, so one coming down shows it above itself first.
	 */
	bool IsInPlay(const UDreamWidget* InWidget)
	{
		UWorld* WidgetWorld = InWidget->GetWorld();
		const UDreamUIManagerWorldSubsystem* Manager = WidgetWorld != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(WidgetWorld) : nullptr;
		const bool bWorldPlays = Manager != nullptr && Manager->HasBegunPlay();
		for (const UDreamWidget* Walker = InWidget; Walker != nullptr; Walker = Walker->GetParent())
		{
			if (!IsValid(Walker) || (bWorldPlays ? !Walker->HasBegunPlay() : !Walker->HasRegistered()))
			{
				return false;
			}
		}
		return true;
	}
}

bool UDreamUINavigationStack::ShouldCreateSubsystem(UObject* Outer) const
{
	//same gate as the other UI subsystems: nothing navigates on a dedicated server
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUINavigationStack::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
}

void UDreamUINavigationStack::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the
	// world had no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	Super::Deinitialize();
}

void UDreamUINavigationStack::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	bTornDownForWorld = true;
	Scopes.Reset();
}

UDreamUINavigationStack* UDreamUINavigationStack::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	return IsValid(World) ? World->GetSubsystem<UDreamUINavigationStack>() : nullptr;
}

void UDreamUINavigationStack::RemoveStaleScopes()
{
	Scopes.RemoveAll([](const TWeakObjectPtr<UDreamUINavigationScope>& Scope) { return !Scope.IsValid(); });
}

void UDreamUINavigationStack::PushScope(UDreamUINavigationScope* InScope)
{
	if (!IsValid(InScope))return;
	RemoveStaleScopes();

	const int32 UserIndex = InScope->GetUserIndex();
	UDreamUINavigationScope* Outgoing = GetActiveScope(UserIndex);
	if (Outgoing != InScope)
	{
		// What the player had focused before this screen came up, whatever screen that was on -- a page with no scope of
		// its own included, which no scope below remembers. PopScope gives it back.
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(this);
		UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(UserIndex) : nullptr;
		InScope->FocusBeforePush = Focused;
		InScope->bHadFocusBeforePush = IsValid(Focused);
	}
	// Whoever is on top is about to lose focus, so give it the chance to record where focus was. Done
	// before the push, because a moment later the answer is "wherever the new scope put it".
	if (Outgoing != nullptr)
	{
		if (Outgoing != InScope)
		{
			Outgoing->RememberFocus(GetFocusedSelectable(this, UserIndex));
			// Being covered is being deactivated. A page that dims itself, stops an animation or drops a
			// poll while a dialog is in front of it had no way to hear about that: the notification only
			// covered "I was popped", so the other three of CommonUI's four cases went unreported.
			Outgoing->NotifyScopeDeactivated();
		}
	}

	// Re-raising rather than stacking: a scope activated twice is one screen, and a second entry would
	// need two pops to close and would leave a copy of itself behind on the first.
	Scopes.Remove(InScope);
	Scopes.Add(InScope);
	InScope->NotifyScopeActivated();

	if (UUISelectable* Target = InScope->ResolveFocusTarget())
	{
		FocusSelectable(this, UserIndex, Target);
		return;
	}
	// Nothing usable, as ResolveFocusTarget reads it -- which is the drawn cache, still saying asleep for a screen whose
	// scope is pushed from inside the walk that wakes it (bActivateWhenEnabled, the screen shown by an ancestor). Its own
	// picks are offered once more, in its order, to FocusForNavigation, which reads what that walk is settling on; on a
	// settled screen it refuses them for the reasons ResolveFocusTarget passed them over, and nothing changes.
	if (InScope->GetRestoreLastFocus() && FocusSelectable(this, UserIndex, InScope->GetRememberedFocus()))
	{
		return;
	}
	FocusSelectable(this, UserIndex, InScope->GetDesiredFocusTarget());
}

void UDreamUINavigationStack::PopScope(UDreamUINavigationScope* InScope)
{
	if (!IsValid(InScope))return;
	RemoveStaleScopes();

	const int32 UserIndex = InScope->GetUserIndex();
	const bool bWasOnTop = GetActiveScope(UserIndex) == InScope;
	if (Scopes.Remove(InScope) == 0)
	{
		return;//never pushed, or already popped
	}
	if (bWasOnTop)
	{
		// Only the scope that actually held focus has anything worth remembering. Recording focus for
		// one buried in the middle would overwrite its memory with whatever the top scope was doing.
		InScope->RememberFocus(GetFocusedSelectable(this, UserIndex));
	}
	InScope->NotifyScopeDeactivated();
	if (bWasOnTop)
	{
		// Uncovered is activated: whatever was underneath is the screen in front now. Told before focus
		// is restored, so a handler that re-targets focus in response is honoured rather than overwritten
		// by the restore a line later -- focus it moved out of the closing screen is the player's now.
		if (UDreamUINavigationScope* Incoming = GetActiveScope(UserIndex))
		{
			Incoming->NotifyScopeActivated();
		}
		RestoreFocusAfterPop(InScope);
	}
	InScope->FocusBeforePush = nullptr;
	InScope->bHadFocusBeforePush = false;
}

void UDreamUINavigationStack::RestoreFocusAfterPop(UDreamUINavigationScope* InPopped)
{
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(this);
	if (Services == nullptr || !IsValid(InPopped))
	{
		return;
	}
	const int32 UserIndex = InPopped->GetUserIndex();
	const UDreamWidget* PoppedWidget = InPopped->GetWidget();
	auto IsInPopped = [PoppedWidget](const UDreamWidget* InWidget)
	{
		return IsValid(InWidget) && IsValid(PoppedWidget) && (InWidget == PoppedWidget || InWidget->IsChildOf(PoppedWidget));
	};
	UDreamWidget* Now = Services->GetFocusedWidget(UserIndex);
	const bool bInside = IsInPopped(Now);
	// Gone nowhere since the push -- a scrim pressed, a control hidden under the player -- from a player who had some.
	const bool bNowhere = !IsValid(Now) && InPopped->bHadFocusBeforePush;
	if (!bInside && !bNowhere)
	{
		// The player moved it elsewhere while the screen was up, or never had any: it is not taken back. This used to
		// refocus the screen underneath whatever the player had done meanwhile.
		return;
	}
	// In play as well: a screen coming down with the one being closed -- the page a dialog sat on, torn down together --
	// is no place to give focus back to. FocusForNavigation alone takes a widget that is only registered.
	auto TryFocus = [Services, UserIndex, &IsInPopped](UDreamWidget* InCandidate)
	{
		return IsValid(InCandidate) && !IsInPopped(InCandidate) && DreamUINavigationStackLocal::IsInPlay(InCandidate)
			&& Services->FocusForNavigation(InCandidate, UserIndex);
	};
	// The screen underneath decides first -- its memory, its authored target, its first control -- and what had focus
	// at the push answers when there is no screen underneath to ask: a modal over a page with no scope.
	const UDreamUINavigationScope* Underneath = GetActiveScope(UserIndex);
	const UUISelectable* Target = Underneath != nullptr ? Underneath->ResolveFocusTarget() : nullptr;
	if (TryFocus(Target != nullptr ? Target->GetWidget() : nullptr) || TryFocus(InPopped->FocusBeforePush.Get()))
	{
		return;
	}
	if (bInside)
	{
		Services->ClearFocus(Now, UserIndex, 0);
	}
}

UDreamUINavigationScope* UDreamUINavigationStack::GetActiveScope(int32 InUserIndex) const
{
	for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
	{
		UDreamUINavigationScope* Scope = Scopes[Index].Get();
		if (IsValid(Scope) && Scope->GetUserIndex() == InUserIndex)
		{
			return Scope;
		}
	}
	return nullptr;
}

UDreamUINavigationScope* UDreamUINavigationStack::FindConfiningScopeFor(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	if (!IsValid(InWidget))return nullptr;

	TSet<int32> UsersConsidered;
	for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
	{
		UDreamUINavigationScope* Scope = Scopes[Index].Get();
		if (!IsValid(Scope))continue;
		const int32 User = Scope->GetUserIndex();
		if (InUserIndex != INDEX_NONE && User != InUserIndex)continue;

		// Walking down means the first entry seen for a player is that player's top scope, and only a
		// top scope confines -- the ones below it are precisely what something was pushed in front of.
		// Marked before the confine test, so a top scope that does not confine leaves that player free
		// rather than handing the job down to a buried scope that once did.
		bool bAlreadySeen = false;
		UsersConsidered.Add(User, &bAlreadySeen);
		if (bAlreadySeen)continue;

		if (!Scope->GetConfineNavigation())continue;
		UDreamWidget* ScopeWidget = Scope->GetWidget();
		if (!IsValid(ScopeWidget))continue;
		// Confinement keeps focus in; it does not drag focus in from outside. Something focused
		// elsewhere while the scope is still opening would otherwise be unable to move at all --
		// every candidate out of bounds, every direction refused.
		if (InWidget == ScopeWidget || InWidget->IsChildOf(ScopeWidget))
		{
			return Scope;
		}
	}
	return nullptr;
}

UDreamWidget* UDreamUINavigationStack::FindConfiningWidgetFor(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	if (!IsValid(InWidget))return nullptr;
	// A popup first: it is in front of every screen, and lifted to the screen root it is inside none of their subtrees --
	// a dropdown's list opened from a dialog would otherwise let the pad walk straight out of it onto the page.
	if (const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(this))
	{
		if (UDreamWidget* Popup = Popups->FindPopupContaining(InWidget, InUserIndex))
		{
			return Popup;
		}
	}
	UDreamUINavigationScope* Scope = FindConfiningScopeFor(InWidget, InUserIndex);
	return Scope != nullptr ? Scope->GetWidget() : nullptr;
}

void UDreamUINavigationStack::GetScopeStack(int32 InUserIndex, TArray<UDreamUINavigationScope*>& OutScopes) const
{
	OutScopes.Reset();
	for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
	{
		UDreamUINavigationScope* Scope = Scopes[Index].Get();
		if (IsValid(Scope) && Scope->GetUserIndex() == InUserIndex)
		{
			OutScopes.Add(Scope);
		}
	}
}

bool UDreamUINavigationStack::HandleBack(int32 InUserIndex)
{
	// A field being edited gets it first, whatever screen it is on. Escape out of a half-typed name is
	// the near-universal meaning of Back at that moment, and closing the screen instead would throw the
	// edit away along with the screen.
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, InUserIndex);
	if (IsValid(EventSystem))
	{
		if (UDreamWidget* Selected = EventSystem->GetCurrentSelectedComponent(0))
		{
			if (IDreamUITextInputTarget* TextInput = Cast<IDreamUITextInputTarget>(
				Selected->GetComponentByInterface(UDreamUITextInputTarget::StaticClass())))
			{
				if (TextInput->IsTextInputActive())
				{
					// Through the CANCEL road, not the plain end of an edit: Back is the player saying
					// "throw this away", and a field asked to revert on escape has to hear which of the
					// two moments this was. Without the knob the two roads are the same call.
					TextInput->CancelTextInput();
					return true;
				}
			}
		}
	}

	// Then an open popup, which is in front of every screen: Back closes the player's top one and goes no further. It
	// used to reach the screens straight away -- a dropdown open inside a dialog cancelled the dialog and left the list
	// up, and one open on a page closed the page.
	if (UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(this); Popups != nullptr && Popups->HandleBack(InUserIndex))
	{
		return true;
	}

	// Snapshot before walking: handling Back closes screens, which mutates the stack underneath us.
	TArray<UDreamUINavigationScope*> Stack;
	GetScopeStack(InUserIndex, Stack);
	for (UDreamUINavigationScope* Scope : Stack)
	{
		if (!IsValid(Scope))continue;
		if (Scope->HandleBackAction())
		{
			return true;
		}
		if (Scope->GetCloseOnBack())
		{
			Scope->DeactivateScope();
			return true;
		}
		// Neither handled nor closes: transparent to Back, so the screen underneath gets its turn.
	}
	return false;
}

UUISelectable* UDreamUINavigationStack::GetFocusedSelectable(const UObject* WorldContextObject, int32 InUserIndex)
{
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UObject*>(WorldContextObject), InUserIndex);
	if (!IsValid(EventSystem))return nullptr;
	UDreamWidget* Selected = EventSystem->GetCurrentSelectedComponent(0);
	return IsValid(Selected) ? Selected->GetComponent<UUISelectable>() : nullptr;
}

bool UDreamUINavigationStack::FocusSelectable(const UObject* WorldContextObject, int32 InUserIndex, UUISelectable* InSelectable)
{
	if (!IsValid(InSelectable))return false;
	UDreamWidget* Widget = InSelectable->GetWidget();
	if (!IsValid(Widget))return false;
	// The selection and the cursor together, in the order that keeps them agreeing -- and refused for a control focus
	// cannot be on now, which the event system's selection used to give it anyway.
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(WorldContextObject);
	return Services != nullptr && Services->FocusForNavigation(Widget, InUserIndex);
}
