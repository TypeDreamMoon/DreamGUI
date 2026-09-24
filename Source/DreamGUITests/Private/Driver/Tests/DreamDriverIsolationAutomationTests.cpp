// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"

/*
 * A RIG LEAVES THE PROCESS AS IT FOUND IT.
 *
 * Some of DreamGUI's state is not in any world: a class-wide switch saying a host delivers characters,
 * the field that owns the keyboard, the depth of the layout pass and of the desired-size memo, the
 * editor's "a Blueprint is compiling" flag. Each outlives every world, so each outlives every test,
 * and a test that moved one decides how the tests after it behave -- in the order the runner happens
 * to pick. The character switch is the sharpest case: it flips for good on the first character a host
 * delivers, and from then on the key-to-character fallback, the road DevTest itself is on, can no
 * longer be reached in that process.
 *
 * The rig puts the switch back, makes sure the keyboard's owner is nothing of its own, and checks the
 * counters are settled when it goes. These pin both halves: the restore happens, and the check says
 * what it would say.
 */
namespace DreamDriverIsolationTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Put the switch back to what it was when the test began, whatever the test did in between. */
	struct FScopedCharacterSwitch
	{
		bool bOriginal = UUITextInput::IsHostDeliveringCharacterEvents();
		~FScopedCharacterSwitch() { UUITextInput::SetHostDeliversCharacterEventsForTesting(bOriginal); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverIsolationCharacterSwitchTest,
	"DreamGUI.Driver.Isolation.ARigPutsTheCharacterDeliverySwitchBackTheWayItFoundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverIsolationCharacterSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverIsolationTestLocal;
	FScopedCharacterSwitch RestoreAtTheEnd;

	// Off going in: the state of a process no host has typed into yet.
	UUITextInput::SetHostDeliversCharacterEventsForTesting(false);
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0));
		if (!TestNotNull(TEXT("A text field can be made"), Field))
		{
			return false;
		}
		Rig.PumpFrames(1);
		TestTrue(TEXT("Typing a character completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")))->Type(TEXT("a")));
		// The driver types through HandleCharacterInput, the host's road, and that road flips the
		// switch for the whole process. This is the flip the rig has to undo.
		TestTrue(TEXT("Typing through the host's road turned the switch on"), UUITextInput::IsHostDeliveringCharacterEvents());
	}
	TestFalse(TEXT("Once the rig is gone the switch is off again, as the rig found it"),
		UUITextInput::IsHostDeliveringCharacterEvents());

	// The other direction: on going in, turned off inside -- what a test of the fallback road does.
	UUITextInput::SetHostDeliversCharacterEventsForTesting(true);
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The second rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UUITextInput::SetHostDeliversCharacterEventsForTesting(false);
	}
	TestTrue(TEXT("Once that rig is gone the switch is on again, as it found it"),
		UUITextInput::IsHostDeliveringCharacterEvents());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverIsolationCounterGuardTest,
	"DreamGUI.Driver.Isolation.TheTeardownGuardNamesEveryProcessCounterThatWasLeftUnsettled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverIsolationCounterGuardTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverIsolationTestLocal;

	// The guard's own verdicts, handed states no tear-down can safely produce for real: the only way
	// to be inside a layout pass when a rig goes is to tear it down from inside one.
	TestEqual(TEXT("All settled: nothing to say"), FDreamDriverRig::DescribeUnsettledProcessState(0, 0, false).Num(), 0);

	const TArray<FString> LayoutLeftOpen = FDreamDriverRig::DescribeUnsettledProcessState(1, 0, false);
	TestEqual(TEXT("A layout pass left open is one complaint"), LayoutLeftOpen.Num(), 1);
	TestTrue(TEXT("And it names the layout pass"), LayoutLeftOpen.Num() == 1 && LayoutLeftOpen[0].Contains(TEXT("layout pass")));

	// A stray decrement is the same fault the other way round, and only the raw depth can show it.
	TestEqual(TEXT("A negative layout depth is a complaint too"), FDreamDriverRig::DescribeUnsettledProcessState(-1, 0, false).Num(), 1);

	const TArray<FString> MemoLeftOpen = FDreamDriverRig::DescribeUnsettledProcessState(0, 2, false);
	TestTrue(TEXT("A desired-size memo left open is named"), MemoLeftOpen.Num() == 1 && MemoLeftOpen[0].Contains(TEXT("memo")));

	const TArray<FString> StillCompiling = FDreamDriverRig::DescribeUnsettledProcessState(0, 0, true);
	TestTrue(TEXT("A compile the editor never heard finish is named"), StillCompiling.Num() == 1 && StillCompiling[0].Contains(TEXT("compiling")));

	TestEqual(TEXT("Every counter unsettled is every complaint"), FDreamDriverRig::DescribeUnsettledProcessState(3, 1, true).Num(), 3);

	// And the live values after a real rig with real layout in it: settled, which is what lets the
	// destructor's check stay quiet in every ordinary test.
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		Rig.MakeWidget(TEXT("Panel"), nullptr, FVector2D(400.0, 300.0));
		Rig.MakeControl<UDreamTextInput>(TEXT("Field"), nullptr, FVector2D(300.0, 40.0), FVector2D(0.0, -200.0));
		Rig.PumpFrames(3);
		TestEqual(TEXT("Between frames no layout pass is open"), UDreamWidget::GetLayoutPassDepthForTesting(), 0);
		TestEqual(TEXT("Between frames no desired-size memo is open"), UDreamPanelLayoutBase::GetDesiredSizeMemoDepthForTesting(), 0);
	}
	TestEqual(TEXT("After the rig no layout pass is open"), UDreamWidget::GetLayoutPassDepthForTesting(), 0);
	TestEqual(TEXT("After the rig no desired-size memo is open"), UDreamPanelLayoutBase::GetDesiredSizeMemoDepthForTesting(), 0);
	TestFalse(TEXT("And the editor object does not think a Blueprint is compiling"), UDreamUIManagerObject::GetIsBlueprintCompiling());
	return true;
}

#endif
