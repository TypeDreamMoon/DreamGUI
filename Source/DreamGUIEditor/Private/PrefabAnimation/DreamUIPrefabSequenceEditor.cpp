// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabSequenceEditor.h"

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "DreamUIPrefabSequenceEditorWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Misc/TextFilter.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "DreamUIEditorTools.h"
#include "SPositiveActionButton.h"
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "SDreamUIPrefabSequenceEditor"


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

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SDreamUIPrefabSequenceEditor* InEditor, TSharedPtr<FWidgetAnimationListItem> InListItem)
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
	SDreamUIPrefabSequenceEditor* Editor = nullptr;
	TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;
};


SDreamUIPrefabSequenceEditor::~SDreamUIPrefabSequenceEditor()
{
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectsReplacedHandle);
	FDreamUIEditorTools::OnEditingPrefabChanged.Remove(EditingPrefabChangedHandle);
	FDreamUIEditorTools::OnBeforeApplyPrefab.Remove(OnBeforeApplyPrefabHandle);
	FEditorDelegates::PostUndoRedo.Remove(PostUndoRedoHandle);
}

void SDreamUIPrefabSequenceEditor::Construct(const FArguments& InArgs)
{
	SAssignNew(AnimationListView, SWidgetAnimationListView)
		.SelectionMode(ESelectionMode::Single)
		.ListItemsSource(&Animations)
		.OnGenerateRow(this, &SDreamUIPrefabSequenceEditor::OnGenerateRowForAnimationListView)
		.OnItemScrolledIntoView(this, &SDreamUIPrefabSequenceEditor::OnItemScrolledIntoView)
		.OnSelectionChanged(this, &SDreamUIPrefabSequenceEditor::OnAnimationListViewSelectionChanged)
		.OnContextMenuOpening(this, &SDreamUIPrefabSequenceEditor::OnContextMenuOpening)
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
								.OnClicked(this, &SDreamUIPrefabSequenceEditor::OnNewAnimationClicked)
							]
							+ SHorizontalBox::Slot()
							.Padding(2.0f, 0.0f)
							.VAlign( VAlign_Center )
							[
								SAssignNew(SearchBoxPtr, SSearchBox)
								.HintText(LOCTEXT("Search Animations", "Search Animations"))
								.OnTextChanged(this, &SDreamUIPrefabSequenceEditor::OnAnimationListViewSearchChanged)
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
				SAssignNew(PrefabSequenceEditor, SDreamUIPrefabSequenceEditorWidget)
			]
		];

	CreateCommandList();

	OnObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SDreamUIPrefabSequenceEditor::OnObjectsReplaced);

	PrefabSequenceEditor->AssignSequence(GetPrefabSequence());
	EditingPrefabChangedHandle = FDreamUIEditorTools::OnEditingPrefabChanged.AddRaw(this, &SDreamUIPrefabSequenceEditor::OnEditingPrefabChanged);
	OnBeforeApplyPrefabHandle = FDreamUIEditorTools::OnBeforeApplyPrefab.AddRaw(this, &SDreamUIPrefabSequenceEditor::OnBeforeApplyPrefab);
	PostUndoRedoHandle = FEditorDelegates::PostUndoRedo.AddSP(this, &SDreamUIPrefabSequenceEditor::OnPostUndoRedo);
}

void SDreamUIPrefabSequenceEditor::AssignDreamUIPrefabSequenceComponent(TWeakObjectPtr<UDreamUIPrefabSequenceComponent> InSequenceComponent)
{
	WeakSequenceComponent = InSequenceComponent;
	RefreshAnimationList();
}

UDreamUIPrefabSequence* SDreamUIPrefabSequenceEditor::GetPrefabSequence() const
{
	return GetSelectedAnimation();
}

void SDreamUIPrefabSequenceEditor::SelectAnimation(UDreamUIPrefabSequence* InAnimation)
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

UDreamUIPrefabSequenceComponent* SDreamUIPrefabSequenceEditor::FindAnimationHost(UDreamWidget* RootWidget) const
{
	if (!IsValid(RootWidget))
	{
		return nullptr;
	}

	if (UDreamUIPrefabSequenceComponent* RootHost = RootWidget->GetComponent<UDreamUIPrefabSequenceComponent>())
	{
		return RootHost;
	}

	UDreamUIPrefabHelperObject* PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(RootWidget);
	TFunction<UDreamUIPrefabSequenceComponent*(UDreamWidget*)> FindRecursive;
	FindRecursive = [&](UDreamWidget* ParentWidget) -> UDreamUIPrefabSequenceComponent*
	{
		for (UDreamWidget* ChildWidget : ParentWidget->GetChildren())
		{
			if (!IsValid(ChildWidget)
				|| (PrefabHelperObject && PrefabHelperObject->IsWidgetBelongsToSubPrefab(ChildWidget)))
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

UDreamUIPrefabSequenceComponent* SDreamUIPrefabSequenceEditor::EnsureAnimationHost()
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

void SDreamUIPrefabSequenceEditor::MarkAnimationDataDirty()
{
	UDreamWidget* ContextWidget = WeakRootWidget.Get();
	if (!IsValid(ContextWidget) && WeakSequenceComponent.IsValid())
	{
		ContextWidget = WeakSequenceComponent->GetWidget();
	}
	if (UDreamUIPrefabHelperObject* Helper = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ContextWidget))
	{
		Helper->Modify();
		Helper->SetAnythingDirty();
	}
}

void SDreamUIPrefabSequenceEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
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

TSharedRef<ITableRow> SDreamUIPrefabSequenceEditor::OnGenerateRowForAnimationListView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	return SNew(SWidgetAnimationListItem, InOwnerTableView, this, InListItem);
}

void SDreamUIPrefabSequenceEditor::OnAnimationListViewSelectionChanged(TSharedPtr<FWidgetAnimationListItem> InListItem, ESelectInfo::Type InSelectInfo)
{
	PrefabSequenceEditor->AssignSequence(GetPrefabSequence());
}

UDreamUIPrefabSequence* SDreamUIPrefabSequenceEditor::GetSelectedAnimation() const
{
	if (!AnimationListView.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FWidgetAnimationListItem>> SelectedItems = AnimationListView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0].IsValid() ? SelectedItems[0]->Animation : nullptr;
}

int32 SDreamUIPrefabSequenceEditor::GetSelectedAnimationSourceIndex() const
{
	UDreamUIPrefabSequence* SelectedAnimation = GetSelectedAnimation();
	return WeakSequenceComponent.IsValid() && IsValid(SelectedAnimation)
		? WeakSequenceComponent->GetSequenceArray().IndexOfByKey(SelectedAnimation)
		: INDEX_NONE;
}

bool SDreamUIPrefabSequenceEditor::CanExecuteAnimationListAction() const
{
	return GetSelectedAnimationSourceIndex() != INDEX_NONE;
}

void SDreamUIPrefabSequenceEditor::RefreshAnimationList()
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
		else if (Animations.Num() > 0)
		{
			AnimationListView->SetSelection(Animations[0]);
		}
		else if (PrefabSequenceEditor.IsValid())
		{
			PrefabSequenceEditor->AssignSequence(nullptr);
		}
	}
}

void SDreamUIPrefabSequenceEditor::OnBeforeApplyPrefab(UDreamUIPrefabHelperObject* InObject)
{
	if (WeakSequenceComponent.IsValid())
	{
		if (auto Widget = WeakSequenceComponent->GetWidget())
		{
			if (InObject->IsWidgetBelongsToThis(Widget))
			{
				// Last point at which a binding's object pointer is still live: apply serializes the
				// tree and drops it, leaving only the display-name path behind to repair from.
				WeakSequenceComponent->FixEditorHelpers();
				this->AnimationListView->ClearSelection();
			}
		}
	}
}

void SDreamUIPrefabSequenceEditor::OnPostUndoRedo()
{
	AssignDreamUIPrefabSequenceComponent(FindAnimationHost(WeakRootWidget.Get()));
}

// Trigger when opening a new prefab
void SDreamUIPrefabSequenceEditor::OnEditingPrefabChanged(UDreamWidget* RootWidget)
{
	WeakRootWidget = RootWidget;
	UDreamUIPrefabSequenceComponent* AnimationHost = FindAnimationHost(RootWidget);

	// Older editor builds could create the animation host on the transient preview root.
	// Move its data into the serialized prefab hierarchy while that preview is still alive.
	if (!IsValid(AnimationHost) && IsValid(RootWidget))
	{
		UDreamWidget* PreviewRoot = RootWidget->GetParent();
		UDreamUIPrefabHelperObject* PrefabHelperObject =
			UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(RootWidget);
		if (IsValid(PreviewRoot)
			&& IsValid(PrefabHelperObject)
			&& !PrefabHelperObject->IsWidgetBelongsToThis(PreviewRoot))
		{
			UDreamUIPrefabSequenceComponent* TemporaryHost =
				PreviewRoot->GetComponent<UDreamUIPrefabSequenceComponent>();
			if (IsValid(TemporaryHost) && !TemporaryHost->GetSequenceArray().IsEmpty())
			{
				const FScopedTransaction Transaction(LOCTEXT(
					"MigrateTemporaryAnimationHost",
					"Move Animations Into Prefab"));
				RootWidget->SetFlags(RF_Transactional);
				RootWidget->Modify();
				PreviewRoot->Modify();
				if (UObject* WidgetOuter = RootWidget->GetOuter())
				{
					WidgetOuter->Modify();
				}

				AnimationHost = RootWidget->AddComponentByTemplate<UDreamUIPrefabSequenceComponent>(TemporaryHost);
				if (IsValid(AnimationHost))
				{
					bool bAnimationDataWasInstanced =
						AnimationHost->GetSequenceArray().Num() == TemporaryHost->GetSequenceArray().Num();
					for (UDreamUIPrefabSequence* Sequence : AnimationHost->GetSequenceArray())
					{
						if (!IsValid(Sequence)
							|| Sequence->GetOuter() != AnimationHost
							|| !IsValid(Sequence->GetMovieScene())
							|| Sequence->GetMovieScene()->GetOuter() != Sequence)
						{
							bAnimationDataWasInstanced = false;
							break;
						}
					}

					if (bAnimationDataWasInstanced)
					{
						for (UDreamUIPrefabSequence* Sequence : AnimationHost->GetSequenceArray())
						{
							Sequence->FixEditorHelpers(RootWidget);
						}
						AnimationHost->SetFlags(RF_Transactional);
						AnimationHost->Modify();
						PreviewRoot->RemoveComponent(TemporaryHost);
						FDreamUIUtils::NotifyPropertyChanged(RootWidget, UDreamWidget::GetPropertyName_Components());
						MarkAnimationDataDirty();
					}
					else
					{
						RootWidget->RemoveComponent(AnimationHost);
						AnimationHost = nullptr;
					}
				}
			}
		}
	}

	AssignDreamUIPrefabSequenceComponent(AnimationHost);
}

TSharedPtr<ISequencer> SDreamUIPrefabSequenceEditor::GetSequencer() const
{
	return PrefabSequenceEditor.IsValid() ? PrefabSequenceEditor->GetSequencer() : nullptr;
}

void SDreamUIPrefabSequenceEditor::OnAnimationListViewSearchChanged(const FText& InSearchText)
{
	RefreshAnimationList();
}

void SDreamUIPrefabSequenceEditor::OnItemScrolledIntoView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedPtr<ITableRow>& InWidget) const
{
	if (InListItem->bRenameRequestPending)
	{
		StaticCastSharedPtr<SWidgetAnimationListItem>(InWidget)->BeginRename();
		InListItem->bRenameRequestPending = false;
	}
}

TSharedPtr<SWidget> SDreamUIPrefabSequenceEditor::OnContextMenuOpening()const
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

							const_cast<SDreamUIPrefabSequenceEditor*>(this)->MarkAnimationDataDirty();
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

void SDreamUIPrefabSequenceEditor::CreateCommandList()
{
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::OnDuplicateAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::CanExecuteAnimationListAction)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::OnDeleteAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::CanExecuteAnimationListAction)
	);

	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::OnRenameAnimation),
		FCanExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditor::CanExecuteAnimationListAction)
	);
}

FReply SDreamUIPrefabSequenceEditor::OnNewAnimationClicked()
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

void SDreamUIPrefabSequenceEditor::OnDuplicateAnimation()
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
void SDreamUIPrefabSequenceEditor::OnDeleteAnimation()
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
void SDreamUIPrefabSequenceEditor::OnRenameAnimation()
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
