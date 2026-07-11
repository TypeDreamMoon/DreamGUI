// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexUIEventDelegateCustomization.h"
#include "LGUIEditorStyle.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyCustomizationHelpers.h"
#include "LexUIEditorUtils.h"
#include "Widget/LexUIVectorInputBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/SWidget.h"
#include "Math/UnitConversion.h"
#include "STextPropertyEditableTextBox.h"
#include "SEnumCombo.h"
#include "Serialization/BufferArchive.h"
#include "LexUIEditableTextPropertyHandle.h"
#include "LGUIEditorModule.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidgetSubObjectBehaviour.h"
#include "PrefabEditor/LexWidgetHierarchyPickerView.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"

#define LOCTEXT_NAMESPACE "LexUIEventDelegateCustomization"

#define LexUIEventWidgetSelfName "(WidgetSelf)"

TArray<FString> FLexUIEventDelegateCustomization::CopySourceData;

TSharedPtr<IPropertyHandleArray> FLexUIEventDelegateCustomization::GetEventListHandle()const
{
	return PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegate, EventList))->AsArray();
}

void FLexUIEventDelegateCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
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
			.Text(this, &FLexUIEventDelegateCustomization::GetEventTitleName)
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
		auto Item = (FLexUIEventDelegate*)(*Iter);
		EventDelegateInstances[Iter.GetIndex()] = Item;
		for (auto& listItem : Item->EventList)
		{
			listItem.CheckTargetObject();
		}
	}

	World = OutObject->GetWorld();

	auto EventListHandle = GetEventListHandle();
	auto RefreshDelegate = FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::UpdateEventsLayout);
	EventListHandle->SetOnNumElementsChanged(RefreshDelegate);
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegate, SupportParameterType));
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
						.HeightOverride(this, &FLexUIEventDelegateCustomization::GetEventTotalHeight)
						[
							SNew(SImage)
							.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.EventGroup"))
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
								.Text(this, &FLexUIEventDelegateCustomization::GetEventTitleName)
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
											PropertyCustomizationHelpers::MakeAddButton(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::OnClickListAdd))
										]
										+ SHorizontalBox::Slot()
										[
											PropertyCustomizationHelpers::MakeEmptyButton(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::OnClickListEmpty))
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

FText FLexUIEventDelegateCustomization::GetEventTitleName()const
{
	auto EventParameterType = GetNativeParameterType();
	auto NameStr = PropertyHandle->GetPropertyDisplayName().ToString();
	FString ParamTypeString = ULexUIEventDelegateParameterHelper::ParameterTypeToName(EventParameterType, nullptr);
	NameStr = NameStr + "(" + ParamTypeString + ")";
	return FText::FromString(NameStr);
}

FText FLexUIEventDelegateCustomization::GetEventItemFunctionName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
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
			if (ULexUIEventDelegateParameterHelper::IsStillSupported(EventFunction, FunctionParameterType))
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

UObject* FLexUIEventDelegateCustomization::GetEventItemTargetObject(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//TargetObject
	auto TargetObjectHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);
	return TargetObject;
}

FText FLexUIEventDelegateCustomization::GetComponentDisplayName(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	FString ComponentDisplayName;
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	auto HelperWidget = GetEventItemHelperWidget(EventItemPropertyHandle);
	if (TargetObject)
	{
		if (TargetObject == HelperWidget)
		{
			ComponentDisplayName = LexUIEventWidgetSelfName;
		}
		else
		{
			if (Cast<ULexUIBehaviour>(TargetObject) != nullptr || Cast<ULexWidgetSubObjectBehaviour>(TargetObject) != nullptr)
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

EVisibility FLexUIEventDelegateCustomization::GetNativeParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
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
		auto UseNativeParameterHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, bUseNativeParameter));
		UseNativeParameterHandle->GetValue(bUseNativeParameter);

		if ((EventParameterType == FunctionParameterType) && bUseNativeParameter)//support native parameter
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

EVisibility FLexUIEventDelegateCustomization::GetDrawFunctionParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
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
		auto UseNativeParameterHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, bUseNativeParameter));
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

EVisibility FLexUIEventDelegateCustomization::GetNotValidParameterWidgetVisibility(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	auto TargetObject = GetEventItemTargetObject(EventItemPropertyHandle);
	//function
	auto FunctionNameHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
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

ULexWidget* FLexUIEventDelegateCustomization::GetEventItemHelperWidget(TSharedRef<IPropertyHandle> EventItemPropertyHandle)const
{
	//HelperWidget
	auto HelperWidgetHandle = EventItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	ULexWidget* HelperWidget = nullptr;
	HelperWidgetHandle->GetValue(*(UObject**)&HelperWidget);
	return HelperWidget;
}

void FLexUIEventDelegateCustomization::UpdateEventsLayout()
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
		auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
		UObject* HelperWidgetObject = nullptr;
		HelperWidgetHandle->GetValue(HelperWidgetObject);
		auto HelperWidget = Cast<ULexWidget>(HelperWidgetObject);

		//TargetObject
		auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
		UObject* TargetObject = nullptr;
		TargetObjectHandle->GetValue(TargetObject);

		UObject* ClassObject = nullptr;
		auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperClass));
		HelperClassHandle->GetValue(ClassObject);
		if (ClassObject != nullptr)
		{
			UClass* ClassValue = Cast<UClass>(ClassObject);
			if (ClassValue == ULexWidget::StaticClass())
			{
				TargetObjectHandle->SetValue(HelperWidget);
			}
			else if (ClassValue->IsChildOf(ULexUIBehaviour::StaticClass()) || ClassValue->IsChildOf(ULexWidgetSubObjectBehaviour::StaticClass()))
			{
				if (HelperWidget != nullptr)
				{
					UObject* FoundTargetObject = nullptr;
					if (ClassValue->IsChildOf(ULexUIBehaviour::StaticClass()))
					{
						auto CompArray = HelperWidget->GetComponents(ClassValue);
						if (CompArray.Num() == 1)
						{
							FoundTargetObject = CompArray[0];
						}
						else if (CompArray.Num() > 1)
						{
							FName HelperComponentName = NAME_None;
							auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperComponentName));
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
					else if (ClassValue->IsChildOf(ULexVisual::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetVisual();
					}
					else if (ClassValue->IsChildOf(ULexLayoutContainer::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetLayoutContainer();
					}
					else if (ClassValue->IsChildOf(ULexLayoutSelf::StaticClass()))
					{
						FoundTargetObject = HelperWidget->GetLayoutSelf();
					}
					if (FoundTargetObject != TargetObject)
					{
						TargetObjectHandle->SetValue(FoundTargetObject);
						TargetObject = FoundTargetObject;
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

		HelperWidgetHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::OnHelperWidgetParameterChanged, ItemPropertyHandle));
			
		//function
		auto FunctionNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
		FName FunctionFName;
		FunctionNameHandle->GetValue(FunctionFName);
		//parameterType
		auto paramTypeHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamType));
		paramTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::OnParameterTypeChange, ItemPropertyHandle));
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
			auto UseNativeParameterHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, bUseNativeParameter));
			UseNativeParameterHandle->GetValue(bUseNativeParameter);
				
			if (EventParameterType != FunctionParameterType)//check "bUseNativeParameter" parameter
			{
				if (bUseNativeParameter)
				{
					bUseNativeParameter = false;
					UseNativeParameterHandle->SetValue(bUseNativeParameter);
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickCopyPaste, true, EventItemIndex)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickCopyPaste, false, EventItemIndex)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickDuplicate, EventItemIndex)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickAddRemove, true, EventItemIndex, (int32)arrayCount)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickAddRemove, false, EventItemIndex, (int32)arrayCount)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickMoveUpDown, true, EventItemIndex)
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
							.OnClicked(this, &FLexUIEventDelegateCustomization::OnClickMoveUpDown, false, EventItemIndex)
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
							.HeightOverride(this, &FLexUIEventDelegateCustomization::GetEventItemHeight, EventItemIndex)
							[
								SNew(SImage)
								.Image(FLGUIEditorStyle::Get().GetBrush("LGUIEditor.EventItem"))
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
											DrawLexWidgetSelectorForPrefabEditor(EventItemIndex)
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
											.IsEnabled(this, &FLexUIEventDelegateCustomization::IsComponentSelectorMenuEnabled, ItemPropertyHandle)
											.ToolTipText(LOCTEXT("Component", "Pick component for this event"))
											.ButtonContent()
											[
												SNew(STextBlock)
												.Text(this, &FLexUIEventDelegateCustomization::GetComponentDisplayName, ItemPropertyHandle)
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
												.IsEnabled(this, &FLexUIEventDelegateCustomization::IsFunctionSelectorMenuEnabled, ItemPropertyHandle)
												.ToolTipText(LOCTEXT("Function", "Pick a function to execute of this event"))
												.ButtonContent()
												[
													SNew(STextBlock)
													.Text(this, &FLexUIEventDelegateCustomization::GetEventItemFunctionName, ItemPropertyHandle)
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

void FLexUIEventDelegateCustomization::OnHelperWidgetParameterChanged(TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	UObject* HelperWidgetObject = nullptr;
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	auto HelperWidget = Cast<ULexWidget>(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);

	UObject* ClassObject = nullptr;
	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperClass));
	HelperClassHandle->GetValue(ClassObject);
	if (ClassObject != nullptr)
	{
		UClass* ClassValue = Cast<UClass>(ClassObject);
		if (ClassValue == ULexWidget::StaticClass())
		{
			TargetObjectHandle->SetValue(HelperWidget);
		}
		else if (ClassValue->IsChildOf(ULexUIBehaviour::StaticClass()))
		{
			if (HelperWidget != nullptr)
			{
				ULexUIBehaviour* FoundHelperComp = nullptr;
				auto CompArray = HelperWidget->GetComponents(ClassValue);
				if (CompArray.Num() == 1)
				{
					FoundHelperComp = CompArray[0];
				}
				else if (CompArray.Num() > 1)
				{
					FName HelperComponentName = NAME_None;
					auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperComponentName));
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

void FLexUIEventDelegateCustomization::OnSelectWidgetSubObject(ULexWidgetSubObjectBehaviour* SubObj, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(SubObj);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(SubObj->GetClass());

	UpdateEventsLayout();
}

void FLexUIEventDelegateCustomization::OnSelectComponent(ULexUIBehaviour* Comp, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(Comp);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(Comp->GetClass());

	auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperComponentName));
	HelperComponentNameHandle->SetValue(Comp->GetFName());

	UpdateEventsLayout();
}
void FLexUIEventDelegateCustomization::OnSelectWidgetSelf(TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	TargetObjectHandle->SetValue(HelperWidgetObject);

	auto HelperClassHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperClass));
	HelperClassHandle->SetValue(ULexWidget::StaticClass());

	auto HelperComponentNameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperComponentName));
	HelperComponentNameHandle->SetValue(NAME_None);

	UpdateEventsLayout();
}
void FLexUIEventDelegateCustomization::OnSelectFunction(FName FuncName, ELexUIEventDelegateParameterType ParamType, bool UseNativeParameter, TSharedRef<IPropertyHandle> ItemPropertyHandle)
{
	auto nameHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FunctionName));
	nameHandle->SetValue(FuncName);
	SetEventDataParameterType(ItemPropertyHandle, ParamType);
	auto UseNativeParameterHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, bUseNativeParameter));
	UseNativeParameterHandle->SetValue(UseNativeParameter);

	UpdateEventsLayout();
}

void FLexUIEventDelegateCustomization::SetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle, ELexUIEventDelegateParameterType ParameterType)
{
	auto ParamTypeHandle = EventDataItemHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamType));
	ParamTypeHandle->SetValue((uint8)ParameterType);
}
ELexUIEventDelegateParameterType FLexUIEventDelegateCustomization::GetNativeParameterType()const
{
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegate, SupportParameterType));
	uint8 supportParameterTypeUint8;
	NativeParameterTypeHandle->GetValue(supportParameterTypeUint8);
	ELexUIEventDelegateParameterType eventParameterType = (ELexUIEventDelegateParameterType)supportParameterTypeUint8;
	return eventParameterType;
}
void FLexUIEventDelegateCustomization::AddNativeParameterTypeProperty(IDetailChildrenBuilder& ChildBuilder)
{
	auto& Group = ChildBuilder.AddGroup(FName(TEXT("NativeParameterType")), PropertyHandle->GetPropertyDisplayName());
	auto NativeParameterTypeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegate, SupportParameterType));
	Group.AddPropertyRow(NativeParameterTypeHandle.ToSharedRef());
}
ELexUIEventDelegateParameterType FLexUIEventDelegateCustomization::GetEventDataParameterType(TSharedRef<IPropertyHandle> EventDataItemHandle)const
{
	auto paramTypeHandle = EventDataItemHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamType));
	uint8 functionParameterTypeUint8;
	paramTypeHandle->GetValue(functionParameterTypeUint8);
	ELexUIEventDelegateParameterType functionParameterType = (ELexUIEventDelegateParameterType)functionParameterTypeUint8;
	return functionParameterType;
}

