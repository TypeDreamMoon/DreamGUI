// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIToggle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The first control whose hierarchy is code rather than an asset.
 *
 * The claim under test is not "it builds a tree" -- DreamUIBuilder's own tests cover that -- but the
 * one thing a toggle gets wrong more than any other, and the one the preset Blueprints exist to
 * work around:
 *
 *   A toggle owns TWO transitions. The selectable one moves normal/hovered/pressed, and the checked
 *   one moves on/off. Aimed at the same visual they overwrite each other, and the symptom is a
 *   checked state that survives exactly until the pointer next enters the widget -- which nobody
 *   notices in a screenshot. So the assertion here is that the two land on DIFFERENT visuals, and
 *   specifically the two this control means: the box for the pointer, the tick for the value.
 *
 * That wiring is also what the builder's deferred pass exists for: both targets are nodes BELOW the
 * one carrying the behaviour, so neither exists at the moment the root is constructed. A .Then that
 * ran inline would leave both null, and UUIToggle::ApplyValueToVisual returns early on a null target
 * -- silently, which is how this defect survives review.
 *
 * No world. UDreamUserWidget::Initialize needs one only if the class carries property bindings, and
 * a control assembled in C++ carries none; the tween manager returns null without one, which is why
 * the colour assertions below go through the immediate path (SetOnColor applies at once) rather than
 * through a value change.
 */
namespace DreamToggleControlTestLocal
{
	UDreamToggle* MakeToggle()
	{
		UDreamToggle* Toggle = NewObject<UDreamToggle>(GetTransientPackage());
		Toggle->Initialize();
		return Toggle;
	}

	UDreamWidget* FindChildNamed(const UDreamWidget* InParent, const TCHAR* InName)
	{
		if (InParent == nullptr)
		{
			return nullptr;
		}
		for (UDreamWidget* Child : InParent->GetChildren())
		{
			if (Child != nullptr && Child->GetDisplayName() == InName)
			{
				return Child;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleControlShapeTest,
	"DreamGUI.Controls.Toggle.BuildsItsOwnPartsWithNoAssetBehindIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleControlShapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleControlTestLocal;

	TStrongObjectPtr<UDreamToggle> Toggle(MakeToggle());

	// The class has no archetype, so InitializeWidgetStatic made no tree. Everything below exists
	// because NativeOnInitialized built it.
	if (!TestNotNull(TEXT("a tree was made for a class with no asset"), Toggle->WidgetTree.Get()))
	{
		return false;
	}
	UDreamWidget* Root = Toggle->WidgetTree->RootWidget;
	if (!TestNotNull(TEXT("and it has a root"), Root))
	{
		return false;
	}
	TestTrue(TEXT("the root hangs under the control"), (UObject*)Root->GetParent() == (UObject*)Toggle.Get());

	// A control that always adds its own behaviour cannot ship without one, which is the failure
	// BP_Button had for months with nothing to say so.
	TestNotNull(TEXT("the toggle behaviour is always there"), Toggle->ToggleBehaviour.Get());

	// The parts, by name and by nesting: the tick is INSIDE the box, not beside it.
	TestNotNull(TEXT("the box is a child of the root"), FindChildNamed(Root, TEXT("Box")));
	TestNotNull(TEXT("the label is a child of the root"), FindChildNamed(Root, TEXT("Label")));
	TestNotNull(TEXT("the tick is a child of the box"), FindChildNamed(Toggle->BoxNode, TEXT("Tick")));
	TestTrue(TEXT("BoxNode names the box"), (UObject*)Toggle->BoxNode.Get() == (UObject*)FindChildNamed(Root, TEXT("Box")));
	TestTrue(TEXT("TickNode names the tick"), (UObject*)Toggle->TickNode.Get() == (UObject*)FindChildNamed(Toggle->BoxNode, TEXT("Tick")));

	TestNotNull(TEXT("the label is drawn by text"), Cast<UDreamText>(Toggle->LabelNode != nullptr ? Toggle->LabelNode->GetVisual() : nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleControlTransitionTest,
	"DreamGUI.Controls.Toggle.ItsTwoTransitionsLandOnTwoVisuals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleControlTransitionTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleControlTestLocal;

	TStrongObjectPtr<UDreamToggle> Toggle(MakeToggle());

	UUIToggle* Behaviour = Toggle->ToggleBehaviour;
	if (!TestNotNull(TEXT("the toggle behaviour exists"), Behaviour))
	{
		return false;
	}

	UDreamVisual* Hover = Behaviour->GetTransitionTarget();
	UDreamVisual* Checked = Behaviour->GetToggleTransitionTarget();

	// Both were named from a .Then on the root, and both point at nodes built after it. Null here is
	// the shape a deferred pass that is not deferred produces.
	TestNotNull(TEXT("the pointer transition has a target"), Hover);
	TestNotNull(TEXT("the checked transition has a target"), Checked);

	// The assertion the control exists for.
	TestTrue(TEXT("they are not the same visual"), (UObject*)Hover != (UObject*)Checked);
	TestTrue(TEXT("the pointer transition tints the box"),
		(UObject*)Hover == (UObject*)(Toggle->BoxNode != nullptr ? Toggle->BoxNode->GetVisual() : nullptr));
	TestTrue(TEXT("the checked transition tints the tick"),
		(UObject*)Checked == (UObject*)(Toggle->TickNode != nullptr ? Toggle->TickNode->GetVisual() : nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleControlStyleTest,
	"DreamGUI.Controls.Toggle.TheStyleIsTheOnlyWayItsLookIsDecided",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleControlStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleControlTestLocal;

	// Authored before initialization, the way a .dui line or a details panel would leave it.
	TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
	Toggle->Style.BoxSize = FVector2D(40.0, 18.0);
	Toggle->Style.TickSize = FVector2D(12.0, 12.0);
	Toggle->Style.TickChecked = FColor(11, 22, 33, 255);
	Toggle->Label = FText::FromString(TEXT("muted"));
	Toggle->bIsOn = true;
	Toggle->Initialize();

	if (!TestNotNull(TEXT("the box exists"), Toggle->BoxNode.Get()))
	{
		return false;
	}

	// A code-built control has no tree for anyone to open, so the style has to be the whole of what
	// an author can decide. If any of these does not arrive, that decision was silently ignored.
	// The box is a procedural rect, which states no intrinsic size: authored width/height feed the
	// Auto slot's desired-size fallback, and before any arrange pass they read straight back.
	UDreamRectBlock* BoxRect = Cast<UDreamRectBlock>(Toggle->BoxNode->GetVisual());
	UDreamText* TickText = Cast<UDreamText>(Toggle->TickNode->GetVisual());
	if (TestNotNull(TEXT("the box is a rect block"), BoxRect) && TestNotNull(TEXT("the tick is a glyph"), TickText))
	{
		TestEqual(TEXT("the box took the style's width"), Toggle->BoxNode->GetWidth(), 40.0f);
		TestEqual(TEXT("and its height"), Toggle->BoxNode->GetHeight(), 18.0f);
		TestEqual(TEXT("the glyph is sized by the style's tick height"), TickText->GetFontSize(), 12.0f);
	}

	if (UDreamText* LabelVisual = Cast<UDreamText>(Toggle->LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the label says what was authored"), LabelVisual->GetText().ToString(), FString(TEXT("muted")));
	}

	// bIsOn is a property, not just a getter/setter pair, precisely so an authored value like this
	// one can arrive at all -- and it has to reach the behaviour, not merely sit on the control.
	TestTrue(TEXT("the authored value reached the behaviour"), Toggle->GetIsOn());

	// Only the legacy spelling was authored, so the reconciliation must side with it: this is the
	// path every existing .dui (`bIsOn = true`, `bIsOn <-> ...`) still takes. CheckedState wins
	// only when IT says something non-default.
	TestEqual(TEXT("and the UMG spelling mirrors it"), Toggle->GetCheckedState(), EDreamCheckState::Checked);

	// The checked colour goes through the immediate path, so the tick actually carries it. A tween
	// would not run here (no world), which is why this is the assertion worth making.
	if (UDreamVisual* TickVisual = Toggle->TickNode->GetVisual())
	{
		TestEqual(TEXT("the tick is wearing the checked colour"), TickVisual->GetColor(), FColor(11, 22, 33, 255));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleControlTriStateTest,
	"DreamGUI.Controls.Toggle.TheThirdStateIsAuthorableAndLeavesByBecomingChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleControlTriStateTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleControlTestLocal;

	// Authored Undetermined, the way a .dui line or a details panel would leave it. Distinct tick
	// colours, so which one the bar wears is an assertion rather than a coincidence.
	TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
	Toggle->CheckedState = EDreamCheckState::Undetermined;
	Toggle->Style.TickChecked = FColor(11, 22, 33, 255);
	Toggle->Style.TickUnchecked = FColor(44, 55, 66, 255);
	Toggle->Initialize();

	if (!TestNotNull(TEXT("the toggle behaviour exists"), Toggle->ToggleBehaviour.Get()))
	{
		return false;
	}
	UDreamText* TickText = Cast<UDreamText>(Toggle->TickNode != nullptr ? Toggle->TickNode->GetVisual() : nullptr);
	if (!TestNotNull(TEXT("the tick is a glyph"), TickText))
	{
		return false;
	}

	// The bar, in the CHECKED colour. The behaviour underneath is two-state and parked at
	// unchecked, so left alone the glyph would wear TickUnchecked; the control aims the off colour
	// at TickChecked for exactly as long as the third state stands, and it must land through the
	// immediate path because no tween runs here.
	TestEqual(TEXT("the tick shows the bar"), TickText->GetText().ToString(), FString(TEXT("—")));
	TestEqual(TEXT("and wears the checked colour"), TickText->GetColor(), FColor(11, 22, 33, 255));

	// Every spelling of the state agrees that Undetermined projects to false.
	TestEqual(TEXT("GetCheckedState says Undetermined"), Toggle->GetCheckedState(), EDreamCheckState::Undetermined);
	TestFalse(TEXT("IsChecked projects it to false"), Toggle->IsChecked());
	TestFalse(TEXT("the compatibility getter agrees"), Toggle->GetIsOn());
	TestFalse(TEXT("the compatibility property agrees"), Toggle->bIsOn);
	TestFalse(TEXT("the behaviour is parked at unchecked"), Toggle->ToggleBehaviour->GetValue());

	// Dynamic delegates need a UFUNCTION to land on, and a test cpp cannot declare a UCLASS of its
	// own -- but another instance of the control IS one. An uninitialized probe has no behaviour,
	// so its setters just store what the event carried, readable afterwards and silent otherwise.
	TStrongObjectPtr<UDreamToggle> StateProbe(NewObject<UDreamToggle>(GetTransientPackage()));
	TStrongObjectPtr<UDreamToggle> BoolProbe(NewObject<UDreamToggle>(GetTransientPackage()));
	Toggle->OnCheckStateChanged.AddDynamic(StateProbe.Get(), &UDreamToggle::SetCheckedState);
	Toggle->OnToggleChanged.AddDynamic(BoolProbe.Get(), &UDreamToggle::SetIsOn);

	// Programmatic set to Checked: the glyph flips back to the check mark, and BOTH events fire --
	// the UMG spelling with the state, the compatibility spelling with the bool.
	Toggle->SetCheckedState(EDreamCheckState::Checked);
	TestEqual(TEXT("the glyph is the check mark again"), TickText->GetText().ToString(), FString(TEXT("✓")));
	TestEqual(TEXT("the state moved"), Toggle->GetCheckedState(), EDreamCheckState::Checked);
	TestTrue(TEXT("the behaviour moved with it"), Toggle->ToggleBehaviour->GetValue());
	TestTrue(TEXT("the compatibility property mirrors it"), Toggle->bIsOn);
	TestEqual(TEXT("OnCheckStateChanged fired, carrying Checked"), StateProbe->CheckedState, EDreamCheckState::Checked);
	TestTrue(TEXT("OnToggleChanged fired, carrying true"), BoolProbe->bIsOn);

	// A behaviour-driven change -- the exact shape a click has, value first, callback after --
	// comes back up: the control translates it and re-broadcasts on both spellings.
	Toggle->ToggleBehaviour->SetValue(false);
	TestEqual(TEXT("the control translated the behaviour's change"), Toggle->GetCheckedState(), EDreamCheckState::Unchecked);
	TestFalse(TEXT("the compatibility property followed"), Toggle->bIsOn);
	TestEqual(TEXT("OnCheckStateChanged fired, carrying Unchecked"), StateProbe->CheckedState, EDreamCheckState::Unchecked);
	TestFalse(TEXT("OnToggleChanged fired, carrying false"), BoolProbe->bIsOn);

	// Back to Undetermined from Unchecked: the tri-state event fires, the bool one stays silent,
	// because false to false is not a change on that spelling. The probe is pre-loaded with true
	// so the silence is observable.
	BoolProbe->bIsOn = true;
	Toggle->SetCheckedState(EDreamCheckState::Undetermined);
	TestEqual(TEXT("the bar is back"), TickText->GetText().ToString(), FString(TEXT("—")));
	TestEqual(TEXT("wearing the checked colour again"), TickText->GetColor(), FColor(11, 22, 33, 255));
	TestEqual(TEXT("OnCheckStateChanged fired, carrying Undetermined"), StateProbe->CheckedState, EDreamCheckState::Undetermined);
	TestTrue(TEXT("OnToggleChanged stayed silent"), BoolProbe->bIsOn);
	TestFalse(TEXT("underneath, a state no click can reach still reads unchecked"), Toggle->ToggleBehaviour->GetValue());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
