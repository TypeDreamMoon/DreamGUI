// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabSequenceEditor.h"

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "EditorStyleSet.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Docking/SDockTab.h"
#include "SSCSEditor.h"
#include "BlueprintEditorTabs.h"
#include "ScopedTransaction.h"
#include "ISequencerModule.h"
#include "Editor.h"
#include "LexUIPrefabSequenceEditorWidget.h"
#include "IPropertyUtilities.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "LGUIEditorModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Misc/TextFilter.h"
#include "PropertyCustomizationHelpers.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "LexUIEditorTools.h"
#include "Core/Components/LexWidget.h"

#define LOCTEXT_NAMESPACE "SLGUIPrefabSequenceEditor"


struct FWidgetAnimationListItem
{
	FWidgetAnimationListItem(ULexUIPrefabSequence* InAnimation, bool bInRenameRequestPending = false, bool bInNewAnimation = false)
		: Animation(InAnimation)
		, bRenameRequestPending(bInRenameRequestPending)
		, bNewAnimation(bInNewAnimation)
	{}

	ULexUIPrefabSequence* Animation;
	bool bRenameRequestPending;
	bool bNewAnimation;
};


typedef SListView<TSharedPtr<FWidgetAnimationListItem> > SWidgetAnimationListView;

class SWidgetAnimationListItem : public STableRow<TSharedPtr<FWidgetAnimationListItem> >
{
public:
	SLATE_BEGIN_ARGS(SWidgetAnimationListItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SLexUIPrefabSequenceEditor* InEditor, TSharedPtr<FWidgetAnimationListItem> InListItem)
	{
		ListItem = InListItem;
		Editor = InEditor;

		STableRow<TSharedPtr<FWidgetAnimationListItem>>::Construct(
			STableRow<TSharedPtr<FWidgetAnimationListItem>>::FArguments()
			.Padding(FMargin(3.0f, 2.0f))
			.Content()
			[
				SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
				.Font(FCoreStyle::Get().GetFontStyle("NormalFont"))
				.Text(this, &SWidgetAnimationListItem::GetMovieSceneText)
				//.HighlightText(InArgs._HighlightText)
				.OnVerifyTextChanged(this, &SWidgetAnimationListItem::OnVerifyNameTextChanged)
				.OnTextCommitted(this, &SWidgetAnimationListItem::OnNameTextCommited)
				.IsSelected(this, &SWidgetAnimationListItem::IsSelectedExclusively)
			],
			InOwnerTableView);
	}

	void BeginRename()
	{
		InlineTextBlock->EnterEditingMode();
	}

private:
	FText GetMovieSceneText() const
	{
		if (ListItem.IsValid())
		{
			return ListItem.Pin()->Animation->GetDisplayName();
		}

		return FText::GetEmpty();
	}

	bool OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
	{
		auto Animation = ListItem.Pin()->Animation;

		auto SequenceComp = Editor->GetSequenceComponent();
		if (SequenceComp)
		{
			auto& SequenceArray = SequenceComp->GetSequenceArray();
			auto ExistIndex = SequenceArray.IndexOfByPredicate([InText, this](const ULexUIPrefabSequence* Item) {
				if (ListItem.Pin()->Animation == Item)
				{
					return false;
				}
				return Item->GetDisplayName().EqualTo(InText);
				});
			if (ExistIndex != INDEX_NONE)
			{
				OutErrorMessage = LOCTEXT("NameInUseByAnimation", "An animation with this name already exists");
				return false;
			}
		}
		return true;
	}

	void OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
	{
		auto Animation = ListItem.Pin()->Animation;

		// Name has already been checked in VerifyAnimationRename
		auto NewName = InText.ToString();
		auto OldName = Animation->GetDisplayName().ToString();

		//FObjectPropertyBase* ExistingProperty = CastField<FObjectPropertyBase>(Blueprint->ParentClass->FindPropertyByName(NewFName));
		//const bool bBindWidgetAnim = ExistingProperty && FWidgetBlueprintEditorUtils::IsBindWidgetAnimProperty(ExistingProperty) && ExistingProperty->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass());

		const bool bValidName = !OldName.Equals(NewName) && !InText.IsEmpty();
		const bool bCanRename = (bValidName/* || bBindWidgetAnim*/);

		const bool bNewAnimation = ListItem.Pin()->bNewAnimation;
		if (bCanRename)
		{
			FText TransactionName = bNewAnimation ? LOCTEXT("NewAnimation", "New Animation") : LOCTEXT("RenameAnimation", "Rename Animation");
			{
				const FScopedTransaction Transaction(TransactionName);
				Animation->Modify();

				Animation->SetDisplayNameString(NewName);

				if (bNewAnimation)
				{
					Editor->RefreshAnimationList();
					ListItem.Pin()->bNewAnimation = false;
				}
			}
		}
		else if (bNewAnimation)
		{
			const FScopedTransaction Transaction(LOCTEXT("NewAnimation", "New Animation"));
			Editor->RefreshAnimationList();
			ListItem.Pin()->bNewAnimation = false;
		}
	}
private:
	TWeakPtr<FWidgetAnimationListItem> ListItem;
	SLexUIPrefabSequenceEditor* Editor = nullptr;
	TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;
};


SLexUIPrefabSequenceEditor::~SLexUIPrefabSequenceEditor()
{
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectsReplacedHandle);
	FLexUIEditorTools::OnEditingPrefabChanged.Remove(EditingPrefabChangedHandle);
	FLexUIEditorTools::OnBeforeApplyPrefab.Remove(OnBeforeApplyPrefabHandle);
}

void SLexUIPrefabSequenceEditor::Construct(const FArguments& InArgs)
{
	SAssignNew(AnimationListView, SWidgetAnimationListView)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&Animations)
		.OnGenerateRow(this, &SLexUIPrefabSequenceEditor::OnGenerateRowForAnimationListView)
		.OnItemScrolledIntoView(this, &SLexUIPrefabSequenceEditor::OnItemScrolledIntoView)
		.OnSelectionChanged(this, &SLexUIPrefabSequenceEditor::OnAnimationListViewSelectionChanged)
		.OnContextMenuOpening(this, &SLexUIPrefabSequenceEditor::OnContextMenuOpening)
		;

	ChildSlot
		[
			SNew(SSplitter)
			+SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SBox)
				.IsEnabled_Lambda([=, this]() {
					return WeakSequenceComponent.IsValid();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding( 2 )
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text_Lambda([=, this](){
									if (WeakSequenceComponent.IsValid())
									{
										if (auto Widget = WeakSequenceComponent->GetWidget())
										{
											auto DisplayText = Widget->GetDisplayName() + TEXT(".") + WeakSequenceComponent->GetName();
											return FText::FromString(DisplayText);
										}
									}
									return LOCTEXT("NullSequenceComponent", "Null (LexUIPrefabSequence)");
								})
								.ToolTipText(LOCTEXT("ObjectButtonTooltipText", "Actor.Component, click to select target"))
								.IsEnabled_Lambda([=, this](){
									return WeakSequenceComponent.IsValid();
								})
								.ButtonStyle( FAppStyle::Get(), "PropertyEditor.AssetComboStyle" )
								.ForegroundColor(FAppStyle::GetColor("PropertyEditor.AssetName.ColorAndOpacity"))
								.OnClicked_Lambda([=, this](){
									if (WeakSequenceComponent.IsValid())
									{
										// GEditor->SelectNone(true, true);
										// GEditor->SelectActor(WeakSequenceComponent, true, true);
										// GEditor->SelectComponent(WeakSequenceComponent.Get(), true, true);
									}
									return FReply::Handled();
								})
							]
							+SHorizontalBox::Slot()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							[
								PropertyCustomizationHelpers::MakeResetButton(
									FSimpleDelegate::CreateLambda([=, this]() {
										AssignLGUIPrefabSequenceComponent(nullptr);
										})
									, LOCTEXT("ClearSequenceComponent", "Click to clear current selected LGUISequenceComponent, so we will not edit it here.")
											)
							]
						]
						+ SVerticalBox::Slot()
						.Padding( 2 )
						.AutoHeight()
						[
							SNew( SHorizontalBox )
							+ SHorizontalBox::Slot()
							.Padding(0)
							.VAlign( VAlign_Center )
							.AutoWidth()
							[
								SNew(SButton)
								.OnClicked(this, &SLexUIPrefabSequenceEditor::OnNewAnimationClicked)
								.Text(LOCTEXT("NewAnimationButtonText", "+ Animation"))
							]
							+ SHorizontalBox::Slot()
							.Padding(2.0f, 0.0f)
							.VAlign( VAlign_Center )
							[
								SAssignNew(SearchBoxPtr, SSearchBox)
								.HintText(LOCTEXT("Search Animations", "Search Animations"))
								.OnTextChanged(this, &SLexUIPrefabSequenceEditor::OnAnimationListViewSearchChanged)
							]
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SScrollBorder, AnimationListView.ToSharedRef())
							[
								AnimationListView.ToSharedRef()
							]
						]
					]
				]
			]
			+SSplitter::Slot()
			.Value(0.8f)
			[
				SAssignNew(PrefabSequenceEditor, SLexUIPrefabSequenceEditorWidget, nullptr)
			]
		];

	CreateCommandList();

	OnObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SLexUIPrefabSequenceEditor::OnObjectsReplaced);

	PrefabSequenceEditor->AssignSequence(GetLGUIPrefabSequence());
	EditingPrefabChangedHandle = FLexUIEditorTools::OnEditingPrefabChanged.AddRaw(this, &SLexUIPrefabSequenceEditor::OnEditingPrefabChanged);
	OnBeforeApplyPrefabHandle = FLexUIEditorTools::OnBeforeApplyPrefab.AddRaw(this, &SLexUIPrefabSequenceEditor::OnBeforeApplyPrefab);
}

void SLexUIPrefabSequenceEditor::AssignLGUIPrefabSequenceComponent(TWeakObjectPtr<ULexUIPrefabSequenceComponent> InSequenceComponent)
{
	WeakSequenceComponent = InSequenceComponent;
	RefreshAnimationList();
}

ULexUIPrefabSequence* SLexUIPrefabSequenceEditor::GetLGUIPrefabSequence() const
{
	if (CurrentSelectedAnimationIndex != INDEX_NONE)
	{
		ULexUIPrefabSequenceComponent* SequenceComponent = WeakSequenceComponent.Get();
		return SequenceComponent ? SequenceComponent->GetSequenceByIndex(CurrentSelectedAnimationIndex) : nullptr;
	}
	return nullptr;
}

void SLexUIPrefabSequenceEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	ULexUIPrefabSequenceComponent* Component = WeakSequenceComponent.Get(true);

	ULexUIPrefabSequenceComponent* NewSequenceComponent = Component ? Cast<ULexUIPrefabSequenceComponent>(ReplacementMap.FindRef(Component)) : nullptr;
	if (NewSequenceComponent)
	{
		WeakSequenceComponent = NewSequenceComponent;
		PrefabSequenceEditor->AssignSequence(GetLGUIPrefabSequence());
	}
}

TSharedRef<ITableRow> SLexUIPrefabSequenceEditor::OnGenerateRowForAnimationListView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	return SNew(SWidgetAnimationListItem, InOwnerTableView, this, InListItem);
}

void SLexUIPrefabSequenceEditor::OnAnimationListViewSelectionChanged(TSharedPtr<FWidgetAnimationListItem> InListItem, ESelectInfo::Type InSelectInfo)
{
	CurrentSelectedAnimationIndex = INDEX_NONE;
	if (InListItem.IsValid())
	{
		auto& SequenceArray = WeakSequenceComponent->GetSequenceArray();
		for (int i = 0; i < SequenceArray.Num(); i++)
		{
			if (SequenceArray[i] == InListItem->Animation)
			{
				CurrentSelectedAnimationIndex = i;
				break;
			}
		}
	}
	PrefabSequenceEditor->AssignSequence(GetLGUIPrefabSequence());
}

