// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/UISelectable.h"
#include "Core/Components/DreamWidget.h"

UDreamUINavigationScope::UDreamUINavigationScope()
{
	bStartWithTickEnabled = false;
}

void UDreamUINavigationScope::OnEnable()
{
	Super::OnEnable();
	if (bActivateWhenEnabled)
	{
		ActivateScope();
	}
}

void UDreamUINavigationScope::OnDisable()
{
	// Whether or not it auto-activated: a hidden scope must not stay on the stack holding focus for a
	// screen nobody can see.
	DeactivateScope();
	Super::OnDisable();
}

void UDreamUINavigationScope::OnUnregister()
{
	DeactivateScope();
	// A binding outliving the screen that registered it would keep answering for a screen that no
	// longer exists; the router's stale sweep would get there eventually, but not before the next key.
	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		Router->UnregisterScope(this);
	}
	Super::OnUnregister();
}

void UDreamUINavigationScope::ActivateScope()
{
	if (UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(this))
	{
		bIsScopeActive = true;
		Stack->PushScope(this);
	}
}

void UDreamUINavigationScope::DeactivateScope()
{
	if (!bIsScopeActive)return;
	bIsScopeActive = false;
	if (UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(this))
	{
		Stack->PopScope(this);
	}
}

void UDreamUINavigationScope::RememberFocus(UUISelectable* InSelectable)
{
	if (!IsValid(InSelectable))
	{
		return;//nothing focused: keep the older memory rather than replacing it with nothing
	}
	UDreamWidget* ScopeWidget = GetWidget();
	UDreamWidget* FocusWidget = InSelectable->GetWidget();
	// Only remember focus that was ours. A scope pushed while focus was still elsewhere would
	// otherwise adopt the previous screen's control and restore to it later.
	if (IsValid(ScopeWidget) && IsValid(FocusWidget) && (FocusWidget == ScopeWidget || FocusWidget->IsChildOf(ScopeWidget)))
	{
		RememberedFocus = InSelectable;
	}
}

UUISelectable* UDreamUINavigationScope::ResolveFocusTarget() const
{
	auto IsUsable = [](UUISelectable* Selectable)
	{
		return IsValid(Selectable) && Selectable->IsInteractable() && Selectable->GetCanNavigateHere();
	};

	if (bRestoreLastFocus && IsUsable(RememberedFocus.Get()))
	{
		return RememberedFocus.Get();
	}
	if (IsUsable(DesiredFocusTarget.Get()))
	{
		return DesiredFocusTarget.Get();
	}
	// Nothing authored and nothing remembered: fall back to the first navigable control inside this
	// scope. Better than the old global default, which reached across every screen in the level.
	return UUISelectable::FindDefaultSelectableIn(const_cast<UDreamUINavigationScope*>(this), GetWidget());
}

bool UDreamUINavigationScope::HandleBackAction_Implementation()
{
	// Nothing by default, which lets Back fall through to bCloseOnBack and then to the screen below.
	// A screen that wants to intercept overrides this in Blueprint or C++.
	return false;
}

void UDreamUINavigationScope::NotifyScopeActivated()
{
	bIsScopeActive = true;
	ReceiveOnScopeActivated();
	OnScopeActivated.Broadcast(this);
}

void UDreamUINavigationScope::NotifyScopeDeactivated()
{
	bIsScopeActive = false;
	ReceiveOnScopeDeactivated();
	OnScopeDeactivated.Broadcast(this);
}
