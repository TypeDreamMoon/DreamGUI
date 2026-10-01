// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetAnimationEditor.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "Animation/DreamUISequence.h"
#include "Animation/DreamUIWidgetBinding.h"
#include "DataFactory/DreamUISequenceFactory.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneCommonHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
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
	FWidgetAnimationListItem(UDreamWidgetAnimation* InAnimation, bool bInRenameRequestPending = false, bool bInNewAnimation = false)
		: Animation(InAnimation)
		, bRenameRequestPending(bInRenameRequestPending)
		, bNewAnimation(bInNewAnimation)
	{}

	/**
	 * Weak, because nothing about a row keeps its animation alive: a compile of a text-authored asset
	 * replaces every animation with a copy, the garbage collector takes the old ones, and a row still
	 * painting one read freed memory.
	 */
	TWeakObjectPtr<UDreamWidgetAnimation> Animation;
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
		const TSharedPtr<FWidgetAnimationListItem> PinnedItem = ListItem.Pin();
		if (const UDreamWidgetAnimation* Animation = PinnedItem.IsValid() ? PinnedItem->Animation.Get() : nullptr)
		{
			return Animation->GetDisplayName();
		}

		return FText::GetEmpty();
	}

	bool OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
	{
		TSharedPtr<FWidgetAnimationListItem> PinnedItem = ListItem.Pin();
		if (!PinnedItem.IsValid())
		{
			return false;
		}
		const UDreamWidgetAnimation* Animation = PinnedItem->Animation.Get();
		if (Animation != nullptr && !Animation->IsEditable())
		{
			OutErrorMessage = LOCTEXT("NameOwnedByTheFile", "The .dui's timeline block makes this animation; rename it there");
			return false;
		}

		// Compared the way the COMPILER compares it -- sanitized to an identifier -- and against widget
		// names as well as animation names. "My Anim" and "My_Anim" are two display names that become
		// one variable name, so this dialog used to accept the second happily and the compiler then
		// exposed only the first, with a warning nobody reads at the moment they could have fixed it in
		// one keystroke. Widgets go into the same set because the compiler declares them first and a
		// collision there costs the animation its variable outright.
		const FName ProposedName = FName(*UDreamWidgetTree::SanitizeIdentifier(InText.ToString()));
		auto SequenceComp = Editor->GetSequenceComponent();
		if (SequenceComp)
		{
			auto& SequenceArray = SequenceComp->GetSequenceArray();
			auto ExistIndex = SequenceArray.IndexOfByPredicate([ProposedName, Animation](const UDreamWidgetAnimation* Item) {
				if (!IsValid(Item) || Animation == Item)
				{
					return false;
				}
				return UDreamWidgetTree::MakeAnimationVariableName(Item) == ProposedName;
				});
			if (ExistIndex != INDEX_NONE)
			{
				OutErrorMessage = LOCTEXT("NameInUseByAnimation", "An animation with this name already exists, or becomes the same variable name as one that does");
				return false;
			}
			if (UDreamWidget* HostWidget = SequenceComp->GetWidget())
			{
				if (const UDreamWidgetTree* Tree = HostWidget->GetTypedOuter<UDreamWidgetTree>())
				{
					if (Tree->FindWidgetByVariableName(ProposedName) != nullptr)
					{
						OutErrorMessage = LOCTEXT("NameInUseByWidget", "A widget in this hierarchy already uses this name; the widget would win and this animation would get no variable");
						return false;
					}
					// And against the Blueprint's own members, which the compiler refuses an animation's name to.
					if (const UBlueprint* Blueprint = Tree->GetTypedOuter<UBlueprint>())
					{
						const bool bVariable = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, ProposedName) != INDEX_NONE;
						const bool bFunction = Blueprint->FunctionGraphs.ContainsByPredicate([ProposedName](const UEdGraph* Graph)
							{
								return Graph != nullptr && Graph->GetFName() == ProposedName;
							});
						if (bVariable || bFunction)
						{
							OutErrorMessage = LOCTEXT("NameInUseByMember", "The Blueprint has a variable or a function of this name; the animation would get no variable");
							return false;
						}
					}
				}
			}
		}
		return true;
	}

	void OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
	{
		TSharedPtr<FWidgetAnimationListItem> PinnedItem = ListItem.Pin();
		if (!PinnedItem.IsValid() || !PinnedItem->Animation.IsValid())
		{
			return;
		}
		UDreamWidgetAnimation* Animation = PinnedItem->Animation.Get();

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
				if (bNewAnimation)
				{
					// Nothing can name an animation made a moment ago, so the class only has to declare it.
					Editor->MarkAnimationDataDirty();
					PinnedItem->bNewAnimation = false;
					Editor->RefreshAnimationList();
				}
				else
				{
					Editor->NotifyAnimationRenamed(OldName, NewName);
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
}

void SDreamWidgetAnimationEditor::Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner)
{
	// The panel's root is its own designer's, asked for when it is needed. It used to be set by a
	// broadcast every designer heard: opening a second asset handed every open panel that asset's root,
	// so Add, Delete and Rename Animation in one window edited another asset.
	WeakDesigner = InDesigner;

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
					return GetRootWidget() != nullptr;
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
				SAssignNew(AnimationEditorWidget, SDreamWidgetAnimationEditorWidget)
			]
		];

	CreateCommandList();

	OnObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &SDreamWidgetAnimationEditor::OnObjectsReplaced);

	AnimationEditorWidget->AssignSequence(GetAnimation());
	// Undo no longer reaches the panel from FEditorDelegates::PostUndoRedo, which every undo anywhere in
	// the editor raises: the designer asks for RefreshAnimationHost from its own PostUndo, which only an
	// undo touching its asset reaches (FDreamWidgetBlueprintEditor::MatchesContext).
	RefreshAnimationHost();
}

