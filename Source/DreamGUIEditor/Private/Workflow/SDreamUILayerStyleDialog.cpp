// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/SDreamUILayerStyleDialog.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Workflow/DreamUIStylePresets.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyHandle.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

#define LOCTEXT_NAMESPACE "SDreamUILayerStyleDialog"

namespace DreamUILayerStyleDialogLocal
{
	/** The dialog open for each designer: one at a time. */
	TMap<const FDreamWidgetBlueprintEditor*, TWeakPtr<SDreamUILayerStyleDialog>>& OpenDialogs()
	{
		static TMap<const FDreamWidgetBlueprintEditor*, TWeakPtr<SDreamUILayerStyleDialog>> Dialogs;
		return Dialogs;
	}

	/** A details layout that is a function: the dialog's own, for the page it shows. */
	class FPageLayout : public IDetailCustomization
	{
	public:
		explicit FPageLayout(TFunction<void(IDetailLayoutBuilder&)> InLayout) : Layout(MoveTemp(InLayout)) {}
		virtual void CustomizeDetails(IDetailLayoutBuilder& InBuilder) override { Layout(InBuilder); }

	private:
		TFunction<void(IDetailLayoutBuilder&)> Layout;
	};

	/** The fields of Blending Options, in order: the layer's blending, then the style's values every effect shares. */
	const FName BlendingFields[] = {
		GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, BlendMode), GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, Opacity),
		GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, FillOpacity), GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, bEnabled),
		GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, Scale), GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, GlobalLightAngle),
		GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, GlobalLightAltitude) };

	/** A field of a list's element: differs from, and resets to, the field of InDefaults (a new element). */
	FResetToDefaultOverride ResetToElementDefault(const TSharedRef<FStructOnScope>& InDefaults)
	{
		const auto DefaultOf = [InDefaults](const FProperty& InProperty) { return InProperty.ContainerPtrToValuePtr<void>(InDefaults->GetStructMemory()); };
		return FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateLambda([DefaultOf](TSharedPtr<IPropertyHandle> InHandle)
			{
				const FProperty* Property = InHandle.IsValid() ? InHandle->GetProperty() : nullptr;
				TArray<void*> Values;
				if (Property != nullptr)
				{
					InHandle->AccessRawData(Values);
				}
				return Values.Num() == 1 && Values[0] != nullptr && !Property->Identical(Values[0], DefaultOf(*Property));
			}),
			FResetToDefaultHandler::CreateLambda([DefaultOf](TSharedPtr<IPropertyHandle> InHandle)
			{
				if (const FProperty* Property = InHandle.IsValid() ? InHandle->GetProperty() : nullptr)
				{
					FString Text;
					Property->ExportTextItem_Direct(Text, DefaultOf(*Property), nullptr, nullptr, PPF_None);
					InHandle->SetValueFromFormattedString(Text);
				}
			}));
	}

	/** A row of an effect there can be several of, being dragged to another place among its kind. */
	class FRowDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FRowDragDropOp, FDecoratedDragDropOp)
		FDreamUIStyleDialogRow Row;

		static TSharedRef<FRowDragDropOp> New(const FDreamUIStyleDialogRow& InRow, const FText& InLabel)
		{
			TSharedRef<FRowDragDropOp> Operation = MakeShared<FRowDragDropOp>();
			Operation->Row = InRow;
			Operation->DefaultHoverText = InLabel;
			Operation->CurrentHoverText = InLabel;
			Operation->Construct();
			return Operation;
		}
	};
}

void SDreamUILayerStyleDialog::Construct(const FArguments& InArgs, const TSharedRef<FDreamUILayerStyleSession>& InSession)
{
	Session = InSession;

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs ViewArgs;
	ViewArgs.bAllowSearch = false;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	ViewArgs.bShowOptions = false;
	// The hook, not the finished-changing event: it hears interactive changes too, and changes an editor notifies by hand.
	ViewArgs.NotifyHook = this;
	FStructureDetailsViewArgs StructArgs;
	Details = PropertyEditor.CreateStructureDetailView(ViewArgs, StructArgs, nullptr);
	if (IDetailsView* View = Details->GetDetailsView())
	{
		View->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateLambda(
			[WeakThis = TWeakPtr<SDreamUILayerStyleDialog>(SharedThis(this))]() -> TSharedRef<IDetailCustomization>
			{
				return MakeShared<DreamUILayerStyleDialogLocal::FPageLayout>([WeakThis](IDetailLayoutBuilder& InBuilder)
				{
					if (const TSharedPtr<SDreamUILayerStyleDialog> Pinned = WeakThis.Pin())
					{
						Pinned->CustomizePage(InBuilder);
					}
				});
			}));
	}

	const TArray<FString> Names = Session->GetTargetNames();
	const FText Targets = Names.Num() > 1
		? FText::Format(LOCTEXT("SeveralTargets", "{0} and {1} more"), FText::FromString(Names[0]), FText::AsNumber(Names.Num() - 1))
		: FText::FromString(Names.IsEmpty() ? FString() : Names[0]);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(210.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 2.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("Styles", "Styles: {0}"), Targets))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(List, SListView<TSharedPtr<FDreamUIStyleDialogRow>>)
						.ListItemsSource(&Rows)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SDreamUILayerStyleDialog::MakeRow)
						.OnSelectionChanged(this, &SDreamUILayerStyleDialog::OnSelectionChanged)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				Details->GetWidget().ToSharedRef()
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(120.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("OK", "OK"))
						.OnClicked_Lambda([this]() { Ok(); return FReply::Handled(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("Cancel", "Cancel"))
						.OnClicked_Lambda([this]() { Cancel(); return FReply::Handled(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("NewStyle", "New Style..."))
						.ToolTipText(LOCTEXT("NewStyleTooltip", "Keep the style as a preset: an asset widgets can follow, or text in the project settings."))
						.OnClicked_Lambda([this]()
						{
							NewStyle([this](const FDreamUIStylePresetSaveArgs& InArgs)
							{
								return DreamUIStylePresets::PromptNewStyle(InArgs, Window.Pin());
							});
							return FReply::Handled();
						})
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return Session->IsPreviewEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState InState) { Session->SetPreviewEnabled(InState == ECheckBoxState::Checked); })
						.ToolTipText(LOCTEXT("PreviewTooltip", "Show the style on the widgets while it is edited. Nothing is written until OK."))
						[
							SNew(STextBlock).Text(LOCTEXT("Preview", "Preview"))
						]
					]
				]
			]
		]
	];

	RefreshRows();
	SelectRow({ EDreamUIStyleDialogPage::Blending, INDEX_NONE });
}

SDreamUILayerStyleDialog::~SDreamUILayerStyleDialog()
{
	// A dialog that goes without OK leaves the widgets as they were.
	if (Session.IsValid() && !Session->IsFinished())
	{
		Session->Cancel();
	}
}

void SDreamUILayerStyleDialog::RefreshRows()
{
	Rows.Reset();
	for (const FDreamUIStyleDialogRow& Row : Session->GetRows())
	{
		Rows.Add(MakeShared<FDreamUIStyleDialogRow>(Row));
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SDreamUILayerStyleDialog::SelectRow(const FDreamUIStyleDialogRow& InRow)
{
	Selected = InRow;
	ShowSelected();
	if (List.IsValid())
	{
		TGuardValue<bool> Selecting(bSelecting, true);
		for (const TSharedPtr<FDreamUIStyleDialogRow>& Row : Rows)
		{
			if (*Row == InRow)
			{
				List->SetSelection(Row, ESelectInfo::Direct);
				List->RequestScrollIntoView(Row);
				break;
			}
		}
	}
}

void SDreamUILayerStyleDialog::ShowSelected()
{
	using namespace DreamUILayerStyleDialogLocal;
	const UScriptStruct* Struct = nullptr;
	void* Memory = Session->GetPageMemory(Selected, Struct);
	ShownStruct = Memory != nullptr ? Struct : nullptr;
	ShownFields.Reset();
	if (Memory != nullptr)
	{
		ShownFields = Selected.Page == EDreamUIStyleDialogPage::Blending
			? TArray<FName>(BlendingFields, UE_ARRAY_COUNT(BlendingFields))
			: FDreamUILayerStyleSession::GetPageFields(Selected.Page);
	}
	// A view over the working copy itself, not a copy of it: an edit is the session's at once. Set again for every page, which
	// lays the view out anew -- and whenever the working copy's arrays may have moved their elements.
	Details->SetStructureData(MakeShared<FStructOnScope>(FDreamUIStyleDialogModel::StaticStruct(), reinterpret_cast<uint8*>(&Session->GetModel())));
}

void SDreamUILayerStyleDialog::CustomizePage(IDetailLayoutBuilder& InBuilder)
{
	const TSharedRef<IPropertyHandle> Style = InBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogModel, Style), FDreamUIStyleDialogModel::StaticStruct());
	const TSharedRef<IPropertyHandle> Blending = InBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogModel, Blending), FDreamUIStyleDialogModel::StaticStruct());
	InBuilder.HideProperty(Style);
	InBuilder.HideProperty(Blending);
	if (ShownFields.IsEmpty())
	{
		return;
	}
	// Each field is added by its own handle, found from the model down: its parents are the style's, as in a widget's details.
	const auto AddField = [](IDetailCategoryBuilder& InCategory, const TSharedPtr<IPropertyHandle>& InParent, FName InField) -> IDetailPropertyRow*
	{
		const TSharedPtr<IPropertyHandle> Field = InParent.IsValid() ? InParent->GetChildHandle(InField, /*bRecurse*/false) : nullptr;
		return Field.IsValid() && Field->IsValidHandle() ? &InCategory.AddProperty(Field) : nullptr;
	};
	if (Selected.Page == EDreamUIStyleDialogPage::Blending)
	{
		IDetailCategoryBuilder& General = InBuilder.EditCategory(TEXT("General Blending"), LOCTEXT("GeneralBlending", "General Blending"));
		AddField(General, Blending, GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, BlendMode));
		AddField(General, Blending, GET_MEMBER_NAME_CHECKED(FDreamUIStyleDialogBlending, Opacity));
		AddField(General, Style, GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, FillOpacity));
		IDetailCategoryBuilder& Effects = InBuilder.EditCategory(TEXT("Layer Effects"), LOCTEXT("LayerEffects", "Layer Effects"));
		AddField(Effects, Style, GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, bEnabled));
		AddField(Effects, Style, GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, Scale));
		IDetailCategoryBuilder& Light = InBuilder.EditCategory(TEXT("Global Light"), LOCTEXT("GlobalLight", "Global Light"));
		AddField(Light, Style, GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, GlobalLightAngle));
		AddField(Light, Style, GET_MEMBER_NAME_CHECKED(FDreamLayerEffects, GlobalLightAltitude));
		return;
	}
	TSharedPtr<IPropertyHandle> Page = Style->GetChildHandle(FDreamUILayerStyleSession::GetPageMember(Selected.Page), /*bRecurse*/false);
	if (Page.IsValid() && FDreamUILayerStyleSession::IsMultiInstance(Selected.Page))
	{
		Page = Selected.Index != INDEX_NONE ? Page->GetChildHandle(static_cast<uint32>(Selected.Index)) : nullptr;
	}
	const FText Label = Session->GetRowLabel(Selected);
	IDetailCategoryBuilder& Category = InBuilder.EditCategory(*Label.ToString(), Label);
	// An element of a list has no default in the model, whose lists are empty: its fields reset to a new element's.
	TSharedPtr<FStructOnScope> ElementDefaults;
	if (FDreamUILayerStyleSession::IsMultiInstance(Selected.Page) && ShownStruct != nullptr)
	{
		ElementDefaults = MakeShared<FStructOnScope>(ShownStruct);
	}
	for (const FName Field : ShownFields)
	{
		IDetailPropertyRow* Row = AddField(Category, Page, Field);
		if (Row != nullptr && ElementDefaults.IsValid())
		{
			Row->OverrideResetToDefault(DreamUILayerStyleDialogLocal::ResetToElementDefault(ElementDefaults.ToSharedRef()));
		}
	}
}

bool SDreamUILayerStyleDialog::IsFieldShown(FName InField) const
{
	return ShownFields.Contains(InField);
}

void SDreamUILayerStyleDialog::NotifyPostChange(const FPropertyChangedEvent& InEvent, FProperty* InPropertyThatChanged)
{
	// The field edited, however deep: on Blending Options, whether it was the layer's blending or the style's.
	if ((InEvent.ChangeType & EPropertyChangeType::Interactive) != 0)
	{
		Session->PreviewChanged(Selected.Page, InEvent.GetPropertyName());
	}
	else
	{
		Session->NotifyChanged(Selected.Page, InEvent.GetPropertyName());
	}
}

void SDreamUILayerStyleDialog::OnSelectionChanged(TSharedPtr<FDreamUIStyleDialogRow> InRow, ESelectInfo::Type InInfo)
{
	if (!bSelecting && InRow.IsValid() && !(*InRow == Selected))
	{
		Selected = *InRow;
		ShowSelected();
	}
}

TSharedRef<ITableRow> SDreamUILayerStyleDialog::MakeRow(TSharedPtr<FDreamUIStyleDialogRow> InRow, const TSharedRef<STableViewBase>& InOwner)
{
	using namespace DreamUILayerStyleDialogLocal;
	const FDreamUIStyleDialogRow Row = *InRow;
	const bool bMulti = FDreamUILayerStyleSession::IsMultiInstance(Row.Page);
	const bool bSubPage = Row.Page == EDreamUIStyleDialogPage::BevelContour || Row.Page == EDreamUIStyleDialogPage::BevelTexture;
	const FText Label = Session->GetRowLabel(Row);

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	if (Session->HasCheckbox(Row))
	{
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bSubPage ? 16.0f : 0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, Row]() { return Session->IsChecked(Row) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Row](ECheckBoxState InState) { SetChecked(Row, InState == ECheckBoxState::Checked); })
		];
	}
	Content->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(Session->HasCheckbox(Row) ? 0.0f : 2.0f, 3.0f)
	[
		SNew(STextBlock).Text(Label)
	];
	if (bMulti)
	{
		Content->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText(FText::Format(LOCTEXT("AddInstance", "Add another {0}"), Label))
			.OnClicked_Lambda([this, Row]() { AddInstance(Row); return FReply::Handled(); })
			[
				SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")).ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
		if (Row.Index != INDEX_NONE)
		{
			Content->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.0f))
				.ToolTipText(FText::Format(LOCTEXT("RemoveInstance", "Remove this {0}"), Label))
				.OnClicked_Lambda([this, Row]() { RemoveInstance(Row); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}
	}

	return SNew(STableRow<TSharedPtr<FDreamUIStyleDialogRow>>, InOwner)
		.ToolTipText(bMulti ? LOCTEXT("MultiTooltip", "Drag onto another of its kind to change the order they draw in: a later one draws over an earlier one.") : FText::GetEmpty())
		.OnDragDetected_Lambda([Row, Label, bMulti](const FGeometry&, const FPointerEvent& InEvent)
		{
			return bMulti && Row.Index != INDEX_NONE && InEvent.IsMouseButtonDown(EKeys::LeftMouseButton)
				? FReply::Handled().BeginDragDrop(FRowDragDropOp::New(Row, Label))
				: FReply::Unhandled();
		})
		.OnCanAcceptDrop_Lambda([Row](const FDragDropEvent& InEvent, EItemDropZone InZone, TSharedPtr<FDreamUIStyleDialogRow>) -> TOptional<EItemDropZone>
		{
			const TSharedPtr<FRowDragDropOp> Operation = InEvent.GetOperationAs<FRowDragDropOp>();
			return Operation.IsValid() && Operation->Row.Page == Row.Page && Row.Index != INDEX_NONE
				? TOptional<EItemDropZone>(EItemDropZone::OntoItem) : TOptional<EItemDropZone>();
		})
		.OnAcceptDrop_Lambda([this, Row](const FDragDropEvent& InEvent, EItemDropZone, TSharedPtr<FDreamUIStyleDialogRow>)
		{
			const TSharedPtr<FRowDragDropOp> Operation = InEvent.GetOperationAs<FRowDragDropOp>();
			if (!Operation.IsValid() || Operation->Row.Page != Row.Page)
			{
				return FReply::Unhandled();
			}
			MoveInstance(Operation->Row, Row);
			return FReply::Handled();
		})
		[
			Content
		];
}

void SDreamUILayerStyleDialog::SetChecked(const FDreamUIStyleDialogRow& InRow, bool bInChecked)
{
	const TArray<FDreamUIStyleDialogRow> RowsBefore = Session->GetRows();
	Session->SetChecked(InRow, bInChecked);
	if (Session->GetRows() != RowsBefore)
	{
		// The first of a kind was made: the row that stood for the kind is now that instance's.
		RefreshRows();
		SelectRow({ InRow.Page, InRow.Index == INDEX_NONE ? 0 : InRow.Index });
		return;
	}
	if (!(InRow == Selected))
	{
		SelectRow(InRow);
	}
}

void SDreamUILayerStyleDialog::AddInstance(const FDreamUIStyleDialogRow& InRow)
{
	const FDreamUIStyleDialogRow Added = Session->AddInstance(InRow);
	RefreshRows();
	SelectRow(Added);
}

void SDreamUILayerStyleDialog::RemoveInstance(const FDreamUIStyleDialogRow& InRow)
{
	if (Session->RemoveInstance(InRow))
	{
		RefreshRows();
		SelectRow({ InRow.Page, FMath::Max(InRow.Index - 1, 0) });
		const UScriptStruct* Struct = nullptr;
		if (Session->GetPageMemory(Selected, Struct) == nullptr)
		{
			SelectRow({ InRow.Page, INDEX_NONE });
		}
	}
}

void SDreamUILayerStyleDialog::MoveInstance(const FDreamUIStyleDialogRow& InFrom, const FDreamUIStyleDialogRow& InTo)
{
	if (InFrom.Page == InTo.Page && Session->MoveInstance(InFrom.Page, InFrom.Index, InTo.Index))
	{
		RefreshRows();
		SelectRow(InTo);
	}
}

int32 SDreamUILayerStyleDialog::Ok()
{
	// The details view looks at the working copy: let it go before the copy is done with.
	Details->SetStructureData(nullptr);
	const int32 Written = Session->Commit();
	CloseWindow();
	return Written;
}

void SDreamUILayerStyleDialog::Cancel()
{
	Details->SetStructureData(nullptr);
	Session->Cancel();
	CloseWindow();
}

bool SDreamUILayerStyleDialog::NewStyle(TFunctionRef<bool(const FDreamUIStylePresetSaveArgs&)> InPrompt)
{
	return InPrompt(Session->MakeSaveArgs());
}

void SDreamUILayerStyleDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Pinned = Window.Pin())
	{
		Window.Reset();
		Pinned->RequestDestroyWindow();
	}
}

// ---------------------------------------------------------------------------------------------------------------------------

TSharedPtr<SWindow> DreamUILayerStyleDialog::Open(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets)
{
	using namespace DreamUILayerStyleDialogLocal;
	if (const TSharedPtr<SDreamUILayerStyleDialog> Existing = Find(InDesigner))
	{
		Existing->Cancel();
	}
	const TSharedPtr<FDreamUILayerStyleSession> Session = FDreamUILayerStyleSession::Begin(InDesigner, InPreviewWidgets);
	if (!Session.IsValid() || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}
	const TSharedRef<SDreamUILayerStyleDialog> Dialog = SNew(SDreamUILayerStyleDialog, Session.ToSharedRef());
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Layer Style"))
		.ClientSize(FVector2D(900.0f, 560.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		[
			Dialog
		];
	Dialog->SetWindow(Window);
	// Closing the window by its own button is a Cancel; the dialog's destructor makes it when nothing else did.
	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakDialog = TWeakPtr<SDreamUILayerStyleDialog>(Dialog)](const TSharedRef<SWindow>&)
	{
		if (const TSharedPtr<SDreamUILayerStyleDialog> Pinned = WeakDialog.Pin())
		{
			if (!Pinned->GetSession()->IsFinished())
			{
				Pinned->GetSession()->Cancel();
			}
		}
	}));
	OpenDialogs().Add(&InDesigner, Dialog);
	const TSharedPtr<SWindow> Parent = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (Parent.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
	return Window;
}

TSharedPtr<SDreamUILayerStyleDialog> DreamUILayerStyleDialog::Find(const FDreamWidgetBlueprintEditor& InDesigner)
{
	using namespace DreamUILayerStyleDialogLocal;
	const TWeakPtr<SDreamUILayerStyleDialog>* Found = OpenDialogs().Find(&InDesigner);
	const TSharedPtr<SDreamUILayerStyleDialog> Dialog = Found != nullptr ? Found->Pin() : nullptr;
	return Dialog.IsValid() && !Dialog->GetSession()->IsFinished() ? Dialog : nullptr;
}

TSharedPtr<SWindow> DreamUILayerStyleDialog::OpenFrom(UDreamWidget* InPreviewWidget)
{
	FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(InPreviewWidget);
	if (Designer == nullptr)
	{
		return nullptr;
	}
	TArray<UDreamWidget*> Targets;
	bool bInSelection = false;
	for (const TWeakObjectPtr<UDreamWidget>& Selected : Designer->GetSelectedWidgets())
	{
		bInSelection |= Selected.Get() == InPreviewWidget;
		if (Selected.IsValid())
		{
			Targets.Add(Selected.Get());
		}
	}
	if (!bInSelection)
	{
		Targets = { InPreviewWidget };
	}
	return Open(*Designer, Targets);
}

#undef LOCTEXT_NAMESPACE
