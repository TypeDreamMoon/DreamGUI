// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UITextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Engine/World.h"

bool UDreamUINavigationStack::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUINavigationStack::Deinitialize()
{
	Scopes.Reset();
	Super::Deinitialize();
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
	// Whoever is on top is about to lose focus, so give it the chance to record where focus was. Done
	// before the push, because a moment later the answer is "wherever the new scope put it".
	if (UDreamUINavigationScope* Outgoing = GetActiveScope(UserIndex))
	{
		if (Outgoing != InScope)
		{
			Outgoing->RememberFocus(GetFocusedSelectable(this, UserIndex));
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
	}
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
		RestoreFocusForTopScope(UserIndex);
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

void UDreamUINavigationStack::RestoreFocusForTopScope(int32 InUserIndex)
{
	UDreamUINavigationScope* Scope = GetActiveScope(InUserIndex);
	if (!IsValid(Scope))return;
	if (UUISelectable* Target = Scope->ResolveFocusTarget())
	{
		FocusSelectable(this, InUserIndex, Target);
	}
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
			if (UUITextInput* TextInput = Selected->GetComponent<UUITextInput>())
			{
				if (TextInput->IsInputActive())
				{
					TextInput->DeactivateInput();
					return true;
				}
			}
		}
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
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UObject*>(WorldContextObject), InUserIndex);
	if (!IsValid(EventSystem))return false;

	EventSystem->SetSelectComponentWithDefault(Widget);
	// Navigation is single-pointer and hard-codes id 0 everywhere else; the cursor has to be moved too
	// or the next directional move would start from wherever focus used to be.
	EventSystem->SetHighlightedComponentForNavigation(Widget, 0);
	return true;
}
