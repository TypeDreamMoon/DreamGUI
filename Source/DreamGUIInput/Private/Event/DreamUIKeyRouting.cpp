// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIKeyRouting.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUISettings.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputUser.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/PlatformInput.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITabOrder.h"
#include "Interaction/DreamUITabSwitchTarget.h"
#include "Interaction/DreamUITextInputTarget.h"
#include "Interaction/DreamUIVirtualCursor.h"

namespace DreamUIKeyRoutingLocal
{
	/** Navigation is single-pointer: the navigation cursor lives on the mouse's pointer. */
	static constexpr int32 NavigationPointerID = 0;

	bool IsSequential(EDreamUINavigationDirection InDirection)
	{
		return InDirection == EDreamUINavigationDirection::Next || InDirection == EDreamUINavigationDirection::Prev;
	}

	/**
	 * The key tables the routing reads: UDreamGUISettings's, with the platform's own accept and back put in when the
	 * settings say so, in the shapes DreamUIKeyRouting's Get*Keys answer with. Rebuilt whenever what they are made from
	 * has changed -- compared at every read, because a remapping screen or a test edits the settings object without
	 * telling anyone, and a remap takes effect at the next key. Game thread, as every key is.
	 */
	struct FKeyTables
	{
		bool bBuilt = false;

		// What the tables were made from.
		TArray<FKey> SourceConfirmKeys;
		TArray<FKey> SourceBackKeys;
		TArray<FDreamUIDirectionKey> SourceDirectionKeys;
		TArray<FDreamUIPageKey> SourcePageKeys;
		TArray<FDreamUIExtentKey> SourceExtentKeys;
		TArray<FKey> SourcePreviousTabKeys;
		TArray<FKey> SourceNextTabKeys;
		bool bSourceUsePlatformAcceptBack = false;
		bool bSourceTabNavigation = true;

		// The tables.
		TArray<FKey> ConfirmKeys;
		TArray<FKey> BackKeys;
		TArray<TPair<FKey, EDreamUINavigationDirection>> DirectionKeys;
		TArray<TPair<FKey, float>> PageKeys;
		TArray<TPair<FKey, bool>> ExtentKeys;
		TArray<FKey> PreviousTabKeys;
		TArray<FKey> NextTabKeys;

		bool IsBuiltFrom(const UDreamGUISettings& InSettings) const
		{
			if (!bBuilt || bSourceUsePlatformAcceptBack != InSettings.bUsePlatformAcceptBack || bSourceTabNavigation != InSettings.bTabNavigation
				|| SourceConfirmKeys != InSettings.ConfirmKeys || SourceBackKeys != InSettings.BackKeys
				|| SourcePreviousTabKeys != InSettings.PreviousTabKeys || SourceNextTabKeys != InSettings.NextTabKeys
				|| SourceDirectionKeys.Num() != InSettings.DirectionKeys.Num() || SourcePageKeys.Num() != InSettings.PageKeys.Num()
				|| SourceExtentKeys.Num() != InSettings.ExtentKeys.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < SourceDirectionKeys.Num(); ++Index)
			{
				if (SourceDirectionKeys[Index].Key != InSettings.DirectionKeys[Index].Key || SourceDirectionKeys[Index].Direction != InSettings.DirectionKeys[Index].Direction)
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < SourcePageKeys.Num(); ++Index)
			{
				if (SourcePageKeys[Index].Key != InSettings.PageKeys[Index].Key || SourcePageKeys[Index].Pages != InSettings.PageKeys[Index].Pages)
				{
					return false;
				}
			}
			for (int32 Index = 0; Index < SourceExtentKeys.Num(); ++Index)
			{
				if (SourceExtentKeys[Index].Key != InSettings.ExtentKeys[Index].Key || SourceExtentKeys[Index].bToStart != InSettings.ExtentKeys[Index].bToStart)
				{
					return false;
				}
			}
			return true;
		}

