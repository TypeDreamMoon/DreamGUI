// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"
#include "DreamViewModelRuntimeTestTypes.h"
#include "DreamViewModelTestTypes.h"
#include "DreamWidgetBlueprint.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "ViewModel/DreamViewModel.h"
#include "ViewModel/DreamViewModelSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

/*
 * View models end to end: a .dui with a `viewmodels` block compiled into a real class, instanced in a world, and
 * driven from the view model's side. What only this can show is the whole route -- the compiler's recorded
 * dependencies reaching the class, UDreamUserWidget ruling each binding subscribed, polled or constant from them,
 * the observer re-aiming when a view model is swapped, the `= new` / `= global` / `= parent` entries filled at the
 * right moment -- with nothing ticking: every update asserted here without an explicit poll came from a broadcast.
 */

namespace DreamUIViewModelRuntimeTestLocal
{
	using DreamTests::FScopedGameWorld;

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const FString& InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
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

	/** A Blueprint in /Temp, deriving from InParentClass, kept alive (its package rooted) for the test. */
	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FCompilerResultsLog Results;

		FScopedBlueprint(const TCHAR* InName, UClass* InParentClass)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}
		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		void Compile()
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		}

		UClass* GetClass() const { return Blueprint != nullptr ? Blueprint->GeneratedClass.Get() : nullptr; }

		FString JoinMessages() const
		{
			FString All;
			for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
			{
				All += Message->ToText().ToString() + TEXT(" | ");
			}
			return All;
		}
	};

	/** One .dui file and the class it compiles into. */
	struct FCompiledDui
	{
		FString Name;
		FScopedDuiFile File;
		TUniquePtr<FScopedBlueprint> Blueprint;

		explicit FCompiledDui(const TCHAR* InName)
			: Name(InName)
			, File(FString::Printf(TEXT("%s.dui"), InName))
		{
		}

		/**
		 * Write InBodyLines under a `class /Temp/DreamGUITests/<Name>` line, compile it into a Blueprint of
		 * InParentClass and return the class -- null, with a test failure saying what the compile said, when it did not
		 * compile clean.
		 */
		UClass* Compile(FAutomationTestBase& InTest, const TArray<FString>& InBodyLines,
			UClass* InParentClass = UDreamTestViewModelWidgetBase::StaticClass())
		{
			TArray<FString> Lines;
			Lines.Add(FString::Printf(TEXT("class /Temp/DreamGUITests/%s"), *Name));
			Lines.Append(InBodyLines);
			if (!InTest.TestTrue(*FString::Printf(TEXT("%s: the file was written"), *Name), File.Write(Lines)))
			{
				return nullptr;
			}
			Blueprint = MakeUnique<FScopedBlueprint>(*Name, InParentClass);
			UDreamTextUserWidget* Defaults = Blueprint->Blueprint != nullptr && Blueprint->Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (!InTest.TestNotNull(*FString::Printf(TEXT("%s: the Blueprint was made"), *Name), Defaults))
			{
				return nullptr;
			}
			Defaults->SourceFile.FilePath = File.FilePath;
			Blueprint->Compile();
			UClass* Class = Blueprint->GetClass();
			if (!InTest.TestNotNull(*FString::Printf(TEXT("%s: a class came out"), *Name), Class)
				|| !InTest.TestEqual(*FString::Printf(TEXT("%s compiles clean, saw [%s]"), *Name, *Blueprint->JoinMessages()),
					Blueprint->Results.NumErrors, 0))
			{
				return nullptr;
			}
			return Class;
		}
	};

	UDreamText* FindText(const UDreamUserWidget* InWidget, const TCHAR* InName)
	{
		UDreamWidget* Widget = InWidget != nullptr ? InWidget->GetWidgetFromName(FName(InName)) : nullptr;
		return Widget != nullptr ? Cast<UDreamText>(Widget->GetVisual()) : nullptr;
	}

	FString TextOf(const UDreamUserWidget* InWidget, const TCHAR* InName)
	{
		const UDreamText* Text = FindText(InWidget, InName);
		return Text != nullptr ? Text->GetText().ToString() : FString(TEXT("<no text widget>"));
	}

	/** What the class authored for InName's text: the value a binding that never evaluated leaves standing. */
	FString AuthoredTextOf(const UClass* InClass, const TCHAR* InName)
	{
		const UDreamWidgetTree* Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass);
		const UDreamWidget* Widget = Archetype != nullptr ? Archetype->FindWidgetByVariableName(FName(InName)) : nullptr;
		const UDreamText* Text = Widget != nullptr ? Cast<UDreamText>(Widget->GetVisual()) : nullptr;
		return Text != nullptr ? Text->GetText().ToString() : FString(TEXT("<no authored text>"));
	}

	float FontSizeOf(const UDreamUserWidget* InWidget, const TCHAR* InName)
	{
		const UDreamText* Text = FindText(InWidget, InName);
		return Text != nullptr ? Text->GetFontSize() : -1.f;
	}

	TStrongObjectPtr<UDreamTestCountingPlayerVM> MakePlayer(const TCHAR* InName)
	{
		TStrongObjectPtr<UDreamTestCountingPlayerVM> Player(NewObject<UDreamTestCountingPlayerVM>(GetTransientPackage()));
		Player->SetName(FText::FromString(InName));
		return Player;
	}

	/** The subscribed fixture: one entry the host gives, one text showing a member of it. */
	TArray<FString> PlayerNameBody()
	{
		return {
			TEXT("viewmodels {"),
			TEXT("    DreamTestPlayerVM Player"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("        Text <- Player.Name"),
			TEXT("    }"),
			TEXT("}"),
		};
	}

	bool ContainsLine(const TArray<FString>& InLines, const TCHAR* InFragment)
	{
		return InLines.ContainsByPredicate([InFragment](const FString& Line) { return Line.Contains(InFragment); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeSubscribedTest,
	"DreamGUI.ViewModel.Runtime.ABindingThroughAViewModelIsSubscribedNotPolled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Text <- Player.Name`: the compiler records {Player, Name}, every hop announces, so the binding subscribes and the
 * widget never joins the manager's poll. Handing the widget a view model shows its name; the view model's own setter
 * changing it shows the new one at once. A write nobody announces is NOT picked up -- not even by the poll, which this
 * binding is not on.
 */
bool FDreamUIViewModelRuntimeSubscribedTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeSubscribed"));
	UClass* Class = Dui.Compile(*this, PlayerNameBody());
	if (Class == nullptr)
	{
		return false;
	}

	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(Class, Bindings);
	const FDreamWidgetPropertyBinding* Recorded = Bindings.FindByPredicate([](const FDreamWidgetPropertyBinding& Binding)
	{
		return Binding.WidgetName == FName(TEXT("Title")) && Binding.PropertyName == FName(TEXT("Text"));
	});
	if (TestNotNull(TEXT("the binding reached the class"), Recorded))
	{
		TestTrue(TEXT("with its dependencies recorded"), Recorded->bDependenciesRecorded);
		TestTrue(TEXT("complete"), Recorded->bDependenciesComplete);
		FDreamWidgetBindingPath Expected;
		Expected.Segments = { FName(TEXT("Player")), FName(TEXT("Name")) };
		TestTrue(TEXT("including Player.Name"), Recorded->Dependencies.Contains(Expected));
	}

	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("the binding is subscribed"), Counts.Subscribed, 1);
	TestEqual(TEXT("not polled"), Counts.Polled, 0);
	TestFalse(TEXT("so the widget has nothing to poll"), Instance->HasPolledPropertyBindings());
	if (Manager != nullptr)
	{
		TestEqual(TEXT("and the manager polls no one"), Manager->GetPropertyBindingUserCount(), 0);
	}
	TestEqual(TEXT("with no view model, the text keeps what was authored"), TextOf(Instance, TEXT("Title")), AuthoredTextOf(Class, TEXT("Title")));

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakePlayer(TEXT("Ayla"));
	TestTrue(TEXT("SetViewModel takes a view model of the entry's class"), Instance->SetViewModel(TEXT("Player"), Player.Get()));
	TestTrue(TEXT("GetViewModel answers it"), Instance->GetViewModel(TEXT("Player")) == Player.Get());
	TestEqual(TEXT("handing it over shows its value"), TextOf(Instance, TEXT("Title")), FString(TEXT("Ayla")));
	TestEqual(TEXT("one delegate on the view model: its Name"), Player->LiveDelegateCount, 1);

	Player->SetName(FText::FromString(TEXT("Bea")));
	TestEqual(TEXT("the view model's setter updates the text, no poll in between"), TextOf(Instance, TEXT("Title")), FString(TEXT("Bea")));

	Player->Name = FText::FromString(TEXT("Silent"));
	Instance->EvaluatePolledPropertyBindings();
	TestEqual(TEXT("a write nobody announces is not seen: the binding is not on the poll"), TextOf(Instance, TEXT("Title")), FString(TEXT("Bea")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimePolledTest,
	"DreamGUI.ViewModel.Runtime.ABindingThroughAnUnannouncedMemberIsPolledAndUpdatesOnThePoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelRuntimePolledTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimePolled"));
	UClass* Class = Dui.Compile(*this, {
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Size {"),
		TEXT("        FontSize <- Player.Untracked"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Class == nullptr)
	{
		return false;
	}

	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("a member nobody announces puts the binding on the poll"), Counts.Polled, 1);
	TestEqual(TEXT("it is not subscribed"), Counts.Subscribed, 0);
	TestTrue(TEXT("the widget has something to poll"), Instance->HasPolledPropertyBindings());
	if (Manager != nullptr)
	{
		TestEqual(TEXT("and is on the manager's visit"), Manager->GetPropertyBindingUserCount(), 1);
	}
	TArray<FString> Why;
	Instance->DescribePolledBindings(Why);
	TestTrue(*FString::Printf(TEXT("the reason names the member, saw [%s]"), *FString::Join(Why, TEXT(" | "))), ContainsLine(Why, TEXT("Untracked")));

	const float Authored = FontSizeOf(Instance, TEXT("Size"));
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakePlayer(TEXT("Poll"));
	Player->Untracked = Authored + 7.f;
	Instance->SetViewModel(TEXT("Player"), Player.Get());
	TestEqual(TEXT("a polled binding waits for the poll"), FontSizeOf(Instance, TEXT("Size")), Authored);
	TestEqual(TEXT("and places nothing on the view model"), Player->LiveDelegateCount, 0);
	Instance->EvaluatePolledPropertyBindings();
	TestEqual(TEXT("the poll shows it"), FontSizeOf(Instance, TEXT("Size")), Authored + 7.f);
	Player->Untracked = Authored + 9.f;
	Instance->EvaluatePolledPropertyBindings();
	TestEqual(TEXT("and every change after"), FontSizeOf(Instance, TEXT("Size")), Authored + 9.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeSwapTest,
	"DreamGUI.ViewModel.Runtime.SwappingOrClearingTheViewModelMovesTheSubscription",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A second view model handed over replaces the first: the binding shows the new one's value, the old one's changes
 * no longer reach the widget and it is left with no delegate. Handing over null breaks the chain: the text keeps the
 * last value rather than resetting, and nothing is left listening.
 */
bool FDreamUIViewModelRuntimeSwapTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeSwap"));
	UClass* Class = Dui.Compile(*this, PlayerNameBody());
	if (Class == nullptr)
	{
		return false;
	}
	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> First = MakePlayer(TEXT("First"));
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Second = MakePlayer(TEXT("Second"));
	Instance->SetViewModel(TEXT("Player"), First.Get());
	TestEqual(TEXT("the first is shown"), TextOf(Instance, TEXT("Title")), FString(TEXT("First")));

	Instance->SetViewModel(TEXT("Player"), Second.Get());
	TestEqual(TEXT("the second replaces it"), TextOf(Instance, TEXT("Title")), FString(TEXT("Second")));
	TestEqual(TEXT("the first is let go"), First->LiveDelegateCount, 0);
	TestEqual(TEXT("the second is listened to"), Second->LiveDelegateCount, 1);
	First->SetName(FText::FromString(TEXT("Stale")));
	TestEqual(TEXT("the first one's changes no longer reach the widget"), TextOf(Instance, TEXT("Title")), FString(TEXT("Second")));
	Second->SetName(FText::FromString(TEXT("Live")));
	TestEqual(TEXT("the second one's do"), TextOf(Instance, TEXT("Title")), FString(TEXT("Live")));

	Instance->SetViewModel(TEXT("Player"), nullptr);
	TestNull(TEXT("null clears the entry"), Instance->GetViewModel(TEXT("Player")));
	TestEqual(TEXT("a broken chain keeps the last value"), TextOf(Instance, TEXT("Title")), FString(TEXT("Live")));
	TestEqual(TEXT("and leaves nothing on the view model"), Second->LiveDelegateCount, 0);
	Second->SetName(FText::FromString(TEXT("Gone")));
	TestEqual(TEXT("which no longer reaches the widget"), TextOf(Instance, TEXT("Title")), FString(TEXT("Live")));

	// The checks SetViewModel makes, each with its warning.
	AddExpectedMessagePlain(TEXT("has no viewmodels entry named"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("so it was not set"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("an entry the class does not have is refused"), Instance->SetViewModel(TEXT("NoSuchEntry"), First.Get()));
	const TStrongObjectPtr<UDreamTestStatsVM> WrongClass(NewObject<UDreamTestStatsVM>(GetTransientPackage()));
	TestFalse(TEXT("an object of another class is refused"), Instance->SetViewModel(TEXT("Player"), WrongClass.Get()));
	TestNull(TEXT("and leaves the entry as it was"), Instance->GetViewModel(TEXT("Player")));
	TestNull(TEXT("GetViewModel of a name that is no entry is null"), Instance->GetViewModel(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeDestroyTest,
	"DreamGUI.ViewModel.Runtime.ADestroyedWidgetLeavesNoDelegateOnItsViewModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelRuntimeDestroyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeDestroy"));
	UClass* Class = Dui.Compile(*this, PlayerNameBody());
	if (Class == nullptr)
	{
		return false;
	}
	FScopedGameWorld TestWorld;
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakePlayer(TEXT("Outlives"));
	TWeakObjectPtr<UDreamUserWidget> Weak;
	{
		UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
		if (!TestNotNull(TEXT("the class instantiates"), Instance))
		{
			return false;
		}
		Instance->SetViewModel(TEXT("Player"), Player.Get());
		TestEqual(TEXT("the widget listens to the view model"), Player->LiveDelegateCount, 1);
		Weak = Instance;
		Instance->DestroyWidget();
	}
	// The widget's delegates are bound weakly to it, so a destroyed widget is never called -- and the collection that
	// frees it takes them off the view model, which outlives it.
	Player->SetName(FText::FromString(TEXT("after destroy")));
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("the widget was collected"), Weak.IsValid(/*bEvenIfPendingKill*/ true));
	TestEqual(TEXT("and left no delegate on the view model"), Player->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeNewTest,
	"DreamGUI.ViewModel.Runtime.ANewEntryIsMadeOuteredToTheWidgetBeforeOnInitialized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `= new`: the widget makes its own, outered to itself, before On Initialized -- so the graph there already sees it,
 * and the first evaluation reads it. Not a design-time instance in a game world. A widget handed one before it
 * initializes keeps that one instead.
 */
bool FDreamUIViewModelRuntimeNewTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeNew"));
	UClass* Class = Dui.Compile(*this, {
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Settings = new"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Settings.Name"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Class == nullptr)
	{
		return false;
	}

	FScopedGameWorld TestWorld;
	UDreamTestViewModelWidgetBase* Instance = Cast<UDreamTestViewModelWidgetBase>(CreateDreamWidget(TestWorld.World, Class));
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	UDreamTestPlayerVM* Made = Cast<UDreamTestPlayerVM>(Instance->GetViewModel(TEXT("Settings")));
	if (!TestNotNull(TEXT("the entry holds one of its class"), Made))
	{
		return false;
	}
	TestTrue(TEXT("outered to the widget"), Made->GetOuter() == Instance);
	TestTrue(TEXT("made before On Initialized ran"), Instance->SeenAtInitialized == Made);
	TestFalse(TEXT("not a design-time instance in a game world"), Made->IsDesignTimeInstance());
	TestTrue(TEXT("with the widget's world"), Made->GetWorld() == TestWorld.World);
	TestEqual(TEXT("its binding is subscribed"), Instance->GetBindingCounts().Subscribed, 1);
	Made->SetName(FText::FromString(TEXT("Made")));
	TestEqual(TEXT("and follows it"), TextOf(Instance, TEXT("Title")), FString(TEXT("Made")));

	// Handed one before Initialize: that one stands, and On Initialized sees it.
	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Given = MakePlayer(TEXT("Given"));
	UDreamTestViewModelWidgetBase* Manual = NewObject<UDreamTestViewModelWidgetBase>(TestWorld.World, Class);
	TestTrue(TEXT("SetViewModel works before Initialize"), Manual->SetViewModel(TEXT("Settings"), Given.Get()));
	Manual->Initialize();
	TestTrue(TEXT("a given object is not replaced"), Manual->GetViewModel(TEXT("Settings")) == Given.Get());
	TestTrue(TEXT("and is what On Initialized saw"), Manual->SeenAtInitialized == Given.Get());
	TestEqual(TEXT("and what the first evaluation showed"), TextOf(Manual, TEXT("Title")), FString(TEXT("Given")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeGlobalTest,
	"DreamGUI.ViewModel.Runtime.AGlobalEntryIsFilledFromTheRegistryNowOrWhenRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelRuntimeGlobalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui ByClassDui(TEXT("BP_VMRuntimeGlobal"));
	UClass* ByClass = ByClassDui.Compile(*this, {
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player = global"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Player.Name"),
		TEXT("    }"),
		TEXT("}"),
	});
	FCompiledDui ByNameDui(TEXT("BP_VMRuntimeGlobalNamed"));
	UClass* ByName = ByNameDui.Compile(*this, {
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Stash = global \"Stash\""),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Stash.Name"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (ByClass == nullptr || ByName == nullptr)
	{
		return false;
	}

	DreamTests::FScopedGameInstanceWorld Game;
	UDreamViewModelSubsystem* Registry = UDreamViewModelSubsystem::Get(Game.World);
	if (!TestNotNull(TEXT("the game instance has a registry"), Registry))
	{
		return false;
	}
	UDreamTestCountingPlayerVM* Shared = NewObject<UDreamTestCountingPlayerVM>(Game.GameInstance);
	Shared->SetName(FText::FromString(TEXT("Shared")));
	Registry->Register(Shared);

	// Registered first: found at Initialize.
	UDreamUserWidget* Early = CreateDreamWidget(Game.World, ByClass);
	if (!TestNotNull(TEXT("the by-class widget instantiates"), Early))
	{
		return false;
	}
	TestTrue(TEXT("a global entry is filled from the registry"), Early->GetViewModel(TEXT("Player")) == Shared);
	TestEqual(TEXT("and shows it"), TextOf(Early, TEXT("Title")), FString(TEXT("Shared")));

	// Registered later: the widget waits, then fills.
	UDreamUserWidget* Waiting = CreateDreamWidget(Game.World, ByName);
	if (!TestNotNull(TEXT("the by-name widget instantiates"), Waiting))
	{
		return false;
	}
	TestNull(TEXT("an unnamed object does not fill a named entry"), Waiting->GetViewModel(TEXT("Stash")));
	TestEqual(TEXT("its text keeps what was authored"), TextOf(Waiting, TEXT("Title")), AuthoredTextOf(ByName, TEXT("Title")));

	UDreamTestCountingPlayerVM* Late = NewObject<UDreamTestCountingPlayerVM>(Game.GameInstance);
	Late->SetName(FText::FromString(TEXT("Late")));
	Registry->Register(Late, TEXT("Stash"));
	TestTrue(TEXT("registering one under its name fills the entry"), Waiting->GetViewModel(TEXT("Stash")) == Late);
	TestEqual(TEXT("and the text follows"), TextOf(Waiting, TEXT("Title")), FString(TEXT("Late")));
	TestTrue(TEXT("the other widget is untouched"), Early->GetViewModel(TEXT("Player")) == Shared);
	Late->SetName(FText::FromString(TEXT("Later")));
	TestEqual(TEXT("and it is subscribed like any other"), TextOf(Waiting, TEXT("Title")), FString(TEXT("Later")));

	UDreamTestCountingPlayerVM* Swapped = NewObject<UDreamTestCountingPlayerVM>(Game.GameInstance);
	Registry->Register(Swapped, TEXT("Stash"));
	TestTrue(TEXT("a filled entry stops waiting: a later swap in the registry is not followed"), Waiting->GetViewModel(TEXT("Stash")) == Late);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeParentTest,
	"DreamGUI.ViewModel.Runtime.AParentEntryFollowsTheEnclosingWidgetsViewModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A component declaring `= parent`, placed in a host that declares an entry of that class. The component initializes
 * inside its host, before the host has a view model or the component a parent; it finds the host anyway and follows
 * the host's entry from then on -- given, replaced, cleared.
 */
bool FDreamUIViewModelRuntimeParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui CardDui(TEXT("BP_VMRuntimeParentCard"));
	UClass* CardClass = CardDui.Compile(*this, {
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player = parent"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Player.Name"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (CardClass == nullptr)
	{
		return false;
	}
	FCompiledDui HostDui(TEXT("BP_VMRuntimeParentHost"));
	UClass* HostClass = HostDui.Compile(*this, {
		FString::Printf(TEXT("use \"%s\" as Card"), *CardDui.File.FilePath),
		TEXT("viewmodels {"),
		TEXT("    DreamTestPlayerVM Player"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Card Item {"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (HostClass == nullptr)
	{
		return false;
	}

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Host = CreateDreamWidget(TestWorld.World, HostClass);
	if (!TestNotNull(TEXT("the host instantiates"), Host))
	{
		return false;
	}
	UDreamUserWidget* Item = Cast<UDreamUserWidget>(Host->GetWidgetFromName(TEXT("Item")));
	if (!TestNotNull(TEXT("with the component in it"), Item))
	{
		return false;
	}
	TestNull(TEXT("the host has none yet, so neither has the component"), Item->GetViewModel(TEXT("Player")));

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> First = MakePlayer(TEXT("Party"));
	Host->SetViewModel(TEXT("Player"), First.Get());
	TestTrue(TEXT("the host's view model reaches the component"), Item->GetViewModel(TEXT("Player")) == First.Get());
	TestEqual(TEXT("whose binding shows it"), TextOf(Item, TEXT("Title")), FString(TEXT("Party")));
	First->SetName(FText::FromString(TEXT("Renamed")));
	TestEqual(TEXT("and follows it"), TextOf(Item, TEXT("Title")), FString(TEXT("Renamed")));

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Second = MakePlayer(TEXT("Second"));
	Host->SetViewModel(TEXT("Player"), Second.Get());
	TestTrue(TEXT("replaced on the host, replaced on the component"), Item->GetViewModel(TEXT("Player")) == Second.Get());
	TestEqual(TEXT("which shows the new one"), TextOf(Item, TEXT("Title")), FString(TEXT("Second")));
	TestEqual(TEXT("and lets the old one go"), First->LiveDelegateCount, 0);

	Host->SetViewModel(TEXT("Player"), nullptr);
	TestNull(TEXT("cleared on the host, cleared on the component"), Item->GetViewModel(TEXT("Player")));
	TestEqual(TEXT("whose text keeps the last value"), TextOf(Item, TEXT("Title")), FString(TEXT("Second")));
	TestEqual(TEXT("with nothing left on the view model"), Second->LiveDelegateCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeForcePollTest,
	"DreamGUI.ViewModel.Runtime.ForcePollPutsEveryRecordedBindingOnThePoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelRuntimeForcePollTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	IConsoleVariable* ForcePoll = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamUI.Binding.ForcePoll"));
	if (!TestNotNull(TEXT("DreamUI.Binding.ForcePoll exists"), ForcePoll))
	{
		return false;
	}
	FCompiledDui Dui(TEXT("BP_VMRuntimeForcePoll"));
	UClass* Class = Dui.Compile(*this, PlayerNameBody());
	if (Class == nullptr)
	{
		return false;
	}

	const int32 Previous = ForcePoll->GetInt();
	ForcePoll->Set(1, ECVF_SetByCode);
	ON_SCOPE_EXIT
	{
		ForcePoll->Set(Previous, ECVF_SetByCode);
	};

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("an announceable binding is polled while it is on"), Counts.Polled, 1);
	TestEqual(TEXT("and not subscribed"), Counts.Subscribed, 0);
	TestTrue(TEXT("the widget is polled"), Instance->HasPolledPropertyBindings());
	TArray<FString> Why;
	Instance->DescribePolledBindings(Why);
	TestTrue(TEXT("and says why"), ContainsLine(Why, TEXT("ForcePoll")));

	const TStrongObjectPtr<UDreamTestCountingPlayerVM> Player = MakePlayer(TEXT("One"));
	Instance->SetViewModel(TEXT("Player"), Player.Get());
	TestEqual(TEXT("nothing is subscribed on the view model"), Player->LiveDelegateCount, 0);
	Instance->EvaluatePolledPropertyBindings();
	TestEqual(TEXT("the poll shows the value"), TextOf(Instance, TEXT("Title")), FString(TEXT("One")));
	Player->Name = FText::FromString(TEXT("Unannounced"));
	Instance->EvaluatePolledPropertyBindings();
	TestEqual(TEXT("including a write nobody announced -- what the switch is for"), TextOf(Instance, TEXT("Title")), FString(TEXT("Unannounced")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeConstantTest,
	"DreamGUI.ViewModel.Runtime.AConstantBindingIsEvaluatedOnceAndNeverPolled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelRuntimeConstantTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeConstant"));
	UClass* Class = Dui.Compile(*this, {
		TEXT("Widget Root {"),
		TEXT("    Text Size {"),
		TEXT("        FontSize <- 12 + 4"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Class == nullptr)
	{
		return false;
	}
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("an expression that reads nothing is a constant"), Counts.Constant, 1);
	TestEqual(TEXT("not polled"), Counts.Polled, 0);
	TestEqual(TEXT("not subscribed"), Counts.Subscribed, 0);
	TestFalse(TEXT("so the widget is not polled"), Instance->HasPolledPropertyBindings());
	if (Manager != nullptr)
	{
		TestEqual(TEXT("and the manager polls no one"), Manager->GetPropertyBindingUserCount(), 0);
	}
	TestEqual(TEXT("its one evaluation happened"), FontSizeOf(Instance, TEXT("Size")), 16.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeNativeFieldTest,
	"DreamGUI.ViewModel.Runtime.ANativeFieldNotifyMemberOfACppParentIsSubscribed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A C++ parent's own FieldNotify member: its class compiles (UHT's descriptor needs FieldNotificationDeclaration.h through
 * DreamUserWidget.h), and `Text <- Headline` finds the field through that descriptor -- subscribed, updated by the C++
 * setter, never polled.
 */
bool FDreamUIViewModelRuntimeNativeFieldTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeNativeField"));
	UClass* Class = Dui.Compile(*this, {
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Headline"),
		TEXT("    }"),
		TEXT("}"),
	}, UDreamTestNativeNotifyWidget::StaticClass());
	if (Class == nullptr)
	{
		return false;
	}
	FScopedGameWorld TestWorld;
	UDreamTestNativeNotifyWidget* Instance = Cast<UDreamTestNativeNotifyWidget>(CreateDreamWidget(TestWorld.World, Class));
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	TestEqual(TEXT("a binding to the parent's FieldNotify member is subscribed"), Instance->GetBindingCounts().Subscribed, 1);
	TestFalse(TEXT("and not polled"), Instance->HasPolledPropertyBindings());
	Instance->SetHeadline(FText::FromString(TEXT("Native")));
	TestEqual(TEXT("the C++ setter's broadcast updates the text"), TextOf(Instance, TEXT("Title")), FString(TEXT("Native")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeWidgetVariableTest,
	"DreamGUI.ViewModel.Runtime.ABindingToAFieldNotifyVariableOfTheWidgetIsSubscribedNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The side effect the recorded dependencies have on code that never mentions a view model: `Text <- Caption`, Caption a
 * `props` variable (FieldNotify), used to be polled and is now subscribed -- a write announced on the widget updates
 * the text with no poll.
 */
bool FDreamUIViewModelRuntimeWidgetVariableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FCompiledDui Dui(TEXT("BP_VMRuntimeWidgetVariable"));
	UClass* Class = Dui.Compile(*this, {
		TEXT("props {"),
		TEXT("    Text Caption = \"Hello\""),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Text Title {"),
		TEXT("        Text <- Caption"),
		TEXT("    }"),
		TEXT("}"),
	});
	if (Class == nullptr)
	{
		return false;
	}
	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("a binding to a FieldNotify variable is subscribed"), Counts.Subscribed, 1);
	TestFalse(TEXT("and not polled"), Instance->HasPolledPropertyBindings());
	TestEqual(TEXT("the first evaluation shows the variable"), TextOf(Instance, TEXT("Title")), FString(TEXT("Hello")));

	const FTextProperty* Caption = FindFProperty<FTextProperty>(Class, TEXT("Caption"));
	if (TestNotNull(TEXT("the variable exists"), Caption))
	{
		Caption->SetPropertyValue_InContainer(Instance, FText::FromString(TEXT("World")));
		Instance->K2_BroadcastFieldValueChanged(FFieldNotificationId(TEXT("Caption")));
		TestEqual(TEXT("an announced write updates the text"), TextOf(Instance, TEXT("Title")), FString(TEXT("World")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeLegacyTest,
	"DreamGUI.ViewModel.Runtime.ABindingWithNoRecordedDependenciesWorksAsItAlwaysDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The designer's Bind button records no dependencies -- nor does any class compiled before they existed. Such a
 * binding is driven exactly as before: by its own FieldNotify field when it has one, else by the poll. IsInitialized is
 * a native function no broadcast carries, so this one is polled, and drives its property.
 */
bool FDreamUIViewModelRuntimeLegacyTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_VMRuntimeLegacy"), UDreamUserWidget::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was made"), Fixture.Blueprint))
	{
		return false;
	}
	UDreamWidgetTree* Tree = Fixture.Blueprint->GetOrCreateWidgetTree();
	UDreamWidget* Label = Tree->ConstructWidget<UDreamWidget>();
	Label->SetDisplayName(TEXT("Label"));
	Label->SetParentBeforeRegister(Tree->RootWidget.Get());
	UDreamText* Text = Cast<UDreamText>(Label->CreateNewVisual(UDreamText::StaticClass()));
	if (!TestNotNull(TEXT("the widget has a text visual"), Text))
	{
		return false;
	}
	Text->SetUseKerning(false);
	FDreamWidgetPropertyBinding& Binding = Fixture.Blueprint->PropertyBindings.AddDefaulted_GetRef();
	Binding.WidgetName = FName(TEXT("Label"));
	Binding.Target = EDreamWidgetBindingTarget::Visual;
	Binding.PropertyName = FName(TEXT("bUseKerning"));
	Binding.FunctionName = FName(TEXT("IsInitialized"));
	Fixture.Compile();
	TestEqual(*FString::Printf(TEXT("the binding compiles clean, saw [%s]"), *Fixture.JoinMessages()), Fixture.Results.NumErrors, 0);

	UDreamWidgetGeneratedClass* Class = Cast<UDreamWidgetGeneratedClass>(Fixture.GetClass());
	if (!TestNotNull(TEXT("a class came out"), Class) || !TestEqual(TEXT("carrying the binding"), Class->GetPropertyBindings().Num(), 1))
	{
		return false;
	}
	TestFalse(TEXT("with no dependencies recorded"), Class->GetPropertyBindings()[0].bDependenciesRecorded);

	FScopedGameWorld TestWorld;
	UDreamUserWidget* Instance = CreateDreamWidget(TestWorld.World, Class);
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	const UDreamUserWidget::FBindingCounts Counts = Instance->GetBindingCounts();
	TestEqual(TEXT("it is a legacy binding"), Counts.Legacy, 1);
	TestEqual(TEXT("on the poll, as before"), Counts.LegacyPolled, 1);
	TestEqual(TEXT("and none of the recorded kinds"), Counts.Subscribed + Counts.Polled + Counts.Constant, 0);
	TestTrue(TEXT("so the widget is polled"), Instance->HasPolledPropertyBindings());
	TArray<FString> Why;
	Instance->DescribePolledBindings(Why);
	TestEqual(TEXT("a legacy binding has no recorded reason to report"), Why.Num(), 0);

	UDreamText* LiveText = FindText(Instance, TEXT("Label"));
	if (TestNotNull(TEXT("the live widget has its text"), LiveText))
	{
		TestTrue(TEXT("the binding drove it"), LiveText->GetUseKerning());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelRuntimeAssignRouteTest,
	"DreamGUI.ViewModel.Runtime.AnAssignRouteBindsASingleCastDelegateReplacingItsListener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `Event = Handler` on a single-cast dynamic delegate (FDelegateProperty): the delegate holds one listener, and the
 * route becomes it -- replacing what was bound before (the host fixture binds HandleOther in its NativeOnInitialized,
 * ahead of the routes). The route record is set on the class by hand: what is under test is the run time's reading of
 * the event's property kind, not the builder's operator rules.
 */
bool FDreamUIViewModelRuntimeAssignRouteTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelRuntimeTestLocal;

	FScopedBlueprint Fixture(TEXT("BP_VMRuntimeAssignRoute"), UDreamTestRouteHostWidget::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was made"), Fixture.Blueprint))
	{
		return false;
	}
	UDreamWidgetTree* Tree = Fixture.Blueprint->GetOrCreateWidgetTree();
	UDreamTestSingleCastWidget* Probe = Tree->ConstructWidget<UDreamTestSingleCastWidget>();
	if (!TestNotNull(TEXT("the probe widget was made"), Probe))
	{
		return false;
	}
	Probe->SetDisplayName(TEXT("Probe"));
	Probe->SetParentBeforeRegister(Tree->RootWidget.Get());
	Fixture.Compile();
	TestEqual(*FString::Printf(TEXT("the Blueprint compiles clean, saw [%s]"), *Fixture.JoinMessages()), Fixture.Results.NumErrors, 0);

	UDreamWidgetGeneratedClass* Class = Cast<UDreamWidgetGeneratedClass>(Fixture.GetClass());
	if (!TestNotNull(TEXT("a class came out"), Class))
	{
		return false;
	}
	FDreamWidgetEventBinding Route;
	Route.WidgetName = FName(TEXT("Probe"));
	Route.Target = EDreamWidgetBindingTarget::Widget;
	Route.EventName = FName(TEXT("OnAsk"));
	Route.FunctionName = GET_FUNCTION_NAME_CHECKED(UDreamTestRouteHostWidget, HandleAsk);
	Class->SetEventBindings({ Route });

	FScopedGameWorld TestWorld;
	UDreamTestRouteHostWidget* Instance = Cast<UDreamTestRouteHostWidget>(CreateDreamWidget(TestWorld.World, Class));
	if (!TestNotNull(TEXT("the class instantiates"), Instance))
	{
		return false;
	}
	UDreamTestSingleCastWidget* LiveProbe = Cast<UDreamTestSingleCastWidget>(Instance->GetWidgetFromName(TEXT("Probe")));
	if (!TestNotNull(TEXT("the instance has the probe"), LiveProbe))
	{
		return false;
	}
	TestTrue(TEXT("the single-cast event is bound"), LiveProbe->OnAsk.IsBound());
	LiveProbe->Ask();
	TestEqual(TEXT("to the route's handler"), Instance->AskCount, 1);
	TestEqual(TEXT("which replaced what it held before"), Instance->OtherCount, 0);
	return true;
}

#endif
