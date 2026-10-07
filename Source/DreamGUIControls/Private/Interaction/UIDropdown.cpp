// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIDropdown.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamGUISettings.h"
#include "DreamUIBPLibrary.h"
#include "Core/DreamUIClipData.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Event/DreamPointerEventData.h"
#include "Misc/ScopeExit.h"



UUIDropdown::UUIDropdown()
{
	// The list it opens is placed when it opens; the dropdown itself has nothing to do each frame, nor when it moves.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
	ListRaycastableBeforeInert = EDreamWidgetRaycastableType::Inherit;
}

void UUIDropdown::Awake()
{
	Super::Awake();
	if (ListRoot.IsValid())
	{
		ListRoot->SetWidgetActive(false);
		ListRoot->SetRenderOpacity(0);
		// Only while nobody has said otherwise. Deriving it from the list root is the right guess for
		// a hand-wired behaviour, and the wrong answer for a control that already pushed the height
		// its own MaxVisibleItems asks for -- see bMaxHeightAuthored.
		if (!bMaxHeightAuthored)
		{
			MaxHeight = ListRoot->GetHeight();
		}
	}
	//set default display
	if (Options.Num() > 0)
	{
		auto tempValue = FMath::Clamp(Value, 0, Options.Num() - 1);
		if (CaptionText.IsValid())
		{
			CaptionText->SetText(Options[tempValue].Text);
		}
		if (CaptionImage.IsValid())
		{
			CaptionImage->SetBrush(Options[tempValue].ImageBrush);
		}
	}
}
void UUIDropdown::OnDisable()
{
	Super::OnDisable();
	// An open list cannot outlive the dropdown that would close it. A list lifted to the popup layer is
	// not under this widget at all -- so a dropdown hidden or destroyed while open left its list showing,
	// its rows calling back into a component that was gone. UDreamMenuAnchor closes on the way out for
	// the same reason. At once rather than faded: home under this widget now, the list is destroyed with
	// it. Asked first because CloseList reports a missing list root as an error, and a closed dropdown
	// has nothing to hide -- unless its last close is still fading, which is put away at once too.
	if (bIsShow)
	{
		CloseList(false);
	}
	else if (bListInert)
	{
		if (ShowOrHideTweener.IsValid())
		{
			ShowOrHideTweener->Kill();
		}
		PutListAway();
	}
}
void UUIDropdown::OnDestroy()
{
	Super::OnDestroy();
	// A list opened from code on a dropdown that was never enabled has had no OnDisable to close it.
	if (bIsShow)
	{
		CloseList(false);
	}
	else if (bListInert)
	{
		if (ShowOrHideTweener.IsValid())
		{
			ShowOrHideTweener->Kill();
		}
		PutListAway();
	}
}
#if WITH_EDITOR
void UUIDropdown::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (Options.Num() > 0)
	{
		auto TempValue = FMath::Clamp(Value, 0, Options.Num() - 1);
		if (CaptionText.IsValid())
		{
			CaptionText->SetText(Options[TempValue].Text);
		}
		if (CaptionImage.IsValid())
		{
			CaptionImage->SetBrush(Options[TempValue].ImageBrush);
		}
	}
}
#endif

bool UUIDropdown::IsCurrentListOperation(const TWeakObjectPtr<UUIDropdown>& InDropdown, uint64 InSerial, bool bInShow)
{
	const UUIDropdown* Dropdown = InDropdown.Get();
	return Dropdown != nullptr && IsValid(Dropdown->GetWidget()) && Dropdown->ListOperationSerial == InSerial
		&& Dropdown->bIsShow == bInShow && Dropdown->ListRoot.IsValid();
}

