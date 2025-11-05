// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIDropdownComponent.h"
#include "LGUI.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexCanvas.h"
#include "LexUIBPLibrary.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexText.h"
#include "Interaction/UIButtonComponent.h"
#if WITH_EDITOR
#include "Utils/LexUIUtils.h"
#endif



UUIDropdownComponent::UUIDropdownComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUIDropdownComponent::Awake()
{
	Super::Awake();
	if (ListRoot.IsValid())
	{
		ListRoot->SetWidgetActive(false);
		ListRoot->SetRenderOpacity(0);
		MaxHeight = ListRoot->GetHeight();
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
#if WITH_EDITOR
void UUIDropdownComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (Options.Num() > 0)
	{
		auto tempValue = FMath::Clamp(Value, 0, Options.Num() - 1);
		if (CaptionText.IsValid())
		{
			CaptionText->SetText(Options[tempValue].Text);
			FLexUIUtils::NotifyPropertyChanged(CaptionText.Get(), ULexText::GetPropertyName_Text());
		}
		if (CaptionImage.IsValid())
		{
			CaptionImage->SetBrush(Options[tempValue].ImageBrush);
			FLexUIUtils::NotifyPropertyChanged(CaptionText.Get(), ULexImage::GetPropertyName_Brush());
		}
	}
}
#endif

void UUIDropdownComponent::Show()
{
	if (!ListRoot.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d ListRoot is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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

	//create blocker
	if (bUseInteractionBlock)
	{
		CreateBlocker();
	}
	//show list
	ListRoot->SetWidgetActive(true);
	ShowOrHideTweener = ListRoot->RenderOpacityTo(1, 0.3f, 0, ELTweenEase::OutCubic);
	auto canvasOnListRoot = ListRoot->GetOwner()->FindComponentByClass<ULexCanvas>();
	if (!IsValid(canvasOnListRoot))
	{
		canvasOnListRoot = NewObject<ULexCanvas>(ListRoot.Get());
		canvasOnListRoot->RegisterComponent();
	}

	bool sortOrderSet = false;
	if (BlockerActor.IsValid())
	{
		if (auto blockerCanvas = BlockerActor->FindComponentByClass<ULexCanvas>())
		{
			canvasOnListRoot->SetSortOrder(blockerCanvas->GetSortOrder() + 1, true);
			sortOrderSet = true;
		}
	}
	if(!sortOrderSet)
	{
		canvasOnListRoot->SetSortOrderToHighestOfHierarchy(true);
	}
	canvasOnListRoot->SetOverrideSorting(true);

	//create list item as options
	if (!ItemTemplate.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d ItemTemplate is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (bNeedRecreate)
	{
		bNeedRecreate = false;
		for (auto item : CreatedItemArray)
		{
			auto itemActor = item->GetOwner();
			ULexUIBPLibrary::DestroyActorWithHierarchy(itemActor, true);
		}
		CreatedItemArray.Reset();
		//create items
		CreateListItems();
	}

	//set position
	auto tempVerticalPosition = VerticalPosition;
	auto tempHorizontalPosition = HorizontalPosition;
	if (tempVerticalPosition == EUIDropdownVerticalPosition::Automatic
		|| tempHorizontalPosition == EUIDropdownHorizontalPosition::Automatic
		)
	{
		//search up til find clipped canvas, or root canvas
		auto clipUIItem = GetWidget();
		while (true)
		{
			if (clipUIItem->GetClipping() != ELexWidgetClipping::Disabled)
			{
				break;
			}
			else
			{
				auto upperCanvas = clipUIItem->GetUIParent();
				if (!upperCanvas)
				{
					break;
				}
				else
				{
					clipUIItem = upperCanvas;
				}
			}
		}

		FTransform selfToClipSpaceTf;
		auto inverseClipSpaceTf = clipUIItem->GetComponentTransform().Inverse();
		FTransform::Multiply(&selfToClipSpaceTf, &GetWidget()->GetComponentTransform(), &inverseClipSpaceTf);
		if (tempVerticalPosition == EUIDropdownVerticalPosition::Automatic)
		{
			//convert top point position from drop-down's self to root ui space, and tell if it is inside root rect
			FVector listBottomInClipSpace;
			if (VerticalOverlap)
			{
				auto selfTop = GetWidget()->GetLocalSpaceTop();
				auto listBottomInSelfSpace = selfTop - ListRoot->GetHeight();
				listBottomInClipSpace = selfToClipSpaceTf.TransformPosition(FVector(0, 0, listBottomInSelfSpace));
			}
			else
			{
				auto selfBottom = GetWidget()->GetLocalSpaceBottom();
				auto listBottomInSelfSpace = selfBottom - ListRoot->GetHeight();
				listBottomInClipSpace = selfToClipSpaceTf.TransformPosition(FVector(0, 0, listBottomInSelfSpace));
			}
			if (listBottomInClipSpace.Z < clipUIItem->GetLocalSpaceBottom())
			{
				tempVerticalPosition = EUIDropdownVerticalPosition::Top;
			}
			else
			{
				tempVerticalPosition = EUIDropdownVerticalPosition::Bottom;//default is bottom
			}
		}
		if (tempHorizontalPosition == EUIDropdownHorizontalPosition::Automatic)
		{
			auto selfRight = GetWidget()->GetLocalSpaceRight();
			auto listRightInCanvasSpace = selfToClipSpaceTf.TransformPosition(FVector(0, selfRight + ListRoot->GetWidth(), 0));
			if (listRightInCanvasSpace.Y > clipUIItem->GetLocalSpaceRight())
			{
				tempHorizontalPosition = EUIDropdownHorizontalPosition::Left;
			}
			else
			{
				tempHorizontalPosition = EUIDropdownHorizontalPosition::Right;//default is right
			}
		}
	}

	FVector2D pivot(0.5f, 0);
	switch (tempVerticalPosition)
	{
	case EUIDropdownVerticalPosition::Top:
	{
		pivot.Y = 0.0f;
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
		pivot.Y = 0.5f;
		ListRoot->SetVerticalAnchorMinMax(FVector2D(0.5f, 0.5f), true);
	}break;
	case EUIDropdownVerticalPosition::Bottom:
	{
		pivot.Y = 1.0f;
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

	switch (tempHorizontalPosition)
	{
	case EUIDropdownHorizontalPosition::Left:
	{
		pivot.X = 1.0f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(0.0f, 0.0f), true);
	}break;
	case EUIDropdownHorizontalPosition::Center:
	{
		pivot.X = 0.5f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(0.5f, 0.5f), true);
	}break;
	case EUIDropdownHorizontalPosition::Right:
	{
		pivot.X = 0.0f;
		ListRoot->SetHorizontalAnchorMinMax(FVector2D(1.0f, 1.0f), true);
	}break;
	}
	ListRoot->SetHorizontalAnchoredPosition(0);

	ListRoot->SetPivot(pivot);
}
void UUIDropdownComponent::Hide()
{
	if (!ListRoot.IsValid())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d ListRoot is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (!bIsShow)return;
	bIsShow = false;
	if (ShowOrHideTweener.IsValid())
	{
		ShowOrHideTweener->Kill();
	}
	
	ShowOrHideTweener = ListRoot->RenderOpacityTo(0, 0.3f, 0, ELTweenEase::InCubic)->OnComplete(
		FSimpleDelegate::CreateWeakLambda(ListRoot.Get(), [=, this] {
		ListRoot->SetWidgetActive(false);
		}));

	if (BlockerActor.IsValid())
	{
		BlockerActor->Destroy();
		BlockerActor = nullptr;
	}
}
void UUIDropdownComponent::CreateBlocker()
{
	auto blocker = this->GetWorld()->SpawnActor<ALexWidgetActor>();
#if WITH_EDITOR
	blocker->SetActorLabel(TEXT("UIDropdown_Blocker"));
#endif
	auto blockerUIItem = blocker->GetLexWidget();
	blockerUIItem->AttachToComponent(this->GetWidget()->GetRootCanvas()->GetLexWidget(), FAttachmentTransformRules::KeepRelativeTransform);
	blockerUIItem->SetSizeDelta(FVector2D::ZeroVector);
	blockerUIItem->SetAnchorMin(FVector2D(0.0f, 0.0f));
	blockerUIItem->SetAnchorMax(FVector2D(1.0f, 1.0f));
	auto blockerCanvas = NewObject<ULexCanvas>(blocker);
	blockerCanvas->RegisterComponent();
	blocker->AddInstanceComponent(blockerCanvas);
	blockerCanvas->SetOverrideSorting(true);
	blockerCanvas->SetSortOrderToHighestOfHierarchy();
	blockerCanvas->SetTraceChannel(this->GetWidget()->GetRootCanvas()->GetTraceChannel());
	auto blockerButton = NewObject<UUIButtonComponent>(blocker);
	blockerButton->RegisterComponent();
	blocker->AddInstanceComponent(blockerButton);
	blockerButton->GetOnClickEvent().AddWeakLambda(this, [this] {
		this->Hide();
		});
	BlockerActor = blocker;
}
void UUIDropdownComponent::CreateListItems()
{
	auto ItemTemplateWidget = ItemTemplate->GetWidget();
	if (!IsValid(ItemTemplateWidget))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d ItemTemplate must be a UIItem!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	ItemTemplateWidget->SetWidgetActive(true);
	auto ScrollViewContentWidget = ItemTemplateWidget->GetUIParent();
	for (int i = 0, count = Options.Num(); i < count; i++)
	{
		auto copiedItemActor = ULexUIBPLibrary::DuplicateActor(ItemTemplate->GetOwner(), ScrollViewContentWidget);
#if WITH_EDITOR
		copiedItemActor->SetActorLabel(FString::Printf(TEXT("Item_%d"), i));
#endif
		auto script = copiedItemActor->FindComponentByClass<UUIDropdownItemComponent>();
		int index = i;
		script->Init(i, Options[i], [=, this]() {
			this->OnSelectItem(index);
			});
		script->SetSelectionState(i == Value);
		OnSetItemCustomDataFunction.ExecuteIfBound(i, script, copiedItemActor);
		CreatedItemArray.Add(script);
	}
	ItemTemplateWidget->SetWidgetActive(false);
	// if (auto contentLayout = contentUIItem->GetOwner()->FindComponentByClass<UUILayoutBase>())
	// {
	// 	contentLayout->OnRebuildLayout();
	// }
	float heightOffset = 0;
	if (auto viewportUIItem = ScrollViewContentWidget->GetUIParent())
	{
		heightOffset = ListRoot->GetHeight() - viewportUIItem->GetHeight();
	}
	//if content is larger smaller than MaxHeight, then make the ListRoot smaller too
	if (ScrollViewContentWidget->GetHeight() + heightOffset < MaxHeight)
	{
		ListRoot->SetHeight(ScrollViewContentWidget->GetHeight() + heightOffset);
	}
	//if content is bigger than MaxHeight, then make the ListRoot as MaxHeight, so the scollview will work
	else if (ScrollViewContentWidget->GetHeight() + heightOffset > MaxHeight)
	{
		ListRoot->SetHeight(MaxHeight + heightOffset);
	}
}
FUIDropdownOptionData UUIDropdownComponent::GetOption(int index)const
{
	if (index >= Options.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s]index: %d out of range: %d!"), ANSI_TO_TCHAR(__FUNCTION__), index, Options.Num());
		return FUIDropdownOptionData();
	}
	return Options[index];
}
FUIDropdownOptionData UUIDropdownComponent::GetCurrentOption()const
{
	if (Value >= Options.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s]Value: %d out of range: %d!"), ANSI_TO_TCHAR(__FUNCTION__), Value, Options.Num());
		return FUIDropdownOptionData();
	}
	return Options[Value];
}
void UUIDropdownComponent::SetValue(int InValue, bool FireEvent)
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
		ApplyValueToUI();
	}
}

