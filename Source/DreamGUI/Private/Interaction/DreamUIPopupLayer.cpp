// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIPopupLayer.h"

#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace DreamUIPopupLayerLocal
{
	/**
	 * The drag visual's band (UDreamUIDragDropSubsystem), with the tooltips' 30000 above it. A popup sorts under both: a
	 * drag or a tooltip started from inside a menu still draws over the menu, as they do over a modal.
	 */
	constexpr int32 DragVisualSortOrder = 29000;

	/** How far an opener has to move, in its plane's units, before an unplaced popup moves after it. */
	constexpr double FollowTolerance = 1.0e-3;

	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot)
	{
		return IsValid(InWidget) && IsValid(InRoot) && (InWidget == InRoot || InWidget->IsChildOf(InRoot));
	}

	/**
	 * What an opener has to stay for its popup to stay open: alive and in play along its whole parent chain -- a parent
	 * ends play before its children, so a destroy in progress shows above the opener first -- active, drawn and
	 * interactable. A world that never began play (the designer, a bare test world) has registered widgets only.
	 */
	bool IsOpenerUsable(const UDreamWidget* InOpener)
	{
		if (!IsValid(InOpener))
		{
			return false;
		}
		UWorld* OpenerWorld = InOpener->GetWorld();
		const UDreamUIManagerWorldSubsystem* Manager = OpenerWorld != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(OpenerWorld) : nullptr;
		const bool bWorldPlays = Manager != nullptr && Manager->HasBegunPlay();
		for (const UDreamWidget* Walker = InOpener; Walker != nullptr; Walker = Walker->GetParent())
		{
			if (!IsValid(Walker) || (bWorldPlays ? !Walker->HasBegunPlay() : !Walker->HasRegistered()))
			{
				return false;
			}
		}
		return InOpener->GetWidgetActiveInHierarchy() && InOpener->GetRenderVisibleInHierarchy() && InOpener->GetInteractableInHierarchy();
	}

	/** Where InWidget is in InPlane's plane -- Y across, Z up -- which is how Elevate anchors a lifted widget. */
	FVector2D PositionInPlane(const UDreamWidget* InWidget, const UDreamWidget* InPlane)
	{
		const FVector Local = InPlane->GetLayoutWorldTransform().InverseTransformPosition(InWidget->GetLayoutWorldTransform().GetLocation());
		return FVector2D(Local.Y, Local.Z);
	}

	/**
	 * Above everything InScreenRoot's canvases draw below the drag band -- the opener's own canvas, the player's top
	 * modal, a parent popup -- InPopup's own canvases left out, since they are what is being sorted. Two above, leaving
	 * the order between free for the player's sheet (UDreamUIPopupLayer::RefreshSheet).
	 */
	int32 ComputeSortOrder(UDreamWidget* InScreenRoot, const UDreamWidget* InPopup)
	{
		UDreamCanvas* RootCanvas = InScreenRoot->GetRootCanvas();
		if (RootCanvas == nullptr)
		{
			RootCanvas = InScreenRoot->GetComponent<UDreamCanvas>();
		}
		int32 Highest = 0;
		TArray<UDreamCanvas*> Canvases;
		UDreamCanvas::CollectChildrenCanvas(RootCanvas, Canvases, true);
		for (const UDreamCanvas* Canvas : Canvases)
		{
			if (IsInside(Canvas->GetWidget(), InPopup))
			{
				continue;
			}
			const int32 Order = Canvas->GetActualSortOrder();
			if (Order < DragVisualSortOrder)
			{
				Highest = FMath::Max(Highest, Order);
			}
		}
		return FMath::Min(Highest + 2, DragVisualSortOrder - 1);
	}

	/**
	 * InFrom's values into InTo, a slot of InFrom's class: every property UDreamPanelSlot and its subclasses declare -- what
	 * an author set (padding, nudge, alignment, the size rule, the size bounds) and the authored-geometry snapshot kept
	 * beside it -- and nothing of the sub-object it is, whose owner is a widget's and not a value.
	 */
	void CopySlotValues(const UDreamPanelSlot& InFrom, UDreamPanelSlot& InTo)
	{
		if (!InTo.IsA(InFrom.GetClass()))
		{
			return;
		}
		for (TFieldIterator<FProperty> It(InFrom.GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* Owner = Property->GetOwnerClass();
			if (Owner == nullptr || !Owner->IsChildOf(UDreamPanelSlot::StaticClass()) || Property->HasAnyPropertyFlags(CPF_Transient))
			{
				continue;
			}
			Property->CopyCompleteValue_InContainer(&InTo, &InFrom);
		}
	}

	/** The first widget under InRoot, in hierarchy order, that navigation can land on now. */
	UDreamWidget* FindFirstNavigable(UDreamWidget* InRoot)
	{
		if (!IsValid(InRoot) || !InRoot->GetWidgetActiveInHierarchy())
		{
			return nullptr;
		}
		if (UDreamUIBehaviour* Navigation = DreamUINavigationScan::FindNavigationBehaviour(InRoot);
			Navigation != nullptr && DreamUINavigationScan::CanNavigateTo(Navigation))
		{
			return InRoot;
		}
		for (UDreamWidget* Child : InRoot->GetChildren())
		{
			if (UDreamWidget* Found = FindFirstNavigable(Child))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

UDreamUIPopupLayer* UDreamUIPopupLayer::Get(const UObject* InWorldContext)
{
	const UWorld* World = IsValid(InWorldContext) ? InWorldContext->GetWorld() : nullptr;
	return World != nullptr ? World->GetSubsystem<UDreamUIPopupLayer>() : nullptr;
}

void UDreamUIPopupLayer::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
}

void UDreamUIPopupLayer::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the
	// world had no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	Super::Deinitialize();
}

void UDreamUIPopupLayer::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	bTornDownForWorld = true;
	// Nothing is given back or put back: the trees these popups belong to come down with the world, and so does the
	// input whose focus Dismiss would return. Their owners are still told, top first, so none of them goes on believing
	// its popup is up.
	TArray<FOpenPopup> Closing = MoveTemp(OpenPopups);
	OpenPopups.Reset();
	UpdateLayoutHook();
	for (int32 Index = Closing.Num() - 1; Index >= 0; --Index)
	{
		Closing[Index].OnDismissed.ExecuteIfBound(Closing[Index].Popup.Get(), EDreamPopupDismissReason::WorldTeardown);
	}
	ElevatedHomes.Reset();
	// The sheets hang off the screen roots, and go with them.
	Sheets.Reset();
}

