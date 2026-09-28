// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIKeyRouting.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputUser.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericApplication.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITextInputTarget.h"
#include "Interaction/DreamUIVirtualCursor.h"

namespace DreamUIKeyRoutingLocal
{
	/**
	 * Written out instead of read through the virtual accept key: a static table can be built before EKeys has
	 * registered its keys (a monolithic build runs every static initialiser first), and an unregistered virtual key
	 * resolves to nothing. What it resolves to -- FGenericPlatformInput::GetGamepadAcceptKey, which no platform this
	 * engine carries overrides -- is Gamepad_FaceButton_Bottom.
	 */
	static const FKey ConfirmKeys[] = {
		EKeys::Enter,
		EKeys::SpaceBar,
		EKeys::Gamepad_FaceButton_Bottom,
	};

	/** A project that wants its own Back puts Back in its action table and binds it; that is offered the key first and wins. */
	static const FKey BackKeys[] = {
		EKeys::Escape,
		EKeys::Gamepad_FaceButton_Right,
	};

	/**
	 * The arrow keys and the D-pad are the rows of Slate's own table (FNavigationConfig's KeyEventRules pairs Left with
	 * Gamepad_DPad_Left, and so on round), so a D-pad moves the highlight exactly as an arrow key does. The left stick's
	 * direction keys stand in for Slate's analog navigation on Gamepad_LeftX/LeftY. Tab is Next -- the sequential-focus
	 * half of navigation, which nothing reached before this table had a row for it.
	 */
	static const TPair<FKey, EDreamUINavigationDirection> DirectionKeys[] = {
		{ EKeys::Left,                     EDreamUINavigationDirection::Left },
		{ EKeys::Right,                    EDreamUINavigationDirection::Right },
		{ EKeys::Up,                       EDreamUINavigationDirection::Up },
		{ EKeys::Down,                     EDreamUINavigationDirection::Down },
		{ EKeys::Tab,                      EDreamUINavigationDirection::Next },
		{ EKeys::Gamepad_DPad_Left,        EDreamUINavigationDirection::Left },
		{ EKeys::Gamepad_DPad_Right,       EDreamUINavigationDirection::Right },
		{ EKeys::Gamepad_DPad_Up,          EDreamUINavigationDirection::Up },
		{ EKeys::Gamepad_DPad_Down,        EDreamUINavigationDirection::Down },
		{ EKeys::Gamepad_LeftStick_Left,   EDreamUINavigationDirection::Left },
		{ EKeys::Gamepad_LeftStick_Right,  EDreamUINavigationDirection::Right },
		{ EKeys::Gamepad_LeftStick_Up,     EDreamUINavigationDirection::Up },
		{ EKeys::Gamepad_LeftStick_Down,   EDreamUINavigationDirection::Down },
	};

	/** A list longer than a screen was reachable a row at a time and no faster; these move by a screenful. */
	static const TPair<FKey, float> PageKeys[] = {
		{ EKeys::PageUp,   -1.0f },
		{ EKeys::PageDown,  1.0f },
	};
	static const TPair<FKey, bool> ExtentKeys[] = {
		{ EKeys::Home, true },
		{ EKeys::End,  false },
	};

	/** Navigation is single-pointer: the navigation cursor lives on the mouse's pointer. */
	static constexpr int32 NavigationPointerID = 0;

	UWorld* WorldOf(const UDreamUIInputUser* InUser)
	{
		return InUser != nullptr ? InUser->GetWorld() : nullptr;
	}

