// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorDetails.h"
#include "Modules/ModuleManager.h"
#include "ISCSEditorUICustomization.h"
#include "GameFramework/Actor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Misc/NotifyHook.h"
#include "LGUIPrefabEditor.h"
#include "DetailLayoutBuilder.h"
#include "LexWidgetDetailPropertyExtensionHandler.h"
#include "LexUIPrefabOverrideDataViewer.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "LexUIEditorTools.h"
#include "SSubobjectEditorModule.h"
#include "SSubobjectInstanceEditor.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexWidget.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorDetailTab"

class LexUISCSEditorUICustomization : public ISCSEditorUICustomization
{
	TWeakPtr<FLGUIPrefabEditor> PrefabEditor;
public:
	LexUISCSEditorUICustomization(TSharedPtr<FLGUIPrefabEditor> InPrefabEditor)
	{
		PrefabEditor = InPrefabEditor;
	}
	virtual bool HideAddComponentButton(TArrayView<UObject*> Context) const override
	{
		if (Context.Num() == 1)
		{
			if (auto Actor = Cast<AActor>(Context[0]))
			{
				if (auto HelperObj = PrefabEditor.Pin()->GetPrefabHelperObject())
				{
					if (HelperObj->IsActorBelongsToSubPrefab(Actor))
					{
						return true;
					}
				}
			}
		}
		return false;
	}
	virtual bool HideBlueprintButtons(TArrayView<UObject*> Context) const override
	{
		if (Context.Num() == 1)
		{
			if (auto Actor = Cast<AActor>(Context[0]))
			{
				if (auto HelperObj = PrefabEditor.Pin()->GetPrefabHelperObject())
				{
					if (HelperObj->IsActorBelongsToSubPrefab(Actor))
					{
						return true;
					}
				}
			}
		}
		return false;
	}
};