bool UDreamUIPopupLayer::Elevate(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (ElevatedHomes.Contains(FObjectKey(InWidget)))
	{
		// Already up. The owner opening twice without closing is a state question the owner settled;
		// re-recording a home here would overwrite the real one with the screen root.
		return true;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(InWidget->GetWorld());
	// The popup goes up on the screen its owner is already on, not always the first player's.
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForWidget(InWidget) : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return false;
	}
	UDreamWidget* Home = InWidget->GetParent();
	if (!IsValid(Home) || Home == ScreenRoot)
	{
		return Home == ScreenRoot;
	}
	// The move keeps the ON-SCREEN placement, and TrySetParent's keep-world flag cannot do that for
	// a laid-out widget: it preserves RelativeLocation, which the next layout pass re-derives from
	// the anchors -- and the anchors the owner authored (UUIDropdown::Show anchors the list against
	// its FACE) mean something entirely different measured against the screen root. So the anchors
	// are collapsed to a point, the size the layout had computed is pinned as authored size, and the
	// anchored position is the old world position expressed in the root's plane -- Y across, Z up,
	// the same axes UUISlider reads pointer positions in.
	const FTransform OldWorld = InWidget->GetLayoutWorldTransform();
	const float Width = InWidget->GetWidth();
	const float Height = InWidget->GetHeight();
	const FVector2D Pivot = InWidget->GetPivot();
	// Read before the move takes it out of its siblings: the trip home puts it back in the same place, so an owner that
	// draws its children in order -- or a layout that counts them -- finds it where it left it.
	const int32 HomeSiblingIndex = InWidget->GetSiblingIndex();
	// And its slot, before the move takes that too -- the screen root hands out none, and a slot made again on the way
	// home would start from defaults. A copy of it goes with the home: the trip back refills the new slot from it, and a
	// lifted widget placed by its home panel's rules (the panel menu anchor's menu) reads its padding and nudge there.
	UDreamPanelSlot* HomeSlotCopy = nullptr;
	if (const UDreamPanelSlot* HomeSlot = InWidget->GetPanelSlot(); IsValid(HomeSlot))
	{
		HomeSlotCopy = NewObject<UDreamPanelSlot>(GetTransientPackage(), HomeSlot->GetClass(), NAME_None, RF_Transient);
		DreamUIPopupLayerLocal::CopySlotValues(*HomeSlot, *HomeSlotCopy);
	}
	if (!InWidget->TrySetParent(ScreenRoot, /*InKeepWorldPosition*/false))
	{
		return false;
	}
	InWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
	InWidget->SetWidth(Width);
	InWidget->SetHeight(Height);
	InWidget->SetPivot(Pivot);
	const FVector LocalInRoot = ScreenRoot->GetLayoutWorldTransform().InverseTransformPosition(OldWorld.GetLocation());
	InWidget->SetAnchoredPosition(FVector2D(LocalInRoot.Y, LocalInRoot.Z));
	FElevatedHome& Entry = ElevatedHomes.Add(FObjectKey(InWidget));
	Entry.Parent = Home;
	Entry.SiblingIndex = HomeSiblingIndex;
	Entry.HomeSlot = TStrongObjectPtr<UDreamPanelSlot>(HomeSlotCopy);
	return true;
}

void UDreamUIPopupLayer::Restore(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	// A popup still open on the stack is closed the way its owner closing it would close it -- its children, its focus,
	// its owner told -- before it goes home. Dismiss has taken it home already when it asked to be put back.
	if (IsOpen(InWidget))
	{
		Dismiss(InWidget, EDreamPopupDismissReason::Explicit);
	}
	RestoreHome(InWidget);
}