UDreamWidget* SDreamWidgetAnimationEditor::GetRootWidget() const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = WeakDesigner.Pin();
	return Designer.IsValid() ? Designer->GetAnimationHostWidget() : nullptr;
}

UDreamWidgetBlueprint* SDreamWidgetAnimationEditor::GetWidgetBlueprint() const
{
	if (const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = WeakDesigner.Pin())
	{
		return Designer->GetWidgetBlueprint();
	}
	return WeakSequenceComponent.IsValid() ? WeakSequenceComponent->GetTypedOuter<UDreamWidgetBlueprint>() : nullptr;
}

void SDreamWidgetAnimationEditor::RefreshAnimationHost()
{
	// Remembered before the host moves: the selected animation is kept alive by the sequencer playing it,
	// so its name is still there to ask for after a compile has replaced it.
	const UDreamWidgetAnimation* Selected = GetSelectedAnimation();
	const FString SelectedName = Selected != nullptr ? Selected->GetDisplayNameString() : FString();
	WeakSequenceComponent = FindAnimationHost(GetRootWidget());
	RebuildAnimationList(Selected, SelectedName);
}

void SDreamWidgetAnimationEditor::AssignDreamWidgetAnimationComponent(TWeakObjectPtr<UDreamWidgetAnimationComponent> InSequenceComponent)
{
	WeakSequenceComponent = InSequenceComponent;
	RefreshAnimationList();
}

UDreamWidgetAnimation* SDreamWidgetAnimationEditor::GetAnimation() const
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
	if (AnimationEditorWidget.IsValid())
	{
		AnimationEditorWidget->SetToolkitHost(InToolkitHost);
	}
}

void SDreamWidgetAnimationEditor::NotifyAnimationRenamed(const FString& OldName, const FString& NewName)
{
	UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (Blueprint == nullptr)
	{
		return;
	}

	// The animation's member variable. Its guid is derived from its name, so nothing found the renamed
	// variable again and every node that read the old one failed the next compile. The class declares
	// the new name first, because until it does, a node renamed below is renamed straight back by its
	// old guid (see MigrateVariableReferences); then the references move, with the refusals a widget's
	// `(was:)` rename makes.
	MarkAnimationDataDirty();
	const FName OldVariableName(*UDreamWidgetTree::SanitizeIdentifier(OldName));
	const FName NewVariableName(*UDreamWidgetTree::SanitizeIdentifier(NewName));
	FString Refusal;
	FDreamWidgetBlueprintCompilerContext::MigrateVariableReferences(Blueprint, OldVariableName, NewVariableName, Refusal);
	if (!Refusal.IsEmpty())
	{
		FDreamUIUtils::EditorNotification(FText::Format(
			LOCTEXT("RenameKeptGraphReferences", "The graph references to \"{0}\" were not moved to \"{1}\": {2}. Repoint them by hand."),
			FText::FromName(OldVariableName), FText::FromName(NewVariableName), FText::FromString(Refusal)), false, 8);
	}

	// Runtime code can also address an animation by its display name (PlayAnimationByDisplayName), and
	// a literal name in a graph is not a reference anything can move -- so list the calls that still say
	// the old name. A warning, not an auto-fix: a literal pin may be meant for another widget's animation.
	int32 StaleCallCount = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call == nullptr || Call->FunctionReference.GetMemberName() != GET_FUNCTION_NAME_CHECKED(UDreamWidgetAnimationComponent, PlayAnimationByDisplayName))
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
	else if (AnimationEditorWidget.IsValid())
	{
		AnimationEditorWidget->AssignSequence(nullptr);
	}
}

