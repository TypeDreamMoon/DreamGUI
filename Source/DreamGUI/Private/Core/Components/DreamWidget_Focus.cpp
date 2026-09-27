// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Event/DreamEventSystem.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#endif

void UDreamWidget::SetIsFocusable(bool Value)
{
	if (bIsFocusable == Value)
	{
		return;
	}
	bIsFocusable = Value;
	if (!bIsFocusable)
	{
		// A widget that can no longer take focus must not keep the focus it already has. ClearFocus
		// is a no-op unless this widget IS the selected one, and answers nothing at all outside a
		// live game world, so this is safe on every path a property setter can arrive from.
		ClearFocus();
	}
}

bool UDreamWidget::SetFocus(int32 UserIndex, int32 PointerId)
{
	if (!bIsFocusable || !GetRenderVisibleInHierarchy() || !GetInteractableInHierarchy())
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, true);
		EventSystem->SetSelectWidget(this, EventData);
		// The navigation cursor has to move with focus, or the next directional press starts from
		// wherever focus USED to be and appears to teleport. UDreamUINavigationStack::FocusSelectable
		// already does both halves for the same reason; this entry point only ever did the first.
		EventSystem->SetHighlightedComponentForNavigation(this, PointerId);
		return true;
	}
	return false;
}

bool UDreamWidget::HasFocus(int32 UserIndex, int32 PointerId) const
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), UserIndex))
	{
		return EventSystem->GetCurrentSelectedComponent(PointerId) == this;
	}
	return false;
}

void UDreamWidget::ClearFocus(int32 UserIndex, int32 PointerId)
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, false);
		if (EventData && EventData->SelectedComponent == this)
		{
			EventSystem->SetSelectWidget(nullptr, EventData);
		}
	}
}

void UDreamWidget::NotifyFocusReceived(int32 UserIndex, int32 PointerId)
{
	OnFocusReceived.Broadcast(UserIndex, PointerId);
	if (AccessibleBehavior != EDreamAccessibleBehavior::NotAccessible)
	{
		AnnounceAccessibleText();
	}
}

void UDreamWidget::NotifyFocusLost(int32 UserIndex, int32 PointerId)
{
	OnFocusLost.Broadcast(UserIndex, PointerId);
}

void UDreamWidget::AnnounceAccessibleText(const FText& Announcement)
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FText TextToAnnounce = Announcement;
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = AccessibleBehavior == EDreamAccessibleBehavior::Summary ? AccessibleSummaryText : AccessibleText;
	}
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = FText::FromString(DisplayName);
	}
	if (!TextToAnnounce.IsEmpty())
	{
		FSlateApplication::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(TextToAnnounce.ToString());
	}
#endif
}

int32 UDreamWidget::GetLocalPlayerIndexOf(const APlayerController* InPlayerController)
{
	// The same index UDreamEventSystem::GetPlayerController reads back, so "this widget's player" and
	// "this event system's player" cannot drift apart: the position of the local player in the game
	// instance's list, which is what UserIndex has always meant here.
	if (!IsValid(InPlayerController))
	{
		return 0;
	}
	const ULocalPlayer* LocalPlayer = InPlayerController->GetLocalPlayer();
	if (LocalPlayer == nullptr)
	{
		return 0;
	}
	const UGameInstance* GameInstance = LocalPlayer->GetGameInstance();
	if (GameInstance == nullptr)
	{
		return 0;
	}
	const int32 Index = GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer);
	return Index != INDEX_NONE ? Index : 0;
}

APlayerController* UDreamWidget::GetOwningPlayer() const
{
	// Whoever hosts this widget owns it. The walk starts at the PARENT because a user widget answers
	// this for itself (its override checks an explicitly set controller first and then calls this),
	// and an ancestor user widget with no explicit owner does the same thing again from its own
	// position -- so the nearest EXPLICIT owner anywhere up the chain is the one that wins.
	for (const UDreamWidget* Ancestor = GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (const UDreamUserWidget* HostUserWidget = Cast<const UDreamUserWidget>(Ancestor))
		{
			return HostUserWidget->GetOwningPlayer();
		}
	}
	// The whole answer in a single-player game, and what UMG's CreateWidget defaults to.
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetFirstPlayerController() : nullptr;
}

ULocalPlayer* UDreamWidget::GetOwningLocalPlayer() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return IsValid(PlayerController) ? PlayerController->GetLocalPlayer() : nullptr;
}

int32 UDreamWidget::GetOwningPlayerIndex() const
{
	return GetLocalPlayerIndexOf(GetOwningPlayer());
}

UGameInstance* UDreamWidget::GetGameInstance() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetGameInstance() : nullptr;
}

bool UDreamWidget::SetKeyboardFocus()
{
	return SetFocus(GetOwningPlayerIndex());
}