void UDreamUIPopupLayer::RestoreHome(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	FElevatedHome Home;
	if (!ElevatedHomes.RemoveAndCopyValue(FObjectKey(InWidget), Home))
	{
		return;
	}
	if (UDreamWidget* HomeWidget = Home.Parent.Get())
	{
		// No position juggling on the way home: the widget is hidden the moment it lands, and the
		// owner's next open rewrites anchors, size and position from scratch anyway. An index the
		// home no longer has puts it last, which is where it would have gone before.
		InWidget->TrySetParent(HomeWidget, /*InKeepWorldPosition*/false, Home.SiblingIndex);
		if (InWidget->GetParent() == HomeWidget)
		{
			// Home as it left: drawn in its home's canvas again, and laid out by its home's panel with the slot it had.
			PutBackPopupCanvas(InWidget, Home);
			RefillHomeSlot(InWidget, Home);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' has nowhere to return to; its owner is gone."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InWidget->GetDisplayName());
	}
}

const UDreamPanelSlot* UDreamUIPopupLayer::GetHomeSlot(const UDreamWidget* InWidget) const
{
	const FElevatedHome* Home = InWidget != nullptr ? ElevatedHomes.Find(FObjectKey(InWidget)) : nullptr;
	return Home != nullptr ? Home->HomeSlot.Get() : nullptr;
}

void UDreamUIPopupLayer::SortPopupCanvas(UDreamWidget* InPopup, UDreamWidget* InScreenRoot)
{
	using namespace DreamUIPopupLayerLocal;
	UDreamCanvas* Canvas = InPopup->GetComponent<UDreamCanvas>();
	const bool bFoundCanvas = IsValid(Canvas);
	if (!bFoundCanvas)
	{
		Canvas = InPopup->AddComponent<UDreamCanvas>();
		if (!IsValid(Canvas))
		{
			return;
		}
	}
	// What the trip home undoes, noted at the trip's first sort only: a popup pushed again while still lifted -- kept up to
	// fade out after it closed -- is in the popup band already, which is not the sorting it came with. A canvas that went
	// away since the note (its owner removed it) is noted afresh.
	if (FElevatedHome* Home = ElevatedHomes.Find(FObjectKey(InPopup)); Home != nullptr && Home->PushCanvas.Get() != Canvas)
	{
		Home->PushCanvas = Canvas;
		Home->bPushAddedCanvas = !bFoundCanvas;
		Home->bCanvasOverrideSortingBefore = Canvas->GetOverrideSorting();
		Home->CanvasSortOrderBefore = Canvas->GetSortOrder();
		Home->CanvasTraceChannelBefore = static_cast<int32>(Canvas->GetTraceChannel().GetValue());
	}
	Canvas->SetOverrideSorting(true);
	Canvas->SetSortOrder(ComputeSortOrder(InScreenRoot, InPopup), /*PropagateToChildrenCanvas*/true);
	if (const UDreamCanvas* RootCanvas = InScreenRoot->GetRootCanvas())
	{
		Canvas->SetTraceChannel(RootCanvas->GetTraceChannel());
	}
}

void UDreamUIPopupLayer::PutBackPopupCanvas(UDreamWidget* InWidget, const FElevatedHome& InHome)
{
	UDreamCanvas* Canvas = InHome.PushCanvas.Get();
	if (!IsValid(Canvas) || Canvas->GetWidget() != InWidget)
	{
		return;
	}
	// The order first, with the children's: the push moved every canvas inside the popup along with its own.
	Canvas->SetSortOrder(InHome.CanvasSortOrderBefore, /*PropagateToChildrenCanvas*/true);
	if (InHome.bPushAddedCanvas)
	{
		// The canvas was the push's, not the widget's: back under its home it draws in its home's canvas again, sorted
		// with its siblings, as it did before it was ever lifted.
		InWidget->RemoveComponent(Canvas);
		return;
	}
	Canvas->SetOverrideSorting(InHome.bCanvasOverrideSortingBefore);
	Canvas->SetTraceChannel(TEnumAsByte<ETraceTypeQuery>(static_cast<ETraceTypeQuery>(InHome.CanvasTraceChannelBefore)));
}

void UDreamUIPopupLayer::RefillHomeSlot(UDreamWidget* InWidget, const FElevatedHome& InHome)
{
	const UDreamPanelSlot* Saved = InHome.HomeSlot.Get();
	UDreamPanelSlot* Slot = InWidget->GetPanelSlot();
	if (Saved == nullptr || !IsValid(Slot))
	{
		// It had no slot before it left, or its home hands out none now: there is nothing to refill.
		return;
	}
	if (Slot->GetClass() != Saved->GetClass())
	{
		// The trip home made a slot of the default class; the one it had was another.
		Slot = InWidget->CreateNewPanelSlot(Saved->GetClass());
		if (!IsValid(Slot))
		{
			return;
		}
	}
	// Over what the new slot captured as it was made -- the lifted rect as "authored" -- with what the old one held,
	// authored-geometry snapshot included: the widget's layout reads the slot it had.
	DreamUIPopupLayerLocal::CopySlotValues(*Saved, *Slot);
	Slot->NotifySlotChanged(EDreamLayoutInvalidation::Measure);
}

bool UDreamUIPopupLayer::Push(const FDreamPopupParams& Params)
{
	using namespace DreamUIPopupLayerLocal;
	UDreamWidget* Popup = Params.Popup;
	if (!IsValid(Popup) || bTornDownForWorld)
	{
		return false;
	}
	if (IsOpen(Popup))
	{
		return true;
	}
	// An opener inside the popup is no opener: a popup following something it carries along would chase itself.
	UDreamWidget* Opener = IsValid(Params.Opener) && !IsInside(Params.Opener, Popup) ? Params.Opener : nullptr;
	const int32 UserIndex = Params.UserIndex;
	// The parent is decided against the stack as it stands, before anything below changes it: the player's deepest open
	// popup holding the opener -- a submenu's anchor sits inside the menu it opens from.
	UDreamWidget* ParentPopup = Opener != nullptr ? FindPopupContaining(Opener, UserIndex) : nullptr;

	// The lift first, so a popup that cannot go up changes nothing: no sibling is replaced for a popup that never opens.
	if (!Elevate(Popup))
	{
		return false;
	}
	UDreamWidget* Plane = Popup->GetParent();
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(Popup->GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForWidget(Popup) : nullptr;
	if (!IsValid(Plane) || !IsValid(ScreenRoot))
	{
		RestoreHome(Popup);
		return false;
	}

	// A new top-level popup replaces the player's others, a child its siblings -- each taking its own children with it.
	// Before the capture below, so the focus it records is what those popups gave back, not something inside them.
	TArray<FObjectKey> Replaced;
	for (const FOpenPopup& Entry : OpenPopups)
	{
		if (Entry.UserIndex == UserIndex && !Entry.bDismissing && Entry.Popup.Get() != Popup
			&& (ParentPopup == nullptr ? Entry.bTopLevel : (!Entry.bTopLevel && Entry.ParentKey == FObjectKey(ParentPopup))))
		{
			Replaced.Add(Entry.PopupKey);
		}
	}
	for (const FObjectKey& Sibling : Replaced)
	{
		DismissEntry(Sibling, EDreamPopupDismissReason::Replaced);
	}
	if (!IsValid(Popup) || IsOpen(Popup))
	{
		// A replaced popup's owner, told, destroyed this one or pushed it itself.
		return IsValid(Popup) && IsOpen(Popup);
	}
	if (Popup->GetParent() != Plane)
	{
		// Or took it back down from the lift: it does not open, and is lifted no longer.
		ElevatedHomes.Remove(FObjectKey(Popup));
		return false;
	}
	if (ParentPopup != nullptr && !IsOpen(ParentPopup))
	{
		// The parent closed while its siblings were going (its owner's handler did it): a top-level popup now.
		ParentPopup = nullptr;
	}

	// A canvas of its own, sorted above everything the player's screen shows under the drag and tooltip bands -- undone on
	// the way home (PutBackPopupCanvas).
	SortPopupCanvas(Popup, ScreenRoot);

	FOpenPopup& Entry = OpenPopups.AddDefaulted_GetRef();
	Entry.Popup = Popup;
	Entry.PopupKey = FObjectKey(Popup);
	Entry.Opener = Opener;
	Entry.bHasOpener = Opener != nullptr;
	Entry.bTopLevel = ParentPopup == nullptr;
	Entry.ParentKey = ParentPopup != nullptr ? FObjectKey(ParentPopup) : FObjectKey();
	Entry.UserIndex = UserIndex;
	Entry.OutsideClick = Params.OutsideClick;
	Entry.bRestoreOnDismiss = Params.bRestoreOnDismiss;
	Entry.TabBehavior = Params.TabBehavior;
	Entry.Place = Params.Place;
	Entry.OnDismissed = Params.OnDismissed;
	Entry.OnClosing = Params.OnClosing;
	Entry.LastOpenerPosition = Opener != nullptr ? PositionInPlane(Opener, Plane) : FVector2D::ZeroVector;
	// Every player's focus, before the popup takes any. With no opener the popup itself stands in for one -- it is the
	// root being closed when focus comes back, so never a place to put it -- and the capture still knows its world.
	Entry.FocusReturn.Capture(Opener != nullptr ? Opener : Popup);
	UpdateLayoutHook();
	RefreshSheet(UserIndex);

	if (Params.Place.IsBound())
	{
		Params.Place.Execute(Popup);
	}

	if (Params.bFocusOnOpen)
	{
		if (UDreamUIInputServices* Services = UDreamUIInputServices::Get(Popup))
		{
			UDreamWidget* Target = IsInside(Params.InitialFocus, Popup) ? Params.InitialFocus : nullptr;
			// Opened, and so registered, before focus moves: the deselect handlers it runs find the popup open -- a
			// dropdown's face losing focus to its own row must not read that as focus leaving the dropdown.
			// The first navigable control only while the popup is still open: a handler of that move may have closed it, and
			// the focus its dismissal gave back is not taken into a popup that is no longer up.
			if ((Target == nullptr || !Services->FocusForNavigation(Target, UserIndex)) && IsValid(Popup) && IsOpen(Popup))
			{
				// A popup with nothing navigable in it keeps focus where it was rather than dropping it somewhere.
				if (UDreamWidget* First = FindFirstNavigable(Popup))
				{
					Services->FocusForNavigation(First, UserIndex);
				}
			}
		}
	}
	// Open as it returns, which the focus move above may have undone: its deselect and select handlers are game code, and
	// one of them closing the popup (or destroying it) has had it dismissed, its owner told. True here would have the
	// owner record as open a popup that is already closed -- a menu announcing it opened after it announced it closed.
	return IsValid(Popup) && IsOpen(Popup);
}

void UDreamUIPopupLayer::Dismiss(UDreamWidget* InPopup, EDreamPopupDismissReason InReason)
{
	const int32 Index = FindOpenIndex(InPopup);
	if (Index != INDEX_NONE)
	{
		DismissEntry(OpenPopups[Index].PopupKey, InReason);
	}
}

void UDreamUIPopupLayer::DismissAll(int32 InUserIndex, EDreamPopupDismissReason InReason)
{
	// By identity, the newest first, over a list taken before any owner runs: each dismissal runs owner code that may
	// close, open or destroy popups, and one closed meanwhile -- a child its parent took with it -- is passed over.
	// Every one is told InReason itself, rather than ParentClosed through its parent.
	TArray<FObjectKey> Closing;
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		if (OpenPopups[Index].UserIndex == InUserIndex && !OpenPopups[Index].bDismissing)
		{
			Closing.Add(OpenPopups[Index].PopupKey);
		}
	}
	for (const FObjectKey& Key : Closing)
	{
		DismissEntry(Key, InReason);
	}
}

void UDreamUIPopupLayer::DismissEntry(FObjectKey InPopupKey, EDreamPopupDismissReason InReason)
{
	int32 Index = FindOpenIndexByKey(InPopupKey);
	if (Index == INDEX_NONE || OpenPopups[Index].bDismissing)
	{
		return;
	}
	OpenPopups[Index].bDismissing = true;

	// Children first, the newest first, as Slate's menu stack closes them: a child gives its focus back into this popup,
	// which is still up to take it. By identity, so a child destroyed meanwhile is closed with the rest.
	TArray<FObjectKey> Children;
	for (int32 ChildIndex = OpenPopups.Num() - 1; ChildIndex >= 0; --ChildIndex)
	{
		const FOpenPopup& Entry = OpenPopups[ChildIndex];
		if (!Entry.bTopLevel && Entry.ParentKey == InPopupKey && !Entry.bDismissing)
		{
			Children.Add(Entry.PopupKey);
		}
	}
	for (const FObjectKey& Child : Children)
	{
		DismissEntry(Child, EDreamPopupDismissReason::ParentClosed);
	}

	// Found again: the children's handlers may have moved this entry in the array, or destroyed the popup -- which is
	// closed all the same -- or the world's teardown may have taken the whole stack, telling every owner itself.
	Index = FindOpenIndexByKey(InPopupKey);
	if (Index == INDEX_NONE)
	{
		return;
	}
	// Off the stack before anything runs: returning focus runs deselect handlers, and one that closes this popup again
	// -- a dropdown hiding on losing focus -- finds it closed.
	FOpenPopup Closing = MoveTemp(OpenPopups[Index]);
	OpenPopups.RemoveAt(Index);
	UpdateLayoutHook();
	RefreshSheet(Closing.UserIndex);

	// The owner's look at the popup as it was, before the focus leaves it: what the player had highlighted in it is still
	// what they have focused (a dropdown commits that row on Tab). Read-only, by contract; read again below all the same.
	Closing.OnClosing.ExecuteIfBound(Closing.Popup.Get(), InReason);

	// Null for a popup destroyed while open: the focus it held went with it, and only the players left with none get any
	// back; there is no way home to take.
	UDreamWidget* Popup = Closing.Popup.Get();
	// Focus first, while everything is still where the player saw it: hiding is what clears a focus that cannot stay.
	Closing.FocusReturn.Return(Popup);
	if (Popup == nullptr)
	{
		ElevatedHomes.Remove(InPopupKey);
	}
	else if (Closing.bRestoreOnDismiss)
	{
		RestoreHome(Popup);
	}
	Closing.OnDismissed.ExecuteIfBound(Popup, InReason);
}

bool UDreamUIPopupLayer::IsOpen(const UDreamWidget* InPopup) const
{
	const int32 Index = FindOpenIndex(InPopup);
	return Index != INDEX_NONE && !OpenPopups[Index].bDismissing;
}

UDreamWidget* UDreamUIPopupLayer::GetTopPopup(int32 InUserIndex) const
{
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		const FOpenPopup& Entry = OpenPopups[Index];
		if (Entry.UserIndex == InUserIndex && !Entry.bDismissing)
		{
			if (UDreamWidget* Popup = Entry.Popup.Get(); IsValid(Popup))
			{
				return Popup;
			}
		}
	}
	return nullptr;
}

