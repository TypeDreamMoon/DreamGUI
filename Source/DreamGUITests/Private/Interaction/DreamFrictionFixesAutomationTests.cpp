// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Text/DreamFontFaceResolver.h"
#include "Extensions/Effects/DreamStylizeEffect.h"
#include "Interaction/UIButton.h"

#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverRig.h"

/*
 * Things a user building two demo screens tripped over, each with nothing on the screen saying why: a press and a release
 * from code on one frame took no step, focus asked for on the frame a widget was shown was refused, a label laid over a
 * sibling took its clicks, a Chinese label on the default font, and a stylize layer's number that could only be changed
 * by building the whole list again.
 */
namespace DreamFrictionFixesTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** A button: a widget with a raycast-target visual and the behaviour, so navigation stops on it. */
	UDreamWidget* MakeButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition)
	{
		UDreamWidget* Button = InRig.MakeWidget(InName, nullptr, FVector2D(200.0, 60.0), InPosition);
		if (Button != nullptr)
		{
			Button->AddComponent<UUIButton>();
		}
		return Button;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTapNavigationTest,
	"DreamGUI.Navigation.ATapFromCodeTakesOneStepWherePressAndReleaseOnOneFrameTookNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTapNavigationTest, "DreamGUI.Navigation.ATapFromCodeTakesOneStepWherePressAndReleaseOnOneFrameTookNone", "[Nav]")

bool FDreamTapNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamFrictionFixesTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Top = MakeButton(Rig, TEXT("Top"), FVector2D(0.0, 120.0));
	UDreamWidget* Middle = MakeButton(Rig, TEXT("Middle"), FVector2D(0.0, 0.0));
	UDreamWidget* Bottom = MakeButton(Rig, TEXT("Bottom"), FVector2D(0.0, -120.0));
	Rig.PumpFrames(2);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	UDreamDriverInputModule* Module = Rig.InputModule();
	if (!TestNotNull(TEXT("The rig's world has input"), Services) || !TestNotNull(TEXT("and an input module"), Module)
		|| !TestTrue(TEXT("The pad's focus goes onto the top button"), Services->FocusForNavigation(Top, 0)))
	{
		return false;
	}

	Module->TapNavigation(EDreamUINavigationDirection::Down, 0);
	Rig.PumpFrames(2);
	TestTrue(TEXT("One tap down steps onto the middle button"), Services->GetFocusedWidget(0) == Middle);

	// Two taps in one frame are two steps, one a frame, and nothing more however long it runs on.
	Module->TapNavigation(EDreamUINavigationDirection::Up, 0);
	Module->TapNavigation(EDreamUINavigationDirection::Down, 0);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The first of two taps is taken on the first frame"), Services->GetFocusedWidget(0) == Top);
	Rig.PumpFrames(1);
	TestTrue(TEXT("and the second on the next"), Services->GetFocusedWidget(0) == Middle);
	Rig.PumpFrames(60);
	TestTrue(TEXT("and no more after them"), Services->GetFocusedWidget(0) == Middle);
	TestNotNull(TEXT("The bottom button is there to step onto"), Bottom);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFocusOnTheNextFrameTest,
	"DreamGUI.Focus.FocusAskedForBeforeAWidgetCanTakeItArrivesOnTheNextFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamFocusOnTheNextFrameTest, "DreamGUI.Focus.FocusAskedForBeforeAWidgetCanTakeItArrivesOnTheNextFrame", "[Nav]")

bool FDreamFocusOnTheNextFrameTest::RunTest(const FString& Parameters)
{
	using namespace DreamFrictionFixesTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* First = MakeButton(Rig, TEXT("First"), FVector2D(0.0, 60.0));
	UDreamWidget* Second = MakeButton(Rig, TEXT("Second"), FVector2D(0.0, -60.0));
	Rig.PumpFrames(2);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The rig's world has input"), Services) || !TestNotNull(TEXT("Two buttons"), First) || !TestNotNull(TEXT("..."), Second))
	{
		return false;
	}

	// Shown and asked for focus on one frame, the way a screen's construct does it: the hierarchy does not yet say the
	// widget is visible, and the answer is no -- but the focus arrives on the next frame.
	Second->SetVisibility(EDreamWidgetVisibility::Hidden);
	Rig.PumpFrames(1);
	Second->SetVisibility(EDreamWidgetVisibility::Visible);
	const bool bAtOnce = Second->SetFocus(0);
	Rig.PumpFrames(2);
	TestTrue(FString::Printf(TEXT("The focus is on the widget a frame later (given at once: %s)"), bAtOnce ? TEXT("yes") : TEXT("no")),
		Services->GetFocusedWidget(0) == Second);

	// A later request wins: hidden again, asked for, and then focus given elsewhere before the next frame.
	Second->SetVisibility(EDreamWidgetVisibility::Hidden);
	Rig.PumpFrames(1);
	if (!Second->SetFocus(0))
	{
		TestTrue(TEXT("Focus can be given to the other button"), Services->FocusForNavigation(First, 0));
		Second->SetVisibility(EDreamWidgetVisibility::Visible);
		Rig.PumpFrames(2);
		TestTrue(TEXT("A request still waiting does not take the focus back from where it moved"), Services->GetFocusedWidget(0) == First);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextIsNoRaycastTargetByDefaultTest,
	"DreamGUI.Text.ATextIsNoRaycastTargetUnlessItSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextIsNoRaycastTargetByDefaultTest::RunTest(const FString& Parameters)
{
	// UMG's TextBlock is not hit-testable either: a label laid over its sibling no longer takes the sibling's pointer.
	const UDreamText* Default = GetDefault<UDreamText>();
	TestFalse(TEXT("The text's default is not a raycast target"), Default->GetRaycastTarget());
	UDreamText* Text = NewObject<UDreamText>();
	TestFalse(TEXT("A new text is not one"), Text->GetRaycastTarget());
	Text->SetRaycastTarget(true);
	TestTrue(TEXT("and one that says so is"), Text->GetRaycastTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDefaultFontDrawsChineseTest,
	"DreamGUI.Text.TheDefaultFontDrawsChineseThroughItsFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDefaultFontDrawsChineseTest::RunTest(const FString& Parameters)
{
	UDreamUIFontData_BaseObject* Font = UDreamUIFontData_BaseObject::GetDefaultFont();
	if (!TestNotNull(TEXT("There is a default font"), Font))
	{
		return false;
	}
	// In an English game too: the CJK face is meant for Chinese, Japanese and Korean, and is still the face that draws an
	// ideograph in a text of another language rather than a box.
	for (const TCHAR* Culture : { TEXT("en"), TEXT("zh-Hans"), TEXT("ja") })
	{
		const uint32 Ideograph = 0x4E2D; // 中
		FDreamFontFaceQuery Query;
		Query.Cluster = TConstArrayView<uint32>(&Ideograph, 1);
		const FDreamTextLanguage Language = FDreamTextLanguage::Make(Culture);
		Query.Cultures = Language.PrioritizedCultureNames;
		const FDreamFontFaceChoice Choice = FDreamFontFaceResolver::Resolve(Font, Query);
		TestTrue(FString::Printf(TEXT("A face of the default font has the ideograph, in a %s text"), Culture), Choice.bCoversBase);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStylizeLayerParameterTest,
	"DreamGUI.Stylize.OneNumberOfOneLayerIsSetByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStylizeLayerParameterTest::RunTest(const FString& Parameters)
{
	UDreamStylizeEffect* Effect = NewObject<UDreamStylizeEffect>();
	TArray<FDreamStylizeLayer> Layers;
	FDreamStylizeLayer& Aberration = Layers.AddDefaulted_GetRef();
	Aberration.Type = EDreamStylizeType::ChromaticAberration;
	FDreamStylizeLayer& Crt = Layers.AddDefaulted_GetRef();
	Crt.Type = EDreamStylizeType::CRT;
	Effect->SetLayers(Layers);

	TestTrue(TEXT("A layer's number is set by its name"), Effect->SetLayerParameter(0, TEXT("AberrationAmount"), 7.5f));
	float Read = 0.0f;
	TestTrue(TEXT("and read back"), Effect->GetLayerParameter(0, TEXT("AberrationAmount"), Read));
	TestNearlyEqual(TEXT("as it was set"), Read, 7.5f, 0.0001f);
	TestNearlyEqual(TEXT("on that layer, in the list"), Effect->GetLayers()[0].AberrationAmount, 7.5f, 0.0001f);
	TestTrue(TEXT("A number of the details panel's range is held to it"), Effect->SetLayerParameter(1, TEXT("ScanlineStrength"), 5.0f));
	TestNearlyEqual(TEXT("a 0..1 strength stops at 1"), Effect->GetLayers()[1].ScanlineStrength, 1.0f, 0.0001f);
	TestTrue(TEXT("An on/off number is set too"), Effect->SetLayerParameter(1, TEXT("bEnabled"), 0.0f));
	TestFalse(TEXT("off"), Effect->GetLayers()[1].bEnabled);
	TestFalse(TEXT("A layer that is not there is refused"), Effect->SetLayerParameter(2, TEXT("AberrationAmount"), 1.0f));
	TestFalse(TEXT("and a name that is no number of a layer"), Effect->SetLayerParameter(0, TEXT("NotAParameter"), 1.0f));
	return true;
}

#endif