		void Build(const UDreamGUISettings& InSettings)
		{
			bBuilt = true;
			SourceConfirmKeys = InSettings.ConfirmKeys;
			SourceBackKeys = InSettings.BackKeys;
			SourceDirectionKeys = InSettings.DirectionKeys;
			SourcePageKeys = InSettings.PageKeys;
			SourceExtentKeys = InSettings.ExtentKeys;
			SourcePreviousTabKeys = InSettings.PreviousTabKeys;
			SourceNextTabKeys = InSettings.NextTabKeys;
			bSourceUsePlatformAcceptBack = InSettings.bUsePlatformAcceptBack;
			bSourceTabNavigation = InSettings.bTabNavigation;

			const auto CopyValidKeys = [](const TArray<FKey>& InFrom, TArray<FKey>& OutTo)
			{
				OutTo.Reset();
				for (const FKey& Candidate : InFrom)
				{
					if (Candidate.IsValid())
					{
						OutTo.AddUnique(Candidate);
					}
				}
			};
			CopyValidKeys(InSettings.ConfirmKeys, ConfirmKeys);
			CopyValidKeys(InSettings.BackKeys, BackKeys);
			CopyValidKeys(InSettings.PreviousTabKeys, PreviousTabKeys);
			CopyValidKeys(InSettings.NextTabKeys, NextTabKeys);
			if (InSettings.bUsePlatformAcceptBack)
			{
				// Read when the tables are built, not when the settings were: a static table can be built before EKeys has
				// registered its keys, and an unregistered key resolves to nothing. A face button the platform uses the
				// other way leaves the other table -- on a Switch, where accept is the right face button, the bottom one
				// confirms no more and the right one is no longer Back.
				const FKey PlatformAccept = FPlatformInput::GetGamepadAcceptKey();
				const FKey PlatformBack = FPlatformInput::GetGamepadBackKey();
				if (PlatformAccept != PlatformBack)
				{
					ConfirmKeys.Remove(PlatformBack);
					BackKeys.Remove(PlatformAccept);
				}
				if (PlatformAccept.IsValid())
				{
					ConfirmKeys.AddUnique(PlatformAccept);
				}
				if (PlatformBack.IsValid())
				{
					BackKeys.AddUnique(PlatformBack);
				}
			}

			DirectionKeys.Reset();
			for (const FDreamUIDirectionKey& Row : InSettings.DirectionKeys)
			{
				// With Tab navigation off, Tab is a key like any other: the game's.
				if (!Row.Key.IsValid() || Row.Direction == EDreamUINavigationDirection::None
					|| (IsSequential(Row.Direction) && !InSettings.bTabNavigation))
				{
					continue;
				}
				DirectionKeys.Emplace(Row.Key, Row.Direction);
			}
			PageKeys.Reset();
			for (const FDreamUIPageKey& Row : InSettings.PageKeys)
			{
				if (Row.Key.IsValid())
				{
					PageKeys.Emplace(Row.Key, Row.Pages);
				}
			}
			ExtentKeys.Reset();
			for (const FDreamUIExtentKey& Row : InSettings.ExtentKeys)
			{
				if (Row.Key.IsValid())
				{
					ExtentKeys.Emplace(Row.Key, Row.bToStart);
				}
			}
		}
	};

	const FKeyTables& GetTables()
	{
		static FKeyTables Tables;
		if (const UDreamGUISettings* Settings = UDreamGUISettings::Get(); Settings != nullptr && !Tables.IsBuiltFrom(*Settings))
		{
			Tables.Build(*Settings);
		}
		return Tables;
	}

	/** How many screenfuls InKey pages, when it is a page key. Looked up into a value: what the routing does next can rebuild the tables. */
	bool FindPages(const FKey& InKey, float& OutPages)
	{
		for (const TPair<FKey, float>& Page : GetTables().PageKeys)
		{
			if (Page.Key == InKey)
			{
				OutPages = Page.Value;
				return true;
			}
		}
		return false;
	}

