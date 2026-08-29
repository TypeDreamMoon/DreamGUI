// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetAnimationEditor.h"
#include "Core/DreamUserWidget.h"
#include "K2Node_CallFunction.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "PrefabSystem/DreamUIWidgetBinding.h"
#include "DataFactory/DreamUISequenceFactory.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneCommonHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "SDreamWidgetAnimationEditorWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Misc/TextFilter.h"
#include "DreamUIEditorTools.h"
#include "SPositiveActionButton.h"
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "SDreamWidgetAnimationEditor"


struct FWidgetAnimationListItem
{
	FWidgetAnimationListItem(UDreamUIPrefabSequence* InAnimation, bool bInRenameRequestPending = false, bool bInNewAnimation = false)
		: Animation(InAnimation)
		, bRenameRequestPending(bInRenameRequestPending)
		, bNewAnimation(bInNewAnimation)
	{}

	UDreamUIPrefabSequence* Animation;
	bool bRenameRequestPending;
	bool bNewAnimation;
};


typedef SListView<TSharedPtr<FWidgetAnimationListItem> > SWidgetAnimationListView;

class SWidgetAnimationListItem : public STableRow<TSharedPtr<FWidgetAnimationListItem> >
{
public:
	SLATE_BEGIN_ARGS(SWidgetAnimationListItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SDreamWidgetAnimationEditor* InEditor, TSharedPtr<FWidgetAnimationListItem> InListItem)
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
			auto ExistIndex = SequenceArray.IndexOfByPredicate([InText, this](const UDreamUIPrefabSequence* Item) {
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
		TSharedPtr<FWidgetAnimationListItem> PinnedItem = ListItem.Pin();
		if (!PinnedItem.IsValid() || !IsValid(PinnedItem->Animation))
		{
			return;
		}
		auto Animation = PinnedItem->Animation;

		// Name has already been checked in VerifyAnimationRename
		auto NewName = InText.ToString();
		auto OldName = Animation->GetDisplayName().ToString();

		//FObjectPropertyBase* ExistingProperty = CastField<FObjectPropertyBase>(Blueprint->ParentClass->FindPropertyByName(NewFName));
		//const bool bBindWidgetAnim = ExistingProperty && FWidgetBlueprintEditorUtils::IsBindWidgetAnimProperty(ExistingProperty) && ExistingProperty->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass());

		const bool bValidName = !OldName.Equals(NewName) && !InText.IsEmpty();
		const bool bCanRename = (bValidName/* || bBindWidgetAnim*/);

		const bool bNewAnimation = PinnedItem->bNewAnimation;
		if (bCanRename)
		{
			FText TransactionName = bNewAnimation ? LOCTEXT("NewAnimation", "New Animation") : LOCTEXT("RenameAnimation", "Rename Animation");
			{
				const FScopedTransaction Transaction(TransactionName);
				Animation->Modify();

				Animation->SetDisplayNameString(NewName);
				Editor->MarkAnimationDataDirty();
				if (!bNewAnimation)
				{
					Editor->NotifyAnimationRenamed(OldName, NewName);
				}

				if (bNewAnimation)
				{
					PinnedItem->bNewAnimation = false;
					Editor->RefreshAnimationList();
				}
			}
		}
		else if (bNewAnimation)
		{
			const FScopedTransaction Transaction(LOCTEXT("NewAnimation", "New Animation"));
			PinnedItem->bNewAnimation = false;
			Editor->RefreshAnimationList();
		}
	}
private:
	TWeakPtr<FWidgetAnimationListItem> ListItem;
	SDreamWidgetAnimationEditor* Editor = nullptr;
	TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;
};


SDreamWidgetAnimationEditor::~SDreamWidgetAnimationEditor()
{
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectsReplacedHandle);
	FDreamUIEditorTools::OnEditingPrefabChanged.Remove(EditingPrefabChangedHandle);
	FEditorDelegates::PostUndoRedo.Remove(PostUndoRedoHandle);
}