void UUIDropdown::Show()
{
	if (!IsValid(this))return;
	if (!ListRoot.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ListRoot is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	// Every reason not to open, BEFORE anything is opened. This check used to sit below, after
	// bIsShow was already true and the full-screen blocker was already built -- so a misconfigured
	// template left an invisible sheet over the whole UI that swallowed every click, and the only way
	// out was to click it (which routes to Hide) without being able to see it.
	if (!ItemTemplate.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ItemTemplate is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (!IsValid(this->GetWidget()))return;
	if (!IsValid(this->GetWidget()->GetRootCanvas()))return;
	if (bIsShow)return;
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 ShowSerial = ++ListOperationSerial;
	if (bRecreatingListItems)bNeedRecreate = true;
	bIsShow = true;
	if (ShowOrHideTweener.IsValid())
	{
		ShowOrHideTweener->Kill();
	}
	// A list still fading out from the last close is taken over where it is: home under the face again
	// to be placed from scratch below, and answering the pointer and the pad again.
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;
	ReturnListHome();
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;
	SetListInert(false);
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;

	//show list
	ListRoot->SetWidgetActive(true);
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;
	ShowOrHideTweener = ListRoot->RenderOpacityTo(1, 0.3f, 0, EDreamTweenEase::OutCubic);
	if (!ShowOrHideTweener.IsValid())
	{
		// No tween manager to fade it in -- a world with no game instance: the designer's preview, a
		// headless test. The end state is the only correct fallback: left alone, the list would open at
		// whatever opacity it was last given, which after Awake (or after a Hide that could not fade
		// either) is zero -- a list that is open, answering the pointer, and invisible.
		ListRoot->SetRenderOpacity(1.0f);
	}
	// Its own canvas over its hierarchy, which is what draws it above its siblings where the list opens in
	// place. Lifted to the popup layer below, the layer sorts it into the popup band.
	auto CanvasOnListRoot = ListRoot->GetComponent<UDreamCanvas>();
	if (!CanvasOnListRoot)
	{
		CanvasOnListRoot = ListRoot->AddComponent<UDreamCanvas>();
	}
	CanvasOnListRoot->SetSortOrderToHighestOfHierarchy(true);
	CanvasOnListRoot->SetOverrideSorting(true);

	//create list item as options -- the template was validated at the top, before anything opened
	if (bNeedRecreate)
	{
		RecreateListItems();
	}
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;

	//set position
	auto TempVerticalPosition = VerticalPosition;
	auto TempHorizontalPosition = HorizontalPosition;
	if (TempVerticalPosition == EUIDropdownVerticalPosition::Automatic
		|| TempHorizontalPosition == EUIDropdownHorizontalPosition::Automatic
		)
	{
		auto ThisWidget = GetWidget();
		if (ThisWidget->GetClipData().IsValid())//have valid ClipData, then use ClipData to tell if the Dropdown list visible
		{
			auto ClipData = ThisWidget->GetClipData().Pin();
			if (TempVerticalPosition == EUIDropdownVerticalPosition::Automatic)
			{
				FVector ListBottomWorldSpace;
				if (VerticalOverlap)
				{
					auto SelfTop = ThisWidget->GetLocalSpaceTop();
					auto ListBottomInSelfSpace = SelfTop - ListRoot->GetHeight();
					ListBottomWorldSpace = ThisWidget->GetWorldTransform().TransformPosition(FVector(0, 0, ListBottomInSelfSpace));
				}
				else
				{
					auto SelfBottom = ThisWidget->GetLocalSpaceBottom();
					auto ListBottomInSelfSpace = SelfBottom - ListRoot->GetHeight();
					ListBottomWorldSpace = ThisWidget->GetWorldTransform().TransformPosition(FVector(0, 0, ListBottomInSelfSpace));
				}
				if (!ClipData->IsPointVisible(ListBottomWorldSpace))
				{
					TempVerticalPosition = EUIDropdownVerticalPosition::Top;
				}
				else
				{
					TempVerticalPosition = EUIDropdownVerticalPosition::Bottom;//default is bottom
				}
			}
			if (TempHorizontalPosition == EUIDropdownHorizontalPosition::Automatic)
			{
				auto SelfRight = ThisWidget->GetLocalSpaceRight();
				auto ListRightWorldSpace = ThisWidget->GetWorldTransform().TransformPosition(FVector(0, SelfRight + ListRoot->GetWidth(), 0));
				if (!ClipData->IsPointVisible(ListRightWorldSpace))
				{
					TempHorizontalPosition = EUIDropdownHorizontalPosition::Left;
				}
				else
				{
					TempHorizontalPosition = EUIDropdownHorizontalPosition::Right;//default is right
				}
			}
		}
		else//no valid ClipData, then use RootCanvas
		{
			auto RootCanvasWidget = ThisWidget->GetRootCanvas()->GetWidget();
			FTransform SelfToCanvasSpaceTf;
			auto InverseCanvasSpaceTf = RootCanvasWidget->GetWorldTransform().Inverse();
			FTransform::Multiply(&SelfToCanvasSpaceTf, &ThisWidget->GetWorldTransform(), &InverseCanvasSpaceTf);
			if (TempVerticalPosition == EUIDropdownVerticalPosition::Automatic)
			{
				//convert top point position from drop-down's self to root ui space, and tell if it is inside root rect
				FVector ListBottomInClipSpace;
				if (VerticalOverlap)
				{
					auto SelfTop = ThisWidget->GetLocalSpaceTop();
					auto ListBottomInSelfSpace = SelfTop - ListRoot->GetHeight();
					ListBottomInClipSpace = SelfToCanvasSpaceTf.TransformPosition(FVector(0, 0, ListBottomInSelfSpace));
				}
				else
				{
					auto SelfBottom = ThisWidget->GetLocalSpaceBottom();
					auto ListBottomInSelfSpace = SelfBottom - ListRoot->GetHeight();
					ListBottomInClipSpace = SelfToCanvasSpaceTf.TransformPosition(FVector(0, 0, ListBottomInSelfSpace));
				}
				if (ListBottomInClipSpace.Z < RootCanvasWidget->GetLocalSpaceBottom())
				{
					TempVerticalPosition = EUIDropdownVerticalPosition::Top;
				}
				else
				{
					TempVerticalPosition = EUIDropdownVerticalPosition::Bottom;//default is bottom
				}
			}
			if (TempHorizontalPosition == EUIDropdownHorizontalPosition::Automatic)
			{
				auto SelfRight = ThisWidget->GetLocalSpaceRight();
				auto ListRightInCanvasSpace = SelfToCanvasSpaceTf.TransformPosition(FVector(0, SelfRight + ListRoot->GetWidth(), 0));
				if (ListRightInCanvasSpace.Y > RootCanvasWidget->GetLocalSpaceRight())
				{
					TempHorizontalPosition = EUIDropdownHorizontalPosition::Left;
				}
				else
				{
					TempHorizontalPosition = EUIDropdownHorizontalPosition::Right;//default is right
				}
			}
		}
	}

	FVector2D Pivot(0.5f, 0);
	switch (TempVerticalPosition)
	{
	case EUIDropdownVerticalPosition::Top:
	{
		Pivot.Y = 0.0f;
		if (VerticalOverlap)
		{
			ListRoot->SetVerticalAnchorMinMax(FVector2D(0.0f, 0.0f), true);
		}
		else
		{
			ListRoot->SetVerticalAnchorMinMax(FVector2D(1.0f, 1.0f), true);
		}
	}break;
	case EUIDropdownVerticalPosition::Middle:
	{
		Pivot.Y = 0.5f;
		ListRoot->SetVerticalAnchorMinMax(FVector2D(0.5f, 0.5f), true);
	}break;
	case EUIDropdownVerticalPosition::Bottom:
	{
		Pivot.Y = 1.0f;
		if (VerticalOverlap)
		{
			ListRoot->SetVerticalAnchorMinMax(FVector2D(1.0f, 1.0f), true);
		}
		else
		{
			ListRoot->SetVerticalAnchorMinMax(FVector2D(0.0f, 0.0f), true);
		}
	}break;
	}
	ListRoot->SetVerticalAnchoredPosition(0);

	switch (TempHorizontalPosition)
	{
	case EUIDropdownHorizontalPosition::Left:
	{
		Pivot.X = 1.0f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(0.0f, 0.0f), true);
	}break;
	case EUIDropdownHorizontalPosition::Center:
	{
		Pivot.X = 0.5f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(0.5f, 0.5f), true);
	}break;
	case EUIDropdownHorizontalPosition::Right:
	{
		Pivot.X = 0.0f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(1.0f, 1.0f), true);
	}break;
	}
	ListRoot->SetHorizontalAnchoredPosition(0);

	ListRoot->SetPivot(Pivot);

	// After the list is awake and POSITIONED, and before it goes up: a listener sizing it -- a control
	// that owns its rows' heights -- does it against the face, where the list still hangs.
	OnListVisibilityChangedCPP.Broadcast(true);
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))
	{
		// A handler of the opening closed it again.
		return;
	}
	// Then up on the popup layer, the UMG menu-stack arrangement: lifted to the screen root so no ancestor clips
	// it, focus moved onto the selected row, closed by a press outside it, by Back, by Tab, or by this face going away.
	// On the layer as far as a close is concerned from before the push: the push moves focus into the list, and a handler
	// that closes it then -- a deselect handler, game code -- must take it off the layer, not leave it there open and
	// asleep. A capture left from an earlier open the layer could not take is not this open's.
	ListFocusReturn.Reset();
	bListOnPopupLayer = true;
	const bool bPushed = PushListToPopupLayer();
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))
	{
		// A late push after a close is taken down only while still closed. A newer Show owns
		// the same list and its popup bookkeeping, and must not be dismissed by this old pass.
		if (WeakThis.IsValid() && !bIsShow)
		{
			bListOnPopupLayer = false;
			UDreamUIPopupLayer* Layer = bPushed ? UDreamUIPopupLayer::Get(this) : nullptr;
			if (Layer != nullptr && Layer->IsOpen(ListRoot.Get()))
			{
				Layer->Dismiss(ListRoot.Get(), EDreamPopupDismissReason::Explicit);
			}
		}
		return;
	}
	bListOnPopupLayer = bPushed;
	if (!bListOnPopupLayer)
	{
		// No layer to take it -- no screen root in this world: the list opens where it hangs, and the focus a player
		// moves into it still comes back to the face when it closes.
		ListFocusReturn.Capture(GetWidget());
	}
}
void UUIDropdown::Hide()
{
	CloseList(true);
}
void UUIDropdown::CloseList(bool bInAnimate)
{
	if (!IsValid(this))return;
	if (!ListRoot.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ListRoot is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (!bIsShow)return;
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 CloseSerial = ++ListOperationSerial;
	const TWeakObjectPtr<UDreamWidget> WeakList(ListRoot);
	bIsShow = false;
	UDreamWidget* List = WeakList.Get();
	// Focus first, before the fade and before anything moves: every player whose focus is in the list -- or
	// went nowhere from it -- is back on the face, in one step, so a row no longer takes the confirm or the
	// stick for the length of the fade, and a face deselected into nothing never gets it back. The popup
	// layer gives it back as it closes the list; a list it never had was captured at Show.
	if (bListOnPopupLayer)
	{
		bListOnPopupLayer = false;
		if (UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this))
		{
			Layer->Dismiss(List, EDreamPopupDismissReason::Explicit);
		}
	}
	else
	{
		ListFocusReturn.Return(List);
	}
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false) || !WeakList.IsValid())return;
	OnListVisibilityChangedCPP.Broadcast(false);
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false) || !WeakList.IsValid())return;
	if (ShowOrHideTweener.IsValid())
	{
		ShowOrHideTweener->Kill();
	}
	ShowOrHideTweener = nullptr;
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false) || !WeakList.IsValid())
	{
		// A handler of the close destroyed the list, or opened it again.
		return;
	}
	// The fade plays where the player saw the list -- still lifted, when it went up -- and nothing in it answers
	// meanwhile.
	SetListInert(true);
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false) || !WeakList.IsValid())return;
	List = WeakList.Get();

	// Asked before anything is chained onto it. RenderOpacityTo answers null wherever there is no tween
	// manager -- any world without a game instance, the designer's preview and a headless test among
	// them -- and chaining OnComplete straight onto that null was an access violation the moment a
	// list closed there. With no fade to wait for, the end state is written at once, the fallback
	// UDreamMenuAnchor::Open and UDreamRingMenu::Close already take.
	UDreamTweener* HideTweener = bInAnimate ? List->RenderOpacityTo(0, 0.3f, 0, EDreamTweenEase::InCubic) : nullptr;
	if (HideTweener != nullptr)
	{
		// Bound to the list, so it runs only while the list is alive; this component is reached weakly, since
		// the list can outlive it. One going away puts its list away at once (OnDisable), which kills this tween.
		UDreamWidget* FadingList = List;
		HideTweener->OnComplete(FSimpleDelegate::CreateWeakLambda(FadingList, [FadingList, WeakThis, CloseSerial]
		{
			if (UUIDropdown* Dropdown = WeakThis.Get())
			{
				if (IsCurrentListOperation(WeakThis, CloseSerial, false))
				{
					Dropdown->PutListAway();
				}
				return;
			}
			FadingList->SetWidgetActive(false);
		}));
		ShowOrHideTweener = HideTweener;
	}
	else
	{
		List->SetRenderOpacity(0.0f);
		PutListAway();
	}
}
void UUIDropdown::PutListAway()
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 CloseSerial = ListOperationSerial;
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false))return;
	ShowOrHideTweener = nullptr;
	if (UDreamWidget* List = ListRoot.Get(); IsValid(List))
	{
		// Home first, then asleep: a list put to sleep while still lifted would be an inactive widget hanging off
		// the screen root that nothing would ever come back for.
		ReturnListHome();
		if (!IsCurrentListOperation(WeakThis, CloseSerial, false))return;
		List->SetWidgetActive(false);
	}
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false))return;
	SetListInert(false);
	if (!IsCurrentListOperation(WeakThis, CloseSerial, false))return;
	OnListPutAwayCPP.Broadcast();
}
void UUIDropdown::ReturnListHome()
{
	UDreamWidget* List = ListRoot.Get();
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this);
	if (IsValid(List) && Layer != nullptr && !Layer->IsOpen(List))
	{
		// Nothing for a list that never went up.
		Layer->Restore(List);
	}
}
void UUIDropdown::SetListInert(bool bInInert)
{
	if (bListInert == bInInert)
	{
		return;
	}
	bListInert = bInInert;
	UDreamWidget* List = ListRoot.Get();
	if (bInInert)
	{
		if (!IsValid(List))
		{
			return;
		}
		// Out of the pointer's hit test -- the raycast does not read opacity -- and out of the navigation, without
		// the disabled look switching the rows off would paint over the fade.
		ListRaycastableBeforeInert = List->GetRaycastable();
		List->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
		TArray<UDreamWidget*> InList;
		UDreamWidget::CollectChildrenWidgets(List, InList, /*IncludeTarget*/true);
		for (UDreamWidget* Widget : InList)
		{
			for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
			{
				UUISelectable* Selectable = Cast<UUISelectable>(Component);
				if (IsValid(Selectable) && Selectable->GetCanNavigateHere())
				{
					Selectable->SetCanNavigateHere(false);
					RowsMadeUnnavigable.Add(Selectable);
				}
			}
		}
		return;
	}
	if (IsValid(List))
	{
		List->SetRaycastable(ListRaycastableBeforeInert);
	}
	for (const TWeakObjectPtr<UUISelectable>& Row : RowsMadeUnnavigable)
	{
		if (UUISelectable* Selectable = Row.Get())
		{
			Selectable->SetCanNavigateHere(true);
		}
	}
	RowsMadeUnnavigable.Reset();
}
EDreamPopupOutsideClick UUIDropdown::ResolveOutsideClick() const
{
	if (bUseInteractionBlock)
	{
		return EDreamPopupOutsideClick::Consume;
	}
	// A dropdown's list is a menu, as SComboBox's is a menu on the Slate menu stack, and follows the project's menus.
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	return Settings != nullptr && Settings->bMenusConsumeOutsideClick ? EDreamPopupOutsideClick::Consume : EDreamPopupOutsideClick::PassThrough;
}
bool UUIDropdown::PushListToPopupLayer()
{
	UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this);
	UDreamWidget* List = ListRoot.Get();
	if (Layer == nullptr || !IsValid(List))
	{
		return false;
	}
	FDreamPopupParams Params;
	Params.Popup = List;
	Params.Opener = GetWidget();
	Params.UserIndex = ResolveListUserIndex();
	ListUserIndex = Params.UserIndex;
	PendingTabCommit = INDEX_NONE;
	Params.OutsideClick = ResolveOutsideClick();
	// Tab leaves an open list rather than walking its rows, as it leaves an HTML select: the list closes -- choosing the row
	// the player is on, while bTabCommitsHighlightedRow -- the focus comes back to the face, and the Tab goes on from there.
	Params.TabBehavior = EDreamPopupTabBehavior::CloseAndContinue;
	// Into the list, onto the selected row, as SComboBox's list takes the focus when it opens with its
	// selection highlighted -- so the first stick press moves from the choice the player already has.
	Params.bFocusOnOpen = true;
	const UUIDropdownItemComponent* SelectedItem = CreatedItemArray.IsValidIndex(Value) ? CreatedItemArray[Value].Get() : nullptr;
	UDreamWidget* SelectedRow = SelectedItem != nullptr ? SelectedItem->GetWidget() : nullptr;
	Params.InitialFocus = IsValid(SelectedRow) && SelectedRow->GetComponent<UUISelectable>() != nullptr ? SelectedRow : nullptr;
	// Kept up when it closes, to fade out where the player saw it; PutListAway brings it home.
	Params.bRestoreOnDismiss = false;
	Params.Place = ListPlacement;
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 ShowSerial = ListOperationSerial;
	Params.OnDismissed = FDreamPopupDismissedDelegate::CreateWeakLambda(this, [WeakThis, ShowSerial](UDreamWidget* InList, EDreamPopupDismissReason InReason)
	{
		if (UUIDropdown* Dropdown = WeakThis.Get(); Dropdown != nullptr && Dropdown->ListOperationSerial == ShowSerial && Dropdown->bIsShow)
		{
			Dropdown->HandleListDismissed(InList, InReason);
		}
	});
	Params.OnClosing = FDreamPopupDismissedDelegate::CreateWeakLambda(this, [WeakThis, ShowSerial](UDreamWidget* InList, EDreamPopupDismissReason InReason)
	{
		if (UUIDropdown* Dropdown = WeakThis.Get(); Dropdown != nullptr && Dropdown->ListOperationSerial == ShowSerial && Dropdown->bIsShow)
		{
			Dropdown->HandleListClosing(InList, InReason);
		}
	});
	return Layer->Push(Params);
}
void UUIDropdown::HandleListClosing(UDreamWidget* InList, EDreamPopupDismissReason InReason)
{
	// Now or never: the layer gives the player's focus back to the face as soon as this returns, and the row they were on
	// is known only by where their focus is.
	PendingTabCommit = InReason == EDreamPopupDismissReason::Tab && bTabCommitsHighlightedRow ? FindHighlightedRow() : INDEX_NONE;
}
int32 UUIDropdown::FindHighlightedRow() const
{
	const UDreamUIInputServices* Services = UDreamUIInputServices::Get(this);
	const UDreamWidget* Focus = Services != nullptr ? Services->GetFocusedWidget(ListUserIndex) : nullptr;
	if (!IsValid(Focus))
	{
		return INDEX_NONE;
	}
	// A row is built per option, in option order, so a row's place among them is its option's index.
	for (int32 RowIndex = 0; RowIndex < CreatedItemArray.Num(); ++RowIndex)
	{
		const UUIDropdownItemComponent* Item = CreatedItemArray[RowIndex].Get();
		const UDreamWidget* Row = Item != nullptr ? Item->GetWidget() : nullptr;
		if (IsValid(Row) && (Focus == Row || Focus->IsChildOf(Row)))
		{
			return RowIndex;
		}
	}
	return INDEX_NONE;
}
void UUIDropdown::HandleListDismissed(UDreamWidget* InList, EDreamPopupDismissReason InReason)
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 ShowSerial = ListOperationSerial;
	bListOnPopupLayer = false;
	const int32 TabCommit = PendingTabCommit;
	PendingTabCommit = INDEX_NONE;
	if (!bIsShow)
	{
		// This dropdown's own close, already under way.
		return;
	}
	if (InReason == EDreamPopupDismissReason::WorldTeardown)
	{
		// The world comes down with the list in it: nothing to fade, nothing to put back, nobody left to tell.
		++ListOperationSerial;
		bIsShow = false;
		return;
	}
	if (InList == nullptr || !ListRoot.IsValid())
	{
		// The list was destroyed while it was open: nothing is left to fade, bring home or put to sleep, and the close
		// is all there is to announce.
		++ListOperationSerial;
		bIsShow = false;
		OnListVisibilityChangedCPP.Broadcast(false);
		return;
	}
	if (InReason == EDreamPopupDismissReason::Tab && Options.IsValidIndex(TabCommit))
	{
		// Tab chose the row the player was on (HandleListClosing noted it), in a click's order: the choice, then the close.
		// The focus is on the face already, and the Tab goes on from there once this returns.
		SetValue(TabCommit, true);
		if (!WeakThis.IsValid() || ListOperationSerial != ShowSerial || !bIsShow)
		{
			// A handler of the choice closed the list itself.
			return;
		}
		if (!ListRoot.IsValid())
		{
			// Or took it away: the close is all there is left to announce, as for a list destroyed while open.
			bIsShow = false;
			OnListVisibilityChangedCPP.Broadcast(false);
			return;
		}
	}
	// Closed from outside -- a press elsewhere, Back, Tab, a menu opened in its place. An opener gone (hidden, put
	// to sleep, disabled) takes its list down at once, as a hidden SMenuAnchor hides its menu.
	CloseList(InReason != EDreamPopupDismissReason::OpenerLost);
}
int32 UUIDropdown::ResolveListUserIndex() const
{
	if (OpeningUserIndex != INDEX_NONE)
	{
		return OpeningUserIndex;
	}
	// Opened from code: the player whose focus is on the face, which is who a confirm or a Show bound to one came from.
	const UDreamWidget* Face = GetWidget();
	if (const UDreamUIInputServices* Services = UDreamUIInputServices::Get(this); Services != nullptr && IsValid(Face))
	{
		TArray<int32> UserIndices;
		Services->GetUserIndices(UserIndices);
		for (const int32 UserIndex : UserIndices)
		{
			if (Services->GetFocusedWidget(UserIndex) == Face)
			{
				return UserIndex;
			}
		}
	}
	return IsValid(Face) ? Face->GetOwningPlayerIndex() : 0;
}
void UUIDropdown::CreateListItems()
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 ShowSerial = ListOperationSerial;
	const uint64 RebuildSerial = ListRebuildSerial;
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true) || !ItemTemplate.IsValid())return;
	const TWeakObjectPtr<UDreamWidget> WeakTemplate(ItemTemplate->GetWidget());
	if (!WeakTemplate.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ItemTemplate must be a DreamWidget!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	const TWeakObjectPtr<UDreamWidget> WeakContent(WeakTemplate->GetParent());
	const auto IsCurrentBuild = [WeakThis, ShowSerial, RebuildSerial, WeakTemplate, WeakContent]()
	{
		return IsCurrentListOperation(WeakThis, ShowSerial, true) && WeakThis->ListRebuildSerial == RebuildSerial
			&& WeakTemplate.IsValid() && WeakContent.IsValid() && WeakThis->ItemTemplate.IsValid()
			&& WeakThis->ItemTemplate->GetWidget() == WeakTemplate.Get();
	};
	bool bCompleted = false;
	// A canceled build still hides its own template. A nested rebuild has already taken over
	// the template and must not have its state changed by this older scope on the way out.
	ON_SCOPE_EXIT
	{
		if (UUIDropdown* Dropdown = WeakThis.Get(); Dropdown != nullptr && Dropdown->ListRebuildSerial == RebuildSerial)
		{
			if (!bCompleted)Dropdown->bNeedRecreate = true;
			if (UDreamWidget* Template = WeakTemplate.Get())Template->SetWidgetActive(false);
		}
	};
	if (!IsCurrentBuild())return;
	WeakTemplate->SetWidgetActive(true);
	if (!IsCurrentBuild())return;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const FUIDropdownOptionData Option = Options[Index];
		UDreamWidget* CopiedItemWidget = UDreamUIBPLibrary::DuplicateWidget(GetWorld(), WeakTemplate.Get(), WeakContent.Get());
		if (!IsCurrentBuild())return;
		const TWeakObjectPtr<UDreamWidget> WeakRow(CopiedItemWidget);
		if (!WeakRow.IsValid())return;
		CopiedItemWidget->SetDisplayName(FString::Printf(TEXT("Item_%d"), Index));
		UUIDropdownItemComponent* Item = CopiedItemWidget->GetComponent<UUIDropdownItemComponent>();
		const TWeakObjectPtr<UUIDropdownItemComponent> WeakItem(Item);
		if (!WeakItem.IsValid())return;
		// Publish before any per-row callback: a nested Show must be able to destroy every
		// row of the old build, including the one whose generation handler it is inside.
		CreatedItemArray.Add(Item);
		Item->Init(Index, Option, [WeakThis, Index]()
		{
			if (UUIDropdown* Dropdown = WeakThis.Get())Dropdown->OnSelectItem(Index);
		});
		if (!IsCurrentBuild() || !WeakRow.IsValid() || !WeakItem.IsValid())return;
		Item->SetSelectionState(Index == Value);
		if (!IsCurrentBuild() || !WeakRow.IsValid() || !WeakItem.IsValid())return;
		OnSetItemCustomDataFunction.ExecuteIfBound(Index, Item, CopiedItemWidget);
		if (!IsCurrentBuild() || !WeakRow.IsValid() || !WeakItem.IsValid())return;
		if (bNeedRecreate)break;
	}
	WeakTemplate->SetWidgetActive(false);
	if (!IsCurrentBuild())return;
	UDreamWidget::RebuildLayoutImmediately(WeakContent.Get());
	if (!IsCurrentBuild())return;
	float HeightOffset = 0;
	if (UDreamWidget* ViewportWidget = WeakContent->GetParent())
	{
		HeightOffset = ListRoot->GetHeight() - ViewportWidget->GetHeight();
	}
	if (WeakContent->GetHeight() + HeightOffset < MaxHeight)
	{
		ListRoot->SetHeight(WeakContent->GetHeight() + HeightOffset);
	}
	else if (WeakContent->GetHeight() + HeightOffset > MaxHeight)
	{
		ListRoot->SetHeight(MaxHeight + HeightOffset);
	}
	bCompleted = true;
}
void UUIDropdown::RecreateListItems()
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	const uint64 ShowSerial = ListOperationSerial;
	if (!IsCurrentListOperation(WeakThis, ShowSerial, true))return;
	// Options changed during this Show wait for a second pass. Hide followed by a new Show
	// belongs to another operation and may rebuild immediately; the old pass then stops.
	if (bRecreatingListItems && RecreatingOperationSerial == ShowSerial)
	{
		bNeedRecreate = true;
		return;
	}
	const uint64 RebuildSerial = ++ListRebuildSerial;
	bRecreatingListItems = true;
	RecreatingOperationSerial = ShowSerial;
	ON_SCOPE_EXIT
	{
		// A raw TGuardValue would write through a collected component, or clear a newer
		// nested rebuild's flag. Only the build that owns this scope may finish it.
		if (UUIDropdown* Dropdown = WeakThis.Get(); Dropdown != nullptr && Dropdown->ListRebuildSerial == RebuildSerial)
		{
			Dropdown->bRecreatingListItems = false;
			if (!IsCurrentListOperation(WeakThis, ShowSerial, true))Dropdown->bNeedRecreate = true;
		}
	};
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		bNeedRecreate = false;
		const TArray<TWeakObjectPtr<UUIDropdownItemComponent>> OldItems = MoveTemp(CreatedItemArray);
		CreatedItemArray.Reset();
		for (const TWeakObjectPtr<UUIDropdownItemComponent>& Item : OldItems)
		{
			UDreamWidget* ItemWidget = Item.IsValid() ? Item->GetWidget() : nullptr;
			if (IsValid(ItemWidget))ItemWidget->DestroyWidget();
		}
		if (!IsCurrentListOperation(WeakThis, ShowSerial, true) || ListRebuildSerial != RebuildSerial)return;
		CreateListItems();
		if (!IsCurrentListOperation(WeakThis, ShowSerial, true) || ListRebuildSerial != RebuildSerial)return;
		if (!bNeedRecreate)break;
	}
}
bool UUIDropdown::IsPartOfDropdown(const UDreamWidget* InWidget)const
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	const UDreamWidget* ThisWidget = GetWidget();
	if (InWidget == ThisWidget || (ThisWidget != nullptr && InWidget->IsChildOf(ThisWidget)))
	{
		return true;
	}
	// Asked separately, because the list is a child of this widget only until a control lifts it to a
	// popup layer -- and then its rows are under the screen root, where a membership test against this
	// widget alone reads a row as somewhere else entirely.
	const UDreamWidget* List = ListRoot.Get();
	return List != nullptr && (InWidget == List || InWidget->IsChildOf(List));
}
FUIDropdownOptionData UUIDropdown::GetOption(int index)const
{
	//IsValidIndex, not just the upper bound: a negative index indexed the array backwards
	if (!Options.IsValidIndex(index))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d index: %d out of range: %d!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, index, Options.Num());
		return FUIDropdownOptionData();
	}
	return Options[index];
}
FUIDropdownOptionData UUIDropdown::GetCurrentOption()const
{
	// Value is documented as -1 for "none selected", which is a state this asked about by indexing
	// Options with it. The empty option data IS the answer for an unselected dropdown.
	if (!Options.IsValidIndex(Value))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s]Value: %d out of range: %d!"), ANSI_TO_TCHAR(__FUNCTION__), Value, Options.Num());
		return FUIDropdownOptionData();
	}
	return Options[Value];
}
void UUIDropdown::SetValue(int InValue, bool FireEvent)
{
	if (Value != InValue)
	{
		Value = InValue;
		if (FireEvent)
		{
			OnValueChangedCPP.Broadcast(Value);
			OnValueChangedBP.Broadcast(Value);
			OnValueChanged.FireEvent(Value);
		}
		ApplyValueToVisual();
	}
}

