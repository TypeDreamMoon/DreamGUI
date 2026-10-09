// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIFocusReturn.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/World.h"

namespace DreamUIFocusReturnLocal
{
	/** InWidget is InRoot or under it. Nothing is inside a root that is gone. */
	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot)
	{
		return IsValid(InWidget) && IsValid(InRoot) && (InWidget == InRoot || InWidget->IsChildOf(InRoot));
	}

	/**
	 * InWidget and every widget above it in play -- registered, in a world that never began play (the designer, a bare
	 * test world). A widget being torn down ends play parents first, so one coming down shows it above itself first.
	 * FocusForNavigation takes a widget that is only registered, for a screen still beginning play under a scope that
	 * focuses from its own; what focus comes back to was in play before, and one no longer in play is on its way out.
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

	/**
	 * Whether focus may be put back on InCandidate: outside the root being closed, in play -- an opener coming down with
	 * its screen is passed over rather than handed a focus that goes with it -- and a thing focus belongs on: a widget
	 * that takes focus, or one navigation can land on now. An opener that is a plain container (a menu anchor, whose
	 * user-widget bridge declines navigation) is passed over for the next candidate rather than handed a focus nobody
	 * can see. The rest of "usable" -- active, drawn, interactable -- is FocusForNavigation's to refuse.
	 */
	bool IsCandidate(UDreamWidget* InCandidate, const UDreamWidget* InRoot)
	{
		if (!IsValid(InCandidate) || IsInside(InCandidate, InRoot) || !IsInPlay(InCandidate))
		{
			return false;
		}
		return InCandidate->GetIsFocusable()
			|| DreamUINavigationScan::CanNavigateTo(DreamUINavigationScan::FindNavigationBehaviour(InCandidate));
	}

	/** Stop after acceptance or a callback's newer choice; only an unchanged refusal may try another candidate. */
	bool TryFocus(UDreamUIInputServices& InServices, int32 InUserIndex, UDreamWidget* InCandidate, const UDreamWidget* InRoot)
	{
		if (!IsCandidate(InCandidate, InRoot))return false;
		const TWeakObjectPtr<UDreamUIInputServices> WeakServices(&InServices);
		const FDreamUIFocusRevision Before = InServices.GetFocusRevision(InUserIndex);
		const bool bFocused = InServices.FocusForNavigation(InCandidate, InUserIndex);
		return bFocused || !WeakServices.IsValid() || WeakServices->GetFocusRevision(InUserIndex) != Before;
	}
}

void FDreamFocusReturn::Capture(UDreamWidget* InOpener)
{
	UWorld* OpenerWorld = IsValid(InOpener) ? InOpener->GetWorld() : nullptr;
	UDreamUIInputServices* Services = OpenerWorld != nullptr ? UDreamUIInputServices::Get(OpenerWorld) : nullptr;
	if (Services == nullptr)
	{
		return;
	}
	Reset();
	Opener = InOpener;
	World = OpenerWorld;
	TArray<int32> UserIndices;
	Services->GetUserIndices(UserIndices);
	Users.Reserve(UserIndices.Num());
	for (const int32 UserIndex : UserIndices)
	{
		UDreamWidget* Focused = Services->GetFocusedWidget(UserIndex);
		FUserFocus& Entry = Users.AddDefaulted_GetRef();
		Entry.UserIndex = UserIndex;
		Entry.Focused = Focused;
		Entry.bHadFocus = IsValid(Focused);
	}
	bCaptured = true;
}