void UDreamUIPopupLayer::GetOpenPopups(int32 InUserIndex, TArray<UDreamWidget*>& OutPopups) const
{
	OutPopups.Reset();
	for (const FOpenPopup& Entry : OpenPopups)
	{
		if (Entry.UserIndex == InUserIndex && !Entry.bDismissing)
		{
			if (UDreamWidget* Popup = Entry.Popup.Get(); IsValid(Popup))
			{
				OutPopups.Add(Popup);
			}
		}
	}
}

UDreamWidget* UDreamUIPopupLayer::FindPopupContaining(const UDreamWidget* InWidget, int32 InUserIndex) const
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	// The newest first: a player's popups are a chain pushed in depth order, so the first that holds the widget is the
	// deepest that does.
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		const FOpenPopup& Entry = OpenPopups[Index];
		if (Entry.bDismissing || (InUserIndex != INDEX_NONE && Entry.UserIndex != InUserIndex))
		{
			continue;
		}
		UDreamWidget* Popup = Entry.Popup.Get();
		if (DreamUIPopupLayerLocal::IsInside(InWidget, Popup))
		{
			return Popup;
		}
	}
	return nullptr;
}

bool UDreamUIPopupLayer::NotifyPointerDown(int32 InUserIndex, UDreamWidget* InHitWidget)
{
	TArray<UDreamWidget*> Chain;
	GetOpenPopups(InUserIndex, Chain);
	if (Chain.Num() == 0)
	{
		return false;
	}
	// The popup the press landed in stays, with everything below it; everything above it closes. A press in none of
	// them closes them all.
	UDreamWidget* PressedIn = FindPopupContaining(InHitWidget, InUserIndex);
	const int32 Keep = PressedIn != nullptr ? Chain.IndexOfByKey(PressedIn) : INDEX_NONE;
	bool bConsume = false;
	for (int32 ChainIndex = Chain.Num() - 1; ChainIndex > Keep; --ChainIndex)
	{
		const int32 Index = FindOpenIndex(Chain[ChainIndex]);
		if (Index == INDEX_NONE || OpenPopups[Index].bDismissing)
		{
			// Gone with a popup closed before it: its parent.
			continue;
		}
		const EDreamPopupOutsideClick OutsideClick = OpenPopups[Index].OutsideClick;
		if (OutsideClick == EDreamPopupOutsideClick::Ignore)
		{
			continue;
		}
		// A press on the popup's own opener is the opener's toggle whatever the mode: let through, it would reach the
		// opener and open the popup it has just closed again -- what UMG's ShouldOpenDueToClick is there to stop.
		const UDreamWidget* Opener = OpenPopups[Index].Opener.Get();
		// And a press on something drawn in front of the popup -- a modal, a dialog or a page put up after it opened -- is
		// that layer's: the popup closes as for any press outside it, but swallowing the press would take the first click
		// on the layer in front from the player, a click the popup was never in the way of. Asked before the dismissal,
		// which takes the popup's canvas away.
		const bool bLandedInFront = IsInFrontOfPopup(InHitWidget, OpenPopups[Index].Popup.Get());
		bConsume |= (OutsideClick == EDreamPopupOutsideClick::Consume && !bLandedInFront)
			|| (Opener != nullptr && DreamUIPopupLayerLocal::IsInside(InHitWidget, Opener));
		Dismiss(Chain[ChainIndex], EDreamPopupDismissReason::OutsideClick);
	}
	return bConsume;
}