void SDreamWidgetAnimationEditor::SelectAnimation(UDreamWidgetAnimation* InAnimation)
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
				AnimationEditorWidget->AssignSequence(InAnimation);
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

UDreamWidgetAnimationComponent* SDreamWidgetAnimationEditor::FindAnimationHost(UDreamWidget* RootWidget) const
{
	if (!IsValid(RootWidget))
	{
		return nullptr;
	}

	if (UDreamWidgetAnimationComponent* RootHost = RootWidget->GetComponent<UDreamWidgetAnimationComponent>())
	{
		return RootHost;
	}

	TFunction<UDreamWidgetAnimationComponent*(UDreamWidget*)> FindRecursive;
	FindRecursive = [&](UDreamWidget* ParentWidget) -> UDreamWidgetAnimationComponent*
	{
		for (UDreamWidget* ChildWidget : ParentWidget->GetChildren())
		{
			if (!IsValid(ChildWidget))
			{
				continue;
			}

			if (UDreamWidgetAnimationComponent* LegacyHost = ChildWidget->GetComponent<UDreamWidgetAnimationComponent>())
			{
				return LegacyHost;
			}
			if (UDreamWidgetAnimationComponent* DescendantHost = FindRecursive(ChildWidget))
			{
				return DescendantHost;
			}
		}
		return nullptr;
	};
	return FindRecursive(RootWidget);
}

UDreamWidgetAnimationComponent* SDreamWidgetAnimationEditor::EnsureAnimationHost()
{
	UDreamWidget* RootWidget = GetRootWidget();
	if (!IsValid(RootWidget))
	{
		return nullptr;
	}
	if (UDreamWidgetAnimationComponent* ExistingHost = FindAnimationHost(RootWidget))
	{
		if (WeakSequenceComponent.Get() != ExistingHost)
		{
			AssignDreamWidgetAnimationComponent(ExistingHost);
		}
		return ExistingHost;
	}

	RootWidget->SetFlags(RF_Transactional);
	RootWidget->Modify();
	if (UObject* WidgetOuter = RootWidget->GetOuter())
	{
		WidgetOuter->Modify();
	}

	UDreamWidgetAnimationComponent* NewHost = RootWidget->AddComponent<UDreamWidgetAnimationComponent>();
	if (!IsValid(NewHost))
	{
		return nullptr;
	}
	NewHost->SetFlags(RF_Transactional);
	NewHost->Modify();
	FDreamUIUtils::NotifyPropertyChanged(RootWidget, UDreamWidget::GetPropertyName_Components());
	AssignDreamWidgetAnimationComponent(NewHost);
	// Not dirtied here: an empty host changes no member of the class, and the animation the caller adds
	// next does. Marking now would compile in between, and for a text-authored asset a compile rebuilds
	// the tree from its file and carries only hosts that hold an animation -- this one would be dropped
	// before the animation reached it.
	return NewHost;
}

void SDreamWidgetAnimationEditor::MarkAnimationDataDirty(bool bStructural)
{
	UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint();
	if (Blueprint == nullptr)
	{
		return;
	}
	if (bStructural)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void SDreamWidgetAnimationEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	// The host is replaced with its widget when that widget's class recompiles. The root itself is not
	// held here -- the designer is asked for it -- so the host is the one thing to follow.
	if (UDreamWidgetAnimationComponent* Component = WeakSequenceComponent.Get(true))
	{
		if (UDreamWidgetAnimationComponent* NewSequenceComponent = Cast<UDreamWidgetAnimationComponent>(ReplacementMap.FindRef(Component)))
		{
			AssignDreamWidgetAnimationComponent(NewSequenceComponent);
		}
	}
}