	/** The bindings -- the focused widget first, then the named actions -- with the chord stated or read from the player. */
	bool OfferToBindings(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
	{
		UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(WorldOf(InUser));
		if (Router == nullptr)
		{
			return false;
		}
		if (InModifiers == nullptr)
		{
			return Router->HandleKey(InUser->GetUserIndex(), InKey, bInPressed);
		}
		return Router->HandleKeyWithModifiers(InUser->GetUserIndex(), InKey, bInPressed, InModifiers->IsShiftDown(),
			InModifiers->IsControlDown(), InModifiers->IsAltDown(), InModifiers->IsCommandDown());
	}

	/**
	 * The player's virtual cursor, while it is up, owns the confirm button and the directions: a confirm is its click,
	 * a direction is not its business and is dropped. Both used to act on pointer 0 and rewrite its input type every
	 * frame, so one press of the face button pressed the navigation highlight AND whatever the cursor was over.
	 */
	bool OfferToVirtualCursor(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed)
	{
		UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(WorldOf(InUser));
		const int32 UserIndex = InUser->GetUserIndex();
		if (Cursor == nullptr || !Cursor->IsVirtualCursorActiveForUser(UserIndex))
		{
			return false;
		}
		for (const FKey& Confirm : ConfirmKeys)
		{
			if (Confirm == InKey)
			{
				Cursor->SetConfirmPressedForUser(UserIndex, bInPressed);
				return true;
			}
		}
		return true;
	}

	void ReportDevice(UDreamUIInputUser* InUser, const FKey& InKey)
	{
		InUser->ReportInputDevice(UDreamEventSystem::GetInputDeviceForKey(InKey));
	}

	bool IsShiftHeld(const UDreamUIInputUser* InUser, const FModifierKeysState* InModifiers)
	{
		if (InModifiers != nullptr)
		{
			return InModifiers->IsShiftDown();
		}
		// This player's controller, not the first one: on a split screen the other player's shift is not ours.
		const APlayerController* Controller = InUser->GetPlayerController();
		const UPlayerInput* Input = Controller != nullptr ? Controller->PlayerInput.Get() : nullptr;
		return Input != nullptr && Input->IsShiftPressed();
	}
}

TConstArrayView<FKey> DreamUIKeyRouting::GetConfirmKeys() { return DreamUIKeyRoutingLocal::ConfirmKeys; }
TConstArrayView<FKey> DreamUIKeyRouting::GetBackKeys() { return DreamUIKeyRoutingLocal::BackKeys; }
TConstArrayView<TPair<FKey, EDreamUINavigationDirection>> DreamUIKeyRouting::GetDirectionKeys() { return DreamUIKeyRoutingLocal::DirectionKeys; }
TConstArrayView<TPair<FKey, float>> DreamUIKeyRouting::GetPageKeys() { return DreamUIKeyRoutingLocal::PageKeys; }
TConstArrayView<TPair<FKey, bool>> DreamUIKeyRouting::GetExtentKeys() { return DreamUIKeyRoutingLocal::ExtentKeys; }

EDreamUINavigationDirection DreamUIKeyRouting::GetDirectionForKey(const FKey& InKey, bool bInShiftDown)
{
	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : DreamUIKeyRoutingLocal::DirectionKeys)
	{
		if (Direction.Key == InKey)
		{
			// Tab is the one key in the table whose meaning depends on a modifier.
			return (Direction.Value == EDreamUINavigationDirection::Next && bInShiftDown) ? EDreamUINavigationDirection::Prev : Direction.Value;
		}
	}
	return EDreamUINavigationDirection::None;
}

bool DreamUIKeyRouting::IsNavigationKey(const FKey& InKey)
{
	using namespace DreamUIKeyRoutingLocal;
	for (const FKey& Confirm : ConfirmKeys)
	{
		if (Confirm == InKey)return true;
	}
	for (const TPair<FKey, float>& Page : PageKeys)
	{
		if (Page.Key == InKey)return true;
	}
	for (const TPair<FKey, bool>& Extent : ExtentKeys)
	{
		if (Extent.Key == InKey)return true;
	}
	return GetDirectionForKey(InKey, false) != EDreamUINavigationDirection::None;
}

bool DreamUIKeyRouting::ShouldReceiveInputWhilePaused()
{
	// A pointer read, every time: the setting can change while the game runs, and the next key should follow it.
	return !GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByGamePause;
}

bool DreamUIKeyRouting::IsInputSuspendedByGamePause(const UWorld* InWorld)
{
	// UWorld::IsPaused is the question the raycaster filter and the UI manager's tick both ask, and the one UWorld::Tick
	// asks before running a pause frame -- so all of them agree on what "paused" is.
	return InWorld != nullptr && InWorld->IsPaused() && !ShouldReceiveInputWhilePaused();
}

UDreamWidget* DreamUIKeyRouting::GetKeyTarget(const UDreamUIInputUser* InUser)
{
	if (InUser == nullptr)
	{
		return nullptr;
	}
	// The navigation highlight first, because that is what the player is looking at during gamepad input; the focus
	// is the answer after a click, when there is no highlight.
	if (const UDreamPointerEventData* Navigation = InUser->FindPointerEventData(DreamUIKeyRoutingLocal::NavigationPointerID))
	{
		if (UDreamWidget* Highlighted = Navigation->HighlightWidgetForNavigation.Get(); IsValid(Highlighted))
		{
			return Highlighted;
		}
	}
	return InUser->GetFocusedWidget();
}

bool DreamUIKeyRouting::RouteConfirmKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	if (bInPressed)
	{
		ReportDevice(InUser, InKey);
	}
	// An action explicitly bound to this key outranks its built-in meaning: Confirm is the likeliest thing a screen binds
	// to Enter, and it must not also press whatever navigation is sitting on. One keypress, one outcome.
	if (OfferToBindings(InUser, InKey, bInPressed, InModifiers))return true;
	if (OfferToVirtualCursor(InUser, InKey, bInPressed))return true;
	InUser->InputTriggerForNavigation(bInPressed, NavigationPointerID);
	// Taken only when there is something for it to press: with nothing focused or highlighted, the key is still the
	// game's -- the space bar in a level with no menu open is a jump.
	return IsValid(GetKeyTarget(InUser));
}

bool DreamUIKeyRouting::RouteDirectionKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	return RouteDirectionKeyAs(InUser, InKey, GetDirectionForKey(InKey, IsShiftHeld(InUser, InModifiers)), bInPressed, InModifiers);
}

