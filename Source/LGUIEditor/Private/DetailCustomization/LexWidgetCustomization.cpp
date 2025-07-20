// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexWidgetCustomization.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "IDetailGroup.h"
#include "LGUIEditorStyle.h"
#include "Editor.h"
#include "Widget/ComponentTransformDetails.h"
#include "Widget/AnchorPreviewWidget.h"
#include "PropertyCustomizationHelpers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorTools.h"
#include "LGUIHeaders.h"
#include "PrefabEditor/LGUIPrefabEditor.h"
#include "PrefabSystem/LGUIPrefabManager.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "PropertyType/LexWidgetAspectRatioCustomization.h"
#include "PropertyType/LexWidgetMarginSizeCustomization.h"
#include "PropertyType/LexWidgetSizeCustomization.h"

#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "UIItemComponentDetails"

UE_DISABLE_OPTIMIZATION

FLexWidgetCustomization::FLexWidgetCustomization()
{
	
}
FLexWidgetCustomization::~FLexWidgetCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexWidgetCustomization::MakeInstance()
{
	return MakeShareable(new FLexWidgetCustomization);
}
void FLexWidgetCustomization::ForceUpdateUI()
{
	for (auto item : TargetScriptArray)
	{
		if (item.IsValid())
		{
			item->EditorForceUpdate();
		}
	}
}

void FLexWidgetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	bool bIsSubPrefab = false;
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<ULexWidget>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
			if (ValidItem->GetWorld() != nullptr)
			{
				if (ValidItem->GetWorld()->WorldType == EWorldType::Editor)
				{
					if (auto PrefabHelper = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(ValidItem->GetOwner()))
					{
						bIsSubPrefab = PrefabHelper->IsActorBelongsToSubPrefab(ValidItem->GetOwner());
					}
					ValidItem->EditorForceUpdate();
				}
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[UIItemCustomization]Get TargetScript is null"));
		return;
	}

	LGUIEditorUtils::ShowError_MultiComponentNotAllowed(&DetailBuilder, TargetScriptArray[0].Get());
	
	IDetailCategoryBuilder& LGUICategory = DetailBuilder.EditCategory("LGUI");

	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexWidgetAspectRatio::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexWidgetAspectRatioCustomization::MakeInstance));
	auto AspectRatio_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, AspectRatio));
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexWidgetSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexWidgetSizeCustomization::MakeInstance, AspectRatio_PH.ToSharedPtr()));
	DetailBuilder.GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout(FLexWidgetMarginSize::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexWidgetMarginSizeCustomization::MakeInstance));
	
	//pivot
	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, Pivot));
	auto PivotHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, Pivot));
	PivotHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPrePivotChange();
		}));
	PivotHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPivotChanged();
		}));
	PivotHandle->SetOnChildPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPrePivotChange();
		}));
	PivotHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPivotChanged();
		}));

	LGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, RenderSize));
	//HierarchyIndex
	{
		auto HierarchyIndexHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex));
		DetailBuilder.HideProperty(HierarchyIndexHandle);
		HierarchyIndexHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
			ForceUpdateUI();
			ULGUIPrefabManagerObject::MarkBroadcastLevelActorListChanged();
			}));
		auto hierarchyIndexWidget =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(2, 0)
			[
				HierarchyIndexHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.Padding(2, 0)
			.AutoWidth()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("IncreaseHierarchyOrder_Tooltip", "Move order up"))
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.VAlign(EVerticalAlignment::VAlign_Center)
				.IsEnabled_Static(LGUIEditorUtils::IsEnabledOnProperty, HierarchyIndexHandle)
				.OnClicked(this, &FLexWidgetCustomization::OnClickIncreaseOrDecreaseHierarchyIndex, true, HierarchyIndexHandle)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("IncreaseHierarchyOrder", "+"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(2, 0)
			.AutoWidth()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("DecreaseHierarchyOrder_Tooltip", "Move order down"))
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.VAlign(EVerticalAlignment::VAlign_Center)
				.IsEnabled_Static(LGUIEditorUtils::IsEnabledOnProperty, HierarchyIndexHandle)
				.OnClicked(this, &FLexWidgetCustomization::OnClickIncreaseOrDecreaseHierarchyIndex, false, HierarchyIndexHandle)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DecreaseHierarchyOrder", "-"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];

		LGUICategory.AddCustomRow(LOCTEXT("HierarchyIndexManager", "HierarchyIndexManager"))
		.CopyAction(FUIAction(
			FExecuteAction::CreateSP(this, &FLexWidgetCustomization::OnCopyHierarchyIndex)
		))
		.PasteAction(FUIAction(
			FExecuteAction::CreateSP(this, &FLexWidgetCustomization::OnPasteHierarchyIndex, HierarchyIndexHandle)
		))
		.NameContent()
		[
			HierarchyIndexHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			hierarchyIndexWidget
		]
		.PropertyHandleList({ HierarchyIndexHandle })
		;

		LGUICategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, FlattenHierarchyIndex)), EPropertyLocation::Advanced);
	}
		
	//displayName
	auto displayNamePropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, DisplayName));
	DetailBuilder.HideProperty(displayNamePropertyHandle);
	LGUICategory.AddCustomRow(LOCTEXT("DisplayName", "Display Name"), true)
		.NameContent()
		[
			displayNamePropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(500)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				displayNamePropertyHandle->CreatePropertyValueWidget(true)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("FixDisplayName", "Fix it"))
				.OnClicked(this, &FLexWidgetCustomization::OnClickFixDisplayNameButton, true, displayNamePropertyHandle)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Visibility(this, &FLexWidgetCustomization::GetDisplayNameWarningVisibility)
				.ToolTipText(LOCTEXT("FixDisplayName_Tooltip", "DisplayName not equal to ActorLabel."))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("FixDisplayNameOnHierarchy", "Fix all hierarchy"))
				.OnClicked(this, &FLexWidgetCustomization::OnClickFixDisplayNameButton, false, displayNamePropertyHandle)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Visibility(this, &FLexWidgetCustomization::GetDisplayNameWarningVisibility)
				.ToolTipText(LOCTEXT("FixDisplayNameOnHierarchy_Tooltip", "DisplayName not equal to ActorLabel."))
			]
		]
		;

	auto LayoutProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, Layout));
	UObject* Layout = nullptr;
	LayoutProperty->GetValue(Layout);
	IDetailCategoryBuilder& LayoutCategory = DetailBuilder.EditCategory("Layout");
	auto LayoutPropertyValueWidget = LayoutProperty->CreatePropertyValueWidget();
	if (bIsSubPrefab)
	{
		LayoutPropertyValueWidget->SetEnabled(false);
	}
	LayoutCategory.HeaderContent(LayoutPropertyValueWidget);
	LayoutCategory.SetIsEmpty(!IsValid(Layout));
	LayoutCategory.AddCustomRow(LOCTEXT("LayoutPlaceholder", "Placeholder"))
		.Visibility(IsValid(Layout) ? EVisibility::Hidden : EVisibility::Visible)
		.NameContent()
		[
			LayoutProperty->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			LayoutProperty->CreatePropertyValueWidget()
		];
	LayoutCategory.AddExternalObjects({ Layout }, EPropertyLocation::Default
		, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	DetailBuilder.HideProperty(LayoutProperty);

	auto LayoutSlotProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, LayoutSlot));
	UObject* LayoutSlot = nullptr;
	LayoutSlotProperty->GetValue(LayoutSlot);
	auto& LayoutSlotCategory = DetailBuilder.EditCategory("LayoutSlot");
	// LayoutSlotCategory.AddExternalObjects({ LayoutSlot }, EPropertyLocation::Default
	// 	, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	DetailBuilder.HideProperty(LayoutSlotProperty);

	auto VisualProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidget, Visual));
	UObject* Visual = nullptr;
	VisualProperty->GetValue(Visual);
	IDetailCategoryBuilder& VisualCategory = DetailBuilder.EditCategory("Visual");
	auto VisualPropertyValueWidget = VisualProperty->CreatePropertyValueWidget();
	if (bIsSubPrefab)
	{
		VisualPropertyValueWidget->SetEnabled(false);
	}
	VisualCategory.HeaderContent(VisualPropertyValueWidget);
	VisualCategory.SetIsEmpty(Visual == nullptr);
	VisualCategory.AddCustomRow(LOCTEXT("VisualPlaceholder", "Placeholder"))
		.Visibility(IsValid(Visual) ? EVisibility::Hidden : EVisibility::Visible)
		.NameContent()
		[
			VisualProperty->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			VisualProperty->CreatePropertyValueWidget()
		]
		;
	VisualCategory.AddExternalObjects({ Visual }, EPropertyLocation::Common
		, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	DetailBuilder.HideProperty(VisualProperty);
}

void FLexWidgetCustomization::OnPrePivotChange()
{
	
}
void FLexWidgetCustomization::OnPivotChanged()
{
	
}

EVisibility FLexWidgetCustomization::GetDisplayNameWarningVisibility()const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return EVisibility::Hidden;

	if (auto actor = TargetScriptArray[0]->GetOwner())
	{
		if (TargetScriptArray[0] == actor->GetRootComponent())
		{
			auto actorLabel = actor->GetActorLabel();
			if (actorLabel.StartsWith("//"))
			{
				actorLabel = actorLabel.Right(actorLabel.Len() - 2);
			}
			if (TargetScriptArray[0]->GetDisplayName() == actorLabel)
			{
				return EVisibility::Hidden;
			}
			else
			{
				return EVisibility::Visible;
			}
		}
		else
		{
			if (TargetScriptArray[0]->GetName() == TargetScriptArray[0]->GetDisplayName())
			{
				return EVisibility::Hidden;
			}
			else
			{
				return EVisibility::Visible;
			}
		}
	}
	else
	{
		auto name = TargetScriptArray[0]->GetName();
		auto genVarSuffix = FString(TEXT("_GEN_VARIABLE"));
		if (name.EndsWith(genVarSuffix))
		{
			name.RemoveAt(name.Len() - genVarSuffix.Len(), genVarSuffix.Len());
		}
		if (TargetScriptArray[0]->GetDisplayName() == name)
		{
			return EVisibility::Hidden;
		}
		else
		{
			return EVisibility::Visible;
		}
	}
}