	/** Which extent InKey scrolls to, when it is an extent key. */
	bool FindExtent(const FKey& InKey, bool& bOutToStart)
	{
		for (const TPair<FKey, bool>& Extent : GetTables().ExtentKeys)
		{
			if (Extent.Key == InKey)
			{
				bOutToStart = Extent.Value;
				return true;
			}
		}
		return false;
	}

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
		if (DreamUIKeyRouting::IsConfirmKey(InKey))
		{
			Cursor->SetConfirmPressedForUser(UserIndex, bInPressed);
		}
		return true;
	}

	void ReportDevice(UDreamUIInputUser* InUser, const FKey& InKey)
	{
		InUser->ReportInputKey(InKey);
	}

	/** The chord a key arrived with: stated by the source, or read from this player's controller. */
	void ReadChord(const UDreamUIInputUser* InUser, const FModifierKeysState* InModifiers, bool& bOutShift, bool& bOutCtrlAltOrCmd)
	{
		if (InModifiers != nullptr)
		{
			bOutShift = InModifiers->IsShiftDown();
			bOutCtrlAltOrCmd = InModifiers->IsControlDown() || InModifiers->IsAltDown() || InModifiers->IsCommandDown();
			return;
		}
		// This player's controller, not the first one: on a split screen the other player's shift is not ours.
		const APlayerController* Controller = InUser->GetPlayerController();
		const UPlayerInput* Input = Controller != nullptr ? Controller->PlayerInput.Get() : nullptr;
		bOutShift = Input != nullptr && Input->IsShiftPressed();
		bOutCtrlAltOrCmd = Input != nullptr && (Input->IsCtrlPressed() || Input->IsAltPressed() || Input->IsCmdPressed());
	}

	void NotePress(UDreamUIInputUser* InUser, const FKey& InKey, EDreamUIKeyPressTaker InTaker,
		EDreamUINavigationDirection InDirection = EDreamUINavigationDirection::None)
	{
		FDreamUIKeyPress Press;
		Press.Taker = InTaker;
		Press.Direction = InDirection;
		InUser->NoteKeyPress(InKey, Press);
	}

	/**
	 * A press of a key whose last press never had its release -- lost to the application losing the focus, say -- lets
	 * go of that one first, so whatever took it is not left holding it.
	 */
	void LetGoOfUnreleasedPress(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers)
	{
		if (InUser->FindKeyPress(InKey) != nullptr)
		{
			DreamUIKeyRouting::RouteKeyRelease(InUser, InKey, InModifiers);
		}
	}

	/**
	 * Whether a navigation press in InDirection is the UI's rather than the game's. A Tab is while the player has a Tab
	 * stop to go to -- a Tab that moves DreamGUI's focus is kept from the game where the policy keeps what the UI takes --
	 * and a direction while the player has something focused to move: a step with nothing focused still looks for
	 * somewhere to land, but the key is the game's until the UI has something to move.
	 */
	bool IsNavigationTaken(const UDreamUIInputUser* InUser, EDreamUINavigationDirection InDirection)
	{
		if (IsSequential(InDirection) && UDreamGUISettings::Get()->TabOrder == EDreamUITabOrder::Hierarchy)
		{
			return DreamUIKeyRouting::HasTabStops(InUser);
		}
		return IsValid(DreamUIKeyRouting::GetKeyTarget(InUser));
	}

	bool CanSwitchTabsOn(UDreamWidget* InWidget, int32 InUserIndex)
	{
		const IDreamUITabSwitchTarget* Target = Cast<IDreamUITabSwitchTarget>(InWidget);
		return Target != nullptr && Target->CanSwitchTab(InUserIndex);
	}

	/** The first widget under InRoot, depth first and InRoot included, that switches tabs for player InUserIndex now. */
	UDreamWidget* FindTabSwitchTargetUnder(UDreamWidget* InRoot, int32 InUserIndex)
	{
		TArray<UDreamWidget*, TInlineAllocator<64>> Pending;
		if (IsValid(InRoot))
		{
			Pending.Add(InRoot);
		}
		while (Pending.Num() > 0)
		{
			UDreamWidget* Candidate = Pending.Pop(EAllowShrinking::No);
			if (!IsValid(Candidate) || !Candidate->GetWidgetActiveInHierarchy())
			{
				continue;//nothing asleep switches tabs, and nothing under it does either
			}
			if (CanSwitchTabsOn(Candidate, InUserIndex))
			{
				return Candidate;
			}
			// Pushed last child first, so the first child is the next one looked at: depth first, in hierarchy order.
			const TArray<UDreamWidget*>& Children = Candidate->GetChildren();
			for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
			{
				Pending.Add(Children[Index]);
			}
		}
		return nullptr;
	}
}

