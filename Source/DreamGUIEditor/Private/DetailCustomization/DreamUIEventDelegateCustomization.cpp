// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIEventDelegateCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "DreamGUIEditorStyle.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyCustomizationHelpers.h"
#include "DreamUIEditorUtils.h"
#include "Widget/DreamUIVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/SWidget.h"
#include "Math/UnitConversion.h"
#include "STextPropertyEditableTextBox.h"
#include "SEnumCombo.h"
#include "Serialization/BufferArchive.h"
#include "DreamUIEditableTextPropertyHandle.h"
#include "DreamGUIEditorModule.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "Designer/DreamWidgetHierarchyPickerView.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"

#define LOCTEXT_NAMESPACE "DreamUIEventDelegateCustomization"

#define DreamUIEventWidgetSelfName "(WidgetSelf)"

TArray<FString> FDreamUIEventDelegateCustomization::CopySourceData;

TSharedPtr<IPropertyHandleArray> FDreamUIEventDelegateCustomization::GetEventListHandle()const
{
	return PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegate, EventList))->AsArray();
}

void FDreamUIEventDelegateCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyUtilites = CustomizationUtils.GetPropertyUtilities();
	PropertyHandle = InPropertyHandle;

	//add parameter type property
	bool bIsInWorld = false;
	TArray<UObject*> NodeSet;
	PropertyHandle->GetOuterObjects(NodeSet);
	if (NodeSet.Num() > 1)
	{
		auto TipText = LOCTEXT("NotSupportMultipleEdit_Content", "(Not support multiple edit)");
		ChildBuilder.AddCustomRow(LOCTEXT("NotSupportMultipleEdit_Row", "NotSupportMultipleEdit"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(this, &FDreamUIEventDelegateCustomization::GetEventTitleName)
			.ToolTipText(PropertyHandle->GetToolTipText())
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(TipText)
			.ToolTipText(TipText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FLinearColor(FColor::Red))
			.AutoWrapText(true)
		]
		;
		return;
	}
	auto OutObject = NodeSet[0];
	bIsInWorld = OutObject->GetWorld() != nullptr;
	if (!bIsInWorld)
	{
		if (CanChangeParameterType)
		{
			AddNativeParameterTypeProperty(ChildBuilder);
		}
		return;
	}

	// copy all EventDelegate I'm accessing right now
	TArray<void*> StructPtrs;
	PropertyHandle->AccessRawData(StructPtrs);
	check(StructPtrs.Num() != 0);

	EventDelegateInstances.AddZeroed(StructPtrs.Num());
	for (auto Iter = StructPtrs.CreateIterator(); Iter; ++Iter)
	{
		check(*Iter);
		auto Item = (FDreamUIEventDelegate*)(*Iter);
		EventDelegateInstances[Iter.GetIndex()] = Item;
		for (auto& listItem : Item->EventList)
		{
			listItem.CheckTargetObject();
		}
	}

	World = OutObject->GetWorld();

	auto EventListHandle = GetEventListHandle();
	auto RefreshDelegate = FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::UpdateEventsLayout);
	EventListHandle->SetOnNumElementsChanged(RefreshDelegate);
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegate, SupportParameterType));
	NativeParameterTypeHandle->SetOnPropertyValueChanged(RefreshDelegate);

	auto EventParameterType = GetNativeParameterType();
	
	ChildBuilder.AddCustomRow(LOCTEXT("EventDelegate", "EventDelegate"))
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(FMargin(-10, 0, -2, 0))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(1000)
						.HeightOverride(this, &FDreamUIEventDelegateCustomization::GetEventTotalHeight)
						[
							SNew(SImage)
							.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.EventGroup"))
							.ColorAndOpacity(FLinearColor(FColor(255, 255, 255, 255)))
						]
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.Padding(FMargin(8, 0))
						.HeightOverride(30)
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.HAlign(EHorizontalAlignment::HAlign_Left)
							.VAlign(EVerticalAlignment::VAlign_Center)
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(this, &FDreamUIEventDelegateCustomization::GetEventTitleName)
								.ToolTipText(PropertyHandle->GetToolTipText())
								//.Font(IDetailLayoutBuilder::GetDetailFont())
							]
							+SHorizontalBox::Slot()
							.HAlign(EHorizontalAlignment::HAlign_Right)
							[
								IsParameterTypeValid(EventParameterType) ?
								(
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.HAlign(HAlign_Left)
									.VAlign(VAlign_Center)
									.Padding(2, 0)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										[
											PropertyCustomizationHelpers::MakeAddButton(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::OnClickListAdd))
										]
										+ SHorizontalBox::Slot()
										[
											PropertyCustomizationHelpers::MakeEmptyButton(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::OnClickListEmpty))
										]
									]
								)
								:
								(
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.HAlign(HAlign_Left)
									.VAlign(VAlign_Center)
									.Padding(2, 0)
									[
										SNew(STextBlock)
										.AutoWrapText(true)
										.ColorAndOpacity(FSlateColor(FLinearColor::Red))
										.Text(LOCTEXT("ParameterTypeWrong", "Parameter type is wrong!"))
										.Font(IDetailLayoutBuilder::GetDetailFont())
									]
								)
							]
						]
					]
					+SVerticalBox::Slot()
					[
						SAssignNew(EventsWidget, SBox)
					]
				]
			]
		]
	;

	UpdateEventsLayout();
}

FText FDreamUIEventDelegateCustomization::GetEventTitleName()const
{
	auto EventParameterType = GetNativeParameterType();
	auto NameStr = PropertyHandle->GetPropertyDisplayName().ToString();
	FString ParamTypeString = UDreamUIEventDelegateParameterHelper::ParameterTypeToName(EventParameterType, nullptr);
	NameStr = NameStr + "(" + ParamTypeString + ")";
	return FText::FromString(NameStr);
}

FText FDreamUIEventDelegateCustomization::GetEventItemFunctionName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
	FName FunctionFName;
	FunctionNameHandle->GetValue(FunctionFName);
	FString FunctionName = FunctionFName.ToString();
	//parameterType
	auto FunctionParameterType = GetEventDataParameterType(EventItemPropertyHandle);

	bool ComponentValid = false;//event target component valid?
	bool EventFunctionValid = false;//event target function valid?
	UFunction* EventFunction = nullptr;

	if (auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle))
	{
		EventFunction = TargetObject->FindFunction(FunctionFName);
		if (EventFunction)
		{
			if (UDreamUIEventDelegateParameterHelper::IsStillSupported(EventFunction, FunctionParameterType))
			{
				EventFunctionValid = true;
			}
		}
	}

	if (!EventFunctionValid)//function not valid, show tip
	{
		if (FunctionName != "None Function" && !FunctionName.IsEmpty())
		{
			FString Prefix = "(NotValid)";
			FunctionName = Prefix.Append(FunctionName);
		}
	}
	if (FunctionName.IsEmpty())FunctionName = "None Function";
	return FText::FromString(FunctionName);
}

UObject* FDreamUIEventDelegateCustomization::GetEventItemTargetObject(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//TargetObject
	auto TargetObjectHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);
	return TargetObject;
}

FText FDreamUIEventDelegateCustomization::GetComponentDisplayName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	FString ComponentDisplayName;
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	auto HelperWidget = GetEventItemHelperWidget(EventItemPropertyHandle);
	if (TargetObject)
	{
		if (TargetObject == HelperWidget)
		{
			ComponentDisplayName = DreamUIEventWidgetSelfName;
		}
		else
		{
			if (Cast<UDreamUIBehaviour>(TargetObject) != nullptr || Cast<UDreamWidgetSubObjectBehaviour>(TargetObject) != nullptr)
			{
				ComponentDisplayName = TargetObject->GetName();
			}
			else
			{
				ComponentDisplayName = "(WrongType)";
			}
		}
	}
	else
	{
		ComponentDisplayName = "None";
	}
	return FText::FromString(ComponentDisplayName);
}