bool UDreamWidget::HasKeyboardFocus() const
{
	return HasFocus(GetOwningPlayerIndex());
}

bool UDreamWidget::SetUserFocus(APlayerController* InPlayerController)
{
	return SetFocus(GetLocalPlayerIndexOf(InPlayerController));
}

bool UDreamWidget::HasUserFocus(APlayerController* InPlayerController) const
{
	return HasFocus(GetLocalPlayerIndexOf(InPlayerController));
}

void UDreamWidget::ClearKeyboardFocus()
{
	ClearFocus(GetOwningPlayerIndex());
}

int32 UDreamWidget::GetLocalPlayerCountForQueries() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	// At least one: outside a game instance -- a test world, the designer preview -- user index 0 is
	// still a meaningful thing to ask about, and answering "no players" would make every one of the
	// any-user queries below silently false.
	return GameInstance != nullptr ? FMath::Max(GameInstance->GetNumLocalPlayers(), 1) : 1;
}

bool UDreamWidget::HasAnyUserFocus() const
{
	const int32 PlayerCount = GetLocalPlayerCountForQueries();
	for (int32 UserIndex = 0; UserIndex < PlayerCount; ++UserIndex)
	{
		if (HasFocus(UserIndex))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasFocusedDescendantForUser(int32 InUserIndex) const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		UDreamWidget* Focused = EventSystem->GetCurrentSelectedComponent(Entry.Key);
		// Descendants, not "this or its descendants" -- UMG draws the same line, and a widget asking
		// whether something INSIDE it has focus already knows whether it has focus itself.
		if (IsValid(Focused) && Focused != this && Focused->IsChildOf(this))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasFocusedDescendants() const
{
	const int32 PlayerCount = GetLocalPlayerCountForQueries();
	for (int32 UserIndex = 0; UserIndex < PlayerCount; ++UserIndex)
	{
		if (HasFocusedDescendantForUser(UserIndex))
		{
			return true;
		}
	}
	return false;
}

bool UDreamWidget::HasUserFocusedDescendants(APlayerController* InPlayerController) const
{
	return HasFocusedDescendantForUser(GetLocalPlayerIndexOf(InPlayerController));
}

bool UDreamWidget::IsHovered() const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), GetOwningPlayerIndex());
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		if (PointerEvent->EnterWidget.Get() == this)
		{
			return true;
		}
		// The enter STACK as well, so a button still reads as hovered while the pointer is over its
		// own label. Slate gets that for free because hover propagates to parents; here the stack is
		// where that fact lives.
		for (const TObjectPtr<UDreamWidget>& Entered : PointerEvent->EnterWidgetStack)
		{
			if (Entered.Get() == this)
			{
				return true;
			}
		}
	}
	return false;
}

bool UDreamWidget::HasMouseCapture() const
{
	return HasMouseCaptureByUser(GetOwningPlayerIndex(), INDEX_NONE);
}

bool UDreamWidget::HasMouseCaptureByUser(int32 InUserIndex, int32 InPointerIndex) const
{
	UDreamEventSystem* EventSystem =
		UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), InUserIndex);
	if (EventSystem == nullptr)
	{
		return false;
	}
	for (const TPair<int, TObjectPtr<UDreamPointerEventData>>& Entry : EventSystem->GetPointerEventDataMap())
	{
		if (InPointerIndex >= 0 && Entry.Key != InPointerIndex)
		{
			continue;
		}
		const UDreamPointerEventData* PointerEvent = Entry.Value;
		if (!IsValid(PointerEvent))
		{
			continue;
		}
		// Held down AND pressed on this widget: that pointer's drag and its release go here whatever
		// it travels over in between, which is the whole of what capture buys a caller.
		if (PointerEvent->bNowIsTriggerPressed && PointerEvent->PressWidget.Get() == this)
		{
			return true;
		}
	}
	return false;
}

UDreamWidgetNavigation* UDreamWidget::GetNavigation() const
{
	return GetComponent<UDreamWidgetNavigation>();
}

UDreamWidgetNavigation* UDreamWidget::GetOrCreateNavigation()
{
	if (UDreamWidgetNavigation* Existing = GetComponent<UDreamWidgetNavigation>())
	{
		return Existing;
	}
	return Cast<UDreamWidgetNavigation>(AddComponent(UDreamWidgetNavigation::StaticClass()));
}

const UDreamWidget* UDreamWidget::GetRestrictNavigationAreaWidget() const
{
	if (bRestrictNavigationArea)
	{
		return this;
	}
	if (Parent.IsValid())
	{
		return Parent->GetRestrictNavigationAreaWidget();
	}
	return nullptr;
}

void UDreamWidget::SetRestrictNavigationArea(bool Value)
{
	bRestrictNavigationArea = Value;
}

void UDreamWidget::SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule Value)
{
	NavigationBoundaryRule = Value;
}