void SDreamWidgetAnimationEditor::Construct(const FArguments& InArgs)
{
	SAssignNew(AnimationListView, SWidgetAnimationListView)
		.SelectionMode(ESelectionMode::SingleToggle)//clicking the selected row again leaves animation mode
		.ListItemsSource(&Animations)
		.OnGenerateRow(this, &SDreamWidgetAnimationEditor::OnGenerateRowForAnimationListView)
		.OnItemScrolledIntoView(this, &SDreamWidgetAnimationEditor::OnItemScrolledIntoView)
		.OnSelectionChanged(this, &SDreamWidgetAnimationEditor::OnAnimationListViewSelectionChanged)
		.OnContextMenuOpening(this, &SDreamWidgetAnimationEditor::OnContextMenuOpening)
		;

	ChildSlot
		[
			SNew(SSplitter)
			+SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SBox)
				.IsEnabled_Lambda([=, this]() {
					return WeakRootWidget.IsValid();
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
							SNew( SHorizontalBox )
							+ SHorizontalBox::Slot()
							.Padding(0)
							.VAlign( VAlign_Center )
							.AutoWidth()
							[
								SNew(SPositiveActionButton)
								.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
								.Text(LOCTEXT("NewAnimationButtonText", "Add Animation"))
								.OnClicked(this, &SDreamWidgetAnimationEditor::OnNewAnimationClicked)
							]
							+ SHorizontalBox::Slot()
							.Padding(2.0f, 0.0f)
							.VAlign( VAlign_Center )
							[
								SAssignNew(SearchBoxPtr, SSearchBox)
								.HintText(LOCTEXT("Search Animations", "Search Animations"))
								.OnTextChanged(this, &SDreamWidgetAnimationEditor::OnAnimationListViewSearchChanged)
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
				SAssignNew(PrefabSequenceEditor, SDreamWidgetAnimationEditorWidget)
			]
		];

	CreateCommandList();

	OnObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SDreamWidgetAnimationEditor::OnObjectsReplaced);

	PrefabSequenceEditor->AssignSequence(GetPrefabSequence());
	EditingPrefabChangedHandle = FDreamUIEditorTools::OnEditingPrefabChanged.AddRaw(this, &SDreamWidgetAnimationEditor::OnEditingPrefabChanged);
	PostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddSP(this, &SDreamWidgetAnimationEditor::OnPostUndoRedo);
}

void SDreamWidgetAnimationEditor::AssignDreamUIPrefabSequenceComponent(TWeakObjectPtr<UDreamUIPrefabSequenceComponent> InSequenceComponent)
{
	WeakSequenceComponent = InSequenceComponent;
	RefreshAnimationList();
}

UDreamUIPrefabSequence* SDreamWidgetAnimationEditor::GetPrefabSequence() const
{
	return GetSelectedAnimation();
}

FReply SDreamWidgetAnimationEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SDreamWidgetAnimationEditor::SetToolkitHost(TSharedPtr<IToolkitHost> InToolkitHost)
{
	if (PrefabSequenceEditor.IsValid())
	{
		PrefabSequenceEditor->SetToolkitHost(InToolkitHost);
	}
}