EVisibility FDreamUIEventDelegateCustomization::GetNativeParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
	FName FunctionFName;
	FunctionNameHandle->GetValue(FunctionFName);
	//parameterType
	auto FunctionParameterType = GetEventDataParameterType(EventItemPropertyHandle);
	UFunction* EventFunction = nullptr;
	auto EventParameterType = GetNativeParameterType();

	if (TargetObject)
	{
		EventFunction = TargetObject->FindFunction(FunctionFName);
	}

	if (IsValid(TargetObject) && IsValid(EventFunction))
	{
		bool bUseNativeParameter = false;
		auto UseNativeParameterHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, bUseNativeParameter));
		UseNativeParameterHandle->GetValue(bUseNativeParameter);

		if ((EventParameterType == FunctionParameterType) && bUseNativeParameter)//support native parameter
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility FDreamUIEventDelegateCustomization::GetDrawFunctionParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
	FName FunctionFName;
	FunctionNameHandle->GetValue(FunctionFName);
	//parameterType
	auto FunctionParameterType = GetEventDataParameterType(EventItemPropertyHandle);
	UFunction* EventFunction = nullptr;
	auto EventParameterType = GetNativeParameterType();

	if (TargetObject)
	{
		EventFunction = TargetObject->FindFunction(FunctionFName);
	}

	if (IsValid(TargetObject) && IsValid(EventFunction))
	{
		bool bUseNativeParameter = false;
		auto UseNativeParameterHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, bUseNativeParameter));
		UseNativeParameterHandle->GetValue(bUseNativeParameter);

		if ((EventParameterType == FunctionParameterType) && bUseNativeParameter)//support native parameter
		{

		}
		else
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility FDreamUIEventDelegateCustomization::GetNotValidParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
	FName FunctionFName;
	FunctionNameHandle->GetValue(FunctionFName);
	//parameterType
	auto FunctionParameterType = GetEventDataParameterType(EventItemPropertyHandle);
	UFunction* EventFunction = nullptr;
	auto EventParameterType = GetNativeParameterType();

	if (TargetObject)
	{
		EventFunction = TargetObject->FindFunction(FunctionFName);
	}

	if (IsValid(TargetObject) && IsValid(EventFunction))
	{
		return EVisibility::Collapsed;
	}
	else
	{
		return EVisibility::Visible;
	}
}

UDreamWidget* FDreamUIEventDelegateCustomization::GetEventItemHelperWidget(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//HelperWidget
	auto HelperWidgetHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UDreamWidget* HelperWidget = nullptr;
	HelperWidgetHandle->GetValue(*(UObject**)&HelperWidget);
	return HelperWidget;
}

void FDreamUIEventDelegateCustomization::UpdateEventsLayout()
{
	auto EventParameterType = GetNativeParameterType();
	auto EventListHandle = GetEventListHandle();

	auto EventsVerticalLayout = SNew(SVerticalBox);
	EventParameterWidgetArray.Empty(); 
	uint32 arrayCount;
	EventListHandle->GetNumElements(arrayCount);
	for (int32 EventItemIndex = 0; EventItemIndex < (int32)arrayCount; EventItemIndex++)
	{
		auto ItemPropertyHandle = EventListHandle->GetElement(EventItemIndex);
		//HelperWidget
		auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
		UObject* HelperWidgetObject = nullptr;
		HelperWidgetHandle->GetValue(HelperWidgetObject);
		auto HelperWidget = Cast<UDreamWidget>(HelperWidgetObject);

		//TargetObject
		auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
		UObject* TargetObject = nullptr;
		TargetObjectHandle->GetValue(TargetObject);

		UObject* ClassObject = nullptr;
		auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperClass));
		HelperClassHandle->GetValue(ClassObject);
		if (ClassObject != nullptr)
		{
			UClass* ClassValue = Cast<UClass>(ClassObject);
			if (ClassValue == UDreamWidget::StaticClass())
			{
				// Only when it is actually out of date, and never into the undo buffer. This whole block
				// re-derives TargetObject from HelperWidget while the panel is being BUILT: with a plain
				// SetValue, opening the details panel on a widget that has events opened a transaction,
				// pushed an undo entry and dirtied the asset before the author touched anything.
				if (TargetObject != HelperWidget)
				{
					TargetObjectHandle->SetValue(HelperWidget, EPropertyValueSetFlags::NotTransactable);
					TargetObject = HelperWidget;
				}
			}
			else if (ClassValue->IsChildOf(UDreamUIBehaviour::StaticClass()) || ClassValue->IsChildOf(UDreamWidgetSubObjectBehaviour::StaticClass()))
			{
				if (HelperWidget != nullptr)
				{
					UObject* FoundTargetObject = nullptr;
					if (ClassValue->IsChildOf(UDreamUIBehaviour::StaticClass()))
					{
						auto CompArray = HelperWidget->GetComponents(ClassValue);
						if (CompArray.Num() == 1)
						{
							FoundTargetObject = CompArray[0];
						}
						else if (CompArray.Num() > 1)
						{
							FName HelperComponentName = NAME_None;
							auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperComponentName));
							HelperComponentNameHandle->GetValue(HelperComponentName);
							if (!HelperComponentName.IsNone())
							{
								for (auto& Comp : CompArray)
								{
									if (Comp->GetFName() == HelperComponentName)
									{
										FoundTargetObject = Comp;
										break;
									}
								}
							}
						}
					}
					else if (ClassValue->IsChildOf(UDreamVisual::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetVisual();
					}
					else if (ClassValue->IsChildOf(UDreamLayoutContainer::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetLayoutContainer();
					}
					else if (ClassValue->IsChildOf(UDreamLayoutSelf::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetLayoutSelf();
					}
					if (FoundTargetObject != TargetObject)
					{
						//re-derived during layout, so not an edit; see the note above
						TargetObjectHandle->SetValue(FoundTargetObject, EPropertyValueSetFlags::NotTransactable);
						TargetObject = FoundTargetObject;
					}
				}
				else
				{
					if (TargetObject != nullptr)
					{
						TargetObjectHandle->SetValue((UObject*)nullptr, EPropertyValueSetFlags::NotTransactable);
						TargetObject = nullptr;
					}
				}
			}
		}
		else
		{
			if (TargetObject != nullptr)
			{
				TargetObjectHandle->SetValue((UObject*)nullptr, EPropertyValueSetFlags::NotTransactable);
				TargetObject = nullptr;
			}
		}

		HelperWidgetHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::OnHelperWidgetParameterChanged, ItemPropertyHandle));
			
		//function
		auto FunctionNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
		FName FunctionFName;
		FunctionNameHandle->GetValue(FunctionFName);
		//parameterType
		auto paramTypeHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamType));
		paramTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::OnParameterTypeChange, ItemPropertyHandle));
		auto FunctionParameterType = GetEventDataParameterType(ItemPropertyHandle);

		UFunction* EventFunction = nullptr;
		if (TargetObject)
		{
			EventFunction = TargetObject->FindFunction(FunctionFName);
		}

		TSharedRef<SWidget> ParameterWidget = SNew(SBox);
		if (IsValid(TargetObject) && IsValid(EventFunction))
		{
			bool bUseNativeParameter = false;
			auto UseNativeParameterHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, bUseNativeParameter));
			UseNativeParameterHandle->GetValue(bUseNativeParameter);
				
			if (EventParameterType != FunctionParameterType)//check "bUseNativeParameter" parameter
			{
				if (bUseNativeParameter)
				{
					bUseNativeParameter = false;
					//a correction made while drawing the row, not an edit; see the note above
					UseNativeParameterHandle->SetValue(bUseNativeParameter, EPropertyValueSetFlags::NotTransactable);
				}
			}
			if ((EventParameterType == FunctionParameterType) && bUseNativeParameter)//support native parameter
			{
				//clear buffer and value
				ClearValueBuffer(ItemPropertyHandle);
				ClearReferenceValue(ItemPropertyHandle);
				//native parameter AnchorData
				ParameterWidget =
					SNew(SBox)
					.VAlign(EVerticalAlignment::VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("(NativeParameter)", "(NativeParameter)"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					;
			}
			else
			{
				ParameterWidget = DrawFunctionParameter(ItemPropertyHandle, FunctionParameterType, EventFunction);
			}
		}
		else
		{
			ParameterWidget = 
				SNew(SBox)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("(NotValid)", "(NotValid)"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				;
		}
		ParameterWidget->SetToolTipText(LOCTEXT("Parameter", "Set parameter for the function of this event"));
		EventParameterWidgetArray.Add(ParameterWidget);

		//additional button
		int additionalButtonHeight = 20;
		auto additionalButtons = 
		SNew(SBox)
		[
			//copy, paste, add, delete
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("C", "C"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickCopyPaste, true, EventItemIndex)
							.ToolTipText(LOCTEXT("Copy", "Copy this function"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("P", "P"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickCopyPaste, false, EventItemIndex)
							.ToolTipText(LOCTEXT("Paste", "Paste copied function to this function"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("D", "D"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickDuplicate, EventItemIndex)
							.ToolTipText(LOCTEXT("Duplicate", "Duplicate this function"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("+", "+"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickAddRemove, true, EventItemIndex, (int32)arrayCount)
							.ToolTipText(LOCTEXT("Add", "Add new one"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("-", "-"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickAddRemove, false, EventItemIndex, (int32)arrayCount)
							.ToolTipText(LOCTEXT("Delete", "Delete this one"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("▲", "▲"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickMoveUpDown, true, EventItemIndex)
							.ToolTipText(LOCTEXT("MoveUp", "Move up"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.HeightOverride(additionalButtonHeight)
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("▼", "▼"))
							.OnClicked(this, &FDreamUIEventDelegateCustomization::OnClickMoveUpDown, false, EventItemIndex)
							.ToolTipText(LOCTEXT("MoveDown", "Move down"))
						]
					]
				]
			]
		];


		EventsVerticalLayout->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.Padding(FMargin(2, 0))
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(1000)
							.HeightOverride(this, &FDreamUIEventDelegateCustomization::GetEventItemHeight, EventItemIndex)
							[
								SNew(SImage)
								.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.EventItem"))
								.ColorAndOpacity(FLinearColor(FColor(255, 255, 255, 255)))
							]
						]
					]
					+ SOverlay::Slot()
					[
						SNew(SBox)
						.Padding(FMargin(4, 4))
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SBox)
								.Padding(FMargin(0, 0, 0, 2))
								[
									SNew(SHorizontalBox)
									+SHorizontalBox::Slot()
									[
										//HelperWidget
										SNew(SBox)
										.Padding(FMargin(0, 0, 6, 0))
										[
											DrawDreamWidgetSelectorForDesigner(EventItemIndex)
										]
									]
									+SHorizontalBox::Slot()
									[
										SNew(SBox)
										.HeightOverride(26)
										[
											//Component
											SNew(SComboButton)
											.HasDownArrow(true)
											.IsEnabled(this, &FDreamUIEventDelegateCustomization::IsComponentSelectorMenuEnabled, ItemPropertyHandle)
											.ToolTipText(LOCTEXT("Component", "Pick component for this event"))
											.ButtonContent()
											[
												SNew(STextBlock)
												.Text(this, &FDreamUIEventDelegateCustomization::GetComponentDisplayName, ItemPropertyHandle)
												.Font(IDetailLayoutBuilder::GetDetailFont())
											]
											.MenuContent()
											[
												MakeComponentSelectorMenu(EventItemIndex)
											]
										]
									]
								]
							]
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SBox)
								.Padding(FMargin(0, 0, 0, 2))
								[
									SNew(SHorizontalBox)
									+SHorizontalBox::Slot()
									[
										SNew(SBox)
										.Padding(FMargin(0, 0, 6, 0))
										[
											SNew(SBox)
											.HeightOverride(26)
											[
												//function
												SNew(SComboButton)
												.HasDownArrow(true)
												.IsEnabled(this, &FDreamUIEventDelegateCustomization::IsFunctionSelectorMenuEnabled, ItemPropertyHandle)
												.ToolTipText(LOCTEXT("Function", "Pick a function to execute of this event"))
												.ButtonContent()
												[
													SNew(STextBlock)
													.Text(this, &FDreamUIEventDelegateCustomization::GetEventItemFunctionName, ItemPropertyHandle)
													.Font(IDetailLayoutBuilder::GetDetailFont())
												]
												.MenuContent()
												[
													MakeFunctionSelectorMenu(EventItemIndex)
												]
											]
										]
									]
									+SHorizontalBox::Slot()
									[
										//parameter
										ParameterWidget
									]
								]
							]
							+SVerticalBox::Slot()
							[
								additionalButtons
							]
						]
					]
				]
			]
		;
	}
	EventsWidget->SetContent(EventsVerticalLayout);
}

