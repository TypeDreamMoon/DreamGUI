// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIDropdown.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamUIBPLibrary.h"
#include "Core/DreamUIClipData.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Event/DreamPointerEventData.h"



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

void UUIDropdown::Show()
{
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
	bIsShow = true;
	if (ShowOrHideTweener.IsValid())
	{
		ShowOrHideTweener->Kill();
	}
	// A list still fading out from the last close is taken over where it is: home under the face again
	// to be placed from scratch below, and answering the pointer and the pad again.
	ReturnListHome();
	SetListInert(false);

	//show list
	ListRoot->SetWidgetActive(true);
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
	if (!bIsShow)
	{
		// A handler of the opening closed it again.
		return;
	}
	// Then up on the popup layer, the UMG menu-stack arrangement: lifted to the screen root so no ancestor clips
	// it, focus moved onto the selected row, closed by a press outside it, by Back, or by this face going away.
	bListOnPopupLayer = PushListToPopupLayer();
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
	if (!ListRoot.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ListRoot is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (!bIsShow)return;
	bIsShow = false;
	UDreamWidget* List = ListRoot.Get();
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
	OnListVisibilityChangedCPP.Broadcast(false);
	if (ShowOrHideTweener.IsValid())
	{
		ShowOrHideTweener->Kill();
	}
	ShowOrHideTweener = nullptr;
	if (!IsValid(List) || bIsShow)
	{
		// A handler of the close destroyed the list, or opened it again.
		return;
	}
	// The fade plays where the player saw the list -- still lifted, when it went up -- and nothing in it answers
	// meanwhile.
	SetListInert(true);

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
		TWeakObjectPtr<UUIDropdown> WeakThis(this);
		HideTweener->OnComplete(FSimpleDelegate::CreateWeakLambda(FadingList, [FadingList, WeakThis]
		{
			if (UUIDropdown* Dropdown = WeakThis.Get())
			{
				if (!Dropdown->bIsShow)
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
	ShowOrHideTweener = nullptr;
	if (UDreamWidget* List = ListRoot.Get(); IsValid(List))
	{
		// Home first, then asleep: a list put to sleep while still lifted would be an inactive widget hanging off
		// the screen root that nothing would ever come back for.
		ReturnListHome();
		List->SetWidgetActive(false);
	}
	SetListInert(false);
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
	Params.OutsideClick = bUseInteractionBlock ? EDreamPopupOutsideClick::Consume : EDreamPopupOutsideClick::PassThrough;
	// Into the list, onto the selected row, as SComboBox's list takes the focus when it opens with its
	// selection highlighted -- so the first stick press moves from the choice the player already has.
	Params.bFocusOnOpen = true;
	const UUIDropdownItemComponent* SelectedItem = CreatedItemArray.IsValidIndex(Value) ? CreatedItemArray[Value].Get() : nullptr;
	UDreamWidget* SelectedRow = SelectedItem != nullptr ? SelectedItem->GetWidget() : nullptr;
	Params.InitialFocus = IsValid(SelectedRow) && SelectedRow->GetComponent<UUISelectable>() != nullptr ? SelectedRow : nullptr;
	// Kept up when it closes, to fade out where the player saw it; PutListAway brings it home.
	Params.bRestoreOnDismiss = false;
	Params.Place = ListPlacement;
	Params.OnDismissed = FDreamPopupDismissedDelegate::CreateUObject(this, &UUIDropdown::HandleListDismissed);
	return Layer->Push(Params);
}
void UUIDropdown::HandleListDismissed(UDreamWidget* InList, EDreamPopupDismissReason InReason)
{
	bListOnPopupLayer = false;
	if (!bIsShow)
	{
		// This dropdown's own close, already under way.
		return;
	}
	if (InReason == EDreamPopupDismissReason::WorldTeardown)
	{
		// The world comes down with the list in it: nothing to fade, nothing to put back, nobody left to tell.
		bIsShow = false;
		return;
	}
	if (InList == nullptr || !ListRoot.IsValid())
	{
		// The list was destroyed while it was open: nothing is left to fade, bring home or put to sleep, and the close
		// is all there is to announce.
		bIsShow = false;
		OnListVisibilityChangedCPP.Broadcast(false);
		return;
	}
	// Closed from outside -- a press elsewhere, Back, a menu opened in its place. An opener gone (hidden, put
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
	auto ItemTemplateWidget = ItemTemplate->GetWidget();
	if (!IsValid(ItemTemplateWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d ItemTemplate must be a DreamWidget!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	ItemTemplateWidget->SetWidgetActive(true);
	auto ScrollViewContentWidget = ItemTemplateWidget->GetParent();
	// Against the options as they are at each step: a row's handler below can change them.
	for (int i = 0; i < Options.Num(); i++)
	{
		auto CopiedItemWidget = UDreamUIBPLibrary::DuplicateWidget(this->GetOuter()->GetWorld(), ItemTemplateWidget, ScrollViewContentWidget);
		CopiedItemWidget->SetDisplayName(FString::Printf(TEXT("Item_%d"), i));
		auto script = CopiedItemWidget->GetComponent<UUIDropdownItemComponent>();
		// Weakly: a row lives as long as its list, and the list can outlive this component -- lifted to
		// a popup layer, or kept outside this widget by a hand-wired dropdown -- so a row clicked after
		// the dropdown went away must find nobody to call rather than a destroyed object.
		TWeakObjectPtr<UUIDropdown> WeakThis(this);
		script->Init(i, Options[i], [WeakThis, i]() {
			if (UUIDropdown* Dropdown = WeakThis.Get())
			{
				Dropdown->OnSelectItem(i);
			}
			});
		script->SetSelectionState(i == Value);
		OnSetItemCustomDataFunction.ExecuteIfBound(i, script, CopiedItemWidget);
		CreatedItemArray.Add(script);
	}
	ItemTemplateWidget->SetWidgetActive(false);

	UDreamWidget::RebuildLayoutImmediately(ScrollViewContentWidget);
	float HeightOffset = 0;
	if (auto ViewportWidget = ScrollViewContentWidget->GetParent())
	{
		HeightOffset = ListRoot->GetHeight() - ViewportWidget->GetHeight();
	}
	//if content is larger smaller than MaxHeight, then make the ListRoot smaller too
	if (ScrollViewContentWidget->GetHeight() + HeightOffset < MaxHeight)
	{
		ListRoot->SetHeight(ScrollViewContentWidget->GetHeight() + HeightOffset);
	}
	//if content is bigger than MaxHeight, then make the ListRoot as MaxHeight, so the scroll-view will work
	else if (ScrollViewContentWidget->GetHeight() + HeightOffset > MaxHeight)
	{
		ListRoot->SetHeight(MaxHeight + HeightOffset);
	}
}
void UUIDropdown::RecreateListItems()
{
	// A row's custom-data handler (UDreamDropdown's OnItemGenerated) can change the options, which asks for a rebuild
	// while this one is still building rows. Rebuilding inside it read past the end of options that had shrunk, and a
	// handler that always pushes rebuilt for ever; asked from inside, the rebuild waits and runs once this one is done
	// -- once: a handler that changes the options every time leaves them for the next open.
	if (bRecreatingListItems)
	{
		bNeedRecreate = true;
		return;
	}
	TGuardValue<bool> Recreating(bRecreatingListItems, true);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		bNeedRecreate = false;
		for (const TWeakObjectPtr<UUIDropdownItemComponent>& Item : CreatedItemArray)
		{
			// The entries are weak, and a row can be gone before the list is rebuilt -- destroyed by whoever
			// owned the list, or with the list itself. A row that is gone has nothing left to destroy.
			UDreamWidget* ItemWidget = Item.IsValid() ? Item->GetWidget() : nullptr;
			if (IsValid(ItemWidget))
			{
				ItemWidget->DestroyWidget();
			}
		}
		CreatedItemArray.Reset();
		CreateListItems();
		if (!bNeedRecreate)
		{
			break;
		}
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
	ApplyValueToVisual();
}
void UUIDropdown::AddOptions(const TArray<FUIDropdownOptionData>& InOptions)
{
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
				Layer->SetOutsideClick(ListRoot.Get(),
					bUseInteractionBlock ? EDreamPopupOutsideClick::Consume : EDreamPopupOutsideClick::PassThrough);
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