void UUIDropdownComponent::SetValue(int InValue)
{
	SetValue(InValue, true);
}

void UUIDropdownComponent::SetValueWithoutNotify(int InValue)
{
	SetValue(InValue, false);
}

void UUIDropdownComponent::SetVerticalPosition(EUIDropdownVerticalPosition InValue)
{
	if (VerticalPosition != InValue)
	{
		VerticalPosition = InValue;
	}
}
void UUIDropdownComponent::SetHorizontalPosition(EUIDropdownHorizontalPosition InValue)
{
	if (HorizontalPosition != InValue)
	{
		HorizontalPosition = InValue;
	}
}
void UUIDropdownComponent::SetVerticalOverlap(bool newValue)
{
	if (VerticalOverlap != newValue)
	{
		VerticalOverlap = newValue;
	}
}
void UUIDropdownComponent::SetOptions(const TArray<FUIDropdownOptionData>& InOptions)
{
	bNeedRecreate = true;
	Options = InOptions;
	ApplyValueToUI();
}
void UUIDropdownComponent::AddOptions(const TArray<FUIDropdownOptionData>& InOptions)
{
	bNeedRecreate = true;
	Options.SetNumUninitialized(Options.Num() + InOptions.Num());
	for (int i = 0; i < InOptions.Num(); i++)
	{
		Options.Add(InOptions[i]);
	}
	ApplyValueToUI();
}
void UUIDropdownComponent::SetUseInteractionBlock(bool InValue)
{
	if (bUseInteractionBlock != InValue)
	{
		bUseInteractionBlock = true;
		if (!bUseInteractionBlock)
		{
			if (BlockerActor.IsValid())
			{
				BlockerActor->Destroy();
				BlockerActor = nullptr;
			}
		}
	}
}

void UUIDropdownComponent::OnSelectItem(int index)
{
	SetValue(index, true);
	Hide();
}
void UUIDropdownComponent::ApplyValueToUI()
{
	if (!Options.IsValidIndex(Value))return;

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
bool UUIDropdownComponent::OnPointerClick_Implementation(ULexPointerEventData* eventData)
{
	Show();
	return AllowEventBubbleUp;
}
bool UUIDropdownComponent::OnPointerDeselect_Implementation(ULexBaseEventData* eventData)
{
	if (IsValid(eventData->SelectedComponent))
	{
		if (!eventData->SelectedComponent->IsAttachedTo(this->GetWidget()))
		{
			Hide();
		}
	}
	return AllowEventBubbleUp;
}

void UUIDropdownComponent::SetItemCustomDataFunction(const FUIDropdownComponentDelegate_SetItemCustomData& InFunction)
{
	OnSetItemCustomDataFunction = InFunction;
}
void UUIDropdownComponent::SetItemCustomDataFunction(const TFunction<void(int, class UUIDropdownItemComponent*, AActor*)>& InFunction)
{
	OnSetItemCustomDataFunction.BindLambda(InFunction);
}
void UUIDropdownComponent::SetItemCustomDataFunction(const FUIDropdownComponentDynamicDelegate_SetItemCustomData& InFunction)
{
	OnSetItemCustomDataFunction.BindLambda([InFunction](int InItemIndex, UUIDropdownItemComponent* InItemScript, AActor* InItemActor) {
		if (InFunction.IsBound())
		{
			InFunction.Execute(InItemIndex, InItemScript, InItemActor);
		}
		else
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d OnSetItemCustomDataFunction function not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
		});
}
void UUIDropdownComponent::ClearItemCustomDataFunction()
{
	OnSetItemCustomDataFunction = FUIDropdownComponentDelegate_SetItemCustomData();
}


#include "Interaction/UIToggleComponent.h"

UUIDropdownItemComponent::UUIDropdownItemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
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
bool UUIDropdownItemComponent::OnPointerClick_Implementation(ULexPointerEventData* eventData)
{
	return false;
}
UUIToggleComponent* UUIDropdownItemComponent::GetToggle()const
{
	return Toggle.Get();
}


