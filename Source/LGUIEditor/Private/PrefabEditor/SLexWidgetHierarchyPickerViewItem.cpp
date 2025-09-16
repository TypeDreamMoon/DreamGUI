// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLexWidgetHierarchyPickerViewItem.h"
#include "Styling/CoreStyle.h"
#include "LGUIPrefabEditor.h"
#include "ScopedTransaction.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "LexWidgetEditorHierarchyViewItemForSelection"

void SLexWidgetHierarchyPickerViewItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SLexWidgetHierarchyPickerView::DataType InModel
	, TSharedPtr<FLGUIPrefabEditor> InManager, UClass* InObjectClass)
{
	Model = InModel;
	Manager = InManager;

	STableRow<SLexWidgetHierarchyPickerView::DataType>::Construct(
		STableRow<SLexWidgetHierarchyPickerView::DataType>::FArguments()
		.Padding(0.0f)
		.Content()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				//.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &SLexWidgetHierarchyPickerViewItem::GetItemText)
				.ColorAndOpacity_Lambda([=, this]()
				{
					if (this->Model->Object->GetClass()->IsChildOf(InObjectClass))
					{
						return FSlateColor(FColor::Green);
					}
					else
					{
						return FSlateColor(FColor(192,192,192,255));
					}
				})
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(4)
			]
			+SHorizontalBox::Slot()
			.FillWidth(0.3)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.WidthOverride(150)
				[
					SNew(STextBlock)
					.Text(this, &SLexWidgetHierarchyPickerViewItem::GetTypeText)
				]
			]
		],
		InOwnerTableView);
}

FText SLexWidgetHierarchyPickerViewItem::GetItemText() const
{
	return FText::FromString(Model->DisplayText);
}

FText SLexWidgetHierarchyPickerViewItem::GetTypeText() const
{
	FString TypeString;
	if (Model->Object->IsA(AActor::StaticClass()))
	{
		TypeString = "(Actor)";
	}
	else if (Model->Object->IsA(UActorComponent::StaticClass()))
	{
		TypeString = "(Component)";
	}
	else
	{
		TypeString = "(Object)";
	}
	return FText::FromString(FString::Printf(TEXT("%s %s"), *Model->Object->GetClass()->GetName(), *TypeString));
}

#undef LOCTEXT_NAMESPACE