TConstArrayView<FKey> DreamUIKeyRouting::GetConfirmKeys() { return DreamUIKeyRoutingLocal::GetTables().ConfirmKeys; }
TConstArrayView<FKey> DreamUIKeyRouting::GetBackKeys() { return DreamUIKeyRoutingLocal::GetTables().BackKeys; }
TConstArrayView<TPair<FKey, EDreamUINavigationDirection>> DreamUIKeyRouting::GetDirectionKeys() { return DreamUIKeyRoutingLocal::GetTables().DirectionKeys; }
TConstArrayView<TPair<FKey, float>> DreamUIKeyRouting::GetPageKeys() { return DreamUIKeyRoutingLocal::GetTables().PageKeys; }
TConstArrayView<TPair<FKey, bool>> DreamUIKeyRouting::GetExtentKeys() { return DreamUIKeyRoutingLocal::GetTables().ExtentKeys; }

EDreamUINavigationDirection DreamUIKeyRouting::GetDirectionForKey(const FKey& InKey, bool bInShiftDown)
{
	using namespace DreamUIKeyRoutingLocal;
	for (const TPair<FKey, EDreamUINavigationDirection>& Direction : GetTables().DirectionKeys)
	{
		if (Direction.Key != InKey)
		{
			continue;
		}
		// Tab is the one key in the table whose meaning depends on a modifier: shift walks the sequence the other way.
		if (bInShiftDown && IsSequential(Direction.Value))
		{
			return Direction.Value == EDreamUINavigationDirection::Next ? EDreamUINavigationDirection::Prev : EDreamUINavigationDirection::Next;
		}
		return Direction.Value;
	}
	return EDreamUINavigationDirection::None;
}

EDreamUINavigationDirection DreamUIKeyRouting::GetDirectionForChord(const FKey& InKey, bool bInShiftDown, bool bInCtrlAltOrCmdDown)
{
	const EDreamUINavigationDirection Direction = GetDirectionForKey(InKey, bInShiftDown);
	return bInCtrlAltOrCmdDown && DreamUIKeyRoutingLocal::IsSequential(Direction) ? EDreamUINavigationDirection::None : Direction;
}

bool DreamUIKeyRouting::IsNavigationKey(const FKey& InKey)
{
	using namespace DreamUIKeyRoutingLocal;
	float Pages = 0.0f;
	bool bToStart = false;
	return IsConfirmKey(InKey) || GetDirectionForKey(InKey, false) != EDreamUINavigationDirection::None
		|| FindPages(InKey, Pages) || FindExtent(InKey, bToStart) || GetTabSwitchDelta(InKey) != 0;
}

FKey DreamUIKeyRouting::GetGamepadAcceptKey()
{
	return UDreamGUISettings::Get()->bUsePlatformAcceptBack ? FPlatformInput::GetGamepadAcceptKey() : EKeys::Gamepad_FaceButton_Bottom;
}

FKey DreamUIKeyRouting::GetGamepadBackKey()
{
	return UDreamGUISettings::Get()->bUsePlatformAcceptBack ? FPlatformInput::GetGamepadBackKey() : EKeys::Gamepad_FaceButton_Right;
}

