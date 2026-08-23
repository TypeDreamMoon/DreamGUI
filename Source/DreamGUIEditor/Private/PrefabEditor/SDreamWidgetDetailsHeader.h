// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "EditorClassUtils.h"
#include "ScopedTransaction.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorDetailTab"

/**
 * The name and class strip above the details view.
 *
 * The stock name area only fills itself in for actors and actor components -- a UDreamWidget is
 * neither -- and the editable box it would offer renames the UObject rather than the DisplayName
 * that every other panel shows, so only its icon and lock button are reused here. The class link is
 * rebuilt from Tick because FEditorClassUtils::GetSourceLink builds against a fixed class, while the
 * panel moves between widgets and behaviours as the selection changes.
 */
class SDreamWidgetDetailsHeader : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(UObject*, FOnGetEditedObject);
	DECLARE_DELEGATE_RetVal(bool, FOnCanEdit);

	SLATE_BEGIN_ARGS(SDreamWidgetDetailsHeader)
	{
	}
		SLATE_EVENT(FOnGetEditedObject, GetEditedObject)
		SLATE_EVENT(FOnCanEdit, CanEdit)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, NameAreaWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		GetEditedObject = InArgs._GetEditedObject;
		CanEdit = InArgs._CanEdit;

		ChildSlot
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				InArgs._NameAreaWidget.IsValid() ? InArgs._NameAreaWidget.ToSharedRef() : SNullWidget::NullWidget
			]
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(4, 0))
			[
				// The name is pushed from Tick, deliberately NOT bound. An SEditableTextBox
				// re-runs OnVerifyTextChanged every time its text changes -- including a change the
				// binding pushes -- and a passing verification calls SetError(""), which destroys
				// the error popup's window then and there. Bindings are updated during Slate's
				// child walk, so that destroy removes a slot from the window overlay while the walk
				// is iterating it, and the walk reads the count it took before the slot went away.
				SAssignNew(NameBox, SEditableTextBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.HintText(LOCTEXT("WidgetNameHint", "Name"))
				.IsEnabled(this, &SDreamWidgetDetailsHeader::CanRename)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.OnVerifyTextChanged(this, &SDreamWidgetDetailsHeader::VerifyDisplayName)
				.OnTextCommitted(this, &SDreamWidgetDetailsHeader::OnDisplayNameCommitted)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(SourceLinkBox, SBox)
			]
		];

		RebuildSourceLink();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		UObject* EditedObject = GetCurrentObject();
		if (EditedObject != CachedObject.Get(true))
		{
			CachedObject = EditedObject;
			RebuildSourceLink();
		}
		// Never while the user is in the box: this would overwrite what they are typing.
		if (NameBox.IsValid() && !NameBox->HasAnyUserFocusOrFocusedDescendants())
		{
			const FText Current = GetEditedObjectText();
			if (!NameBox->GetText().EqualTo(Current))
			{
				NameBox->SetText(Current);
			}
		}
	}

private:
	UObject* GetCurrentObject() const
	{
		return GetEditedObject.IsBound() ? GetEditedObject.Execute() : nullptr;
	}

	UDreamWidget* GetCurrentWidget() const
	{
		return Cast<UDreamWidget>(GetCurrentObject());
	}

	FText GetEditedObjectText() const
	{
		UObject* EditedObject = GetCurrentObject();
		if (!IsValid(EditedObject))
		{
			return FText::GetEmpty();
		}
		if (auto Widget = Cast<UDreamWidget>(EditedObject))
		{
			return FText::FromString(Widget->GetDisplayName());
		}
		return FText::FromString(EditedObject->GetName());
	}

	bool CanRename() const
	{
		return IsValid(GetCurrentWidget()) && (!CanEdit.IsBound() || CanEdit.Execute());
	}

	bool VerifyDisplayName(const FText& InText, FText& OutErrorMessage) const
	{
		const FString ProposedName = InText.ToString().TrimStartAndEnd();
		if (ProposedName.IsEmpty())
		{
			OutErrorMessage = LOCTEXT("EmptyWidgetName", "Widget name cannot be empty.");
			return false;
		}
		return FName::IsValidXName(ProposedName, FString(INVALID_OBJECTNAME_CHARACTERS) + TEXT("/"), &OutErrorMessage);
	}

	void OnDisplayNameCommitted(const FText& InText, ETextCommit::Type CommitInfo)
	{
		UDreamWidget* Widget = GetCurrentWidget();
		if (!CanRename() || !IsValid(Widget))
		{
			return;
		}
		const FString ProposedName = InText.ToString().TrimStartAndEnd();
		if (ProposedName.IsEmpty() || ProposedName == Widget->GetDisplayName())
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("ChangeWidgetName_Transaction", "Change Name"));
		Widget->SetFlags(RF_Transactional);
		Widget->Modify();
		const FString UniqueName = FDreamUIEditorTools::MakeUniqueWidgetDisplayName(Widget, ProposedName, Widget);
		FDreamUIUtils::ChangePropertyWithNotify(Widget, UDreamWidget::GetPropertyName_DisplayName(), [Widget, UniqueName]()
		{
			Widget->SetDisplayName(UniqueName);
		});
	}

	void RebuildSourceLink()
	{
		UObject* EditedObject = GetCurrentObject();
		if (!IsValid(EditedObject))
		{
			SourceLinkBox->SetContent(SNullWidget::NullWidget);
			return;
		}

		FEditorClassUtils::FSourceLinkParams Params;
		Params.Object = EditedObject;
		Params.bUseDefaultFormat = true;
		Params.bEmptyIfNoLink = true;
		SourceLinkBox->SetContent(FEditorClassUtils::GetSourceLink(EditedObject->GetClass(), Params));
	}

	FOnGetEditedObject GetEditedObject;
	FOnCanEdit CanEdit;
	TWeakObjectPtr<UObject> CachedObject;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SBox> SourceLinkBox;
};

#undef LOCTEXT_NAMESPACE
