// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIPrefabOverrideDataViewer.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "DreamUIPrefabEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Editor.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabOverrideDataViewer"

namespace DreamUIOverrideDataViewerLocal
{
	/**
	 * Run InAction once the current Slate pass is over.
	 *
	 * These buttons live inside the tree RefreshDataContent() clears, and the AfterApplyPrefab /
	 * AfterRevertPrefab delegates can close the whole prefab editor -- which takes the popup this
	 * viewer sits in with it. Doing either from the click handler mutates a widget tree Slate is
	 * still walking, and FChildren::ForEachWidget reads the child count ONCE before the walk, so a
	 * slot removed underneath it is read back as an out-of-bounds index several frames of engine
	 * code away from the code that removed it.
	 */
	void RunAfterThisFrame(TFunction<void()> InAction)
	{
		if (GEditor == nullptr)
		{
			InAction();
			return;
		}
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[Action = MoveTemp(InAction)]() { Action(); }));
	}
}

void SDreamUIPrefabOverrideDataViewer::Construct(const FArguments& InArgs, TFunction<UDreamWidget*()> InGetSelectedWidgetFunction)
{
	AfterRevertPrefab = InArgs._AfterRevertPrefab;
	AfterApplyPrefab = InArgs._AfterApplyPrefab;

	GetSelectedWidgetFunction = InGetSelectedWidgetFunction;
	RootContentVerticalBox = SNew(SVerticalBox);
	ChildSlot
	[
		RootContentVerticalBox.ToSharedRef()
	]
	;
	RefreshDataContent();
}

void SDreamUIPrefabOverrideDataViewer::RefreshDataContent()
{
	//drop the old rows before any early-out: a row left behind still carries a live Apply button, and pressing it writes the source prefab for a selection that is no longer there
	RootContentVerticalBox->ClearChildren();
	auto SelectedWidget = GetSelectedWidgetFunction();
	if (!SelectedWidget)return;
	PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(SelectedWidget);
	if (!PrefabHelperObject.IsValid())return;
	
	bool bIsSubPrefabRoot = false;
	for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
	{
		if (KeyValue.Key == SelectedWidget)
		{
			bIsSubPrefabRoot = true;
			break;
		}
	}
	this->RefreshDataContent(PrefabHelperObject->GetSubPrefabData(SelectedWidget).ObjectOverrideParameterArray, bIsSubPrefabRoot ? nullptr : SelectedWidget);
}