// Out of line: the header only forward-declares these part types, and a weak-pointer assignment
// needs the complete type.
void UUIDropdownItemComponent::SetText(UDreamText* InText) { Text = InText; }
void UUIDropdownItemComponent::SetImage(UDreamImage* InImage) { Image = InImage; }
void UUIDropdownItemComponent::SetToggle(UUIToggle* InToggle) { Toggle = InToggle; }

void UUIDropdown::SetListRoot(UDreamWidget* InListRoot)
{
	if (ListRoot != InListRoot)
	{
		ListRoot = InListRoot;
		bNeedRecreate = true;
	}
}

void UUIDropdown::SetCaptionText(UDreamText* InCaptionText)
{
	if (CaptionText != InCaptionText)
	{
		CaptionText = InCaptionText;
		ApplyValueToVisual();
	}
}

void UUIDropdown::SetItemTemplate(UUIDropdownItemComponent* InItemTemplate)
{
	if (ItemTemplate != InItemTemplate)
	{
		ItemTemplate = InItemTemplate;
		bNeedRecreate = true;
	}
}

void UUIDropdown::SetValue(int InValue)
{
	SetValue(InValue, true);
}

void UUIDropdown::SetValueWithoutNotify(int InValue)
{
	SetValue(InValue, false);
}

