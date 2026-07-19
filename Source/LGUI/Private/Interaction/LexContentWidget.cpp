// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Interaction/LexContentWidget.h"
#include "Core/Components/LexWidget.h"

void ULexContentWidget::OnRegister()
{
	Super::OnRegister();
	if (!IsValid(Content) && GetWidget() && !GetWidget()->GetChildren().IsEmpty())
	{
		Content = GetWidget()->GetChildren()[0];
	}
}

bool ULexContentWidget::CanAcceptChild(const ULexWidget* Child) const
{
	return IsValid(Child) && Child != GetWidget() && !GetWidget()->IsChildOf(Child);
}

bool ULexContentWidget::SetContent(ULexWidget* NewContent)
{
	if (NewContent == Content)
	{
		return true;
	}
	if (!CanAcceptChild(NewContent))
	{
		return false;
	}
	if (IsValid(Content) && Content->GetParent() == GetWidget())
	{
		Content->SetParent(nullptr, true);
	}
	Content = NewContent;
	Content->SetParent(GetWidget(), false, 0);
	ULexWidget::MarkLayoutForRebuild(GetWidget());
	return true;
}

void ULexContentWidget::ClearContent(bool bDetach)
{
	if (bDetach && IsValid(Content) && Content->GetParent() == GetWidget())
	{
		Content->SetParent(nullptr, true);
	}
	Content = nullptr;
	ULexWidget::MarkLayoutForRebuild(GetWidget());
}

bool ULexNamedSlotHost::SetContentForSlot(FName SlotName, ULexWidget* Content)
{
	if (SlotName.IsNone() || !IsValid(Content) || Content == GetWidget() || GetWidget()->IsChildOf(Content))
	{
		return false;
	}
	if (TObjectPtr<ULexWidget>* Existing = NamedSlots.Find(SlotName))
	{
		if (IsValid(*Existing) && (*Existing)->GetParent() == GetWidget())
		{
			(*Existing)->SetParent(nullptr, true);
		}
	}
	NamedSlots.Add(SlotName, Content);
	Content->SetParent(GetWidget(), false);
	ULexWidget::MarkLayoutForRebuild(GetWidget());
	return true;
}

ULexWidget* ULexNamedSlotHost::GetContentForSlot(FName SlotName) const
{
	if (const TObjectPtr<ULexWidget>* Found = NamedSlots.Find(SlotName))
	{
		return Found->Get();
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
	NamedSlots.GetKeys(Result);
	return Result;
}