void SDreamUIPrefabOverrideDataViewer::RefreshDataContent(TArray<FDreamUIPrefabOverrideParameterData> ObjectOverrideParameterArray, UDreamWidget* InReferenceWidget)
{
	RootContentVerticalBox->ClearChildren();
	if (ObjectOverrideParameterArray.Num() == 0)return;

	auto RootObject = ObjectOverrideParameterArray[0].Object.Get();
	if (InReferenceWidget != nullptr)
	{
		for (int i = 0; i < ObjectOverrideParameterArray.Num(); i++)
		{
			auto& Item = ObjectOverrideParameterArray[i];
			if (!Item.Object->IsInOuter(InReferenceWidget)
				&& Item.Object != InReferenceWidget
				)
			{
				ObjectOverrideParameterArray.RemoveAt(i);
				i--;
			}
		}
	}

	const float ButtonHeight = 32;
	for (int i = 0; i < ObjectOverrideParameterArray.Num(); i++)
	{
		auto& DataItem = ObjectOverrideParameterArray[i];
		if (!DataItem.Object.IsValid())continue;
		FString DisplayName;
		auto Widget = Cast<UDreamWidget>(DataItem.Object.Get());
		auto Component = Cast<UDreamUIBehaviour>(DataItem.Object.Get());
		if (Widget)
		{
			DisplayName = Widget->GetDisplayName();
		}
		else if (Component)
		{
			Widget = Component->GetWidget();
			DisplayName = Widget->GetDisplayName() + TEXT(".") + Component->GetName();
		}
		else
		{
			DisplayName = DataItem.Object->GetName();
			for (UObject* NextOuter = DataItem.Object->GetOuter(); NextOuter != NULL; NextOuter = NextOuter->GetOuter())
			{
				if (NextOuter->IsA(UDreamWidget::StaticClass()))
				{
					DisplayName = ((UDreamWidget*)NextOuter)->GetDisplayName() + "." + DisplayName;
					break;
				}
				else
				{
					DisplayName = NextOuter->GetName() + "." + DisplayName;
				}
			}
		}

		auto FilteredMemeberPropertyNames = DataItem.MemberPropertyNames;

		RootContentVerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.HeightOverride(ButtonHeight)
				.Padding(FMargin(4, 2))
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[ 
					SNew(SButton)
					.Text(FText::FromString(DisplayName))
					.ToolTipText(LOCTEXT("ObjectButtonTooltipText", "Widget.Component, click to select target"))
					.ButtonStyle(FAppStyle::Get(), "PropertyEditor.AssetComboStyle" )
					.ForegroundColor(FAppStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
					.OnClicked_Lambda([=](){
						UDreamUISelection::GetInstance(Widget->GetWorld())->SelectNone();
						UDreamUISelection::GetInstance(Widget->GetWorld())->SelectWidget(Widget);
						if(Component)UDreamUISelection::GetInstance(Widget->GetWorld())->SelectComponent(Component);
						return FReply::Handled();
					})
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					PropertyCustomizationHelpers::MakeResetButton(
						FSimpleDelegate::CreateLambda([=, this]() {
							PrefabHelperObject->RevertPrefabOverride(DataItem.Object.Get(), FilteredMemeberPropertyNames);
							// Resolve the asset first: RefreshDataContent() re-derives PrefabHelperObject
							// from the current selection and can leave it null, and the delegate argument
							// below reads through it.
							UDreamUIPrefab* Asset = PrefabHelperObject->GetPrefabAssetBySubPrefabObject(DataItem.Object.Get());
							DreamUIOverrideDataViewerLocal::RunAfterThisFrame(
								[WeakSelf = TWeakPtr<SDreamUIPrefabOverrideDataViewer>(SharedThis(this)), Asset, Delegate = AfterRevertPrefab]()
								{
									if (TSharedPtr<SDreamUIPrefabOverrideDataViewer> Self = WeakSelf.Pin())Self->RefreshDataContent();
									Delegate.ExecuteIfBound(Asset);
								});
						})
						, LOCTEXT("RevertObjectAllParameterSet", "Click to revert all parameters of this object to prefab's default value.")
					)
				]
			]
			+SHorizontalBox::Slot()
			.Padding(FMargin(6, 0, 0, 0))
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					PropertyCustomizationHelpers::MakeUseSelectedButton(
						FSimpleDelegate::CreateLambda([=, this]() {
							PrefabHelperObject->ApplyPrefabOverride(DataItem.Object.Get(), FilteredMemeberPropertyNames);
							// Resolve the asset first: RefreshDataContent() re-derives PrefabHelperObject
							// from the current selection and can leave it null, and the delegate argument
							// below reads through it.
							UDreamUIPrefab* Asset = PrefabHelperObject->GetPrefabAssetBySubPrefabObject(DataItem.Object.Get());
							DreamUIOverrideDataViewerLocal::RunAfterThisFrame(
								[WeakSelf = TWeakPtr<SDreamUIPrefabOverrideDataViewer>(SharedThis(this)), Asset, Delegate = AfterApplyPrefab]()
								{
									if (TSharedPtr<SDreamUIPrefabOverrideDataViewer> Self = WeakSelf.Pin())Self->RefreshDataContent();
									Delegate.ExecuteIfBound(Asset);
								});
						})
						, LOCTEXT("ApplyObjectParameterSet", "Click to apply all parameters of this object to prefab's default value.")
					)
				]
			]
		]
		;
		for (auto& PropertyName : DataItem.MemberPropertyNames)
		{
			auto Property = FindFProperty<FProperty>(DataItem.Object->GetClass(), PropertyName);
			if (!Property)continue;
			auto HorizontalBox = SNew(SHorizontalBox);
			HorizontalBox->AddSlot()
			.AutoWidth()
			[
				SNew(SBox)
				.Padding(FMargin(20, 2, 2, 2))
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Property->GetDisplayNameText())
					.ToolTipText(LOCTEXT("ModifiedPropertyName", "Modified property name"))
				]
			]
			;
			//apply and revert
			HorizontalBox->AddSlot()
			.Padding(FMargin(6, 0, 0, 0))
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					PropertyCustomizationHelpers::MakeResetButton(
						FSimpleDelegate::CreateLambda([=, this]() {
							PrefabHelperObject->RevertPrefabOverride(DataItem.Object.Get(), {PropertyName});
							// Asset first: RefreshDataContent() re-derives PrefabHelperObject from the
							// current selection and can leave it null, and the delegate reads through it.
							UDreamUIPrefab* Asset = PrefabHelperObject->GetPrefabAssetBySubPrefabObject(DataItem.Object.Get());
							DreamUIOverrideDataViewerLocal::RunAfterThisFrame(
								[WeakSelf = TWeakPtr<SDreamUIPrefabOverrideDataViewer>(SharedThis(this)), Asset, Delegate = AfterRevertPrefab]()
								{
									if (TSharedPtr<SDreamUIPrefabOverrideDataViewer> Self = WeakSelf.Pin())Self->RefreshDataContent();
									Delegate.ExecuteIfBound(Asset);
								});
						})
						, LOCTEXT("ResetThisParameter", "Click to revert this parameter to prefab's default value.")
					)
				]
			]
			;
			HorizontalBox->AddSlot()
			.Padding(FMargin(6, 0, 0, 0))
			.AutoWidth()
			.HAlign(EHorizontalAlignment::HAlign_Left)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					PropertyCustomizationHelpers::MakeUseSelectedButton(
						FSimpleDelegate::CreateLambda([=, this]() {
							PrefabHelperObject->ApplyPrefabOverride(DataItem.Object.Get(), {PropertyName});
							// Asset first: RefreshDataContent() re-derives PrefabHelperObject from the
							// current selection and can leave it null, and the delegate reads through it.
							UDreamUIPrefab* Asset = PrefabHelperObject->GetPrefabAssetBySubPrefabObject(DataItem.Object.Get());
							DreamUIOverrideDataViewerLocal::RunAfterThisFrame(
								[WeakSelf = TWeakPtr<SDreamUIPrefabOverrideDataViewer>(SharedThis(this)), Asset, Delegate = AfterApplyPrefab]()
								{
									if (TSharedPtr<SDreamUIPrefabOverrideDataViewer> Self = WeakSelf.Pin())Self->RefreshDataContent();
									Delegate.ExecuteIfBound(Asset);
								});
						})
						, LOCTEXT("ApplyThisParameter", "Click to apply this parameter to origin prefab.")
					)
				]
			]
			;

			RootContentVerticalBox->AddSlot()
			[
				HorizontalBox
			]
			;
		}
	}
	//revert all, apply all
	if(InReferenceWidget == nullptr)
	{
		RootContentVerticalBox->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				.HeightOverride(ButtonHeight)
				.Padding(FMargin(4, 2))
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("RevertAll", "Revert All"))
					.ToolTipText(LOCTEXT("RevertAll_Tooltip", "Revert all overrides of this instance, including the root widget's transform - unlike Apply All, this one does move the instance"))
					.OnClicked_Lambda([=, this](){
						PrefabHelperObject->RevertAllPrefabOverride(RootObject);
						AfterRevertPrefab.ExecuteIfBound(PrefabHelperObject->GetPrefabAssetBySubPrefabObject(RootObject));
						return FReply::Handled();
					})
				]
			]
			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				.HeightOverride(ButtonHeight)
				.Padding(FMargin(4, 2))
				.HAlign(EHorizontalAlignment::HAlign_Left)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyAll", "Apply All"))
					.ToolTipText(LOCTEXT("ApplyAll_Tooltip", "Apply all overrides to source prefab, except root widget's transform"))
					.OnClicked_Lambda([=, this](){
						PrefabHelperObject->ApplyAllOverrideToPrefab(RootObject);
						AfterApplyPrefab.ExecuteIfBound(PrefabHelperObject->GetPrefabAssetBySubPrefabObject(RootObject));
						return FReply::Handled();
					})
				]
			]
		]
		;
	}
}

#undef LOCTEXT_NAMESPACE