TSharedRef<ITableRow> SDreamWidgetAnimationEditor::OnGenerateRowForAnimationListView(TSharedPtr<FWidgetAnimationListItem> InListItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	return SNew(SWidgetAnimationListItem, InOwnerTableView, this, InListItem);
}

void SDreamWidgetAnimationEditor::OnAnimationListViewSelectionChanged(TSharedPtr<FWidgetAnimationListItem> InListItem, ESelectInfo::Type InSelectInfo)
{
	if (bRebuildingAnimationList)
	{
		return;
	}
	AnimationEditorWidget->AssignSequence(GetAnimation());
}

UDreamWidgetAnimation* SDreamWidgetAnimationEditor::GetSelectedAnimation() const
{
	if (!AnimationListView.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FWidgetAnimationListItem>> SelectedItems = AnimationListView->GetSelectedItems();
	return SelectedItems.Num() == 1 && SelectedItems[0].IsValid() ? SelectedItems[0]->Animation.Get() : nullptr;
}

int32 SDreamWidgetAnimationEditor::GetSelectedAnimationSourceIndex() const
{
	UDreamWidgetAnimation* SelectedAnimation = GetSelectedAnimation();
	return WeakSequenceComponent.IsValid() && IsValid(SelectedAnimation)
		? WeakSequenceComponent->GetSequenceArray().IndexOfByKey(SelectedAnimation)
		: INDEX_NONE;
}

bool SDreamWidgetAnimationEditor::CanExecuteAnimationListAction() const
{
	// An animation the .dui's timeline block builds is the file's. Renamed, deleted or copied here, it came back from the
	// file at the next compile -- and a rename first moved the graph's references onto a name the class then did not
	// have.
	const UDreamWidgetAnimation* Selected = GetSelectedAnimation();
	return GetSelectedAnimationSourceIndex() != INDEX_NONE && Selected != nullptr && Selected->IsEditable();
}

void SDreamWidgetAnimationEditor::RefreshAnimationList()
{
	const UDreamWidgetAnimation* Selected = GetSelectedAnimation();
	RebuildAnimationList(Selected, Selected != nullptr ? Selected->GetDisplayNameString() : FString());
}

TSharedPtr<FWidgetAnimationListItem> SDreamWidgetAnimationEditor::FindListItem(const UDreamWidgetAnimation* InAnimation, const FString& InName) const
{
	if (InAnimation != nullptr)
	{
		if (const TSharedPtr<FWidgetAnimationListItem>* Item = Animations.FindByPredicate(
			[InAnimation](const TSharedPtr<FWidgetAnimationListItem>& Candidate) { return Candidate.IsValid() && Candidate->Animation == InAnimation; }))
		{
			return *Item;
		}
	}
	if (!InName.IsEmpty())
	{
		if (const TSharedPtr<FWidgetAnimationListItem>* Item = Animations.FindByPredicate(
			[&InName](const TSharedPtr<FWidgetAnimationListItem>& Candidate)
			{
				return Candidate.IsValid() && Candidate->Animation.IsValid() && Candidate->Animation->GetDisplayNameString() == InName;
			}))
		{
			return *Item;
		}
	}
	return nullptr;
}

void SDreamWidgetAnimationEditor::RebuildAnimationList(const UDreamWidgetAnimation* InSelected, const FString& InSelectedName)
{
	// The selection callback sits this out, and the sequence is assigned once at the end. Clearing and
	// then re-selecting went through "nothing selected" on the way, which releases Sequencer and builds
	// a new one: every refresh -- every undo anywhere in the editor, when the panel listened for those --
	// threw the open animation's Sequencer away for an identical one.
	{
		TGuardValue<bool> RebuildGuard(bRebuildingAnimationList, true);
		if (AnimationListView.IsValid())
		{
			AnimationListView->ClearSelection();
		}
		Animations.Reset();
		if (WeakSequenceComponent.IsValid())
		{
			const FText SearchText = SearchBoxPtr.IsValid() ? SearchBoxPtr->GetText() : FText::GetEmpty();
			TTextFilter<UDreamWidgetAnimation*> TextFilter(
				TTextFilter<UDreamWidgetAnimation*>::FItemToStringArray::CreateLambda(
					[](UDreamWidgetAnimation* InAnimation, TArray<FString>& OutFilterStrings)
					{
						OutFilterStrings.Add(InAnimation->GetDisplayNameString());
						OutFilterStrings.Add(InAnimation->GetName());
					}));
			TextFilter.SetRawFilterText(SearchText);
			if (SearchBoxPtr.IsValid())
			{
				SearchBoxPtr->SetError(TextFilter.GetFilterErrorText());
			}

			for (UDreamWidgetAnimation* Item : WeakSequenceComponent->GetSequenceArray())
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
			// The same animation when it is still listed, else the one that took its name -- the copy a
			// text-authored asset's compile made of it. With neither, nothing: no animation is selected
			// until the designer picks one. Selecting the first on every refresh put the viewport into
			// animation mode the moment a prefab with any animation was opened, which is invisible as a
			// cause while the Animations tab is closed.
			if (const TSharedPtr<FWidgetAnimationListItem> ItemToSelect = FindListItem(InSelected, InSelectedName))
			{
				AnimationListView->SetSelection(ItemToSelect);
			}
		}
	}

	// Once, for the whole rebuild. The same animation keeps the Sequencer it has, a copy found by name
	// takes that Sequencer over, and an animation that is gone leaves animation mode.
	if (AnimationEditorWidget.IsValid())
	{
		AnimationEditorWidget->AssignSequence(GetSelectedAnimation());
	}
}

TSharedPtr<ISequencer> SDreamWidgetAnimationEditor::GetSequencer() const
{
	return AnimationEditorWidget.IsValid() ? AnimationEditorWidget->GetSequencer() : nullptr;
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
				if (SelectedItem->Animation.IsValid() && !SelectedItem->Animation->IsObjectReferencesGood(WeakSequenceComponent->GetWidget()))
				{
					MenuBuilder.AddMenuSeparator();
					MenuBuilder.AddMenuEntry(
						LOCTEXT("TryFixObjectReference", "Try fix object reference"),
						LOCTEXT("TryFixObjectReference_Tooltip", "DreamUI can search target object by Widget's path relative to ContextObject (Owner Widget of DreamWidgetAnimationComponent), "
											   "so if Widget's DisplayName and Widget's hierarchy is same as before, it is possible to fix the bad tracks."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							UDreamWidgetAnimationComponent* SequenceComponent = WeakSequenceComponent.Get();
							UDreamWidgetAnimation* Animation = SelectedItem->Animation.Get();
							UDreamWidget* ContextWidget = IsValid(SequenceComponent) ? SequenceComponent->GetWidget() : nullptr;
							if (!IsValid(Animation) || !IsValid(ContextWidget))
							{
								return;
							}

							// A repair rewrites bindings the prefab has already saved, so it has to be
							// both recorded and announced: RefreshOpenedDesigner closes and reopens
							// this editor without prompting, and a prefab that still looks clean takes
							// the repair down with it.
							TArray<FGuid> BrokenBindingsBefore;
							Animation->GetInvalidObjectBindingIds(ContextWidget, BrokenBindingsBefore);

							FScopedTransaction Transaction(LOCTEXT("FixObjectReference_Transaction", "Fix Animation Object References"));
							// Recorded here rather than left to UDreamWidgetAnimation::FixObjectReferences,
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

							// A value edit: the bindings changed, no member of the class did.
							const_cast<SDreamWidgetAnimationEditor*>(this)->MarkAnimationDataDirty(/*bStructural*/false);
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

void SDreamWidgetAnimationEditor::BeginRenamingNewAnimation(const UDreamWidgetAnimation* InAnimation, const FString& InName)
{
	// By name as well as by object: the compile that declares the new animation's variable rebuilds a
	// text-authored asset's tree from its file, and the row the list shows afterwards is the copy.
	if (const TSharedPtr<FWidgetAnimationListItem> NewItem = FindListItem(InAnimation, InName))
	{
		NewItem->bRenameRequestPending = true;
		NewItem->bNewAnimation = true;
		AnimationListView->SetSelection(NewItem);
		AnimationListView->RequestScrollIntoView(NewItem);
	}
}

FReply SDreamWidgetAnimationEditor::OnNewAnimationClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add DreamUI Animation"));
	if (UDreamWidgetAnimationComponent* SequenceComponent = EnsureAnimationHost())
	{
		SequenceComponent->Modify();
		UDreamWidgetAnimation* Sequence = SequenceComponent->AddNewAnimation();
		const FString SequenceName = Sequence != nullptr ? Sequence->GetDisplayNameString() : FString();
		MarkAnimationDataDirty();
		if (SearchBoxPtr.IsValid())
		{
			SearchBoxPtr->SetText(FText::GetEmpty());
		}
		RefreshAnimationList();
		BeginRenamingNewAnimation(Sequence, SequenceName);
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
		UDreamWidgetAnimation* Sequence = WeakSequenceComponent->DuplicateAnimationByIndex(SourceIndex);
		const FString SequenceName = Sequence != nullptr ? Sequence->GetDisplayNameString() : FString();
		MarkAnimationDataDirty();

		if (Sequence)
		{
			if (SearchBoxPtr.IsValid())
			{
				SearchBoxPtr->SetText(FText::GetEmpty());
			}
			RefreshAnimationList();
			BeginRenamingNewAnimation(Sequence, SequenceName);
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
	UDreamWidgetAnimation* Source = GetSelectedAnimation();
	if (Source == nullptr || GetRootWidget() == nullptr)
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

	int32 KeptCount = 0;
	int32 DroppedCount = 0;
	if (!ExportAnimationToAsset(Source, Asset, KeptCount, DroppedCount))
	{
		return;
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("ExportedAnimation", "Exported '{0}' to {1} ({2} bindings kept, {3} unresolved dropped)."),
		FText::FromString(Source->GetDisplayNameString()), FText::FromString(Asset->GetName()),
		FText::AsNumber(KeptCount), FText::AsNumber(DroppedCount)));
	Info.ExpireDuration = 6.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

bool SDreamWidgetAnimationEditor::ExportAnimationToAsset(UDreamWidgetAnimation* InSource, UDreamUISequence* InAsset,
	int32& OutKept, int32& OutDropped) const
{
	OutKept = 0;
	OutDropped = 0;
	UDreamWidget* RootWidget = GetRootWidget();
	if (InSource == nullptr || InAsset == nullptr || RootWidget == nullptr)
	{
		return false;
	}
	UDreamWidgetAnimation* Source = InSource;
	UDreamUISequence* Asset = InAsset;

	// The movie scene is copied whole; the bindings are rebuilt as widget paths, because the
	// embedded form's direct HelperWidget pointers mean nothing outside this prefab instance.
	Asset->Modify();
	// The asset remembers which widget CLASS it was authored against, so its own editor can put up a
	// live preview tree: this designer's class. It used to be read off the user widget the root
	// belongs to, which an authored root has none of -- it is outered to the asset's tree -- so every
	// export came out with no preview class at all.
	if (const UDreamWidgetBlueprint* Blueprint = GetWidgetBlueprint())
	{
		if (Blueprint->GeneratedClass != nullptr && Blueprint->GeneratedClass->IsChildOf(UDreamUserWidget::StaticClass()))
		{
			Asset->PreviewWidgetClass = Blueprint->GeneratedClass.Get();
		}
	}
	UMovieScene* CopiedScene = DuplicateObject<UMovieScene>(Source->GetMovieScene(), Asset);
	Asset->MovieScene = CopiedScene;
	Asset->BindingReferences = FMovieSceneBindingReferences();

	// The authored root is the playback context the state is made for, as well as the context the
	// bindings resolve against. It is in no world, and asking it for one handed this a null that
	// CreateTransientSharedPlaybackState verifies against: fatal in an editor build, after the dialog
	// had already made the package. The state is only the vessel LocateBoundObjects is handed --
	// resolution walks names down from the context and never reads a world.
	const TSharedRef<UE::MovieScene::FSharedPlaybackState> TransientState =
		MovieSceneHelpers::CreateTransientSharedPlaybackState(RootWidget, Source);
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

	OutKept = Exported.Num();
	OutDropped = Unresolved.Num();
	return true;
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
