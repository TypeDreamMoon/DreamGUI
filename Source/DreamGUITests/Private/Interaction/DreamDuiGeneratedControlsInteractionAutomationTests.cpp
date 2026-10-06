// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamViewModelTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/UIButton.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * Controls a .dui makes for itself -- the copies a `for` stamps out, the widgets a `rows` table writes, the branches of
 * an `if` -- clicked and walked with the pad like any other.
 *
 * The language's own suites hold what each form builds (DreamGUI.Binding.For, DreamGUI.Text.Syntax.ARows*) and, for a
 * `for` over a view model, call a copy's routed event by broadcasting it. What only input can show is that the made
 * widgets are real controls on the screen: hit-tested where they are drawn, in the navigation order of the panel that
 * arranges them, a copy's route reaching its own item from a click, and a branch that is not taken neither clickable nor
 * a stop for the pad -- `if` collapses what it hides, and a collapsed widget is neither hit nor navigated to in UMG
 * either (EVisibility::Collapsed).
 */
namespace DreamDuiGeneratedControlsTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}
		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
		}
		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}
		FString FilePath;
	};

	/** A .dui-backed Blueprint in /Temp, its package rooted for the test, pointed at InFilePath and compiled. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FCompilerResultsLog Results;

		FScopedTextBlueprint(const TCHAR* InName, const FString& InFilePath)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				Blueprint = nullptr;
				return;
			}
			Defaults->SourceFile.FilePath = InFilePath;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		}
		~FScopedTextBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
		UClass* GetClass() const { return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr; }
		FString Messages() const
		{
			FString All;
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				All += Message->ToText().ToString() + TEXT(" | ");
			}
			return All;
		}
	};

	/** The compiled class on the rig's screen, given InViewModel as Player when there is one. Null, having said why, otherwise. */
	UDreamUserWidget* PlaceCompiled(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FScopedTextBlueprint& InFixture, UObject* InViewModel)
	{
		UClass* Class = InFixture.GetClass();
		if (!InTest.TestNotNull(TEXT("The .dui compiled into a class"), Class)
			|| !InTest.TestEqual(*FString::Printf(TEXT("...clean, saw [%s]"), *InFixture.Messages()), InFixture.Results.NumErrors, 0))
		{
			return nullptr;
		}
		UDreamUserWidget* Instance = Cast<UDreamUserWidget>(InRig.MakeControl(Class, TEXT("GeneratedScreen"), nullptr, FVector2D(420.0, 400.0)));
		if (!InTest.TestNotNull(TEXT("The class is placed on the rig's screen"), Instance))
		{
			return nullptr;
		}
		if (InViewModel != nullptr && !InTest.TestTrue(TEXT("...and takes the view model"), Instance->SetViewModel(TEXT("Player"), InViewModel)))
		{
			return nullptr;
		}
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(2);
		return Instance;
	}

	/** The copies of InTemplate: its siblings carrying its display name, in the order they stand. */
	TArray<UDreamWidget*> CopiesOf(const UDreamWidget* InTemplate)
	{
		TArray<UDreamWidget*> Copies;
		const UDreamWidget* Panel = IsValid(InTemplate) ? InTemplate->GetParent() : nullptr;
		if (Panel != nullptr)
		{
			for (UDreamWidget* Child : Panel->GetChildren())
			{
				if (IsValid(Child) && Child != InTemplate && Child->GetDisplayName() == InTemplate->GetDisplayName())
				{
					Copies.Add(Child);
				}
			}
		}
		return Copies;
	}

	/** Every button under InParent, in tree order, a button's own parts not searched. */
	void CollectButtons(const UDreamWidget* InParent, TArray<UDreamButton*>& OutButtons)
	{
		if (!IsValid(InParent))
		{
			return;
		}
		for (UDreamWidget* Child : InParent->GetChildren())
		{
			if (UDreamButton* Button = Cast<UDreamButton>(Child))
			{
				OutButtons.Add(Button);
			}
			else
			{
				CollectButtons(Child, OutButtons);
			}
		}
	}

	TArray<UDreamButton*> ButtonsUnder(const UDreamWidget* InParent)
	{
		TArray<UDreamButton*> Buttons;
		CollectButtons(InParent, Buttons);
		return Buttons;
	}

	UDreamTestItemVM* MakeItem(UObject* InOuter, const TCHAR* InName)
	{
		UDreamTestItemVM* Item = NewObject<UDreamTestItemVM>(InOuter);
		Item->Name = FText::FromString(InName);
		return Item;
	}

	/** Every click of each button, logged by its place in InButtons. */
	void LogClicks(const TArray<UDreamButton*>& InButtons, TArray<int32>& OutLog)
	{
		TArray<int32>* Log = &OutLog;
		for (int32 Index = 0; Index < InButtons.Num(); ++Index)
		{
			if (InButtons[Index] != nullptr && InButtons[Index]->ButtonBehaviour != nullptr)
			{
				InButtons[Index]->ButtonBehaviour->GetOnClickEvent().AddLambda([Log, Index]() { Log->Add(Index); });
			}
		}
	}

	/** Whether player 0's focus is on InControl or on one of its parts. */
	bool IsFocusOn(FDreamDriverRig& InRig, const UDreamWidget* InControl)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
		return Focused != nullptr && InControl != nullptr && (Focused == InControl || Focused->IsChildOf(InControl));
	}

	/** Focus InButton as a screen's initial focus does: on its face, where its selectable is. */
	bool FocusFirst(FDreamDriverRig& InRig, UDreamButton* InButton)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0);
	}

	bool PressDown(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform();
	}

	bool Confirm(FDreamDriverRig& InRig)
	{
		return InRig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform();
	}

	/** The `for` every test of copies here compiles: a button per item of Player.Items, its click routed to its item. */
	TArray<FString> ForButtonsBody(const TCHAR* InClassName)
	{
		return {
			FString::Printf(TEXT("class /Temp/DreamGUITests/%s"), InClassName),
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("}"),
			TEXT("VerticalBox Root {"),
			TEXT("    for Item in Player.Items {"),
			TEXT("        Native.Button UseButton { OnClicked -> Item.Use() }"),
			TEXT("    }"),
			TEXT("}"),
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDuiForCopiesClickedTest,
	"DreamGUI.Binding.For.EachButtonAForMakesIsClickedWhereItIsDrawnAndCallsItsOwnItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDuiForCopiesClickedTest, "DreamGUI.Binding.For.EachButtonAForMakesIsClickedWhereItIsDrawnAndCallsItsOwnItem", "[Pointer][Animated]")

/*
 * `for Item in Player.Items { Native.Button UseButton { OnClicked -> Item.Use() } }` over three items: three buttons, one
 * under another, the template itself collapsed. A click on the second calls the second item's Use, once, and no other's;
 * a click on the third the third's.
 */
bool FDreamDuiForCopiesClickedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDuiGeneratedControlsTestLocal;
	FScopedDuiFile File(TEXT("ForCopiesClicked.dui"));
	TestTrue(TEXT("The file was written"), File.Write(ForButtonsBody(TEXT("BP_ForCopiesClicked"))));
	FScopedTextBlueprint Fixture(TEXT("BP_ForCopiesClicked"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	UDreamTestItemVM* A = MakeItem(Player.Get(), TEXT("A"));
	UDreamTestItemVM* B = MakeItem(Player.Get(), TEXT("B"));
	UDreamTestItemVM* C = MakeItem(Player.Get(), TEXT("C"));
	Player->Items = { A, B, C };
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamWidget* Template = Instance != nullptr ? Instance->GetWidgetFromName(TEXT("UseButton")) : nullptr;
	const TArray<UDreamWidget*> Copies = CopiesOf(Template);
	if (!TestEqual(TEXT("The for made a button per item"), Copies.Num(), 3))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	TestFalse(TEXT("The template it copies is not on screen to be clicked"), Driver->Find(FDreamBy::Widget(Template))->IsVisible());

	TestTrue(TEXT("Clicking the second copy completes"), Driver->Find(FDreamBy::Widget(Copies[1]))->Click());
	TestEqual(TEXT("The click called the second item's Use, once"), B->UseCount, 1);
	TestEqual(TEXT("...and no other item's"), A->UseCount + C->UseCount, 0);
	TestTrue(TEXT("Clicking the third copy completes"), Driver->Find(FDreamBy::Widget(Copies[2]))->Click());
	TestEqual(TEXT("The click called the third item's Use, once"), C->UseCount, 1);
	TestEqual(TEXT("...and left the others where they were"), A->UseCount + B->UseCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDuiForCopiesNavigatedTest,
	"DreamGUI.Binding.For.TheDPadWalksTheButtonsAForMadeInItemOrderAndConfirmCallsTheFocusedOnesItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDuiForCopiesNavigatedTest, "DreamGUI.Binding.For.TheDPadWalksTheButtonsAForMadeInItemOrderAndConfirmCallsTheFocusedOnesItem", "[Nav][Animated]")

/*
 * The same `for` over three items, the focus on the first copy. The D-pad's Down goes to the second copy and then the
 * third -- the panel's order, the items' order -- never to the collapsed template; confirm on each calls its own item.
 */
bool FDreamDuiForCopiesNavigatedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDuiGeneratedControlsTestLocal;
	FScopedDuiFile File(TEXT("ForCopiesNavigated.dui"));
	TestTrue(TEXT("The file was written"), File.Write(ForButtonsBody(TEXT("BP_ForCopiesNavigated"))));
	FScopedTextBlueprint Fixture(TEXT("BP_ForCopiesNavigated"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	UDreamTestItemVM* A = MakeItem(Player.Get(), TEXT("A"));
	UDreamTestItemVM* B = MakeItem(Player.Get(), TEXT("B"));
	UDreamTestItemVM* C = MakeItem(Player.Get(), TEXT("C"));
	Player->Items = { A, B, C };
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamWidget* Template = Instance != nullptr ? Instance->GetWidgetFromName(TEXT("UseButton")) : nullptr;
	const TArray<UDreamWidget*> Copies = CopiesOf(Template);
	if (!TestEqual(TEXT("The for made a button per item"), Copies.Num(), 3)
		|| !TestTrue(TEXT("The first copy takes the screen's initial focus"), FocusFirst(Rig, Cast<UDreamButton>(Copies[0]))))
	{
		return false;
	}

	TestTrue(TEXT("Down completes"), PressDown(Rig));
	TestTrue(TEXT("Down went to the second copy"), IsFocusOn(Rig, Copies[1]));
	TestTrue(TEXT("Confirm completes"), Confirm(Rig));
	TestEqual(TEXT("Confirm called the second item's Use"), B->UseCount, 1);
	TestTrue(TEXT("Down again completes"), PressDown(Rig));
	TestTrue(TEXT("Down went to the third copy"), IsFocusOn(Rig, Copies[2]));
	TestFalse(TEXT("...never to the collapsed template"), IsFocusOn(Rig, Template));
	TestTrue(TEXT("Confirm completes"), Confirm(Rig));
	TestEqual(TEXT("Confirm called the third item's Use"), C->UseCount, 1);
	TestEqual(TEXT("...and the first item was never called"), A->UseCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDuiRowsButtonsTest,
	"DreamGUI.Binding.Rows.EachButtonARowsTableWritesIsClickedAndTheDPadWalksThemInTableOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDuiRowsButtonsTest, "DreamGUI.Binding.Rows.EachButtonARowsTableWritesIsClickedAndTheDPadWalksThemInTableOrder", "[Pointer][Nav][Animated]")

/*
 * `rows Native.Button (ToolTipText) { "Low"; "Medium"; "High" }`: three buttons, written into the panel as if each line
 * were a node of its own. A click on the second answers on the second alone. From the first, the D-pad goes down to the
 * second and then the third, in the table's order, and confirm presses the one it is on.
 */
bool FDreamDuiRowsButtonsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDuiGeneratedControlsTestLocal;
	FScopedDuiFile File(TEXT("RowsButtons.dui"));
	TestTrue(TEXT("The file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_RowsButtons"),
		TEXT("VerticalBox Root {"),
		TEXT("    rows Native.Button (ToolTipText) {"),
		TEXT("        \"Low\""),
		TEXT("        \"Medium\""),
		TEXT("        \"High\""),
		TEXT("    }"),
		TEXT("}") }));
	FScopedTextBlueprint Fixture(TEXT("BP_RowsButtons"), File.FilePath);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, nullptr);
	const TArray<UDreamButton*> Buttons = ButtonsUnder(Instance);
	if (!TestEqual(TEXT("The table wrote a button per line"), Buttons.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("...in the table's order: the first line's value on the first"), Buttons[0]->GetToolTipText().ToString(), FString(TEXT("Low")));
	TestEqual(TEXT("...and the last line's on the last"), Buttons[2]->GetToolTipText().ToString(), FString(TEXT("High")));
	TArray<int32> Clicks;
	LogClicks(Buttons, Clicks);

	TestTrue(TEXT("Clicking the second button completes"), Rig.Driver()->Find(FDreamBy::Widget(Buttons[1]))->Click());
	if (TestEqual(TEXT("One button answered the click"), Clicks.Num(), 1))
	{
		TestEqual(TEXT("...the second"), Clicks[0], 1);
	}

	if (!TestTrue(TEXT("The first button takes the screen's initial focus"), FocusFirst(Rig, Buttons[0])))
	{
		return false;
	}
	TestTrue(TEXT("Down completes"), PressDown(Rig));
	TestTrue(TEXT("Down went to the second line's button"), IsFocusOn(Rig, Buttons[1]));
	TestTrue(TEXT("Down again completes"), PressDown(Rig));
	TestTrue(TEXT("...and then to the third's"), IsFocusOn(Rig, Buttons[2]));
	TestTrue(TEXT("Confirm completes"), Confirm(Rig));
	if (TestEqual(TEXT("Confirm pressed one button more"), Clicks.Num(), 2))
	{
		TestEqual(TEXT("...the third, which had the focus"), Clicks[1], 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDuiIfBranchesTest,
	"DreamGUI.Binding.If.OnlyTheBranchTakenIsClickedOrReachedByTheDPadAndSwitchingTheBranchSwapsWhichOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDuiIfBranchesTest, "DreamGUI.Binding.If.OnlyTheBranchTakenIsClickedOrReachedByTheDPadAndSwitchingTheBranchSwapsWhichOne", "[Pointer][Nav][Animated]")

/*
 * A Top button, then `if Player.bIsDead { Respawn } else { Resume }`. Alive: Resume is on screen and clicked, Respawn is
 * not on screen, and Down from Top lands on Resume. The player dies -- the view model announces it -- and the branches
 * swap: Respawn is on screen and clicked, and Down from Top lands on it, never on the collapsed Resume.
 */
bool FDreamDuiIfBranchesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDuiGeneratedControlsTestLocal;
	FScopedDuiFile File(TEXT("IfBranchButtons.dui"));
	TestTrue(TEXT("The file was written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_IfBranchButtons"),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("VerticalBox Root {"),
		TEXT("    Native.Button Top { }"),
		TEXT("    if Player.bIsDead {"),
		TEXT("        Native.Button Respawn { }"),
		TEXT("    } else {"),
		TEXT("        Native.Button Resume { }"),
		TEXT("    }"),
		TEXT("}") }));
	FScopedTextBlueprint Fixture(TEXT("BP_IfBranchButtons"), File.FilePath);
	TStrongObjectPtr<UDreamTestPlayerVM> Player(NewObject<UDreamTestPlayerVM>(GetTransientPackage()));
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUserWidget* Instance = PlaceCompiled(*this, Rig, Fixture, Player.Get());
	UDreamButton* Top = Instance != nullptr ? Cast<UDreamButton>(Instance->GetWidgetFromName(TEXT("Top"))) : nullptr;
	UDreamButton* Respawn = Instance != nullptr ? Cast<UDreamButton>(Instance->GetWidgetFromName(TEXT("Respawn"))) : nullptr;
	UDreamButton* Resume = Instance != nullptr ? Cast<UDreamButton>(Instance->GetWidgetFromName(TEXT("Resume"))) : nullptr;
	if (!TestTrue(TEXT("Top and both branches' buttons are there"), Top != nullptr && Respawn != nullptr && Resume != nullptr))
	{
		return false;
	}
	TArray<int32> Clicks;
	LogClicks({ Top, Respawn, Resume }, Clicks);
	FDreamDriverRef Driver = Rig.Driver();

	// Alive: the else branch.
	TestTrue(TEXT("Alive, Resume is on screen"), Driver->Find(FDreamBy::Widget(Resume))->IsVisible());
	TestFalse(TEXT("...and Respawn is not"), Driver->Find(FDreamBy::Widget(Respawn))->IsVisible());
	TestTrue(TEXT("Clicking Resume completes"), Driver->Find(FDreamBy::Widget(Resume))->Click());
	TestTrue(TEXT("...and Resume answered it"), Clicks.Num() == 1 && Clicks[0] == 2);
	if (!TestTrue(TEXT("Top takes the screen's initial focus"), FocusFirst(Rig, Top)))
	{
		return false;
	}
	TestTrue(TEXT("Down completes"), PressDown(Rig));
	TestTrue(TEXT("Down from Top lands on Resume"), IsFocusOn(Rig, Resume));
	TestFalse(TEXT("...not on the hidden Respawn"), IsFocusOn(Rig, Respawn));

	// Dead: the branches swap.
	Player->SetIsDead(true);
	Rig.PumpFrames(2);
	TestTrue(TEXT("Dead, Respawn is on screen"), Driver->Find(FDreamBy::Widget(Respawn))->IsVisible());
	TestFalse(TEXT("...and Resume is not"), Driver->Find(FDreamBy::Widget(Resume))->IsVisible());
	TestTrue(TEXT("Clicking Respawn completes"), Driver->Find(FDreamBy::Widget(Respawn))->Click());
	TestTrue(TEXT("...and Respawn answered it"), Clicks.Num() == 2 && Clicks[1] == 1);
	if (!TestTrue(TEXT("Top takes the focus again"), FocusFirst(Rig, Top)))
	{
		return false;
	}
	TestTrue(TEXT("Down completes"), PressDown(Rig));
	TestTrue(TEXT("Down from Top lands on Respawn now"), IsFocusOn(Rig, Respawn));
	TestFalse(TEXT("...not on the collapsed Resume"), IsFocusOn(Rig, Resume));
	return true;
}

#endif
