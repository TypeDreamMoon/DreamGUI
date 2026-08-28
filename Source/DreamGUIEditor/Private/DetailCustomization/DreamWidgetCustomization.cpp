// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DetailCustomization/DreamWidgetCustomization.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "IDetailGroup.h"
#include "DreamGUIEditorStyle.h"
#include "Editor.h"
#include "Widget/ComponentTransformDetails.h"
#include "Widget/AnchorPreviewWidget.h"
#include "PropertyCustomizationHelpers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DreamUIEditorUtils.h"
#include "DreamUIEditorTools.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"
#include "UnrealEdGlobals.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "DetailCustomization/DreamPanelSlotCustomization.h"
#include "Editor/UnrealEdEngine.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"
#include "Utils/DreamUIUtils.h"

#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "UIItemComponentDetails"

class SDreamWidgetSubObjectWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamWidgetSubObjectWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<IPropertyHandle> InPropertyHandle, bool InEditable)
	{
		PropertyHandle = InPropertyHandle;
		auto VisualPropertyValueWidget = InPropertyHandle->CreatePropertyValueWidget();
		if (!InEditable)
		{
			VisualPropertyValueWidget->SetEnabled(false);
		}
		ChildSlot
		[
			VisualPropertyValueWidget
		];
	}

	virtual FReply OnMouseButtonUp(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			OpenContextMenu(MouseEvent);
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
	static TWeakObjectPtr<UObject> CopiedObject;
	
	void OpenContextMenu(const FPointerEvent& MouseEvent)
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyProps", "Copy all properties"),
			LOCTEXT("CopyProps_Tooltip", "You copy all properties of this object then paste it to others"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([=, this]()
			{
				UObject* Object = nullptr;
				PropertyHandle->GetValue(Object);
				if (Object)
				{
					CopiedObject = Object;
				}
			}), FCanExecuteAction::CreateLambda([=, this]()
			{
				UObject* Object = nullptr;
				PropertyHandle->GetValue(Object);
				return Object != nullptr;
			}))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("PasteProps", "Paste all properties"),
			LOCTEXT("PasteProps_Tooltip", "You paste all properties of copied object to this"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([=, this]()
			{
				UObject* Object = nullptr;
				PropertyHandle->GetValue(Object);
				if (Object)
				{
					UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
					Options.bNotifyObjectReplacement = true;
					UEditorEngine::CopyPropertiesForUnrelatedObjects(CopiedObject.Get(), Object, Options);
					Object->PostReinitProperties();
				}
			}), FCanExecuteAction::CreateLambda([=, this]()
			{
				UObject* Object = nullptr;
				PropertyHandle->GetValue(Object);
				return Object != nullptr && CopiedObject.IsValid();
			}))
		);

		FSlateApplication::Get().PushMenu(
			AsShared(),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			MouseEvent.GetScreenSpacePosition(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
		);
	}
};
TWeakObjectPtr<UObject> SDreamWidgetSubObjectWidget::CopiedObject = nullptr;


FDreamWidgetCustomization::FDreamWidgetCustomization()
{
	
}
FDreamWidgetCustomization::~FDreamWidgetCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamWidgetCustomization::MakeInstance()
{
	return MakeShareable(new FDreamWidgetCustomization);
}

FText FDreamWidgetCustomization::GetAnchorsTooltipText()const
{
	return GetLayoutControlAnchorValue().AnyControl() ? LOCTEXT("AnchorIsControlledByLayout", "Anchor is controlled by layout") : LOCTEXT("ChangeAnchor_Tooltip", "Change anchor");
}

void FDreamWidgetCustomization::ForceUpdateUI()
{
	for (auto item : TargetScriptArray)
	{
		if (item.IsValid())
		{
			item->MarkCanvasUpdate(true);
		}
	}
}

/**
 * The prefab's design canvas, on the root widget's details. The toolbar's screen-size menu edits the
 * same numbers; this row is the explicit, always-visible place for them. "Design Screen Size" is the
 * resolution the designer picked; "Canvas Size" is what the root canvas's scale rule makes of it and
 * is what a parentless load sizes the root to (UDreamUIPrefab::CanvasSize).
 */