bool UDreamUIPopupLayer::HandleBack(int32 InUserIndex)
{
	// The top one only, whatever it does with outside clicks -- SComboBox's gamepad Back -- where Slate's Escape would
	// close every menu at once.
	UDreamWidget* Top = GetTopPopup(InUserIndex);
	if (Top == nullptr)
	{
		return false;
	}
	// The newest layer first: focus the player has moved onto something drawn in front of the popup -- a dialog or a page
	// put up after it opened -- belongs to that layer's screen, which the navigation stack offers Back to when this says
	// no. The popup behind it waits for the Back after. (A modal closes the player's popups as it comes up.)
	if (const UDreamUIInputServices* Services = UDreamUIInputServices::Get(this);
		Services != nullptr && IsInFrontOfPopup(Services->GetFocusedWidget(InUserIndex), Top))
	{
		return false;
	}
	Dismiss(Top, EDreamPopupDismissReason::Back);
	return true;
}

EDreamPopupTabBehavior UDreamUIPopupLayer::GetTopPopupTabBehavior(int32 InUserIndex) const
{
	// The same popup GetTopPopup answers: the player's newest that is open and alive.
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		const FOpenPopup& Entry = OpenPopups[Index];
		if (Entry.UserIndex == InUserIndex && !Entry.bDismissing && IsValid(Entry.Popup.Get()))
		{
			return Entry.TabBehavior;
		}
	}
	return EDreamPopupTabBehavior::Cycle;
}

