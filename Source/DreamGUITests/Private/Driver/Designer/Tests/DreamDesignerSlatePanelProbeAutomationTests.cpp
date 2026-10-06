// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/DreamSlatePanelDriver.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Async/AsyncResult.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerModes.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Designer/SDreamWidgetDesignerViewport.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "DreamWidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformTime.h"
#include "IAutomationDriver.h"
#include "IAutomationDriverModule.h"
#include "IDriverElement.h"
#include "LocateBy.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

/*
 * CAN THE DESIGNER'S OWN SLATE PANELS BE DRIVEN BY A HAND'S INPUT?
 *
 * The designer viewport has a driver (FDreamDesignerDriver), which goes in through FSceneViewport. The panels around it
 * -- the hierarchy tree, the details panel, the toolbar -- are plain Slate, and every test of them so far calls the
 * function behind the widget. These probes put input into the widgets themselves and say what came of it: a row of
 * the hierarchy dragged onto another to re-parent it, a number typed into the details panel and the preview following
 * it, the toolbar's mode switch clicked. Each one reports, at Info, the road its input took (DreamSlatePanel's two:
 * Slate's hit test, or the widget's own path when the hit test does not reach the panel) and how many engine frames it
 * spent, so a run of them answers whether the next designer tests can be written against the panels.
 *
 * NonNullRHI, because they need Slate to have laid the designer's tabs out and painted them: a headless editor under
 * -nullrhi leaves the designer's tab unarranged (DreamDesignerDriverProbeAutomationTests.cpp, the viewport size probe),
 * and a widget never arranged has no position for a pointer to be at. Under a real RHI rendering off screen the tab is
 * laid out; whether its window takes Slate's hit test there is one of the things the probes report.
 *
 * The details probe enters its number by keyboard, as a keyboard user tabs into a field: a click into a spin box asks
 * Slate for high-precision mouse movement and puts the desk's cursor back on release (SSpinBox::OnMouseButtonDown and
 * OnMouseButtonUp), which a test on a machine somebody is using must not do. The last probe drives one click through the
 * engine's AutomationDriver, whose application stands in for the platform's and so owns the cursor, for comparison.
 */
namespace DreamDesignerSlatePanelProbeLocal
{
	using namespace DreamTests;

	/** Everything one probe carries across the frames it spans. */
	struct FProbeState
	{
		FDesignerTestAsset Asset;
		TSharedPtr<FDreamDesignerDriver> Driver;
		/** Cleared by the first step that gives up; latent commands all run, so every step asks. */
		bool bAlive = true;
		/** Engine frames the probe's own steps have spent, counted from the designer opening. */
		int32 Frames = 0;
		/** The road the input took, and why, for the report. */
		FString Route;
		TWeakObjectPtr<UDreamWidget> FirstTemplate;
		TWeakObjectPtr<UDreamWidget> SecondTemplate;
		/** For the AutomationDriver probe: held until its answer is in, so nothing it is still doing loses its driver. */
		bool bDriverWasEnabled = false;
		TSharedPtr<IAsyncAutomationDriver, ESPMode::ThreadSafe> AutomationDriver;
		TSharedPtr<IAsyncDriverElement, ESPMode::ThreadSafe> AutomationElement;
		TOptional<TAsyncResult<bool>> Pending;
		double PendingSince = 0.0;
	};

	/** The asset the New Widget Blueprint dialog builds, a Canvas Panel on its root, and its designer open and sized. */
	TSharedRef<FProbeState> OpenProbe(const TCHAR* InName)
	{
		TSharedRef<FProbeState> State = MakeShared<FProbeState>();
		State->Asset = CreateDesignerTestAsset(InName, /*bGiveRootAPanel*/ true);
		if (!State->Asset.IsValid())
		{
			State->bAlive = false;
			return State;
		}
		State->Driver = FDreamDesignerDriver::Open(State->Asset.Blueprint);
		if (!State->Driver.IsValid())
		{
			State->bAlive = false;
			return State;
		}
		// Changes nothing on a designer a real window has laid out; fills the hole where nothing has.
		State->Driver->EnsureHeadlessSize(FIntPoint(1280, 720));
		return State;
	}