void UUIDropdown::SetVerticalPosition(EUIDropdownVerticalPosition InValue)
{
	if (VerticalPosition != InValue)
	{
		VerticalPosition = InValue;
	}
}
void UUIDropdown::SetHorizontalPosition(EUIDropdownHorizontalPosition InValue)
{
	if (HorizontalPosition != InValue)
	{
		HorizontalPosition = InValue;
	}
}
void UUIDropdown::SetVerticalOverlap(bool newValue)
{
	if (VerticalOverlap != newValue)
	{
		VerticalOverlap = newValue;
	}
}
void UUIDropdown::SetOptions(const TArray<FUIDropdownOptionData>& InOptions)
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	bNeedRecreate = true;
	Options = InOptions;
	// At once while the list is up. Marking the rows for the next open left an open list showing the
	// old ones, each still carrying the index it was built with: a click on one past the new end set
	// a value no option has, and the caption went blank. This is also the road an OnOpening handler
	// refreshing the options takes, because Show announces the open after the rows are built.
	if (bIsShow && ItemTemplate.IsValid() && ListRoot.IsValid())
	{
		RecreateListItems();
	}
	if (!WeakThis.IsValid() || !IsValid(WeakThis->GetWidget()))return;
	ApplyValueToVisual();
}
void UUIDropdown::AddOptions(const TArray<FUIDropdownOptionData>& InOptions)
{
	const TWeakObjectPtr<UUIDropdown> WeakThis(this);
	bNeedRecreate = true;
	// Reserve, not SetNumUninitialized. FUIDropdownOptionData holds an FText and an
	// FDreamUIImageBrush, so growing the array without constructing anything left N raw-garbage
	// entries in front of the ones Add() then appended -- garbage that gets destructed (running
	// FText's destructor over whatever was on the heap) at the next reallocation, and that every
	// reader of Options walks straight into. Reserve buys the same capacity and adds no elements.
	Options.Reserve(Options.Num() + InOptions.Num());
	for (int i = 0; i < InOptions.Num(); i++)
	{
		Options.Add(InOptions[i]);
	}
	// An open list gains its rows now, for SetOptions' reason.
	if (bIsShow && ItemTemplate.IsValid() && ListRoot.IsValid())
	{
		RecreateListItems();
	}
	if (!WeakThis.IsValid() || !IsValid(WeakThis->GetWidget()))return;
	ApplyValueToVisual();
}
void UUIDropdown::SetUseInteractionBlock(bool InValue)
{
	if (bUseInteractionBlock != InValue)
	{
		//was assigned a literal true, so this setter could only ever turn the blocker ON
		bUseInteractionBlock = InValue;
		// An open list answers the next press by the new setting, not only the next list.
		if (bListOnPopupLayer)
		{
			if (UDreamUIPopupLayer* Layer = UDreamUIPopupLayer::Get(this))
			{
				Layer->SetOutsideClick(ListRoot.Get(), ResolveOutsideClick());
			}
		}
	}
}