void FDreamUIEventDelegateCustomization::OnHelperWidgetParameterChanged(TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	UObject* HelperWidgetObject = nullptr;
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	auto HelperWidget = Cast<UDreamWidget>(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);

	UObject* ClassObject = nullptr;
	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperClass));
	HelperClassHandle->GetValue(ClassObject);
	if (ClassObject != nullptr)
	{
		UClass* ClassValue = Cast<UClass>(ClassObject);
		if (ClassValue == UDreamWidget::StaticClass())
		{
			TargetObjectHandle->SetValue(HelperWidget);
		}
		else if (ClassValue->IsChildOf(UDreamUIBehaviour::StaticClass()))
		{
			if (HelperWidget != nullptr)
			{
				UDreamUIBehaviour* FoundHelperComp = nullptr;
				auto CompArray = HelperWidget->GetComponents(ClassValue);
				if (CompArray.Num() == 1)
				{
					FoundHelperComp = CompArray[0];
				}
				else if (CompArray.Num() > 1)
				{
					FName HelperComponentName = NAME_None;
					auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperComponentName));
					HelperComponentNameHandle->GetValue(HelperComponentName);
					if (!HelperComponentName.IsNone())
					{
						for (auto& Comp : CompArray)
						{
							if (Comp->GetFName() == HelperComponentName)
							{
								FoundHelperComp = Comp;
								break;
							}
						}
					}
				}
				if (FoundHelperComp != TargetObject)
				{
					TargetObjectHandle->SetValue(FoundHelperComp);
				}
			}
			else
			{
				if (TargetObject != nullptr)
				{
					TargetObjectHandle->SetValue((UObject*)nullptr);
				}
			}
		}
	}
	else
	{
		if (TargetObject != nullptr)
		{
			TargetObjectHandle->SetValue((UObject*)nullptr);
		}
	}

	UpdateEventsLayout();
}

void FDreamUIEventDelegateCustomization::OnSelectWidgetSubObject(UDreamWidgetSubObjectBehaviour* SubObj, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(SubObj);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(SubObj->GetClass());

	UpdateEventsLayout();
}

void FDreamUIEventDelegateCustomization::OnSelectComponent(UDreamUIBehaviour* Comp, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(Comp);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(Comp->GetClass());

	auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperComponentName));
	HelperComponentNameHandle->SetValue(Comp->GetFName());

	UpdateEventsLayout();
}
void FDreamUIEventDelegateCustomization::OnSelectWidgetSelf(TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(HelperWidgetObject);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(UDreamWidget::StaticClass());

	auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperComponentName));
	HelperComponentNameHandle->SetValue(NAME_None);

	UpdateEventsLayout();
}
void FDreamUIEventDelegateCustomization::OnSelectFunction(FName FuncName, EDreamUIEventDelegateParameterType ParamType, bool UseNativeParameter, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto nameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FunctionName));
	nameHandle->SetValue(FuncName);
	SetEventDataParameterType(ItemPropertyHandle, ParamType);
	auto UseNativeParameterHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, bUseNativeParameter));
	UseNativeParameterHandle->SetValue(UseNativeParameter);

	UpdateEventsLayout();
}

