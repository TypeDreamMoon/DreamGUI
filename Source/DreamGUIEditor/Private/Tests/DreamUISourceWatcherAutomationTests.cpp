// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * Save a .dui, and the classes built from it recompile.
 *
 * Driven through QueueFile/FlushPending rather than by touching a file and waiting for the OS to
 * notice. That is deliberate and it is not a shortcut: what these assert is the DRAIN -- which files
 * map to which classes, what gets suppressed, what gets compiled -- and every one of those answers
 * is the same whether the queue was filled by a directory event or by a menu command. Waiting on
 * DirectoryWatcher would add the one part of this that cannot be made deterministic, in exchange for
 * testing a callback that does nothing but filter on an extension.
 *
 * The file lives in Saved/ rather than a DUI root, so a test run cannot leave a source behind for
 * the real watcher to react to, and cannot have the real watcher react to the test.
 */

namespace DreamUISourceWatcherTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
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
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		/** One widget under the root, named InWidgetName. Enough to tell one build from another. */
		bool WriteWith(const TCHAR* InWidgetName) const
		{
			const FString Content = FString::Printf(
				TEXT("Widget Root {\n  + CanvasPanel { }\n  Text %s { }\n}\n"), InWidgetName);
			return FFileHelper::SaveStringToFile(Content, *FilePath);
		}

		FString FilePath;
	};

	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedTextBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedTextBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedTextBlueprint(const FScopedTextBlueprint&) = delete;
		FScopedTextBlueprint& operator=(const FScopedTextBlueprint&) = delete;

		UDreamWidget* FindTemplate(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				const FString Wanted(InDisplayName);
				Blueprint->WidgetTree->ForEachWidget([&Found, &Wanted](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == Wanted)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherSavedSourceRebuildsItsClassTest,
	"DreamGUI.Text.ASavedSourceRebuildsItsClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWatcherSavedSourceRebuildsItsClassTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherRebuild.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}

	FScopedTextBlueprint Fixture(TEXT("WatcherRebuild"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("which built the file's hierarchy"), Fixture.FindTemplate(TEXT("Title"))))
	{
		return false;
	}

	// The class is found from the file, and the two are spelled differently: the Blueprint stores
	// whatever MakePortablePath made of the picked path, and the watcher only ever has the absolute
	// one. Comparing the spellings instead of the resolved paths would make a rebuild depend on how
	// the class happened to name its file.
	TArray<UDreamWidgetBlueprint*> Found;
	FDreamUISourceWatcher::FindBlueprintsForSource(File.FilePath, Found);
	TestTrue(TEXT("the file finds its class"), Found.Contains(Fixture.Blueprint));

	// The save.
	if (!TestTrue(TEXT("the file was rewritten"), File.WriteWith(TEXT("Renamed"))))
	{
		return false;
	}
	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	TestNotNull(TEXT("the class rebuilt from the new text"), Fixture.FindTemplate(TEXT("Renamed")));
	TestNull(TEXT("and the widget the old text named is gone"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWatcherOwnWriteDoesNotRebuildTest,
	"DreamGUI.Text.TheDesignersOwnWriteDoesNotRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The loop this closes is not hypothetical: the designer writes the file, the watcher sees a change,
 * the class rebuilds, the rebuild replaces the tree the designer is mirroring, and the next flush
 * writes again. It would run for as long as the editor was open.
 *
 * Asserted by leaving a mark that only a rebuild could erase. A test that checked "the file was not
 * re-read" would pass on a version that re-read it and compiled anyway.
 */
bool FDreamUIWatcherOwnWriteDoesNotRebuildTest::RunTest(const FString&)
{
	using namespace DreamUISourceWatcherTestLocal;

	FScopedDuiFile File(TEXT("WatcherOwnWrite.dui"));
	if (!TestTrue(TEXT("the .dui was written"), File.WriteWith(TEXT("Title"))))
	{
		return false;
	}

	FScopedTextBlueprint Fixture(TEXT("WatcherOwnWrite"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("and points at the file"),
			DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, File.FilePath)))
	{
		return false;
	}

	// A document open on the file is what the designer has, and its hash is what makes the
	// suppression answerable: with no document there is nothing of ours for a foreign edit to be
	// mistaken for, which is exactly the case the test above covers.
	FString OpenError;
	FDreamUIDocumentHandle Document = FDreamUIDocumentHandle::Open(File.FilePath, OpenError);
	if (!TestTrue(TEXT("a document opened on the file"), Document.IsValid()))
	{
		return false;
	}

	// The mark. Renaming the template is not something a rebuild would preserve -- the tree is
	// rebuilt from the file, and the file still says Title.
	UDreamWidget* Title = Fixture.FindTemplate(TEXT("Title"));
	if (!TestNotNull(TEXT("the built hierarchy is there to mark"), Title))
	{
		return false;
	}
	Title->SetDisplayName(TEXT("Sentinel"));

	FDreamUISourceWatcher::QueueFile(File.FilePath);
	FDreamUISourceWatcher::FlushPending();

	TestNotNull(TEXT("the class was left alone"), Fixture.FindTemplate(TEXT("Sentinel")));
	TestNull(TEXT("so no rebuild put the file's name back"), Fixture.FindTemplate(TEXT("Title")));
	return true;
}

#endif