void UUIDropdown::OnSelectItem(int Index)
{
	// Only an option that exists. A row carries the index it was built with, and a value past the end
	// of Options is a selection the caption can only show as blank -- announced to every listener as a
	// choice all the same.
	if (Options.IsValidIndex(Index))
	{
		SetValue(Index, true);
	}
	Hide();
}
void UUIDropdown::ApplyValueToVisual()
{
	/*
	 * An invalid index is a REAL state, not a call to ignore: Value is -1 for "nothing chosen" (the
	 * control says so in its header) and SetOptions with an empty array is how a filter that matched
	 * nothing reports itself. Returning early here left the caption showing the last option that had
	 * been chosen -- a word naming a choice the dropdown could no longer make -- and left every
	 * created row still drawn as the selected one.
	 *
	 * The image is cleared alongside the text because they are one caption: an icon outliving its
	 * label is the same lie with a picture.
	 */
	if (!Options.IsValidIndex(Value))
	{
		if (CaptionText.IsValid())
		{
			CaptionText->SetText(FText::GetEmpty());
		}
		if (CaptionImage.IsValid())
		{
			CaptionImage->SetBrush(FDreamUIImageBrush());
		}
		for (auto& Script : CreatedItemArray)
		{
			if (Script.IsValid())
			{
				Script->SetSelectionState(false);
			}
		}
		return;
	}

	if (CaptionText.IsValid())
	{
		CaptionText->SetText(Options[Value].Text);
	}
	if (CaptionImage.IsValid())
	{
		CaptionImage->SetBrush(Options[Value].ImageBrush);
	}

	//apply to options
	for (int i = 0; i < Options.Num() && i < CreatedItemArray.Num(); i++)
	{
		auto script = CreatedItemArray[i];
		if (script.IsValid())
		{
			script->SetSelectionState(i == Value);
		}
	}
}
bool UUIDropdown::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	if (!AcceptsPointerButton(EventData))
	{
		// A mouse button this dropdown was told not to answer opens nothing, and is passed on. Every
		// button counts by default here, as it always has; see UUISelectable::AcceptedMouseButtons.
		return true;
	}
	if (!IsInteractable())
	{
		// Disabled means disabled. A dropdown switched off through this behaviour is still hit-tested
		// (see bInteractable), so its click arrives here, and the base's refusal of the press does not
		// reach the click -- the test UUIButton::OnPointerClick makes, made here too.
		return AllowEventBubbleUp;
	}
	// The player whose click it is owns the list: their focus goes into it, their Back and their presses close it.
	OpeningUserIndex = IsValid(EventData) ? EventData->UserIndex : INDEX_NONE;
	Show();
	OpeningUserIndex = INDEX_NONE;
	return AllowEventBubbleUp;
}
bool UUIDropdown::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	// The base first: it is what stops this dropdown being drawn focused once focus has gone elsewhere,
	// and leaving it out kept the face wearing its focused look for good.
	Super::OnPointerDeselect_Implementation(EventData);
	// Focus moving INTO the list is not focus leaving the dropdown. A pad moving down from the face onto
	// the first row used to read as leaving whenever the list had been lifted to a popup layer, and
	// closed the list it was moving into.
	if (IsValid(EventData->SelectedComponent) && !IsPartOfDropdown(EventData->SelectedComponent))
	{
		Hide();
	}
	return AllowEventBubbleUp;
}