bool DreamUIKeyRouting::IsConfirmKey(const FKey& InKey)
{
	return DreamUIKeyRoutingLocal::GetTables().ConfirmKeys.Contains(InKey);
}

bool DreamUIKeyRouting::IsBackKey(const FKey& InKey)
{
	return DreamUIKeyRoutingLocal::GetTables().BackKeys.Contains(InKey);
}

int32 DreamUIKeyRouting::GetTabSwitchDelta(const FKey& InKey)
{
	const DreamUIKeyRoutingLocal::FKeyTables& Tables = DreamUIKeyRoutingLocal::GetTables();
	if (Tables.PreviousTabKeys.Contains(InKey))
	{
		return -1;
	}
	return Tables.NextTabKeys.Contains(InKey) ? 1 : 0;
}

UDreamWidget* DreamUIKeyRouting::FindTabSwitchTarget(const UDreamUIInputUser* InUser)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return nullptr;
	}
	const int32 UserIndex = InUser->GetUserIndex();
	// The nearest one around the focus: a tab view inside another's page switches its own tabs, not the outer one's.
	for (UDreamWidget* Walker = InUser->GetFocusedWidget(); IsValid(Walker); Walker = Walker->GetParent())
	{
		if (CanSwitchTabsOn(Walker, UserIndex))
		{
			return Walker;
		}
	}
	// With the focus in none: the first one on the screen in front -- the active scope's, else the player's screen.
	UWorld* World = WorldOf(InUser);
	const UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(World);
	const UDreamUINavigationScope* Scope = Stack != nullptr ? Stack->GetActiveScope(UserIndex) : nullptr;
	if (UDreamWidget* ScopeWidget = Scope != nullptr ? Scope->GetWidget() : nullptr; IsValid(ScopeWidget))
	{
		return FindTabSwitchTargetUnder(ScopeWidget, UserIndex);
	}
	const UDreamScreenUISubsystem* ScreenUI = World != nullptr ? UDreamScreenUISubsystem::Get(World) : nullptr;
	return ScreenUI != nullptr ? FindTabSwitchTargetUnder(ScreenUI->GetScreenRoot(InUser->GetPlayerController()), UserIndex) : nullptr;
}

bool DreamUIKeyRouting::HasBuiltInMeanings(const UDreamUIInputUser* InUser)
{
	if (InUser == nullptr)
	{
		return false;
	}
	const UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(InUser);
	const EDreamUIScopeInputMode Mode = Stack != nullptr
		? Stack->GetEffectiveInputMode(InUser->GetUserIndex())
		: UDreamGUISettings::Get()->InputModeWithoutScope;
	return Mode != EDreamUIScopeInputMode::Game;
}

bool DreamUIKeyRouting::HasTabStops(const UDreamUIInputUser* InUser)
{
	if (InUser == nullptr)
	{
		return false;
	}
	const FDreamUITabDomain Domain = FDreamUITabOrder::FindDomain(InUser, InUser->GetUserIndex(), InUser->GetFocusedWidget());
	return Domain.IsValid() && FDreamUITabOrder::FindFirstStop(Domain) != nullptr;
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
	// The focus, and never the hover highlight. The highlight was asked first, as what a player looks at during pad input,
	// but every hover rewrote it: Enter pressed the button under a resting mouse rather than the one Tab had reached, and a
	// screen opening under an idle cursor made the button there what the next confirm pressed. A widget hidden or disabled
	// while it holds the focus is nothing a key can act on, as the router finds too (UDreamUIActionRouter::GetFocusedWidget).
	UDreamWidget* Focused = InUser->GetFocusedWidget();
	return IsValid(Focused) && Focused->GetRenderVisibleInHierarchy() && Focused->GetInteractableInHierarchy() ? Focused : nullptr;
}