void SDreamWidgetAnimationEditor::NotifyAnimationRenamed(const FString& OldName, const FString& NewName)
{
	// Runtime code addresses an animation by its display name (PlayAnimationByDisplayName), and a
	// rename edits only the sequence -- so list the companion-blueprint calls that still say the old
	// name. A warning, not an auto-fix: a literal pin may be assembled for a different prefab.
	UDreamWidget* RootWidget = WeakRootWidget.Get();
	// The companion behaviour Blueprint a prefab used to carry its graph in. A widget class is its own
	// Blueprint, so there is nothing beside it to find.
	UBlueprint* Blueprint = nullptr;
	if (Blueprint == nullptr)
	{
		return;
	}
	int32 StaleCallCount = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call == nullptr || Call->FunctionReference.GetMemberName() != GET_FUNCTION_NAME_CHECKED(UDreamUIPrefabSequenceComponent, PlayAnimationByDisplayName))
			{
				continue;
			}
			const UEdGraphPin* NamePin = Call->FindPin(TEXT("Name"));
			if (NamePin != nullptr && NamePin->LinkedTo.Num() == 0 && NamePin->DefaultValue == OldName)
			{
				++StaleCallCount;
			}
		}
	}
	if (StaleCallCount > 0)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("RenameBreaksBlueprintCalls", "{0} call(s) in {1} still play \"{2}\". Update them to \"{3}\" or the animation will not be found."),
			FText::AsNumber(StaleCallCount), FText::FromString(Blueprint->GetName()), FText::FromString(OldName), FText::FromString(NewName)));
		Info.ExpireDuration = 8.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void SDreamWidgetAnimationEditor::ClearAnimationSelection()
{
	if (AnimationListView.IsValid())
	{
		//the selection-changed callback assigns the null sequence, which restores the pre-animated state
		AnimationListView->ClearSelection();
	}
	else if (PrefabSequenceEditor.IsValid())
	{
		PrefabSequenceEditor->AssignSequence(nullptr);
	}
}

void SDreamWidgetAnimationEditor::SelectAnimation(UDreamUIPrefabSequence* InAnimation)
{
	if (!AnimationListView.IsValid() || !IsValid(InAnimation))
	{
		return;
	}
	auto TrySelect = [this, InAnimation]()
	{
		for (const TSharedPtr<FWidgetAnimationListItem>& Item : Animations)
		{
			if (Item.IsValid() && Item->Animation == InAnimation)
			{
				AnimationListView->SetSelection(Item, ESelectInfo::Direct);
				AnimationListView->RequestScrollIntoView(Item);
				PrefabSequenceEditor->AssignSequence(InAnimation);
				return true;
			}
		}
		return false;
	};
	if (!TrySelect() && SearchBoxPtr.IsValid() && !SearchBoxPtr->GetText().IsEmpty())
	{
		SearchBoxPtr->SetText(FText::GetEmpty());
		RefreshAnimationList();
		TrySelect();
	}
}

UDreamUIPrefabSequenceComponent* SDreamWidgetAnimationEditor::FindAnimationHost(UDreamWidget* RootWidget) const
{
	if (!IsValid(RootWidget))
	{
		return nullptr;
	}

	if (UDreamUIPrefabSequenceComponent* RootHost = RootWidget->GetComponent<UDreamUIPrefabSequenceComponent>())
	{
		return RootHost;
	}

	TFunction<UDreamUIPrefabSequenceComponent*(UDreamWidget*)> FindRecursive;
	FindRecursive = [&](UDreamWidget* ParentWidget) -> UDreamUIPrefabSequenceComponent*
	{
		for (UDreamWidget* ChildWidget : ParentWidget->GetChildren())
		{
			if (!IsValid(ChildWidget))
			{
				continue;
			}

			if (UDreamUIPrefabSequenceComponent* LegacyHost = ChildWidget->GetComponent<UDreamUIPrefabSequenceComponent>())
			{
				return LegacyHost;
			}
			if (UDreamUIPrefabSequenceComponent* DescendantHost = FindRecursive(ChildWidget))
			{
				return DescendantHost;
			}
		}
		return nullptr;
	};
	return FindRecursive(RootWidget);
}

UDreamUIPrefabSequenceComponent* SDreamWidgetAnimationEditor::EnsureAnimationHost()
{
	UDreamWidget* RootWidget = WeakRootWidget.Get();
	if (!IsValid(RootWidget))
	{
		return nullptr;
	}
	if (UDreamUIPrefabSequenceComponent* ExistingHost = FindAnimationHost(RootWidget))
	{
		if (WeakSequenceComponent.Get() != ExistingHost)
		{
			AssignDreamUIPrefabSequenceComponent(ExistingHost);
		}
		return ExistingHost;
	}

	RootWidget->SetFlags(RF_Transactional);
	RootWidget->Modify();
	if (UObject* WidgetOuter = RootWidget->GetOuter())
	{
		WidgetOuter->Modify();
	}

	UDreamUIPrefabSequenceComponent* NewHost = RootWidget->AddComponent<UDreamUIPrefabSequenceComponent>();
	if (!IsValid(NewHost))
	{
		return nullptr;
	}
	NewHost->SetFlags(RF_Transactional);
	NewHost->Modify();
	FDreamUIUtils::NotifyPropertyChanged(RootWidget, UDreamWidget::GetPropertyName_Components());
	AssignDreamUIPrefabSequenceComponent(NewHost);
	MarkAnimationDataDirty();
	return NewHost;
}

void SDreamWidgetAnimationEditor::MarkAnimationDataDirty()
{
	UDreamWidget* ContextWidget = WeakRootWidget.Get();
	if (!IsValid(ContextWidget) && WeakSequenceComponent.IsValid())
	{
		ContextWidget = WeakSequenceComponent->GetWidget();
	}
}

void SDreamWidgetAnimationEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	bool bRootReplaced = false;
	if (UDreamWidget* RootWidget = WeakRootWidget.Get(true))
	{
		if (UDreamWidget* NewRootWidget = Cast<UDreamWidget>(ReplacementMap.FindRef(RootWidget)))
		{
			WeakRootWidget = NewRootWidget;
			bRootReplaced = true;
		}
	}

	if (UDreamUIPrefabSequenceComponent* Component = WeakSequenceComponent.Get(true))
	{
		if (UDreamUIPrefabSequenceComponent* NewSequenceComponent = Cast<UDreamUIPrefabSequenceComponent>(ReplacementMap.FindRef(Component)))
		{
			AssignDreamUIPrefabSequenceComponent(NewSequenceComponent);
			return;
		}
	}

	if (bRootReplaced)
	{
		AssignDreamUIPrefabSequenceComponent(FindAnimationHost(WeakRootWidget.Get()));
	}
}

TSharedRef<ITableRow> SDreamWidgetAnimationEditor::OnGenerateRowForAnimationListView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	return SNew(SWidgetAnimationListItem, InOwnerTableView, this, InListItem);
}

void SDreamWidgetAnimationEditor::OnAnimationListViewSelectionChanged(TSharedPtr<FWidgetAnimationListItem> InListItem, ESelectInfo::Type InSelectInfo)
{
	PrefabSequenceEditor->AssignSequence(GetPrefabSequence());
}

UDreamUIPrefabSequence* SDreamWidgetAnimationEditor::GetSelectedAnimation() const
{
	if (!AnimationListView.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FWidgetAnimationListItem>> SelectedItems = AnimationListView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0].IsValid() ? SelectedItems[0]->Animation : nullptr;
}

int32 SDreamWidgetAnimationEditor::GetSelectedAnimationSourceIndex() const
{
	UDreamUIPrefabSequence* SelectedAnimation = GetSelectedAnimation();
	return WeakSequenceComponent.IsValid() && IsValid(SelectedAnimation)
		? WeakSequenceComponent->GetSequenceArray().IndexOfByKey(SelectedAnimation)
		: INDEX_NONE;
}

bool SDreamWidgetAnimationEditor::CanExecuteAnimationListAction() const
{
	return GetSelectedAnimationSourceIndex() != INDEX_NONE;
}

void SDreamWidgetAnimationEditor::RefreshAnimationList()
{
	UDreamUIPrefabSequence* PreviouslySelectedAnimation = GetSelectedAnimation();
	if (AnimationListView.IsValid())
	{
		AnimationListView->ClearSelection();
	}
	Animations.Reset();
	if (WeakSequenceComponent.IsValid())
	{
		const FText SearchText = SearchBoxPtr.IsValid() ? SearchBoxPtr->GetText() : FText::GetEmpty();
		TTextFilter<UDreamUIPrefabSequence*> TextFilter(
			TTextFilter<UDreamUIPrefabSequence*>::FItemToStringArray::CreateLambda(
				[](UDreamUIPrefabSequence* InAnimation, TArray<FString>& OutFilterStrings)
				{
					OutFilterStrings.Add(InAnimation->GetDisplayNameString());
					OutFilterStrings.Add(InAnimation->GetName());
				}));
		TextFilter.SetRawFilterText(SearchText);
		if (SearchBoxPtr.IsValid())
		{
			SearchBoxPtr->SetError(TextFilter.GetFilterErrorText());
		}

		for (UDreamUIPrefabSequence* Item : WeakSequenceComponent->GetSequenceArray())
		{
			if (IsValid(Item) && (SearchText.IsEmpty() || TextFilter.PassesFilter(Item)))
			{
				Animations.Add(MakeShareable(new FWidgetAnimationListItem(Item)));
			}
		}
	}
	else if (SearchBoxPtr.IsValid())
	{
		SearchBoxPtr->SetError(FText::GetEmpty());
	}

	if (AnimationListView.IsValid())
	{
		AnimationListView->RequestListRefresh();
		TSharedPtr<FWidgetAnimationListItem>* ItemToSelect = Animations.FindByPredicate(
			[PreviouslySelectedAnimation](const TSharedPtr<FWidgetAnimationListItem>& Item)
			{
				return Item.IsValid() && Item->Animation == PreviouslySelectedAnimation;
			});
		if (ItemToSelect)
		{
			AnimationListView->SetSelection(*ItemToSelect);
		}
		else
		{
			// No animation is selected until the designer picks one. Selecting the first on every
			// refresh put the viewport into animation mode the moment a prefab with any animation was
			// opened, which is invisible as a cause while the Animations tab is closed.
			AnimationListView->ClearSelection();
			if (PrefabSequenceEditor.IsValid())
			{
				PrefabSequenceEditor->AssignSequence(nullptr);
			}
		}
	}
}

void SDreamWidgetAnimationEditor::OnPostUndoRedo()
{
	AssignDreamUIPrefabSequenceComponent(FindAnimationHost(WeakRootWidget.Get()));
}

// Trigger when opening a new prefab
void SDreamWidgetAnimationEditor::OnEditingPrefabChanged(UDreamWidget* RootWidget)
{
	WeakRootWidget = RootWidget;
	UDreamUIPrefabSequenceComponent* AnimationHost = FindAnimationHost(RootWidget);

	// A migration for animation hosts that older builds put on the transient preview root. It
	// needed the prefab helper to tell preview from authored; the designer answers that itself now,
	// and no build in this tree can produce that state any more.

	AssignDreamUIPrefabSequenceComponent(AnimationHost);
}

TSharedPtr<ISequencer> SDreamWidgetAnimationEditor::GetSequencer() const
{
	return PrefabSequenceEditor.IsValid() ? PrefabSequenceEditor->GetSequencer() : nullptr;
}

void SDreamWidgetAnimationEditor::OnAnimationListViewSearchChanged(const FText& InSearchText)
{
	RefreshAnimationList();
}

void SDreamWidgetAnimationEditor::OnItemScrolledIntoView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedPtr<ITableRow>& InWidget) const
{
	if (InListItem->bRenameRequestPending)
	{
		StaticCastSharedPtr<SWidgetAnimationListItem>(InWidget)->BeginRename();
		InListItem->bRenameRequestPending = false;
	}
}

TSharedPtr<SWidget> SDreamWidgetAnimationEditor::OnContextMenuOpening()const
{
	FMenuBuilder MenuBuilder(true, CommandList.ToSharedRef());

	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExportAnimationToAsset", "Export to Asset..."),
			LOCTEXT("ExportAnimationToAssetTooltip", "Copy this animation into a standalone DreamUI Animation asset, with bindings converted to widget paths, so a Level Sequence can play it as a subsequence."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(const_cast<SDreamWidgetAnimationEditor*>(this), &SDreamWidgetAnimationEditor::OnExportAnimationToAsset),
				FCanExecuteAction::CreateSP(const_cast<SDreamWidgetAnimationEditor*>(this), &SDreamWidgetAnimationEditor::CanExecuteAnimationListAction)));
		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		//create fix button
		{
			auto SelectedItems = AnimationListView->GetSelectedItems();
			if (SelectedItems.Num() == 1 && WeakSequenceComponent.IsValid())
			{
				auto SelectedItem = SelectedItems[0];
				if (!SelectedItem->Animation->IsObjectReferencesGood(WeakSequenceComponent->GetWidget()))
				{
					MenuBuilder.AddMenuSeparator();
					MenuBuilder.AddMenuEntry(
						LOCTEXT("TryFixObjectReference", "Try fix object reference"),
						LOCTEXT("TryFixObjectReference_Tooltip", "DreamUI can search target object by Widget's path relative to ContextObject (Owner Widget of DreamUIPrefabSequenceComponent), "
											   "so if Widget's DisplayName and Widget's hierarchy is same as before, it is possible to fix the bad tracks."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							UDreamUIPrefabSequenceComponent* SequenceComponent = WeakSequenceComponent.Get();
							UDreamUIPrefabSequence* Animation = SelectedItem->Animation;
							UDreamWidget* ContextWidget = IsValid(SequenceComponent) ? SequenceComponent->GetWidget() : nullptr;
							if (!IsValid(Animation) || !IsValid(ContextWidget))
							{
								return;
							}

							// A repair rewrites bindings the prefab has already saved, so it has to be
							// both recorded and announced: RefreshOpenedPrefabEditor closes and reopens
							// this editor without prompting, and a prefab that still looks clean takes
							// the repair down with it.
							TArray<FGuid> BrokenBindingsBefore;
							Animation->GetInvalidObjectBindingIds(ContextWidget, BrokenBindingsBefore);

							FScopedTransaction Transaction(LOCTEXT("FixObjectReference_Transaction", "Fix Animation Object References"));
							// Recorded here rather than left to UDreamUIPrefabSequence::FixObjectReferences,
							// which calls Modify() once the references have already been rewritten: the
							// snapshot a transaction restores is taken when Modify() runs, so from in
							// there it is a snapshot of the repair, and Cancel() below would keep a
							// partial repair instead of undoing it.
							Animation->SetFlags(RF_Transactional);
							Animation->Modify();
							Animation->FixObjectReferences(ContextWidget);

							TArray<FGuid> BrokenBindingsAfter;
							Animation->GetInvalidObjectBindingIds(ContextWidget, BrokenBindingsAfter);
							const int32 RepairedCount = BrokenBindingsBefore.Num() - BrokenBindingsAfter.Num();
							if (RepairedCount <= 0)
							{
								Transaction.Cancel();
								FDreamUIUtils::EditorNotification(LOCTEXT("FixObjectReferenceFailed"
									, "No animation binding could be repaired. A binding is only recoverable while the widget it was made against still sits at the same path under the animation's owner widget."), false, 8);
								return;
							}

							const_cast<SDreamWidgetAnimationEditor*>(this)->MarkAnimationDataDirty();
							FDreamUIUtils::EditorNotification(FText::Format(
								LOCTEXT("FixObjectReferenceSucceeded", "Repaired {0} animation binding(s)."), FText::AsNumber(RepairedCount)), true);
							}))
					);
				}
			}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDreamWidgetAnimationEditor::CreateCommandList()
{
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::OnDuplicateAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::CanExecuteAnimationListAction)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::OnDeleteAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::CanExecuteAnimationListAction)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::OnRenameAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditor::CanExecuteAnimationListAction)
	);
}

FReply SDreamWidgetAnimationEditor::OnNewAnimationClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add DreamUI Animation"));
	if (UDreamUIPrefabSequenceComponent* SequenceComponent = EnsureAnimationHost())
	{
		SequenceComponent->Modify();
		UDreamUIPrefabSequence* Sequence = SequenceComponent->AddNewAnimation();
		MarkAnimationDataDirty();
		if (SearchBoxPtr.IsValid())
		{
			SearchBoxPtr->SetText(FText::GetEmpty());
		}
		RefreshAnimationList();
		if (TSharedPtr<FWidgetAnimationListItem>* NewItem = Animations.FindByPredicate(
			[Sequence](const TSharedPtr<FWidgetAnimationListItem>& Item) { return Item.IsValid() && Item->Animation == Sequence; }))
		{
			(*NewItem)->bRenameRequestPending = true;
			(*NewItem)->bNewAnimation = true;
			AnimationListView->SetSelection(*NewItem);
			AnimationListView->RequestScrollIntoView(*NewItem);
		}
	}
	return FReply::Handled();
}

void SDreamWidgetAnimationEditor::OnDuplicateAnimation()
{
	const int32 SourceIndex = GetSelectedAnimationSourceIndex();
	if (WeakSequenceComponent.IsValid() && SourceIndex != INDEX_NONE)
	{
		const FScopedTransaction Transaction(LOCTEXT("DuplicateAnimation_Transaction", "DreamUISequence Duplicate Animation"));
		WeakSequenceComponent->Modify();
		UDreamUIPrefabSequence* Sequence = WeakSequenceComponent->DuplicateAnimationByIndex(SourceIndex);
		MarkAnimationDataDirty();

		if (Sequence)
		{
			if (SearchBoxPtr.IsValid())
			{
				SearchBoxPtr->SetText(FText::GetEmpty());
			}
			RefreshAnimationList();
			if (TSharedPtr<FWidgetAnimationListItem>* NewItem = Animations.FindByPredicate(
				[Sequence](const TSharedPtr<FWidgetAnimationListItem>& Item) { return Item.IsValid() && Item->Animation == Sequence; }))
			{
				(*NewItem)->bRenameRequestPending = true;
				(*NewItem)->bNewAnimation = true;
				AnimationListView->SetSelection(*NewItem);
				AnimationListView->RequestScrollIntoView(*NewItem);
			}
		}
	}
}
void SDreamWidgetAnimationEditor::OnDeleteAnimation()
{
	const int32 SourceIndex = GetSelectedAnimationSourceIndex();
	if (WeakSequenceComponent.IsValid() && SourceIndex != INDEX_NONE)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteAnimation_Transaction", "DreamUISequence Delete Animation"));
		WeakSequenceComponent->Modify();
		const bool bDeleted = WeakSequenceComponent->DeleteAnimationByIndex(SourceIndex);

		if (bDeleted)
		{
			MarkAnimationDataDirty();
			RefreshAnimationList();
		}
	}
}
void SDreamWidgetAnimationEditor::OnExportAnimationToAsset()
{
	UDreamUIPrefabSequence* Source = GetSelectedAnimation();
	UDreamWidget* RootWidget = WeakRootWidget.Get();
	if (Source == nullptr || RootWidget == nullptr)
	{
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UDreamUISequenceFactory* Factory = NewObject<UDreamUISequenceFactory>();
	UDreamUISequence* Asset = Cast<UDreamUISequence>(AssetToolsModule.Get().CreateAssetWithDialog(
		ObjectTools::SanitizeObjectName(Source->GetDisplayNameString()), TEXT("/Game"), UDreamUISequence::StaticClass(), Factory));
	if (Asset == nullptr)
	{
		return;
	}

	// The movie scene is copied whole; the bindings are rebuilt as widget paths, because the
	// embedded form's direct HelperWidget pointers mean nothing outside this prefab instance.
	Asset->Modify();
	// The asset remembers which widget CLASS it was authored against, so its own editor can put up a
	// live preview tree. The instance the animation was authored on is the class: an embedded
	// animation lives on a widget that belongs to exactly one UDreamUserWidget, preview or live.
	{
		UDreamUserWidget* OwningInstance = Cast<UDreamUserWidget>(RootWidget);
		if (OwningInstance == nullptr && RootWidget != nullptr)
		{
			OwningInstance = RootWidget->GetTypedOuter<UDreamUserWidget>();
		}
		if (OwningInstance != nullptr)
		{
			Asset->PreviewWidgetClass = OwningInstance->GetClass();
		}
	}
	UMovieScene* CopiedScene = DuplicateObject<UMovieScene>(Source->GetMovieScene(), Asset);
	Asset->MovieScene = CopiedScene;
	Asset->BindingReferences = FMovieSceneBindingReferences();

	const TSharedRef<UE::MovieScene::FSharedPlaybackState> TransientState =
		MovieSceneHelpers::CreateTransientSharedPlaybackState(RootWidget->GetWorld(), Source);
	FGuid RootGuid;
	struct FExportedBinding { FGuid Guid; FString WidgetPath; FString SubObjectPath; };
	TArray<FExportedBinding> Exported;
	TArray<FGuid> Unresolved;
	for (int32 Index = 0; Index < CopiedScene->GetPossessableCount(); ++Index)
	{
		const FGuid Guid = CopiedScene->GetPossessable(Index).GetGuid();
		TArray<UObject*, TInlineAllocator<1>> BoundObjects;
		Source->LocateBoundObjects(Guid, RootWidget, TransientState, BoundObjects);
		if (BoundObjects.Num() == 0 || !IsValid(BoundObjects[0]))
		{
			Unresolved.Add(Guid);
			continue;
		}
		FExportedBinding& Entry = Exported.AddDefaulted_GetRef();
		Entry.Guid = Guid;
		if (UDreamWidget* Widget = Cast<UDreamWidget>(BoundObjects[0]))
		{
			Entry.WidgetPath = UDreamUIWidgetBinding::BuildWidgetPathFromRoot(RootWidget, Widget);
			if (Widget == RootWidget)
			{
				RootGuid = Guid;
			}
		}
		else
		{
			UDreamWidget* OwnerWidget = BoundObjects[0]->GetTypedOuter<UDreamWidget>();
			Entry.WidgetPath = UDreamUIWidgetBinding::BuildWidgetPathFromRoot(RootWidget, OwnerWidget);
			Entry.SubObjectPath = BoundObjects[0]->GetPathName(OwnerWidget);
		}
	}
	// Every exported animation gets a root binding, present in the source or not: the subsequence
	// override re-roots the whole tree through it.
	if (!RootGuid.IsValid())
	{
		RootGuid = CopiedScene->AddPossessable(TEXT("Root"), UDreamWidget::StaticClass());
		UDreamUIWidgetBinding* RootBinding = NewObject<UDreamUIWidgetBinding>(CopiedScene, NAME_None, RF_Transactional);
		Asset->BindingReferences.AddBinding(RootGuid, RootBinding);
	}
	for (const FExportedBinding& Entry : Exported)
	{
		UDreamUIWidgetBinding* Binding = NewObject<UDreamUIWidgetBinding>(CopiedScene, NAME_None, RF_Transactional);
		Binding->WidgetPath = Entry.WidgetPath;
		Binding->SubObjectPathRelativeToWidget = Entry.SubObjectPath;
		Asset->BindingReferences.AddBinding(Entry.Guid, Binding);
		if (Entry.Guid != RootGuid)
		{
			if (FMovieScenePossessable* Possessable = CopiedScene->FindPossessable(Entry.Guid))
			{
				Possessable->SetParent(RootGuid, CopiedScene);
			}
		}
	}
	Asset->SetRootBindingGuidForExport(RootGuid);
	for (const FGuid& Guid : Unresolved)
	{
		CopiedScene->RemovePossessable(Guid);
	}
	Asset->MarkPackageDirty();

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ExportedAnimation", "Exported '{0}' to {1} ({2} bindings kept, {3} unresolved dropped)."),
		FText::FromString(Source->GetDisplayNameString()), FText::FromString(Asset->GetName()),
		FText::AsNumber(Exported.Num()), FText::AsNumber(Unresolved.Num())));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SDreamWidgetAnimationEditor::OnRenameAnimation()
{
	TArray< TSharedPtr<FWidgetAnimationListItem> > SelectedAnimations = AnimationListView->GetSelectedItems();
	if (SelectedAnimations.Num() != 1)
	{
		return;
	}

	TSharedPtr<FWidgetAnimationListItem> SelectedAnimation = SelectedAnimations[0];
	SelectedAnimation->bRenameRequestPending = true;

	AnimationListView->RequestScrollIntoView(SelectedAnimation);
}

#undef LOCTEXT_NAMESPACE