void FDreamUIEventDelegateCustomization::SetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle, EDreamUIEventDelegateParameterType ParameterType)
{
	auto ParamTypeHandle = EventDataItemHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamType));
	ParamTypeHandle->SetValue((uint8)ParameterType);
}
EDreamUIEventDelegateParameterType FDreamUIEventDelegateCustomization::GetNativeParameterType()const
{
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegate, SupportParameterType));
	// None is the enum's own "not initialized", so it is also the right answer for "they disagree".
	const auto eventParameterType = (EDreamUIEventDelegateParameterType)DreamDetailsMultiSelect::ValueOr<uint8>(
		NativeParameterTypeHandle, (uint8)EDreamUIEventDelegateParameterType::None);
	return eventParameterType;
}
void FDreamUIEventDelegateCustomization::AddNativeParameterTypeProperty(IDetailChildrenBuilder& ChildBuilder)
{
	auto& Group = ChildBuilder.AddGroup(FName(TEXT("NativeParameterType")), PropertyHandle->GetPropertyDisplayName());
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegate, SupportParameterType));
	Group.AddPropertyRow(NativeParameterTypeHandle.ToSharedRef());
}
EDreamUIEventDelegateParameterType FDreamUIEventDelegateCustomization::GetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle)const
{
	auto paramTypeHandle = EventDataItemHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamType));
	const auto functionParameterType = (EDreamUIEventDelegateParameterType)DreamDetailsMultiSelect::ValueOr<uint8>(
		paramTypeHandle, (uint8)EDreamUIEventDelegateParameterType::None);
	return functionParameterType;
}

TSharedRef<SWidget> FDreamUIEventDelegateCustomization::DrawDreamWidgetSelectorForDesigner(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UObject* Object = nullptr;
	if (HelperWidgetHandle->GetValue(Object) != FPropertyAccess::Success)return SNullWidget::NullWidget;
	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		if (auto Widget = Cast<UDreamWidget>(Object))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		else
		{
			auto OuterWidget = Object->GetTypedOuter<UDreamWidget>();
			return FText::FromString(Object->GetPathName(OuterWidget));
		}
	};
	auto GetTooltipText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		UDreamWidget* Widget = nullptr;
		FString PathStr;
		if (auto CastWidget = Cast<UDreamWidget>(Object))
		{
			Widget = CastWidget;
		}
		else
		{
			Widget = Object->GetTypedOuter<UDreamWidget>();
			PathStr = "." + Object->GetPathName(Widget);
		}
		while (Widget && !Widget->IsRootWidgetInHierarchy())
		{
			PathStr = "/" + Widget->GetDisplayName() + PathStr;
			Widget = Widget->GetParent();
		}
		return FText::FromString(PathStr);
	};
	return
		SNew(SBox)
		.IsEnabled_Lambda([=]()
		{
			return HelperWidgetHandle->IsEditable();
		})
		.WidthOverride(5000)
		[
			SNew(SBox)
			.MinDesiredWidth(125)
			.Padding(0, 0)
			[
				SAssignNew(WidgetPickerComboButton, SComboButton)
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
					.Padding(4, 0)
					[
						SNew(SDreamWidgetHierarchyPickerView, World.Get(), UDreamWidget::StaticClass())
						.OnSelectItem_Lambda([=, this](UObject* InItem)
						{
							HelperWidgetHandle->SetValueFromFormattedString(InItem->GetPathName());
							WidgetPickerComboButton->SetIsOpen(false);
						})
					]
				]
			]
		]
	;
}

TSharedRef<SWidget> FDreamUIEventDelegateCustomization::MakeComponentSelectorMenu(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	if (HelperWidgetObject == nullptr)
	{
		return SNew(SBox);
	}

	FMenuBuilder MenuBuilder(true, MakeShareable(new FUICommandList));

	auto HelperWidget = Cast<UDreamWidget>(HelperWidgetObject);
	MenuBuilder.AddMenuEntry(
		FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectWidgetSelf, ItemPropertyHandle)),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(DreamUIEventWidgetSelfName))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	);
	if (auto Visual = HelperWidget->GetVisual())
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<UDreamWidgetSubObjectBehaviour>(Visual), ItemPropertyHandle)),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Visual->GetClass()->GetName()))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		);
	}
	if (auto LayoutContainer = HelperWidget->GetLayoutContainer())
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<UDreamWidgetSubObjectBehaviour>(LayoutContainer), ItemPropertyHandle)),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(FText::FromString(LayoutContainer->GetClass()->GetName()))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		);
	}
	if (auto LayoutSelf = HelperWidget->GetLayoutSelf())
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<UDreamWidgetSubObjectBehaviour>(LayoutSelf), ItemPropertyHandle)),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(FText::FromString(LayoutSelf->GetClass()->GetName()))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		);
	}
	auto Components = HelperWidget->GetAllComponents();
	for (auto Comp : Components)
	{
		if(Comp->HasAnyFlags(EObjectFlags::RF_Transient))continue;
		auto CompName = Comp->GetFName();
		auto CompTypeName = Comp->GetClass()->GetName();
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectComponent, Comp, ItemPropertyHandle)),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			[
				SNew(STextBlock)
				.Text(FText::FromString(CompName.ToString()))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		);
	}
	return MenuBuilder.MakeWidget();
}
TSharedRef<SWidget> FDreamUIEventDelegateCustomization::MakeFunctionSelectorMenu(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto EventParameterType = GetNativeParameterType();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);
	if (TargetObject == nullptr)
	{
		return SNew(SBox);
	}

	FMenuBuilder MenuBuilder(true, MakeShareable(new FUICommandList));

	auto FunctionField = TFieldRange<UFunction>(TargetObject->GetClass());
	for (auto Func : FunctionField)
	{
		EDreamUIEventDelegateParameterType ParamType;
		if (UDreamUIEventDelegateParameterHelper::IsSupportedFunction(Func, ParamType))//show only supported type
		{
			FString ParamTypeString = UDreamUIEventDelegateParameterHelper::ParameterTypeToName(ParamType, Func);
			auto FunctionSelectorName = FString::Printf(TEXT("%s(%s)"), *Func->GetName(), *ParamTypeString);
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectFunction, Func->GetFName(), ParamType, false, ItemPropertyHandle)),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FunctionSelectorName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			);
			if (ParamType == EventParameterType && EventParameterType != EDreamUIEventDelegateParameterType::Empty)//if function support native parameter, then draw another button, and show as native parameter
			{
				FunctionSelectorName = FString::Printf(TEXT("%s(NativeParameter)"), *Func->GetName());
				MenuBuilder.AddMenuEntry(
					FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIEventDelegateCustomization::OnSelectFunction, Func->GetFName(), ParamType, true, ItemPropertyHandle)),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(EHorizontalAlignment::HAlign_Left)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FunctionSelectorName))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				);
			}					
		}
	}
	return MenuBuilder.MakeWidget();
}

bool FDreamUIEventDelegateCustomization::IsComponentSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	return IsValid(HelperWidgetObject);
}
bool FDreamUIEventDelegateCustomization::IsFunctionSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);

	return IsValid(HelperWidgetObject) && IsValid(TargetObject);
}
void FDreamUIEventDelegateCustomization::OnClickListAdd()
{
	auto EventListHandle = GetEventListHandle();
	EventListHandle->AddItem();
	UpdateEventsLayout();
}
void FDreamUIEventDelegateCustomization::OnClickListEmpty()
{
	auto EventListHandle = GetEventListHandle();
	EventListHandle->EmptyArray();
	UpdateEventsLayout();
}
FReply FDreamUIEventDelegateCustomization::OnClickAddRemove(bool AddOrRemove, int32 Index, int32 Count)
{
	auto EventListHandle = GetEventListHandle();
	if (AddOrRemove)
	{
		if (Count == 0)
		{
			EventListHandle->AddItem();
		}
		else
		{
			if (Index == Count - 1)//current is last, add to last
				EventListHandle->AddItem();
			else
				EventListHandle->Insert(Index + 1);
		}
	}
	else
	{
		if (Count != 0)
			EventListHandle->DeleteItem(Index);
	}
	UpdateEventsLayout();
	return FReply::Handled();
}
FReply FDreamUIEventDelegateCustomization::OnClickCopyPaste(bool CopyOrPaste, int32 Index)
{
	auto EventListHandle = GetEventListHandle();
	auto EventDataHandle = EventListHandle->GetElement(Index);
	if (CopyOrPaste)
	{
		CopySourceData.Reset();
		EventDataHandle->GetPerObjectValues(CopySourceData);
	}
	else
	{
		EventDataHandle->SetPerObjectValues(CopySourceData);
		UpdateEventsLayout();
	}
	return FReply::Handled();
}