UDreamWidget* UDreamUIPopupLayer::CloseForTab(int32 InUserIndex)
{
	if (GetTopPopupTabBehavior(InUserIndex) != EDreamPopupTabBehavior::CloseAndContinue)
	{
		// The top one cycles, or there is none: the Tab stays in it, or never concerned a popup.
		return nullptr;
	}
	// The player's chain from the top down, as far as it closes on Tab: a popup that cycles stays open, and with it every
	// popup under it. Taken before any owner runs, by identity; the opener kept is the lowest closing popup's.
	TArray<FObjectKey> Closing;
	TWeakObjectPtr<UDreamWidget> BottomOpener;
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		const FOpenPopup& Entry = OpenPopups[Index];
		if (Entry.UserIndex != InUserIndex || Entry.bDismissing || !IsValid(Entry.Popup.Get()))
		{
			continue;
		}
		if (Entry.TabBehavior != EDreamPopupTabBehavior::CloseAndContinue)
		{
			break;
		}
		Closing.Add(Entry.PopupKey);
		BottomOpener = Entry.Opener;
	}
	// Top first, each told Tab itself -- a dropdown commits its highlighted row -- and each giving its focus back as it
	// goes: a submenu's to the item it opened from, the menu's to what had focus when it opened.
	for (const FObjectKey& Key : Closing)
	{
		DismissEntry(Key, EDreamPopupDismissReason::Tab);
	}
	UDreamWidget* Opener = BottomOpener.Get();
	if (!IsValid(Opener))
	{
		return nullptr;
	}
	// From the opener -- or from inside it, where the focus came back to: a panel menu anchor's opener is the panel, which
	// holds the very trigger its menu gave the focus back to, and a walk on from the panel would land on that trigger again.
	if (const UDreamUIInputServices* Services = UDreamUIInputServices::Get(this))
	{
		UDreamWidget* Focus = Services->GetFocusedWidget(InUserIndex);
		if (IsValid(Focus) && Focus != Opener && Focus->IsChildOf(Opener))
		{
			return Focus;
		}
	}
	return Opener;
}

