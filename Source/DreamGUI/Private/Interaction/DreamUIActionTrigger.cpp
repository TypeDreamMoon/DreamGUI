// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIActionTrigger.h"

#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamUINavigationScope.h"

UDreamUIActionTrigger::UDreamUIActionTrigger()
{
	bStartWithTickEnabled = false;//everything here is event driven
}

void UDreamUIActionTrigger::OnEnable()
{
	Super::OnEnable();
	RegisterAction();
}

void UDreamUIActionTrigger::OnDisable()
{
	// A hidden or deactivated widget is one the player cannot click, so its key must stop working too
	// -- and its prompt must leave the bar with it. That symmetry is the whole reason the binding lives
	// on the widget rather than being registered once by the screen.
	UnregisterAction();
	Super::OnDisable();
}

void UDreamUIActionTrigger::OnUnregister()
{
	UnregisterAction();
	Super::OnUnregister();
}

void UDreamUIActionTrigger::OnInteractableChanged(bool Interactable)
{
	Super::OnInteractableChanged(Interactable);
	// Disabled is not hidden: OnDisable does not run for a widget that is merely uninteractable, and a
	// greyed-out button answering its key is exactly the bug this class would otherwise introduce.
	if (Interactable)
	{
		RegisterAction();
	}
	else
	{
		UnregisterAction();
	}
}

void UDreamUIActionTrigger::SetAction(const FDataTableRowHandle& InAction)
{
	if (Action.DataTable == InAction.DataTable && Action.RowName == InAction.RowName)return;
	const bool bWasBound = IsActionBound();
	UnregisterAction();
	Action = InAction;
	if (bWasBound)
	{
		RegisterAction();//it was live a moment ago; the new action should be live too
	}
}

void UDreamUIActionTrigger::SetUserIndex(int32 Value)
{
	if (UserIndex == Value)return;
	const bool bWasBound = IsActionBound();
	UnregisterAction();
	UserIndex = Value;
	if (bWasBound)
	{
		RegisterAction();
	}
}

UDreamUINavigationScope* UDreamUIActionTrigger::FindOwningScope()const
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))return nullptr;
	// The nearest scope at or above this widget is the screen the button is part of, which is exactly
	// the screen whose keys it should answer. A widget with no scope above it belongs to no screen, and
	// the router treats that as a global binding -- which is what bBindGlobally asks for explicitly.
	return Widget->GetComponentInParent<UDreamUINavigationScope>(true);
}

void UDreamUIActionTrigger::RegisterAction()
{
	if (IsActionBound())return;
	if (Action.IsNull())return;//nothing authored; this component is simply inert

	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget) || !Widget->GetInteractableInHierarchy())
	{
		return;//a key must not reach a button the player could not have clicked
	}
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this);
	if (Router == nullptr)return;

	FDreamUIActionExecutedDelegate Callback;
	Callback.BindUFunction(this, TEXT("HandleActionExecuted"));
	UDreamUINavigationScope* Scope = bBindGlobally ? nullptr : FindOwningScope();
	Handle = Router->RegisterAction(Scope, Action, Callback, UserIndex, bDisplayInActionBar);
}

void UDreamUIActionTrigger::UnregisterAction()
{
	if (!IsActionBound())return;
	if (UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(this))
	{
		Router->UnregisterAction(Handle);
	}
	Handle = FDreamUIActionHandle();
}

void UDreamUIActionTrigger::HandleActionExecuted()
{
	TriggerAction();
}

void UDreamUIActionTrigger::TriggerAction()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))return;
	// Re-checked at the moment of firing, not only at registration: a screen can disable a button
	// between the keypress and here -- a hold action that finishes after the screen has greyed itself
	// out is the ordinary case -- and a click into a disabled button is exactly what must not happen.
	if (!Widget->GetInteractableInHierarchy() || !Widget->GetRenderVisibleInHierarchy())return;

	// A pointer event of its own, never one from the event system's map. Dispatching through the live
	// pointer would rewrite the mouse's state and would tell every global listener (tooltips, drag and
	// drop) that the mouse had clicked, which it did not. This one exists purely to carry the widget.
	if (!IsValid(SyntheticPointerEvent))
	{
		SyntheticPointerEvent = NewObject<UDreamPointerEventData>(this);
	}
	SyntheticPointerEvent->PointerID = INDEX_NONE;//not a pointer anyone can look up; that is deliberate
	SyntheticPointerEvent->UserIndex = UserIndex;
	SyntheticPointerEvent->PressWidget = Widget;
	SyntheticPointerEvent->EnterWidget = Widget;
	SyntheticPointerEvent->PointerPosition = Widget->GetWorldLocation();
	SyntheticPointerEvent->PressPointerPosition = SyntheticPointerEvent->PointerPosition;

	// The static dispatch, which reaches the widget's own click handlers -- UUISelectable's included,
	// so a button visibly presses -- and bubbles like a real click, without going through the event
	// system's broadcast. The action bar already told the player which key does this; announcing it as
	// a pointer click to the whole world as well would be a lie about where it came from.
	UDreamEventSystem::ExecuteEvent_OnPointerClick(Widget, SyntheticPointerEvent, true);
	ReceiveOnActionTriggered();
}