TSharedRef<SWidget> FLexUIEventDelegateCustomization::DrawLexWidgetSelectorForPrefabEditor(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	UObject* Object = nullptr;
	if (HelperWidgetHandle->GetValue(Object) != FPropertyAccess::Success)return SNullWidget::NullWidget;
	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
		if (auto Widget = Cast<ULexWidget>(Object))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		else
		{
			auto OuterWidget = Object->GetTypedOuter<ULexWidget>();
			return FText::FromString(Object->GetPathName(OuterWidget));
		}
	};
	auto GetTooltipText = [=, this]()
	{
		if (Object == nullptr)return NoneObjectText;
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
						SNew(SLexWidgetHierarchyPickerView, World.Get(), ULexWidget::StaticClass())
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

TSharedRef<SWidget> FLexUIEventDelegateCustomization::MakeComponentSelectorMenu(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	if (HelperWidgetObject == nullptr)
	{
		return SNew(SBox);
	}

	FMenuBuilder MenuBuilder(true, MakeShareable(new FUICommandList));

	auto HelperWidget = Cast<ULexWidget>(HelperWidgetObject);
	MenuBuilder.AddMenuEntry(
		FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectWidgetSelf, ItemPropertyHandle)),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(LexUIEventWidgetSelfName))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	);
	if (auto Visual = HelperWidget->GetVisual())
	{
		MenuBuilder.AddMenuEntry(
			FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<ULexWidgetSubObjectBehaviour>(Visual), ItemPropertyHandle)),
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
			FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<ULexWidgetSubObjectBehaviour>(LayoutContainer), ItemPropertyHandle)),
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
			FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectWidgetSubObject, Cast<ULexWidgetSubObjectBehaviour>(LayoutSelf), ItemPropertyHandle)),
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
			FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectComponent, Comp, ItemPropertyHandle)),
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
TSharedRef<SWidget> FLexUIEventDelegateCustomization::MakeFunctionSelectorMenu(int32 itemIndex)
{
	auto EventListHandle = GetEventListHandle();
	auto EventParameterType = GetNativeParameterType();
	auto ItemPropertyHandle = EventListHandle->GetElement(itemIndex);
	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
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
		ELexUIEventDelegateParameterType ParamType;
		if (ULexUIEventDelegateParameterHelper::IsSupportedFunction(Func, ParamType))//show only supported type
		{
			FString ParamTypeString = ULexUIEventDelegateParameterHelper::ParameterTypeToName(ParamType, Func);
			auto FunctionSelectorName = FString::Printf(TEXT("%s(%s)"), *Func->GetName(), *ParamTypeString);
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectFunction, Func->GetFName(), ParamType, false, ItemPropertyHandle)),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FunctionSelectorName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			);
			if (ParamType == EventParameterType && EventParameterType != ELexUIEventDelegateParameterType::Empty)//if function support native parameter, then draw another button, and show as native parameter
			{
				FunctionSelectorName = FString::Printf(TEXT("%s(NativeParameter)"), *Func->GetName());
				MenuBuilder.AddMenuEntry(
					FUIAction(FExecuteAction::CreateRaw(this, &FLexUIEventDelegateCustomization::OnSelectFunction, Func->GetFName(), ParamType, true, ItemPropertyHandle)),
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

bool FLexUIEventDelegateCustomization::IsComponentSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);
	return IsValid(HelperWidgetObject);
}
bool FLexUIEventDelegateCustomization::IsFunctionSelectorMenuEnabled(TSharedRef<IPropertyHandle> ItemPropertyHandle)const
{
	auto HelperWidgetHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, HelperWidget));
	UObject* HelperWidgetObject = nullptr;
	HelperWidgetHandle->GetValue(HelperWidgetObject);

	auto TargetObjectHandle = ItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TargetObject));
	UObject* TargetObject = nullptr;
	TargetObjectHandle->GetValue(TargetObject);

	return IsValid(HelperWidgetObject) && IsValid(TargetObject);
}
void FLexUIEventDelegateCustomization::OnClickListAdd()
{
	auto EventListHandle = GetEventListHandle();
	EventListHandle->AddItem();
	UpdateEventsLayout();
}
void FLexUIEventDelegateCustomization::OnClickListEmpty()
{
	auto EventListHandle = GetEventListHandle();
	EventListHandle->EmptyArray();
	UpdateEventsLayout();
}
FReply FLexUIEventDelegateCustomization::OnClickAddRemove(bool AddOrRemove, int32 Index, int32 Count)
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
FReply FLexUIEventDelegateCustomization::OnClickCopyPaste(bool CopyOrPaste, int32 Index)
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

FReply FLexUIEventDelegateCustomization::OnClickDuplicate(int32 Index)
{
	auto EventListHandle = GetEventListHandle();
	auto EventDataHandle = EventListHandle->GetElement(Index);
	EventListHandle->DuplicateItem(Index);
	return FReply::Handled();
}
FReply FLexUIEventDelegateCustomization::OnClickMoveUpDown(bool UpOrDown, int32 Index)
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


#define SET_VALUE_ON_BUFFER(type)\
auto ParamBuffer = GetBuffer(ParamBufferHandle);\
FMemoryReader Reader(ParamBuffer);\
type Value;\
Reader << Value;\
ValueHandle->SetValue(Value);

