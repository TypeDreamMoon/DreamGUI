// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamRectBlockCustomization.h"
#include "DreamDetailsMultiSelect.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamRectBlock.h"
#include "Utils/DreamUIUtils.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "Core/Components/DreamWidget.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "DreamRectBlockCustomization"
FDreamRectBlockCustomization::FDreamRectBlockCustomization()
{
}

FDreamRectBlockCustomization::~FDreamRectBlockCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamRectBlockCustomization::MakeInstance()
{
	return MakeShareable(new FDreamRectBlockCustomization);
}
void FDreamRectBlockCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UDreamRectBlock>(item.Get()))
		{
			TargetScriptArray.Add(TWeakObjectPtr<UDreamRectBlock>(validItem));
			if (validItem->GetWorld() && validItem->GetWorld()->WorldType == EWorldType::Editor)
			{
				validItem->GetWidget()->MarkCanvasUpdate(true);
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	const FMargin OuterPadding(2, 0);
	const FMargin ContentPadding(2);
	auto CreateUnitSelector = [=](TSharedRef<IPropertyHandle> PropertyHandle) {
		return
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(OuterPadding)
		[
			SNew( SCheckBox )
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("Value_Tooltip", "Use direct value"))
			.Padding(ContentPadding)
			.OnCheckStateChanged_Lambda([=](ECheckBoxState InCheckboxState){
				PropertyHandle->SetValue((uint8)EDreamRectBlockUnitMode::Value);
				})
			.IsChecked_Lambda([=] {
				return DreamDetailsMultiSelect::CheckedIfEqual<uint8>(PropertyHandle, (uint8)EDreamRectBlockUnitMode::Value);
				})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Value", "V"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.Padding(OuterPadding)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("Percentage_Tooltip", "Use percentage of rect width and height"))
			.Padding(ContentPadding)
			.OnCheckStateChanged_Lambda([=](ECheckBoxState InCheckboxState) {
			PropertyHandle->SetValue((uint8)EDreamRectBlockUnitMode::Percentage);
				})
			.IsChecked_Lambda([=] {
				return DreamDetailsMultiSelect::CheckedIfEqual<uint8>(PropertyHandle, (uint8)EDreamRectBlockUnitMode::Percentage);
				})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Percentage", "%"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		;
	};

	auto CreateNumericPropertyWithUnitMode = [](TSharedPtr<IPropertyHandle> PropertyHandle, TSharedPtr<IPropertyHandle> UnitModePropertyHandle, bool EnableMinMax)
	{
		auto GetUnitMode = [=]()
		{
			EDreamRectBlockUnitMode UnitMode = EDreamRectBlockUnitMode::Value;
			UnitModePropertyHandle->GetValue(*(uint8*)&UnitMode);
			return UnitMode;
		};
		return
			SNew(SNumericEntryBox<float>)
			.MinValue(EnableMinMax ? 0 : TOptional<float>())
			.MaxValue_Lambda([=]()
			{
				if (!EnableMinMax)return TOptional<float>();
				return GetUnitMode() == EDreamRectBlockUnitMode::Percentage ? 100 : TOptional<float>();
			})
			.AllowSpin(true)
			.MinSliderValue(EnableMinMax ? 0 : TOptional<float>())
			.MaxSliderValue_Lambda([=]()
			{
				if (!EnableMinMax)return TOptional<float>();
				return GetUnitMode() == EDreamRectBlockUnitMode::Percentage ? 100 : TOptional<float>();
			})
			.OnValueChanged_Lambda([=](float Value)
			{
				Value = GetUnitMode() == EDreamRectBlockUnitMode::Percentage ? Value * 0.01f : Value;
				PropertyHandle->SetValue(Value);
			})
			.Value_Lambda([=]()
			{
				float Value = 0;
				if (PropertyHandle->GetValue(Value) == FPropertyAccess::Success)
				{
					Value = GetUnitMode() == EDreamRectBlockUnitMode::Percentage ? Value * 100 : Value;
					return Value;
				}
				return Value;
			});
	};

	auto CreateVectorPropertyWithUnitMode = [&](FName PropertyName, IDetailGroup& Group, FText PropertyDisplayName, const TAttribute<bool>& IsEnabledAttribute, bool EnableMinMax) {
		auto PropertyHandle = DetailBuilder.GetProperty(PropertyName);
		PropertyHandle->SetPropertyDisplayName(PropertyDisplayName);
		auto PropertyUnitHandle = DetailBuilder.GetProperty(FName(*(PropertyName.ToString() + TEXT("UnitMode"))));
		auto ValueHorizontalBox = SNew(SHorizontalBox);
		uint32 NumChildren = 0;
		PropertyHandle->GetNumChildren(NumChildren);
		if (NumChildren == 0)
		{
			ValueHorizontalBox->AddSlot()
			[
				CreateNumericPropertyWithUnitMode(PropertyHandle, PropertyUnitHandle, EnableMinMax)
			];
		}
		else
		{
			for (uint32 i = 0; i < NumChildren; i++)
			{
				ValueHorizontalBox->AddSlot()
				[
					CreateNumericPropertyWithUnitMode(PropertyHandle->GetChildHandle(i), PropertyUnitHandle, EnableMinMax)
				];
			}
		}
		Group.AddWidgetRow()
		.PropertyHandleList({ PropertyHandle })
		.IsEnabled(IsEnabledAttribute)
		.NameContent()
		[
			SNew(SBox)
			.MinDesiredWidth(1000)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					CreateUnitSelector(PropertyUnitHandle)
				]
			]
		]
		.ValueContent()
		[
			ValueHorizontalBox
		]
	;
	};