int32 FDreamFocusReturn::Return(const UDreamWidget* InPopupRoot)
{
	using namespace DreamUIFocusReturnLocal;
	if (!bCaptured)
	{
		return 0;
	}
	// Taken out and forgotten before anything moves: moving focus runs the deselect and select handlers, and one of
	// them may open the popup again, capturing anew into this same struct.
	const TArray<FUserFocus> Captured = MoveTemp(Users);
	const TWeakObjectPtr<UDreamWidget> CapturedOpener = Opener;
	const TWeakObjectPtr<const UDreamWidget> PopupRoot(InPopupRoot);
	UWorld* CapturedWorld = World.Get();
	Reset();

	UDreamUIInputServices* Services = CapturedWorld != nullptr ? UDreamUIInputServices::Get(CapturedWorld) : nullptr;
	if (Services == nullptr)
	{
		return 0;
	}
	TArray<int32> UserIndices;
	Services->GetUserIndices(UserIndices);
	const TWeakObjectPtr<UDreamUIInputServices> WeakServices(Services);
	TMap<int32, FDreamUIFocusRevision> Revisions;
	for (const int32 UserIndex : UserIndices)Revisions.Add(UserIndex, Services->GetFocusRevision(UserIndex));
	int32 Changed = 0;
	for (const int32 UserIndex : UserIndices)
	{
		if (!WeakServices.IsValid())break;
		// An earlier player's callback may have chosen for this player too, including choosing no focus.
		if (Services->GetFocusRevision(UserIndex) != Revisions.FindChecked(UserIndex))continue;
		UDreamWidget* Now = Services->GetFocusedWidget(UserIndex);
		const FUserFocus* Before = Captured.FindByPredicate([UserIndex](const FUserFocus& InEntry)
		{
			return InEntry.UserIndex == UserIndex;
		});
		const bool bInside = IsInside(Now, PopupRoot.Get());
		// Focus gone with nobody having moved it on: a click catcher that took it and was destroyed, a row hidden under
		// the player. Only for a player who had focus to lose -- one who never had any is not owed any.
		const bool bNowhere = !IsValid(Now) && Before != nullptr && Before->bHadFocus;
		if (!bInside && !bNowhere)
		{
			// Elsewhere: the player moved it on while the popup was up, and it is theirs to keep.
			continue;
		}
		if (TryFocus(*Services, UserIndex, CapturedOpener.Get(), PopupRoot.Get())
			|| (Before != nullptr && TryFocus(*Services, UserIndex, Before->Focused.Get(), PopupRoot.Get()))
			|| TryFocus(*Services, UserIndex, Services->ResolveScopeFocusTarget(UserIndex), PopupRoot.Get()))
		{
			++Changed;
			continue;
		}
		if (bInside)
		{
			// Nowhere usable to go: the focus does not stay on something about to be hidden.
			Services->ClearFocus(Now, UserIndex, 0);
			++Changed;
		}
	}
	return Changed;
}

void FDreamFocusReturn::Reset()
{
	Opener.Reset();
	World.Reset();
	Users.Reset();
	bCaptured = false;
}

int32 FDreamFocusReturn::MoveFocusOutOf(const UDreamWidget* InRoot, UDreamWidget* InFallback)
{
	using namespace DreamUIFocusReturnLocal;
	const TWeakObjectPtr<const UDreamWidget> Root(InRoot);
	const TWeakObjectPtr<UDreamWidget> Fallback(InFallback);
	UWorld* RootWorld = IsValid(InRoot) ? InRoot->GetWorld() : nullptr;
	UDreamUIInputServices* Services = RootWorld != nullptr ? UDreamUIInputServices::Get(RootWorld) : nullptr;
	if (Services == nullptr)
	{
		return 0;
	}
	TArray<int32> UserIndices;
	Services->GetUserIndices(UserIndices);
	const TWeakObjectPtr<UDreamUIInputServices> WeakServices(Services);
	TMap<int32, FDreamUIFocusRevision> Revisions;
	for (const int32 UserIndex : UserIndices)Revisions.Add(UserIndex, Services->GetFocusRevision(UserIndex));
	int32 Changed = 0;
	for (const int32 UserIndex : UserIndices)
	{
		if (!WeakServices.IsValid())break;
		// An earlier player's callback may have chosen for this player too, including choosing no focus.
		if (Services->GetFocusRevision(UserIndex) != Revisions.FindChecked(UserIndex))continue;
		UDreamWidget* Now = Services->GetFocusedWidget(UserIndex);
		if (!IsInside(Now, Root.Get()))
		{
			// Elsewhere, or nowhere: nothing says this part of the widget ever had it.
			continue;
		}
		if (TryFocus(*Services, UserIndex, Fallback.Get(), Root.Get())
			|| TryFocus(*Services, UserIndex, Services->ResolveScopeFocusTarget(UserIndex), Root.Get()))
		{
			++Changed;
			continue;
		}
		Services->ClearFocus(Now, UserIndex, 0);
		++Changed;
	}
	return Changed;
}