bool DreamUIKeyRouting::RouteDirectionKeyAs(UDreamUIInputUser* InUser, const FKey& InKey, EDreamUINavigationDirection InDirection, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	// Reported BEFORE the cursor is consulted: a keyboard arrow reports the keyboard, which is what takes an auto-mode
	// virtual cursor down, so the same press then falls through to navigation instead of being eaten by a cursor on its
	// way out.
	if (bInPressed)
	{
		ReportDevice(InUser, InKey);
	}
	if (OfferToBindings(InUser, InKey, bInPressed, InModifiers))return true;
	if (OfferToVirtualCursor(InUser, InKey, bInPressed))return true;
	InUser->InputNavigation(InDirection, bInPressed, NavigationPointerID);
	// As a confirm: a step with nothing focused or highlighted still looks for somewhere to land, but the key is the
	// game's until the UI has something to move.
	return IsValid(GetKeyTarget(InUser));
}

bool DreamUIKeyRouting::RouteScrollKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	ReportDevice(InUser, InKey);
	// A screen that binds End to "jump to newest" wins over the built-in meaning.
	if (OfferToBindings(InUser, InKey, true, InModifiers))return true;
	UDreamWidget* Target = GetKeyTarget(InUser);
	if (!IsValid(Target))
	{
		return false;//nothing has focus, so there is no list to page
	}
	for (const TPair<FKey, float>& Page : PageKeys)
	{
		if (Page.Key == InKey)
		{
			return FDreamUINavigationScroll::ScrollByPages(Target, Page.Value);
		}
	}
	for (const TPair<FKey, bool>& Extent : ExtentKeys)
	{
		if (Extent.Key == InKey)
		{
			return FDreamUINavigationScroll::ScrollToExtent(Target, Extent.Value);
		}
	}
	return false;
}

bool DreamUIKeyRouting::RouteOtherKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	if (bInPressed)
	{
		ReportDevice(InUser, InKey);
	}
	// A modifier is not a key anything is bound to; it is what qualifies the key that follows, and the router reads its
	// state when that key comes through.
	if (InKey.IsModifierKey())
	{
		return false;
	}
	if (OfferToBindings(InUser, InKey, bInPressed, InModifiers))
	{
		return true;
	}
	if (!bInPressed)
	{
		return false;
	}
	// Only once nothing has claimed the key: a project that binds its own Back action defines what Back does, and the
	// built-in behaviour is the fallback for one that has not.
	for (const FKey& BackKey : BackKeys)
	{
		if (BackKey != InKey)
		{
			continue;
		}
		// A drag in flight outranks Back: Escape is the universal "put it back" while something is held. This player's
		// drags only -- any player's Escape used to cancel player 0's.
		UWorld* World = WorldOf(InUser);
		if (UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(World);
			DragDrop != nullptr && DragDrop->CancelActiveDragForUser(InUser->GetUserIndex()))
		{
			return true;
		}
		UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(World);
		return Stack != nullptr && Stack->HandleBack(InUser->GetUserIndex());
	}
	return false;
}

bool DreamUIKeyRouting::RouteTextKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers)
{
	// The field the player is typing into takes the keys it types with before anything else sees them -- press and
	// release alike, so no half of a keystroke reaches a binding. Escape is not among them: Back reaches an edit
	// through the navigation stack, which ends it before anything else.
	IDreamUITextInputTarget* Target = InUser != nullptr ? Cast<IDreamUITextInputTarget>(InUser->GetTextTarget()) : nullptr;
	if (Target == nullptr || !Target->IsTextInputActive())
	{
		return false;
	}
	TArray<FKey> TextKeys;
	Target->GetTextInputKeys(TextKeys);
	if (!TextKeys.Contains(InKey))
	{
		return false;
	}
	if (bInPressed)
	{
		Target->HandleTextInputKeyWithModifiers(InKey, InModifiers);
	}
	return true;
}

bool DreamUIKeyRouting::RouteKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers, bool& bOutTyped)
{
	using namespace DreamUIKeyRoutingLocal;
	bOutTyped = false;
	if (InUser == nullptr)
	{
		return false;
	}
	if (RouteTextKey(InUser, InKey, bInPressed, InModifiers))
	{
		bOutTyped = true;
		return true;
	}
	for (const FKey& Confirm : ConfirmKeys)
	{
		if (Confirm == InKey)
		{
			return RouteConfirmKey(InUser, InKey, bInPressed, &InModifiers);
		}
	}
	if (GetDirectionForKey(InKey, false) != EDreamUINavigationDirection::None)
	{
		return RouteDirectionKey(InUser, InKey, bInPressed, &InModifiers);
	}
	for (const TPair<FKey, float>& Page : PageKeys)
	{
		if (Page.Key == InKey)
		{
			return bInPressed && RouteScrollKey(InUser, InKey, &InModifiers);
		}
	}
	for (const TPair<FKey, bool>& Extent : ExtentKeys)
	{
		if (Extent.Key == InKey)
		{
			return bInPressed && RouteScrollKey(InUser, InKey, &InModifiers);
		}
	}
	return RouteOtherKey(InUser, InKey, bInPressed, &InModifiers);
}