bool DreamUIKeyRouting::RouteConfirmKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	if (!bInPressed)
	{
		return RouteKeyRelease(InUser, InKey, InModifiers);
	}
	ReportDevice(InUser, InKey);
	LetGoOfUnreleasedPress(InUser, InKey, InModifiers);
	// An action explicitly bound to this key outranks its built-in meaning: Confirm is the likeliest thing a screen binds
	// to Enter, and it must not also press whatever navigation is sitting on. One keypress, one outcome.
	if (OfferToBindings(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Bindings);
		return true;
	}
	if (OfferToVirtualCursor(InUser, InKey, true))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::VirtualCursor);
		return true;
	}
	// In gameplay (input mode Game) a confirm is the game's: the jump button does not press the HUD's buttons. And so it is
	// with nothing focused, which leaves the confirm nothing to press: the space bar in a level with no menu open is a jump,
	// and the mouse's pointer -- which a confirm turns into the navigation cursor -- keeps its hover.
	if (!HasBuiltInMeanings(InUser) || !IsValid(GetKeyTarget(InUser)))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
		return false;
	}
	InUser->InputTriggerForNavigation(true, NavigationPointerID);
	NotePress(InUser, InKey, EDreamUIKeyPressTaker::NavigationConfirm);
	return true;
}

bool DreamUIKeyRouting::RouteDirectionKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	if (!bInPressed)
	{
		return RouteKeyRelease(InUser, InKey, InModifiers);
	}
	bool bShiftDown = false;
	bool bCtrlAltOrCmdDown = false;
	ReadChord(InUser, InModifiers, bShiftDown, bCtrlAltOrCmdDown);
	return RouteDirectionKeyAs(InUser, InKey, GetDirectionForChord(InKey, bShiftDown, bCtrlAltOrCmdDown), true, InModifiers);
}

bool DreamUIKeyRouting::RouteDirectionKeyAs(UDreamUIInputUser* InUser, const FKey& InKey, EDreamUINavigationDirection InDirection, bool bInPressed, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	if (!bInPressed)
	{
		// The direction its press stepped in, whatever the key would mean now: Tab let go of with shift since pressed is
		// still the Next it pressed.
		return RouteKeyRelease(InUser, InKey, InModifiers);
	}
	if (InDirection == EDreamUINavigationDirection::None)
	{
		// A direction key that is no direction for this press -- Tab with Ctrl, Alt or Cmd held -- is an ordinary key: the
		// bindings, and nothing after them. Ctrl+Tab used to navigate as Tab does.
		return RouteOtherKey(InUser, InKey, true, InModifiers);
	}
	// Reported BEFORE the cursor is consulted: a keyboard arrow reports the keyboard, which is what takes an auto-mode
	// virtual cursor down, so the same press then falls through to navigation instead of being eaten by a cursor on its
	// way out.
	ReportDevice(InUser, InKey);
	LetGoOfUnreleasedPress(InUser, InKey, InModifiers);
	if (OfferToBindings(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Bindings);
		return true;
	}
	if (OfferToVirtualCursor(InUser, InKey, true))
	{
		// Dropped: a direction is not the cursor's business, and its release has nothing to tell it either.
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
		return true;
	}
	// In gameplay the D-pad and the stick are the game's: the HUD's buttons are not walked onto.
	if (!HasBuiltInMeanings(InUser))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
		return false;
	}
	InUser->InputNavigation(InDirection, true, NavigationPointerID);
	NotePress(InUser, InKey, EDreamUIKeyPressTaker::NavigationDirection, InDirection);
	return IsNavigationTaken(InUser, InDirection);
}

bool DreamUIKeyRouting::RouteScrollKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	ReportDevice(InUser, InKey);
	LetGoOfUnreleasedPress(InUser, InKey, InModifiers);
	// A screen that binds End to "jump to newest" wins over the built-in meaning.
	if (OfferToBindings(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Bindings);
		return true;
	}
	NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
	if (!HasBuiltInMeanings(InUser))
	{
		return false;//gameplay: the triggers are the game's
	}
	UDreamWidget* Target = GetKeyTarget(InUser);
	if (!IsValid(Target))
	{
		return false;//nothing has focus, so there is no list to page
	}
	float Pages = 0.0f;
	if (FindPages(InKey, Pages))
	{
		return FDreamUINavigationScroll::ScrollByPages(Target, Pages);
	}
	bool bToStart = false;
	if (FindExtent(InKey, bToStart))
	{
		return FDreamUINavigationScroll::ScrollToExtent(Target, bToStart);
	}
	return false;
}

