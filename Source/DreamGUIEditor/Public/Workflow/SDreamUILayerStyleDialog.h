// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Workflow/DreamUILayerStyleDialog.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/NotifyHook.h"
#include "Widgets/Views/SListView.h"

class IDetailLayoutBuilder;
class IStructureDetailsView;
class ITableRow;
class STableViewBase;
class SWindow;
struct FPropertyChangedEvent;

/**
 * The Layer Style dialog's content: the effects on the left, the selected one's parameters, OK, Cancel, New Style and
 * Preview on the right. It edits a FDreamUILayerStyleSession; OK commits it, Cancel and closing the window cancel it.
 *
 * The parameters are a structure details view of the working style, laid out to show the selected page's fields alone. Each
 * field is the style's own (Style.DropShadows[1].Angle), so whatever a project or DreamGUI registers for a property type or
 * a field's metadata -- an angle dial, a curve, a blend mode picker -- is used here as in a widget's details, and an editor
 * that looks up from its field to the style (a light angle turning the global light) finds the style it is in.
 */
class DREAMGUIEDITOR_API SDreamUILayerStyleDialog : public SCompoundWidget, public FNotifyHook
{
public:
	SLATE_BEGIN_ARGS(SDreamUILayerStyleDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FDreamUILayerStyleSession>& InSession);
	virtual ~SDreamUILayerStyleDialog() override;

	TSharedRef<FDreamUILayerStyleSession> GetSession() const { return Session.ToSharedRef(); }
	/** The window the dialog is in, closed by OK and Cancel. */
	void SetWindow(const TSharedPtr<SWindow>& InWindow) { Window = InWindow; }

	/** The rows the list shows now. */
	const TArray<TSharedPtr<FDreamUIStyleDialogRow>>& GetListRows() const { return Rows; }
	/** Show InRow's parameters. */
	void SelectRow(const FDreamUIStyleDialogRow& InRow);
	FDreamUIStyleDialogRow GetSelectedRow() const { return Selected; }
	/** The struct the parameter area shows, and whether a field of it is shown on the selected page. */
	const UScriptStruct* GetShownStruct() const { return ShownStruct; }
	bool IsFieldShown(FName InField) const;

	/** What the + of a row does: another instance after it, selected. What the - does: that instance taken out. */
	void AddInstance(const FDreamUIStyleDialogRow& InRow);
	void RemoveInstance(const FDreamUIStyleDialogRow& InRow);
	/** What a row dragged onto another of its kind does. */
	void MoveInstance(const FDreamUIStyleDialogRow& InFrom, const FDreamUIStyleDialogRow& InTo);
	/** A tick in a row's checkbox. */
	void SetChecked(const FDreamUIStyleDialogRow& InRow, bool bInChecked);

	/** OK: commit, close. How many widgets took the style. */
	int32 Ok();
	/** Cancel: put the preview back, close. */
	void Cancel();
	/** New Style: the working style as a preset, through InPrompt (DreamUIStylePresets::PromptNewStyle by default). */
	bool NewStyle(TFunctionRef<bool(const FDreamUIStylePresetSaveArgs&)> InPrompt);

	/**
	 * A change made in the parameter area, as its details view's notify hook hears every one: an interactive change (a drag
	 * in progress, a value an editor tries -- a blend mode hovered) is previewed, any other is an edit.
	 */
	virtual void NotifyPostChange(const FPropertyChangedEvent& InEvent, FProperty* InPropertyThatChanged) override;

private:
	void RefreshRows();
	void ShowSelected();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FDreamUIStyleDialogRow> InRow, const TSharedRef<STableViewBase>& InOwner);
	void OnSelectionChanged(TSharedPtr<FDreamUIStyleDialogRow> InRow, ESelectInfo::Type InInfo);
	/** The details view's layout: the selected page's fields of the model, in a category named after the page. */
	void CustomizePage(IDetailLayoutBuilder& InBuilder);
	void CloseWindow();

	TSharedPtr<FDreamUILayerStyleSession> Session;
	TWeakPtr<SWindow> Window;
	TArray<TSharedPtr<FDreamUIStyleDialogRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FDreamUIStyleDialogRow>>> List;
	TSharedPtr<IStructureDetailsView> Details;
	FDreamUIStyleDialogRow Selected;
	const UScriptStruct* ShownStruct = nullptr;
	TArray<FName> ShownFields;
	bool bSelecting = false;
};