bool UDreamUIPopupLayer::IsInFrontOfPopup(const UDreamWidget* InWidget, const UDreamWidget* InPopup) const
{
	if (!IsValid(InWidget) || !IsValid(InPopup) || FindPopupContaining(InWidget) != nullptr)
	{
		return false;
	}
	const UDreamCanvas* WidgetCanvas = InWidget->GetRenderCanvas();
	const UDreamCanvas* PopupCanvas = InPopup->GetRenderCanvas();
	if (!IsValid(WidgetCanvas) || !IsValid(PopupCanvas) || WidgetCanvas->GetRootCanvas() != PopupCanvas->GetRootCanvas())
	{
		// Another root canvas -- the world, a render target, another player's screen -- is no layer over this popup.
		return false;
	}
	return WidgetCanvas->GetActualSortOrder() > PopupCanvas->GetActualSortOrder();
}

void UDreamUIPopupLayer::SetOutsideClick(const UDreamWidget* InPopup, EDreamPopupOutsideClick InOutsideClick)
{
	const int32 Index = FindOpenIndex(InPopup);
	if (Index != INDEX_NONE && !OpenPopups[Index].bDismissing)
	{
		OpenPopups[Index].OutsideClick = InOutsideClick;
		RefreshSheet(OpenPopups[Index].UserIndex);
	}
}

bool UDreamUIPopupLayer::IsSheetOfAnotherPlayer(const UDreamWidget* InHitWidget, int32 InUserIndex) const
{
	if (InHitWidget == nullptr)
	{
		return false;
	}
	for (const TPair<int32, TWeakObjectPtr<UDreamWidget>>& Sheet : Sheets)
	{
		if (Sheet.Key != InUserIndex && Sheet.Value.Get() == InHitWidget)
		{
			return true;
		}
	}
	return false;
}

int32 UDreamUIPopupLayer::FindOpenIndex(const UDreamWidget* InPopup) const
{
	if (InPopup == nullptr)
	{
		return INDEX_NONE;
	}
	return OpenPopups.IndexOfByPredicate([InPopup](const FOpenPopup& InEntry)
	{
		return InEntry.Popup.Get() == InPopup;
	});
}

int32 UDreamUIPopupLayer::FindOpenIndexByKey(const FObjectKey& InPopupKey) const
{
	return OpenPopups.IndexOfByPredicate([&InPopupKey](const FOpenPopup& InEntry)
	{
		return InEntry.PopupKey == InPopupKey;
	});
}

void UDreamUIPopupLayer::UpdateLayoutHook()
{
	const bool bWantHook = !bTornDownForWorld && OpenPopups.Num() > 0;
	if (bWantHook == LayoutPassesFinishedHandle.IsValid())
	{
		return;
	}
	if (bWantHook)
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			LayoutPassesFinishedHandle = Manager->GetOnLayoutPassesFinished().AddUObject(this, &UDreamUIPopupLayer::HandleLayoutPassesFinished);
			HookedManager = Manager;
		}
		return;
	}
	if (UDreamUIManagerWorldSubsystem* Manager = HookedManager.Get())
	{
		Manager->GetOnLayoutPassesFinished().Remove(LayoutPassesFinishedHandle);
	}
	LayoutPassesFinishedHandle.Reset();
	HookedManager.Reset();
}

void UDreamUIPopupLayer::HandleLayoutPassesFinished()
{
	using namespace DreamUIPopupLayerLocal;
	SweepVanishedPopups();

	// Over a snapshot, bottom first: a popup dismissed here takes its children, which the walk then finds closed.
	TArray<TWeakObjectPtr<UDreamWidget>> Snapshot;
	Snapshot.Reserve(OpenPopups.Num());
	for (const FOpenPopup& Entry : OpenPopups)
	{
		Snapshot.Add(Entry.Popup);
	}
	for (const TWeakObjectPtr<UDreamWidget>& WeakPopup : Snapshot)
	{
		UDreamWidget* Popup = WeakPopup.Get();
		int32 Index = IsValid(Popup) ? FindOpenIndex(Popup) : INDEX_NONE;
		if (Index == INDEX_NONE || OpenPopups[Index].bDismissing)
		{
			continue;
		}
		// The opener first: one destroyed, out of play, put to sleep, hidden -- collapsed under an ancestor included --
		// or disabled takes its popup with it, as a hidden SMenuAnchor hides its menu.
		if (OpenPopups[Index].bHasOpener && !IsOpenerUsable(OpenPopups[Index].Opener.Get()))
		{
			Dismiss(Popup, EDreamPopupDismissReason::OpenerLost);
			continue;
		}
		if (OpenPopups[Index].Place.IsBound())
		{
			// A copy: the owner's code runs inside, and the array it lives in is not to be held across it.
			const FDreamPopupPlaceDelegate Place = OpenPopups[Index].Place;
			Place.Execute(Popup);
			continue;
		}
		// Unplaced: the popup keeps the offset from its opener it had -- moved by what the opener moved since the last
		// look, so an owner that moved the popup itself keeps its own placement too.
		UDreamWidget* Opener = OpenPopups[Index].Opener.Get();
		const UDreamWidget* Plane = Popup->GetParent();
		if (Opener == nullptr || !IsValid(Plane))
		{
			continue;
		}
		const FVector2D OpenerNow = PositionInPlane(Opener, Plane);
		const FVector2D Moved = OpenerNow - OpenPopups[Index].LastOpenerPosition;
		OpenPopups[Index].LastOpenerPosition = OpenerNow;
		if (!Moved.IsNearlyZero(FollowTolerance))
		{
			Popup->SetAnchoredPosition(Popup->GetAnchoredPosition() + Moved);
		}
	}
}