FReply FDreamUIEventDelegateCustomization::OnClickDuplicate(int32 Index)
{
	auto EventListHandle = GetEventListHandle();
	auto EventDataHandle = EventListHandle->GetElement(Index);
	EventListHandle->DuplicateItem(Index);
	return FReply::Handled();
}
FReply FDreamUIEventDelegateCustomization::OnClickMoveUpDown(bool UpOrDown, int32 Index)
{
	auto EventListHandle = GetEventListHandle();
	if (UpOrDown)
	{
		if (Index <= 0)
			return FReply::Handled();

		EventListHandle->SwapItems(Index, Index - 1);
	}
	else
	{
		uint32 arrayCount;
		EventListHandle->GetNumElements(arrayCount);
		if (Index + 1 >= (int32)arrayCount)
			return FReply::Handled();

		EventListHandle->SwapItems(Index, Index + 1);
	}
	return FReply::Handled();
}


// NotTransactable, deliberately: this only copies the SERIALIZED parameter buffer into the typed
// field the editor widget reads, and it runs while the row is being drawn. A plain SetValue put an
// undo entry on the stack every time the panel was built. (The property system already skips the
// write entirely when the value is unchanged, so the common case costs nothing either way.)
#define SET_VALUE_ON_BUFFER(type)\
auto ParamBuffer = GetBuffer(ParamBufferHandle);\
FMemoryReader Reader(ParamBuffer);\
type Value;\
Reader << Value;\
ValueHandle->SetValue(Value, EPropertyValueSetFlags::NotTransactable);

TSharedRef<SWidget> FDreamUIEventDelegateCustomization::DrawFunctionParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, EDreamUIEventDelegateParameterType InFunctionParameterType, UFunction* InFunction)
{
	auto ParamBufferHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamBuffer));
	if (InFunctionParameterType != EDreamUIEventDelegateParameterType::None)//None means not select function yet
	{
		//One call, one table. The length used to be a literal per case, and those literals were still
		//the UE4 single precision sizes: Vector3 was pinned to 12 bytes while FVector is 24, so every
		//redraw truncated a correct 24 byte buffer back to 12 zero bytes, and the reader below then ran
		//off the end of it. The length now comes from the runtime that consumes the buffer.
		PrepareParameterBuffer(ParamBufferHandle, InFunctionParameterType);
		switch (InFunctionParameterType)
		{
		default:
		case EDreamUIEventDelegateParameterType::Empty:
		{
			ClearValueBuffer(InDataContainerHandle);
			ClearReferenceValue(InDataContainerHandle);
			return
				SNew(SBox)
				.MinDesiredWidth(500)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("(No parameter)", "(No parameter)"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::Bool:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, BoolValue));
			auto ParamBuffer = GetBuffer(ParamBufferHandle);
			bool Value = ParamBuffer.Num() > 0 && ParamBuffer[0] == 1;
			//mirror of the serialized buffer, drawn not edited; see SET_VALUE_ON_BUFFER
			ValueHandle->SetValue(Value, EPropertyValueSetFlags::NotTransactable);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::BoolValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Float:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FloatValue));
			SET_VALUE_ON_BUFFER(float);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::FloatValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Double:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, DoubleValue));
			SET_VALUE_ON_BUFFER(double);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::DoubleValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Int8:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int8Value));
			SET_VALUE_ON_BUFFER(int8);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::Int8ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::UInt8:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt8Value));
			SET_VALUE_ON_BUFFER(uint8);
			if (auto enumValue = UDreamUIEventDelegateParameterHelper::GetEnumParameter(InFunction))
			{
				return
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.FillWidth(1.0f)
					.Padding(0.0f, 2.0f)
					[
						SNew(SBox)
						.MinDesiredWidth(500)
						[
							SNew(SEnumComboBox, enumValue)
							.CurrentValue(this, &FDreamUIEventDelegateCustomization::GetEnumValue, ValueHandle)
							.OnEnumSelectionChanged(this, &FDreamUIEventDelegateCustomization::EnumValueChange, ValueHandle, ParamBufferHandle)
						]
					]
				;
			}
			else
			{
				ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::UInt8ValueChange, ValueHandle, ParamBufferHandle));
				return ValueHandle->CreatePropertyValueWidget();
			}
		}
		break;
		case EDreamUIEventDelegateParameterType::Int16:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int16Value));
			SET_VALUE_ON_BUFFER(int16);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::Int16ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::UInt16:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt16Value));
			SET_VALUE_ON_BUFFER(uint16);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::UInt16ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Int32:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int32Value));
			SET_VALUE_ON_BUFFER(int32);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::Int32ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::UInt32:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt32Value));
			SET_VALUE_ON_BUFFER(uint32);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::UInt32ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Int64:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int64Value));
			SET_VALUE_ON_BUFFER(int64);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::Int64ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::UInt64:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt64Value));
			SET_VALUE_ON_BUFFER(uint64);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::UInt64ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Vector2:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector2Value));
			SET_VALUE_ON_BUFFER(FVector2D);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SDreamUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.ShowX(true)
					.ShowY(true)
					.X(this, &FDreamUIEventDelegateCustomization::Vector2GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FDreamUIEventDelegateCustomization::Vector2GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FDreamUIEventDelegateCustomization::Vector2ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FDreamUIEventDelegateCustomization::Vector2ItemValueChange, 1, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::Vector3:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector3Value));
			SET_VALUE_ON_BUFFER(FVector);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SDreamUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.EnableZ(true)
					.ShowX(true)
					.ShowY(true)
					.ShowZ(true)
					.X(this, &FDreamUIEventDelegateCustomization::Vector3GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FDreamUIEventDelegateCustomization::Vector3GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FDreamUIEventDelegateCustomization::Vector3GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FDreamUIEventDelegateCustomization::Vector3ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FDreamUIEventDelegateCustomization::Vector3ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FDreamUIEventDelegateCustomization::Vector3ItemValueChange, 2, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::Vector4:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector4Value));
			SET_VALUE_ON_BUFFER(FVector4);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SDreamUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.EnableZ(true)
					.EnableW(true)
					.ShowX(true)
					.ShowY(true)
					.ShowZ(true)
					.ShowW(true)
					.X(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.W(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 3, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 2, ValueHandle, ParamBufferHandle)
					.OnWCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 3, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::Color:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ColorValue));
			auto ParamBuffer = GetBuffer(ParamBufferHandle);
			FMemoryReader Reader(ParamBuffer);
			FColor Value;
			Reader << Value;
			//mirror of the serialized buffer, drawn not edited; see SET_VALUE_ON_BUFFER
			ValueHandle->SetValueFromFormattedString(Value.ToString(), EPropertyValueSetFlags::NotTransactable);
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color with alpha unless it is ignored
					SAssignNew(ColorPickerParentWidget, SColorBlock)
					.Color(this, &FDreamUIEventDelegateCustomization::LinearColorGetValue, false, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(true)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Separate)
					.OnMouseButtonDown(this, &FDreamUIEventDelegateCustomization::OnMouseButtonDownColorBlock, false, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color without alpha
					SNew(SColorBlock)
					.Color(this, &FDreamUIEventDelegateCustomization::LinearColorGetValue, false, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(false)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.OnMouseButtonDown(this, &FDreamUIEventDelegateCustomization::OnMouseButtonDownColorBlock, false, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				];
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::LinearColor:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, LinearColorValue));
			SET_VALUE_ON_BUFFER(FLinearColor);
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color with alpha unless it is ignored
					SAssignNew(ColorPickerParentWidget, SColorBlock)
					.Color(this, &FDreamUIEventDelegateCustomization::LinearColorGetValue, true, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(true)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Separate)
					.OnMouseButtonDown(this, &FDreamUIEventDelegateCustomization::OnMouseButtonDownColorBlock, true, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color without alpha
					SNew(SColorBlock)
					.Color(this, &FDreamUIEventDelegateCustomization::LinearColorGetValue, true, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(false)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.OnMouseButtonDown(this, &FDreamUIEventDelegateCustomization::OnMouseButtonDownColorBlock, true, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				];
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::Quaternion:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, QuatValue));
			SET_VALUE_ON_BUFFER(FQuat);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SDreamUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.EnableZ(true)
					.EnableW(true)
					.ShowX(true)
					.ShowY(true)
					.ShowZ(true)
					.ShowW(true)
					.X(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.W(this, &FDreamUIEventDelegateCustomization::Vector4GetItemValue, 3, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 2, ValueHandle, ParamBufferHandle)
					.OnWCommitted(this, &FDreamUIEventDelegateCustomization::Vector4ItemValueChange, 3, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case EDreamUIEventDelegateParameterType::String:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, StringValue));
			SET_VALUE_ON_BUFFER(FString);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::StringValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case EDreamUIEventDelegateParameterType::Name:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, NameValue));
			SET_VALUE_ON_BUFFER(FName);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::NameValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		case EDreamUIEventDelegateParameterType::Text:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, TextValue));
			SET_VALUE_ON_BUFFER(FText);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIEventDelegateCustomization::TextValueChange, ValueHandle, ParamBufferHandle));
			TSharedRef<IEditableTextProperty> EditableTextProperty = MakeShareable(new FDreamUIEditableTextPropertyHandle(ValueHandle.ToSharedRef(), PropertyUtilites));
			const bool bIsMultiLine = EditableTextProperty->IsMultiLineText();
			return 
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SBox)
					.MinDesiredWidth(bIsMultiLine ? 250.f : 125.f)
					.MaxDesiredWidth(600)
					[
						SNew(STextPropertyEditableTextBox, EditableTextProperty)
						.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
						.AutoWrapText(true)
					]
				]
				;
		}
		case EDreamUIEventDelegateParameterType::PointerEvent:
		{
			ClearValueBuffer(InDataContainerHandle);
			ClearReferenceValue(InDataContainerHandle);
			return
				SNew(SBox)
				.MinDesiredWidth(500)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PointerEventDataNotEditableError", "(PointerEventData not editable! You can only pass native parameter!)"))
					.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
					.AutoWrapText(true)
					.ColorAndOpacity(FLinearColor(FColor(255, 0, 0, 255)))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		break;
		case EDreamUIEventDelegateParameterType::Asset:
		case EDreamUIEventDelegateParameterType::DreamWidget:
		case EDreamUIEventDelegateParameterType::Class:
		{
			return
				SNew(SBox)
				.MinDesiredWidth(500)
				[
					DrawFunctionReferenceParameter(InDataContainerHandle, InFunctionParameterType, InFunction)
				];
		}
		break;
		case EDreamUIEventDelegateParameterType::Rotator:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, RotatorValue));
			SET_VALUE_ON_BUFFER(FRotator);
			TSharedPtr<INumericTypeInterface<float>> TypeInterface;
			if (FUnitConversion::Settings().ShouldDisplayUnits())
			{
				TypeInterface = MakeShareable(new TNumericUnitTypeInterface<float>(EUnit::Degrees));
			}
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SRotatorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.Roll(this, &FDreamUIEventDelegateCustomization::RotatorGetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Pitch(this, &FDreamUIEventDelegateCustomization::RotatorGetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Yaw(this, &FDreamUIEventDelegateCustomization::RotatorGetItemValue, 2, ValueHandle, ParamBufferHandle)
					.OnRollCommitted(this, &FDreamUIEventDelegateCustomization::RotatorValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnPitchCommitted(this, &FDreamUIEventDelegateCustomization::RotatorValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnYawCommitted(this, &FDreamUIEventDelegateCustomization::RotatorValueChange, 2, ValueHandle, ParamBufferHandle)
					.TypeInterface(TypeInterface)
				]
			;
		}
		break;
		}
	}
	else
	{
		ClearValueBuffer(InDataContainerHandle);
		ClearReferenceValue(InDataContainerHandle);
		return
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("(Not handled)", "(Not handled)"));
	}
}
//function's parameter editor
TSharedRef<SWidget> FDreamUIEventDelegateCustomization::DrawFunctionReferenceParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, EDreamUIEventDelegateParameterType FunctionParameterType, UFunction* InFunction)
{
	auto ParamBufferHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamBuffer));

	TSharedPtr<SWidget> ParameterContent;
	switch (FunctionParameterType)
	{
	case EDreamUIEventDelegateParameterType::Asset:
	{
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SObjectPropertyEntryBox)
			.IsEnabled(true)
			.AllowedClass(UDreamUIEventDelegateParameterHelper::GetObjectParameterClass(InFunction))
			.PropertyHandle(InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject)))
			.AllowClear(true)
			.ToolTipText(LOCTEXT("UObjectTips", "UObject only reference asset, dont use for HelperWidget"))
			.OnObjectChanged(this, &FDreamUIEventDelegateCustomization::ObjectValueChange, ParamBufferHandle, InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject)), true);
	}
	break;
	case EDreamUIEventDelegateParameterType::DreamWidget:
	{
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SObjectPropertyEntryBox)
			.IsEnabled(true)
			.AllowedClass(UDreamUIEventDelegateParameterHelper::GetObjectParameterClass(InFunction))
			.PropertyHandle(InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject)))
			.AllowClear(true)
			.OnObjectChanged(this, &FDreamUIEventDelegateCustomization::ObjectValueChange, ParamBufferHandle, InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject)), false);
	}
	break;
	case EDreamUIEventDelegateParameterType::Class:
	{
		auto MetaClass = UDreamUIEventDelegateParameterHelper::GetClassParameterClass(InFunction);
		auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject));
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SClassPropertyEntryBox)
			.IsEnabled(true)
			.AllowAbstract(true)
			.AllowNone(true)
			.MetaClass(MetaClass)
			.SelectedClass(this, &FDreamUIEventDelegateCustomization::GetClassValue, ValueHandle)
			.OnSetClass(this, &FDreamUIEventDelegateCustomization::ClassValueChange, ValueHandle);
	}
	break;
	default:
		break;
	}
	return 
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("(Not handled)", "(Not handled)"));
}