void FDreamWidgetCustomization::AddCanvasSizeRowsForPrefabRoot(IDetailLayoutBuilder& DetailBuilder)
{
	if (TargetScriptArray.Num() != 1 || !TargetScriptArray[0].IsValid())
	{
		return;
	}
	UDreamWidget* Widget = TargetScriptArray[0].Get();
	const TWeakPtr<FDreamWidgetBlueprintEditor> EditorPtr = FDreamWidgetBlueprintEditor::GetEditorByWorld(Widget->GetWorld());
	FDreamWidgetBlueprintEditor* Editor = EditorPtr.IsValid() ? EditorPtr.Pin().Get() : nullptr;
	if (Editor == nullptr || Editor->GetPreviewRootWidget() != Widget)
	{
		return;
	}
	PrefabEditorForCanvasSize = EditorPtr;

	IDetailCategoryBuilder& CanvasCategory = DetailBuilder.EditCategory(
		"DreamCanvasSize", LOCTEXT("CanvasSizeCategory", "Canvas"), ECategoryPriority::Important);
	CanvasCategory.SetSortOrder(-95);

	auto MakeAxisBox = [this](int32 AxisIndex)
	{
		return SNew(SNumericEntryBox<int32>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.MinValue(1)
			.MinSliderValue(1)
			.Value(this, &FDreamWidgetCustomization::GetDesignScreenSizeAxis, AxisIndex)
			.OnValueCommitted(this, &FDreamWidgetCustomization::OnDesignScreenSizeAxisCommitted, AxisIndex)
			.Label()
			[
				SNumericEntryBox<int32>::BuildNarrowColorLabel(AxisIndex == 0 ? FLinearColor(0.8f, 0.3f, 0.3f) : FLinearColor(0.3f, 0.8f, 0.3f))
			];
	};
	CanvasCategory.AddCustomRow(LOCTEXT("DesignScreenSizeRow", "Design Screen Size"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("DesignScreenSize", "Design Screen Size"))
		.ToolTipText(LOCTEXT("DesignScreenSizeTooltip", "The screen resolution this prefab is designed for. Same as the toolbar's screen-size menu."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(200.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)[ MakeAxisBox(0) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ MakeAxisBox(1) ]
	];
	CanvasCategory.AddCustomRow(LOCTEXT("CanvasSizeRow", "Canvas Size"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CanvasSize", "Canvas Size"))
		.ToolTipText(LOCTEXT("CanvasSizeTooltip", "The design canvas after the root canvas's scale rule. A prefab loaded without a parent widget (a world-space presenter) sizes its root to this."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(STextBlock)
		.Text(this, &FDreamWidgetCustomization::GetCanvasSizeText)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];
}

TOptional<int32> FDreamWidgetCustomization::GetDesignScreenSizeAxis(int32 AxisIndex) const
{
	if (const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = PrefabEditorForCanvasSize.Pin())
	{
		const FIntPoint Size = Editor->GetDesignerViewportSize();
		return AxisIndex == 0 ? Size.X : Size.Y;
	}
	return TOptional<int32>();
}

void FDreamWidgetCustomization::OnDesignScreenSizeAxisCommitted(int32 NewValue, ETextCommit::Type CommitType, int32 AxisIndex)
{
	if (const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = PrefabEditorForCanvasSize.Pin())
	{
		FIntPoint Size = Editor->GetDesignerViewportSize();
		(AxisIndex == 0 ? Size.X : Size.Y) = FMath::Max(1, NewValue);
		Editor->SetDesignerViewportSize(Size);
	}
}

FText FDreamWidgetCustomization::GetCanvasSizeText() const
{
	if (const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = PrefabEditorForCanvasSize.Pin())
	{
		const FIntPoint Size = Editor->GetDesignerCanvasSize();
		return FText::FromString(FString::Printf(TEXT("%d x %d"), Size.X, Size.Y));
	}
	return FText::GetEmpty();
}

void FDreamWidgetCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	bool bIsSubPrefab = false;
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<UDreamWidget>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
			if (ValidItem->GetWorld() != nullptr)
			{
				if (ValidItem->GetWorld()->WorldType == EWorldType::Editor)
				{
					if (auto PrefabHelper = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ValidItem))
					{
						bIsSubPrefab = PrefabHelper->IsWidgetBelongsToSubPrefab(ValidItem);
					}
					ValidItem->MarkCanvasUpdate(true);
				}
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	DetailBuilder.HideCategory("DreamGUI");
	DetailBuilder.HideCategory("Interaction");
	DetailBuilder.HideCategory("Accessibility");
	DetailBuilder.HideCategory("TransformCommon");
	// Marking a property Interp so Sequencer can animate it also implies Edit, which drops the
	// transform properties into an auto-named category. The Layout category already presents them.
	DetailBuilder.HideCategory("DreamWidget");
	IDetailCategoryBuilder& TransformCategory = DetailBuilder.EditCategory(
		"DreamLayout", LOCTEXT("LayoutCategory", "Layout"), ECategoryPriority::Important);
	IDetailCategoryBuilder& BehaviorCategory = DetailBuilder.EditCategory(
		"DreamBehavior", LOCTEXT("BehaviorCategory", "Behavior"), ECategoryPriority::Important);
	IDetailCategoryBuilder& AppearanceCategory = DetailBuilder.EditCategory(
		"DreamAppearance", LOCTEXT("AppearanceCategory", "Appearance"), ECategoryPriority::Default);
	IDetailCategoryBuilder& AccessibilityCategory = DetailBuilder.EditCategory(
		"DreamAccessibility", LOCTEXT("AccessibilityCategory", "Accessibility"), ECategoryPriority::Default);
	TransformCategory.SetSortOrder(-90);
	BehaviorCategory.SetSortOrder(-80);
	AppearanceCategory.SetSortOrder(-70);
	AccessibilityCategory.SetSortOrder(-30);
	AccessibilityCategory.InitiallyCollapsed(true);
	AddCanvasSizeRowsForPrefabRoot(DetailBuilder);

	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData));

	auto DisplayName_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, DisplayName));
	auto WidgetActive_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bWidgetActive));
	auto Visibility_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Visibility));
	auto Interactable_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Interactable));
	auto Raycastable_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Raycastable));
	auto Focusable_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bIsFocusable));
	auto RestrictNavigation_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bRestrictNavigationArea));
	auto Cursor_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Cursor));
	auto ToolTip_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ToolTipText));
	auto RenderOpacity_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderOpacity));
	auto PixelSnapping_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, PixelSnapping));
	auto AccessibleBehavior_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AccessibleBehavior));
	auto AccessibleText_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AccessibleText));
	auto AccessibleSummaryText_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AccessibleSummaryText));
	for (const TSharedPtr<IPropertyHandle>& Property : {
		DisplayName_PH, WidgetActive_PH, Visibility_PH, Interactable_PH, Raycastable_PH,
		Focusable_PH, RestrictNavigation_PH, Cursor_PH, ToolTip_PH, RenderOpacity_PH,
		PixelSnapping_PH, AccessibleBehavior_PH, AccessibleText_PH, AccessibleSummaryText_PH })
	{
		DetailBuilder.HideProperty(Property);
	}

	BehaviorCategory.AddProperty(WidgetActive_PH).DisplayName(LOCTEXT("WidgetActive", "Active"));
	BehaviorCategory.AddProperty(Visibility_PH);
	BehaviorCategory.AddProperty(Interactable_PH);
	BehaviorCategory.AddProperty(Raycastable_PH);
	BehaviorCategory.AddProperty(Focusable_PH).DisplayName(LOCTEXT("Focusable", "Is Focusable"));
	BehaviorCategory.AddProperty(Cursor_PH);
	BehaviorCategory.AddProperty(ToolTip_PH);
	BehaviorCategory.AddProperty(RestrictNavigation_PH, EPropertyLocation::Advanced);
	BehaviorCategory.AddProperty(DisplayName_PH, EPropertyLocation::Advanced);

	AppearanceCategory.AddProperty(RenderOpacity_PH).DisplayName(LOCTEXT("RenderOpacity", "Opacity"));
	AppearanceCategory.AddProperty(PixelSnapping_PH);
	AccessibilityCategory.AddProperty(AccessibleBehavior_PH).DisplayName(LOCTEXT("AccessibleBehavior", "Behavior"));
	AccessibilityCategory.AddProperty(AccessibleText_PH);
	AccessibilityCategory.AddProperty(AccessibleSummaryText_PH);

	auto Clipping_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Clipping));
	DetailBuilder.HideProperty(Clipping_PH);
	auto& ClippingGroup = AppearanceCategory.AddGroup(
		TEXT("ClippingGroup"), LOCTEXT("ClippingGroup", "Clipping"), false, false);
	ClippingGroup.HeaderProperty(Clipping_PH);
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bUniformSetClippingCornerRadius));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius));

		auto UniformSetCornerRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bUniformSetClippingCornerRadius));
		auto CornerRadiusHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius));
		auto CornerRadiusXHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius.X));
		auto CornerRadiusYHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius.Y));
		auto CornerRadiusZHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius.Z));
		auto CornerRadiusWHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius.W));
		auto CornerRadiusPropertyIsEnabledFunction = [=] {
			bool bUniformSetCornerRadius = false;
			UniformSetCornerRadiusHandle->GetValue(bUniformSetCornerRadius);
			return !bUniformSetCornerRadius;
		};

		CornerRadiusXHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=] {
			bool bUniformSetCornerRadius = false;
			UniformSetCornerRadiusHandle->GetValue(bUniformSetCornerRadius);
			if (bUniformSetCornerRadius)
			{
				float CornerRadiusX;
				CornerRadiusXHandle->GetValue(CornerRadiusX);
				CornerRadiusYHandle->SetValue(CornerRadiusX);
				CornerRadiusZHandle->SetValue(CornerRadiusX);
				CornerRadiusWHandle->SetValue(CornerRadiusX);
			}
			}));

		ClippingGroup.AddWidgetRow()
		.PropertyHandleList({ CornerRadiusHandle, UniformSetCornerRadiusHandle })
		.OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>::CreateLambda([=]()
		{
			return UniformSetCornerRadiusHandle->CanResetToDefault() || CornerRadiusHandle->CanResetToDefault();
		}), FSimpleDelegate::CreateLambda([=]()
		{
			UniformSetCornerRadiusHandle->ResetToDefault();
			CornerRadiusHandle->ResetToDefault();
		})))
		.NameContent()
		[
			SNew(SBox)
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
						bool bUniformSetCornerRadius = NewState == ECheckBoxState::Checked;
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
			]
		]
		.ValueContent()
		.MinDesiredWidth(180)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1)
			[
				CornerRadiusXHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
				[
					CornerRadiusYHandle->CreatePropertyValueWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
				[
					CornerRadiusZHandle->CreatePropertyValueWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled_Lambda(CornerRadiusPropertyIsEnabledFunction)
				[
					CornerRadiusWHandle->CreatePropertyValueWidget()
				]
			]
		]
		;
	}
	auto ClippingMargin_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingMargin));
	ClippingGroup.AddPropertyRow(ClippingMargin_PH);

	//anchor, width, height
	{
		auto AnchorHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData));
		auto AnchorMinHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchorMin));
		auto AnchorMaxHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchorMax));
		auto AnchoredPositionHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchoredPosition));
		auto SizeDeltaHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.SizeDelta));
		FVector2D AnchorMin, AnchorMax;
		AnchorMinHandle->GetValue(AnchorMin);
		AnchorMaxHandle->GetValue(AnchorMax);

		//anchors preset menu
		FVector2D anchorItemSize(42, 42);
		float itemBasePadding = 8;
		FMargin AnchorLabelMargin = FMargin(4, 2);
		FMargin AnchorValueMargin = FMargin(2, 2);

		auto MakeAnchorLabelWidget = [&](int AnchorLabelIndex) {
			return
				SNew(SBox)
				.Padding(AnchorLabelMargin)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FDreamWidgetCustomization::GetAnchorLabelText, AnchorMinHandle, AnchorMaxHandle, AnchorLabelIndex)
					.ToolTipText(this, &FDreamWidgetCustomization::GetAnchorLabelTooltipText, AnchorMinHandle, AnchorMaxHandle, AnchorLabelIndex)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			;
		};
		auto DetailBuilderPtr = &DetailBuilder;
		auto MakeAnchorValueWidget = [=, this](int AnchorValueIndex) {
			return
				SNew(SBox)
				.Padding(AnchorValueMargin)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					//GetAnchorPropertyHandle(DetailBuilderPtr, AnchorMinHandle, AnchorMaxHandle, AnchorValueIndex)->CreatePropertyValueWidget()
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.MinSliderValue(this, &FDreamWidgetCustomization::GetMinMaxSliderValue, AnchorHandle, AnchorValueIndex, true)
					.MaxSliderValue(this, &FDreamWidgetCustomization::GetMinMaxSliderValue, AnchorHandle, AnchorValueIndex, false)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.UndeterminedString( NSLOCTEXT( "PropertyEditor", "MultipleValues", "Multiple Values") )
					.Value(this, &FDreamWidgetCustomization::GetAnchorValue, AnchorHandle, AnchorValueIndex)
					.OnValueChanged(this, &FDreamWidgetCustomization::OnAnchorValueChanged, AnchorHandle, AnchorValueIndex)
					.OnValueCommitted(this, &FDreamWidgetCustomization::OnAnchorValueCommitted, AnchorHandle, AnchorValueIndex)
					.OnBeginSliderMovement(this, &FDreamWidgetCustomization::OnAnchorValueSliderMovementBegin)
					.OnEndSliderMovement(this, &FDreamWidgetCustomization::OnAnchorValueSliderMovementEnd, AnchorHandle, AnchorValueIndex)
					.IsEnabled(this, &FDreamWidgetCustomization::IsAnchorValueEnable, AnchorHandle, AnchorValueIndex)
				]
			;
		};
		auto MakeAnchorPreviewWidget = [=, this](DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HAlign, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign VAlign) {
			return
				SNew(DreamGUIAnchorPreviewWidget::SAnchorPreviewWidget, anchorItemSize)
				.BasePadding(itemBasePadding)
				.SelectedHAlign(this, &FDreamWidgetCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
				.SelectedVAlign(this, &FDreamWidgetCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
				.PersistentHAlign(HAlign)
				.PersistentVAlign(VAlign)
				.ButtonEnable(true)
				.OnAnchorChange(this, &FDreamWidgetCustomization::OnSelectAnchor, DetailBuilderPtr)
			;
		};//@todo: auto refresh SAnchorPreviewWidget when change from AnchorMinMax

		// UMG hides the transform of a widget in a non-canvas slot entirely; here the fields stay visible
		// (they show the arranged result) but a banner says who owns them and where to edit instead.
		TransformCategory.AddCustomRow(LOCTEXT("ArrangedByBanner_Filter", "Arranged By"))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(
			this, &FDreamWidgetCustomization::GetArrangedByBannerVisibility)))
		.WholeRowContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 4))
			[
				SNew(STextBlock)
				.Text(this, &FDreamWidgetCustomization::GetArrangedByBannerText)
				.ColorAndOpacity(FLinearColor(1.0f, 0.78f, 0.30f))
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		;

		// Surfaced next to the banner it modulates: checking this opts the widget out of the parent
		// layout's arrangement. It was buried by HideCategory("DreamGUI") and invisible in the panel while
		// silently disabling things like scroll-box content participation.
		TransformCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, bIgnoreLayout)))
			.DisplayName(LOCTEXT("IgnoreLayoutDisplayName", "Ignore Layout"))
			.ToolTip(LOCTEXT("IgnoreLayoutTip",
				"Opt this widget out of its parent layout's arrangement: the panel will neither position nor size it (its authored anchors apply), and panels that arrange content — including Scroll Box — will skip it entirely. Leftover from the old UIScrollView content workflow on many widgets; clear it unless you really mean it."));

		auto SplitLineColor = FLinearColor(0.5f, 0.5f, 0.5f);
		TransformCategory.AddCustomRow(LOCTEXT("Anchor","Anchor"))
		.CopyAction(FUIAction
		(
			FExecuteAction::CreateSP(this, &FDreamWidgetCustomization::OnCopyAnchor),
			FCanExecuteAction::CreateSP(this, &FDreamWidgetCustomization::OnCanCopyAnchor)
		))
		.PasteAction(FUIAction
		(
			FExecuteAction::CreateSP(this, &FDreamWidgetCustomization::OnPasteAnchor, DetailBuilderPtr),
			FCanExecuteAction::CreateSP(this, &FDreamWidgetCustomization::OnCanPasteAnchor)
		))
		.PropertyHandleList({AnchorHandle})
		.ValueContent()
		.MinDesiredWidth(220)
		[
			SNew(SBox)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(0)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(1)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(0)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorValueWidget(1)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(2)
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					[
						MakeAnchorLabelWidget(3)
					]
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.IsEnabled_Lambda([=, this]()
					{
						if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
						{
							auto Widget = TargetScriptArray[0];
							if (Widget->IsCanvasWidget() && Widget->GetRenderCanvas() != nullptr && Widget->GetRenderCanvas()->IsRenderToScreenSpace())//is root canvas, and is render to screen space
							{
								return false;
							}
						}
						return true;
					})
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(0.5f)
						[
							MakeAnchorValueWidget(2)
						]
						+SHorizontalBox::Slot()
						.FillWidth(0.5f)
						[
							MakeAnchorValueWidget(3)
						]
					]
				]
			]
		]
		.NameContent()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			[
				SNew(SBox)
				.Visibility(this, &FDreamWidgetCustomization::GetAnchorPresetButtonVisibility)
				[
					SNew(SComboButton)
					.ContentPadding(8)
					.HasDownArrow(false)
					// Anchors are position-domain data; while a panel arranges this widget the preset
					// would be stomped by the next layout pass, so it is not offered.
					.IsEnabled(this, &FDreamWidgetCustomization::AreAnchorsFreeToEdit)
					.ToolTipText(this, &FDreamWidgetCustomization::GetAnchorsTooltipText)
					.ButtonStyle(FDreamGUIEditorStyle::Get(), "AnchorButton")
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(EHorizontalAlignment::HAlign_Left)
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.Padding(FMargin(0, 0))
							[
								SNew(SBox)
								.Padding(FMargin(0, 0))
								.HAlign(EHorizontalAlignment::HAlign_Center)
								[
									SNew(STextBlock)
									.Text(this, &FDreamWidgetCustomization::GetHAlignText, AnchorMinHandle, AnchorMaxHandle)
									.Font(IDetailLayoutBuilder::GetDetailFont())
								]
							]
							+SVerticalBox::Slot()
							.Padding(FMargin(0, 0))
							.AutoHeight()
							[
								TargetScriptArray[0]->GetParent() != nullptr
								?
								SNew(SBox)
								[
									SNew(DreamGUIAnchorPreviewWidget::SAnchorPreviewWidget, FVector2D(40, 40))
									.BasePadding(0)
									.ButtonEnable(false)
									.PersistentHAlign(this, &FDreamWidgetCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
									.PersistentVAlign(this, &FDreamWidgetCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
									//.SelectedHAlign(this, &FUIItemCustomization::GetAnchorHAlign, AnchorMinHandle, AnchorMaxHandle)
									//.SelectedVAlign(this, &FUIItemCustomization::GetAnchorVAlign, AnchorMinHandle, AnchorMaxHandle)
								]
								:
								SNew(SBox)
							]
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.Padding(FMargin(0, 0))
							.HAlign(EHorizontalAlignment::HAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &FDreamWidgetCustomization::GetVAlignText, AnchorMinHandle, AnchorMaxHandle)
								.Font(IDetailLayoutBuilder::GetDetailFont())
								.Justification(ETextJustify::Center)
								.RenderTransformPivot(FVector2D(0, 0.5f))
								.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90)), FVector2D(-12, -10)))
							]
						]
					]
					.MenuContent()
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SBorder)
							.Padding(4)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnchorPresets", "Anchor Presets"))
								]
								+SVerticalBox::Slot()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("AnchorPresetsHelperKeys", "Shift: Also set pivot		Alt: Also set position"))
									.Font(IDetailLayoutBuilder::GetDetailFont())
								]
							]
						]
						+SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4)
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SOverlay)
									+SOverlay::Slot()
									[
										SNew(SUniformGridPanel)
										+SUniformGridPanel::Slot(1, 0)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(2, 0) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(3, 0) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										+SUniformGridPanel::Slot(4, 0) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None)
										]
										//Top
										+SUniformGridPanel::Slot(0, 1)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(1, 1)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(2, 1) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(3, 1) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										+SUniformGridPanel::Slot(4, 1) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top)
										]
										//Center
										+SUniformGridPanel::Slot(0, 2)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(1, 2)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(2, 2) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(3, 2) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										+SUniformGridPanel::Slot(4, 2) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle)
										]
										//Bottom
										+SUniformGridPanel::Slot(0, 3)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(1, 3)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(2, 3) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(3, 3) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										+SUniformGridPanel::Slot(4, 3) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom)
										]
										//Bottom stretch
										+SUniformGridPanel::Slot(0, 4)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(1, 4)
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(2, 4) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(3, 4) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
										+SUniformGridPanel::Slot(4, 4) 
										[
											MakeAnchorPreviewWidget(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch)
										]
									]
									//split line
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.HAlign(EHorizontalAlignment::HAlign_Left)
										[
											SNew(SBox)
											.WidthOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.HAlign(EHorizontalAlignment::HAlign_Right)
												.WidthOverride(1)
												[
													SNew(SImage)
													.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.HAlign(EHorizontalAlignment::HAlign_Right)
										[
											SNew(SBox)
											.WidthOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.HAlign(EHorizontalAlignment::HAlign_Left)
												.WidthOverride(1)
												[
													SNew(SImage)
													.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.VAlign(EVerticalAlignment::VAlign_Top)
										[
											SNew(SBox)
											.HeightOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.VAlign(EVerticalAlignment::VAlign_Bottom)
												.HeightOverride(1)
												[
													SNew(SImage)
													.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
									+ SOverlay::Slot()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.VAlign(EVerticalAlignment::VAlign_Bottom)
										[
											SNew(SBox)
											.HeightOverride(anchorItemSize.X + 16)
											[
												SNew(SBox)
												.VAlign(EVerticalAlignment::VAlign_Top)
												.HeightOverride(1)
												[
													SNew(SImage)
													.Image(FDreamGUIEditorStyle::Get().GetBrush("DreamGUIEditor.WhiteDot"))
													.ColorAndOpacity(SplitLineColor)
												]
											]
										]
									]
								]
							]
						]
					]
				]
			]
		]
		;

		IDetailGroup& AnchorGroup = TransformCategory.AddGroup(FName("Anchors"), LOCTEXT("AnchorsGroup", "Anchors"));

		const TAttribute<bool> AnchorsFreeToEdit = TAttribute<bool>::Create(
			TAttribute<bool>::FGetter::CreateSP(this, &FDreamWidgetCustomization::AreAnchorsFreeToEdit));
		IDetailPropertyRow& AnchorMinProperty = AnchorGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchorMin)));
		AnchorMinProperty.IsEnabled(AnchorsFreeToEdit);
		if (!this->AreAnchorsFreeToEdit())
		{
			AnchorMinProperty.ToolTip(LOCTEXT("ControlledByLayoutTip", "This property is controlled by layout"));
		}

		IDetailPropertyRow& AnchorMaxProperty = AnchorGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchorMax)));
		AnchorMaxProperty.IsEnabled(AnchorsFreeToEdit);
		if (!this->AreAnchorsFreeToEdit())
		{
			AnchorMaxProperty.ToolTip(LOCTEXT("ControlledByLayoutTip", "This property is controlled by layout"));
		}

		auto& AnchorRawDataGroup = TransformCategory.AddGroup(FName("AnchorsRawData"), LOCTEXT("AnchorsRawData", "AnchorsRawData"), true);
		AnchorRawDataGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnchorRawDataWarning", "Normally do not edit these!"))
				.ColorAndOpacity(FLinearColor(FColor::Yellow))
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
		;
		auto& AnchoredPositionProperty = AnchorRawDataGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchoredPosition)));
		auto& SizeDeltaProperty = AnchorRawDataGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.SizeDelta)));
		AnchoredPositionProperty.IsEnabled(TAttribute<bool>::Create(
			TAttribute<bool>::FGetter::CreateSP(this, &FDreamWidgetCustomization::IsAnchoredPositionRowEnabled)));
		SizeDeltaProperty.IsEnabled(TAttribute<bool>::Create(
			TAttribute<bool>::FGetter::CreateSP(this, &FDreamWidgetCustomization::IsSizeDeltaRowEnabled)));
	}
	//pivot
	auto Pivot_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.Pivot));
	auto& PivotPropertyRow = TransformCategory.AddProperty(Pivot_PH);
	// A one-shot read freezes at whatever was true when the panel was built, and the render mode that
	// decides this can change while the panel is up.
	PivotPropertyRow.IsEnabled(TAttribute<bool>::Create(
		TAttribute<bool>::FGetter::CreateSP(this, &FDreamWidgetCustomization::IsAnchorEditable)));
	Pivot_PH->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPrePivotChange(Pivot_PH);
		}));
	Pivot_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPivotChanged(Pivot_PH);
		}));
	Pivot_PH->SetOnChildPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPrePivotChange(Pivot_PH);
		}));
	Pivot_PH->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		this->OnPivotChanged(Pivot_PH);
		}));

	//location rotation scale
	const FSelectedActorInfo& selectedActorInfo = DetailBuilder.GetDetailsViewSharedPtr()->GetSelectedActorInfo();
	TSharedRef<FComponentTransformDetails> transformDetails = MakeShareable(new FComponentTransformDetails(TargetScriptArray, selectedActorInfo, DetailBuilder));
	TransformCategory.AddCustomBuilder(transformDetails);
	
	//SiblingIndex
	{
		auto SiblingIndex_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, SiblingIndex));
		DetailBuilder.HideProperty(SiblingIndex_PH);
		SiblingIndex_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
			ForceUpdateUI();
			}));
		// Sibling order is authored by dragging in the hierarchy, so this stays a read-out. Any editable
		// control here would need its own transaction over every sibling of every selected widget.
		TransformCategory.AddProperty(SiblingIndex_PH, EPropertyLocation::Advanced).IsEnabled(false);
		TransformCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, FlattenHierarchyIndex)), EPropertyLocation::Advanced);
	}

	//Layout
	{
		auto Layout_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutContainer));
		DetailBuilder.HideProperty(Layout_PH);
		UObject* Layout = nullptr;
		Layout_PH->GetValue(Layout);
		auto& LayoutCategory = DetailBuilder.EditCategory(
			"LayoutContainer", LOCTEXT("PanelCategory", "Panel"), ECategoryPriority::Default);
		LayoutCategory.SetSortOrder(-60);
		LayoutCategory.HeaderContent(SNew(SDreamWidgetSubObjectWidget, Layout_PH, !bIsSubPrefab));
		LayoutCategory.SetIsEmpty(!IsValid(Layout));
		LayoutCategory.AddCustomRow(LOCTEXT("LayoutPlaceholder", "Placeholder"))
			.Visibility(IsValid(Layout) ? EVisibility::Hidden : EVisibility::Visible)
			.NameContent()
			[
				Layout_PH->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				Layout_PH->CreatePropertyValueWidget()
			];
		LayoutCategory.AddExternalObjects({ Layout }, EPropertyLocation::Default
			, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	}

	//LayoutSelf
	{
		auto LayoutSelf_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutSelf));
		DetailBuilder.HideProperty(LayoutSelf_PH);
		UObject* LayoutSelf = nullptr;
		LayoutSelf_PH->GetValue(LayoutSelf);
		auto& LayoutSelfCategory = DetailBuilder.EditCategory(
			"LayoutSelf", LOCTEXT("SelfLayoutCategory", "Self Layout"), ECategoryPriority::Default);
		LayoutSelfCategory.SetSortOrder(-50);
		LayoutSelfCategory.InitiallyCollapsed(!IsValid(LayoutSelf));
		LayoutSelfCategory.HeaderContent(SNew(SDreamWidgetSubObjectWidget, LayoutSelf_PH, !bIsSubPrefab));
		LayoutSelfCategory.SetIsEmpty(!IsValid(LayoutSelf));
		LayoutSelfCategory.AddCustomRow(LOCTEXT("LayoutPlaceholder", "Placeholder"))
			.Visibility(IsValid(LayoutSelf) ? EVisibility::Hidden : EVisibility::Visible)
			.NameContent()
			[
				LayoutSelf_PH->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				LayoutSelf_PH->CreatePropertyValueWidget()
			];
		LayoutSelfCategory.AddExternalObjects({ LayoutSelf }, EPropertyLocation::Default
			, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	}

	// Parent-owned slot, presented as a UMG-style Slot category without an object picker.
	{
		auto PanelSlot_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, PanelSlot));
		DetailBuilder.HideProperty(PanelSlot_PH);
		TArray<UObject*> PanelSlotObjects;
		UDreamLayoutContainer* ParentLayout = nullptr;
		UClass* ParentLayoutClass = nullptr;
		bool bCompatiblePanelContext = true;
		for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : TargetScriptArray)
		{
			UDreamWidget* Widget = WeakWidget.Get();
			UDreamWidget* Parent = IsValid(Widget) ? Widget->GetParent() : nullptr;
			UDreamLayoutContainer* ThisParentLayout = IsValid(Parent) ? Parent->GetLayoutContainer() : nullptr;
			UDreamPanelSlot* Slot = IsValid(Widget) ? Widget->GetPanelSlot() : nullptr;
			if (!IsValid(ThisParentLayout) || !ThisParentLayout->IsA<UDreamPanelLayoutBase>() || !IsValid(Slot))
			{
				bCompatiblePanelContext = false;
				break;
			}
			if (!ParentLayoutClass)
			{
				ParentLayout = ThisParentLayout;
				ParentLayoutClass = ThisParentLayout->GetClass();
			}
			else if (ParentLayoutClass != ThisParentLayout->GetClass())
			{
				bCompatiblePanelContext = false;
				break;
			}
			PanelSlotObjects.Add(Slot);
		}
		const bool bHasPanelSlot = bCompatiblePanelContext && !PanelSlotObjects.IsEmpty()
			&& PanelSlotObjects.Num() == TargetScriptArray.Num();
		FText SlotType = LOCTEXT("PanelSlotType", "Panel");
		if (ParentLayout)
		{
			// The class DisplayName carries the family prefix ("UMG Vertical Box"), and deriving it
			// here keeps the slot header covering every container class without a hand-kept chain.
			SlotType = ParentLayout->GetClass()->GetDisplayNameText();
		}
		auto& PanelSlotCategory = DetailBuilder.EditCategory(
			TEXT("DreamSlot"),
			FText::Format(LOCTEXT("SlotCategoryFormat", "Slot ({0})"), SlotType),
			ECategoryPriority::Important);
		PanelSlotCategory.SetSortOrder(-100);
		PanelSlotCategory.SetIsEmpty(!bHasPanelSlot);
		if (bHasPanelSlot)
		{
			FDreamPanelSlotCustomization::AddSlotProperties(PanelSlotCategory, PanelSlotObjects, ParentLayout);
		}
	}

	//visual
	{
		auto Visual_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, Visual));
		DetailBuilder.HideProperty(Visual_PH);
		UObject* Visual = nullptr;
		Visual_PH->GetValue(Visual);
		IDetailCategoryBuilder& VisualCategory = DetailBuilder.EditCategory("Visual");
		VisualCategory.SetSortOrder(-40);
		VisualCategory.HeaderContent(SNew(SDreamWidgetSubObjectWidget, Visual_PH, !bIsSubPrefab));
		VisualCategory.SetIsEmpty(Visual == nullptr);
		VisualCategory.AddCustomRow(LOCTEXT("VisualPlaceholder", "Placeholder"))
			.Visibility(IsValid(Visual) ? EVisibility::Hidden : EVisibility::Visible)
			.NameContent()
			[
				Visual_PH->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				Visual_PH->CreatePropertyValueWidget()
			]
			;
		VisualCategory.AddExternalObjects({ Visual }, EPropertyLocation::Common
			, FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(false));
	}
}

