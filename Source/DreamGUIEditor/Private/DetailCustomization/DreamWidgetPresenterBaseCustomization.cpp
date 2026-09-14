// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DetailCustomization/DreamWidgetPresenterBaseCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Window/DreamUIWidgetInspector.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPresenterBaseCustomization"

namespace DreamWidgetPresenterBaseCustomizationLocal
{
	/**
	 * The inspector inside a dock tab, or nothing.
	 *
	 * A tab found by ID in a global registry contains whatever its spawner put there: a layout
	 * restored from an older config, or a spawner that failed and left a placeholder, hands back some
	 * other widget entirely. The two sites below used to StaticCastSharedRef the content with no
	 * check at all and then call through the result, which is a call into an unrelated object's
	 * vtable. Slate's own type name is the only identity a widget carries, so that is what is asked.
	 */
	TSharedPtr<SDreamUIWidgetInspector> GetInspectorIn(const TSharedPtr<SDockTab>& InTab)
	{
		if (!InTab.IsValid())
		{
			return nullptr;
		}
		//the name SNew stamps on the widget; keep it spelled the same as the class
		static const FName InspectorTypeName = TEXT("SDreamUIWidgetInspector");
		TSharedRef<SWidget> Content = InTab->GetContent();
		if (Content->GetType() != InspectorTypeName)
		{
			return nullptr;
		}
		return StaticCastSharedRef<SDreamUIWidgetInspector>(Content);
	}
}

FDreamWidgetPresenterBaseCustomization::FDreamWidgetPresenterBaseCustomization()
{
}

FDreamWidgetPresenterBaseCustomization::~FDreamWidgetPresenterBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamWidgetPresenterBaseCustomization::MakeInstance()
{
	return MakeShareable(new FDreamWidgetPresenterBaseCustomization);
}
void FDreamWidgetPresenterBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<UDreamWidgetPresenterComponentBase>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	const TWeakObjectPtr<UDreamWidgetPresenterComponentBase> PrimaryTarget = TargetScriptArray[0];
	const TWeakObjectPtr<UWorld> TargetWorld = PrimaryTarget.IsValid() ? PrimaryTarget->GetWorld() : nullptr;

	auto& Category = DetailBuilder.EditCategory("DreamWidgetPresenter");

	// ReloadWidget button
	Category.AddCustomRow(LOCTEXT("ReloadWidget", "ReloadWidget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ReloadWidget", "ReloadWidget"))
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([Targets = TargetScriptArray]()
			{
				for (const TWeakObjectPtr<UDreamWidgetPresenterComponentBase>& Target : Targets)
				{
					if (Target.IsValid())
					{
						Target->ReloadWidget();
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReloadWidgetBtn", "Reload"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	TSharedPtr<FTabManager> HostTabManager = nullptr;
	if (auto DetailsView = DetailBuilder.GetDetailsViewSharedPtr())
	{
		HostTabManager = DetailsView->GetHostTabManager();
	}
	if (!HostTabManager.IsValid())
	{
		HostTabManager = FGlobalTabmanager::Get();
	}

	bool bIsExternalTabAlreadyOpened = false;

	TSharedPtr<SDockTab> ExistingTab = HostTabManager->FindExistingLiveTab(FDreamGUIEditorModule:: DreamUIWidgetInspectorTabName);
	if (TSharedPtr<SDreamUIWidgetInspector> WidgetInspector =
		DreamWidgetPresenterBaseCustomizationLocal::GetInspectorIn(ExistingTab))
	{
		bIsExternalTabAlreadyOpened = TargetWorld.IsValid() && WidgetInspector->GetWorld() == TargetWorld.Get();
	}
	Category.AddCustomRow(FText())
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WidgetInspector", "WidgetInspector"))
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([PrimaryTarget]()
			{
				if (const UDreamWidgetPresenterComponentBase* Target = PrimaryTarget.Get(); Target && IsValid(Target->GetWorld()))
				{
					return EVisibility::Visible;
				}
				return EVisibility::Collapsed;
			})
			.OnClicked_Lambda([PrimaryTarget]()
			{
				UDreamWidgetPresenterComponentBase* Target = PrimaryTarget.Get();
				if (Target && IsValid(Target->GetWorld()))
				{
					TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FDreamGUIEditorModule::DreamUIWidgetInspectorTabName);
					if (TSharedPtr<SDreamUIWidgetInspector> WidgetInspector =
						DreamWidgetPresenterBaseCustomizationLocal::GetInspectorIn(Tab))
					{
						WidgetInspector->AssignWorld(Target->GetWorld());
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(bIsExternalTabAlreadyOpened ? LOCTEXT("OpenWidgetInspector", "Focus Tab") : LOCTEXT("OpenWidgetInspector", "Open in Tab"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	//canvas template
	{
		auto CanvasTemplate_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidgetPresenterComponentBase, CanvasTemplate));
		UObject* CanvasTemplate = nullptr;
		CanvasTemplate_PH->GetValue(CanvasTemplate);
		auto& CanvasTemplateCategory = DetailBuilder.EditCategory("CanvasTemplate");
		if (IsValid(CanvasTemplate))
		{
			if (IDetailPropertyRow* CanvasTemplateRow = CanvasTemplateCategory.AddExternalObjects(
				{ CanvasTemplate }, EPropertyLocation::Default,
				FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(true)))
			{
				CanvasTemplateRow->ShouldAutoExpand(true);
				CanvasTemplateRow->Visibility(TAttribute<EVisibility>::CreateLambda([TargetWorld]()
				{
					const UWorld* World = TargetWorld.Get();
					return World && World->IsGameWorld() ? EVisibility::Collapsed : EVisibility::Visible;
				}));
				DetailBuilder.HideProperty(CanvasTemplate_PH);
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE
