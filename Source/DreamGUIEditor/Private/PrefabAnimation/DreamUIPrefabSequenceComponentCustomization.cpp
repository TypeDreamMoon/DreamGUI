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
	if (!PrefabEditor.IsValid())
	{
		return;
	}

	DetailBuilder.HideProperty("SequenceArray");

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Animation", LOCTEXT("AnimationCategory", "Animation"), ECategoryPriority::Important);

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