void FDreamUIEventDelegateCustomization::ObjectValueChange(const FAssetData& InObj, TSharedPtr<IPropertyHandle> BufferHandle, TSharedPtr<IPropertyHandle> ObjectReferenceHandle, bool ObjectOrWidget)
{
	if (ObjectOrWidget)
	{
		//ObjectReference is not for HelperWidget reference
		if (InObj.IsValid() && InObj.GetClass()->IsChildOf(UDreamWidget::StaticClass()))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("Please use DreamWidget type for reference DreamWidget, UObject is for asset object reference"));
			UDreamWidget* NullWidget = nullptr;
			ObjectReferenceHandle->SetValue(NullWidget);
		}
		else
		{
			ObjectReferenceHandle->SetValue(InObj);
		}
	}
	else
	{
		ObjectReferenceHandle->SetValue(InObj);
	}
}
const UClass* FDreamUIEventDelegateCustomization::GetClassValue(TSharedPtr<IPropertyHandle> ClassReferenceHandle)const
{
	UObject* referenceClassObject = nullptr;
	ClassReferenceHandle->GetValue(referenceClassObject);
	return (UClass*)referenceClassObject;
}
void FDreamUIEventDelegateCustomization::ClassValueChange(const UClass* InClass, TSharedPtr<IPropertyHandle> ClassReferenceHandle)
{
	ClassReferenceHandle->SetValue(InClass);
}
void FDreamUIEventDelegateCustomization::EnumValueChange(int32 InValue, ESelectInfo::Type SelectionType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	uint8 Value = (uint8)InValue;
	ValueHandle->SetValue(Value);
	UInt8ValueChange(ValueHandle, BufferHandle);
}

#define SET_BUFFER_ON_VALUE(type)\
type Value;\
ValueHandle->GetValue(Value);\
FBufferArchive ToBinary;\
ToBinary << Value;\
SetBufferValue(BufferHandle, ToBinary);

