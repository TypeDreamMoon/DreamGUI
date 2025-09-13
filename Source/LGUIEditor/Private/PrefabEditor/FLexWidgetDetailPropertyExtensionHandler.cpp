// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "FLexWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "LGUIPrefabEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"
#include "PrefabSystem/LGUIPrefabHelperObject.h"

FLexWidgetDetailPropertyExtensionHandler::FLexWidgetDetailPropertyExtensionHandler(TWeakPtr<FLGUIPrefabEditor> InPrefabEditor)
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
	if (!ObjectProperty->PropertyClass->IsChildOf(ULexWidget::StaticClass())
		&& !ObjectProperty->PropertyClass->IsChildOf(ULexVisual::StaticClass())
		&& !ObjectProperty->PropertyClass->IsChildOf(ULexLayout::StaticClass())
		&& !ObjectProperty->PropertyClass->IsChildOf(ULexLayoutSlot::StaticClass())
		)return;
	UObject* Object = nullptr;
	if (InPropertyHandle->GetValue(Object) != FPropertyAccess::Success)return;
	InWidgetRow.ValueContent()
	[
		SNew(SBox)
		.WidthOverride(5000)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillHeight(1)
			[
				//InPropertyHandle->CreatePropertyValueWidget()
				SNew(SObjectPropertyEntryBox)
				.IsEnabled(true)
				.AllowedClass(ObjectProperty->PropertyClass)
				.PropertyHandle(InPropertyHandle)
				.AllowClear(true)
				.DisplayThumbnail(InPropertyHandle->GetBoolMetaData("DisplayThumbnail"))
				.ToolTipText(InPropertyHandle->GetToolTipText())
				.OnObjectChanged_Lambda([=](const FAssetData& InObj)
				{
					InPropertyHandle->SetValue(InObj);
				})
			]
			+SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility_Lambda([=, this]()
				{
					if (Object != nullptr)return EVisibility::Visible;
					if (PrefabEditorPtr.IsValid())return EVisibility::Visible;
					return EVisibility::Collapsed;
				})
				[
					SNew(STextBlock)
					.Text_Lambda([=, this]()
					{
						if (Object == nullptr)return FText::GetEmpty();
						if (!PrefabEditorPtr.IsValid())return FText::GetEmpty();
						auto Actor = Object->GetTypedOuter<AActor>();
						FString PathStr = "." + Object->GetClass()->GetName();
						while (Actor)
						{
							PathStr = "/" + Actor->GetActorLabel() + PathStr;
							Actor = Actor->GetAttachParentActor();
							if (PrefabEditorPtr.Pin()->GetPrefabManagerObject()->LoadedRootActor == Actor)
								break;
						}
						return FText::FromString(PathStr);
					})
					.Font(InDetailBuilder.GetDetailFont())
				]
			]
		]
	]
	;
}