// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/Designer/DreamDesignerPanels.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/DreamSlatePanelDriver.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "Designer/SDreamWidgetPalette.h"

#include "Widgets/SWidget.h"

namespace DreamTests
{
	namespace DreamDesignerPanelsLocal
	{
		/** The first painted descendant of InRoot whose type contains InTypeNamePart, or null. */
		TSharedPtr<SWidget> FindPaintedOfType(const TSharedRef<SWidget>& InRoot, const TCHAR* InTypeNamePart)
		{
			const TSharedPtr<SWidget> Found = DreamSlatePanel::FindDescendant(InRoot, [InTypeNamePart](const TSharedRef<SWidget>& InWidget)
			{
				return DreamSlatePanel::IsOfType(*InWidget, InTypeNamePart) && DreamDesignerPanels::IsPainted(InWidget);
			});
			return Found;
		}
	}

	bool DreamDesignerPanels::IsPainted(const TSharedPtr<SWidget>& InWidget)
	{
		return InWidget.IsValid() && DreamSlatePanel::CentreOf(InWidget.ToSharedRef()).IsSet();
	}

	TSharedPtr<SWidget> DreamDesignerPanels::HierarchyRowFor(const FDreamDesignerDriver& InDriver, const UDreamWidget* InWidget)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		const TSharedPtr<SDreamWidgetEditorHierarchyView> Hierarchy = Toolkit != nullptr ? Toolkit->GetHierarchyWidget() : nullptr;
		if (!Hierarchy.IsValid() || !::IsValid(InWidget))
		{
			return nullptr;
		}
		const TSharedPtr<SWidget> Name = DreamSlatePanel::FindTextBlock(Hierarchy.ToSharedRef(), InWidget->GetDisplayName());
		const TSharedPtr<SWidget> Row = Name.IsValid()
			? DreamSlatePanel::FindAncestorOfType(Name.ToSharedRef(), TEXT("SDreamWidgetEditorHierarchyViewItem"))
			: nullptr;
		return IsPainted(Row) ? Row : nullptr;
	}

	TSharedPtr<SWidget> DreamDesignerPanels::DetailsSearchBox(const FDreamDesignerDriver& InDriver)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		const TSharedPtr<SDreamWidgetDesignerDetails> Details = Toolkit != nullptr ? Toolkit->GetDesignerDetailsWidget() : nullptr;
		return Details.IsValid() ? DreamDesignerPanelsLocal::FindPaintedOfType(Details.ToSharedRef(), TEXT("SSearchBox")) : nullptr;
	}

	TSharedPtr<SWidget> DreamDesignerPanels::DetailsValueWidget(const FDreamDesignerDriver& InDriver, const FString& InLabel, const TCHAR* InTypeNamePart)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		const TSharedPtr<SDreamWidgetDesignerDetails> Details = Toolkit != nullptr ? Toolkit->GetDesignerDetailsWidget() : nullptr;
		if (!Details.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<SWidget> Label = DreamSlatePanel::FindTextBlock(Details.ToSharedRef(), InLabel);
		const TSharedPtr<SWidget> Row = Label.IsValid() ? DreamSlatePanel::FindAncestorOfType(Label.ToSharedRef(), TEXT("SDetailSingleItemRow")) : nullptr;
		return Row.IsValid() ? DreamDesignerPanelsLocal::FindPaintedOfType(Row.ToSharedRef(), InTypeNamePart) : nullptr;
	}

	TSharedPtr<SWidget> DreamDesignerPanels::PaletteSearchBox(const FDreamDesignerDriver& InDriver)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		const TSharedPtr<SDreamWidgetPalette> Palette = Toolkit != nullptr ? Toolkit->GetPaletteWidget() : nullptr;
		return Palette.IsValid() ? DreamDesignerPanelsLocal::FindPaintedOfType(Palette.ToSharedRef(), TEXT("SSearchBox")) : nullptr;
	}

	TSharedPtr<SWidget> DreamDesignerPanels::PaletteRowFor(const FDreamDesignerDriver& InDriver, const FString& InDisplayName)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InDriver.Toolkit();
		const TSharedPtr<SDreamWidgetPalette> Palette = Toolkit != nullptr ? Toolkit->GetPaletteWidget() : nullptr;
		if (!Palette.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<SWidget> Name = DreamSlatePanel::FindTextBlock(Palette.ToSharedRef(), InDisplayName);
		// The row itself, which is what detects the drag (SDreamWidgetPalette::OnGenerateRow binds OnDragDetected on it).
		const TSharedPtr<SWidget> Row = Name.IsValid() ? DreamSlatePanel::FindAncestorOfType(Name.ToSharedRef(), TEXT("STableRow")) : nullptr;
		return IsPainted(Row) ? Row : nullptr;
	}
}