void UUIDropdown::SetItemCustomDataFunction(const FUIDropdownComponentDelegate_SetItemCustomData& InFunction)
{
	OnSetItemCustomDataFunction = InFunction;
}
void UUIDropdown::SetItemCustomDataFunction(const TFunction<void(int, class UUIDropdownItemComponent*, UDreamWidget*)>& InFunction)
{
	OnSetItemCustomDataFunction.BindLambda(InFunction);
}
void UUIDropdown::SetItemCustomDataFunction(const FUIDropdownComponentDynamicDelegate_SetItemCustomData& InFunction)
{
	OnSetItemCustomDataFunction.BindLambda([InFunction](int InItemIndex, UUIDropdownItemComponent* InItemScript, UDreamWidget* InItemWidget) {
		if (InFunction.IsBound())
		{
			InFunction.Execute(InItemIndex, InItemScript, InItemWidget);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d OnSetItemCustomDataFunction function not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
		});
}
void UUIDropdown::ClearItemCustomDataFunction()
{
	OnSetItemCustomDataFunction = FUIDropdownComponentDelegate_SetItemCustomData();
}


#include "Interaction/UIToggle.h"

UUIDropdownItemComponent::UUIDropdownItemComponent()
{
}

void UUIDropdownItemComponent::Awake()
{
	Super::Awake();
	this->SetCanExecuteTick(false);
}

void UUIDropdownItemComponent::Init(int32 Index, const FUIDropdownOptionData& Data, const TFunction<void()>& OnSelect)
{
	if (Text.IsValid())
	{
		Text->SetText(Data.Text);
	}
	if (Image.IsValid())
	{
		Image->SetBrush(Data.ImageBrush);
	}
	if (Toggle.IsValid())
	{
		Toggle->GetOnValueChangedEvent().AddWeakLambda(this, [OnSelect](bool select){
			OnSelect();
		});
	}
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		OnSelectDynamic.BindDynamic(this, &UUIDropdownItemComponent::DynamicDelegate_OnSelect);
		OnSelectCPP.BindLambda(OnSelect);
		ReceiveInit(Index, Data, OnSelectDynamic);
	}
}
void UUIDropdownItemComponent::SetSelectionState(const bool& InSelect)
{
	if (Toggle.IsValid())
	{
		Toggle->SetValueWithoutNotify(InSelect);
	}
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveSetSelectionState(InSelect);
	}
}
bool UUIDropdownItemComponent::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	return false;
}
UUIToggle* UUIDropdownItemComponent::GetToggle()const
{
	return Toggle.Get();
}