void SLexUIPrefabEditorDetails::Construct(const FArguments& Args, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;

	InPrefabEditor->OnSelectedWidgetsChanged.AddRaw(this, &SLexUIPrefabEditorDetails::OnEditorSelectionChanged);

    FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.bUpdatesFromSelection = true;
    DetailsViewArgs.bLockable = true;
    DetailsViewArgs.NotifyHook = GUnrealEd;
    DetailsViewArgs.ViewIdentifier = FName(TEXT("LGUIPrefabEditor"));
    DetailsViewArgs.bCustomNameAreaLocation = true;
    DetailsViewArgs.bCustomFilterAreaLocation = false;
    DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ComponentsAndActorsUseNameArea;
    DetailsViewArgs.bShowOptions = true;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bShowObjectLabel = true;
    //DetailsViewArgs.HostCommandList = InCommandList;

    DetailsView = PropPlugin.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SLexUIPrefabEditorDetails::IsPropertyReadOnly));

	TSharedRef<FLexWidgetDetailPropertyExtensionHandler> BindingHandler = MakeShareable(new FLexWidgetDetailPropertyExtensionHandler(PrefabEditorPtr));
	DetailsView->SetExtensionHandler(BindingHandler);

	FModuleManager::LoadModuleChecked<FSubobjectEditorModule>("SubobjectEditor");
	SubobjectEditor = SNew(SSubobjectInstanceEditor)
		.AllowEditing(this, &SLexUIPrefabEditorDetails::IsEditorAllowEditing)
		.ObjectContext(this, &SLexUIPrefabEditorDetails::GetActorContextAsObject)
		.OnSelectionUpdated(this, &SLexUIPrefabEditorDetails::OnSubObjectSelectionChanged)
		.OnItemDoubleClicked(this, &SLexUIPrefabEditorDetails::OnSubObjectItemDoubleClicked);

	
	TSharedPtr<ISCSEditorUICustomization> Customization = MakeShared<LexUISCSEditorUICustomization>(InPrefabEditor);
	SubobjectEditor->SetUICustomization(Customization);
	auto ButtonBox = SubobjectEditor->GetToolButtonsBox().ToSharedRef();
	DetailsView->SetNameAreaCustomContent(ButtonBox);

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(FMargin(2, 2))
			.AutoHeight()
			[
				DetailsView->GetNameAreaWidget().ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.Padding(FMargin(2, 2))
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility(this, &SLexUIPrefabEditorDetails::GetPrefabButtonVisibility)
				.IsEnabled(this, &SLexUIPrefabEditorDetails::IsPrefabButtonEnable)
				.HeightOverride(this, &SLexUIPrefabEditorDetails::GetPrefabButtonHeight)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(4, 0))
					[
						SNew(SBox)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PrefabFunctions", "Prefab"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.2f)
					.Padding(FMargin(2, 0))
					[
						SNew(SButton)
						.OnClicked_Lambda([=, this]() {
							PrefabEditorPtr.Pin()->OpenSubPrefab(CachedActor.Get());
							return FReply::Handled();
						})
						[
							SNew(SBox)
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("OpenPrefab", "Open"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.2f)
					.Padding(FMargin(2, 0))
					[
						SNew(SButton)
						.OnClicked_Lambda([=, this]() {
							PrefabEditorPtr.Pin()->SelectSubPrefab(CachedActor.Get());
							return FReply::Handled();
						})
						[
							SNew(SBox)
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SelectPrefab", "Select"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(0.5f)
					.Padding(FMargin(2, 0))
					[
						SNew(SComboButton)
						.HasDownArrow(true)
						.ToolTipText(LOCTEXT("PrefabOverride", "Edit override parameters for this prefab"))
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("OverrideButton", "Prefab Override Properties"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.MenuContent()
						[
							SNew(SBox)
							.Padding(FMargin(4, 4))
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SVerticalBox)
									+SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SHorizontalBox)
										+SHorizontalBox::Slot()
										.AutoWidth()
										[
											SAssignNew(PrefabOverrideDataViewer, SLexUIPrefabOverrideDataViewer, [=, this]()
											{
												return CachedActor.Get();
											})
											.AfterRevertPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset) {
												})
											.AfterApplyPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset){
												FLexUIEditorTools::RefreshLevelLoadedPrefab();
												FLexUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
												FLexUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
												})
										]
									]
								]
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Vertical)
				+ SSplitter::Slot()
				.Resizable(true)
				.SizeRule(SSplitter::ESizeRule::FractionOfParent)
				.Value(0.2f)
				[
					SNew(SBox)
					.MinDesiredHeight(200)
					.Padding(FMargin(0, 2))
					[
						SubobjectEditor.ToSharedRef()
					]
				]
				+ SSplitter::Slot()
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.Padding(FMargin(0, 2))
					[
						DetailsView.ToSharedRef()
					]
				]
			]
		];
}

SLexUIPrefabEditorDetails::~SLexUIPrefabEditorDetails()
{
}

bool SLexUIPrefabEditorDetails::IsPrefabButtonEnable()const
{
	if (PrefabEditorPtr.IsValid() && CachedActor.IsValid())
	{
		return PrefabEditorPtr.Pin()->ActorIsSubPrefabRoot(CachedActor.Get());
	}
	return false;
}

FOptionalSize SLexUIPrefabEditorDetails::GetPrefabButtonHeight()const
{
	return IsPrefabButtonEnable() ? 26 : 0;
}

EVisibility SLexUIPrefabEditorDetails::GetPrefabButtonVisibility()const
{
	return IsPrefabButtonEnable() ? EVisibility::Visible : EVisibility::Hidden;
}

bool SLexUIPrefabEditorDetails::IsEditorAllowEditing()const
{
	if (PrefabEditorPtr.IsValid() && CachedActor.IsValid())
	{
		return !PrefabEditorPtr.Pin()->ActorBelongsToSubPrefab(CachedActor.Get());
	}
	return true;
}

UObject* SLexUIPrefabEditorDetails::GetActorContextAsObject() const
{
	auto SelectedWidgets = PrefabEditorPtr.Pin()->GetSelectedWidgets();
	if (SelectedWidgets.Num() > 0 && SelectedWidgets[0].IsValid())
	{
		return SelectedWidgets[0]->GetOwner();
	}
	return nullptr;
}