void UDreamUIPopupLayer::SweepVanishedPopups()
{
	// Destroyed while open, without their owners closing them, the newest first: closed the way Dismiss closes a popup
	// that is gone. The focus each held went with it, and its capture gives some back to the players left with none; its
	// children have nothing left to hang from, and close first; its owner is told, with no popup to hand over.
	TArray<FObjectKey> Vanished;
	for (int32 Index = OpenPopups.Num() - 1; Index >= 0; --Index)
	{
		if (!OpenPopups[Index].Popup.IsValid() && !OpenPopups[Index].bDismissing)
		{
			Vanished.Add(OpenPopups[Index].PopupKey);
		}
	}
	for (const FObjectKey& Key : Vanished)
	{
		DismissEntry(Key, EDreamPopupDismissReason::Explicit);
	}
}

void UDreamUIPopupLayer::RefreshSheet(int32 InUserIndex)
{
	// The player's open popups: the lowest sort order among them, the root the bottom one hangs in, and whether one of
	// them eats the presses outside it -- the one kind a sheet is for: a press let through, or ignored, has to reach
	// what is under it, and so does the hover before it.
	bool bWanted = false;
	int32 LowestSort = MAX_int32;
	UDreamWidget* Root = nullptr;
	for (const FOpenPopup& Entry : OpenPopups)
	{
		UDreamWidget* Popup = Entry.Popup.Get();
		if (Entry.UserIndex != InUserIndex || Entry.bDismissing || !IsValid(Popup))
		{
			continue;
		}
		if (Root == nullptr)
		{
			Root = Popup->GetParent();
		}
		if (const UDreamCanvas* PopupCanvas = Popup->GetComponent<UDreamCanvas>())
		{
			LowestSort = FMath::Min(LowestSort, PopupCanvas->GetActualSortOrder());
		}
		bWanted |= Entry.OutsideClick == EDreamPopupOutsideClick::Consume;
	}
	bWanted = bWanted && !bTornDownForWorld && IsValid(Root) && LowestSort != MAX_int32;

	const TWeakObjectPtr<UDreamWidget>* Existing = Sheets.Find(InUserIndex);
	UDreamWidget* Sheet = Existing != nullptr ? Existing->Get() : nullptr;
	if (!bWanted || (IsValid(Sheet) && Sheet->GetParent() != Root))
	{
		if (IsValid(Sheet))
		{
			Sheet->DestroyWidget();
		}
		Sheets.Remove(InUserIndex);
		Sheet = nullptr;
		if (!bWanted)
		{
			return;
		}
	}
	if (!IsValid(Sheet))
	{
		// Built the way the click catchers it replaces were, minus the button: a stretched widget with an empty visual,
		// which draws nothing and is still what a ray hits, on a canvas of its own.
		Sheet = NewObject<UDreamWidget>(Root->GetOuter(), NAME_None, RF_Transient);
		Sheet->SetDisplayName(TEXT("DreamUIPopupSheet"));
		Sheet->SetParent(Root, /*InKeepWorldPosition*/false);
		Sheet->SetSizeDelta(FVector2D::ZeroVector);
		Sheet->SetAnchorMin(FVector2D(0.0, 0.0));
		Sheet->SetAnchorMax(FVector2D(1.0, 1.0));
		Sheet->CreateNewVisual<UDreamVisualEmpty>();
		if (UDreamCanvas* SheetCanvas = Sheet->AddComponent<UDreamCanvas>())
		{
			SheetCanvas->SetOverrideSorting(true);
			if (const UDreamCanvas* RootCanvas = Root->GetRootCanvas())
			{
				SheetCanvas->SetTraceChannel(RootCanvas->GetTraceChannel());
			}
		}
		Sheets.Add(InUserIndex, Sheet);
	}
	if (UDreamCanvas* SheetCanvas = Sheet->GetComponent<UDreamCanvas>())
	{
		// Just under the player's lowest popup, over everything else on the screen: Push sorts each popup two above what
		// is there, which leaves this order free.
		SheetCanvas->SetSortOrder(LowestSort - 1, /*PropagateToChildrenCanvas*/true);
	}
}