#define TO_TEXT(x) #x

#define AddPropertyRowToGroup(PropertyName, DisplayName, Group, IsEnabledAttribute)\
auto PropertyName##Handle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, PropertyName));\
PropertyName##Handle->SetPropertyDisplayName(LOCTEXT(TO_TEXT(PropertyName##_DisplayName), TO_TEXT(DisplayName)));\
Group.AddPropertyRow(PropertyName##Handle).IsEnabled(IsEnabledAttribute);

#define AddVectorPropertyRowToGroup(PropertyName, DisplayName, Group, IsEnabledAttribute, EnableMinMax)\
CreateVectorPropertyWithUnitMode(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, PropertyName), Group, LOCTEXT(TO_TEXT(PropertyName##_DisplayName), TO_TEXT(DisplayName)), IsEnabledAttribute, EnableMinMax);
	
	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	
	DetailBuilder.HideCategory(TEXT("DreamGUI-ProceduralRect"));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bUniformSetCornerRadius));

	auto UniformSetCornerRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bUniformSetCornerRadius));
	auto CornerRadiusUnitModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadiusUnitMode));
	auto CornerRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadius));
	auto CornerRadiusXHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadius.X));
	auto CornerRadiusYHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadius.Y));
	auto CornerRadiusZHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadius.Z));
	auto CornerRadiusWHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, CornerRadius.W));
	auto CornerRadiusPropertyIsEnabledFunction = [=] {
		bool bUniformSetCornerRadius = false;
		UniformSetCornerRadiusHandle->GetValue(bUniformSetCornerRadius);
		return !bUniformSetCornerRadius;
	};

	CornerRadiusXHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=] {
		float CornerRadiusX = 0.0f;
		if (DreamDetailsMultiSelect::AllEqual(UniformSetCornerRadiusHandle, true)
			&& CornerRadiusXHandle->GetValue(CornerRadiusX) == FPropertyAccess::Success)
		{
			CornerRadiusYHandle->SetValue(CornerRadiusX);
			CornerRadiusZHandle->SetValue(CornerRadiusX);
			CornerRadiusWHandle->SetValue(CornerRadiusX);
		}
		}));

	DreamGUICategory.AddCustomRow(LOCTEXT("CornerRadius", "CornerRadius"), false)
	.PropertyHandleList({ CornerRadiusHandle, UniformSetCornerRadiusHandle, CornerRadiusUnitModeHandle })
	.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>::CreateLambda([=]()
	{
		return UniformSetCornerRadiusHandle->CanResetToDefault() || CornerRadiusUnitModeHandle->CanResetToDefault() || CornerRadiusHandle->CanResetToDefault();
	}), FSimpleDelegate::CreateLambda([=]()
	{
		UniformSetCornerRadiusHandle->ResetToDefault();
		CornerRadiusUnitModeHandle->ResetToDefault();
		CornerRadiusHandle->ResetToDefault();
	})))
	.NameContent()
	[
		SNew(SBox)
		.MinDesiredWidth(1000)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(CornerRadiusHandle->GetPropertyDisplayName())
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([=] {
					bool bUniformSetCornerRadius = false;
					UniformSetCornerRadiusHandle->GetValue(bUniformSetCornerRadius);
					return bUniformSetCornerRadius ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([=](ECheckBoxState NewState){
					bool bUniformSetCornerRadius = (NewState == ECheckBoxState::Checked) ? true : false;
					UniformSetCornerRadiusHandle->SetValue(bUniformSetCornerRadius);
					})
				.Style(FAppStyle::Get(), "TransparentCheckBox")
				.ToolTipText(LOCTEXT("UniformSetCornerRadiusToolTip", "When locked, corner radius will all set with x value"))
				[
					SNew(SImage)
					.Image_Lambda([=] {
						bool bUniformSetCornerRadius = false;
						UniformSetCornerRadiusHandle->GetValue(bUniformSetCornerRadius);
						return bUniformSetCornerRadius ? FAppStyle::GetBrush(TEXT("Icons.Lock")) : FAppStyle::GetBrush(TEXT("Icons.Unlock"));
						})
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			[
				CreateUnitSelector(CornerRadiusUnitModeHandle)
			]
		]
	]
	.ValueContent()
	.MinDesiredWidth(500)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			CreateNumericPropertyWithUnitMode(CornerRadiusXHandle, CornerRadiusUnitModeHandle, true)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SBox)
			.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
			[
				CreateNumericPropertyWithUnitMode(CornerRadiusYHandle, CornerRadiusUnitModeHandle, true)
			]
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SBox)
			.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
			[
				CreateNumericPropertyWithUnitMode(CornerRadiusZHandle, CornerRadiusUnitModeHandle, true)
			]
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.FillWidth(1)
		[
			SNew(SBox)
			.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
			[
				CreateNumericPropertyWithUnitMode(CornerRadiusWHandle, CornerRadiusUnitModeHandle, true)
			]
		]
	]
	;
	DreamGUICategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bSoftEdge)));

	//body
	auto EnableBodyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBody));
	EnableBodyHandle->SetPropertyDisplayName(LOCTEXT("EnableBody_DisplayName", "Body"));
	auto& BodyGroup = DreamGUICategory.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBody), EnableBodyHandle->GetPropertyDisplayName(), false, true);
	BodyGroup.HeaderProperty(EnableBodyHandle);
	{
		auto EnableBodyAttribute = TAttribute<bool>::CreateLambda([=]()
		{
			bool bEnable = false;
			EnableBodyHandle->GetValue(bEnable);
			return bEnable;
		});
		AddPropertyRowToGroup(BodyColor, Color, BodyGroup, EnableBodyAttribute);

		auto BodyTextureModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyTextureMode));
		// A disagreeing selection gets the first mode, which is the property's own default: the panel
		// has to build one shape or the other, and picking it off the stack is not a choice.
		const auto BodyTextureMode = (EDreamRectBlockTextureMode)DreamDetailsMultiSelect::ValueOr<uint8>(BodyTextureModeHandle, 0);
		BodyTextureModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&DetailBuilder] {
			DetailBuilder.ForceRefreshDetails();
			}));
		auto BodyTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyTexture));
		BodyTextureHandle->SetPropertyDisplayName(LOCTEXT("BodyTexture_DisplayName", "Texture"));
		auto BodySpriteTextureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodySpriteTexture));
		BodySpriteTextureHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
			for (auto item : TargetScriptArray)
			{
				if (item.IsValid())
				{
					item->OnPreChangeSpriteProperty();
				}
			}
			}));
		BodySpriteTextureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
			for (auto item : TargetScriptArray)
			{
				if (item.IsValid())
				{
					item->OnPostChangeSpriteProperty();
				}
			}
			}));
		BodySpriteTextureHandle->SetPropertyDisplayName(LOCTEXT("BodySpriteTexture_DisplayName", "Sprite"));
		auto& TextureGroup = BodyTextureMode == EDreamRectBlockTextureMode::Texture
			? BodyGroup.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyTexture), BodyTextureHandle->GetPropertyDisplayName(), true)
			: BodyGroup.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodySpriteTexture), BodySpriteTextureHandle->GetPropertyDisplayName(), true)
			;
		auto TempBodyTextureHandle = BodyTextureMode == EDreamRectBlockTextureMode::Texture ? BodyTextureHandle : BodySpriteTextureHandle;
		TextureGroup.HeaderRow()
			.PropertyHandleList({ BodyTextureModeHandle, BodyTextureHandle, BodySpriteTextureHandle })
			.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>::CreateLambda([=]()
			{
				return BodyTextureModeHandle->CanResetToDefault() || BodyTextureHandle->CanResetToDefault() || BodySpriteTextureHandle->CanResetToDefault();
			}), FSimpleDelegate::CreateLambda([=]()
			{
				BodyTextureModeHandle->ResetToDefault();
				BodyTextureHandle->ResetToDefault();
				BodySpriteTextureHandle->ResetToDefault();
			})))
			.NameContent()
			[
				SNew(SBox)
				.MinDesiredWidth(1000)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						TempBodyTextureHandle->CreatePropertyNameWidget()
					]
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(EVerticalAlignment::VAlign_Center)
						.Padding(OuterPadding)
						[
							SNew( SCheckBox )
							.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
							.ToolTipText(LOCTEXT("Texture_Tooltip", "Use texture"))
							.Padding(ContentPadding)
							.OnCheckStateChanged_Lambda([=](ECheckBoxState InCheckboxState){
								BodyTextureModeHandle->SetValue((uint8)EDreamRectBlockTextureMode::Texture);
								})
							.IsChecked_Lambda([=] {
								return DreamDetailsMultiSelect::CheckedIfEqual<uint8>(BodyTextureModeHandle, (uint8)EDreamRectBlockTextureMode::Texture);
								})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Texture", "T"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(EVerticalAlignment::VAlign_Center)
						.Padding(OuterPadding)
						[
							SNew(SCheckBox)
							.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
							.ToolTipText(LOCTEXT("Sprite_Tooltip", "Use sprite"))
							.Padding(ContentPadding)
							.OnCheckStateChanged_Lambda([=](ECheckBoxState InCheckboxState) {
								BodyTextureModeHandle->SetValue((uint8)EDreamRectBlockTextureMode::Sprite);
								})
							.IsChecked_Lambda([=] {
								return DreamDetailsMultiSelect::CheckedIfEqual<uint8>(BodyTextureModeHandle, (uint8)EDreamRectBlockTextureMode::Sprite);
								})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Sprite", "S"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
				]
			]
			.ValueContent()
			[
				TempBodyTextureHandle->CreatePropertyValueWidget()
			]
			.IsEnabled(EnableBodyAttribute)
		;
		TextureGroup.AddWidgetRow()
			.ValueContent()
			[
				SNew(SButton)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SnapSize_Button", "Snap Size"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.OnClicked_Lambda([=, this]()
				{
					GEditor->BeginTransaction(LOCTEXT("TextureSnapSize_Transaction", "UIProceduralRect texture snap size"));
					for (auto item : TargetScriptArray)
					{
						if (item.IsValid())
						{
							item->Modify();
							item->SetSizeFromBodyTexture();
							FDreamUIUtils::NotifyPropertyChanged(item.Get(), UDreamWidget::GetPropertyName_AnchorData());
							item->GetWidget()->MarkCanvasUpdate(true);
						}
					}
					GEditor->EndTransaction();
					return FReply::Handled();
				})
			]
			.IsEnabled(EnableBodyAttribute)
		;

		auto BodyTextureScaleModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, BodyTextureScaleMode));
		BodyTextureScaleModeHandle->SetPropertyDisplayName(LOCTEXT("BodyTextureScaleMode_DisplayName", "Scale Mode"));
		TextureGroup.AddPropertyRow(BodyTextureScaleModeHandle).IsEnabled(TAttribute<bool>::CreateLambda([=]()
		{
			UObject* BodyTexture = nullptr;
			TempBodyTextureHandle->GetValue(BodyTexture);
			return BodyTexture != nullptr && EnableBodyAttribute.Get();
		}));

		//gradient
		auto EnableBodyGradientHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBodyGradient));
		EnableBodyGradientHandle->SetPropertyDisplayName(LOCTEXT("EnableBodyGradient_DisplayName", "Gradient"));
		auto& BodyGradientGroup = BodyGroup.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBodyGradient), EnableBodyGradientHandle->GetPropertyDisplayName(), true);
		BodyGradientGroup.HeaderProperty(EnableBodyGradientHandle).IsEnabled(EnableBodyAttribute);
		{
			auto IsEnableGradientAttribute = TAttribute<bool>::CreateLambda([=]()
			{
				bool bEnable = false;
				EnableBodyGradientHandle->GetValue(bEnable);
				return bEnable && EnableBodyAttribute.Get();
			});
			AddPropertyRowToGroup(BodyGradientColor, Color, BodyGradientGroup, IsEnableGradientAttribute);
			AddVectorPropertyRowToGroup(BodyGradientCenter, Center, BodyGradientGroup, IsEnableGradientAttribute, false);
			AddVectorPropertyRowToGroup(BodyGradientRadius, Radius, BodyGradientGroup, IsEnableGradientAttribute, false);
			AddPropertyRowToGroup(BodyGradientRotation, Rotation, BodyGradientGroup, IsEnableGradientAttribute);
		}
	}

	//border
	auto BorderHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBorder));
	BorderHandle->SetPropertyDisplayName(LOCTEXT("bEnableBorder_DisplayName", "Border"));
	auto& BorderGroup = DreamGUICategory.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBorder), BorderHandle->GetPropertyDisplayName(), false, true);
	BorderGroup.HeaderProperty(BorderHandle);
	{
		auto IsEnableBorderAttribute = TAttribute<bool>::CreateLambda([=]()
		{
			bool bEnable = false;
			BorderHandle->GetValue(bEnable);
			return bEnable;
		});
		AddVectorPropertyRowToGroup(BorderWidth, Width, BorderGroup, IsEnableBorderAttribute, true);
		AddPropertyRowToGroup(BorderColor, Color, BorderGroup, IsEnableBorderAttribute);

		//gradient
		auto BorderGradientHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBorderGradient));
		BorderGradientHandle->SetPropertyDisplayName(LOCTEXT("bEnableBorderGradient_DisplayName", "Gradient"));
		auto& BorderGradientGroup = BorderGroup.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBorderGradient), BorderGradientHandle->GetPropertyDisplayName(), true);
		BorderGradientGroup.HeaderProperty(BorderGradientHandle).IsEnabled(IsEnableBorderAttribute);
		{
			auto IsEnableGradientAttribute = TAttribute<bool>::CreateLambda([=]()
			{
				bool bEnableGradient = false;
				BorderGradientHandle->GetValue(bEnableGradient);
				return bEnableGradient && IsEnableBorderAttribute.Get();
			});
			AddPropertyRowToGroup(BorderGradientColor, Color, BorderGradientGroup, IsEnableGradientAttribute);
			AddVectorPropertyRowToGroup(BorderGradientCenter, Center, BorderGradientGroup, IsEnableGradientAttribute, false);
			AddVectorPropertyRowToGroup(BorderGradientRadius, Radius, BorderGradientGroup, IsEnableGradientAttribute, false);
			AddPropertyRowToGroup(BorderGradientRotation, Rotation, BorderGradientGroup, IsEnableGradientAttribute);
		}
	}

	//inner shadow
	auto InnerShadowHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableInnerShadow));
	InnerShadowHandle->SetPropertyDisplayName(LOCTEXT("bEnableInnerShadow_DisplayName", "Inner Shadow"));
	auto& InnerShadowGroup = DreamGUICategory.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableBorder), InnerShadowHandle->GetPropertyDisplayName(), false, true);
	InnerShadowGroup.HeaderProperty(InnerShadowHandle);
	{
		auto IsEnabledAttribute = TAttribute<bool>::CreateLambda([=]()
		{
			bool bEnable = false;
			InnerShadowHandle->GetValue(bEnable);
			return bEnable;
		});
		AddPropertyRowToGroup(InnerShadowColor, Color, InnerShadowGroup, IsEnabledAttribute);
		AddVectorPropertyRowToGroup(InnerShadowSize, Size, InnerShadowGroup, IsEnabledAttribute, true);
		AddVectorPropertyRowToGroup(InnerShadowBlur, Blur, InnerShadowGroup, IsEnabledAttribute, true);
		AddPropertyRowToGroup(InnerShadowAngle, Angle, InnerShadowGroup, IsEnabledAttribute);
		AddVectorPropertyRowToGroup(InnerShadowDistance, Distance, InnerShadowGroup, IsEnabledAttribute, true);
	}

	//outer shadow
	auto OuterShadowHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableOuterShadow));
	OuterShadowHandle->SetPropertyDisplayName(LOCTEXT("EnableOuterShadow_DisplayName", "Outer Shadow"));
	auto& OuterShadowGroup = DreamGUICategory.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableOuterShadow), OuterShadowHandle->GetPropertyDisplayName(), false, true);
	OuterShadowGroup.HeaderProperty(OuterShadowHandle);
	{
		auto IsEnabledAttribute = TAttribute<bool>::CreateLambda([=]()
		{
			bool bEnable = false;
			OuterShadowHandle->GetValue(bEnable);
			return bEnable;
		});
		AddPropertyRowToGroup(OuterShadowColor, Color, OuterShadowGroup, IsEnabledAttribute);
		AddVectorPropertyRowToGroup(OuterShadowSize, Size, OuterShadowGroup, IsEnabledAttribute, true);
		AddVectorPropertyRowToGroup(OuterShadowBlur, Blur, OuterShadowGroup, IsEnabledAttribute, true);
		AddPropertyRowToGroup(OuterShadowAngle, Angle, OuterShadowGroup, IsEnabledAttribute);
		AddVectorPropertyRowToGroup(OuterShadowDistance, Distance, OuterShadowGroup, IsEnabledAttribute, true);
	}

	//radial fill
	auto RadialFillHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableRadialFill));
	RadialFillHandle->SetPropertyDisplayName(LOCTEXT("EnableRadialFill_DisplayName", "Radial Fill"));
	auto& RadialFillGroup = DreamGUICategory.AddGroup(GET_MEMBER_NAME_CHECKED(UDreamRectBlock, bEnableRadialFill), RadialFillHandle->GetPropertyDisplayName(), false, true);
	RadialFillGroup.HeaderProperty(RadialFillHandle);
	{
		auto IsEnabledAttribute = TAttribute<bool>::CreateLambda([=]()
		{
			bool bEnable = false;
			RadialFillHandle->GetValue(bEnable);
			return bEnable;
		});
		AddVectorPropertyRowToGroup(RadialFillCenter, Center, RadialFillGroup, IsEnabledAttribute, false);
		AddPropertyRowToGroup(RadialFillRotation, Rotation, RadialFillGroup, IsEnabledAttribute);
		AddPropertyRowToGroup(RadialFillAngle, Angle, RadialFillGroup, IsEnabledAttribute);
	}

	auto TintColorHandle = DetailBuilder.GetProperty(UDreamRectBlock::GetPropertyName_Color(), UDreamVisual::StaticClass());
	TintColorHandle->SetPropertyDisplayName(LOCTEXT("TintColor", "Tint Color"));
	TintColorHandle->SetToolTipText(LOCTEXT("TintColorTooltip", "Known as \"Color\" property in other UI elements. This can tint all color of this UI element. Usually only set alpha value."));
	DreamGUICategory.AddProperty(TintColorHandle);
}
void FDreamRectBlockCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE