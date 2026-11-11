// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"

#include "Driver/Designer/DreamDesignerDriver.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetBlueprint.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/*
 * A designer on a class whose hierarchy is a .dui on disk, for the tests of what a designer gesture writes into the file,
 * and the undo stack's length and last step, for telling whether a gesture left a step behind.
 */
namespace DreamTests
{
	/** The undo stack as it stands: how long it is and which step is last, which between them change whenever a step is added. */
	struct FUndoMark
	{
		int32 Length = 0;
		const FTransaction* Last = nullptr;

		static FUndoMark Now()
		{
			FUndoMark Mark;
			if (GEditor != nullptr && GEditor->Trans != nullptr)
			{
				Mark.Length = GEditor->Trans->GetQueueLength();
				Mark.Last = Mark.Length > 0 ? GEditor->Trans->GetTransaction(Mark.Length - 1) : nullptr;
			}
			return Mark;
		}

		bool operator==(const FUndoMark& InOther) const
		{
			return Length == InOther.Length && Last == InOther.Last;
		}
	};

	/**
	 * A Widget Blueprint whose hierarchy is a .dui on disk, with its designer open and sized: the class a text author makes
	 * and opens. The file is under Saved/, where no DUI root is and so no source watcher looks, and the package under
	 * /Temp, both with a fresh suffix. The designer closes first, a Slate tick lets the close happen, and only then do the
	 * asset and the file go -- the order FScopedDesignerSession keeps, for its reason.
	 */
	struct FScopedDuiDesigner
	{
		FString FilePath;
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		TSharedPtr<FDreamDesignerDriver> Driver;
		FString Failure;

		FScopedDuiDesigner(const TCHAR* InName, const TArray<FString>& InLines)
		{
			const FString Unique = FString::Printf(TEXT("%s_%s"), InName, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), Unique + TEXT(".dui")));
			FPaths::NormalizeFilename(FilePath);
			if (!FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath))
			{
				Failure = FString::Printf(TEXT("the .dui could not be written to %s"), *FilePath);
				return;
			}
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), *Unique));
			if (Package == nullptr)
			{
				Failure = TEXT("the package could not be created");
				return;
			}
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				Failure = TEXT("the Widget Blueprint could not be created");
				return;
			}
			// The default canvas, picked rather than left to default: a root with a size of its own would otherwise bring
			// the canvas down to that size once the file is read, and these tests place and drag against the full one.
			// Set before the path, which compiles.
			Blueprint->DesignerData.DesignViewportSize = FDreamWidgetDesignerData().CanvasSize;
			if (!DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, FilePath))
			{
				Failure = TEXT("the Widget Blueprint would not take the .dui as its source");
				return;
			}
			TSharedPtr<FDreamDesignerDriver> Opened = FDreamDesignerDriver::Open(Blueprint);
			if (!Opened.IsValid())
			{
				Failure = TEXT("the designer did not open, or opened without a viewport (see LogDreamDesignerDriver)");
				return;
			}
			if (!Opened->EnsureHeadlessSize(FIntPoint(1280, 720)))
			{
				Failure = FString::Printf(TEXT("the designer viewport could not be given a size; it measures %dx%d"),
					Opened->ViewportPixelSize().X, Opened->ViewportPixelSize().Y);
				Opened->Close();
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().Tick();
				}
				return;
			}
			Opened->PumpFrame();
			Driver = Opened;
		}

		~FScopedDuiDesigner()
		{
			if (Driver.IsValid())
			{
				Driver->Close();
				Driver.Reset();
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().Tick();
				}
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
			if (!FilePath.IsEmpty())
			{
				// Quiet, and even read-only: a file left behind is read by the next run of this test.
				IFileManager::Get().Delete(*FilePath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
			}
		}

		FScopedDuiDesigner(const FScopedDuiDesigner&) = delete;
		FScopedDuiDesigner& operator=(const FScopedDuiDesigner&) = delete;

		bool IsReady() const
		{
			return Driver.IsValid();
		}

		/** The file as it is on disk now, which is what a reviewer, a diff and the next compile read. */
		FString ReadFile() const
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *FilePath);
			return Text;
		}

		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && ::IsValid(Blueprint->WidgetTree))
			{
				Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* InWidget)
				{
					if (Found == nullptr && InWidget->GetDisplayName() == InDisplayName)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