TSharedRef<SWidget> FLexUIEventDelegateCustomization::DrawFunctionParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, ELexUIEventDelegateParameterType InFunctionParameterType, UFunction* InFunction)
{
	auto ParamBufferHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamBuffer));
	if (InFunctionParameterType != ELexUIEventDelegateParameterType::None)//None means not select function yet
	{
		switch (InFunctionParameterType)
		{
		default:
		case ELexUIEventDelegateParameterType::Empty:
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
		case ELexUIEventDelegateParameterType::Bool:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 1);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, BoolValue));
			auto ParamBuffer = GetBuffer(ParamBufferHandle);
			bool Value = ParamBuffer[0] == 1;
			ValueHandle->SetValue(Value);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::BoolValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Float:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 4);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FloatValue));
			SET_VALUE_ON_BUFFER(float);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::FloatValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Double:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 8);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, DoubleValue));
			SET_VALUE_ON_BUFFER(double);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::DoubleValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Int8:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 1);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int8Value));
			SET_VALUE_ON_BUFFER(int8);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::Int8ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::UInt8:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 1);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt8Value));
			SET_VALUE_ON_BUFFER(uint8);
			if (auto enumValue = ULexUIEventDelegateParameterHelper::GetEnumParameter(InFunction))
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
							.CurrentValue(this, &FLexUIEventDelegateCustomization::GetEnumValue, ValueHandle)
							.OnEnumSelectionChanged(this, &FLexUIEventDelegateCustomization::EnumValueChange, ValueHandle, ParamBufferHandle)
						]
					]
				;
			}
			else
			{
				ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::UInt8ValueChange, ValueHandle, ParamBufferHandle));
				return ValueHandle->CreatePropertyValueWidget();
			}
		}
		break;
		case ELexUIEventDelegateParameterType::Int16:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 2);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int16Value));
			SET_VALUE_ON_BUFFER(int16);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::Int16ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::UInt16:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 2);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt16Value));
			SET_VALUE_ON_BUFFER(uint16);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::UInt16ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Int32:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 4);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int32Value));
			SET_VALUE_ON_BUFFER(int32);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::Int32ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::UInt32:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 4);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt32Value));
			SET_VALUE_ON_BUFFER(uint32);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::UInt32ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Int64:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 8);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int64Value));
			SET_VALUE_ON_BUFFER(int64);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::Int64ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::UInt64:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 8);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt64Value));
			SET_VALUE_ON_BUFFER(uint64);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::UInt64ValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Vector2:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 8);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector2Value));
			SET_VALUE_ON_BUFFER(FVector2D);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SLexUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.ShowX(true)
					.ShowY(true)
					.X(this, &FLexUIEventDelegateCustomization::Vector2GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FLexUIEventDelegateCustomization::Vector2GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FLexUIEventDelegateCustomization::Vector2ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FLexUIEventDelegateCustomization::Vector2ItemValueChange, 1, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case ELexUIEventDelegateParameterType::Vector3:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 12);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector3Value));
			SET_VALUE_ON_BUFFER(FVector);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SLexUIVectorInputBox)
					.AllowSpin(false)
					.bColorAxisLabels(true)
					.EnableX(true)
					.EnableY(true)
					.EnableZ(true)
					.ShowX(true)
					.ShowY(true)
					.ShowZ(true)
					.X(this, &FLexUIEventDelegateCustomization::Vector3GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FLexUIEventDelegateCustomization::Vector3GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FLexUIEventDelegateCustomization::Vector3GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FLexUIEventDelegateCustomization::Vector3ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FLexUIEventDelegateCustomization::Vector3ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FLexUIEventDelegateCustomization::Vector3ItemValueChange, 2, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case ELexUIEventDelegateParameterType::Vector4:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 16);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector4Value));
			SET_VALUE_ON_BUFFER(FVector4);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SLexUIVectorInputBox)
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
					.X(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.W(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 3, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 2, ValueHandle, ParamBufferHandle)
					.OnWCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 3, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case ELexUIEventDelegateParameterType::Color:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 4);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ColorValue));
			auto ParamBuffer = GetBuffer(ParamBufferHandle);
			FMemoryReader Reader(ParamBuffer);
			FColor Value;
			Reader << Value;
			ValueHandle->SetValueFromFormattedString(Value.ToString());
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color with alpha unless it is ignored
					SAssignNew(ColorPickerParentWidget, SColorBlock)
					.Color(this, &FLexUIEventDelegateCustomization::LinearColorGetValue, false, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(true)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Separate)
					.OnMouseButtonDown(this, &FLexUIEventDelegateCustomization::OnMouseButtonDownColorBlock, false, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color without alpha
					SNew(SColorBlock)
					.Color(this, &FLexUIEventDelegateCustomization::LinearColorGetValue, false, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(false)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.OnMouseButtonDown(this, &FLexUIEventDelegateCustomization::OnMouseButtonDownColorBlock, false, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				];
			;
		}
		break;
		case ELexUIEventDelegateParameterType::LinearColor:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 16);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, LinearColorValue));
			SET_VALUE_ON_BUFFER(FLinearColor);
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color with alpha unless it is ignored
					SAssignNew(ColorPickerParentWidget, SColorBlock)
					.Color(this, &FLexUIEventDelegateCustomization::LinearColorGetValue, true, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(true)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Separate)
					.OnMouseButtonDown(this, &FLexUIEventDelegateCustomization::OnMouseButtonDownColorBlock, true, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 2.0f)
				[
					// Displays the color without alpha
					SNew(SColorBlock)
					.Color(this, &FLexUIEventDelegateCustomization::LinearColorGetValue, true, ValueHandle, ParamBufferHandle)
					.ShowBackgroundForAlpha(false)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.OnMouseButtonDown(this, &FLexUIEventDelegateCustomization::OnMouseButtonDownColorBlock, true, ValueHandle, ParamBufferHandle)
					.Size(FVector2D(35.0f, 12.0f))
				];
			;
		}
		break;
		case ELexUIEventDelegateParameterType::Quaternion:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 16);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, QuatValue));
			SET_VALUE_ON_BUFFER(FQuat);
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.FillWidth(1.0f)
				.Padding(0.0f, 2.0f)
				[
					SNew(SLexUIVectorInputBox)
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
					.X(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Y(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Z(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 2, ValueHandle, ParamBufferHandle)
					.W(this, &FLexUIEventDelegateCustomization::Vector4GetItemValue, 3, ValueHandle, ParamBufferHandle)
					.OnXCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnYCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnZCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 2, ValueHandle, ParamBufferHandle)
					.OnWCommitted(this, &FLexUIEventDelegateCustomization::Vector4ItemValueChange, 3, ValueHandle, ParamBufferHandle)
				]
			;
		}
		break;
		case ELexUIEventDelegateParameterType::String:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, StringValue));
			SET_VALUE_ON_BUFFER(FString);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::StringValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		break;
		case ELexUIEventDelegateParameterType::Name:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, NameValue));
			SET_VALUE_ON_BUFFER(FName);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::NameValueChange, ValueHandle, ParamBufferHandle));
			return ValueHandle->CreatePropertyValueWidget();
		}
		case ELexUIEventDelegateParameterType::Text:
		{
			ClearReferenceValue(InDataContainerHandle);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, TextValue));
			SET_VALUE_ON_BUFFER(FText);
			ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIEventDelegateCustomization::TextValueChange, ValueHandle, ParamBufferHandle));
			TSharedRef<IEditableTextProperty> EditableTextProperty = MakeShareable(new FLexUIEditableTextPropertyHandle(ValueHandle.ToSharedRef(), PropertyUtilites));
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
		case ELexUIEventDelegateParameterType::PointerEvent:
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
		case ELexUIEventDelegateParameterType::Asset:
		case ELexUIEventDelegateParameterType::LexWidget:
		case ELexUIEventDelegateParameterType::Class:
		{
			return
				SNew(SBox)
				.MinDesiredWidth(500)
				[
					DrawFunctionReferenceParameter(InDataContainerHandle, InFunctionParameterType, InFunction)
				];
		}
		break;
		case ELexUIEventDelegateParameterType::Rotator:
		{
			ClearReferenceValue(InDataContainerHandle);
			SetBufferLength(ParamBufferHandle, 12);
			auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, RotatorValue));
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
					.Roll(this, &FLexUIEventDelegateCustomization::RotatorGetItemValue, 0, ValueHandle, ParamBufferHandle)
					.Pitch(this, &FLexUIEventDelegateCustomization::RotatorGetItemValue, 1, ValueHandle, ParamBufferHandle)
					.Yaw(this, &FLexUIEventDelegateCustomization::RotatorGetItemValue, 2, ValueHandle, ParamBufferHandle)
					.OnRollCommitted(this, &FLexUIEventDelegateCustomization::RotatorValueChange, 0, ValueHandle, ParamBufferHandle)
					.OnPitchCommitted(this, &FLexUIEventDelegateCustomization::RotatorValueChange, 1, ValueHandle, ParamBufferHandle)
					.OnYawCommitted(this, &FLexUIEventDelegateCustomization::RotatorValueChange, 2, ValueHandle, ParamBufferHandle)
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
TSharedRef<SWidget> FLexUIEventDelegateCustomization::DrawFunctionReferenceParameter(TSharedRef<IPropertyHandle> InDataContainerHandle, ELexUIEventDelegateParameterType FunctionParameterType, UFunction* InFunction)
{
	auto ParamBufferHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamBuffer));

	TSharedPtr<SWidget> ParameterContent;
	switch (FunctionParameterType)
	{
	case ELexUIEventDelegateParameterType::Asset:
	{
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SObjectPropertyEntryBox)
			.IsEnabled(true)
			.AllowedClass(ULexUIEventDelegateParameterHelper::GetObjectParameterClass(InFunction))
			.PropertyHandle(InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject)))
			.AllowClear(true)
			.ToolTipText(LOCTEXT("UObjectTips", "UObject only reference asset, dont use for HelperWidget"))
			.OnObjectChanged(this, &FLexUIEventDelegateCustomization::ObjectValueChange, ParamBufferHandle, InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject)), true);
	}
	break;
	case ELexUIEventDelegateParameterType::LexWidget:
	{
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SObjectPropertyEntryBox)
			.IsEnabled(true)
			.AllowedClass(ULexUIEventDelegateParameterHelper::GetObjectParameterClass(InFunction))
			.PropertyHandle(InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject)))
			.AllowClear(true)
			.OnObjectChanged(this, &FLexUIEventDelegateCustomization::ObjectValueChange, ParamBufferHandle, InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject)), false);
	}
	break;
	case ELexUIEventDelegateParameterType::Class:
	{
		auto MetaClass = ULexUIEventDelegateParameterHelper::GetClassParameterClass(InFunction);
		auto ValueHandle = InDataContainerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject));
		ClearValueBuffer(InDataContainerHandle);
		return SNew(SClassPropertyEntryBox)
			.IsEnabled(true)
			.AllowAbstract(true)
			.AllowNone(true)
			.MetaClass(MetaClass)
			.SelectedClass(this, &FLexUIEventDelegateCustomization::GetClassValue, ValueHandle)
			.OnSetClass(this, &FLexUIEventDelegateCustomization::ClassValueChange, ValueHandle);
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

