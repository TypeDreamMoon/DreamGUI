// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Interaction/LexContentWidget.h"
#include "Core/Components/LexWidget.h"

void ULexContentWidget::OnRegister()
{
	Super::OnRegister();
	SynchronizeContentFromChildren();
}

void ULexContentWidget::OnWidgetChildAttached(ULexWidget* Child)
{
	SynchronizeContentFromChildren();
}

void ULexContentWidget::OnWidgetChildDetached(ULexWidget* Child)
{
	SynchronizeContentFromChildren();
}

void ULexContentWidget::SynchronizeContentFromChildren()
{
	Content = nullptr;
	if (const ULexWidget* Host = GetWidget(); IsValid(Host))
	{
		for (ULexWidget* Child : Host->GetChildren())
		{
			if (IsValid(Child))
			{
				Content = Child;
				break;
			}
		}
	}
}

ULexWidget* ULexContentWidget::GetContent() const
{
	ULexWidget* Host = GetWidget();
	if (!IsValid(Host))
	{
		return nullptr;
	}
	if (IsValid(Content) && Content->GetParent() == Host)
	{
		return Content;
	}
	for (ULexWidget* Child : Host->GetChildren())
	{
		if (IsValid(Child))
		{
			return Child;
		}
	}
	return nullptr;
}

bool ULexContentWidget::CanAcceptChild(const ULexWidget* Child) const
{
	ULexWidget* Host = GetWidget();
	if (!IsValid(Host) || !IsValid(Child) || Child == Host || Host->IsChildOf(Child))
	{
		return false;
	}
	ULexWidget* CurrentContent = GetContent();
	return !IsValid(CurrentContent) || CurrentContent == Child;
}

bool ULexContentWidget::SetContent(ULexWidget* NewContent)
{
	ULexWidget* Host = GetWidget();
	if (!IsValid(Host))
	{
		return false;
	}
	if (!IsValid(NewContent))
	{
		ClearContent(true);
		return true;
	}
	ULexWidget* CurrentContent = GetContent();
	if (NewContent == CurrentContent)
	{
		return true;
	}
	if (NewContent == Host || Host->IsChildOf(NewContent))
	{
		return false;
	}
	for (ULexWidget* Child : Host->GetChildren())
	{
		if (IsValid(Child) && Child != CurrentContent && Child != NewContent)
		{
			return false;
		}
	}
	const int32 Capacity = Host->GetMaxChildrenCapacity();
	if (Capacity != INDEX_NONE)
	{
		TSet<const ULexWidget*> ProjectedChildren;
		for (const ULexWidget* Child : Host->GetChildren())
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
	ULexWidget::MarkLayoutForRebuild(Host);
	return true;
}

void ULexContentWidget::ClearContent(bool bDetach)
{
	(void)bDetach;
	ULexWidget* Host = GetWidget();
	ULexWidget* CurrentContent = GetContent();
	if (IsValid(CurrentContent) && CurrentContent->GetParent() == Host)
	{
		CurrentContent->TrySetParent(nullptr, true);
	}
	Content = nullptr;
	SynchronizeContentFromChildren();
	ULexWidget::MarkLayoutForRebuild(Host);
}

void ULexNamedSlotHost::OnRegister()
{
	Super::OnRegister();
	SynchronizeNamedSlots();
}

void ULexNamedSlotHost::OnWidgetChildDetached(ULexWidget* Child)
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

void ULexNamedSlotHost::SynchronizeNamedSlots()
{
	ULexWidget* Host = GetWidget();
	TSet<const ULexWidget*> AssignedChildren;
	for (auto It = NamedSlots.CreateIterator(); It; ++It)
	{
		ULexWidget* Child = It.Value().Get();
		if (It.Key().IsNone() || !IsValid(Host) || !IsValid(Child)
			|| Child->GetParent() != Host || AssignedChildren.Contains(Child))
		{
			It.RemoveCurrent();
			continue;
		}
		AssignedChildren.Add(Child);
	}
}

bool ULexNamedSlotHost::SetContentForSlot(FName SlotName, ULexWidget* Content)
{
	ULexWidget* Host = GetWidget();
	if (SlotName.IsNone() || !IsValid(Host) || !IsValid(Content) || Content == Host || Host->IsChildOf(Content))
	{
		return false;
	}
	for (const TPair<FName, TObjectPtr<ULexWidget>>& Pair : NamedSlots)
	{
		if (Pair.Key != SlotName && Pair.Value == Content)
		{
			return false;
		}
	}

	ULexWidget* ExistingContent = nullptr;
	if (const TObjectPtr<ULexWidget>* Existing = NamedSlots.Find(SlotName))
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
		TSet<const ULexWidget*> ProjectedChildren;
		for (const ULexWidget* Child : Host->GetChildren())
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
	ULexWidget::MarkLayoutForRebuild(Host);
	return true;
}

ULexWidget* ULexNamedSlotHost::GetContentForSlot(FName SlotName) const
{
	if (const TObjectPtr<ULexWidget>* Found = NamedSlots.Find(SlotName))
	{
		ULexWidget* Content = Found->Get();
		return IsValid(Content) && Content->GetParent() == GetWidget() ? Content : nullptr;
	}
	return nullptr;
}

void ULexNamedSlotHost::ClearSlot(FName SlotName, bool bDetach)
{
	if (TObjectPtr<ULexWidget>* Existing = NamedSlots.Find(SlotName))
	{
		if (bDetach && IsValid(*Existing) && (*Existing)->GetParent() == GetWidget())
		{
			(*Existing)->SetParent(nullptr, true);
		}
		NamedSlots.Remove(SlotName);
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

TArray<FName> ULexNamedSlotHost::GetSlotNames() const
{
	TArray<FName> Result;
	for (const TPair<FName, TObjectPtr<ULexWidget>>& Pair : NamedSlots)
	{
		if (GetContentForSlot(Pair.Key)) Result.Add(Pair.Key);
	}
	return Result;
}
