// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetAnimationComponentCustomization.h"
#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimation.h"

#include "Animation/DreamWidgetAnimationComponent.h"
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
#include "SDreamWidgetAnimationEditor.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Core/DreamWidgetPresenterComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "DreamWidgetAnimationComponentCustomization"


TSharedRef<IDetailCustomization> FDreamWidgetAnimationComponentCustomization::MakeInstance()
{
	return MakeShared<FDreamWidgetAnimationComponentCustomization>();
}

void FDreamWidgetAnimationComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}

	WeakSequenceComponent = Cast<UDreamWidgetAnimationComponent>(Objects[0].Get());
	if (!WeakSequenceComponent.Get())
	{
		return;
	}

	auto World = WeakSequenceComponent->GetWorld();
	if (!World)
	{
		return;
	}

	auto DesignerEditor = FDreamWidgetBlueprintEditor::GetEditorByWorld(World);

	DetailBuilder.HideProperty("SequenceArray");

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Animation", LOCTEXT("AnimationCategory", "Animation"), ECategoryPriority::Important);

	if (!DesignerEditor.IsValid())
	{
		// A level instance: its widget tree is transient and owns no prefab helper, so nothing
		// edited here could ever be saved. The honest offer is the designer, whose Apply
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
				.ToolTipText(LOCTEXT("EditInDesignerTooltip", "Open this widget's own Blueprint and edit its animations there. Apply in the designer reloads this instance."))
				.OnClicked_Lambda([WeakComponent = WeakSequenceComponent]()
				{
					UDreamWidgetAnimationComponent* Component = WeakComponent.Get();
					UDreamWidget* Root = Component ? Component->GetWidget() : nullptr;
					while (Root != nullptr && Root->GetParent() != nullptr)
					{
						Root = Root->GetParent();
					}
					// The presenter holds a hierarchy CLASS now; the asset behind it is the Blueprint
					// that generated the class.
					UObject* SourceAsset = nullptr;
					for (TObjectIterator<UDreamWidgetPresenterComponent> It; It && SourceAsset == nullptr; ++It)
					{
						if (It->GetLoadedWidget() == Root)
						{
							if (UClass* WidgetClass = It->GetWidgetClass())
							{
								SourceAsset = WidgetClass->ClassGeneratedBy;
							}
						}
					}
					if (SourceAsset == nullptr || GEditor == nullptr)
					{
						return FReply::Handled();
					}
					const FString CurrentName = Component->GetCurrentSequence() ? Component->GetCurrentSequence()->GetDisplayName().ToString() : FString();
					UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
					Subsystem->OpenEditorForAsset(SourceAsset);
					// The animation focus below used to blind-cast the returned editor to
					// FDreamWidgetBlueprintEditor. That was only ever safe because the asset was always a
					// prefab; against a Blueprint it is undefined behaviour, so the jump-to-animation
					// only happens when the editor really is the designer. Restoring it for the
					// class model is the editor branch's job.
					if (IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(SourceAsset, /*bFocusIfOpen*/true))
					{
						if (false)
						{
							static_cast<FDreamWidgetBlueprintEditor*>(Instance)->FocusAnimationByDisplayName(CurrentName);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditInDesignerButtonText", "Edit Animations in Designer"))
					.Font(DetailBuilder.GetDetailFont())
				]
			];
		return;
	}

	bool bIsExternalTabAlreadyOpened = false;

	auto HostTabManager = DesignerEditor.Pin()->GetTabManager();
	TSharedPtr<SDockTab> ExistingTab = HostTabManager->FindExistingLiveTab(FDreamWidgetBlueprintEditor::GetSequencerTabID());
	if (ExistingTab.IsValid())
	{
		auto SequencerWidget = StaticCastSharedRef<SDreamWidgetAnimationEditor>(ExistingTab->GetContent());
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
				if (TSharedPtr<SDockTab> Tab = HostTabManager->TryInvokeTab(FDreamWidgetBlueprintEditor::GetSequencerTabID()))
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
	
					StaticCastSharedRef<SDreamWidgetAnimationEditor>(Tab->GetContent())->AssignDreamWidgetAnimationComponent(WeakSequenceComponent);
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
