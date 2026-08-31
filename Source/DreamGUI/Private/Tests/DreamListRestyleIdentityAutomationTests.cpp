// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamListView.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Demo/DreamUIShowcase.h"
#include "Interaction/UIButton.h"
#include "UObject/Package.h"

/**
 * Re-styling a list must not create or destroy a single widget.
 *
 * Not a performance nicety -- a correctness requirement about the EDITOR, and the reason the
 * designer was unusable while a list was selected. Creating or destroying any widget marks the UI
 * outliner dirty; the designer answers an outliner change by force-refreshing the engine's details
 * view, which rebuilds the whole property tree for the selected object (measured at 18.7 ms for
 * UDreamListView) and drags a Slate slow-path repaint (~20 ms) behind it. ApplyStyle runs on every
 * PostEditChangeProperty, so a list that tore its rows down and duplicated them again inside
 * ApplyStyle charged ~40 ms to every single click in the details panel.
 *
 * This control is the only one in the library whose ApplyStyle touches the widget tree at all,
 * which is exactly why it was the only one that felt broken to edit. The claim below is therefore
 * about widget IDENTITY, not about timing: same pointers before and after, and the new style still
 * arrived.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRestyleKeepsItsWidgets,
	"DreamGUI.Controls.List.ReStylingDoesNotCreateOrDestroyAnyWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListRestyleKeepsItsWidgets::RunTest(const FString& Parameters)
{
	// The gallery's shape, read off the designer's inspector: 454.5 x 100, three item objects
	// arriving through a binding.
	UDreamListView* List = NewObject<UDreamListView>(GetTransientPackage());
	List->StyleSource = EDreamUIStyleSource::Inline;
	List->SetWidth(454.5f);
	List->SetHeight(100.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		List->ItemObjects.Add(NewObject<UDreamUIShowcaseTrack>(GetTransientPackage()));
	}
	List->Initialize();
	TDreamTestControl<UDreamListView> Owned(List);

	if (!TestEqual(TEXT("the list built one row per item"), List->GetRealizedRowCount(), 3))
	{
		return false;
	}
	TArray<UDreamWidget*> Before;
	for (const TObjectPtr<UDreamWidget>& Row : List->RowNodes)
	{
		Before.Add(Row.Get());
	}

	// One details-panel edit. Any property will do -- PostEditChangeProperty does not tell the
	// control WHICH one moved, so every edit takes this path.
	List->Style.RowSelected = FColor(9, 8, 7, 255);
	List->Style.RowNormal = FColor(1, 2, 3, 255);
	// Through the BASE pointer, which is the road the editor takes: UDreamUIControl declares
	// ApplyStyle public and virtual, PostEditChangeProperty calls it there, and the list's override
	// is private -- so this is the same dispatch a details-panel edit performs, not a shortcut.
	static_cast<UDreamUIControl*>(List)->ApplyStyle();

	if (!TestEqual(TEXT("the pool did not change size"), List->GetRealizedRowCount(), 3))
	{
		return false;
	}
	for (int32 Index = 0; Index < Before.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("row %d is the same widget it was"), Index),
			(UObject*)List->RowNodes[Index].Get() == (UObject*)Before[Index]);
	}

	// And the edit still arrived, which is the half a "keep everything" optimisation is most likely
	// to break: the rows are copies of a template, so a style change reaches them only because
	// BindRow pushes the whole look rather than trusting the copy.
	if (UUIButton* RowButton = List->RowNodes[0]->GetComponent<UUIButton>())
	{
		TestEqual(TEXT("the new resting colour reached the row's selectable"),
			RowButton->GetNormalColor(), FColor(1, 2, 3, 255));
	}
	if (UDreamVisual* RowVisual = List->RowNodes[0]->GetVisual())
	{
		TestEqual(TEXT("and the row is wearing it"), RowVisual->GetColor(), FColor(1, 2, 3, 255));
	}

	// A source that really changed still rebuilds, because a different number of rows cannot be
	// reached by re-binding the ones there are. Through the OBJECT source, because that is the one
	// this list is driven by: GetItemCount lets objects decide the count whenever there are any, so
	// writing the text source here would leave the row count exactly where it was.
	TArray<UObject*> Shorter;
	Shorter.Add(NewObject<UDreamUIShowcaseTrack>(GetTransientPackage()));
	List->SetItemObjects(Shorter);
	TestEqual(TEXT("a shorter source shrinks the pool"), List->GetRealizedRowCount(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