void FDreamWidgetCustomization::CaptureAnchorOffsets(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets, TArray<FMargin>& OutAnchorOffsets)
{
	OutAnchorOffsets.Reset();
	OutAnchorOffsets.AddDefaulted(Widgets.Num());
	for (int i = 0; i < Widgets.Num(); i++)
	{
		if (!Widgets[i].IsValid())continue;
		OutAnchorOffsets[i].Left = Widgets[i]->GetAnchorOffsetLeft();
		OutAnchorOffsets[i].Top = Widgets[i]->GetAnchorOffsetTop();
		OutAnchorOffsets[i].Right = Widgets[i]->GetAnchorOffsetRight();
		OutAnchorOffsets[i].Bottom = Widgets[i]->GetAnchorOffsetBottom();
	}
}
void FDreamWidgetCustomization::RestoreAnchorOffsets(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets, const TArray<FMargin>& AnchorOffsets)
{
	//nothing was captured for this selection, and replaying another selection's rects would move them
	if (AnchorOffsets.Num() != Widgets.Num())return;
	for (int i = 0; i < Widgets.Num(); i++)
	{
		if (!Widgets[i].IsValid())continue;
		Widgets[i]->SetAnchorOffsetLeft(AnchorOffsets[i].Left);
		Widgets[i]->SetAnchorOffsetTop(AnchorOffsets[i].Top);
		Widgets[i]->SetAnchorOffsetRight(AnchorOffsets[i].Right);
		Widgets[i]->SetAnchorOffsetBottom(AnchorOffsets[i].Bottom);
	}
}
void FDreamWidgetCustomization::OnPrePivotChange(TSharedPtr<IPropertyHandle> PivotPH)
{
	//the pivot write lands on the whole selection, so every widget needs its own rect remembered
	CaptureAnchorOffsets(TargetScriptArray, AnchorOffsetArray);
}
void FDreamWidgetCustomization::OnPivotChanged(TSharedPtr<IPropertyHandle> PivotPH)
{
	RestoreAnchorOffsets(TargetScriptArray, AnchorOffsetArray);
}

EVisibility FDreamWidgetCustomization::GetAnchorPresetButtonVisibility()const
{
	if (TargetScriptArray.Num() > 0 && TargetScriptArray[0].IsValid())
	{
		return TargetScriptArray[0]->GetParent() != nullptr ? EVisibility::Visible : EVisibility::Hidden;
	}
	return EVisibility::Hidden;
}

bool FDreamWidgetCustomization::OnCanCopyAnchor()const
{
	return TargetScriptArray.Num() == 1;
}
#define BEGIN_DreamGUI_AnchorData_CLIPBOARD TEXT("Begin DreamGUI AnchorData")
bool FDreamWidgetCustomization::OnCanPasteAnchor()const
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	return PastedText.StartsWith(BEGIN_DreamGUI_AnchorData_CLIPBOARD);
}
void FDreamWidgetCustomization::OnCopyAnchor()
{
	if (TargetScriptArray.Num() == 1)
	{
		auto script = TargetScriptArray[0];
		if (script.IsValid())
		{
			auto AnchorData = script->GetAnchorData();
			auto CopiedText = FString::Printf(TEXT("%s, PivotX=%f, PivotY=%f\
, AnchorMinX=%f, AnchorMinY=%f, AnchorMaxX=%f, AnchorMaxY=%f\
, AnchoredPositionX=%f, AnchoredPositionY=%f\
, SizeDeltaX=%f, SizeDeltaY=%f")
, BEGIN_DreamGUI_AnchorData_CLIPBOARD
, AnchorData.Pivot.X
, AnchorData.Pivot.Y
, AnchorData.AnchorMin.X
, AnchorData.AnchorMin.Y
, AnchorData.AnchorMax.X
, AnchorData.AnchorMax.Y
, AnchorData.AnchoredPosition.X
, AnchorData.AnchoredPosition.Y
, AnchorData.SizeDelta.X
, AnchorData.SizeDelta.Y
);
			FPlatformApplicationMisc::ClipboardCopy(*CopiedText);
		}
	}
}
void FDreamWidgetCustomization::OnPasteAnchor(IDetailLayoutBuilder* DetailBuilder)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	if (PastedText.StartsWith(BEGIN_DreamGUI_AnchorData_CLIPBOARD))
	{
		FDreamUIAnchorData AnchorData;
		FParse::Value(*PastedText, TEXT("PivotX="), AnchorData.Pivot.X);
		FParse::Value(*PastedText, TEXT("PivotY="), AnchorData.Pivot.Y);
		FParse::Value(*PastedText, TEXT("AnchorMinX="), AnchorData.AnchorMin.X);
		FParse::Value(*PastedText, TEXT("AnchorMinY="), AnchorData.AnchorMin.Y);
		FParse::Value(*PastedText, TEXT("AnchorMaxX="), AnchorData.AnchorMax.X);
		FParse::Value(*PastedText, TEXT("AnchorMaxY="), AnchorData.AnchorMax.Y);
		FParse::Value(*PastedText, TEXT("AnchoredPositionX="), AnchorData.AnchoredPosition.X);
		FParse::Value(*PastedText, TEXT("AnchoredPositionY="), AnchorData.AnchoredPosition.Y);
		FParse::Value(*PastedText, TEXT("SizeDeltaX="), AnchorData.SizeDelta.X);
		FParse::Value(*PastedText, TEXT("SizeDeltaY="), AnchorData.SizeDelta.Y);
		// Pasting an anchor rewrites pivot, both anchors, position and size at once -- the single
		// biggest thing this panel can do to a widget -- and it used to do it outside any
		// transaction, so Ctrl+Z rolled back whatever came before it instead. Same shape as
		// OnSelectAnchor below.
		GEditor->BeginTransaction(LOCTEXT("PasteAnchor_Transaction", "Paste DreamGUI Anchor"));
		for (auto item : TargetScriptArray)
		{
			if (item.IsValid())
			{
				item->Modify();
				item->SetAnchorData(AnchorData);
				FDreamUIUtils::NotifyPropertyChanged(item.Get(), GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData));
				item->MarkPackageDirty();
			}
		}
		GEditor->EndTransaction();
		ForceUpdateUI();
		DetailBuilder->ForceRefreshDetails();
	}
}

bool FDreamWidgetCustomization::IsAnyGeometryAxisArranged()const
{
	const FDreamLayoutControlAnchorData Control = GetLayoutControlAnchorValue();
	return Control.bCanControlHorizontalPosition || Control.bCanControlVerticalPosition
		|| Control.bCanControlHorizontalSize || Control.bCanControlVerticalSize;
}

bool FDreamWidgetCustomization::AreAnchorsFreeToEdit()const
{
	if (!IsAnchorEditable())
	{
		return false;
	}
	const FDreamLayoutControlAnchorData Control = GetLayoutControlAnchorValue();
	return !Control.bCanControlHorizontalPosition && !Control.bCanControlVerticalPosition;
}

bool FDreamWidgetCustomization::IsAnchoredPositionRowEnabled()const
{
	return AreAnchorsFreeToEdit();
}

bool FDreamWidgetCustomization::IsSizeDeltaRowEnabled()const
{
	if (!IsAnchorEditable())
	{
		return false;
	}
	const FDreamLayoutControlAnchorData Control = GetLayoutControlAnchorValue();
	return !Control.bCanControlHorizontalSize && !Control.bCanControlVerticalSize;
}

EVisibility FDreamWidgetCustomization::GetArrangedByBannerVisibility()const
{
	return IsAnyGeometryAxisArranged() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FDreamWidgetCustomization::GetArrangedByBannerText()const
{
	const TArray<FString> Arrangers = CollectArrangerNames(TargetScriptArray);
	if (Arrangers.Num() == 0)
	{
		return FText::GetEmpty();
	}
	return FText::Format(
		LOCTEXT("ArrangedByBannerFormat", "Arranged by {0} — the greyed-out values below are layout results, not yours to edit. Edit the Panel Slot or the layout's own properties instead."),
		FText::FromString(FString::Join(Arrangers, TEXT(" and "))));
}

bool FDreamWidgetCustomization::IsAnchorEditable()const
{
	return IsAnchorEditableForSelection(TargetScriptArray);
}

TSharedPtr<IPropertyHandle> FDreamWidgetCustomization::GetAnchorPropertyHandle(IDetailLayoutBuilder* DetailBuilder, 
	TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int Index) const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return nullptr;

	FVector2D AnchorMinValue;
	FVector2D AnchorMaxValue;
	if (AnchorMinHandle->GetValue(AnchorMinValue) == FPropertyAccess::Success
		&& AnchorMaxHandle->GetValue(AnchorMaxValue) == FPropertyAccess::Success)
	{
		switch (Index)
		{
		case 0://anchored position y, stretch left
			if (AnchorMinValue.X == AnchorMaxValue.X)
				return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchoredPosition.X));
			return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, CacheAnchorOffsetLeft));
		case 1://anchored position z, stretch top
			if (AnchorMinValue.Y == AnchorMaxValue.Y)
				return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.AnchoredPosition.Y));
			return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, CacheAnchorOffsetTop));
		case 2://width, stretch right
			if (AnchorMinValue.X == AnchorMaxValue.X)
				return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.SizeDelta.X));
			return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, CacheAnchorOffsetRight));
		case 3://height, stretch bottom
			if (AnchorMinValue.Y == AnchorMaxValue.Y)
				return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData.SizeDelta.Y));
			return DetailBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidget, CacheAnchorOffsetBottom));
		}
	}
	return nullptr;
}

void FDreamWidgetCustomization::GetAnchorMinMaxForDisplay(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, FVector2D& OutAnchorMin, FVector2D& OutAnchorMax)const
{
	//GetValue leaves a component it cannot agree on untouched, so seed it with the primary selection's own anchors
	const auto& PrimaryAnchorData = TargetScriptArray[0]->GetAnchorData();
	OutAnchorMin = PrimaryAnchorData.AnchorMin;
	OutAnchorMax = PrimaryAnchorData.AnchorMax;
	AnchorMinHandle->GetValue(OutAnchorMin);
	AnchorMaxHandle->GetValue(OutAnchorMax);
}

FText FDreamWidgetCustomization::GetHAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	if (AnchorMinValue.X == AnchorMaxValue.X)
	{
		if (AnchorMinValue.X == 0)
		{
			return LOCTEXT("AnchorLeft", "Left");
		}
		else if (AnchorMinValue.X == 0.5f)
		{
			return LOCTEXT("AnchorCenter", "Center");
		}
		else if (AnchorMinValue.X == 1.0f)
		{
			return LOCTEXT("AnchorRight", "Right");
		}
		else
		{
			return LOCTEXT("AnchorCustom", "Custom");
		}
	}
	else if (AnchorMinValue.X == 0.0f && AnchorMaxValue.X == 1.0f)
	{
		return LOCTEXT("AnchorStretch", "Stretch");
	}
	else
	{
		return LOCTEXT("AnchorCustom", "Custom");
	}
}
FText FDreamWidgetCustomization::GetVAlignText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	if (AnchorMinValue.Y == AnchorMaxValue.Y)
	{
		if (AnchorMinValue.Y == 0)
		{
			return LOCTEXT("AnchorBottom", "Bottom");
		}
		else if (AnchorMinValue.Y == 0.5f)
		{
			return LOCTEXT("AnchorMiddle", "Middle");
		}
		else if (AnchorMinValue.Y == 1.0f)
		{
			return LOCTEXT("AnchorTop", "Top");
		}
		else
		{
			return LOCTEXT("AnchorCustom", "Custom");
		}
	}
	else if (AnchorMinValue.Y == 0.0f && AnchorMaxValue.Y == 1.0f)
	{
		return LOCTEXT("AnchorStretch", "Stretch");
	}
	else
	{
		return LOCTEXT("AnchorCustom", "Custom");
	}
}

FText FDreamWidgetCustomization::GetAnchorLabelText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int LabelIndex)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	switch (LabelIndex)
	{
	case 0://anchored position y, stretch left
	{
		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			return LOCTEXT("AnchoredPositionX", "PosY");
		}
		else
		{
			return LOCTEXT("AnchoredLeft", "Left");
		}
	}
	break;
	case 1://anchored position z, stretch top
	{
		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			return LOCTEXT("AnchoredPositionY", "PosZ");
		}
		else
		{
			return LOCTEXT("AnchoredTop", "Top");
		}
	}
	break;
	case 2://width, stretch right
	{
		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			return LOCTEXT("Width", "Width");
		}
		else
		{
			return LOCTEXT("AnchoredRight", "Right");
		}
	}
	break;
	case 3://height, stretch bottom
	{
		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			return LOCTEXT("Height", "Height");
		}
		else
		{
			return LOCTEXT("AnchoredBottom", "Bottom");
		}
	}
	break;
	}
	return LOCTEXT("AnchorError", "Error");
}

FText FDreamWidgetCustomization::GetAnchorLabelTooltipText(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle, int LabelTooltipIndex)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return FText();

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	switch (LabelTooltipIndex)
	{
	default:
	case 0://anchored position x, stretch left
	{
		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			return FText::Format(LOCTEXT("AnchoredPositionX_Tooltip", "Horizontal anchored position. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchoredPosition)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchoredPosition)));
		}
		else
		{
			return FText::Format(LOCTEXT("AnchoredLeft_Tooltip", "Calculated distance to parent's left anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchorOffsetLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchorOffsetLeft)));
		}
	}
	case 1://anchored position y, stretch top
	{
		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			return FText::Format(LOCTEXT("AnchoredPositionY_Tooltip", "Vertical anchored position. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchoredPosition)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchoredPosition)));
		}
		else
		{
			return FText::Format(LOCTEXT("AnchoredTop_Tooltip", "Calculated distance to parent's top anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchorOffsetLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchorOffsetLeft)));
		}
	}
	case 2://width, stretch right
	{
		if (AnchorMinValue.X == AnchorMaxValue.X)
		{
			return FText::Format(LOCTEXT("Width_Tooltip", "Horizontal size. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetWidth)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetWidth)));
		}
		else
		{
			return FText::Format(LOCTEXT("AnchoredRight_Tooltip", "Calculated distance to parent's right anchor point. Related function: {0} / {1}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchorOffsetLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchorOffsetLeft)));
		}
	}
	case 3://height, stretch bottom
	{
		if (AnchorMinValue.Y == AnchorMaxValue.Y)
		{
			return FText::Format(LOCTEXT("Height_Tooltip", "Vertical size. Related function: {0} / {1}"), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetHeight)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetHeight)));
		}
		else
		{
			return FText::Format(LOCTEXT("AnchoredBottom_Tooltip", "Calculated distance to parent's bottom anchor point. Related function: {0} / {0}."), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, GetAnchorOffsetLeft)), FText::FromString(GET_FUNCTION_NAME_STRING_CHECKED(UDreamWidget, SetAnchorOffsetLeft)));
		}
	}
	}
}

DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign FDreamWidgetCustomization::GetAnchorHAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None;

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign AnchorHAlign = DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None;
	if (AnchorMinValue.X == AnchorMaxValue.X)
	{
		if (AnchorMinValue.X == 0)
		{
			AnchorHAlign = DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left;
		}
		else if (AnchorMinValue.X == 0.5f)
		{
			AnchorHAlign = DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center;
		}
		else if (AnchorMinValue.X == 1.0f)
		{
			AnchorHAlign = DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right;
		}
	}
	else if (AnchorMinValue.X == 0.0f && AnchorMaxValue.X == 1.0f)
	{
		AnchorHAlign = DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch;
	}
	return AnchorHAlign;
}
DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign FDreamWidgetCustomization::GetAnchorVAlign(TSharedRef<IPropertyHandle> AnchorMinHandle, TSharedRef<IPropertyHandle> AnchorMaxHandle)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None;

	FVector2D AnchorMinValue, AnchorMaxValue;
	GetAnchorMinMaxForDisplay(AnchorMinHandle, AnchorMaxHandle, AnchorMinValue, AnchorMaxValue);

	DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign AnchorVAlign = DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None;
	if (AnchorMinValue.Y == AnchorMaxValue.Y)
	{
		if (AnchorMinValue.Y == 0)
		{
			AnchorVAlign = DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom;
		}
		else if (AnchorMinValue.Y == 0.5f)
		{
			AnchorVAlign = DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle;
		}
		else if (AnchorMinValue.Y == 1.0f)
		{
			AnchorVAlign = DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top;
		}
	}
	else if (AnchorMinValue.Y == 0.0f && AnchorMaxValue.Y == 1.0f)
	{
		AnchorVAlign = DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch;
	}
	return AnchorVAlign;
}

void FDreamWidgetCustomization::OnSelectAnchor(DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign HorizontalAlign, DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign VerticalAlign, IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return;

	bool ShiftPressed = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	bool AltPressed = FSlateApplication::Get().GetModifierKeys().IsAltDown();

	GEditor->BeginTransaction(LOCTEXT("ChangeAnchor_Transaction", "Change DreamGUI Anchor"));
	for (auto& UIItem : TargetScriptArray)
	{
		UIItem->Modify();
	}

	for (auto& Widget : TargetScriptArray)
	{
		FVector2D DesiredPivot = Widget->GetPivot();
		auto AnchorMin = Widget->GetAnchorMin();
		auto AnchorMax = Widget->GetAnchorMax();
		switch (HorizontalAlign)
		{
		case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::None:
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left:
		{
			DesiredPivot.X = 0;
			AnchorMin.X = AnchorMax.X = 0;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center:
		{
			DesiredPivot.X = 0.5f;
			AnchorMin.X = AnchorMax.X = 0.5f;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right:
		{
			DesiredPivot.X = 1.0f;
			AnchorMin.X = AnchorMax.X = 1.0f;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch:
		{
			DesiredPivot.X = 0.5f;
			AnchorMin.X = 0;
			AnchorMax.X = 1.0f;
		}
		break;
		}
		switch (VerticalAlign)
		{
		case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::None:
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top:
		{
			DesiredPivot.Y = 1.0f;
			AnchorMin.Y = AnchorMax.Y = 1;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle:
		{
			DesiredPivot.Y = 0.5f;
			AnchorMin.Y = AnchorMax.Y = 0.5f;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom:
		{
			DesiredPivot.Y = 0.0f;
			AnchorMin.Y = AnchorMax.Y = 0.0f;
		}
			break;
		case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch:
		{
			DesiredPivot.Y = 0.5f;
			AnchorMin.Y = 0;
			AnchorMax.Y = 1.0f;
		}
		break;
		}
		auto PrevRelativeLocation = Widget->GetRelativeLocation();
		auto PrevWidth = Widget->GetWidth();
		auto PrevHeight = Widget->GetHeight();
		Widget->SetAnchorData(FDreamUIAnchorData{Widget->GetPivot(), AnchorMin, AnchorMax, Widget->GetAnchoredPosition(), Widget->GetSizeDelta()});
		Widget->MarkAllDirtyRecursive();
		Widget->SetWidth(PrevWidth);
		Widget->SetHeight(PrevHeight);
		Widget->SetRelativeLocation(PrevRelativeLocation);
		if (AltPressed)
		{
			switch (HorizontalAlign)
			{
			case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Left:
			{
				Widget->SetHorizontalAnchoredPosition(-Widget->GetLocalSpaceLeft());
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Center:
			{
				Widget->SetHorizontalAnchoredPosition(Widget->GetWidth() * (Widget->GetPivot().X - 0.5f));
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Right:
			{
				Widget->SetHorizontalAnchoredPosition(-Widget->GetLocalSpaceRight());
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorHorizontalAlign::Stretch:
			{
				Widget->SetAnchorOffsetLeft(0);
				Widget->SetAnchorOffsetRight(0);
			}
				break;
			}
			switch (VerticalAlign)
			{
			case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Top:
			{
				Widget->SetVerticalAnchoredPosition(-Widget->GetLocalSpaceTop());
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Middle:
			{
				Widget->SetVerticalAnchoredPosition(Widget->GetHeight() * (Widget->GetPivot().Y - 0.5f));
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Bottom:
			{
				Widget->SetVerticalAnchoredPosition(-Widget->GetLocalSpaceBottom());
			}
				break;
			case DreamGUIAnchorPreviewWidget::UIAnchorVerticalAlign::Stretch:
			{
				Widget->SetAnchorOffsetBottom(0);
				Widget->SetAnchorOffsetTop(0);
			}
				break;
			}
		}
		if (ShiftPressed)
		{
			FMargin PrevAnchorAsMargin(Widget->GetAnchorOffsetLeft(), Widget->GetAnchorOffsetTop(), Widget->GetAnchorOffsetRight(), Widget->GetAnchorOffsetBottom());
			Widget->SetPivot(DesiredPivot);
			Widget->SetAnchorOffsetLeft(PrevAnchorAsMargin.Left);
			Widget->SetAnchorOffsetRight(PrevAnchorAsMargin.Right);
			Widget->SetAnchorOffsetBottom(PrevAnchorAsMargin.Bottom);
			Widget->SetAnchorOffsetTop(PrevAnchorAsMargin.Top);
		}

		SyncPanelSlotAfterAnchorEdit(Widget.Get());
		FDreamUIUtils::NotifyPropertyChanged(Widget.Get(), GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData));
	}
	TargetScriptArray[0]->MarkCanvasUpdate(true);
	DetailBuilder->GetPropertyUtilities()->RequestForceRefresh();
	GEditor->EndTransaction();
}

void FDreamWidgetCustomization::SyncPanelSlotAfterAnchorEdit(UDreamWidget* Widget)
{
	if (!IsValid(Widget))return;
	if (UDreamPanelSlot* PanelSlot = Widget->GetPanelSlot(); IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredGeometryAfterUserEdit();
	}
}

FDreamLayoutControlAnchorData FDreamWidgetCustomization::FoldLayoutControlAcrossSelection(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets)
{
	FDreamLayoutControlAnchorData Result;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Widgets)
	{
		UDreamWidget* Widget = WeakWidget.Get();
		if (!IsValid(Widget))
		{
			continue;
		}
		if (auto LayoutSelf = Widget->GetLayoutSelf())
		{
			Result.Or(LayoutSelf->GetLayoutControlAnchor(Widget));
		}
		if (auto Parent = Widget->GetParent())
		{
			if (auto ParentLayout = Parent->GetLayoutContainer())
			{
				Result.Or(ParentLayout->GetLayoutControlAnchor(Widget));
			}
		}
	}
	return Result;
}

TArray<FString> FDreamWidgetCustomization::CollectArrangerNames(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets)
{
	TArray<FString> Arrangers;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Widgets)
	{
		UDreamWidget* Widget = WeakWidget.Get();
		if (!IsValid(Widget))
		{
			continue;
		}
		if (UDreamLayoutSelf* LayoutSelf = Widget->GetLayoutSelf();
			IsValid(LayoutSelf) && LayoutSelf->GetLayoutControlAnchor(Widget).AnyControl())
		{
			Arrangers.AddUnique(LayoutSelf->GetClass()->GetDisplayNameText().ToString());
		}
		if (UDreamWidget* Parent = Widget->GetParent(); IsValid(Parent))
		{
			if (UDreamLayoutContainer* ParentLayout = Parent->GetLayoutContainer();
				IsValid(ParentLayout) && ParentLayout->GetLayoutControlAnchor(Widget).AnyControl())
			{
				// Named with the panel it lives on: a selection can span several panels of the same class.
				Arrangers.AddUnique(FString::Printf(TEXT("%s on '%s'"),
					*ParentLayout->GetClass()->GetDisplayNameText().ToString(), *Parent->GetDisplayName()));
			}
		}
	}
	return Arrangers;
}

bool FDreamWidgetCustomization::IsAnchorEditableForSelection(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets)
{
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Widgets)
	{
		UDreamWidget* Widget = WeakWidget.Get();
		if (!IsValid(Widget) || Widget->GetParent() != nullptr)//anything with a parent has a rect to anchor against
		{
			continue;
		}
		if (Widget->IsCanvasWidget() && Widget->GetRenderCanvas() != nullptr && Widget->GetRenderCanvas()->IsRenderToScreenSpace())//is root canvas, and is render to screen space
		{
			return false;
		}
	}
	return true;
}

FDreamLayoutControlAnchorData FDreamWidgetCustomization::GetLayoutControlAnchorValue()const
{
	return FoldLayoutControlAcrossSelection(TargetScriptArray);
}

bool FDreamWidgetCustomization::GetLayoutControlHorizontalAnchoredPosition()const
{
	return GetLayoutControlAnchorValue().bCanControlHorizontalPosition;
}
bool FDreamWidgetCustomization::GetLayoutControlVerticalAnchoredPosition()const
{
	return GetLayoutControlAnchorValue().bCanControlVerticalPosition;
}
bool FDreamWidgetCustomization::GetLayoutControlHorizontalSizeDelta()const
{
	return GetLayoutControlAnchorValue().bCanControlHorizontalSize;
}
bool FDreamWidgetCustomization::GetLayoutControlVerticalSizeDelta()const
{
	return GetLayoutControlAnchorValue().bCanControlVerticalSize;
}

TArray<float> FDreamWidgetCustomization::ValueRangeArray = {
		1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f
};
TOptional<float> FDreamWidgetCustomization::GetMinMaxSliderValue(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex, bool MinOrMax)const
{
	auto Value = GetAnchorValue(AnchorHandle, AnchorValueIndex).Get(0.0f);
	Value = FMath::Abs(Value);
	float MaxRangeValue = ValueRangeArray[ValueRangeArray.Num() - 1];
	float RangeValue = MaxRangeValue;
	for (int i = ValueRangeArray.Num() - 1; i >= 0; i--)
	{
		auto RangeValueItem = ValueRangeArray[i];
		if (Value > RangeValueItem)
		{
			break;
		}
		else
		{
			RangeValue = RangeValueItem;
		}
	}
	return RangeValue * 
		(RangeValue >= MaxRangeValue ? 1.0f : (FMath::Abs(Value - RangeValue) < KINDA_SMALL_NUMBER ? 2.0f : 1.0f))
		* (MinOrMax ? -1.0f : 1.0f);
}

TOptional<float> FDreamWidgetCustomization::GetAnchorValue(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return TOptional<float>();

	auto AnchorMinHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchorMin));
	auto AnchorMaxHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchorMax));
	auto AnchoredPositionHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchoredPosition));
	auto SizeDeltaHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, SizeDelta));

	FVector2D AnchorMinValue;
	auto AnchorMinValueAccessResult = AnchorMinHandle->GetValue(AnchorMinValue);
	FVector2D AnchorMaxValue;
	auto AnchorMaxValueAccessResult = AnchorMaxHandle->GetValue(AnchorMaxValue);
	FVector2D AnchoredPosition;
	auto AnchoredPositionAccessResult = AnchoredPositionHandle->GetValue(AnchoredPosition);
	FVector2D SizeDelta;
	auto SizeDeltaAccessResult = SizeDeltaHandle->GetValue(SizeDelta);

	switch (AnchorValueIndex)
	{
	default:
	case 0://anchored position x, stretch left
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
		{
			auto GetValue = [=](TWeakObjectPtr<UDreamWidget> Item)->float {
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return Item->GetHorizontalAnchoredPosition();
				}
				else
				{
					return Item->GetAnchorOffsetLeft();
				}
			};
			if (AnchoredPositionAccessResult == FPropertyAccess::Result::Success)
			{
				return GetValue(TargetScriptArray[0]);
			}
			else if (AnchoredPositionAccessResult == FPropertyAccess::Result::MultipleValues)
			{
				bool bIsSameValue = true;
				float Value = 0;
				bool bIsFirst = true;
				for (auto& Item : TargetScriptArray)
				{
					if (bIsFirst)
					{
						Value = GetValue(Item);
						bIsFirst = false;
					}
					else
					{
						if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
						{
							bIsSameValue = false;
							break;
						}
					}
				}
				if (bIsSameValue)
				{
					return Value;
				}
			}
		}
		return TOptional<float>();
	}
	break;
	case 1://anchored position y, stretch top
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
		{
			auto GetValue = [=](TWeakObjectPtr<UDreamWidget> Item)->float {
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return Item->GetVerticalAnchoredPosition();
				}
				else
				{
					return Item->GetAnchorOffsetTop();
				}
			};
			if (AnchoredPositionAccessResult == FPropertyAccess::Result::Success)
			{
				return GetValue(TargetScriptArray[0]);
			}
			else if (AnchoredPositionAccessResult == FPropertyAccess::Result::MultipleValues)
			{
				bool bIsSameValue = true;
				float Value = 0;
				bool bIsFirst = true;
				for (auto& Item : TargetScriptArray)
				{
					if (bIsFirst)
					{
						Value = GetValue(Item);
						bIsFirst = false;
					}
					else
					{
						if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
						{
							bIsSameValue = false;
							break;
						}
					}
				}
				if (bIsSameValue)
				{
					return Value;
				}
			}
		}
		return TOptional<float>();
	}
	break;
	case 2://width, stretch right
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
		{
			auto GetValue = [=](TWeakObjectPtr<UDreamWidget> Item)->float {
				if (AnchorMinValue.X == AnchorMaxValue.X)
				{
					return Item->GetSizeDelta().X;
				}
				else
				{
					return Item->GetAnchorOffsetRight();
				}
			};
			if (SizeDeltaAccessResult == FPropertyAccess::Result::Success)
			{
				return GetValue(TargetScriptArray[0]);
			}
			else if (SizeDeltaAccessResult == FPropertyAccess::Result::MultipleValues)
			{
				bool bIsSameValue = true;
				float Value = 0;
				bool bIsFirst = true;
				for (auto& Item : TargetScriptArray)
				{
					if (bIsFirst)
					{
						Value = GetValue(Item);
						bIsFirst = false;
					}
					else
					{
						if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
						{
							bIsSameValue = false;
							break;
						}
					}
				}
				if (bIsSameValue)
				{
					return Value;
				}
			}
		}
		return TOptional<float>();
	}
	break;
	case 3://height, stretch bottom
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success)
		{
			auto GetValue = [=](TWeakObjectPtr<UDreamWidget> Item)->float {
				if (AnchorMinValue.Y == AnchorMaxValue.Y)
				{
					return Item->GetSizeDelta().Y;
				}
				else
				{
					return Item->GetAnchorOffsetBottom();
				}
			};
			if (SizeDeltaAccessResult == FPropertyAccess::Result::Success)
			{
				return GetValue(TargetScriptArray[0]);
			}
			else if (SizeDeltaAccessResult == FPropertyAccess::Result::MultipleValues)
			{
				bool bIsSameValue = true;
				float Value = 0;
				bool bIsFirst = true;
				for (auto& Item : TargetScriptArray)
				{
					if (bIsFirst)
					{
						Value = GetValue(Item);
						bIsFirst = false;
					}
					else
					{
						if (FMath::Abs(GetValue(Item) - Value) > KINDA_SMALL_NUMBER)
						{
							bIsSameValue = false;
							break;
						}
					}
				}
				if (bIsSameValue)
				{
					return Value;
				}
			}
		}
		return TOptional<float>();
	}
	break;
	}
}
void FDreamWidgetCustomization::ApplyAnchorValueToWidgets(const TArray<TWeakObjectPtr<UDreamWidget>>& Widgets, float Value, int AnchorValueIndex)
{
	for (auto& Item : Widgets)
	{
		if (!Item.IsValid())continue;
		//the property handle answers MultipleValues for a mixed selection, so the stretched-or-not question is asked of each widget
		switch (AnchorValueIndex)
		{
		case 0://anchored position x, stretch left
		{
			if (Item->GetAnchorData().IsHorizontalStretched())
			{
				Item->SetAnchorOffsetLeft(Value);
			}
			else
			{
				Item->SetHorizontalAnchoredPosition(Value);
			}
		}
		break;
		case 1://anchored position y, stretch top
		{
			if (Item->GetAnchorData().IsVerticalStretched())
			{
				Item->SetAnchorOffsetTop(Value);
			}
			else
			{
				Item->SetVerticalAnchoredPosition(Value);
			}
		}
		break;
		case 2://width, stretch right
		{
			if (Item->GetAnchorData().IsHorizontalStretched())
			{
				Item->SetAnchorOffsetRight(Value);
			}
			else
			{
				Item->SetWidth(Value);
			}
		}
		break;
		case 3://height, stretch bottom
		{
			if (Item->GetAnchorData().IsVerticalStretched())
			{
				Item->SetAnchorOffsetBottom(Value);
			}
			else
			{
				Item->SetHeight(Value);
			}
		}
		break;
		}
	}
}
void FDreamWidgetCustomization::ApplyValueChanged(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex, bool Commited)
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return;

	ApplyAnchorValueToWidgets(TargetScriptArray, Value, AnchorValueIndex);

	GUnrealEd->UpdatePivotLocationForSelection();
	GUnrealEd->SetPivotMovedIndependently(false);
	// Redraw
	GUnrealEd->RedrawLevelEditingViewports();

	auto AnchorProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_AnchorData());
	auto RelativeLocationProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), FName(TEXT("RelativeLocation")));
	for (auto& Item : TargetScriptArray)
	{
		FDreamUIUtils::NotifyPropertyChanged(Item.Get(), AnchorProperty);
		FDreamUIUtils::NotifyPropertyChanged(Item.Get(), RelativeLocationProperty);
	}
}
void FDreamWidgetCustomization::OnAnchorValueChanged(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
{
	ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex, false);
}
void FDreamWidgetCustomization::OnAnchorValueCommitted(float Value, ETextCommit::Type commitType, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
{
	GEditor->BeginTransaction(LOCTEXT("ChangeWidgetAnchor_Transaction", "Change Widget Anchor"));
	for (auto& Item : TargetScriptArray)
	{
		Item->Modify();
	}
	ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex, true);
	GEditor->EndTransaction();
}

void FDreamWidgetCustomization::OnAnchorValueSliderMovementBegin()
{
	GEditor->BeginTransaction(LOCTEXT("SlideChangeWidgetAnchor_Transaction", "Change Widget Anchor"));
	for (auto& Item : TargetScriptArray)
	{
		Item->Modify();
	}
}

void FDreamWidgetCustomization::OnAnchorValueSliderMovementEnd(float Value, TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)
{
	//ApplyValueChanged(Value, AnchorHandle, AnchorValueIndex);
	GEditor->EndTransaction();
}

bool FDreamWidgetCustomization::IsAnchorValueEnable(TSharedRef<IPropertyHandle> AnchorHandle, int AnchorValueIndex)const
{
	if (TargetScriptArray.Num() == 0 || !TargetScriptArray[0].IsValid())return false;

	auto AnchorMinHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchorMin));
	auto AnchorMaxHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchorMax));
	auto AnchoredPositionHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, AnchoredPosition));
	auto SizeDeltaHandle = AnchorHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIAnchorData, SizeDelta));

	FVector2D AnchorMinValue;
	auto AnchorMinValueAccessResult = AnchorMinHandle->GetValue(AnchorMinValue);
	FVector2D AnchorMaxValue;
	auto AnchorMaxValueAccessResult = AnchorMaxHandle->GetValue(AnchorMaxValue);
	FVector2D AnchoredPosition;
	auto AnchoredPositionAccessResult = AnchoredPositionHandle->GetValue(AnchoredPosition);
	FVector2D SizeDelta;
	auto SizeDeltaAccessResult = SizeDeltaHandle->GetValue(SizeDelta);

	switch (AnchorValueIndex)
	{
	default:
	case 0://anchored position x, stretch left
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
			&& AnchoredPositionAccessResult == FPropertyAccess::Result::Success
			)
		{
			if (AnchorMinValue.X == AnchorMaxValue.X)
			{
				return !GetLayoutControlHorizontalAnchoredPosition();
			}
			else
			{
				return !GetLayoutControlHorizontalAnchoredPosition() && !GetLayoutControlHorizontalSizeDelta();
			}
		}
		else
		{
			return true;
		}
	}
	break;
	case 1://anchored position y, stretch top
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
			&& AnchoredPositionAccessResult == FPropertyAccess::Result::Success
			)
		{
			if (AnchorMinValue.Y == AnchorMaxValue.Y)
			{
				return !GetLayoutControlVerticalAnchoredPosition();
			}
			else
			{
				return !GetLayoutControlVerticalAnchoredPosition() && !GetLayoutControlVerticalSizeDelta();
			}
		}
		else
		{
			return true;
		}
	}
	break;
	case 2://width, stretch right
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
			&& SizeDeltaAccessResult == FPropertyAccess::Result::Success
			)
		{
			if (AnchorMinValue.X == AnchorMaxValue.X)
			{
				return !GetLayoutControlHorizontalSizeDelta();
			}
			else
			{
				return !GetLayoutControlHorizontalAnchoredPosition() && !GetLayoutControlHorizontalSizeDelta();
			}
		}
		else
		{
			return true;
		}
	}
	break;
	case 3://height, stretch bottom
	{
		if (AnchorMinValueAccessResult == FPropertyAccess::Result::Success && AnchorMaxValueAccessResult == FPropertyAccess::Result::Success
			&& SizeDeltaAccessResult == FPropertyAccess::Result::Success
			)
		{
			if (AnchorMinValue.Y == AnchorMaxValue.Y)
			{
				return !GetLayoutControlVerticalSizeDelta();
			}
			else
			{
				return !GetLayoutControlVerticalAnchoredPosition() && !GetLayoutControlVerticalSizeDelta();
			}
		}
		else
		{
			return true;
		}
	}
	break;
	}
}


#undef LOCTEXT_NAMESPACE