void FDreamUIEventDelegateCustomization::BoolValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	const bool Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, false);
	TArray<uint8> Buffer;
	Buffer.Add(Value ? 1 : 0);
	SetBufferValue(BufferHandle, Buffer);
}
void FDreamUIEventDelegateCustomization::FloatValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(float);
}
void FDreamUIEventDelegateCustomization::DoubleValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(double);
}
void FDreamUIEventDelegateCustomization::Int8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int8);
}
void FDreamUIEventDelegateCustomization::UInt8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint8);
}
void FDreamUIEventDelegateCustomization::Int16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int16);
}
void FDreamUIEventDelegateCustomization::UInt16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint16);
}
void FDreamUIEventDelegateCustomization::Int32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int32);
}
void FDreamUIEventDelegateCustomization::UInt32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint32);
}
void FDreamUIEventDelegateCustomization::Int64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int64);
}
void FDreamUIEventDelegateCustomization::UInt64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint64);
}
void FDreamUIEventDelegateCustomization::StringValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FString);
}
void FDreamUIEventDelegateCustomization::NameValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FName);
}
void FDreamUIEventDelegateCustomization::TextValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FText);
}
void FDreamUIEventDelegateCustomization::Vector2ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FVector2D Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector2D::ZeroVector);
	switch (AxisType)
	{
	case 0:	Value.X = NewValue; break;
	case 1:	Value.Y = NewValue; break;
	}
	ValueHandle->SetValue(Value);
	FBufferArchive ToBinary;
	ToBinary << Value;
	SetBufferValue(BufferHandle, ToBinary);
}
TOptional<float> FDreamUIEventDelegateCustomization::Vector2GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	const FVector2D Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector2D::ZeroVector);
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	}
}
void FDreamUIEventDelegateCustomization::Vector3ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	// WRITES: one axis is set and the pair stored back, so an uninitialised read put stack
	// bytes into every OTHER axis of the saved parameter.
	FVector Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector::ZeroVector);
	switch (AxisType)
	{
	case 0:	Value.X = NewValue; break;
	case 1:	Value.Y = NewValue; break;
	case 2:	Value.Z = NewValue; break;
	}
	ValueHandle->SetValue(Value);
	FBufferArchive ToBinary;
	ToBinary << Value;
	SetBufferValue(BufferHandle, ToBinary);
}
TOptional<float> FDreamUIEventDelegateCustomization::Vector3GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	const FVector Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector::ZeroVector);
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	case 2: return	Value.Z;
	}
}
void FDreamUIEventDelegateCustomization::Vector4ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	// WRITES: one axis is set and the pair stored back, so an uninitialised read put stack
	// bytes into every OTHER axis of the saved parameter.
	FVector4 Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector4(0.0f, 0.0f, 0.0f, 0.0f));
	switch (AxisType)
	{
	case 0:	Value.X = NewValue; break;
	case 1:	Value.Y = NewValue; break;
	case 2:	Value.Z = NewValue; break;
	case 3:	Value.W = NewValue; break;
	}
	ValueHandle->SetValue(Value);
	FBufferArchive ToBinary;
	ToBinary << Value;
	SetBufferValue(BufferHandle, ToBinary);
}
TOptional<float> FDreamUIEventDelegateCustomization::Vector4GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	const FVector4 Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FVector4(0.0f, 0.0f, 0.0f, 0.0f));
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	case 2: return	Value.Z;
	case 3: return	Value.W;
	}
}
FLinearColor FDreamUIEventDelegateCustomization::LinearColorGetValue(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	if (bIsLinearColor)
	{
		FLinearColor Value;
		FString FormatedString;
		ValueHandle->GetValueAsFormattedString(FormatedString);
		Value.InitFromString(FormatedString);
		return Value;
	}
	else
	{
		FColor Value;
		FString FormatedString;
		ValueHandle->GetValueAsFormattedString(FormatedString);
		Value.InitFromString(FormatedString);
		return FLinearColor(Value.R / 255.0f, Value.G / 255.0f, Value.B / 255.0f, Value.A / 255.0f);
	}
}
void FDreamUIEventDelegateCustomization::LinearColorValueChange(FLinearColor NewValue, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	if (bIsLinearColor)
	{
		FString FormatedString = NewValue.ToString();
		ValueHandle->SetValueFromFormattedString(FormatedString);
		FBufferArchive ToBinary;
		ToBinary << NewValue;
		SetBufferValue(BufferHandle, ToBinary);
	}
	else
	{
		FColor ColorValue = NewValue.ToFColor(false);
		FString FormatedString = ColorValue.ToString();
		ValueHandle->SetValueFromFormattedString(FormatedString);
		FBufferArchive ToBinary;
		ToBinary << ColorValue;
		SetBufferValue(BufferHandle, ToBinary);
	}
}
FReply FDreamUIEventDelegateCustomization::OnMouseButtonDownColorBlock(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	CreateColorPicker(bIsLinearColor, ValueHandle, BufferHandle);

	return FReply::Handled();
}
TOptional<float> FDreamUIEventDelegateCustomization::RotatorGetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	const FRotator Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FRotator::ZeroRotator);
	switch (AxisType)
	{
	default:
	case 0: return	Value.Roll;
	case 1: return	Value.Pitch;
	case 2: return	Value.Yaw;
	}
}
void FDreamUIEventDelegateCustomization::RotatorValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	// WRITES: one axis is set and the pair stored back, so an uninitialised read put stack
	// bytes into every OTHER axis of the saved parameter.
	FRotator Value = DreamDetailsMultiSelect::ValueOr(ValueHandle, FRotator::ZeroRotator);
	switch (AxisType)
	{
	case 0:	Value.Roll = NewValue; break;
	case 1:	Value.Pitch = NewValue; break;
	case 2:	Value.Yaw = NewValue; break;
	}
	ValueHandle->SetValue(Value);
	FBufferArchive ToBinary;
	ToBinary << Value;
	SetBufferValue(BufferHandle, ToBinary);
}
void FDreamUIEventDelegateCustomization::SetBufferValue(TSharedPtr<IPropertyHandle> BufferHandle, const TArray<uint8>& BufferArray)
{
	auto BufferArrayHandle = BufferHandle->AsArray();
	auto bufferCount = BufferArray.Num();
	uint32 bufferHandleCount;
	BufferArrayHandle->GetNumElements(bufferHandleCount);
	if (bufferCount != (int32)bufferHandleCount)
	{
		BufferArrayHandle->EmptyArray();
		for (int i = 0; i < bufferCount; i++)
		{
			BufferArrayHandle->AddItem();

			auto bufferHandle = BufferArrayHandle->GetElement(i);
			auto buffer = BufferArray[i];
			bufferHandle->SetValue(buffer);
		}
	}
	else
	{
		for (int i = 0; i < bufferCount; i++)
		{
			auto bufferHandle = BufferArrayHandle->GetElement(i);
			auto buffer = BufferArray[i];
			bufferHandle->SetValue(buffer);
		}
	}
}

namespace DreamUIEventDelegateBuffer
{
	/**
	 * ParamBuffer is scratch storage the panel keeps in sync while it lays itself out: before a mirror
	 * value can be decoded the buffer has to be exactly as long as the selected function's parameter
	 * needs. That bookkeeping must not go through IPropertyHandleArray - AddItem()/EmptyArray() reach
	 * FPropertyValueImpl::AddChild/ClearChildren (PropertyHandleImpl.cpp:1272/1487), both of which open
	 * their own FScopedTransaction and take no EPropertyValueSetFlags, so merely drawing the panel
	 * dirtied the asset and pushed an "Add Child" entry onto the undo stack. Reaching the raw TArray
	 * has none of those side effects; genuine user edits still go through the property handles.
	 */
	static const FArrayProperty* ResolveByteArrayProperty(const TSharedPtr<IPropertyHandle>& BufferHandle)
	{
		const FArrayProperty* ArrayProperty = BufferHandle.IsValid() ? CastField<FArrayProperty>(BufferHandle->GetProperty()) : nullptr;
		if (ArrayProperty == nullptr || ArrayProperty->Inner == nullptr || ArrayProperty->Inner->GetElementSize() != (int32)sizeof(uint8))
		{
			return nullptr;
		}
		return ArrayProperty;
	}

	/**
	 * Read the buffer straight out of the raw TArray. SetBufferLength resizes it without going through
	 * the property tree, so the cached element handles can still be one layout pass behind.
	 */
	static TArray<uint8> ReadBytes(const TSharedPtr<IPropertyHandle>& BufferHandle)
	{
		TArray<uint8> ResultBuffer;
		const FArrayProperty* ArrayProperty = ResolveByteArrayProperty(BufferHandle);
		if (ArrayProperty == nullptr)
		{
			return ResultBuffer;
		}

		TArray<void*> RawDatas;
		BufferHandle->AccessRawData(RawDatas);
		if (RawDatas.Num() > 0 && RawDatas[0] != nullptr)
		{
			FScriptArrayHelper ArrayHelper(ArrayProperty, RawDatas[0]);
			const int32 NumBytes = ArrayHelper.Num();
			if (NumBytes > 0)
			{
				ResultBuffer.Append(ArrayHelper.GetRawPtr(0), NumBytes);
			}
		}
		return ResultBuffer;
	}
}

void FDreamUIEventDelegateCustomization::SetBufferLength(TSharedPtr<IPropertyHandle> BufferHandle, int32 Count)
{
	const FArrayProperty* ArrayProperty = DreamUIEventDelegateBuffer::ResolveByteArrayProperty(BufferHandle);
	if (ArrayProperty == nullptr)
	{
		return;
	}

	TArray<void*> RawDatas;
	BufferHandle->AccessRawData(RawDatas);

	bool bResized = false;
	for (void* RawData : RawDatas)
	{
		if (RawData == nullptr)
		{
			continue;
		}
		FScriptArrayHelper ArrayHelper(ArrayProperty, RawData);
		if (ArrayHelper.Num() == Count)
		{
			continue;
		}
		//same outcome as the old EmptyArray()+AddItem() loop: every byte of a re-sized buffer starts at zero
		ArrayHelper.EmptyAndAddValues(Count);
		bResized = true;
	}

	if (bResized)
	{
		//the element nodes cached by the property tree no longer match the array. GetBuffer reads the raw
		//array so THIS layout pass is already consistent; this only keeps the tree honest afterwards.
		BufferHandle->RequestRebuildChildren();
	}
}

void FDreamUIEventDelegateCustomization::PrepareParameterBuffer(TSharedPtr<IPropertyHandle> BufferHandle, EDreamUIEventDelegateParameterType InParamType)
{
	const int32 RequiredSize = UDreamUIEventDelegateParameterHelper::GetParameterBufferSize(InParamType);
	if (RequiredSize <= 0)
	{
		//String/Name/Text are serialized at whatever length their value needs, and the reference types
		//keep their value in ReferenceObject. Resizing either would destroy what is stored.
		return;
	}

	const FArrayProperty* ArrayProperty = DreamUIEventDelegateBuffer::ResolveByteArrayProperty(BufferHandle);
	if (ArrayProperty == nullptr)
	{
		return;
	}

	TArray<void*> RawDatas;
	BufferHandle->AccessRawData(RawDatas);

	bool bResized = false;
	for (void* RawData : RawDatas)
	{
		if (RawData == nullptr)
		{
			continue;
		}
		FScriptArrayHelper ArrayHelper(ArrayProperty, RawData);
		if (ArrayHelper.Num() == RequiredSize)
		{
			continue;
		}
		//Read what is stored BEFORE resizing: a buffer written while the math types were single
		//precision holds the author's real value, and clearing it would throw that away silently.
		TArray<uint8> Bytes;
		if (ArrayHelper.Num() > 0)
		{
			Bytes.Append(ArrayHelper.GetRawPtr(0), ArrayHelper.Num());
		}
		UDreamUIEventDelegateParameterHelper::UpgradeParameterBuffer(InParamType, Bytes);
		ArrayHelper.EmptyAndAddValues(RequiredSize);
		if (Bytes.Num() == RequiredSize)
		{
			FMemory::Memcpy(ArrayHelper.GetRawPtr(0), Bytes.GetData(), RequiredSize);
		}
		bResized = true;
	}

	if (bResized)
	{
		//see SetBufferLength: the cached element nodes no longer match, and GetBuffer reads raw
		BufferHandle->RequestRebuildChildren();
	}
}

TArray<uint8> FDreamUIEventDelegateCustomization::GetBuffer(TSharedPtr<IPropertyHandle> BufferHandle)
{
	return DreamUIEventDelegateBuffer::ReadBytes(BufferHandle);
}

TArray<uint8> FDreamUIEventDelegateCustomization::GetPropertyBuffer(TSharedPtr<IPropertyHandle> BufferHandle) const
{
	return DreamUIEventDelegateBuffer::ReadBytes(BufferHandle);
}
int32 FDreamUIEventDelegateCustomization::GetEnumValue(TSharedPtr<IPropertyHandle> ValueHandle)const
{
	uint8 Value = 0;
	ValueHandle->GetValue(Value);
	return Value;
}
FText FDreamUIEventDelegateCustomization::GetTextValue(TSharedPtr<IPropertyHandle> ValueHandle)const
{
	FText Value;
	ValueHandle->GetValue(Value);
	return Value;
}
void FDreamUIEventDelegateCustomization::SetTextValue(const FText& InText, ETextCommit::Type InCommitType, TSharedPtr<IPropertyHandle> ValueHandle)
{
	ValueHandle->SetValue(InText);
}

void FDreamUIEventDelegateCustomization::ClearValueBuffer(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	//layout-time bookkeeping just like SetBufferLength, so it takes the same non-transactional route:
	//emptying through IPropertyHandleArray would open a "Clear Children" transaction for merely drawing
	auto handle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ParamBuffer));
	SetBufferLength(handle, 0);
}
void FDreamUIEventDelegateCustomization::ClearReferenceValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	ClearObjectValue(InItemPropertyHandle);
}
void FDreamUIEventDelegateCustomization::ClearObjectValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	auto handle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ReferenceObject));
	UObject* Obj = nullptr;
	if (handle->GetValue(Obj) == FPropertyAccess::Result::Success && Obj != nullptr)
	{
		//ResetToDefault() opens its own "Reset to Default" transaction and this runs while the panel is
		//being drawn, not on an edit; clearing the reference is what "reset" means here anyway
		handle->SetValue((UObject*)nullptr, EPropertyValueSetFlags::NotTransactable);
	}
}

void FDreamUIEventDelegateCustomization::OnParameterTypeChange(TSharedRef<IPropertyHandle> InItemPropertyHandle)
{
	auto ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, BoolValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, FloatValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, DoubleValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int8Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt8Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int16Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt16Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int32Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt32Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Int64Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, UInt64Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector2Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector3Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, Vector4Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, QuatValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, ColorValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, LinearColorValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIEventDelegateData, RotatorValue)); ValueHandle->ResetToDefault();
}



void FDreamUIEventDelegateCustomization::CreateColorPicker(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FLinearColor InitialColor = LinearColorGetValue(bIsLinearColor, ValueHandle, BufferHandle);

	FColorPickerArgs PickerArgs;
	{
		PickerArgs.bUseAlpha = true;
		PickerArgs.bOnlyRefreshOnMouseUp = false;
		PickerArgs.bOnlyRefreshOnOk = false;
		PickerArgs.sRGBOverride = bIsLinearColor;
		PickerArgs.DisplayGamma = TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(GEngine, &UEngine::GetDisplayGamma));
		PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &FDreamUIEventDelegateCustomization::LinearColorValueChange, bIsLinearColor, ValueHandle, BufferHandle);
		//PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateSP(this, &FColorStructCustomization::OnColorPickerCancelled);
		//PickerArgs.OnInteractivePickBegin = FSimpleDelegate::CreateSP(this, &FColorStructCustomization::OnColorPickerInteractiveBegin);
		//PickerArgs.OnInteractivePickEnd = FSimpleDelegate::CreateSP(this, &FColorStructCustomization::OnColorPickerInteractiveEnd);
		PickerArgs.InitialColor = InitialColor;
		PickerArgs.ParentWidget = ColorPickerParentWidget;
		PickerArgs.OptionalOwningDetailsView = ColorPickerParentWidget;
		FWidgetPath ParentWidgetPath;
		if (FSlateApplication::Get().FindPathToWidget(ColorPickerParentWidget.ToSharedRef(), ParentWidgetPath))
		{
			PickerArgs.bOpenAsMenu = FSlateApplication::Get().FindMenuInWidgetPath(ParentWidgetPath).IsValid();
		}
	}

	OpenColorPicker(PickerArgs);
}


#undef LOCTEXT_NAMESPACE