	void EnqueueStep(TFunction<bool()> InStep)
	{
		// Through a named local rather than straight into the macro: a lambda's capture list carries commas.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/** Let InFrames engine frames pass, counted on the probe. */
	void EnqueueFrames(const TSharedRef<FProbeState>& InState, int32 InFrames)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(FMath::Max(InFrames, 0));
		EnqueueStep([InState, Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			--(*Remaining);
			++InState->Frames;
			return *Remaining <= 0;
		});
	}

	/**
	 * Frames until InReady says the panel is there to act on, up to InMaxFrames; then, if it never was, InTest is told
	 * what was being waited for and the probe gives up.
	 */
	void EnqueueUntil(const TSharedRef<FProbeState>& InState, FAutomationTestBase* InTest, TFunction<bool()> InReady, int32 InMaxFrames, const FString& InWhat)
	{
		TSharedRef<int32> Spent = MakeShared<int32>(0);
		EnqueueStep([InState, InTest, InReady, InMaxFrames, InWhat, Spent]()
		{
			if (!InState->bAlive)
			{
				return true;
			}
			if (InReady())
			{
				return true;
			}
			++InState->Frames;
			if (++(*Spent) >= InMaxFrames)
			{
				InTest->AddError(FString::Printf(TEXT("After %d frames %s still was not so; the probe stops here."), *Spent, *InWhat));
				InState->bAlive = false;
				return true;
			}
			return false;
		});
	}

	/** Close the designer, a frame, and let the asset go -- the order every latent designer probe in this module keeps. */
	void EnqueueTeardown(const TSharedRef<FProbeState>& InState)
	{
		EnqueueStep([InState]()
		{
			if (InState->Driver.IsValid())
			{
				InState->Driver->Close();
				InState->Driver.Reset();
			}
			return true;
		});
		EnqueueStep([InState]()
		{
			ReleaseDesignerTestAsset(InState->Asset);
			return true;
		});
	}

	/** The hierarchy tree's row showing the preview of InTemplate, or null while it has none (not generated, not painted). */
	TSharedPtr<SWidget> HierarchyRowFor(const FProbeState& InState, const UDreamWidget* InTemplate)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InState.Driver.IsValid() ? InState.Driver->Toolkit() : nullptr;
		const TSharedPtr<SDreamWidgetEditorHierarchyView> Hierarchy = Toolkit != nullptr ? Toolkit->GetHierarchyWidget() : nullptr;
		if (!Hierarchy.IsValid() || !::IsValid(InTemplate))
		{
			return nullptr;
		}
		// The row's name is its widget's display name (SDreamWidgetEditorHierarchyViewItem::GetItemText), which a preview
		// shares with the template it stands for.
		const TSharedPtr<SWidget> Name = DreamSlatePanel::FindTextBlock(Hierarchy.ToSharedRef(), InTemplate->GetDisplayName());
		const TSharedPtr<SWidget> Row = Name.IsValid() ? DreamSlatePanel::FindAncestorOfType(Name.ToSharedRef(), TEXT("SDreamWidgetEditorHierarchyViewItem")) : nullptr;
		return Row.IsValid() && DreamSlatePanel::CentreOf(Row.ToSharedRef()).IsSet() ? Row : nullptr;
	}

	/** The details panel's spin box on the row labelled InLabel, or null while there is none painted. */
	TSharedPtr<SWidget> DetailsSpinBoxFor(const FProbeState& InState, const FString& InLabel)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InState.Driver.IsValid() ? InState.Driver->Toolkit() : nullptr;
		const TSharedPtr<SDreamWidgetDesignerDetails> Details = Toolkit != nullptr ? Toolkit->GetDesignerDetailsWidget() : nullptr;
		if (!Details.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<SWidget> Label = DreamSlatePanel::FindTextBlock(Details.ToSharedRef(), InLabel);
		// The property row the label is the name of: a details view row is an SDetailSingleItemRow holding name and value.
		const TSharedPtr<SWidget> Row = Label.IsValid() ? DreamSlatePanel::FindAncestorOfType(Label.ToSharedRef(), TEXT("SDetailSingleItemRow")) : nullptr;
		const TSharedPtr<SWidget> SpinBox = Row.IsValid()
			? DreamSlatePanel::FindDescendant(Row.ToSharedRef(), [](const TSharedRef<SWidget>& InWidget) { return DreamSlatePanel::IsOfType(*InWidget, TEXT("SSpinBox")); })
			: nullptr;
		return SpinBox.IsValid() && DreamSlatePanel::CentreOf(SpinBox.ToSharedRef()).IsSet() ? SpinBox : nullptr;
	}

	/** The details panel's search box, or null while there is none painted. */
	TSharedPtr<SWidget> DetailsSearchBox(const FProbeState& InState)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InState.Driver.IsValid() ? InState.Driver->Toolkit() : nullptr;
		const TSharedPtr<SDreamWidgetDesignerDetails> Details = Toolkit != nullptr ? Toolkit->GetDesignerDetailsWidget() : nullptr;
		const TSharedPtr<SWidget> SearchBox = Details.IsValid()
			? DreamSlatePanel::FindDescendant(Details.ToSharedRef(), [](const TSharedRef<SWidget>& InWidget) { return DreamSlatePanel::IsOfType(*InWidget, TEXT("SSearchBox")); })
			: nullptr;
		return SearchBox.IsValid() && DreamSlatePanel::CentreOf(SearchBox.ToSharedRef()).IsSet() ? SearchBox : nullptr;
	}

	/**
	 * The check box the toolbar's mode switch for InModeLabel clicks ("Designer", "Graph": SModeWidget's own label), or
	 * null while there is none painted. Looked for in the toolkit's own tab first, then in the window its viewport is in.
	 */
	TSharedPtr<SWidget> ModeSwitchFor(const FProbeState& InState, const FString& InModeLabel)
	{
		FDreamWidgetBlueprintEditor* Toolkit = InState.Driver.IsValid() ? InState.Driver->Toolkit() : nullptr;
		if (Toolkit == nullptr)
		{
			return nullptr;
		}
		TArray<TSharedRef<SWidget>> Roots;
		if (const TSharedPtr<FTabManager> TabManager = Toolkit->GetTabManager())
		{
			if (const TSharedPtr<SDockTab> OwnerTab = TabManager->GetOwnerTab())
			{
				Roots.Add(OwnerTab->GetContent());
			}
		}
		if (const TSharedPtr<SWidget> Shell = InState.Driver->ViewportShell())
		{
			if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(Shell.ToSharedRef()))
			{
				Roots.Add(Window.ToSharedRef());
			}
		}
		for (const TSharedRef<SWidget>& Root : Roots)
		{
			const TSharedPtr<SWidget> Switch = DreamSlatePanel::FindDescendant(Root, [&InModeLabel](const TSharedRef<SWidget>& InWidget)
			{
				return DreamSlatePanel::IsOfType(*InWidget, TEXT("SModeWidget"))
					&& DreamSlatePanel::FindTextBlock(InWidget, InModeLabel).IsValid();
			});
			const TSharedPtr<SWidget> CheckBox = Switch.IsValid()
				? DreamSlatePanel::FindDescendant(Switch.ToSharedRef(), [](const TSharedRef<SWidget>& InWidget) { return DreamSlatePanel::IsOfType(*InWidget, TEXT("SCheckBox")); })
				: nullptr;
			if (CheckBox.IsValid() && DreamSlatePanel::CentreOf(CheckBox.ToSharedRef()).IsSet())
			{
				return CheckBox;
			}
		}
		return nullptr;
	}

	FString ModeLabel(FName InMode)
	{
		return FDreamWidgetBlueprintApplicationModes::GetLocalizedMode(InMode).ToString();
	}
}

/**
 * A row of the hierarchy tree dragged onto another row, with a hand's press, moves and release: the dragged widget
 * becomes the target's child, in the asset and in the preview. STableRow's own drag detection and drop zones take the
 * gesture (OnDragDetected, OnCanAcceptDrop, OnAcceptDrop), as for a user; the probe lets go on the target row's middle,
 * which is the zone that drops onto it rather than beside it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSlateHierarchyDragProbe,
	"DreamGUI.Designer.SlatePanels.DraggingAHierarchyRowOntoAPanelsRowMakesItThatPanelsChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerSlateHierarchyDragProbe::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerSlatePanelProbeLocal;
	TSharedRef<FProbeState> State = OpenProbe(TEXT("SlatePanelHierarchyDrag"));
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	EnqueueFrames(State, 2);

	// A vertical box and a plain widget, both straight under the root, dropped from the palette by the viewport driver.
	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const FIntPoint Size = State->Driver->ViewportPixelSize();
		UDreamWidget* Panel = DropOntoRootAndFindTemplate(*State->Driver, UDreamLayoutContainerVerticalBox::StaticClass(), FIntPoint(Size.X / 4, Size.Y / 2));
		UDreamWidget* Mover = DropOntoRootAndFindTemplate(*State->Driver, nullptr, FIntPoint(Size.X * 3 / 4, Size.Y / 2));
		if (!TestTrue(TEXT("A vertical box and a plain widget were dropped under the root"), Panel != nullptr && Mover != nullptr)
			|| !TestNotEqual(TEXT("...under two different names, so their rows can be told apart"), Panel->GetDisplayName(), Mover->GetDisplayName()))
		{
			State->bAlive = false;
			return true;
		}
		State->FirstTemplate = Panel;
		State->SecondTemplate = Mover;
		return true;
	});
	EnqueueUntil(State, this, [State]()
	{
		return HierarchyRowFor(*State, State->FirstTemplate.Get()).IsValid() && HierarchyRowFor(*State, State->SecondTemplate.Get()).IsValid();
	}, 60, TEXT("the hierarchy tree showing a painted row for each dropped widget"));

	EnqueueStep([this, State]()
	{
		if (!State->bAlive)
		{
			return true;
		}
		const TSharedPtr<SWidget> PanelRow = HierarchyRowFor(*State, State->FirstTemplate.Get());
		const TSharedPtr<SWidget> MoverRow = HierarchyRowFor(*State, State->SecondTemplate.Get());
		if (!TestTrue(TEXT("Both rows are there to drag between"), PanelRow.IsValid() && MoverRow.IsValid()))
		{
			State->bAlive = false;
			return true;
		}
		const TSharedRef<SWidget> Rows[] = { MoverRow.ToSharedRef(), PanelRow.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Rows, State->Route);
		FString WhyNot;
		const bool bDragged = DreamSlatePanel::DragOnto(MoverRow.ToSharedRef(), PanelRow.ToSharedRef(), Route, 4, WhyNot);
		if (!TestTrue(*FString::Printf(TEXT("The drag from row to row completes (%s)"), *WhyNot), bDragged))
		{
			State->bAlive = false;
		}
		return true;
	});
	EnqueueFrames(State, 2);

	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		UDreamWidget* Panel = State->FirstTemplate.Get();
		UDreamWidget* Mover = State->SecondTemplate.Get();
		if (TestTrue(TEXT("Both authored widgets survived the drop"), Panel != nullptr && Mover != nullptr))
		{
			TestTrue(TEXT("In the asset, the dragged widget is the panel's child now"), Mover->GetParent() == Panel);
			const UDreamWidget* PreviewMover = State->Driver->PreviewFor(Mover);
			const UDreamWidget* PreviewPanel = State->Driver->PreviewFor(Panel);
			TestTrue(TEXT("...and in the preview"), PreviewMover != nullptr && PreviewPanel != nullptr && PreviewMover->GetParent() == PreviewPanel);
		}
		AddInfo(FString::Printf(TEXT("Hierarchy drag probe: the input went %s; %d engine frames from the designer opening to the check."),
			*State->Route, State->Frames));
		return true;
	});
	EnqueueTeardown(State);
	return true;
}

/**
 * A number typed into the details panel reaches the preview: the plain widget selected (by a click in the viewport),
 * the details panel's search box given "Opacity" -- so the row is in view whatever the panel's height -- the Opacity
 * spin box entered as a keyboard user enters it, "0.5" typed and Enter pressed. The preview's render opacity follows,
 * and so does the asset's (SDreamWidgetDesignerDetails carries the edit from the preview onto the template).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSlateDetailsValueProbe,
	"DreamGUI.Designer.SlatePanels.ANumberTypedIntoTheDetailsPanelIsWhatThePreviewShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerSlateDetailsValueProbe::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerSlatePanelProbeLocal;
	TSharedRef<FProbeState> State = OpenProbe(TEXT("SlatePanelDetailsValue"));
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	EnqueueFrames(State, 2);

	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		UDreamWidget* Target = DropOntoRootAndFindTemplate(*State->Driver, nullptr, State->Driver->ViewportCentrePixel());
		if (!TestNotNull(TEXT("A plain widget was dropped under the root"), Target))
		{
			State->bAlive = false;
			return true;
		}
		State->FirstTemplate = Target;
		return true;
	});
	EnqueueFrames(State, 1);
	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const UDreamWidget* Preview = State->Driver->PreviewFor(State->FirstTemplate.Get());
		const TOptional<FIntPoint> Pixel = State->Driver->WidgetPixel(Preview);
		if (!TestTrue(TEXT("The dropped widget is on the design surface to be clicked"), Pixel.IsSet()))
		{
			State->bAlive = false;
			return true;
		}
		TestTrue(TEXT("A click on it in the viewport selects it"), State->Driver->ClickAt(Pixel.GetValue()));
		return true;
	});
	EnqueueUntil(State, this, [State]() { return DetailsSearchBox(*State).IsValid(); }, 60, TEXT("the details panel showing a painted search box"));

	EnqueueStep([this, State]()
	{
		if (!State->bAlive)
		{
			return true;
		}
		const TSharedPtr<SWidget> SearchBox = DetailsSearchBox(*State);
		FString WhyNot;
		const bool bFocused = SearchBox.IsValid() && DreamSlatePanel::FocusAsKeyboardUser(SearchBox.ToSharedRef(), WhyNot);
		const bool bTyped = bFocused && DreamSlatePanel::TypeCharacters(TEXT("Opacity"), WhyNot);
		if (!TestTrue(*FString::Printf(TEXT("The details panel's search box takes the keyboard, and the label is typed into it (%s)"), *WhyNot), bTyped))
		{
			State->bAlive = false;
		}
		return true;
	});
	EnqueueUntil(State, this, [State]() { return DetailsSpinBoxFor(*State, TEXT("Opacity")).IsValid(); }, 60,
		TEXT("the details panel showing the Opacity row's spin box, painted"));

	EnqueueStep([this, State]()
	{
		if (!State->bAlive)
		{
			return true;
		}
		const TSharedPtr<SWidget> SpinBox = DetailsSpinBoxFor(*State, TEXT("Opacity"));
		FString WhyNot;
		const bool bEntered = SpinBox.IsValid() && DreamSlatePanel::FocusAsKeyboardUser(SpinBox.ToSharedRef(), WhyNot);
		// A spin box given the keyboard by navigation enters its text mode and hands the focus to its text, all of it
		// selected (SSpinBox::OnFocusReceived, SelectAllTextWhenFocused), so the characters replace the value.
		const TSharedPtr<SWidget> Focused = DreamSlatePanel::GetKeyboardFocus();
		State->Route = FString::Printf(TEXT("by keyboard: the spin box focused as a Tab would focus it, the keyboard focus then on %s"),
			*DreamSlatePanel::Describe(Focused));
		const bool bTyped = bEntered && DreamSlatePanel::TypeCharacters(TEXT("0.5"), WhyNot);
		const bool bCommitted = bTyped && DreamSlatePanel::PressKey(EKeys::Enter, FModifierKeysState(), WhyNot);
		if (!TestTrue(*FString::Printf(TEXT("The Opacity spin box takes the keyboard, \"0.5\" is typed into it and Enter commits it (%s)"), *WhyNot), bCommitted))
		{
			State->bAlive = false;
		}
		return true;
	});
	EnqueueFrames(State, 2);

	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const UDreamWidget* Template = State->FirstTemplate.Get();
		const UDreamWidget* Preview = State->Driver->PreviewFor(Template);
		if (TestTrue(TEXT("The widget and its preview are still there"), Template != nullptr && Preview != nullptr))
		{
			TestEqual(TEXT("The preview shows the opacity typed into the details panel"), Preview->GetRenderOpacity(), 0.5f, 0.001f);
			TestEqual(TEXT("...and the asset holds it"), Template->GetRenderOpacity(), 0.5f, 0.001f);
		}
		AddInfo(FString::Printf(TEXT("Details value probe: the value went in %s; %d engine frames from the designer opening to the check."),
			*State->Route, State->Frames));
		return true;
	});
	EnqueueTeardown(State);
	return true;
}

/**
 * The toolbar's mode switch, clicked: Graph, then Designer again. SModeWidget is a check box whose change asks the
 * toolkit for its mode (SModeWidget::OnModeTabClicked), so a click that reaches it is a mode change.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSlateToolbarModeProbe,
	"DreamGUI.Designer.SlatePanels.ClickingTheToolbarsModeSwitchChangesTheEditorsMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerSlateToolbarModeProbe::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerSlatePanelProbeLocal;
	TSharedRef<FProbeState> State = OpenProbe(TEXT("SlatePanelToolbarMode"));
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	const FString GraphLabel = ModeLabel(FDreamWidgetBlueprintApplicationModes::GraphMode);
	const FString DesignerLabel = ModeLabel(FDreamWidgetBlueprintApplicationModes::DesignerMode);
	EnqueueFrames(State, 2);
	EnqueueUntil(State, this, [State, GraphLabel]() { return ModeSwitchFor(*State, GraphLabel).IsValid(); }, 60,
		TEXT("the toolbar showing a painted Graph mode switch"));

	EnqueueStep([this, State, GraphLabel]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const TSharedPtr<SWidget> GraphSwitch = ModeSwitchFor(*State, GraphLabel);
		if (!TestTrue(TEXT("The Graph switch is there to click"), GraphSwitch.IsValid()))
		{
			State->bAlive = false;
			return true;
		}
		const TSharedRef<SWidget> Targets[] = { GraphSwitch.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Targets, State->Route);
		FString WhyNot;
		const bool bClicked = DreamSlatePanel::Click(GraphSwitch.ToSharedRef(), Route, WhyNot);
		TestTrue(*FString::Printf(TEXT("The click on the Graph switch completes (%s)"), *WhyNot), bClicked);
		TestTrue(TEXT("The editor is in Graph mode"), State->Driver->Toolkit() != nullptr
			&& State->Driver->Toolkit()->GetCurrentMode() == FDreamWidgetBlueprintApplicationModes::GraphMode);
		return true;
	});
	// The other mode builds its own toolbar, and the switch back is on it.
	EnqueueUntil(State, this, [State, DesignerLabel]() { return ModeSwitchFor(*State, DesignerLabel).IsValid(); }, 60,
		TEXT("the Graph mode's toolbar showing a painted Designer mode switch"));
	EnqueueStep([this, State, DesignerLabel]()
	{
		if (!State->bAlive || !State->Driver.IsValid())
		{
			return true;
		}
		const TSharedPtr<SWidget> DesignerSwitch = ModeSwitchFor(*State, DesignerLabel);
		if (!TestTrue(TEXT("The Designer switch is there to click"), DesignerSwitch.IsValid()))
		{
			State->bAlive = false;
			return true;
		}
		FString UnusedWhy;
		const TSharedRef<SWidget> Targets[] = { DesignerSwitch.ToSharedRef() };
		const EDreamSlateRoute Route = DreamSlatePanel::ChooseRoute(Targets, UnusedWhy);
		FString WhyNot;
		const bool bClicked = DreamSlatePanel::Click(DesignerSwitch.ToSharedRef(), Route, WhyNot);
		TestTrue(*FString::Printf(TEXT("The click on the Designer switch completes (%s)"), *WhyNot), bClicked);
		TestTrue(TEXT("The editor is back in Designer mode"), State->Driver->Toolkit() != nullptr
			&& State->Driver->Toolkit()->GetCurrentMode() == FDreamWidgetBlueprintApplicationModes::DesignerMode);
		AddInfo(FString::Printf(TEXT("Toolbar mode probe: the clicks went %s; %d engine frames from the designer opening to the second click."),
			*State->Route, State->Frames));
		return true;
	});
	EnqueueFrames(State, 2);
	EnqueueTeardown(State);
	return true;
}

/**
 * The same click on the Graph switch, through the engine's AutomationDriver: its application stands in for the
 * platform's while it is enabled, so the cursor it moves and the buttons it presses are its own, and the click goes
 * through FSlateApplication's message handlers and Slate's hit test like a hand's. The driver acts on the core ticker,
 * so the probe polls its answer frame by frame. Enabled only for this probe, and put back as it was found.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSlateAutomationDriverProbe,
	"DreamGUI.Designer.SlatePanels.TheAutomationDriverClicksTheToolbarsModeSwitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerSlateAutomationDriverProbe::RunTest(const FString& Parameters)
{
	using namespace DreamDesignerSlatePanelProbeLocal;
	TSharedRef<FProbeState> State = OpenProbe(TEXT("SlatePanelAutomationDriver"));
	TestTrue(TEXT("The designer opened"), State->Driver.IsValid());
	const FString GraphLabel = ModeLabel(FDreamWidgetBlueprintApplicationModes::GraphMode);
	EnqueueFrames(State, 2);
	EnqueueUntil(State, this, [State, GraphLabel]() { return ModeSwitchFor(*State, GraphLabel).IsValid(); }, 60,
		TEXT("the toolbar showing a painted Graph mode switch"));

	EnqueueStep([this, State, GraphLabel]()
	{
		if (!State->bAlive)
		{
			return true;
		}
		const TSharedPtr<SWidget> GraphSwitch = ModeSwitchFor(*State, GraphLabel);
		if (!TestTrue(TEXT("The Graph switch is there to click"), GraphSwitch.IsValid()))
		{
			State->bAlive = false;
			return true;
		}
		FString HitTestReport;
		State->Route = DreamSlatePanel::DoesHitTestReach(GraphSwitch.ToSharedRef(), HitTestReport)
			? FString(TEXT("Slate's hit test reaches the switch"))
			: FString::Printf(TEXT("Slate's hit test does not reach the switch (%s)"), *HitTestReport);

		IAutomationDriverModule& DriverModule = IAutomationDriverModule::Get();
		State->bDriverWasEnabled = DriverModule.IsEnabled();
		if (!State->bDriverWasEnabled)
		{
			DriverModule.Enable();
		}
		const TWeakPtr<SWidget> WeakSwitch = GraphSwitch;
		TSharedRef<IAsyncAutomationDriver, ESPMode::ThreadSafe> AutomationDriver = DriverModule.CreateAsyncDriver();
		State->AutomationDriver = AutomationDriver;
		TSharedRef<IAsyncDriverElement, ESPMode::ThreadSafe> Element = AutomationDriver->FindElement(By::WidgetLambda(
			[WeakSwitch](TArray<TSharedRef<SWidget>>& OutWidgets)
			{
				if (const TSharedPtr<SWidget> Switch = WeakSwitch.Pin())
				{
					OutWidgets.Add(Switch.ToSharedRef());
				}
			}, TEXT("the Graph mode switch")));
		State->AutomationElement = Element;
		State->Pending = Element->Click();
		State->PendingSince = FPlatformTime::Seconds();
		return true;
	});
	// The driver's own wait for an element is three seconds (FDriverConfiguration::ImplicitWait); this gives it more.
	EnqueueStep([this, State]()
	{
		if (!State->bAlive || !State->Pending.IsSet())
		{
			return true;
		}
		++State->Frames;
		const double Waited = FPlatformTime::Seconds() - State->PendingSince;
		if (!State->Pending->GetFuture().IsReady() && Waited < 10.0)
		{
			return false;
		}
		const bool bClicked = State->Pending->GetFuture().IsReady() && State->Pending->GetFuture().Get();
		State->Pending.Reset();
		State->AutomationElement.Reset();
		State->AutomationDriver.Reset();
		if (!State->bDriverWasEnabled)
		{
			IAutomationDriverModule::Get().Disable();
		}
		TestTrue(*FString::Printf(TEXT("The AutomationDriver says its click completed (after %.2f seconds)"), Waited), bClicked);
		TestTrue(TEXT("...and the editor is in Graph mode"), State->Driver.IsValid() && State->Driver->Toolkit() != nullptr
			&& State->Driver->Toolkit()->GetCurrentMode() == FDreamWidgetBlueprintApplicationModes::GraphMode);
		AddInfo(FString::Printf(TEXT("AutomationDriver probe: %s; the click took %.2f seconds and %d engine frames counted from the designer opening."),
			*State->Route, Waited, State->Frames));
		return true;
	});
	// Never left enabled, whatever happened above: an enabled driver keeps the desk's input from the editor.
	EnqueueStep([State]()
	{
		if (!State->bDriverWasEnabled && IAutomationDriverModule::Get().IsEnabled())
		{
			IAutomationDriverModule::Get().Disable();
		}
		return true;
	});
	EnqueueFrames(State, 2);
	EnqueueTeardown(State);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
