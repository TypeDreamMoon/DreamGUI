// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/DreamDesignerPanels.h"
#include "Driver/Designer/DreamSlatePanelDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/SDreamWidgetDesignerViewport.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

/*
 * The designer's Slate panels, driven by a hand's input: the hierarchy tree, the details panel, the viewport's toolbar
 * and the palette.
 *
 * The four probes beside this file (DreamDesignerSlatePanelProbeAutomationTests.cpp) answered whether these panels can be
 * driven at all, and how -- Slate's own hit test reaches them in an editor rendering off screen, and a spin box is entered
 * by keyboard because clicking one takes the desk's cursor. These are the tests that answer was for: every pointer event
 * goes through FSlateApplication's input functions (DreamSlatePanel), by the road ChooseRoute picks and reports, and the
 * panel's own widgets -- STableRow's drag detection and drop zones, the details view's property rows, the toolbar's
 * buttons and menus, the palette's rows -- take it from there. What each one does is held to UMG's Widget Blueprint
 * designer: its hierarchy (reorder above a row, reparent onto a row), its details (a typed value is one transaction),
 * its DPI preview and its palette drag.
 *
 * NonNullRHI: Slate has to have laid the designer's tabs out and painted them for a pointer to have anywhere to be, and
 * under -nullrhi it never does. Everything waits for what it needs to be painted, with a time limit, not for a count of
 * frames.
 */
namespace DreamDesignerPanelInputTestLocal
{
	using namespace DreamTests;

	/** How long a panel may take to show what a test is waiting for. */
	constexpr double PanelSeconds = 5.0;

	/** A plain widget dropped at a fraction of the viewport and remembered under InName; false, having said so, when it did not arrive. */
	bool DropPlain(FAutomationTestBase& InTest, const FDesignerLatentRef& InState, FName InName, double InFractionX, double InFractionY)
	{
		FDreamDesignerDriver& Driver = *InState->Driver;
		const FIntPoint Size = Driver.ViewportPixelSize();
		UDreamWidget* Made = DropOntoRootAndFindTemplate(Driver, /*Plain Widget row*/ nullptr,
			FIntPoint(FMath::RoundToInt32(Size.X * InFractionX), FMath::RoundToInt32(Size.Y * InFractionY)));
		if (!InTest.TestNotNull(*FString::Printf(TEXT("A plain widget, %s, was dropped under the root"), *InName.ToString()), Made))
		{
			InState->bAlive = false;
			return false;
		}
		InState->Made.Add(InName, Made);
		return true;
	}

	/** Give InWidget the keyboard as a keyboard user's Tab would and type InText into it; false, said on InTest, when either failed. */
	bool TypeInto(FAutomationTestBase& InTest, const TSharedPtr<SWidget>& InWidget, const FString& InText, const TCHAR* InWhat)
	{
		FString WhyNot;
		const bool bTyped = InWidget.IsValid()
			&& DreamSlatePanel::FocusAsKeyboardUser(InWidget.ToSharedRef(), WhyNot)
			&& DreamSlatePanel::TypeCharacters(InText, WhyNot);
		return InTest.TestTrue(*FString::Printf(TEXT("%s takes the keyboard and \"%s\" is typed into it (%s)"), InWhat, *InText, *WhyNot), bTyped);
	}

	/** Click InWidget by whichever road reaches it, writing the road into the state for the report. */
	bool ClickPanelWidget(FAutomationTestBase& InTest, const FDesignerLatentRef& InState, const TSharedPtr<SWidget>& InWidget, const TCHAR* InWhat)
	{
		if (!InTest.TestTrue(*FString::Printf(TEXT("%s is there to click"), InWhat), InWidget.IsValid()))
		{
			InState->bAlive = false;
			return false;
		}
		const TSharedRef<SWidget> Targets[] = { InWidget.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Targets, InState->Route);
		FString WhyNot;
		const bool bClicked = DreamSlatePanel::Click(InWidget.ToSharedRef(), Route, WhyNot);
		return InTest.TestTrue(*FString::Printf(TEXT("The click on %s completes (%s)"), InWhat, *WhyNot), bClicked);
	}

	/** The authored children of InParent, in order, by display name. */
	FString OrderUnder(const UDreamWidget* InParent)
	{
		TArray<FString> Names;
		for (const UDreamWidget* Child : LiveChildrenOf(InParent))
		{
			Names.Add(Child->GetDisplayName());
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Whether InWidget's tooltip says InNeedle anywhere: its own text, or a text block of the content it shows. */
	bool ToolTipSays(const TSharedRef<SWidget>& InWidget, const TCHAR* InNeedle)
	{
		const TSharedPtr<IToolTip> ToolTip = InWidget->GetToolTip();
		if (!ToolTip.IsValid())
		{
			return false;
		}
		const TSharedRef<SWidget> Shown = ToolTip->AsWidget();
		if (DreamSlatePanel::IsOfType(*Shown, TEXT("SToolTip"))
			&& StaticCastSharedRef<SToolTip>(Shown)->GetTextTooltip().ToString().Contains(InNeedle))
		{
			return true;
		}
		// The editor's tooltips are SToolTips around a documentation tooltip, whose text is a text block inside it.
		static const FName TextBlockType(TEXT("STextBlock"));
		return DreamSlatePanel::FindDescendant(Shown, [InNeedle](const TSharedRef<SWidget>& InCandidate)
		{
			return InCandidate->GetType() == TextBlockType
				&& StaticCastSharedRef<STextBlock>(InCandidate)->GetText().ToString().Contains(InNeedle);
		}).IsValid();
	}

	/**
	 * The DPI preview's toggle on the viewport's toolbar: the check box whose tooltip speaks of the project's DPI curve
	 * (SDreamWidgetDesignerViewportToolbar, PreviewDPITooltip). It has no label and shares its icon with the rulers
	 * toggle, so the tooltip is what tells it apart; the test's own assertions -- the DPI preview switched by the click --
	 * confirm it was the right one.
	 */
	TSharedPtr<SWidget> FindDPIToggle(const FDreamDesignerDriver& InDriver)
	{
		const TSharedPtr<SWidget> Shell = InDriver.ViewportShell();
		if (!Shell.IsValid())
		{
			return nullptr;
		}
		return DreamSlatePanel::FindDescendant(Shell.ToSharedRef(), [](const TSharedRef<SWidget>& InWidget)
		{
			return DreamSlatePanel::IsOfType(*InWidget, TEXT("SCheckBox"))
				&& DreamDesignerPanels::IsPainted(InWidget)
				&& ToolTipSays(InWidget, TEXT("DPI curve"));
		});
	}

	/**
	 * For the report when the DPI toggle is not found: how many check boxes the toolbar has and how many are painted, and
	 * every widget there whose own tooltip speaks of the DPI curve, or that holds the entries a narrow toolbar clips.
	 */
	FString DescribeDPICandidates(const FDreamDesignerDriver& InDriver)
	{
		const TSharedPtr<SWidget> Shell = InDriver.ViewportShell();
		if (!Shell.IsValid())
		{
			return TEXT("There is no viewport shell.");
		}
		TArray<TSharedRef<SWidget>> All;
		DreamSlatePanel::CollectDescendants(Shell.ToSharedRef(), All);
		TArray<FString> Found;
		int32 CheckBoxes = 0;
		int32 PaintedCheckBoxes = 0;
		for (const TSharedRef<SWidget>& Widget : All)
		{
			const bool bPainted = DreamDesignerPanels::IsPainted(Widget);
			if (DreamSlatePanel::IsOfType(*Widget, TEXT("SCheckBox")))
			{
				++CheckBoxes;
				PaintedCheckBoxes += bPainted ? 1 : 0;
			}
			const FString Type = Widget->GetTypeAsString();
			if (ToolTipSays(Widget, TEXT("DPI curve")) || Type.Contains(TEXT("Clipping")) || Type.Contains(TEXT("Wrap")))
			{
				Found.Add(FString::Printf(TEXT("%s (%s)"), *Type, bPainted ? TEXT("painted") : TEXT("not painted")));
			}
		}
		return FString::Printf(TEXT("The toolbar has %d check boxes, %d of them painted; with the DPI curve in their tooltip, or holding clipped entries: %s."),
			CheckBoxes, PaintedCheckBoxes, Found.Num() > 0 ? *FString::Join(Found, TEXT(", ")) : TEXT("none"));
	}

	/** The DPI toggle whether or not the toolbar has room to show it: painted, or clipped into the overflow. */
	TSharedPtr<SWidget> FindAnyDPIToggle(const FDreamDesignerDriver& InDriver)
	{
		const TSharedPtr<SWidget> Shell = InDriver.ViewportShell();
		return Shell.IsValid() ? DreamSlatePanel::FindDescendant(Shell.ToSharedRef(), [](const TSharedRef<SWidget>& InWidget)
		{
			return DreamSlatePanel::IsOfType(*InWidget, TEXT("SCheckBox")) && ToolTipSays(InWidget, TEXT("DPI curve"));
		}) : nullptr;
	}

	/**
	 * The button a toolbar too narrow for all of its entries shows at its end, whose menu holds the entries it clipped
	 * (SClippingHorizontalBox's wrap button, which SMultiBoxWidget::OnWrapButtonClicked fills): a combo button in a slot
	 * of the clipping box itself, or in the box with separators it is put in -- every entry of the toolbar is in a block
	 * widget of its own instead. Null when the toolbar holding InClipped clipped nothing.
	 */
	TSharedPtr<SWidget> FindToolbarWrapButton(const TSharedRef<SWidget>& InClipped)
	{
		const TSharedPtr<SWidget> Box = DreamSlatePanel::FindAncestorOfType(InClipped, TEXT("SClippingHorizontalBox"));
		FChildren* Children = Box.IsValid() ? Box->GetChildren() : nullptr;
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
			TSharedPtr<SWidget> Button;
			if (DreamSlatePanel::IsOfType(*Child, TEXT("SComboButton")))
			{
				Button = Child;
			}
			else if (Child->GetType() == FName(TEXT("SHorizontalBox")) && Child->GetChildren() != nullptr)
			{
				for (int32 Inner = 0; Inner < Child->GetChildren()->Num() && !Button.IsValid(); ++Inner)
				{
					const TSharedRef<SWidget> InnerChild = Child->GetChildren()->GetChildAt(Inner);
					if (DreamSlatePanel::IsOfType(*InnerChild, TEXT("SComboButton")))
					{
						Button = InnerChild;
					}
				}
			}
			if (DreamDesignerPanels::IsPainted(Button))
			{
				return Button;
			}
		}
		return nullptr;
	}

	/** The DPI toggle's entry in the open overflow menu: a painted widget, in a window of its own, saying what the toggle says. */
	TSharedPtr<SWidget> FindDPIOverflowEntry()
	{
		return DreamSlatePanel::FindInAnyWindow([](const TSharedRef<SWidget>& InWidget)
		{
			return !DreamSlatePanel::IsOfType(*InWidget, TEXT("SCheckBox")) && ToolTipSays(InWidget, TEXT("DPI curve"))
				&& DreamDesignerPanels::IsPainted(InWidget);
		});
	}

	/**
	 * A hand's click on the DPI toggle: on the toolbar where it has room, or -- as a narrow toolbar leaves it -- through
	 * the overflow button and the entry the toggle has in its menu, which runs the same action.
	 */
	void EnqueueClickDPIToggle(FAutomationTestBase* InTest, const FDesignerLatentRef& InState, const TCHAR* InWhat)
	{
		TSharedRef<bool> bClicked = MakeShared<bool>(false);
		EnqueueDesignerUntil(InState, InTest, [InState]()
		{
			const TSharedPtr<SWidget> Clipped = FindAnyDPIToggle(*InState->Driver);
			return FindDPIToggle(*InState->Driver).IsValid() || FindDPIOverflowEntry().IsValid()
				|| (Clipped.IsValid() && FindToolbarWrapButton(Clipped.ToSharedRef()).IsValid());
		}, PanelSeconds, TEXT("the viewport's toolbar showing the DPI preview toggle, or the button its overflow is behind"), /*bInDraw*/ false,
			[InState]() { return DescribeDPICandidates(*InState->Driver); });
		EnqueueDesignerAction(InState, [InTest, InState, InWhat, bClicked]()
		{
			if (const TSharedPtr<SWidget> Toggle = FindDPIToggle(*InState->Driver))
			{
				*bClicked = ClickPanelWidget(*InTest, InState, Toggle, InWhat);
			}
			else if (!FindDPIOverflowEntry().IsValid())
			{
				const TSharedPtr<SWidget> Clipped = FindAnyDPIToggle(*InState->Driver);
				ClickPanelWidget(*InTest, InState, Clipped.IsValid() ? FindToolbarWrapButton(Clipped.ToSharedRef()) : nullptr,
					TEXT("the toolbar's overflow button"));
			}
		});
		EnqueueDesignerUntil(InState, InTest, [bClicked]() { return *bClicked || FindDPIOverflowEntry().IsValid(); }, PanelSeconds,
			TEXT("the toolbar's overflow menu showing the DPI preview toggle"));
		EnqueueDesignerAction(InState, [InTest, InState, InWhat, bClicked]()
		{
			if (!*bClicked)
			{
				ClickPanelWidget(*InTest, InState, FindDPIOverflowEntry(), InWhat);
			}
		});
	}

	/** A painted entry of an open menu showing exactly InLabel, in whichever window the menu opened in. */
	TSharedPtr<SWidget> FindMenuEntry(const FString& InLabel)
	{
		return DreamSlatePanel::FindInAnyWindow([&InLabel](const TSharedRef<SWidget>& InWidget)
		{
			return DreamSlatePanel::IsOfType(*InWidget, TEXT("SMenuEntryBlock"))
				&& DreamSlatePanel::FindTextBlock(InWidget, InLabel).IsValid()
				&& DreamDesignerPanels::IsPainted(InWidget);
		});
	}

	/** The painted text on the viewport's toolbar that reads InLabel -- the camera menu's own button says 2D or 3D. */
	TSharedPtr<SWidget> FindToolbarLabel(const FDreamDesignerDriver& InDriver, const FString& InLabel)
	{
		const TSharedPtr<SWidget> Shell = InDriver.ViewportShell();
		const TSharedPtr<SWidget> Label = Shell.IsValid() ? DreamSlatePanel::FindTextBlock(Shell.ToSharedRef(), InLabel) : nullptr;
		return DreamDesignerPanels::IsPainted(Label) ? Label : nullptr;
	}

	/** The combo button -- a menu anchor -- that holds InLabel, the camera button's text; null without one. */
	TSharedPtr<SWidget> CameraMenuAnchor(const TSharedPtr<SWidget>& InLabel)
	{
		return InLabel.IsValid() ? DreamSlatePanel::FindAncestorOfType(InLabel.ToSharedRef(), TEXT("SComboButton")) : nullptr;
	}

	/** Whether the menu of the combo button holding InLabel is open, and whether a click would open it, for a report. */
	FString MenuAnchorStateOf(const TSharedPtr<SWidget>& InLabel)
	{
		const TSharedPtr<SWidget> Anchor = CameraMenuAnchor(InLabel);
		if (!Anchor.IsValid())
		{
			return TEXT("menu anchor not found");
		}
		return FString::Printf(TEXT("menu open %d, a click would open it %d, menus %s"),
			StaticCastSharedPtr<SMenuAnchor>(Anchor)->IsOpen() ? 1 : 0, StaticCastSharedPtr<SMenuAnchor>(Anchor)->ShouldOpenDueToClick() ? 1 : 0,
			FSlateApplication::Get().AnyMenusVisible() ? TEXT("up") : TEXT("down"));
	}
}

// ================================================================================================ the hierarchy tree

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerHierarchyReorderTest,
	"DreamGUI.Designer.SlatePanels.DraggingAHierarchyRowOntoTheTopEdgeOfAnotherPutsItAheadOfThatRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * Three widgets under the root, made in the order First, Second, Third. Third's row is dragged onto the top edge of
 * First's -- the few units STableRow counts as above a row -- and let go: Third moves ahead of First among its siblings,
 * in the asset and in the preview, as UMG's hierarchy inserts a row dropped above another before it. One undo puts the
 * order back.
 */
bool FDreamDesignerHierarchyReorderTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerHierarchyReorder"));
	TSharedRef<FString> OrderBefore = MakeShared<FString>();
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, OrderBefore]()
	{
		if (DropPlain(*this, State, TEXT("First"), 0.3, 0.3) && DropPlain(*this, State, TEXT("Second"), 0.5, 0.5) && DropPlain(*this, State, TEXT("Third"), 0.7, 0.7))
		{
			*OrderBefore = OrderUnder(DesignerTemplateRoot(State->Asset.Blueprint));
		}
	});
	EnqueueDesignerUntil(State, this, [State]()
	{
		return DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("First"))).IsValid()
			&& DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("Third"))).IsValid();
	}, PanelSeconds, TEXT("the hierarchy tree showing a painted row for the first and the third widget"));
	EnqueueDesignerAction(State, [this, State]()
	{
		const TSharedPtr<SWidget> ThirdRow = DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("Third")));
		const TSharedPtr<SWidget> FirstRow = DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("First")));
		if (!TestTrue(TEXT("Both rows are there to drag between"), ThirdRow.IsValid() && FirstRow.IsValid()))
		{
			State->bAlive = false;
			return;
		}
		const TSharedRef<SWidget> Rows[] = { ThirdRow.ToSharedRef(), FirstRow.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Rows, State->Route);
		// A unit and a half down from the top: inside the boundary STableRow::ZoneFromPointerPosition keeps for "above"
		// (a quarter of the row, but never less than three units), and across the row's width.
		const FVector2D AboveFirst(FirstRow->GetPaintSpaceGeometry().GetLocalSize().X * 0.5, 1.5);
		FString WhyNot;
		if (!DreamSlatePanel::DragOntoAt(ThirdRow.ToSharedRef(), FirstRow.ToSharedRef(), AboveFirst, Route, 4, WhyNot))
		{
			AddError(FString::Printf(TEXT("The drag of the third row onto the first's top edge did not complete: %s."), *WhyNot));
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, OrderBefore]()
	{
		const UDreamWidget* Root = DesignerTemplateRoot(State->Asset.Blueprint);
		const FString Expected = FString::Printf(TEXT("%s, %s, %s"), *State->Get(TEXT("Third"))->GetDisplayName(),
			*State->Get(TEXT("First"))->GetDisplayName(), *State->Get(TEXT("Second"))->GetDisplayName());
		TestEqual(TEXT("In the asset, the dragged widget now stands ahead of the row it was dropped above"), OrderUnder(Root), Expected);
		TestEqual(TEXT("...and in the preview"), OrderUnder(State->Driver->BlueprintRoot()), Expected);
		TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, OrderBefore]()
	{
		TestEqual(TEXT("One undo puts the order back"), OrderUnder(DesignerTemplateRoot(State->Asset.Blueprint)), *OrderBefore);
		AddInfo(FString::Printf(TEXT("The drag went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerHierarchyMoveBetweenPanelsTest,
	"DreamGUI.Designer.SlatePanels.DraggingAHierarchyRowFromOnePanelOntoAnotherMovesTheWidgetThereAndUndoBringsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * Two vertical boxes, the first holding a plain widget. The widget's row is dragged onto the second box's row and let
 * go on its middle: the widget is the second box's child now, and the first box's no longer, in the asset and in the
 * preview -- UMG's hierarchy reparents a row dropped onto a panel's row into that panel. One undo puts it back in the
 * first box.
 */
bool FDreamDesignerHierarchyMoveBetweenPanelsTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerHierarchyBetweenPanels"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		const FIntPoint Size = Driver.ViewportPixelSize();
		UDreamWidget* From = DropOntoRootAndFindTemplate(Driver, UDreamLayoutContainerVerticalBox::StaticClass(), FIntPoint(Size.X / 3, Size.Y / 2));
		UDreamWidget* To = DropOntoRootAndFindTemplate(Driver, UDreamLayoutContainerVerticalBox::StaticClass(), FIntPoint(Size.X * 2 / 3, Size.Y / 2));
		if (!TestTrue(TEXT("Two vertical boxes were dropped under the root"), From != nullptr && To != nullptr))
		{
			State->bAlive = false;
			return;
		}
		State->Made.Add(TEXT("From"), From);
		State->Made.Add(TEXT("To"), To);
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* From = State->Get(TEXT("From"));
		const TOptional<FIntPoint> Inside = Driver.WidgetPixel(Driver.PreviewFor(From));
		const TArray<UDreamWidget*> Before = LiveChildrenOf(From);
		// Dropped into the first box: the container under the pointer takes a palette drop (GetDropContainerUnderCursor).
		if (!TestTrue(TEXT("The first box is on screen"), Inside.IsSet()) || !TestTrue(TEXT("A plain widget's drop into the first box is taken"),
			Driver.DropFromPalette(static_cast<UClass*>(nullptr), Inside.GetValue())))
		{
			State->bAlive = false;
			return;
		}
		for (UDreamWidget* Child : LiveChildrenOf(From))
		{
			if (!Before.Contains(Child))
			{
				State->Made.Add(TEXT("Mover"), Child);
			}
		}
		if (!TestNotNull(TEXT("The plain widget is the first box's child"), State->Get(TEXT("Mover"))))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerUntil(State, this, [State]()
	{
		return DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("Mover"))).IsValid()
			&& DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("To"))).IsValid();
	}, PanelSeconds, TEXT("the hierarchy tree showing a painted row for the widget in the first box and for the second box"));
	EnqueueDesignerAction(State, [this, State]()
	{
		const TSharedPtr<SWidget> MoverRow = DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("Mover")));
		const TSharedPtr<SWidget> ToRow = DreamDesignerPanels::HierarchyRowFor(*State->Driver, State->Get(TEXT("To")));
		if (!TestTrue(TEXT("Both rows are there to drag between"), MoverRow.IsValid() && ToRow.IsValid()))
		{
			State->bAlive = false;
			return;
		}
		const TSharedRef<SWidget> Rows[] = { MoverRow.ToSharedRef(), ToRow.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Rows, State->Route);
		FString WhyNot;
		if (!DreamSlatePanel::DragOnto(MoverRow.ToSharedRef(), ToRow.ToSharedRef(), Route, 4, WhyNot))
		{
			AddError(FString::Printf(TEXT("The drag of the widget's row onto the second box's row did not complete: %s."), *WhyNot));
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		UDreamWidget* Mover = State->Get(TEXT("Mover"));
		UDreamWidget* From = State->Get(TEXT("From"));
		UDreamWidget* To = State->Get(TEXT("To"));
		if (!TestTrue(TEXT("All three authored widgets survived the drop"), Mover != nullptr && From != nullptr && To != nullptr))
		{
			return;
		}
		TestTrue(TEXT("In the asset, the widget is the second box's child now"), Mover->GetParent() == To);
		TestEqual(TEXT("...and the first box holds nothing"), LiveChildrenOf(From).Num(), 0);
		const UDreamWidget* PreviewMover = State->Driver->PreviewFor(Mover);
		const UDreamWidget* PreviewTo = State->Driver->PreviewFor(To);
		TestTrue(TEXT("...and so it is in the preview"), PreviewMover != nullptr && PreviewTo != nullptr && PreviewMover->GetParent() == PreviewTo);
		TestTrue(TEXT("There is something to undo"), GEditor->UndoTransaction());
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		const UDreamWidget* Mover = State->Get(TEXT("Mover"));
		TestTrue(TEXT("One undo puts the widget back in the first box"), Mover != nullptr && Mover->GetParent() == State->Get(TEXT("From")));
		AddInfo(FString::Printf(TEXT("The drag went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

// ================================================================================================ the details panel

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDetailsTypedUndoTest,
	"DreamGUI.Designer.SlatePanels.AnOpacityTypedIntoTheDetailsPanelReachesThePreviewAndTheAssetAndCtrlZTakesItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A widget clicked in the viewport, "Opacity" typed into the details panel's search, the Opacity spin box entered as a
 * keyboard user enters it and "0.25" typed and committed: the preview shows it and the asset holds it. The value is one
 * transaction, as a UMG details edit is (FScopedTransaction in the property handle's SetValue), so Ctrl+Z on the viewport
 * puts both back.
 */
bool FDreamDesignerDetailsTypedUndoTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDetailsTypedUndo"));
	TSharedRef<float> OpacityBefore = MakeShared<float>(1.0f);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, OpacityBefore]()
	{
		if (!DropPlain(*this, State, TEXT("Target"), 0.5, 0.5))
		{
			return;
		}
		FDreamDesignerDriver& Driver = *State->Driver;
		*OpacityBefore = State->Get(TEXT("Target"))->GetRenderOpacity();
		const TOptional<FIntPoint> Pixel = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Target"))));
		TestTrue(TEXT("A click on the widget in the viewport selects it"), Pixel.IsSet() && Driver.ClickAt(Pixel.GetValue()));
	});
	EnqueueDesignerUntil(State, this, [State]() { return DreamDesignerPanels::DetailsSearchBox(*State->Driver).IsValid(); },
		PanelSeconds, TEXT("the details panel showing a painted search box"));
	EnqueueDesignerAction(State, [this, State]()
	{
		if (!TypeInto(*this, DreamDesignerPanels::DetailsSearchBox(*State->Driver), TEXT("Opacity"), TEXT("The details panel's search box")))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerUntil(State, this, [State]() { return DreamDesignerPanels::DetailsValueWidget(*State->Driver, TEXT("Opacity"), TEXT("SSpinBox")).IsValid(); },
		PanelSeconds, TEXT("the details panel showing the Opacity row's spin box, painted"));
	EnqueueDesignerAction(State, [this, State]()
	{
		FString WhyNot;
		const TSharedPtr<SWidget> SpinBox = DreamDesignerPanels::DetailsValueWidget(*State->Driver, TEXT("Opacity"), TEXT("SSpinBox"));
		// Entered by navigation, which selects all of its text (SSpinBox::OnFocusReceived): the characters replace the value.
		const bool bTyped = TypeInto(*this, SpinBox, TEXT("0.25"), TEXT("The Opacity spin box"));
		const bool bCommitted = bTyped && DreamSlatePanel::PressKey(EKeys::Enter, FModifierKeysState(), WhyNot);
		if (!TestTrue(*FString::Printf(TEXT("Enter commits the typed value (%s)"), *WhyNot), bCommitted))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		const UDreamWidget* Template = State->Get(TEXT("Target"));
		const UDreamWidget* Preview = State->Driver->PreviewFor(Template);
		if (TestTrue(TEXT("The widget and its preview are still there"), Template != nullptr && Preview != nullptr))
		{
			TestEqual(TEXT("The preview shows the typed opacity"), Preview->GetRenderOpacity(), 0.25f, 0.001f);
			TestEqual(TEXT("...and the asset holds it"), Template->GetRenderOpacity(), 0.25f, 0.001f);
		}
	});
	// Back on the viewport, as an author clicks back into it, so the keys reach the designer rather than the spin box's text.
	EnqueueDesignerKeyboardFocus(State, this);
	EnqueueDesignerShortcut(State, this, EKeys::Z, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, OpacityBefore]()
	{
		const UDreamWidget* Template = State->Get(TEXT("Target"));
		const UDreamWidget* Preview = State->Driver->PreviewFor(Template);
		if (TestTrue(TEXT("The widget and its preview are still there after the undo"), Template != nullptr && Preview != nullptr))
		{
			TestEqual(TEXT("Ctrl+Z took the typed opacity back in the asset"), Template->GetRenderOpacity(), *OpacityBefore, 0.001f);
			TestEqual(TEXT("...and in the preview"), Preview->GetRenderOpacity(), *OpacityBefore, 0.001f);
		}
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDetailsCheckBoxTest,
	"DreamGUI.Designer.SlatePanels.ClickingTheIsFocusableCheckBoxInTheDetailsPanelFlipsItOnThePreviewAndTheAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A widget selected by a click in the viewport, the details panel filtered to "Focusable", and the Is Focusable check box
 * clicked: the flag flips on the preview and on the asset, as a UMG bool property's check box sets the property on the
 * template and the preview together. Clicked again, it flips back. A check box takes a click without moving the desk's
 * cursor, so this one is clicked, where a spin box is typed into.
 */
bool FDreamDesignerDetailsCheckBoxTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDetailsCheckBox"));
	TSharedRef<bool> FocusableBefore = MakeShared<bool>(false);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, FocusableBefore]()
	{
		if (!DropPlain(*this, State, TEXT("Target"), 0.5, 0.5))
		{
			return;
		}
		FDreamDesignerDriver& Driver = *State->Driver;
		*FocusableBefore = State->Get(TEXT("Target"))->GetIsFocusable();
		const TOptional<FIntPoint> Pixel = Driver.WidgetPixel(Driver.PreviewFor(State->Get(TEXT("Target"))));
		TestTrue(TEXT("A click on the widget in the viewport selects it"), Pixel.IsSet() && Driver.ClickAt(Pixel.GetValue()));
	});
	EnqueueDesignerUntil(State, this, [State]() { return DreamDesignerPanels::DetailsSearchBox(*State->Driver).IsValid(); },
		PanelSeconds, TEXT("the details panel showing a painted search box"));
	EnqueueDesignerAction(State, [this, State]()
	{
		if (!TypeInto(*this, DreamDesignerPanels::DetailsSearchBox(*State->Driver), TEXT("Focusable"), TEXT("The details panel's search box")))
		{
			State->bAlive = false;
		}
	});
	const auto CheckBox = [State]() { return DreamDesignerPanels::DetailsValueWidget(*State->Driver, TEXT("Is Focusable"), TEXT("SCheckBox")); };
	EnqueueDesignerUntil(State, this, [CheckBox]() { return CheckBox().IsValid(); }, PanelSeconds,
		TEXT("the details panel showing the Is Focusable row's check box, painted"));
	EnqueueDesignerAction(State, [this, State, CheckBox]() { ClickPanelWidget(*this, State, CheckBox(), TEXT("the Is Focusable check box")); });
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, FocusableBefore]()
	{
		const UDreamWidget* Template = State->Get(TEXT("Target"));
		const UDreamWidget* Preview = State->Driver->PreviewFor(Template);
		if (TestTrue(TEXT("The widget and its preview are still there"), Template != nullptr && Preview != nullptr))
		{
			TestEqual(TEXT("The click flipped Is Focusable in the asset"), Template->GetIsFocusable(), !*FocusableBefore);
			TestEqual(TEXT("...and on the preview"), Preview->GetIsFocusable(), !*FocusableBefore);
		}
	});
	EnqueueDesignerUntil(State, this, [CheckBox]() { return CheckBox().IsValid(); }, PanelSeconds,
		TEXT("the details panel showing the Is Focusable check box again after the edit"));
	EnqueueDesignerAction(State, [this, State, CheckBox]() { ClickPanelWidget(*this, State, CheckBox(), TEXT("the Is Focusable check box, again")); });
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, FocusableBefore]()
	{
		const UDreamWidget* Template = State->Get(TEXT("Target"));
		TestTrue(TEXT("A second click flipped it back"), Template != nullptr && Template->GetIsFocusable() == *FocusableBefore);
		AddInfo(FString::Printf(TEXT("The clicks went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

// ================================================================================================ the viewport's toolbar

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerToolbarDPIPreviewTest,
	"DreamGUI.Designer.SlatePanels.ClickingTheToolbarsDPIPreviewShrinksTheDesignCanvasByTheProjectsCurveAndAgainPutsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A 3840 by 2160 device previewed, and the toolbar's DPI preview clicked: the design canvas becomes the device size over
 * the project's own DPI curve at that size (UUserInterfaceSettings::GetDPIScaleBasedOnSize), which is what UMG's designer
 * previews; clicked again, it is the device size once more. The toggle saves the author's preference, so the test saves
 * the original back when it is done.
 */
bool FDreamDesignerToolbarDPIPreviewTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FIntPoint Device(3840, 2160);
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerToolbarDPIPreview"));
	State->Preferences = MakeShared<FScopedDesignerPreferences>(/*bInSaveWhenPuttingBack*/ true);
	State->Preferences->SetPreviewDPIScale(false);
	// An overflow menu the clicks went through is not left up for the next test.
	State->OnTeardown.Add([]() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().DismissAllMenus(); } });
	const float Curve = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Device);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Device, Curve]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
		// Picked the way the toolbar's screen-size menu picks a device (SDreamWidgetDesignerViewportToolbar): the asset
		// records it, and the DPI toggle re-applies the recorded size. Applied without recording, the toggle went back
		// to the asset's own 1920 by 1080.
		Toolkit->SetDesignerSizeRule(EDreamUIDesignerSizeRule::Custom);
		Toolkit->SetDesignerViewportSize(Device);
		TestTrue(FString::Printf(TEXT("With the preview off the design canvas is the device: %s"), *Toolkit->GetDesignerCanvasSize().ToString()),
			Toolkit->GetDesignerCanvasSize() == Device);
		if (FMath::IsNearlyEqual(Curve, 1.0f, 0.01f))
		{
			AddWarning(FString::Printf(TEXT("This project's DPI curve gives %s a scale of %.3f, so the preview has nothing to shrink."), *Device.ToString(), Curve));
		}
	});
	EnqueueClickDPIToggle(this, State, TEXT("the DPI preview toggle"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Device, Curve]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
		const FIntPoint Expected = FDreamWidgetBlueprintEditor::ApplyDPIScaleToViewportSize(Device, Curve);
		TestTrue(TEXT("The click turned the DPI preview on"), Toolkit->GetPreviewDPIScale());
		const FIntPoint Canvas = Toolkit->GetDesignerCanvasSize();
		TestTrue(FString::Printf(TEXT("...and the design canvas is the device over the curve: %s, expected %s"), *Canvas.ToString(), *Expected.ToString()),
			FMath::Abs(Canvas.X - Expected.X) <= 1 && FMath::Abs(Canvas.Y - Expected.Y) <= 1);
	});
	EnqueueClickDPIToggle(this, State, TEXT("the DPI preview toggle, again"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Device]()
	{
		FDreamWidgetBlueprintEditor* Toolkit = State->Driver->Toolkit();
		TestFalse(TEXT("The second click turned the DPI preview off"), Toolkit->GetPreviewDPIScale());
		TestTrue(FString::Printf(TEXT("...and the design canvas is the device again: %s"), *Toolkit->GetDesignerCanvasSize().ToString()),
			Toolkit->GetDesignerCanvasSize() == Device);
		AddInfo(FString::Printf(TEXT("The clicks went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerToolbarCameraMenuTest,
	"DreamGUI.Designer.SlatePanels.ChoosingThreeDFromTheToolbarsCameraMenuTurnsTheViewPerspectiveAndTwoDTurnsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * The toolbar's camera button -- it reads 2D in the designer's flat view -- clicked, and 3D chosen from the menu it opens:
 * the view is the perspective one. The button reads 3D then; clicked, and 2D chosen: the view is flat again. A menu entry
 * is a click like any other, in whichever window the menu opened in.
 */
bool FDreamDesignerToolbarCameraMenuTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerToolbarCameraMenu"));
	State->OnTeardown.Add([]() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().DismissAllMenus(); } });
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerUntil(State, this, [State]() { return FindToolbarLabel(*State->Driver, TEXT("2D")).IsValid(); }, PanelSeconds,
		TEXT("the viewport's toolbar showing its camera button reading 2D"));
	EnqueueDesignerAction(State, [this, State]() { ClickPanelWidget(*this, State, FindToolbarLabel(*State->Driver, TEXT("2D")), TEXT("the camera button")); });
	EnqueueDesignerUntil(State, this, []() { return FindMenuEntry(TEXT("3D")).IsValid(); }, PanelSeconds, TEXT("the camera menu showing its 3D entry"));
	EnqueueDesignerAction(State, [this, State]() { ClickPanelWidget(*this, State, FindMenuEntry(TEXT("3D")), TEXT("the menu's 3D entry")); });
	EnqueueDesignerUntil(State, this, [State]()
	{
		const FEditorViewportClient* Client = State->Driver->ViewportClient();
		return Client != nullptr && Client->IsPerspective();
	}, PanelSeconds, TEXT("the view turning perspective"));
	// Reading 3D, and past the frame its menu went down in. A combo button clicked in the same frame its menu was dismissed
	// closes it rather than opening it again -- the click that dismissed a menu is taken to be the one on its button
	// (SComboButton::OnButtonClicked asks SMenuAnchor::ShouldOpenDueToClick, which says no until the anchor's next Tick) --
	// and the entry's click and this one would otherwise come in one frame, which no hand does.
	EnqueueDesignerUntil(State, this, [State]()
	{
		const TSharedPtr<SWidget> Anchor = CameraMenuAnchor(FindToolbarLabel(*State->Driver, TEXT("3D")));
		return Anchor.IsValid() && StaticCastSharedPtr<SMenuAnchor>(Anchor)->ShouldOpenDueToClick();
	}, PanelSeconds, TEXT("the camera button reading 3D, ready to open its menu again"));
	EnqueueDesignerAction(State, [this, State]() { ClickPanelWidget(*this, State, FindToolbarLabel(*State->Driver, TEXT("3D")), TEXT("the camera button, again")); });
	EnqueueDesignerUntil(State, this, []() { return FindMenuEntry(TEXT("2D")).IsValid(); }, PanelSeconds, TEXT("the camera menu showing its 2D entry"),
		/*bInDraw*/ false, [State]()
	{
		// What there was instead: every menu entry any window holds, with its text, and whether it is drawn.
		TArray<FString> Entries;
		TArray<TSharedRef<SWindow>> Windows;
		DreamSlatePanel::CollectWindows(Windows);
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			TArray<TSharedRef<SWidget>> All;
			DreamSlatePanel::CollectDescendants(Window, All);
			for (const TSharedRef<SWidget>& Widget : All)
			{
				if (DreamSlatePanel::IsOfType(*Widget, TEXT("SMenuEntryBlock")))
				{
					const TSharedPtr<SWidget> Text = DreamSlatePanel::FindDescendant(Widget, [](const TSharedRef<SWidget>& InText)
					{
						return InText->GetType() == FName(TEXT("STextBlock"));
					});
					Entries.Add(FString::Printf(TEXT("\"%s\"%s"), Text.IsValid() ? *StaticCastSharedPtr<STextBlock>(Text)->GetText().ToString() : TEXT(""),
						DreamDesignerPanels::IsPainted(Widget) ? TEXT("") : TEXT(" (not drawn)")));
				}
			}
		}
		return FString::Printf(TEXT("%d windows; the camera button's click went %s; its %s; the menu entries: %s."), Windows.Num(), *State->Route,
			*MenuAnchorStateOf(FindToolbarLabel(*State->Driver, TEXT("3D"))), Entries.Num() > 0 ? *FString::Join(Entries, TEXT(", ")) : TEXT("none"));
	});
	EnqueueDesignerAction(State, [this, State]() { ClickPanelWidget(*this, State, FindMenuEntry(TEXT("2D")), TEXT("the menu's 2D entry")); });
	EnqueueDesignerUntil(State, this, [State]()
	{
		const FEditorViewportClient* Client = State->Driver->ViewportClient();
		return Client != nullptr && Client->IsOrtho();
	}, PanelSeconds, TEXT("the view turning flat again"));
	EnqueueDesignerAction(State, [this, State]()
	{
		AddInfo(FString::Printf(TEXT("The clicks went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

// ================================================================================================ the palette

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPaletteDragTest,
	"DreamGUI.Designer.SlatePanels.DraggingTheButtonRowOutOfThePaletteOntoTheViewportPlacesAButtonWhereItWasLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * "Button" typed into the palette's search, the Button row pressed, dragged out of the palette and let go on the middle
 * of the designer viewport: a button is under the root, centred where the pointer let go -- UMG's palette drag onto the
 * design surface. Slate's own drag and drop carries it: the row detects the drag (STableRow, OnDragDetected), the
 * viewport takes the drop (SDreamWidgetDesignerViewport::OnDrop).
 */
bool FDreamDesignerPaletteDragTest::RunTest(const FString&)
{
	using namespace DreamDesignerPanelInputTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerPaletteDrag"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerUntil(State, this, [State]() { return DreamDesignerPanels::PaletteSearchBox(*State->Driver).IsValid(); }, PanelSeconds,
		TEXT("the palette showing a painted search box"));
	EnqueueDesignerAction(State, [this, State]()
	{
		if (!TypeInto(*this, DreamDesignerPanels::PaletteSearchBox(*State->Driver), TEXT("Button"), TEXT("The palette's search box")))
		{
			State->bAlive = false;
		}
	});
	EnqueueDesignerUntil(State, this, [State]()
	{
		return DreamDesignerPanels::PaletteRowFor(*State->Driver, TEXT("Button")).IsValid()
			&& DreamDesignerPanels::IsPainted(State->Driver->ViewportWidget());
	}, PanelSeconds, TEXT("the palette showing a painted Button row, and the viewport painted to drop it on"));
	EnqueueDesignerAction(State, [this, State]()
	{
		const TSharedPtr<SWidget> Row = DreamDesignerPanels::PaletteRowFor(*State->Driver, TEXT("Button"));
		const TSharedPtr<SWidget> Viewport = State->Driver->ViewportWidget();
		if (!TestTrue(TEXT("The palette row and the viewport are there to drag between"), Row.IsValid() && Viewport.IsValid()))
		{
			State->bAlive = false;
			return;
		}
		const TSharedRef<SWidget> Ends[] = { Row.ToSharedRef(), Viewport.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Ends, State->Route);
		FString WhyNot;
		if (!DreamSlatePanel::DragOnto(Row.ToSharedRef(), Viewport.ToSharedRef(), Route, 6, WhyNot))
		{
			AddError(FString::Printf(TEXT("The drag from the palette onto the viewport did not complete: %s."), *WhyNot));
			State->bAlive = false;
		}
	});
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* Made = nullptr;
		for (UDreamWidget* Child : LiveChildrenOf(DesignerTemplateRoot(State->Asset.Blueprint)))
		{
			if (Child->IsA<UDreamButton>())
			{
				Made = Child;
			}
		}
		if (!TestNotNull(TEXT("The drop put a button under the root"), Made))
		{
			return;
		}
		const TOptional<FIntPoint> Landed = Driver.WidgetPixel(Driver.PreviewFor(Made));
		const FIntPoint Middle = Driver.ViewportCentrePixel();
		if (TestTrue(TEXT("The button is on screen"), Landed.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("...centred where the pointer let go: (%d, %d), the viewport's middle is (%d, %d)"),
				Landed->X, Landed->Y, Middle.X, Middle.Y), FMath::Abs(Landed->X - Middle.X) <= 4 && FMath::Abs(Landed->Y - Middle.Y) <= 4);
		}
		AddInfo(FString::Printf(TEXT("The drag went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
