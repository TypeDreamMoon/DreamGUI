// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamForLoopTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetBlueprint.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIEachBindingHandler.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetGeneratedClass.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

/*
 * A `for` end to end: two real .dui files compiled into two classes, one repeating the other, and the widget
 * created from the outer one in a game world. What only this can show is the route the smaller tests stand
 * either side of: the builder's record reaching the generated class through the compiler, UDreamUserWidget::
 * ResolveEachBindings handing it to the core's adapter rather than to a list view -- with the list views' module
 * taken out of the picture to prove it -- and a copy that is a compiled component running its own bindings
 * after the item was written into it.
 */

namespace DreamUIForLoopCompileTestLocal
{
	using DreamTests::FScopedGameWorld;

	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
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

	/** A .dui-backed Blueprint in /Temp, deriving from InParentClass, pointed at InFilePath and compiled. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FCompilerResultsLog Results;

		FScopedTextBlueprint(const TCHAR* InName, UClass* InParentClass, const FString& InFilePath)
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
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
	};

	UDreamForLoopTestItem* MakeItem(UObject* InOuter, const TCHAR* InLabel)
	{
		UDreamForLoopTestItem* Item = NewObject<UDreamForLoopTestItem>(InOuter);
		Item->Label = FText::FromString(InLabel);
		return Item;
	}

	/** InRoot's children carrying the template's name -- the template itself and every copy of it. */
	int32 CountNamed(const UDreamWidget* InRoot, const TCHAR* InDisplayName)
	{
		int32 Count = 0;
		for (const UDreamWidget* Child : InRoot->GetChildren())
		{
			if (IsValid(Child) && Child->GetDisplayName() == InDisplayName)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIForCompiledTest,
	"DreamGUI.Binding.For.ACompiledForMakesItsCopiesInTheWidgetItselfWithNoListViewModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIForCompiledTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIForLoopCompileTestLocal;

	// The component: a row whose own binding shows the property a `for` writes. Caption has no setter -- the shape
	// a `props` variable compiles into -- and GetCaptionText is how the row's own file reads it.
	FScopedDuiFile RowFile(TEXT("ForLoopRowFixture.dui"));
	if (!TestTrue(TEXT("the row's file was written"), RowFile.Write({
		TEXT("class /Temp/DreamGUITests/BP_ForLoopRow"),
		// A Text, not a bare Widget: a root with nothing on it is the designer's unauthored placeholder, and the class
		// would inherit its parent's (empty) hierarchy instead of this one.
		TEXT("Text Face {"),
		TEXT("    ToolTipText <- GetCaptionText()"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedTextBlueprint Row(TEXT("BP_ForLoopRow"), UDreamForLoopTestRow::StaticClass(), RowFile.FilePath);
	if (!TestNotNull(TEXT("the row's Blueprint was made"), Row.GetClass()))
	{
		return false;
	}
	TestEqual(TEXT("the row compiles clean"), Row.Results.NumErrors, 0);

	// The screen: a header, one row per option where the `for` is written, a footer.
	FScopedDuiFile HostFile(TEXT("ForLoopHostFixture.dui"));
	if (!TestTrue(TEXT("the host's file was written"), HostFile.Write({
		TEXT("class /Temp/DreamGUITests/BP_ForLoopHost"),
		TEXT("Widget Root {"),
		TEXT("    Widget Header {"),
		TEXT("    }"),
		TEXT("    for Option in GetOptions() {"),
		TEXT("        /Temp/DreamGUITests/BP_ForLoopRow Choice {"),
		TEXT("            Caption <- Option.Label"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Widget Footer {"),
		TEXT("    }"),
		TEXT("}")})))
	{
		return false;
	}
	FScopedTextBlueprint Host(TEXT("BP_ForLoopHost"), UDreamForLoopTestHost::StaticClass(), HostFile.FilePath);
	if (!TestNotNull(TEXT("the host's Blueprint was made"), Host.GetClass()))
	{
		return false;
	}
	TestEqual(TEXT("the host compiles clean"), Host.Results.NumErrors, 0);

	// The record reached the class, through the same array an `each` uses and the same source checks.
	TArray<FDreamWidgetEachBinding> Loops;
	UDreamWidgetGeneratedClass::CollectEachBindings(Host.GetClass(), Loops);
	if (!TestEqual(TEXT("the class carries the loop"), Loops.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("as a 'for'"), Loops[0].bInPanel);
	if (TestEqual(TEXT("with its item write"), Loops[0].EntryBindings.Num(), 1))
	{
		TestEqual(TEXT("onto the row's property"), Loops[0].EntryBindings[0].PropertyName, FName(TEXT("Caption")));
		TestTrue(TEXT("which has no setter"), Loops[0].EntryBindings[0].SetterName.IsNone());
	}

	// A `for` is the core's: with the list views' handler gone, every `each` would bind nothing, and this must
	// still make its copies.
	IDreamUIEachBindingHandler* Registered = DreamUI::GetEachBindingHandler();
	DreamUI::SetEachBindingHandler(nullptr);
	ON_SCOPE_EXIT
	{
		DreamUI::SetEachBindingHandler(Registered);
	};

	FScopedGameWorld TestWorld;
	UDreamForLoopTestHost* Instance = Cast<UDreamForLoopTestHost>(CreateDreamWidget(TestWorld.World, Host.GetClass()));
	if (!TestNotNull(TEXT("the host instantiates"), Instance))
	{
		return false;
	}
	UDreamWidget* Root = Instance->GetWidgetFromName(TEXT("Root"));
	UDreamWidget* Template = Instance->GetWidgetFromName(TEXT("Choice"));
	UDreamWidget* Header = Instance->GetWidgetFromName(TEXT("Header"));
	UDreamWidget* Footer = Instance->GetWidgetFromName(TEXT("Footer"));
	if (!TestNotNull(TEXT("the root is there"), Root) || !TestNotNull(TEXT("and the template"), Template))
	{
		Instance->DestroyWidget();
		return false;
	}
	// Resolved at creation even with nothing in the source yet: the template is out of the way from the first frame.
	TestEqual(TEXT("the template is collapsed from creation"), Template->GetVisibility(), EDreamWidgetVisibility::Collapsed);
	TestEqual(TEXT("and nothing was copied from an empty source"), CountNamed(Root, TEXT("Choice")), 1);

	UDreamForLoopTestItem* First = MakeItem(Instance, TEXT("First"));
	UDreamForLoopTestItem* Second = MakeItem(Instance, TEXT("Second"));
	Instance->Options = { First, Second };
	// A function source has nothing to broadcast, so the code that changed it asks.
	Instance->RefreshEachBindings();

	const TArray<UDreamWidget*> Children = Root->GetChildren();
	if (TestEqual(TEXT("the root holds its three widgets and two copies"), Children.Num(), 5))
	{
		TestEqual(TEXT("Header first"), Children[0], Header);
		TestEqual(TEXT("then the template"), Children[1], Template);
		TestEqual(TEXT("Footer last"), Children[4], Footer);
		const TCHAR* Labels[] = { TEXT("First"), TEXT("Second") };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const UDreamForLoopTestRow* Copy = Cast<UDreamForLoopTestRow>(Children[2 + Index]);
			if (!TestNotNull(FString::Printf(TEXT("copy %d is a row"), Index), Copy))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("copy %d is of the compiled row class"), Index), Copy->IsA(Row.GetClass()));
			TestTrue(FString::Printf(TEXT("copy %d was initialized as a row of its own"), Index), Copy->IsInitialized());
			TestEqual(FString::Printf(TEXT("copy %d holds its item"), Index), Copy->Caption.ToString(), FString(Labels[Index]));
			// The row's own `ToolTipText <- GetCaptionText()`, run again after the write: the item is not only in the
			// property, it is on screen.
			const UDreamWidget* Face = Copy->GetContentRoot();
			if (TestNotNull(FString::Printf(TEXT("copy %d has its own contents"), Index), Face))
			{
				TestEqual(FString::Printf(TEXT("copy %d's own binding shows the item"), Index),
					Face->GetToolTipText().ToString(), FString(Labels[Index]));
			}
		}
	}

	// A copy of the whole widget -- a cell of a list made from this screen -- brings its copies along. Its own `for`
	// takes those over rather than adding a second set beside them.
	UDreamUserWidget* Duplicate = Cast<UDreamUserWidget>(DuplicateDreamWidgetHierarchy(Instance->GetOuter(), Instance, nullptr));
	if (TestNotNull(TEXT("the screen duplicates"), Duplicate))
	{
		const UDreamWidget* DuplicateRoot = Duplicate->GetWidgetFromName(TEXT("Root"));
		if (TestNotNull(TEXT("with a root of its own"), DuplicateRoot))
		{
			TestTrue(TEXT("which is not the source's"), DuplicateRoot != Root);
			TestEqual(TEXT("holding the template and one copy per item, not two"), CountNamed(DuplicateRoot, TEXT("Choice")), 3);
		}
		Duplicate->DestroyWidget();
	}
	TestEqual(TEXT("and the source keeps its own"), CountNamed(Root, TEXT("Choice")), 3);

	// Fewer items through the same route.
	Instance->Options = { Second };
	Instance->RefreshEachBindings();
	TestEqual(TEXT("a refresh with fewer items leaves fewer copies"), CountNamed(Root, TEXT("Choice")), 2);
	Instance->DestroyWidget();

	// The designer's preview, an edit world: no copies, and the template stands as written for the author to see.
	FScopedGameWorld EditWorld(EWorldType::Editor);
	UDreamForLoopTestHost* Preview = Cast<UDreamForLoopTestHost>(CreateDreamWidget(EditWorld.World, Host.GetClass()));
	if (TestNotNull(TEXT("the host instantiates in an edit world"), Preview))
	{
		UDreamWidget* PreviewTemplate = Preview->GetWidgetFromName(TEXT("Choice"));
		if (TestNotNull(TEXT("with its template"), PreviewTemplate))
		{
			Preview->Options = { MakeItem(Preview, TEXT("Shown")) };
			Preview->RefreshEachBindings();
			TestEqual(TEXT("which is left visible"), PreviewTemplate->GetVisibility(), EDreamWidgetVisibility::Visible);
			TestEqual(TEXT("and copied nowhere"), CountNamed(PreviewTemplate->GetParent(), TEXT("Choice")), 1);
		}
		Preview->DestroyWidget();
	}
	return true;
}

#endif
