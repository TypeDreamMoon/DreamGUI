// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabSequenceComponentCustomization.h"

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "IPropertyUtilities.h"
#include "Widgets/Input/SButton.h"
#include "DreamUIPrefabSequenceEditor.h"
#include "PrefabEditor/DreamUIPrefabEditor.h"
#include "PrefabSystem/DreamUIPrefabPresenterComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabSequenceComponentCustomization"


TSharedRef<IDetailCustomization> FDreamUIPrefabSequenceComponentCustomization::MakeInstance()
{
	return MakeShared<FDreamUIPrefabSequenceComponentCustomization>();
}

void FDreamUIPrefabSequenceComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}

	WeakSequenceComponent = Cast<UDreamUIPrefabSequenceComponent>(Objects[0].Get());
	if (!WeakSequenceComponent.Get())
	{
		return;
	}

	auto World = WeakSequenceComponent->GetWorld();
	if (!World)
	{
		return;
	}

	auto PrefabEditor = FDreamUIPrefabEditor::GetEditorByWorld(World);

	DetailBuilder.HideProperty("SequenceArray");

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Animation", LOCTEXT("AnimationCategory", "Animation"), ECategoryPriority::Important);

	if (!PrefabEditor.IsValid())
	{
		// A level instance: its widget tree is transient and owns no prefab helper, so nothing
		// edited here could ever be saved. The honest offer is the prefab editor, whose Apply
		// flows back into this instance through the presenter's version check.
		Category.AddCustomRow(FText())
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationsValueText", "Animations"))
				.Font(DetailBuilder.GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("EditInPrefabTooltip", "Open this widget's prefab and edit its animations there. Apply in the prefab editor reloads this instance."))
				.OnClicked_Lambda([WeakComponent = WeakSequenceComponent]()
				{
					UDreamUIPrefabSequenceComponent* Component = WeakComponent.Get();
					UDreamWidget* Root = Component ? Component->GetWidget() : nullptr;
					while (Root != nullptr && Root->GetParent() != nullptr)
					{
						Root = Root->GetParent();
					}
					UDreamUIPrefab* Prefab = nullptr;
					for (TObjectIterator<UDreamUIPrefabPresenterComponent> It; It && Prefab == nullptr; ++It)
					{
						if (It->GetLoadedWidget() == Root)
						{
							Prefab = It->GetPrefab();
						}
					}
					if (Prefab == nullptr || GEditor == nullptr)
					{
						return FReply::Handled();
					}
					const FString CurrentName = Component->GetCurrentSequence() ? Component->GetCurrentSequence()->GetDisplayName().ToString() : FString();
					UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
					Subsystem->OpenEditorForAsset(Prefab);
					if (IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Prefab, /*bFocusIfOpen*/true))
					{
						static_cast<FDreamUIPrefabEditor*>(Instance)->FocusAnimationByDisplayName(CurrentName);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditInPrefabButtonText", "Edit Animations in Prefab"))
					.Font(DetailBuilder.GetDetailFont())
				]
			];
		return;
	}

	bool bIsExternalTabAlreadyOpened = false;

	auto HostTabManager = PrefabEditor.Pin()->GetTabManager();
	TSharedPtr<SDockTab> ExistingTab = HostTabManager->FindExistingLiveTab(FDreamUIPrefabEditor::GetSequencerTabID());
	if (ExistingTab.IsValid())
	{
		auto SequencerWidget = StaticCastSharedRef<SDreamUIPrefabSequenceEditor>(ExistingTab->GetContent());
		bIsExternalTabAlreadyOpened = WeakSequenceComponent.IsValid() && SequencerWidget->GetSequenceComponent() == WeakSequenceComponent.Get();
	}
	Category.AddCustomRow(FText())
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AnimationsValueText", "Animations"))
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.OnClicked_Lambda([=, this]()
			{
				if (TSharedPtr<SDockTab> Tab = HostTabManager->TryInvokeTab(FDreamUIPrefabEditor::GetSequencerTabID()))
				{
					// Set up a delegate that forces a refresh of this panel when the tab is closed to ensure we see the inline widget
					TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;
					auto OnClosed = [WeakUtilities](TSharedRef<SDockTab>)
					{
						TSharedPtr<IPropertyUtilities> PinnedPropertyUtilities = WeakUtilities.Pin();
						if (PinnedPropertyUtilities.IsValid())
						{
							PinnedPropertyUtilities->EnqueueDeferredAction(FSimpleDelegate::CreateSP(PinnedPropertyUtilities.ToSharedRef(), &IPropertyUtilities::ForceRefresh));
						}
					};
	
					Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(OnClosed));
	
					StaticCastSharedRef<SDreamUIPrefabSequenceEditor>(Tab->GetContent())->AssignDreamUIPrefabSequenceComponent(WeakSequenceComponent);
				}

				PropertyUtilities->ForceRefresh();

				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(bIsExternalTabAlreadyOpened ? LOCTEXT("FocusAnimationsTabButtonText", "Focus Tab") : LOCTEXT("OpenAnimationsTabButtonText", "Open Animations"))
				.Font(DetailBuilder.GetDetailFont())
			]
		];

}

#undef LOCTEXT_NAMESPACE
