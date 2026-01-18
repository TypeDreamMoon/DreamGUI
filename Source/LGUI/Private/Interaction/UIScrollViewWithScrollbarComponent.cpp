// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIScrollViewWithScrollbarComponent.h"

#include "LTweenBPLibrary.h"
#include "Core/LexUIManager.h"
#include "Interaction/UIScrollbarComponent.h"
#include "PrefabSystem/LexUIPrefabManager.h"


UUIScrollViewWithScrollbarComponent::UUIScrollViewWithScrollbarComponent()
{
	
}

void UUIScrollViewWithScrollbarComponent::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	CheckScrollbarParameter();//Check and register scrollbar event
}

bool UUIScrollViewWithScrollbarComponent::OnPointerDrag_Implementation(ULexPointerEventData* EventData)
{
	return Super::OnPointerDrag_Implementation(EventData);
}
bool UUIScrollViewWithScrollbarComponent::OnPointerScroll_Implementation(ULexPointerEventData* EventData)
{
	return Super::OnPointerScroll_Implementation(EventData);
}
void UUIScrollViewWithScrollbarComponent::UpdateProgress(bool InFireEvent)
{
	Super::UpdateProgress(InFireEvent);
	if (CheckScrollbarParameter())
	{
		if (bAllowHorizontalScroll && HorizontalScrollbarWidget->GetWidgetActiveInHierarchy())
		{
			if (Progress.X > 1.0f)
			{
				HorizontalScrollbar->SetValueAndSize(1.0f, ContentParent->GetWidth() / (Content->GetWidth() + (HorizontalRange.Y - HorizontalRange.X) * (Progress.X - 1.0f)), false);
			}
			else if (Progress.X < 0.0f)
			{
				HorizontalScrollbar->SetValueAndSize(0.0f, ContentParent->GetWidth() / (Content->GetWidth() + (HorizontalRange.Y - HorizontalRange.X) * (0.0f - Progress.X)), false);
			}
			else
			{
				HorizontalScrollbar->SetValueWithoutNotify(Progress.X);
			}
		}
		if (bAllowVerticalScroll && VerticalScrollbarWidget->GetWidgetActiveInHierarchy())
		{
			if (Progress.Y > 1.0f)
			{
				VerticalScrollbar->SetValueAndSize(1.0f, ContentParent->GetHeight() / (Content->GetHeight() + (VerticalRange.Y - VerticalRange.X) * (Progress.Y - 1.0f)), false);
			}
			else if (Progress.Y < 0.0f)
			{
				VerticalScrollbar->SetValueAndSize(0.0f, ContentParent->GetHeight() / (Content->GetHeight() + (VerticalRange.Y - VerticalRange.X) * (0.0f - Progress.Y)), false);
			}
			else
			{
				VerticalScrollbar->SetValueWithoutNotify(Progress.Y);
			}
		}
	}
}
bool UUIScrollViewWithScrollbarComponent::CheckScrollbarParameter()
{
	bool bHorizontalValid = false;
	bool bVerticalValid = false;
	if (Horizontal)
	{
		if (HorizontalScrollbarWidget.IsValid())
		{
			bHorizontalValid = true;
		}
		else
		{
			if (HorizontalScrollbar.IsValid())
			{
				HorizontalScrollbar->GetOnValueChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar);
				HorizontalScrollbarWidget = HorizontalScrollbar->GetWidget();
				bHorizontalValid = true;
			}
		}
	}

	if (Vertical)
	{
		if (VerticalScrollbarWidget.IsValid())
		{
			bVerticalValid = true;
		}
		else
		{
			if (VerticalScrollbar.IsValid())
			{
				VerticalScrollbar->GetOnValueChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar);
				VerticalScrollbarWidget = VerticalScrollbar->GetWidget();
				bVerticalValid = true;
			}
		}
	}

	if (Horizontal && Vertical)
	{
		if (bHorizontalValid && bVerticalValid)
		{
			return true;
		}
	}
	else
	{
		if (Horizontal)
		{
			return bHorizontalValid;
		}
		if (Vertical)
		{
			return bVerticalValid;
		}
	}

	return false;
}
bool UUIScrollViewWithScrollbarComponent::CheckValidHit(USceneComponent* InHitComp)
{
	bool bHitHorizontalScrollbar = HorizontalScrollbarWidget.IsValid() && (InHitComp->IsAttachedTo(HorizontalScrollbarWidget.Get()) || InHitComp == HorizontalScrollbarWidget);
	bool bHitVerticalScrollbar = VerticalScrollbarWidget.IsValid() && (InHitComp->IsAttachedTo(VerticalScrollbarWidget.Get()) || InHitComp == VerticalScrollbarWidget);
	return Super::CheckValidHit(InHitComp)
		&& !bHitHorizontalScrollbar && !bHitVerticalScrollbar;//make sure hit component is not scrollbar
}
void UUIScrollViewWithScrollbarComponent::CalculateHorizontalRange()
{
	Super::CalculateHorizontalRange();
	if (CheckScrollbarParameter())
	{
		auto ParentWidth = ContentParent->GetWidth();
		auto ContentWidth = Content->GetWidth();
		if (ParentWidth >= ContentWidth)
		{
			if (HorizontalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				if (HorizontalScrollbarWidget.IsValid())
				{
					HorizontalScrollbarWidget->SetWidgetActive(false);
				}
			}
		}
		else
		{
			if (HorizontalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				if (HorizontalScrollbarWidget.IsValid())
				{
					HorizontalScrollbarWidget->SetWidgetActive(true);
				}
			}
		}
		if (HorizontalScrollbar.IsValid())
		{
			HorizontalScrollbar->SetValueAndSize(Progress.X, ParentWidth / ContentWidth, false);
		}
	}
}
void UUIScrollViewWithScrollbarComponent::CalculateVerticalRange()
{
	Super::CalculateVerticalRange();
	if (CheckScrollbarParameter())
	{
		auto ParentHeight = ContentParent->GetHeight();
		auto ContentHeight = Content->GetHeight();
		if (ParentHeight >= ContentHeight)
		{
			if (VerticalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				if (VerticalScrollbarWidget.IsValid())
				{
					VerticalScrollbarWidget->SetWidgetActive(false);
				}
			}
		}
		else
		{
			if (VerticalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				if (VerticalScrollbarWidget.IsValid())
				{
					VerticalScrollbarWidget->SetWidgetActive(true);
				}
			}
		}
		if (VerticalScrollbar.IsValid())
		{
			VerticalScrollbar->SetValueAndSize(Progress.Y, ParentHeight / ContentHeight, false);
		}
	}
}

void UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar(float InScrollValue)
{
	if (!Content.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowHorizontalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = Content->GetRelativeLocation();
	Position.Y = FMath::Lerp(HorizontalRange.X, HorizontalRange.Y, 1.0f - InScrollValue);
	Content->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar(float InScrollValue)
{
	if (!Content.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowVerticalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = Content->GetRelativeLocation();
	Position.Z = FMath::Lerp(VerticalRange.X, VerticalRange.Y, InScrollValue);
	Content->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::SetHorizontalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value)
{
	if (HorizontalScrollbarVisibility != value)
	{
		HorizontalScrollbarVisibility = value;
		CalculateHorizontalRange();
	}
}
void UUIScrollViewWithScrollbarComponent::SetVerticalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value)
{
	if (VerticalScrollbarVisibility != value)
	{
		VerticalScrollbarVisibility = value;
		CalculateVerticalRange();
	}
}