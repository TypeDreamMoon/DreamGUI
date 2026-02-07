// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "LexUIPrefabEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "LexWidgetHierarchyPickerView.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"

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
		&& !ObjectClass->IsChildOf(ULexVisual::StaticClass())
		&& !ObjectClass->IsChildOf(ULexLayoutContainer::StaticClass())
		&& !ObjectClass->IsChildOf(ULexLayoutSelf::StaticClass())
		&& !ObjectClass->IsChildOf(AActor::StaticClass())
		&& !ObjectClass->IsChildOf(UActorComponent::StaticClass())
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
		if (auto Actor = Cast<AActor>(Object))
		{
			return FText::FromString(Actor->GetActorLabel());
		}
		else
		{
			auto OuterActor = Object->GetTypedOuter<AActor>();
			return FText::FromString(Object->GetPathName(OuterActor));
		}
	};
	auto GetTooltipText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		if (!PrefabEditorPtr.IsValid())return NoneObjectText;
		AActor* Actor = nullptr;
		FString PathStr;
		if (auto CastActor = Cast<AActor>(Object))
		{
			Actor = CastActor;
		}
		else
		{
			Actor = Object->GetTypedOuter<AActor>();
			PathStr = "." + Object->GetPathName(Actor);
		}
		auto RootAgentActor = PrefabEditorPtr.Pin()->GetRootAgentActor();
		while (Actor && Actor != RootAgentActor)
		{
			PathStr = "/" + Actor->GetActorLabel() + PathStr;
			Actor = Actor->GetAttachParentActor();
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