// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/UISlider.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The control library: four code-built controls, one test each, every test aimed at the wiring that
 * fails SILENTLY. A missing behaviour logs nothing (BP_Button proved it for months); a transition
 * aimed at the wrong visual shows up only as a checked state that dies on the next hover; a part
 * the behaviour was never handed just means the behaviour's early-outs run forever. None of that is
 * visible in a screenshot, which is why each is an assertion here.
 *
 * The toggle has its own suite (DreamToggleControlAutomationTests); these are the other four.
 * Everything runs headless: no world, no registration, no layout pass -- the claims are about what
 * NativeOnInitialized wired, not about pixels.
 */
namespace DreamControlLibraryTestLocal
{
	template<class T>
	T* Make()
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->Initialize();
		return Control;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlButtonTest,
	"DreamGUI.Controls.Button.AlwaysHasItsBehaviourAndTintsItsOwnFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlLibraryTestLocal;
	TStrongObjectPtr<UDreamButton> Button(Make<UDreamButton>());

	// The BP_Button failure, made structurally impossible.
	if (!TestNotNull(TEXT("the behaviour is always there"), Button->ButtonBehaviour.Get()))
	{
		return false;
	}
	TestNotNull(TEXT("the face draws"), Button->FaceNode != nullptr ? Button->FaceNode->GetVisual() : nullptr);
	TestTrue(TEXT("the pointer transition tints the face it stands on"),
		(UObject*)Button->ButtonBehaviour->GetTransitionTarget()
			== (UObject*)(Button->FaceNode != nullptr ? Button->FaceNode->GetVisual() : nullptr));

	// The style reached the parts: the resolved normal colour is on the behaviour.
	TestEqual(TEXT("the style's normal colour arrived"),
		Button->ButtonBehaviour->GetNormalColor(), FDreamButtonStyle().Normal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlSliderTest,
	"DreamGUI.Controls.Slider.ItsPartsReachTheBehaviourAndOneClassServesBothAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlSliderTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlLibraryTestLocal;

	// Horizontal: authored range and value arrive clamped and without an event.
	{
		TStrongObjectPtr<UDreamSlider> Slider(NewObject<UDreamSlider>(GetTransientPackage()));
		Slider->MinValue = 0.0f;
		Slider->MaxValue = 10.0f;
		Slider->Value = 2.5f;
		Slider->Initialize();

		if (!TestNotNull(TEXT("the behaviour is always there"), Slider->SliderBehaviour.Get()))
		{
			return false;
		}
		// The behaviour reads each part's PARENT as the space it works in; being handed the parts is
		// the entire mechanism. Un-handed, every drag is a silent no-op.
		TestTrue(TEXT("the fill was handed over"),
			(UObject*)Slider->SliderBehaviour->GetFill() == (UObject*)Slider->FillNode.Get());
		TestTrue(TEXT("the handle was handed over"),
			(UObject*)Slider->SliderBehaviour->GetHandle() == (UObject*)Slider->HandleNode.Get());
		TestTrue(TEXT("the pointer transition rides the handle"),
			(UObject*)Slider->SliderBehaviour->GetTransitionTarget()
				== (UObject*)(Slider->HandleNode != nullptr ? Slider->HandleNode->GetVisual() : nullptr));
		TestEqual(TEXT("the authored range arrived"), Slider->SliderBehaviour->GetMaxValue(), 10.0f);
		TestEqual(TEXT("the authored value arrived"), Slider->GetValue(), 2.5f);
		// Anchor-driven geometry: the track's thickness is its SizeDelta, readable with no layout pass.
		TestEqual(TEXT("the track is as thick as the style says"),
			Slider->TrackNode->GetHeight(), FDreamSliderStyle().TrackThickness);
	}

	// Vertical: the same class, one property -- where the presets needed a second asset.
	{
		TStrongObjectPtr<UDreamSlider> Slider(NewObject<UDreamSlider>(GetTransientPackage()));
		Slider->Direction = EUISliderDirectionType::BottomToTop;
		Slider->Initialize();
		TestEqual(TEXT("the direction arrived"),
			Slider->SliderBehaviour->GetDirectionType(), EUISliderDirectionType::BottomToTop);
		TestEqual(TEXT("and the thickness moved to the other axis"),
			Slider->TrackNode->GetWidth(), FDreamSliderStyle().TrackThickness);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlTextInputTest,
	"DreamGUI.Controls.TextInput.TheFieldGetsItsTextItsPlaceholderAndItsClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlTextInputTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlLibraryTestLocal;

	TStrongObjectPtr<UDreamTextInput> Input(NewObject<UDreamTextInput>(GetTransientPackage()));
	Input->Text = TEXT("hello");
	Input->bMultiLine = true;
	Input->Initialize();

	if (!TestNotNull(TEXT("the behaviour is always there"), Input->InputBehaviour.Get()))
	{
		return false;
	}
	// The behaviour edits THROUGH the text visual; without it, typing goes nowhere and says nothing.
	TestTrue(TEXT("the text visual was handed over"),
		(UObject*)Input->InputBehaviour->GetTextComponent()
			== (UObject*)(Input->TextNode != nullptr ? Input->TextNode->GetVisual() : nullptr));
	TestTrue(TEXT("the placeholder was handed over"),
		(UObject*)Input->InputBehaviour->GetPlaceHolderActor() == (UObject*)Input->PlaceholderNode.Get());
	TestEqual(TEXT("the authored text arrived"), Input->GetText(), FString(TEXT("hello")));
	TestTrue(TEXT("one property serves both line modes"), Input->InputBehaviour->GetAllowMultiLine());
	// The one structural fact of a text field: its content overflows, and the overflow is clipped.
	TestEqual(TEXT("the clip area clips"),
		Input->ClipNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);

	// UMG's event spelling, OnTextCommitted, rides the same submit as OnSubmitted. Dynamic
	// delegates need a UFUNCTION to land on and a test cpp cannot declare a UCLASS of its own, so
	// a second, uninitialized input is the listener: its SetText is signature-compatible and, with
	// no behaviour underneath, just stores what the event carried. Driving the behaviour's native
	// submit delegate is not simulating Enter -- it is the seam the control subscribed to,
	// exercised directly, which is all a headless test may claim.
	TStrongObjectPtr<UDreamTextInput> CommitProbe(NewObject<UDreamTextInput>(GetTransientPackage()));
	Input->OnTextCommitted.AddDynamic(CommitProbe.Get(), &UDreamTextInput::SetText);
	Input->InputBehaviour->GetOnSubmitEvent().Broadcast(FString(TEXT("committed")));
	TestEqual(TEXT("OnTextCommitted fired with the submitted string"), CommitProbe->Text, FString(TEXT("committed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlDropdownTest,
	"DreamGUI.Controls.Dropdown.TheListTheTemplateAndTheCaptionAllReachTheBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlDropdownTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlLibraryTestLocal;

	TStrongObjectPtr<UDreamDropdown> Dropdown(NewObject<UDreamDropdown>(GetTransientPackage()));
	Dropdown->Options = { FText::FromString(TEXT("Low")), FText::FromString(TEXT("High")) };
	Dropdown->SelectedIndex = 1;
	Dropdown->Initialize();

	if (!TestNotNull(TEXT("the behaviour is always there"), Dropdown->DropdownBehaviour.Get()))
	{
		return false;
	}
	TestTrue(TEXT("the list root was handed over"),
		(UObject*)Dropdown->DropdownBehaviour->GetListRoot() == (UObject*)Dropdown->ListNode.Get());
	TestEqual(TEXT("the options arrived, as data"), Dropdown->DropdownBehaviour->GetOptions().Num(), 2);
	TestEqual(TEXT("the authored selection arrived"), Dropdown->GetSelectedIndex(), 1);
	// The caption shows the selection the moment the parts are wired -- ApplyValueToVisual ran.
	if (UDreamText* Caption = Cast<UDreamText>(Dropdown->CaptionNode != nullptr ? Dropdown->CaptionNode->GetVisual() : nullptr))
	{
		TestEqual(TEXT("the caption wears the selected option"),
			Caption->GetText().ToString(), FString(TEXT("High")));
	}

	// The row template: asleep, and carrying the library's recurring two-transition split.
	if (TestNotNull(TEXT("the template exists"), Dropdown->ItemTemplateNode.Get()))
	{
		TestFalse(TEXT("the template is not a row"), Dropdown->ItemTemplateNode->GetWidgetActive());
		if (UUIToggle* RowToggle = Dropdown->ItemTemplateNode->GetComponent<UUIToggle>())
		{
			UDreamVisual* Hover = RowToggle->GetTransitionTarget();
			UDreamVisual* Check = RowToggle->GetToggleTransitionTarget();
			TestNotNull(TEXT("the row's hover has a target"), Hover);
			TestNotNull(TEXT("the row's check has a target"), Check);
			TestTrue(TEXT("and they are two visuals, not one"), (UObject*)Hover != (UObject*)Check);
		}
	}
	TestFalse(TEXT("the list starts asleep"), Dropdown->ListNode->GetWidgetActive());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
