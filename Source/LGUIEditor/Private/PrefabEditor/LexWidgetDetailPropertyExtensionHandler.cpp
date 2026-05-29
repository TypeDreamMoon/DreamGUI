// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "LexUIPrefabEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "LexWidgetHierarchyPickerView.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexWidgetSubObjectBehaviour.h"

#define LOCTEXT_NAMESPACE "LexWidgetDetailPropertyExtensionHandler"

FLexWidgetDetailPropertyExtensionHandler::FLexWidgetDetailPropertyExtensionHandler(TWeakPtr<FLexUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
}

bool FLexWidgetDetailPropertyExtensionHandler::IsPropertyExtendable(const UClass* ObjectClass, const IPropertyHandle& PropertyHandle) const
{
	return true;
}

void FLexWidgetDetailPropertyExtensionHandler::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass,	TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	InDetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	if (TargetObjects.Num() != 1)return;
	auto TargetObject = TargetObjects[0];
	if (!TargetObject.IsValid())return;
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
	InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&InDetailBuilder]()
	{
		InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
	}));

	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		if (!PrefabEditorPtr.IsValid())return NoneObjectText;
		if (auto Widget = Cast<ULexWidget>(Object))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		else
		{
			auto OuterWidget = Object->GetTypedOuter<ULexWidget>();
			return FText::FromString(OuterWidget->GetDisplayName());
		}
	};
	auto GetTooltipText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		if (!PrefabEditorPtr.IsValid())return NoneObjectText;
		ULexWidget* Widget = nullptr;
		FString PathStr;
		if (auto CastWidget = Cast<ULexWidget>(Object))
		{
			Widget = CastWidget;
		}
		else
		{
			Widget = Object->GetTypedOuter<ULexWidget>();
			PathStr = "." + Object->GetPathName(Widget);
		}
		auto RootAgentWidget = PrefabEditorPtr.Pin()->GetRootAgentWidget();
		while (Widget && Widget != RootAgentWidget)
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
						SNew(SLexWidgetHierarchyPickerView, PrefabEditorPtr.Pin(), ObjectClass)
						.OnSelectItem_Lambda([=, this](UObject* InItem)
						{
							// InPropertyHandle->SetValue(InItem);
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