bool DreamUIKeyRouting::RouteTabSwitchKey(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	ReportDevice(InUser, InKey);
	LetGoOfUnreleasedPress(InUser, InKey, InModifiers);
	// A screen that binds the shoulder buttons -- a character sheet's previous and next hero -- wins over the tab view.
	if (OfferToBindings(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Bindings);
		return true;
	}
	NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
	const int32 Delta = GetTabSwitchDelta(InKey);
	if (Delta == 0 || !HasBuiltInMeanings(InUser))
	{
		return false;
	}
	IDreamUITabSwitchTarget* Target = Cast<IDreamUITabSwitchTarget>(FindTabSwitchTarget(InUser));
	return Target != nullptr && Target->SwitchTab(InUser->GetUserIndex(), Delta);
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
	if (!bInPressed)
	{
		return RouteKeyRelease(InUser, InKey, InModifiers);
	}
	LetGoOfUnreleasedPress(InUser, InKey, InModifiers);
	if (OfferToBindings(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Bindings);
		return true;
	}
	NotePress(InUser, InKey, EDreamUIKeyPressTaker::PressOnly);
	// Only once nothing has claimed the key: a project that binds its own Back action defines what Back does, and the
	// built-in behaviour is the fallback for one that has not.
	if (!IsBackKey(InKey))
	{
		return false;
	}
	// A drag in flight outranks Back: Escape is the universal "put it back" while something is held. This player's drags
	// only -- any player's Escape used to cancel player 0's. A drag is a pointer's, and pointers work in gameplay too.
	UWorld* World = WorldOf(InUser);
	if (UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(World);
		DragDrop != nullptr && DragDrop->CancelActiveDragForUser(InUser->GetUserIndex()))
	{
		return true;
	}
	// In gameplay Back is the game's: the pause menu it opens is the game's to open.
	if (!HasBuiltInMeanings(InUser))
	{
		return false;
	}
	UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(World);
	return Stack != nullptr && Stack->HandleBack(InUser->GetUserIndex());
}

bool DreamUIKeyRouting::RouteTextKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers)
{
	// The field the player is typing into takes the keys it types with before anything else sees them, so no
	// keystroke it types also reaches a binding. Escape is not among them: Back reaches an edit through the
	// navigation stack, which ends it before anything else. A release is not asked here: it goes to whatever took its
	// press (RouteKeyRelease), and the field takes only those it typed.
	if (!bInPressed)
	{
		return false;
	}
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
	// Typing is keys: what code moves the focus to next is drawn, as it is after a step (IsFocusVisible).
	InUser->ReportInputKey(InKey);
	// The field's own answer: a chord it does not take -- Ctrl, Alt or Cmd with Tab, in a field that leaves Tab to the
	// bindings -- goes on to them, as an ordinary key, rather than being swallowed as typing.
	return Target->HandleTextInputKeyWithModifiers(InKey, InModifiers);
}

