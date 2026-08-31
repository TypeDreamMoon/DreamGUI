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
	// The box is an image (the rect-block face is parked on a render defect), so the Auto slot reads
	// its size off the brush.
	UDreamImage* BoxImage = Cast<UDreamImage>(Toggle->BoxNode->GetVisual());
	UDreamText* TickText = Cast<UDreamText>(Toggle->TickNode->GetVisual());
	if (TestNotNull(TEXT("the box is drawn by an image"), BoxImage) && TestNotNull(TEXT("the tick is a glyph"), TickText))
	{
		TestEqual(TEXT("the box states the style's width to the layout"), BoxImage->GetPreferredWidth(), 40.0f);
		TestEqual(TEXT("and its height"), BoxImage->GetPreferredHeight(), 18.0f);
		TestEqual(TEXT("the glyph is sized by the style's tick height"), TickText->GetFontSize(), 12.0f);
	}

	if (UDreamText* LabelVisual = Cast<UDreamText>(Toggle->LabelNode->GetVisual()))
	{
		TestEqual(TEXT("the label says what was authored"), LabelVisual->GetText().ToString(), FString(TEXT("muted")));
	}

	// bIsOn is a property, not just a getter/setter pair, precisely so an authored value like this
	// one can arrive at all -- and it has to reach the behaviour, not merely sit on the control.
	TestTrue(TEXT("the authored value reached the behaviour"), Toggle->GetIsOn());

	// The checked colour goes through the immediate path, so the tick actually carries it. A tween
	// would not run here (no world), which is why this is the assertion worth making.
	if (UDreamVisual* TickVisual = Toggle->TickNode->GetVisual())
	{
		TestEqual(TEXT("the tick is wearing the checked colour"), TickVisual->GetColor(), FColor(11, 22, 33, 255));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