FReply FLexWidgetCustomization::OnClickIncreaseOrDecreaseHierarchyIndex(bool IncreaseOrDecrease, TSharedRef<IPropertyHandle> HierarchyIndexHandle)
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FReply::Handled();

	//hierarchy index could affect other items
	GEditor->BeginTransaction(LOCTEXT("ChangeHierarchyIndex_Transaction", "Change LGUI Hierarchy Index"));
	for (auto& Item : TargetScriptArray)
	{
		Item->Modify();
		if (auto Parent = Item->GetUIParent())
		{
			for (auto Child : Parent->UIChildren)
			{
				Child->Modify();
			}
		}
	}

	for (auto& Item : TargetScriptArray)
	{
		HierarchyIndexHandle->SetValue(Item->SiblingIndex + (IncreaseOrDecrease ? 1 : -1));
		//notify others
		if (auto Parent = Item->GetUIParent())
		{
			for (auto Child : Parent->UIChildren)
			{
				auto HierarchyIndexProperty = FindFProperty<FIntProperty>(ULexWidget::StaticClass(), GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex));
				check(HierarchyIndexProperty != nullptr);
				FLexUIUtils::NotifyPropertyChanged(Child, HierarchyIndexProperty);
			}
		}
	}
	GEditor->EndTransaction();

	ULGUIPrefabManagerObject::MarkBroadcastLevelActorListChanged();
	return FReply::Handled();
}

FReply FLexWidgetCustomization::OnClickFixDisplayNameButton(bool singleOrAll, TSharedRef<IPropertyHandle> DisplayNameHandle)
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FReply::Handled();

	TArray<TWeakObjectPtr<ULexWidget>> UIItems;
	if (singleOrAll)
	{
		UIItems = TargetScriptArray;
	}
	else
	{
		TArray<AActor*> SelectedActors;
		for (auto& UIItem : TargetScriptArray)
		{
			if (!SelectedActors.Contains(UIItem->GetOwner()))
			{
				SelectedActors.Add(UIItem->GetOwner());
			}
		}
		auto SelectedRootActors = LGUIEditorTools::GetRootActorListFromSelection(SelectedActors);
		for (auto& RootActor : SelectedRootActors)
		{
			TArray<AActor*> ChildrenActors;
			FLexUIUtils::CollectChildrenActors(RootActor, ChildrenActors, true);
			for (auto& Actor : ChildrenActors)
			{
				if (auto UIItem = Cast<ULexWidget>(Actor->GetRootComponent()))
				{
					UIItems.Add(UIItem);
				}
			}
		}
	}

	GEditor->BeginTransaction(LOCTEXT("FixDisplayName_Transaction", "Fix DisplayName"));
	for (auto& UIItem : UIItems)
	{
		UIItem->Modify();
	}

	for (auto& UIItem : TargetScriptArray)
	{
		FString DisplayName;
		if (auto actor = UIItem->GetOwner())
		{
			if (UIItem == actor->GetRootComponent())
			{
				auto actorLabel = UIItem->GetOwner()->GetActorLabel();
				if (actorLabel.StartsWith("//"))
				{
					actorLabel = actorLabel.Right(actorLabel.Len() - 2);
				}
				DisplayName = actorLabel;
			}
			else
			{
				DisplayName = UIItem->GetName();
			}
		}
		else
		{
			auto name = UIItem->GetName();
			auto genVarSuffix = FString(TEXT("_GEN_VARIABLE"));
			if (name.EndsWith(genVarSuffix))
			{
				name.RemoveAt(name.Len() - genVarSuffix.Len(), genVarSuffix.Len());
			}
			DisplayName = name;
		}
		DisplayNameHandle->SetValue(DisplayName);

		FLexUIUtils::NotifyPropertyChanged(UIItem.Get(), GET_MEMBER_NAME_CHECKED(ULexWidget, DisplayName));
	}
	GEditor->EndTransaction();

	return FReply::Handled();
}
void FLexWidgetCustomization::OnCopyHierarchyIndex()
{
	if (TargetScriptArray.Num() > 0)
	{
		if (TargetScriptArray[0].IsValid())
		{
			FPlatformApplicationMisc::ClipboardCopy(*FString::Printf(TEXT("%d"), TargetScriptArray[0]->GetSiblingIndex()));
		}
	}
}
void FLexWidgetCustomization::OnPasteHierarchyIndex(TSharedRef<IPropertyHandle> PropertyHandle)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	if (PastedText.IsNumeric())
	{
		int value = FCString::Atoi(*PastedText);
		PropertyHandle->SetValue(value);
	}
}

UE_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE