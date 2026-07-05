// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "LexWidgetHierarchyPickerView.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexWidgetSubObjectBehaviour.h"

#define LOCTEXT_NAMESPACE "LexWidgetDetailPropertyExtensionHandler"

FLexWidgetDetailPropertyExtensionHandler::FLexWidgetDetailPropertyExtensionHandler(UWorld* InWorld)
{
	World = InWorld;
}

bool FLexWidgetDetailPropertyExtensionHandler::IsPropertyExtendable(const UClass* ObjectClass, const IPropertyHandle& PropertyHandle) const
{
	return true;
}

void FLexWidgetDetailPropertyExtensionHandler::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass,	TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)return;
	if (!ObjectsBeingCustomized[0].IsValid())return;
	auto ObjectProperty = CastField<FObjectPropertyBase>(InPropertyHandle->GetProperty());
	if (!ObjectProperty)return;
	if (CastField<FClassProperty>(ObjectProperty) != nullptr)return;//skip class property
	auto ObjectClass = ObjectProperty->PropertyClass;
	if (!ObjectClass->IsChildOf(ULexWidget::StaticClass())
		&& !ObjectClass->IsChildOf(ULexWidgetSubObjectBehaviour::StaticClass())
		&& !ObjectClass->IsChildOf(ULexUIBehaviour::StaticClass())
		)return;
	if (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance))
		return;
	UObject* Object = nullptr;
	if (InPropertyHandle->GetValue(Object) != FPropertyAccess::Success)return;
	auto WeakObject = MakeWeakObjectPtr(Object);
	InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&InDetailBuilder]()
	{
		InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
	}));

	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetText = [=, this]()
	{
		if (!WeakObject.IsValid())return NoneObjectText;
		if (auto Widget = Cast<ULexWidget>(WeakObject.Get()))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		else
		{
			if (auto OuterWidget = WeakObject->GetTypedOuter<ULexWidget>())
			{
				return FText::FromString(OuterWidget->GetDisplayName());
			}
			return NoneObjectText;
		}
	};
	auto GetTooltipText = [=, this]()
	{
		if (!WeakObject.IsValid())return NoneObjectText;
		ULexWidget* Widget = nullptr;
		FString PathStr;
		if (auto CastWidget = Cast<ULexWidget>(WeakObject.Get()))
		{
			Widget = CastWidget;
		}
		else
		{
			Widget = WeakObject->GetTypedOuter<ULexWidget>();
			if (!Widget)return NoneObjectText;
			PathStr = "." + WeakObject->GetPathName(Widget);
		}
		while (Widget && !Widget->IsRootWidgetInHierarchy())
		{
			PathStr = "/" + Widget->GetDisplayName() + PathStr;
			Widget = Widget->GetParent();
		}
		return FText::FromString(PathStr);
	};
	InWidgetRow.ValueContent()
	[
		SNew(SBox)
		.IsEnabled_Lambda([=]()
		{
			return InPropertyHandle->IsEditable();
		})
		.WidthOverride(5000)
		[
			SNew(SBox)
			.MinDesiredWidth(125)
			.Padding(0, 4)
			[
				SAssignNew(PickerButton, SComboButton)
				.HasDownArrow(true)
				.ToolTipText_Lambda(GetTooltipText)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda(GetText)
				]
				.MenuContent()
				[
					SNew(SBox)
					.Padding(4, 4)
					[
						SNew(SLexWidgetHierarchyPickerView, World.Get(), ObjectClass)
						.OnSelectItem_Lambda([=, this](UObject* InItem)
						{
							InPropertyHandle->SetValueFromFormattedString(InItem->GetPathName());
							PickerButton->SetIsOpen(false);
						})
					]
				]
			]
		]
	]
	;
}

#undef LOCTEXT_NAMESPACE