void SLexUIPrefabSequenceEditor::RefreshAnimationList()
{
	if (WeakSequenceComponent.IsValid())
	{
		Animations.Reset();
		auto& SequenceArray = WeakSequenceComponent->GetSequenceArray();
		for (auto& Item : SequenceArray)
		{
			Animations.Add(MakeShareable(new FWidgetAnimationListItem(Item)));
		}
		AnimationListView->RequestListRefresh();
		if (Animations.Num() > 0)
		{
			AnimationListView->SetSelection(Animations[0]);
		}
	}
}

void SLexUIPrefabSequenceEditor::OnBeforeApplyPrefab(ULexUIPrefabHelperObject* InObject)
{
	if (WeakSequenceComponent.IsValid())
	{
		if (auto Widget = WeakSequenceComponent->GetWidget())
		{
			if (InObject->IsWidgetBelongsToThis(Widget))
			{
				this->AnimationListView->ClearSelection();
			}
		}
	}
}

// Trigger when opening a new prefab
void SLexUIPrefabSequenceEditor::OnEditingPrefabChanged(ULexWidget* RootWidget)
{
	if (RootWidget)
	{
		for (auto ChildWidget : RootWidget->GetChildren())
		{
			auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(RootWidget);
			if (PrefabHelperObject)
			{
				//skip sub prefab's PrefabSequenceComponent
				if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(ChildWidget))
				{
					continue;
				}
			}

			auto PrefabSequencerComponent = ChildWidget->GetComponent<ULexUIPrefabSequenceComponent>();
			if (PrefabSequencerComponent)
			{
				AssignLGUIPrefabSequenceComponent(PrefabSequencerComponent);
			}
		}
	}
}

void SLexUIPrefabSequenceEditor::OnAnimationListViewSearchChanged(const FText& InSearchText)
{
	if (WeakSequenceComponent.IsValid())
	{
		auto& SequenceArray = WeakSequenceComponent->GetSequenceArray();
		if (!InSearchText.IsEmpty())
		{
			struct Local
			{
				static void UpdateFilterStrings(ULexUIPrefabSequence* InAnimation, OUT TArray< FString >& OutFilterStrings)
				{
					OutFilterStrings.Add(InAnimation->GetName());
				}
			};

			TTextFilter<ULexUIPrefabSequence*> TextFilter(TTextFilter<ULexUIPrefabSequence*>::FItemToStringArray::CreateStatic(&Local::UpdateFilterStrings));

			TextFilter.SetRawFilterText(InSearchText);
			SearchBoxPtr->SetError(TextFilter.GetFilterErrorText());

			Animations.Reset();

			for (ULexUIPrefabSequence* Animation : SequenceArray)
			{
				if (TextFilter.PassesFilter(Animation))
				{
					Animations.Add(MakeShareable(new FWidgetAnimationListItem(Animation)));
				}
			}

			AnimationListView->RequestListRefresh();
		}
		else
		{
			SearchBoxPtr->SetError(FText::GetEmpty());
			RefreshAnimationList();
		}
	}
}

void SLexUIPrefabSequenceEditor::OnItemScrolledIntoView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedPtr<ITableRow>& InWidget) const
{
	if (InListItem->bRenameRequestPending)
	{
		StaticCastSharedPtr<SWidgetAnimationListItem>(InWidget)->BeginRename();
		InListItem->bRenameRequestPending = false;
	}
}

TSharedPtr<SWidget> SLexUIPrefabSequenceEditor::OnContextMenuOpening()const
{
	FMenuBuilder MenuBuilder(true, CommandList.ToSharedRef());

	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		//create fix button
		{
			auto SelectedItems = AnimationListView->GetSelectedItems();
			if (SelectedItems.Num() == 1)
			{
				auto SelectedItem = SelectedItems[0];
				if (!SelectedItem->Animation->IsObjectReferencesGood(WeakSequenceComponent->GetWidget()))
				{
					MenuBuilder.AddMenuSeparator();
					MenuBuilder.AddMenuEntry(
						LOCTEXT("TryFixObjectReference", "Try fix object reference"),
						LOCTEXT("TryFixObjectReference_Tooltip", "LGUI can search target object by actor's path relative to ContextActor (Owner actor of LGUIPrefabSequenceComponent), so if ActorLabel and Actor's hierarchy is same as before, it is possible to fix the bad tracks."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							SelectedItem->Animation->FixObjectReferences(WeakSequenceComponent->GetWidget());
							}))
					);
				}
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SLexUIPrefabSequenceEditor::CreateCommandList()
{
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SLexUIPrefabSequenceEditor::OnDuplicateAnimation)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SLexUIPrefabSequenceEditor::OnDeleteAnimation)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SLexUIPrefabSequenceEditor::OnRenameAnimation)
	);
}

FReply SLexUIPrefabSequenceEditor::OnNewAnimationClicked()
{
	if (WeakSequenceComponent.IsValid())
	{
		auto Sequence = WeakSequenceComponent->AddNewAnimation();
		bool bRequestRename = true;
		bool bNewAnimation = true;
		int32 NewIndex = Animations.Add(MakeShareable(new FWidgetAnimationListItem(Sequence, bRequestRename, bNewAnimation)));
		AnimationListView->RequestScrollIntoView(Animations[NewIndex]);
	}
	return FReply::Handled();
}

void SLexUIPrefabSequenceEditor::OnDuplicateAnimation()
{
	if (WeakSequenceComponent.IsValid())
	{
		GEditor->BeginTransaction(LOCTEXT("DuplicateAnimation_Transaction", "LGUISequence Duplicate Animation"));
		WeakSequenceComponent->Modify();
		auto Sequence = WeakSequenceComponent->DuplicateAnimationByIndex(CurrentSelectedAnimationIndex);
		GEditor->EndTransaction();

		if (Sequence)
		{
			bool bRequestRename = true;
			bool bNewAnimation = true;
			int32 NewIndex = Animations.Insert(MakeShareable(new FWidgetAnimationListItem(Sequence, bRequestRename, bNewAnimation)), CurrentSelectedAnimationIndex + 1);
			AnimationListView->RequestScrollIntoView(Animations[NewIndex]);
		}
	}
}
void SLexUIPrefabSequenceEditor::OnDeleteAnimation()
{
	if (WeakSequenceComponent.IsValid())
	{
		GEditor->BeginTransaction(LOCTEXT("DeleteAnimation_Transaction", "LGUISequence Delete Animation"));
		WeakSequenceComponent->Modify();
		bool bDeleted = WeakSequenceComponent->DeleteAnimationByIndex(CurrentSelectedAnimationIndex);
		GEditor->EndTransaction();

		if (bDeleted)
		{
			Animations.RemoveAt(CurrentSelectedAnimationIndex);
			AnimationListView->RebuildList();
			CurrentSelectedAnimationIndex = INDEX_NONE;
			PrefabSequenceEditor->AssignSequence(nullptr);
		}
	}
}
void SLexUIPrefabSequenceEditor::OnRenameAnimation()
{
	TArray< TSharedPtr<FWidgetAnimationListItem> > SelectedAnimations = AnimationListView->GetSelectedItems();
	check(SelectedAnimations.Num() == 1);

	TSharedPtr<FWidgetAnimationListItem> SelectedAnimation = SelectedAnimations[0];
	SelectedAnimation->bRenameRequestPending = true;

	AnimationListView->RequestScrollIntoView(SelectedAnimation);
}

#undef LOCTEXT_NAMESPACE
