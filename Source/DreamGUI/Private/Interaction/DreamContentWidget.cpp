// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamContentWidget.h"
#include "Core/Components/DreamWidget.h"

void UDreamContentWidget::OnRegister()
{
	Super::OnRegister();
	SynchronizeContentFromChildren();
}

void UDreamContentWidget::OnWidgetChildAttached(UDreamWidget* Child)
{
	SynchronizeContentFromChildren();
}

void UDreamContentWidget::OnWidgetChildDetached(UDreamWidget* Child)
{
	SynchronizeContentFromChildren();
}

void UDreamContentWidget::SynchronizeContentFromChildren()
{
	Content = nullptr;
	if (const UDreamWidget* Host = GetWidget(); IsValid(Host))
	{
		for (UDreamWidget* Child : Host->GetChildren())
		{
			if (IsValid(Child))
			{
				Content = Child;
				break;
			}
		}
	}
}

UDreamWidget* UDreamContentWidget::GetContent() const
{
	UDreamWidget* Host = GetWidget();
	if (!IsValid(Host))
	{
		return nullptr;
	}
	if (IsValid(Content) && Content->GetParent() == Host)
	{
		return Content;
	}
	for (UDreamWidget* Child : Host->GetChildren())
	{
		if (IsValid(Child))
		{
			return Child;
		}
	}
	return nullptr;
}

bool UDreamContentWidget::CanAcceptChild(const UDreamWidget* Child) const
{
	UDreamWidget* Host = GetWidget();
	if (!IsValid(Host) || !IsValid(Child) || Child == Host || Host->IsChildOf(Child))
	{
		return false;
	}
	UDreamWidget* CurrentContent = GetContent();
	return !IsValid(CurrentContent) || CurrentContent == Child;
}

bool UDreamContentWidget::SetContent(UDreamWidget* NewContent)
{
	UDreamWidget* Host = GetWidget();
	if (!IsValid(Host))
	{
		return false;
	}
	if (!IsValid(NewContent))
	{
		ClearContent(true);
		return true;
	}
	UDreamWidget* CurrentContent = GetContent();
	if (NewContent == CurrentContent)
	{
		return true;
	}
	if (NewContent == Host || Host->IsChildOf(NewContent))
	{
		return false;
	}
	for (UDreamWidget* Child : Host->GetChildren())
	{
		if (IsValid(Child) && Child != CurrentContent && Child != NewContent)
		{
			return false;
		}
	}
	const int32 Capacity = Host->GetMaxChildrenCapacity();
	if (Capacity != INDEX_NONE)
	{
		TSet<const UDreamWidget*> ProjectedChildren;
		for (const UDreamWidget* Child : Host->GetChildren())
		{
			if (IsValid(Child)) ProjectedChildren.Add(Child);
		}
		if (IsValid(CurrentContent) && CurrentContent->GetParent() == Host)
		{
			ProjectedChildren.Remove(CurrentContent);
		}
		ProjectedChildren.Add(NewContent);
		if (ProjectedChildren.Num() > Capacity)
		{
			return false;
		}
	}
	if (IsValid(CurrentContent) && CurrentContent->GetParent() == Host)
	{
		if (!CurrentContent->TrySetParent(nullptr, true))
		{
			return false;
		}
	}

	Content = NewContent;
	if (!NewContent->TrySetParent(Host, false, 0))
	{
		Content = CurrentContent;
		if (IsValid(CurrentContent) && CurrentContent->GetParent() != Host)
		{
			ensureMsgf(CurrentContent->TrySetParent(Host, true, 0),
				TEXT("Failed to restore ContentWidget child '%s'."), *GetNameSafe(CurrentContent));
		}
		SynchronizeContentFromChildren();
		return false;
	}
	SynchronizeContentFromChildren();
	UDreamWidget::MarkLayoutForRebuild(Host);
	return true;
}

void UDreamContentWidget::ClearContent(bool bDetach)
{
	(void)bDetach;
	UDreamWidget* Host = GetWidget();
	UDreamWidget* CurrentContent = GetContent();
	if (IsValid(CurrentContent) && CurrentContent->GetParent() == Host)
	{
		CurrentContent->TrySetParent(nullptr, true);
	}
	Content = nullptr;
	SynchronizeContentFromChildren();
	UDreamWidget::MarkLayoutForRebuild(Host);
}