void SLexUIPrefabEditorDetails::OnEditorSelectionChanged()
{
	if (bIsSelectFromDetails)return;
	bIsSelectFromLexUIEditor = true;
	auto SelectedWidgets = PrefabEditorPtr.Pin()->GetSelectedWidgets();
	if (SelectedWidgets.Num() > 0)
	{
		if (AActor* Actor = SelectedWidgets[0]->GetOwner())
		{
			if (Actor->GetWorld() != PrefabEditorPtr.Pin()->GetWorld())
			{
				return;
			}

			CachedActor = Actor;
			PrefabOverrideDataViewer->RefreshDataContent();
			if (SubobjectEditor)
			{
				SubobjectEditor->ClearSelection();
				SubobjectEditor->UpdateTree();
			}
		}

		TArray<UObject*> SelectedObjectList;
		for (int32 i = 0; i < SelectedWidgets.Num(); i++)
		{
			auto SelectedObject = SelectedWidgets[i];
			if (SelectedObject.IsValid())
			{
				if (SelectedObject->GetWorld() != PrefabEditorPtr.Pin()->GetWorld())
				{
					continue;
				}

				SelectedObjectList.Add(SelectedObject.Get());
			}
		}

		if (DetailsView)
		{
			DetailsView->SetObjects(SelectedObjectList, true);
		}
		if (SelectedObjectList.Num() == 0)
		{
			CachedActor = nullptr;
			PrefabOverrideDataViewer->RefreshDataContent();
			SubobjectEditor->ClearSelection();
			SubobjectEditor->UpdateTree();
		}
	}
	else
	{
		TArray<UObject*> SelectedObjectList;
		if (DetailsView)
		{
			DetailsView->SetObjects(SelectedObjectList, true);
		}
		CachedActor = nullptr;
		PrefabOverrideDataViewer->RefreshDataContent();
		SubobjectEditor->ClearSelection();
		SubobjectEditor->UpdateTree();
	}
	bIsSelectFromLexUIEditor = false;
}

void SLexUIPrefabEditorDetails::OnSubObjectSelectionChanged(const TArray<FSubobjectEditorTreeNodePtrType>& SelectedNodes)
{
	bIsSelectFromDetails = true;
	if (SelectedNodes.Num() > 0)
	{
		TArray<UObject*> SelectedObjects;
		TArray<UActorComponent*> SelectedComponents;
		for (auto& Node : SelectedNodes)
		{
			if (Node.IsValid())
			{
				UObject* Object = const_cast<UObject*>(Node->GetObject());
				if (Object)
				{
					SelectedObjects.Add(Object);
					if (auto Comp = Cast<UActorComponent>(Object))
					{
						SelectedComponents.Add(Comp);
					}
				}
			}
		}

		if (SelectedObjects.Num() > 0 && DetailsView.IsValid())
		{
			DetailsView->SetObjects(SelectedObjects);
		}
		if (!bIsSelectFromLexUIEditor)
		{
			if (SelectedComponents.Num() > 0)
			{
				ULexUIManagerWorldSubsystem::GetInstance(PrefabEditorPtr.Pin()->GetWorld())->GetSelection()->SelectNone();
				for (auto Comp : SelectedComponents)
				{
					ULexUIManagerWorldSubsystem::GetInstance(PrefabEditorPtr.Pin()->GetWorld())->GetSelection()->SelectComponent(Comp);
				}
			}
			else
			{
				ULexUIManagerWorldSubsystem::GetInstance(PrefabEditorPtr.Pin()->GetWorld())->GetSelection()->SelectNone();
			}
		}
	}
	bIsSelectFromDetails = false;
}

void SLexUIPrefabEditorDetails::OnSubObjectItemDoubleClicked(const FSubobjectEditorTreeNodePtrType ClickedNode)
{

}

bool SLexUIPrefabEditorDetails::IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent)
{
	return false;
}

#undef LOCTEXT_NAMESPACE