void FLexUIEventDelegateCustomization::ObjectValueChange(const FAssetData& InObj, TSharedPtr<IPropertyHandle> BufferHandle, TSharedPtr<IPropertyHandle> ObjectReferenceHandle, bool ObjectOrWidget)
{
	if (ObjectOrWidget)
	{
		//ObjectReference is not for HelperWidget reference
		if (InObj.IsValid() && InObj.GetClass()->IsChildOf(ULexWidget::StaticClass()))
		{
			UE_LOG(LGUIEditor, Error, TEXT("Please use LexWidget type for reference LexWidget, UObject is for asset object reference"));
			ULexWidget* NullWidget = nullptr;
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
const UClass* FLexUIEventDelegateCustomization::GetClassValue(TSharedPtr<IPropertyHandle> ClassReferenceHandle)const
{
	UObject* referenceClassObject = nullptr;
	ClassReferenceHandle->GetValue(referenceClassObject);
	return (UClass*)referenceClassObject;
}
void FLexUIEventDelegateCustomization::ClassValueChange(const UClass* InClass, TSharedPtr<IPropertyHandle> ClassReferenceHandle)
{
	ClassReferenceHandle->SetValue(InClass);
}
void FLexUIEventDelegateCustomization::EnumValueChange(int32 InValue, ESelectInfo::Type SelectionType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
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

void FLexUIEventDelegateCustomization::BoolValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	bool Value; 
	ValueHandle->GetValue(Value); 
	TArray<uint8> Buffer;
	Buffer.Add(Value ? 1 : 0);
	SetBufferValue(BufferHandle, Buffer);
}
void FLexUIEventDelegateCustomization::FloatValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(float);
}
void FLexUIEventDelegateCustomization::DoubleValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(double);
}
void FLexUIEventDelegateCustomization::Int8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int8);
}
void FLexUIEventDelegateCustomization::UInt8ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint8);
}
void FLexUIEventDelegateCustomization::Int16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int16);
}
void FLexUIEventDelegateCustomization::UInt16ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint16);
}
void FLexUIEventDelegateCustomization::Int32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int32);
}
void FLexUIEventDelegateCustomization::UInt32ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint32);
}
void FLexUIEventDelegateCustomization::Int64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(int64);
}
void FLexUIEventDelegateCustomization::UInt64ValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(uint64);
}
void FLexUIEventDelegateCustomization::StringValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FString);
}
void FLexUIEventDelegateCustomization::NameValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FName);
}
void FLexUIEventDelegateCustomization::TextValueChange(TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	SET_BUFFER_ON_VALUE(FText);
}
void FLexUIEventDelegateCustomization::Vector2ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FVector2D Value;
	ValueHandle->GetValue(Value);
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
TOptional<float> FLexUIEventDelegateCustomization::Vector2GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	FVector2D Value;
	ValueHandle->GetValue(Value);
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	}
}
void FLexUIEventDelegateCustomization::Vector3ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FVector Value;
	ValueHandle->GetValue(Value);
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
TOptional<float> FLexUIEventDelegateCustomization::Vector3GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	FVector Value;
	ValueHandle->GetValue(Value);
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	case 2: return	Value.Z;
	}
}
void FLexUIEventDelegateCustomization::Vector4ItemValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FVector4 Value;
	ValueHandle->GetValue(Value);
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
TOptional<float> FLexUIEventDelegateCustomization::Vector4GetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	FVector4 Value;
	ValueHandle->GetValue(Value);
	switch (AxisType)
	{
	default:
	case 0: return	Value.X;
	case 1: return	Value.Y;
	case 2: return	Value.Z;
	case 3: return	Value.W;
	}
}
FLinearColor FLexUIEventDelegateCustomization::LinearColorGetValue(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
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
void FLexUIEventDelegateCustomization::LinearColorValueChange(FLinearColor NewValue, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
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
FReply FLexUIEventDelegateCustomization::OnMouseButtonDownColorBlock(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	CreateColorPicker(bIsLinearColor, ValueHandle, BufferHandle);

	return FReply::Handled();
}
TOptional<float> FLexUIEventDelegateCustomization::RotatorGetItemValue(int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)const
{
	FRotator Value;
	ValueHandle->GetValue(Value);
	switch (AxisType)
	{
	default:
	case 0: return	Value.Roll;
	case 1: return	Value.Pitch;
	case 2: return	Value.Yaw;
	}
}
void FLexUIEventDelegateCustomization::RotatorValueChange(float NewValue, ETextCommit::Type CommitInfo, int AxisType, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FRotator Value;
	ValueHandle->GetValue(Value);
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
void FLexUIEventDelegateCustomization::SetBufferValue(TSharedPtr<IPropertyHandle> BufferHandle, const TArray<uint8>& BufferArray)
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

void FLexUIEventDelegateCustomization::SetBufferLength(TSharedPtr<IPropertyHandle> BufferHandle, int32 Count)
{
	auto BufferArrayHandle = BufferHandle->AsArray();
	uint32 bufferHandleCount;
	BufferArrayHandle->GetNumElements(bufferHandleCount);
	if (Count != (int32)bufferHandleCount)
	{
		BufferArrayHandle->EmptyArray();
		for (int i = 0; i < Count; i++)
		{
			BufferArrayHandle->AddItem();
		}
	}
}

TArray<uint8> FLexUIEventDelegateCustomization::GetBuffer(TSharedPtr<IPropertyHandle> BufferHandle)
{
	auto BufferArrayHandle = BufferHandle->AsArray();
	uint32 bufferHandleCount;
	BufferArrayHandle->GetNumElements(bufferHandleCount);
	TArray<uint8> resultBuffer;
	resultBuffer.Reserve(bufferHandleCount);
	for (uint32 i = 0; i < bufferHandleCount; i++)
	{
		auto elementHandle = BufferArrayHandle->GetElement(i);
		uint8 value;
		elementHandle->GetValue(value);
		resultBuffer.Add(value);
	}
	return resultBuffer;
}

TArray<uint8> FLexUIEventDelegateCustomization::GetPropertyBuffer(TSharedPtr<IPropertyHandle> BufferHandle) const
{
	auto paramBufferArrayHandle = BufferHandle->AsArray();
	uint32 bufferCount;
	paramBufferArrayHandle->GetNumElements(bufferCount);
	TArray<uint8> paramBuffer;
	for (uint32 i = 0; i < bufferCount; i++)
	{
		auto bufferHandle = paramBufferArrayHandle->GetElement(i);
		uint8 buffer;
		bufferHandle->GetValue(buffer);
		paramBuffer.Add(buffer);
	}
	return paramBuffer;
}
int32 FLexUIEventDelegateCustomization::GetEnumValue(TSharedPtr<IPropertyHandle> ValueHandle)const
{
	uint8 Value = 0;
	ValueHandle->GetValue(Value);
	return Value;
}
FText FLexUIEventDelegateCustomization::GetTextValue(TSharedPtr<IPropertyHandle> ValueHandle)const
{
	FText Value;
	ValueHandle->GetValue(Value);
	return Value;
}
void FLexUIEventDelegateCustomization::SetTextValue(const FText& InText, ETextCommit::Type InCommitType, TSharedPtr<IPropertyHandle> ValueHandle)
{
	ValueHandle->SetValue(InText);
}

void FLexUIEventDelegateCustomization::ClearValueBuffer(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	auto handle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ParamBuffer))->AsArray();
	uint32 NumElements = 0;
	if (handle->GetNumElements(NumElements) == FPropertyAccess::Result::Success && NumElements > 0)
	{
		handle->EmptyArray();
	}
}
void FLexUIEventDelegateCustomization::ClearReferenceValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	ClearObjectValue(InItemPropertyHandle);
}
void FLexUIEventDelegateCustomization::ClearObjectValue(TSharedPtr<IPropertyHandle> InItemPropertyHandle)
{
	auto handle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ReferenceObject));
	UObject* Obj = nullptr;
	if (handle->GetValue(Obj) == FPropertyAccess::Result::Success && Obj != nullptr)
	{
		handle->ResetToDefault();
	}
}

void FLexUIEventDelegateCustomization::OnParameterTypeChange(TSharedRef<IPropertyHandle> InItemPropertyHandle)
{
	auto ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, BoolValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, FloatValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, DoubleValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int8Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt8Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int16Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt16Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int32Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt32Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Int64Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, UInt64Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector2Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector3Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, Vector4Value)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, QuatValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, ColorValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, LinearColorValue)); ValueHandle->ResetToDefault();
	ValueHandle = InItemPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIEventDelegateData, RotatorValue)); ValueHandle->ResetToDefault();
}



void FLexUIEventDelegateCustomization::CreateColorPicker(bool bIsLinearColor, TSharedPtr<IPropertyHandle> ValueHandle, TSharedPtr<IPropertyHandle> BufferHandle)
{
	FLinearColor InitialColor = LinearColorGetValue(bIsLinearColor, ValueHandle, BufferHandle);

	FColorPickerArgs PickerArgs;
	{
		PickerArgs.bUseAlpha = true;
		PickerArgs.bOnlyRefreshOnMouseUp = false;
		PickerArgs.bOnlyRefreshOnOk = false;
		PickerArgs.sRGBOverride = bIsLinearColor;
		PickerArgs.DisplayGamma = TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(GEngine, &UEngine::GetDisplayGamma));
		PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &FLexUIEventDelegateCustomization::LinearColorValueChange, bIsLinearColor, ValueHandle, BufferHandle);
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
