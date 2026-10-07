// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamInputKeySelector.h"
#include "Controls/DreamListView.h"
#include "Controls/DreamMenuAnchor.h"
#include "Controls/DreamRichTextBlock.h"
#include "Controls/DreamRingMenu.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamSpinBox.h"
#include "Controls/DreamTabView.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIGeometry.h"
#include "DreamUIBPLibrary.h"
#include "Engine/World.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextHyperlink.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamListsInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"
#include "Pie/DreamPieTestTypes.h"

/*
 * EVERY CONTROL ONCE, IN A PLAY SESSION.
 *
 * The headless rig drives each control through a pump of its own, a controller it spawned and a viewport size it handed
 * the canvas. Here each is driven once in a real play session -- the engine's game mode, local player, viewport, viewport
 * client and frames, the input actor behind the player controller (FDreamDriverPieRig) -- with the one gesture that is
 * the control's reason to exist, judged by what the control announced, as its headless tests judge it: a check box's
 * click, a slider's drag, a scroll box's wheel, a combo box's open and choice, a list row's click, a tab's click, a
 * dialog's answer, a menu's open and dismissal, a spin box's scrub, an expandable area's header, a ring menu's wedge, a
 * key selector's capture and a rich text link's click. And a button made the way a graph makes one and added to the
 * viewport.
 *
 * Everything is latent and every wait is a condition, not a count of frames: an engine frame is as long as the machine
 * makes it. Nothing of the play world is held past a step but weakly; the listeners live outside it.
 */
namespace DreamPieControlSmokeTestLocal
{
	using DreamPieTests::Live;

	FWaitTimeout ConditionLimit()
	{
		return FWaitTimeout::InSeconds(2.0);
	}

	/** The wheel's rim and hub, for the ring menu's test. */
	const float OuterRadius = 200.0f;
	const float InnerRadius = 80.0f;

	FWaitTimeout StepLimit()
	{
		return FWaitTimeout::InSeconds(3.0);
	}

	/** A wait in the sequence for InCondition, under the two limits every wait here takes. */
	FDreamDriverSequence& WaitFor(FDreamDriverSequence& InSteps, TFunction<bool()> InCondition, const TCHAR* InWhat)
	{
		return InSteps.Wait(FDreamUntil::Condition(MoveTemp(InCondition), ConditionLimit()), StepLimit(), InWhat);
	}

	/** Press and release where the pointer is, a frame each, as a click does. */
	FDreamDriverSequence& PressAndRelease(FDreamDriverSequence& InSteps)
	{
		return InSteps.Press().Release();
	}

	/** The pixel at the centre of a widget, through the context's camera when it has one. */
	TOptional<FVector2D> CentreOf(const UDreamWidget* InWidget, const FDreamDriverContext& InContext)
	{
		return InWidget != nullptr ? FDreamDriverProjection::WidgetCentrePixel(InWidget, InContext.Camera.Get()) : TOptional<FVector2D>();
	}

	/** A pixel near the bottom-right corner of the session's viewport, where nothing a test builds reaches. */
	TOptional<FVector2D> FarCorner(const FDreamDriverContext& InContext)
	{
		const FIntPoint Size = IsValid(InContext.RootCanvas) ? InContext.RootCanvas->GetViewportSize() : FIntPoint::ZeroValue;
		return Size.X > 40 && Size.Y > 40 ? TOptional<FVector2D>(FVector2D(Size.X - 20.0, Size.Y - 20.0)) : TOptional<FVector2D>();
	}

	/** The option rows of a dropdown's open list, in option order: the column's children but the template. */
	TArray<UDreamWidget*> RowsOf(const UDreamDropdown* InDropdown)
	{
		TArray<UDreamWidget*> Rows;
		UDreamWidget* Column = InDropdown != nullptr && InDropdown->ListNode != nullptr ? InDropdown->ListNode->FindChildByDisplayName(TEXT("Column")) : nullptr;
		if (Column != nullptr)
		{
			for (UDreamWidget* Row : Column->GetChildren())
			{
				if (IsValid(Row) && Row != InDropdown->ItemTemplateNode.Get())
				{
					Rows.Add(Row);
				}
			}
		}
		return Rows;
	}

	/** The pixel InRadius from the ring's centre at InAngleDegrees clockwise from twelve, in the frame of InWedge. */
	TOptional<FVector2D> RingPixel(const UDreamWidget* InWedge, float InAngleDegrees, float InRadius)
	{
		if (InWedge == nullptr)
		{
			return TOptional<FVector2D>();
		}
		const FVector2D RectCentre(
			(InWedge->GetLocalSpaceLeft() + InWedge->GetLocalSpaceRight()) * 0.5,
			(InWedge->GetLocalSpaceBottom() + InWedge->GetLocalSpaceTop()) * 0.5);
		const double Radians = FMath::DegreesToRadians(static_cast<double>(InAngleDegrees));
		const FVector2D Offset(InRadius * FMath::Sin(Radians), InRadius * FMath::Cos(Radians));
		return FDreamDriverProjection::WidgetLocalPointToPixel(InWedge, RectCentre + Offset);
	}

	/** The pixel at the middle of the quads of visible characters InFirst to InLast of a text, or unset before they are laid. */
	TOptional<FVector2D> CharactersPixel(const UDreamText& InText, int32 InFirst, int32 InLast)
	{
		const TArray<FDreamUITextCharProperty>& Chars = InText.GetCharPropertyArray();
		const FDreamUIGeometry* Geometry = InText.GetGeometry();
		if (Geometry == nullptr)
		{
			return TOptional<FVector2D>();
		}
		FBox2D Bounds(ForceInit);
		for (int32 CharIndex = FMath::Max(0, InFirst); CharIndex <= InLast && Chars.IsValidIndex(CharIndex); CharIndex++)
		{
			const FDreamUITextCharProperty& Char = Chars[CharIndex];
			for (int32 Vertex = Char.StartVertIndex; Vertex < Char.StartVertIndex + Char.VertCount; Vertex++)
			{
				if (Geometry->OriginVertices.IsValidIndex(Vertex))
				{
					const FVector3f& Position = Geometry->OriginVertices[Vertex].Position;
					Bounds += FVector2D(Position.Y, Position.Z);
				}
			}
		}
		if (!Bounds.bIsValid)
		{
			return TOptional<FVector2D>();
		}
		return FDreamDriverProjection::WidgetLocalPointToPixel(InText.GetWidget(), Bounds.GetCenter());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieToggleSmokeTest,
	"DreamGUI.Pie.Toggle.AClickThroughThePlayerControllerChecksItAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieToggleSmokeTest, "DreamGUI.Pie.Toggle.AClickThroughThePlayerControllerChecksItAndSaysSoOnce", "[Pointer][Animated]")

/* SCheckBox::ToggleCheckedState: an unchecked box clicked is Checked, and OnCheckStateChanged is told so once. */
bool FDreamPieToggleSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamToggle>> Toggle = MakeShared<TWeakObjectPtr<UDreamToggle>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Toggle](FDreamDriverPieRig& InRig)
	{
		UDreamToggle* Made = InRig.MakeControl<UDreamToggle>(TEXT("Mute"), nullptr, FVector2D(40.0, 40.0));
		if (Made != nullptr)
		{
			Made->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
		}
		*Toggle = Made;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Rig->Made(TEXT("Mute")));
	WaitFor(Steps, [Listener]() { return Listener->CheckStates.Num() > 0; }, TEXT("the toggle to announce its new state"));
	Steps.Then([this, Listener, Toggle](FDreamDriverContext&)
	{
		TestTrue(TEXT("The click checked the toggle"), Toggle->IsValid() && (*Toggle)->IsChecked());
		if (TestEqual(TEXT("...and said so once"), Listener->CheckStates.Num(), 1))
		{
			TestEqual(TEXT("...carrying Checked"), Listener->CheckStates[0], EDreamCheckState::Checked);
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieSliderSmokeTest,
	"DreamGUI.Pie.Slider.DraggingTheHandleThroughThePlayerControllerToTheMiddleOfItsTravelMovesTheValueHalfway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieSliderSmokeTest, "DreamGUI.Pie.Slider.DraggingTheHandleThroughThePlayerControllerToTheMiddleOfItsTravelMovesTheValueHalfway", "[Pointer][Animated]")

/* SSlider follows a captured pointer absolutely: the handle let go on the middle of its travel is a value of one half. */
bool FDreamPieSliderSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamDragInteractionProbe> Values(NewObject<UDreamDragInteractionProbe>());
	const TSharedRef<TWeakObjectPtr<UDreamSlider>> Slider = MakeShared<TWeakObjectPtr<UDreamSlider>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Values, Slider](FDreamDriverPieRig& InRig)
	{
		UDreamSlider* Made = InRig.MakeControl<UDreamSlider>(TEXT("Volume"), nullptr, FVector2D(400.0, 40.0));
		if (Made != nullptr)
		{
			Made->OnValueChanged.AddDynamic(Values.Get(), &UDreamDragInteractionProbe::RecordFloat);
		}
		*Slider = Made;
	});
	const auto Handle = Live([Slider]() -> UDreamWidget* { return Slider->IsValid() ? (*Slider)->HandleNode.Get() : nullptr; }, TEXT("the slider's handle"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveTo(Handle)
		.Press()
		.MoveBy(FVector2D(20.0, 0.0))
		.MoveToResolvedPixel([Slider](FDreamDriverContext& InContext) -> TOptional<FVector2D>
		{
			return Slider->IsValid() ? CentreOf((*Slider)->HandleAreaNode.Get(), InContext) : TOptional<FVector2D>();
		}, TEXT("the middle of the slider's travel"))
		.WaitFrames(1)
		.Release();
	WaitFor(Steps, [Values]() { return Values->NumFloats() > 0; }, TEXT("the slider to report its value"));
	Steps.Then([this, Slider](FDreamDriverContext&)
	{
		TestTrue(TEXT("The slider is there"), Slider->IsValid());
		TestNearlyEqual(TEXT("The value is halfway"), Slider->IsValid() ? (*Slider)->GetValue() : -1.0f, 0.5f, 0.02f);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieScrollBoxSmokeTest,
	"DreamGUI.Pie.ScrollBox.TwoWheelNotchesThroughThePlayerControllerScrollTheBoxTwoNotches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieScrollBoxSmokeTest, "DreamGUI.Pie.ScrollBox.TwoWheelNotchesThroughThePlayerControllerScrollTheBoxTwoNotches", "[Pointer][Animated]")

/* SScrollBox::OnMouseWheel: one notch of offset per wheel delta, the wheel going to the box under the pointer. */
bool FDreamPieScrollBoxSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TSharedRef<TWeakObjectPtr<UDreamScrollBox>> Box = MakeShared<TWeakObjectPtr<UDreamScrollBox>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Box](FDreamDriverPieRig& InRig)
	{
		UDreamScrollBox* Made = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 300.0));
		if (Made != nullptr && Made->GetContentNode() != nullptr)
		{
			for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
			{
				InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Made->GetContentNode(), FVector2D(300.0, 100.0));
			}
			Made->RefreshContentExtent();
		}
		*Box = Made;
	});
	const auto Viewport = Live([Box]() -> UDreamWidget* { return Box->IsValid() ? (*Box)->ViewportNode.Get() : nullptr; }, TEXT("the scroll box's viewport"));
	const TSharedRef<float> Notch = MakeShared<float>(0.0f);
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Then([this, Box, Notch](FDreamDriverContext&)
		{
			*Notch = Box->IsValid() ? (*Box)->GetScrollSensitivity() * (*Box)->GetWheelScrollMultiplier() : 0.0f;
			TestTrue(TEXT("There is more than two notches to scroll"), Box->IsValid() && *Notch > 0.0f && (*Box)->GetScrollOffsetOfEnd() > 2.0f * *Notch);
		})
		.MoveTo(Viewport)
		.ScrollBy(FVector2D(-1.0, -1.0))
		.ScrollBy(FVector2D(-1.0, -1.0));
	WaitFor(Steps, [Box, Notch]() { return Box->IsValid() && (*Box)->GetScrollOffset() >= 2.0f * *Notch - 0.5f; }, TEXT("the box to scroll two notches"));
	Steps.Then([this, Box, Notch](FDreamDriverContext&)
	{
		TestNearlyEqual(TEXT("Two notches through the controller scrolled two notches' distance"),
			Box->IsValid() ? (*Box)->GetScrollOffset() : -1.0f, 2.0f * *Notch, 0.5f);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieDropdownSmokeTest,
	"DreamGUI.Pie.Dropdown.AClickOpensTheListAndAClickOnTheSecondOptionChoosesItAndClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieDropdownSmokeTest, "DreamGUI.Pie.Dropdown.AClickOpensTheListAndAClickOnTheSecondOptionChoosesItAndClosesIt", "[Pointer][Animated]")

/* UComboBoxString: the button opens the menu, an item chosen is the selection, announced once, and the menu dismissed. */
bool FDreamPieDropdownSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamDropdown>> Dropdown = MakeShared<TWeakObjectPtr<UDreamDropdown>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Dropdown](FDreamDriverPieRig& InRig)
	{
		UDreamDropdown* Made = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(200.0, 40.0), FVector2D(0.0, 150.0));
		if (Made != nullptr)
		{
			Made->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")), FText::AsCultureInvariant(TEXT("High")) });
			Made->SetSelectedIndex(0);
			Made->OnSelectionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleSelectionChanged);
		}
		*Dropdown = Made;
	});
	const auto SecondRow = Live([Dropdown]() -> UDreamWidget*
	{
		const TArray<UDreamWidget*> Rows = Dropdown->IsValid() ? RowsOf(Dropdown->Get()) : TArray<UDreamWidget*>();
		return Rows.IsValidIndex(1) ? Rows[1] : nullptr;
	}, TEXT("the dropdown's second option"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Rig->Made(TEXT("Quality")));
	WaitFor(Steps, [Dropdown]() { return Dropdown->IsValid() && (*Dropdown)->IsOpen() && RowsOf(Dropdown->Get()).Num() == 3; },
		TEXT("the list to open with a row per option"));
	Steps.WaitFrames(2).Click(SecondRow);
	WaitFor(Steps, [Listener]() { return Listener->SelectionIndices.Num() > 0; }, TEXT("the choice to be announced"));
	Steps.Then([this, Listener, Dropdown](FDreamDriverContext&)
	{
		TestTrue(TEXT("The second option is the selection"), Dropdown->IsValid() && (*Dropdown)->GetSelectedIndex() == 1);
		if (TestEqual(TEXT("...announced once"), Listener->SelectionIndices.Num(), 1))
		{
			TestEqual(TEXT("...naming the second option"), Listener->SelectionIndices[0], 1);
		}
		TestFalse(TEXT("Choosing closed the list"), Dropdown->IsValid() && (*Dropdown)->IsOpen());
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieListViewSmokeTest,
	"DreamGUI.Pie.ListView.ClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieListViewSmokeTest, "DreamGUI.Pie.ListView.ClickingTheThirdRowSelectsItWithOneSelectionChangeAndOneClick", "[Pointer][Animated]")

/* SObjectTableRow: a click on a row selects its item -- one selection change -- and reports one click. */
bool FDreamPieListViewSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamListsInteractionProbe> Probe(NewObject<UDreamListsInteractionProbe>());
	const TSharedRef<TWeakObjectPtr<UDreamListView>> List = MakeShared<TWeakObjectPtr<UDreamListView>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Probe, List](FDreamDriverPieRig& InRig)
	{
		UDreamListView* Made = InRig.MakeControl<UDreamListView>(TEXT("List"), nullptr, FVector2D(300.0, 400.0));
		if (Made != nullptr)
		{
			Made->SetStyleSource(EDreamUIStyleSource::Inline);
			Made->SetStyle(DreamListsInteraction::WithRows(Made->GetStyle(), 40.0f));
			Made->SetItemObjects(DreamListsInteraction::MakeItems(12));
			DreamListsInteraction::ListenToList(*Made, *Probe);
		}
		*List = Made;
	});
	const auto ThirdRow = Live([List]() -> UDreamWidget* { return List->IsValid() ? (*List)->GetRowWidget(2) : nullptr; }, TEXT("the list's third row"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Then([Probe](FDreamDriverContext&) { Probe->ClearRecords(); }).Click(ThirdRow);
	WaitFor(Steps, [Probe]() { return Probe->ClickedItems.Num() > 0; }, TEXT("the row's click to be announced"));
	Steps.Then([this, Probe, List](FDreamDriverContext&)
	{
		TestTrue(TEXT("The third item is the selection"), List->IsValid() && (*List)->GetSelectedIndex() == 2);
		if (TestEqual(TEXT("The selection changed once"), Probe->SelectionChanges.Num(), 1))
		{
			TestEqual(TEXT("...to the third item"), Probe->SelectionChanges[0], 2);
		}
		if (TestEqual(TEXT("One click was announced"), Probe->ClickedItems.Num(), 1))
		{
			TestEqual(TEXT("...for the third item"), Probe->ClickedItems[0], 2);
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieTabViewSmokeTest,
	"DreamGUI.Pie.TabView.ClickingTheThirdTabOpensItAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieTabViewSmokeTest, "DreamGUI.Pie.TabView.ClickingTheThirdTabOpensItAndSaysSoOnce", "[Pointer][Animated]")

/* A tab strip's tab is a button: the third clicked is the open one, and the switch is announced once. */
bool FDreamPieTabViewSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamTabView>> TabView = MakeShared<TWeakObjectPtr<UDreamTabView>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, TabView](FDreamDriverPieRig& InRig)
	{
		UDreamTabView* Made = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
		if (Made != nullptr)
		{
			Made->SetTabLabels({ FText::AsCultureInvariant(TEXT("Video")), FText::AsCultureInvariant(TEXT("Audio")), FText::AsCultureInvariant(TEXT("Input")) });
			Made->SetActiveTabIndex(0);
			Made->OnTabChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleTabChanged);
		}
		*TabView = Made;
	});
	const auto ThirdTab = Live([TabView]() -> UDreamWidget*
	{
		return TabView->IsValid() && (*TabView)->Tabs.IsValidIndex(2) ? (*TabView)->Tabs[2].TabNode.Get() : nullptr;
	}, TEXT("the third tab"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(ThirdTab);
	WaitFor(Steps, [Listener]() { return Listener->TabChangedIndices.Num() > 0; }, TEXT("the switch to be announced"));
	Steps.Then([this, Listener, TabView](FDreamDriverContext&)
	{
		TestTrue(TEXT("The third tab is the open one"), TabView->IsValid() && (*TabView)->GetActiveTabIndex() == 2);
		if (TestEqual(TEXT("The switch was announced once"), Listener->TabChangedIndices.Num(), 1))
		{
			TestEqual(TEXT("...naming the third tab"), Listener->TabChangedIndices[0], 2);
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieDialogSmokeTest,
	"DreamGUI.Pie.Dialog.ClickingTheConfirmButtonClosesTheDialogWithItsResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieDialogSmokeTest, "DreamGUI.Pie.Dialog.ClickingTheConfirmButtonClosesTheDialogWithItsResult", "[Pointer][Animated]")

/* The dialog's buttons answer it: the confirm button clicked closes it once, with that button's result. */
bool FDreamPieDialogSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamDialog>> Dialog = MakeShared<TWeakObjectPtr<UDreamDialog>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Dialog](FDreamDriverPieRig& InRig)
	{
		const FIntPoint Size = InRig.GetReport().ViewportSize;
		UDreamDialog* Made = InRig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr, FVector2D(Size.X, Size.Y));
		if (Made != nullptr)
		{
			Made->SetTitle(FText::AsCultureInvariant(TEXT("Delete the save?")));
			Made->OnDialogClosed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleDialogClosed);
		}
		*Dialog = Made;
	});
	const auto Confirm = Live([Dialog]() -> UDreamWidget*
	{
		if (!Dialog->IsValid())
		{
			return nullptr;
		}
		const TArray<FDreamDialogButton> Specs = (*Dialog)->GetButtons();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (Specs[Index].Result == FName(TEXT("Confirm")) && (*Dialog)->ButtonWidgets.IsValidIndex(Index))
			{
				return (*Dialog)->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}, TEXT("the dialog's confirm button"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Confirm);
	WaitFor(Steps, [Listener]() { return Listener->DialogClosedResults.Num() > 0; }, TEXT("the dialog to close"));
	Steps.Then([this, Listener](FDreamDriverContext&)
	{
		if (TestEqual(TEXT("The dialog closed once"), Listener->DialogClosedResults.Num(), 1))
		{
			TestEqual(TEXT("...with the confirm button's result"), Listener->DialogClosedResults[0], FName(TEXT("Confirm")));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieMenuAnchorSmokeTest,
	"DreamGUI.Pie.MenuAnchor.TheTriggersClickOpensTheMenuAndAPressFarFromItClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieMenuAnchorSmokeTest, "DreamGUI.Pie.MenuAnchor.TheTriggersClickOpensTheMenuAndAPressFarFromItClosesIt", "[Pointer][Animated]")

/* SMenuAnchor opened from a button's click (ShouldOpenDueToClick), and dismissed by a press outside it through the menu stack. */
bool FDreamPieMenuAnchorSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPieMenuTrigger> Trigger(NewObject<UDreamPieMenuTrigger>());
	const TSharedRef<TWeakObjectPtr<UDreamMenuAnchor>> Anchor = MakeShared<TWeakObjectPtr<UDreamMenuAnchor>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Trigger, Anchor](FDreamDriverPieRig& InRig)
	{
		const FVector2D Size(160.0, 40.0);
		const FVector2D Where(-200.0, 120.0);
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Trigger"), nullptr, Size, Where);
		UDreamMenuAnchor* Made = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, Size, Where);
		if (Button != nullptr && Made != nullptr && Made->MenuNode != nullptr)
		{
			InRig.MakeWidget(TEXT("MenuItem"), Made->MenuNode.Get(), FVector2D(160.0, 40.0));
			Trigger->Anchor = Made;
			Button->OnClicked.AddDynamic(Trigger.Get(), &UDreamPieMenuTrigger::HandleTriggerClicked);
		}
		*Anchor = Made;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Rig->Made(TEXT("Trigger")));
	WaitFor(Steps, [Anchor]() { return Anchor->IsValid() && (*Anchor)->IsOpen(); }, TEXT("the trigger's click to open the menu"));
	Steps.WaitFrames(2)
		.MoveToResolvedPixel([](FDreamDriverContext& InContext) { return FarCorner(InContext); }, TEXT("the far corner of the viewport"));
	PressAndRelease(Steps);
	WaitFor(Steps, [Anchor]() { return Anchor->IsValid() && !(*Anchor)->IsOpen(); }, TEXT("the press far from the menu to close it"));
	Steps.Then([this, Trigger](FDreamDriverContext&)
	{
		TestEqual(TEXT("The trigger was clicked once"), Trigger->TriggerClickedCount, 1);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieSpinBoxSmokeTest,
	"DreamGUI.Pie.SpinBox.ScrubbingThroughThePlayerControllerRaisesTheValueAndCommitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieSpinBoxSmokeTest, "DreamGUI.Pie.SpinBox.ScrubbingThroughThePlayerControllerRaisesTheValueAndCommitsOnce", "[Pointer][Animated]")

/* SSpinBox: a press dragged to the right past the drag distance raises the value, and letting go commits it once. */
bool FDreamPieSpinBoxSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamDragInteractionProbe> Commits(NewObject<UDreamDragInteractionProbe>());
	const TSharedRef<TWeakObjectPtr<UDreamSpinBox>> SpinBox = MakeShared<TWeakObjectPtr<UDreamSpinBox>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Commits, SpinBox](FDreamDriverPieRig& InRig)
	{
		UDreamSpinBox* Made = InRig.MakeControl<UDreamSpinBox>(TEXT("Amount"), nullptr, FVector2D(400.0, 40.0));
		if (Made != nullptr)
		{
			Made->SetMinValue(0.0f);
			Made->SetMaxValue(400.0f);
			Made->SetValue(100.0f);
			Made->OnValueCommitted.AddDynamic(Commits.Get(), &UDreamDragInteractionProbe::RecordFloat);
		}
		*SpinBox = Made;
	});
	const auto Field = Live([SpinBox]() -> UDreamWidget* { return SpinBox->IsValid() ? (*SpinBox)->FieldNode.Get() : nullptr; }, TEXT("the spin box's field"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.MoveTo(Field)
		.Press()
		.MoveBy(FVector2D(20.0, 0.0))
		.MoveBy(FVector2D(40.0, 0.0))
		.MoveBy(FVector2D(40.0, 0.0))
		.WaitFrames(1)
		.Release();
	WaitFor(Steps, [Commits]() { return Commits->NumFloats() > 0; }, TEXT("the scrub to commit"));
	Steps.Then([this, Commits, SpinBox](FDreamDriverContext&)
	{
		TestTrue(TEXT("The scrub raised the value"), SpinBox->IsValid() && (*SpinBox)->GetValue() > 100.5f);
		TestEqual(TEXT("...and letting go committed once"), Commits->NumFloats(), 1);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieExpandableAreaSmokeTest,
	"DreamGUI.Pie.ExpandableArea.AClickOnTheHeaderCollapsesTheAreaAndSaysSoOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieExpandableAreaSmokeTest, "DreamGUI.Pie.ExpandableArea.AClickOnTheHeaderCollapsesTheAreaAndSaysSoOnce", "[Pointer][Animated]")

/* SExpandableArea's header button flips it: expanded, a click collapses it, and OnExpansionChanged says so once. */
bool FDreamPieExpandableAreaSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamExpandableArea>> Area = MakeShared<TWeakObjectPtr<UDreamExpandableArea>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Area](FDreamDriverPieRig& InRig)
	{
		UDreamExpandableArea* Made = InRig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0));
		if (Made != nullptr && Made->ContentNode != nullptr)
		{
			InRig.MakeWidget(TEXT("Body"), Made->ContentNode.Get(), FVector2D(200.0, 60.0));
			Made->SetIsExpanded(true);
			Made->OnExpansionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleExpansionChanged);
		}
		*Area = Made;
	});
	const auto Header = Live([Area]() -> UDreamWidget* { return Area->IsValid() ? (*Area)->HeaderNode.Get() : nullptr; }, TEXT("the area's header"));
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Header);
	WaitFor(Steps, [Listener]() { return Listener->ExpansionStates.Num() > 0; }, TEXT("the area to announce its flip"));
	Steps.Then([this, Listener, Area](FDreamDriverContext&)
	{
		TestTrue(TEXT("The click on the header collapsed the area"), Area->IsValid() && !(*Area)->GetIsExpanded());
		if (TestEqual(TEXT("...and said so once"), Listener->ExpansionStates.Num(), 1))
		{
			TestFalse(TEXT("...as collapsed"), Listener->ExpansionStates[0]);
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieRingMenuSmokeTest,
	"DreamGUI.Pie.RingMenu.AClickInTheMiddleOfAWedgeChoosesAndActivatesThatWedgesItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieRingMenuSmokeTest, "DreamGUI.Pie.RingMenu.AClickInTheMiddleOfAWedgeChoosesAndActivatesThatWedgesItem", "[Pointer][Animated]")

/* A four-item wheel, open: a click halfway between the hub and the rim of the third wedge chooses and activates its item. */
bool FDreamPieRingMenuSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamRingMenu>> Wheel = MakeShared<TWeakObjectPtr<UDreamRingMenu>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Wheel](FDreamDriverPieRig& InRig)
	{
		UDreamRingMenu* Made = InRig.MakeControl<UDreamRingMenu>(TEXT("Wheel"), nullptr, FVector2D(OuterRadius * 2.0f, OuterRadius * 2.0f));
		if (Made == nullptr)
		{
			return;
		}
		Made->StyleSource = EDreamUIStyleSource::Inline;
		Made->Style.OuterRadius = OuterRadius;
		Made->Style.InnerRadius = InnerRadius;
		Made->Style.StartAngle = 0.0f;
		Made->Style.SweepAngle = 360.0f;
		Made->Style.HighlightGrowth = 0.0f;
		Made->Style.OpenDuration = 0.0f;
		Made->ApplyStyle();
		const auto Item = [](const TCHAR* InLabel)
		{
			FDreamRingMenuItem Entry;
			Entry.Label = FText::AsCultureInvariant(InLabel);
			Entry.Tag = FName(InLabel);
			return Entry;
		};
		Made->SetItems({ Item(TEXT("Reload")), Item(TEXT("Grenade")), Item(TEXT("Heal")), Item(TEXT("Melee")) });
		Made->Open();
		Made->OnSelectionChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleSelectionChanged);
		Made->OnItemActivated.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleItemActivated);
		*Wheel = Made;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	WaitFor(Steps, [Wheel]() { return Wheel->IsValid() && (*Wheel)->IsOpen() && (*Wheel)->WedgeNodes.Num() == 4; }, TEXT("the wheel to be open with four wedges"));
	Steps.MoveToResolvedPixel([Wheel](FDreamDriverContext&) -> TOptional<FVector2D>
		{
			return Wheel->IsValid() ? RingPixel((*Wheel)->GetWedgeWidget(2), (*Wheel)->GetItemMidAngle(2), (InnerRadius + OuterRadius) * 0.5f)
				: TOptional<FVector2D>();
		}, TEXT("the middle of the third wedge"));
	PressAndRelease(Steps);
	WaitFor(Steps, [Listener]() { return Listener->ActivatedTags.Num() > 0; }, TEXT("the wedge's item to be activated"));
	Steps.Then([this, Listener, Wheel](FDreamDriverContext&)
	{
		TestTrue(TEXT("The third item is chosen"), Wheel->IsValid() && (*Wheel)->GetSelectedIndex() == 2);
		if (TestEqual(TEXT("The item was activated once"), Listener->ActivatedTags.Num(), 1))
		{
			TestEqual(TEXT("...by its tag"), Listener->ActivatedTags[0], FName(TEXT("Heal")));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieInputKeySelectorSmokeTest,
	"DreamGUI.Pie.InputKeySelector.AClickArmsItAndTheNextKeyThroughSlateBecomesTheBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieInputKeySelectorSmokeTest, "DreamGUI.Pie.InputKeySelector.AClickArmsItAndTheNextKeyThroughSlateBecomesTheBinding", "[Pointer][Text][Animated]")

/*
 * SInputKeySelector: a click starts listening, and the next key pressed is the selection, announced once, the selector no
 * longer listening. The key is the platform's, through FSlateApplication to the session's viewport and the player
 * controller, where the armed selector's capture sits on top of the input stack.
 */
bool FDreamPieInputKeySelectorSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamInputKeySelector>> Selector = MakeShared<TWeakObjectPtr<UDreamInputKeySelector>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener, Selector](FDreamDriverPieRig& InRig)
	{
		UDreamInputKeySelector* Made = InRig.MakeControl<UDreamInputKeySelector>(TEXT("Jump"), nullptr, FVector2D(220.0, 60.0));
		if (Made != nullptr)
		{
			Made->OnKeySelected.AddDynamic(Listener.Get(), &UDreamTextInteractionListener::HandleKeySelected);
		}
		*Selector = Made;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Rig->Made(TEXT("Jump")));
	WaitFor(Steps, [Selector]() { return Selector->IsValid() && (*Selector)->GetIsListening(); }, TEXT("the click to arm the selector"));
	FDreamDriverPieRig::KeyThroughSlate(Steps, EKeys::F);
	WaitFor(Steps, [Listener]() { return Listener->KeySelectedCount > 0; }, TEXT("the key to be selected"));
	Steps.Then([this, Listener, Selector](FDreamDriverContext&)
	{
		TestTrue(TEXT("F is the new binding"), Selector->IsValid() && (*Selector)->GetSelectedKey() == EKeys::F);
		TestEqual(TEXT("...announced once"), Listener->KeySelectedCount, 1);
		TestFalse(TEXT("...and the selector has stopped listening"), Selector->IsValid() && (*Selector)->GetIsListening());
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieRichTextLinkSmokeTest,
	"DreamGUI.Pie.RichText.ClickingALinkAnnouncesItsIdOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieRichTextLinkSmokeTest, "DreamGUI.Pie.RichText.ClickingALinkAnnouncesItsIdOnce", "[Pointer][Animated]")

/*
 * SRichTextBlock's hyperlink decorator: a click on the link's text announces it, once, by its id. The link's pixel is read
 * off the laid-out glyphs once the paragraph has them, which in engine frames is whenever its font has delivered them.
 */
bool FDreamPieRichTextLinkSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	struct FLinkState
	{
		TWeakObjectPtr<UDreamText> Paragraph;
		TArray<FName> Ids;
	};
	const TSharedRef<FLinkState> State = MakeShared<FLinkState>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([State](FDreamDriverPieRig& InRig)
	{
		UDreamRichTextBlock* Block = InRig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), nullptr, FVector2D(480.0, 80.0));
		UDreamText* Paragraph = Block != nullptr && Block->TextNode != nullptr ? Cast<UDreamText>(Block->TextNode->GetVisual()) : nullptr;
		if (Paragraph == nullptr)
		{
			return;
		}
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(InRig.GetWorld());
		Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), TEXT("Roboto-Regular.ttf")), false);
		Font->InitFont();
		Paragraph->SetFont(Font);
		Paragraph->SetFontSize(32.0f);
		Block->SetText(FText::FromString(TEXT("a <a=x>link</a> b")));
		Block->TextNode->AddComponent<UUITextHyperlink>();
		Paragraph->OnHyperlinkClickedCPP.AddLambda([State](FName InId) { State->Ids.Add(InId); });
		State->Paragraph = Paragraph;
	});
	const auto LinkPixel = [State]() -> TOptional<FVector2D>
	{
		const UDreamText* Paragraph = State->Paragraph.Get();
		if (Paragraph == nullptr)
		{
			return TOptional<FVector2D>();
		}
		const TArray<FDreamUIText_RichTextCustomTag>& Tags = Paragraph->GetRichTextCustomTagArray();
		const int32 LinkIndex = Tags.IndexOfByPredicate([](const FDreamUIText_RichTextCustomTag& Tag) { return Tag.bHyperlink && Tag.TagName == FName(TEXT("x")); });
		return LinkIndex != INDEX_NONE ? CharactersPixel(*Paragraph, Tags[LinkIndex].CharIndexStart, Tags[LinkIndex].CharIndexEnd) : TOptional<FVector2D>();
	};
	FDreamDriverSequence Steps = Rig->Sequence();
	WaitFor(Steps, [LinkPixel]() { return LinkPixel().IsSet(); }, TEXT("the link's glyphs to be laid out"));
	Steps.MoveToResolvedPixel([LinkPixel](FDreamDriverContext&) { return LinkPixel(); }, TEXT("the middle of the link"));
	PressAndRelease(Steps);
	WaitFor(Steps, [State]() { return State->Ids.Num() > 0; }, TEXT("the link to be announced"));
	Steps.Then([this, State](FDreamDriverContext&)
	{
		if (TestEqual(TEXT("A click on the link announces it once"), State->Ids.Num(), 1))
		{
			TestEqual(TEXT("...by the id its markup gave it"), State->Ids[0], FName(TEXT("x")));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPieAddedToViewportSmokeTest,
	"DreamGUI.Pie.Button.MadeByTheCreateDreamWidgetNodeAndAddedToTheViewportItIsClickedThroughThePlayerControllerOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPieAddedToViewportSmokeTest, "DreamGUI.Pie.Button.MadeByTheCreateDreamWidgetNodeAndAddedToTheViewportItIsClickedThroughThePlayerControllerOnce", "[Pointer][Animated]")

/*
 * UMG's Create Widget for the player and AddToViewport, in the play session: the button made by the Create Dream Widget
 * node's calls for the session's player, added to the viewport and placed by hand, is on the player's screen and a click
 * through the player controller clicks it once.
 */
bool FDreamPieAddedToViewportSmokeTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieControlSmokeTestLocal;
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<TWeakObjectPtr<UDreamButton>> Button = MakeShared<TWeakObjectPtr<UDreamButton>>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, Listener, Button](FDreamDriverPieRig& InRig)
	{
		UDreamUserWidget* Begun = UDreamUIBPLibrary::BeginDeferredCreateDreamWidget(InRig.GetWorld(), UDreamButton::StaticClass(), InRig.GetPlayerController());
		UDreamButton* Made = Cast<UDreamButton>(Begun != nullptr ? UDreamUIBPLibrary::FinishDeferredCreateDreamWidget(Begun) : nullptr);
		if (!TestNotNull(TEXT("The node's calls made a button for the player"), Made))
		{
			return;
		}
		Made->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Made->AddToViewport(0);
		Made->SetAnchorsInViewport(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5));
		Made->SetDesiredSizeInViewport(FVector2D(200.0, 60.0));
		Made->SetPositionInViewport(FVector2D(150.0, -100.0));
		const UDreamScreenUISubsystem* Screens = UDreamScreenUISubsystem::Get(InRig.GetWorld());
		TestTrue(TEXT("Added, it is in the viewport"), Screens != nullptr && Screens->IsInViewport(Made));
		*Button = Made;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Click(Live([Button]() -> UDreamWidget* { return Button->Get(); }, TEXT("the button added to the viewport")));
	WaitFor(Steps, [Listener]() { return Listener->ClickedCount > 0; }, TEXT("the click on the button added to the viewport"));
	Steps.Then([this, Listener, Button](FDreamDriverContext& InContext)
	{
		TestEqual(TEXT("It was clicked once"), Listener->ClickedCount, 1);
		if (UDreamScreenUISubsystem* Screens = InContext.World != nullptr ? UDreamScreenUISubsystem::Get(InContext.World) : nullptr)
		{
			Screens->RemoveFromViewport(Button->Get());
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
