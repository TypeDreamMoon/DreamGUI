// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

/*
 * The fixtures every test of structure on a `.dui`-backed class uses: a file that has really been to disk, a
 * text-authored widget blueprint on it (with a designer, or without one), and the few questions asked of a parse.
 * Shared so the tests of each kind of structural edit differ in the edit and in nothing else.
 */
namespace DreamUITextStructureFixtures
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
			FileName = FPaths::GetCleanFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		/** What is on the disk now -- the thing the next compile will read. */
		FString Read() const
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *FilePath);
			return Text;
		}

		FString FilePath;
		FString FileName;
	};

	/** A text-authored widget blueprint on a .dui, of InParentClass, with a designer open on it unless asked otherwise. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;
		int32 CompileErrors = 0;

		FScopedTextBlueprint(const TCHAR* InName, const FString& InDuiFilePath, bool bInOpenDesigner = true,
			UClass* InParentClass = UDreamTextUserWidget::StaticClass())
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}
			if (UDreamTextUserWidget* Defaults = Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
			{
				Defaults->SourceFile.FilePath = InDuiFilePath;
			}
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			CompileErrors = Results.NumErrors;

			if (bInOpenDesigner)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
				Designer = static_cast<FDreamWidgetBlueprintEditor*>(
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
			}
		}

		~FScopedTextBlueprint()
		{
			if (GEditor != nullptr && Blueprint != nullptr && Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedTextBlueprint(const FScopedTextBlueprint&) = delete;
		FScopedTextBlueprint& operator=(const FScopedTextBlueprint&) = delete;

		/** By name in the Blueprint's CURRENT tree: a structural edit's compile replaces the tree object. */
		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}

		UDreamWidget* FindPreview(const FString& InDisplayName) const
		{
			UDreamWidget* Template = FindTemplate(InDisplayName);
			return Designer != nullptr && Designer->GetPreviewHost().IsValid() && Template != nullptr
				? Designer->GetPreviewHost()->FindPreviewForTemplate(Template) : nullptr;
		}

		int32 TemplateCount() const
		{
			return Blueprint != nullptr && IsValid(Blueprint->WidgetTree) ? Blueprint->WidgetTree->CountWidgets() : 0;
		}
	};

	/** The node with this id in a parse, or null. */
	inline const FDreamUINode* FindNode(const FDreamUIAst& InAst, const FString& InId)
	{
		const FDreamUINode* Found = nullptr;
		InAst.ForEachNode([&Found, &InId](const FDreamUINode& InNode)
		{
			if (Found == nullptr && InNode.Id == InId)
			{
				Found = &InNode;
			}
		});
		return Found;
	}

	inline bool ParseText(const FString& InText, FDreamUIAst& OutAst)
	{
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("Structure.dui");
		return FDreamUISourceFile::Parse(InText, Diagnostics.SourceName, OutAst, Diagnostics);
	}

	inline bool HoldsChild(const FDreamUINode* InParent, const FString& InChildId)
	{
		return InParent != nullptr && InParent->Children.ContainsByPredicate(
			[&InChildId](const FDreamUINode& InChild) { return InChild.Id == InChildId; });
	}

	/** The ids of a node's widget children, in the order the file writes them. */
	inline TArray<FString> ChildIds(const FDreamUINode* InParent)
	{
		TArray<FString> Ids;
		if (InParent != nullptr)
		{
			for (const FDreamUINode& Child : InParent->Children)
			{
				if (!Child.Id.IsEmpty())
				{
					Ids.Add(Child.Id);
				}
			}
		}
		return Ids;
	}
}

#endif