void UDreamNamedSlotHost::OnRegister()
{
	Super::OnRegister();
	SynchronizeNamedSlots();
}

void UDreamNamedSlotHost::OnWidgetChildDetached(UDreamWidget* Child)
{
	Super::OnWidgetChildDetached(Child);
	for (auto It = NamedSlots.CreateIterator(); It; ++It)
	{
		if (It.Value() == Child)
		{
			It.RemoveCurrent();
		}
	}
}

void UDreamNamedSlotHost::SynchronizeNamedSlots()
{
	UDreamWidget* Host = GetWidget();
	TSet<const UDreamWidget*> AssignedChildren;
	for (auto It = NamedSlots.CreateIterator(); It; ++It)
	{
		UDreamWidget* Child = It.Value().Get();
		if (It.Key().IsNone() || !IsValid(Host) || !IsValid(Child)
			|| Child->GetParent() != Host || AssignedChildren.Contains(Child))
		{
			It.RemoveCurrent();
			continue;
		}
		AssignedChildren.Add(Child);
	}
}

bool UDreamNamedSlotHost::SetContentForSlot(FName SlotName, UDreamWidget* Content)
{
	UDreamWidget* Host = GetWidget();
	if (SlotName.IsNone() || !IsValid(Host) || !IsValid(Content) || Content == Host || Host->IsChildOf(Content))
	{
		return false;
	}
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& Pair : NamedSlots)
	{
		if (Pair.Key != SlotName && Pair.Value == Content)
		{
			return false;
		}
	}

	UDreamWidget* ExistingContent = nullptr;
	if (const TObjectPtr<UDreamWidget>* Existing = NamedSlots.Find(SlotName))
	{
		ExistingContent = Existing->Get();
		if (ExistingContent == Content && Content->GetParent() == Host)
		{
			return true;
		}
	}

	const int32 Capacity = Host->GetMaxChildrenCapacity();
	if (Capacity != INDEX_NONE)
	{
		TSet<const UDreamWidget*> ProjectedChildren;
		for (const UDreamWidget* Child : Host->GetChildren())
		{
			if (IsValid(Child))
			{
				ProjectedChildren.Add(Child);
			}
		}
		if (IsValid(ExistingContent) && ExistingContent->GetParent() == Host)
		{
			ProjectedChildren.Remove(ExistingContent);
		}
		ProjectedChildren.Add(Content);
		if (ProjectedChildren.Num() > Capacity)
		{
			return false;
		}
	}

	const bool bRestoreExisting = IsValid(ExistingContent) && ExistingContent->GetParent() == Host;
	if (bRestoreExisting)
	{
		ExistingContent->SetParent(nullptr, true);
	}
	if (!Content->TrySetParent(Host, false))
	{
		if (bRestoreExisting)
		{
			if (ExistingContent->TrySetParent(Host, true))
			{
				NamedSlots.Add(SlotName, ExistingContent);
			}
		}
		return false;
	}

	NamedSlots.Add(SlotName, Content);
	UDreamWidget::MarkLayoutForRebuild(Host);
	return true;
}

UDreamWidget* UDreamNamedSlotHost::GetContentForSlot(FName SlotName) const
{
	if (const TObjectPtr<UDreamWidget>* Found = NamedSlots.Find(SlotName))
	{
		UDreamWidget* Content = Found->Get();
		return IsValid(Content) && Content->GetParent() == GetWidget() ? Content : nullptr;
	}
	return nullptr;
}

void UDreamNamedSlotHost::ClearSlot(FName SlotName, bool bDetach)
{
	if (TObjectPtr<UDreamWidget>* Existing = NamedSlots.Find(SlotName))
	{
		if (bDetach && IsValid(*Existing) && (*Existing)->GetParent() == GetWidget())
		{
			(*Existing)->SetParent(nullptr, true);
		}
		NamedSlots.Remove(SlotName);
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

TArray<FName> UDreamNamedSlotHost::GetSlotNames() const
{
	TArray<FName> Result;
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& Pair : NamedSlots)
	{
		if (GetContentForSlot(Pair.Key)) Result.Add(Pair.Key);
	}
	return Result;
}
