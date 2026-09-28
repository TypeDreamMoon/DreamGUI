// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "DreamGUIEditorSubsystem.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"

/*
 * A RIG LEAVES THE PROCESS AS IT FOUND IT.
 *
 * State that is in no world outlives every world, so it outlives every test, and a test that moved it
 * decides how the tests after it behave -- in the order the runner happens to pick. The editor's "a
 * Blueprint is compiling" flag is the one left. The switch saying a host delivers characters, and the
 * field that owns a player's keyboard, used to be among them: the switch flipped for good on the first
 * character a host delivered, and from then on the key-to-character fallback, the road DevTest itself is
 * on, could no longer be reached in that process. Both are kept on each world's input now and go with it.
 *
 * The layout pass and the desired-size memo are no longer among them either: their depths live in each
 * world's layout context and go with the world. The rig still checks them settled before its world goes,
 * since one left open is a pass that never ended.
 *
 * The rig makes sure the keyboard's owner is nothing of its own, and checks the counters are settled
 * when it goes. These pin that the switch is each world's, and that the check says what it would say.
 */
namespace DreamDriverIsolationTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverIsolationCharacterSwitchTest,
	"DreamGUI.Driver.Isolation.WhetherAHostDeliversCharactersIsEachWorldsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverIsolationCharacterSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverIsolationTestLocal;
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		TestFalse(TEXT("A world starts on the key road"), UUITextInput::IsHostDeliveringCharacterEvents(Rig.GetWorld()));
		UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0));
		if (!TestNotNull(TEXT("A text field can be made"), Field))
		{
			return false;
		}
		Rig.PumpFrames(1);
		TestTrue(TEXT("Typing a character completes"), Rig.Driver()->Find(FDreamBy::Name(TEXT("Name")))->Type(TEXT("a")));
		// The driver types through HandleCharacterInput, the host's road, and that road flips the switch --
		// for this world.
		TestTrue(TEXT("Typing through the host's road turned this world's switch on"),
			UUITextInput::IsHostDeliveringCharacterEvents(Rig.GetWorld()));
	}
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(this);
		if (!TestTrue(TEXT("The second rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		// Nothing was put back in between: there was nothing outside the first world to put back.
		TestFalse(TEXT("A world that comes up after it starts on the key road all the same"),
			UUITextInput::IsHostDeliveringCharacterEvents(Rig.GetWorld()));
		TestNull(TEXT("...and nobody in it is typing into anything"), UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 0));
	}
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
		const UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld());
		if (!TestNotNull(TEXT("The rig's world has a UI manager"), Manager))
		{
			return false;
		}
		const FDreamLayoutPassContext& LayoutContext = Manager->GetLayoutPassContext();
		TestEqual(TEXT("Between frames no layout pass is open"), LayoutContext.GetPassDepth(), 0);
		TestEqual(TEXT("Between frames no desired-size memo is open"), LayoutContext.GetMemoDepth(), 0);
		TestEqual(TEXT("Between frames no widget is still recorded as writing layout"), LayoutContext.GetWriterCount(), 0);
		TestEqual(TEXT("and no desired size is remembered past its pass"), LayoutContext.GetRecordedDesiredSizeCount(), 0);
	}
	const UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
	TestFalse(TEXT("And the editor does not think a widget class is recompiling"), EditorSubsystem != nullptr && EditorSubsystem->IsRecompiling());
	return true;
}

#endif