bool DreamUIKeyRouting::RouteKeyRelease(UDreamUIInputUser* InUser, const FKey& InKey, const FModifierKeysState* InModifiers)
{
	using namespace DreamUIKeyRoutingLocal;
	if (InUser == nullptr)
	{
		return false;
	}
	FDreamUIKeyPress Press;
	const bool bRouted = InUser->TakeKeyPress(InKey, Press);
	if (bRouted && Press.Taker == EDreamUIKeyPressTaker::Text)
	{
		return true;//typed on the press, which nothing else heard
	}
	// The bindings hear every other release: the focused widget's key-up, as UMG delivers it to the focus whoever took
	// the down, and the binding that took the press when one did -- which the router remembers and answers for. A
	// binding that did not see the press takes nothing.
	if (OfferToBindings(InUser, InKey, false, InModifiers))
	{
		return true;
	}
	if (!bRouted)
	{
		return false;
	}
	switch (Press.Taker)
	{
	case EDreamUIKeyPressTaker::Bindings:
		return true;//the press was kept from everything else, and so is its release
	case EDreamUIKeyPressTaker::VirtualCursor:
		if (UDreamUIVirtualCursorSubsystem* Cursor = UDreamUIVirtualCursorSubsystem::Get(WorldOf(InUser)))
		{
			Cursor->SetConfirmPressedForUser(InUser->GetUserIndex(), false);
		}
		return true;
	case EDreamUIKeyPressTaker::NavigationConfirm:
		InUser->InputTriggerForNavigation(false, NavigationPointerID);
		return IsValid(GetKeyTarget(InUser));
	case EDreamUIKeyPressTaker::NavigationDirection:
		InUser->InputNavigation(Press.Direction, false, NavigationPointerID);
		return IsNavigationTaken(InUser, Press.Direction);
	default:
		return false;
	}
}

void DreamUIKeyRouting::AbandonKeyPress(UDreamUIInputUser* InUser, const FKey& InKey)
{
	using namespace DreamUIKeyRoutingLocal;
	const FDreamUIKeyPress* Press = InUser != nullptr ? InUser->FindKeyPress(InKey) : nullptr;
	if (Press == nullptr)
	{
		return;
	}
	// The confirm's press is a pointer press -- the navigation pointer's, or the virtual cursor's on the same pointer --
	// and ends first, so the release that follows lands on a pointer no longer pressed and clicks nothing.
	if (Press->Taker == EDreamUIKeyPressTaker::NavigationConfirm || Press->Taker == EDreamUIKeyPressTaker::VirtualCursor)
	{
		InUser->CancelPointerPress(NavigationPointerID);
	}
	RouteKeyRelease(InUser, InKey);
}

bool DreamUIKeyRouting::RouteKey(UDreamUIInputUser* InUser, const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifiers, bool& bOutTyped)
{
	using namespace DreamUIKeyRoutingLocal;
	bOutTyped = false;
	if (InUser == nullptr)
	{
		return false;
	}
	if (!bInPressed)
	{
		// Where its press went, not what the key would mean now: a field that began its edit while the key was held --
		// the arrow that navigated into it -- does not take the release of a press it never saw.
		const FDreamUIKeyPress* Press = InUser->FindKeyPress(InKey);
		bOutTyped = Press != nullptr && Press->Taker == EDreamUIKeyPressTaker::Text;
		return RouteKeyRelease(InUser, InKey, &InModifiers);
	}
	LetGoOfUnreleasedPress(InUser, InKey, &InModifiers);
	if (RouteTextKey(InUser, InKey, true, InModifiers))
	{
		NotePress(InUser, InKey, EDreamUIKeyPressTaker::Text);
		bOutTyped = true;
		return true;
	}
	if (IsConfirmKey(InKey))
	{
		return RouteConfirmKey(InUser, InKey, true, &InModifiers);
	}
	// A direction key whatever its chord makes of it: Ctrl+Tab goes on from there as an ordinary key.
	if (GetDirectionForKey(InKey, false) != EDreamUINavigationDirection::None)
	{
		return RouteDirectionKey(InUser, InKey, true, &InModifiers);
	}
	float Pages = 0.0f;
	bool bToStart = false;
	if (FindPages(InKey, Pages) || FindExtent(InKey, bToStart))
	{
		return RouteScrollKey(InUser, InKey, &InModifiers);
	}
	// After the field had its turn: a field being edited may take a shoulder button for its own, and then it is typing.
	if (GetTabSwitchDelta(InKey) != 0)
	{
		return RouteTabSwitchKey(InUser, InKey, &InModifiers);
	}
	return RouteOtherKey(InUser, InKey, true, &InModifiers);
}
