// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamGUIEditorSubsystem.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIDocument.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace DreamUIImportedStyleWatcherTestLocal
{
	struct FScopedSources
	{
		FScopedSources()
		{
			Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			const FString Directory = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests")));
			MainPath = FPaths::Combine(Directory, TEXT("WatcherMain_") + Suffix + TEXT(".dui"));
			BridgePath = FPaths::Combine(Directory, TEXT("WatcherBridge_") + Suffix + TEXT(".dui"));
			StylesPath = FPaths::Combine(Directory, TEXT("WatcherStyles_") + Suffix + TEXT(".dui"));
			FPaths::NormalizeFilename(MainPath);
			FPaths::NormalizeFilename(BridgePath);
			FPaths::NormalizeFilename(StylesPath);
		}

		~FScopedSources()
		{
			for (const FString& File : { MainPath, BridgePath, StylesPath })
			{
				FDreamUISourceWatcher::NoteImports(File, TArray<FString>());
				IFileManager::Get().Delete(*File, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
			}
		}

		static bool Write(const FString& InPath, const FString& InText)
		{
			return IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/true)
				&& FFileHelper::SaveStringToFile(InText, *InPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		bool WriteStyle(float InOpacity) const
		{
			return Write(StylesPath, FString::Printf(TEXT("style Card {\n  RenderOpacity = %g\n}\n"), InOpacity));
		}

		FString Suffix;
		FString MainPath;
		FString BridgePath;
		FString StylesPath;
	};

	struct FScopedBlueprint
	{
		explicit FScopedBlueprint(const FString& InSuffix)
		{
			const FString Name = TEXT("WatcherImportedStyle_") + InSuffix;
			Package = CreatePackage(*(TEXT("/Temp/DreamGUITests/") + Name));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(*Name), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			Package->RemoveFromRoot();
		}

		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
	};

	bool CheckImportedStyleChange(FAutomationTestBase& InTest, bool bInTransitive, bool bInMainEvent = false)
	{
		if (!InTest.TestNotNull(TEXT("the real editor watcher is running"), UDreamGUIEditorSubsystem::Get()))
		{
			return false;
		}

		FScopedSources Sources;
		const FString ImportPath = bInTransitive ? Sources.BridgePath : Sources.StylesPath;
		const FString MainText = FString::Printf(
			TEXT("use \"%s\"\nWidget Root : Card { }\n"), *ImportPath);
		if (!InTest.TestTrue(TEXT("the style library was written"), Sources.WriteStyle(0.5f))
			|| !InTest.TestTrue(TEXT("the intermediate library was written"), FScopedSources::Write(
				Sources.BridgePath, FString::Printf(TEXT("use \"%s\"\n"), *Sources.StylesPath)))
			|| !InTest.TestTrue(TEXT("the main source was written"), FScopedSources::Write(Sources.MainPath, MainText)))
		{
			return false;
		}

		// Saved is outside the watched DUI roots, so deliver the intermediate library's creation
		// ourselves. This seeds its real direct import edge as a root scan or directory event would.
		if (bInTransitive)
		{
			FDreamUISourceWatcher::QueueFile(Sources.BridgePath);
			FDreamUISourceWatcher::FlushPending();
		}

		FScopedBlueprint Fixture(Sources.Suffix);
		if (!InTest.TestNotNull(TEXT("the text Blueprint was created"), Fixture.Blueprint)
			|| !InTest.TestTrue(TEXT("it compiled the real source and its imports"),
				DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, Sources.MainPath))
			|| !InTest.TestNotNull(TEXT("the initial tree was built"), Fixture.Blueprint->WidgetTree.Get())
			|| !InTest.TestNotNull(TEXT("the initial root was built"), Fixture.Blueprint->WidgetTree->RootWidget.Get()))
		{
			return false;
		}
		if (!InTest.TestEqual(TEXT("the initial imported style reached the Blueprint"),
			Fixture.Blueprint->WidgetTree->RootWidget->GetRenderOpacity(), 0.5f))
		{
			return false;
		}

		// Open the same registry document the designer uses. Its file remains unchanged throughout.
		FString OpenError;
		FDreamUIDocumentHandle Document = FDreamUIDocumentHandle::Open(Sources.MainPath, OpenError);
		if (!InTest.TestTrue(TEXT("the main document opened"), Document.IsValid())
			|| !InTest.TestTrue(TEXT("the unchanged main file matches the document's own-write hash"),
				Document.Get()->IsOwnWrite(MainText)))
		{
			return false;
		}

		// An original event for this unchanged file must still be suppressed.
		UDreamWidgetTree* const InitialTree = Fixture.Blueprint->WidgetTree.Get();
		FDreamUISourceWatcher::QueueFile(Sources.MainPath);
		FDreamUISourceWatcher::FlushPending();
		if (!InTest.TestEqual(TEXT("the main file's own event did not rebuild its tree"),
			Fixture.Blueprint->WidgetTree.Get(), InitialTree))
		{
			return false;
		}

		if (!InTest.TestTrue(TEXT("only the style library changed on disk"), Sources.WriteStyle(0.25f)))
		{
			return false;
		}
		if (bInMainEvent)
		{
			// The unchanged main file also delivered its own notification in this batch. It must
			// still rebuild when the external library event reaches it as a dependency.
			FDreamUISourceWatcher::QueueFile(Sources.MainPath);
		}
		FDreamUISourceWatcher::QueueFile(Sources.StylesPath);
		FDreamUISourceWatcher::FlushPending();

		FString MainOnDisk;
		InTest.TestTrue(TEXT("the main source can still be read"), FFileHelper::LoadFileToString(MainOnDisk, *Sources.MainPath));
		InTest.TestEqual(TEXT("the main source was never edited"), MainOnDisk, MainText);
		InTest.TestTrue(TEXT("its own-write hash still matches"), Document.Get()->IsOwnWrite(MainOnDisk));
		if (InTest.TestNotNull(TEXT("the Blueprint still has its root"), Fixture.Blueprint->WidgetTree->RootWidget.Get()))
		{
			InTest.TestEqual(TEXT("the watcher's real compile applied the changed imported style"),
				Fixture.Blueprint->WidgetTree->RootWidget->GetRenderOpacity(), 0.25f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIOpenDocumentImportedStyleRebuildsTest,
	"DreamGUI.Text.AnOpenDocumentRebuildsAfterAnImportedStyleChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIOpenDocumentImportedStyleRebuildsTest::RunTest(const FString&)
{
	return DreamUIImportedStyleWatcherTestLocal::CheckImportedStyleChange(*this, /*bInTransitive*/false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIOpenDocumentTransitiveStyleRebuildsTest,
	"DreamGUI.Text.AnOpenDocumentRebuildsAfterATransitiveStyleChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIOpenDocumentTransitiveStyleRebuildsTest::RunTest(const FString&)
{
	return DreamUIImportedStyleWatcherTestLocal::CheckImportedStyleChange(*this, /*bInTransitive*/true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIOpenDocumentMixedStyleEventsRebuildTest,
	"DreamGUI.Text.AnOwnWriteEventDoesNotHideAChangedImportedStyleInTheSameBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIOpenDocumentMixedStyleEventsRebuildTest::RunTest(const FString&)
{
	return DreamUIImportedStyleWatcherTestLocal::CheckImportedStyleChange(
		*this, /*bInTransitive*/true, /*bInMainEvent*/true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIOwnWriteRebuildsStyleImportersTest,
	"DreamGUI.Text.ADocumentsOwnWriteRebuildsItsStyleImporters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIOwnWriteRebuildsStyleImportersTest::RunTest(const FString&)
{
	using namespace DreamUIImportedStyleWatcherTestLocal;

	if (!TestNotNull(TEXT("the real editor watcher is running"), UDreamGUIEditorSubsystem::Get()))
	{
		return false;
	}
	FScopedSources Sources;
	const FString MainText = FString::Printf(
		TEXT("use \"%s\"\nWidget Root : Card { }\n"), *Sources.StylesPath);
	if (!TestTrue(TEXT("the style library was written"), Sources.WriteStyle(0.5f))
		|| !TestTrue(TEXT("the importer was written"), FScopedSources::Write(Sources.MainPath, MainText)))
	{
		return false;
	}
	FScopedBlueprint Fixture(Sources.Suffix);
	if (!TestNotNull(TEXT("the text Blueprint was created"), Fixture.Blueprint)
		|| !TestTrue(TEXT("the importer compiled"), DreamUITextAuthoring::SetAuthoredSourcePath(Fixture.Blueprint, Sources.MainPath))
		|| !TestNotNull(TEXT("the importer has its tree"), Fixture.Blueprint->WidgetTree.Get())
		|| !TestNotNull(TEXT("the importer has its root"), Fixture.Blueprint->WidgetTree->RootWidget.Get()))
	{
		return false;
	}
	TestEqual(TEXT("the initial style was applied"), Fixture.Blueprint->WidgetTree->RootWidget->GetRenderOpacity(), 0.5f);
	TestNull(TEXT("the importer has no open document that could suppress a rebuild"), FDreamUIDocumentRegistry::Find(Sources.MainPath));

	FString OpenError;
	FDreamUIDocumentHandle LibraryDocument = FDreamUIDocumentHandle::Open(Sources.StylesPath, OpenError);
	if (!TestTrue(TEXT("the library document opened"), LibraryDocument.IsValid()))
	{
		return false;
	}
	const FString NewStyleText(TEXT("style Card {\n  RenderOpacity = 0.25\n}\n"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Edit the style document")));
		FString WriteError;
		if (!TestTrue(TEXT("the document performed a real new write"), LibraryDocument.Get()->SetContent(NewStyleText, WriteError)))
		{
			return false;
		}
	}
	TestFalse(TEXT("the new document text reached disk"), LibraryDocument.Get()->HasUnflushedWrite());
	TestTrue(TEXT("the new file is the document's own write"), LibraryDocument.Get()->IsOwnWrite(NewStyleText));

	UDreamWidgetTree* const InitialTree = Fixture.Blueprint->WidgetTree.Get();
	FDreamUISourceWatcher::QueueFile(Sources.StylesPath);
	FDreamUISourceWatcher::FlushPending();
	TestTrue(TEXT("the library's own write rebuilt its importer"), Fixture.Blueprint->WidgetTree.Get() != InitialTree);
	TestEqual(TEXT("the importer applied the library's newly written style"),
		Fixture.Blueprint->WidgetTree->RootWidget->GetRenderOpacity(), 0.25f);
	TestTrue(TEXT("the library document still identifies its own write"), LibraryDocument.Get()->IsOwnWrite(NewStyleText));
	return true;
}

#endif
