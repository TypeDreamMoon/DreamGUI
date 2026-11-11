// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIWorkspaceService.h"

#include "DreamGUIEditorModule.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUISymbolExport.h"

#include "Dom/JsonObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace DreamUIWorkspaceLocal
{
	// ---- launching, the DreamShader launcher's shape re-spoken here (shapes shared, packages
	// not: the three plugins are independent repos on purpose) --------------------------------

	FString QuoteProcessArgument(const FString& InArgument)
	{
		FString Escaped = InArgument;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	void AddExistingFileCandidate(TArray<FString>& OutCandidates, const FString& InCandidate)
	{
		if (!InCandidate.IsEmpty() && FPaths::FileExists(InCandidate))
		{
			OutCandidates.AddUnique(InCandidate);
		}
	}

	TArray<FString> FindVSCodeExecutableCandidates()
	{
		TArray<FString> Candidates;
		auto AddFromEnvironmentDirectory = [&Candidates](const TCHAR* InVariable, const TCHAR* InRelative)
		{
			const FString Directory = FPlatformMisc::GetEnvironmentVariable(InVariable);
			if (!Directory.IsEmpty())
			{
				AddExistingFileCandidate(Candidates, FPaths::Combine(Directory, InRelative));
			}
		};

		AddFromEnvironmentDirectory(TEXT("LOCALAPPDATA"), TEXT("Programs/Microsoft VS Code/Code.exe"));
		AddFromEnvironmentDirectory(TEXT("LOCALAPPDATA"), TEXT("Programs/Microsoft VS Code/bin/code.cmd"));
		AddFromEnvironmentDirectory(TEXT("ProgramFiles"), TEXT("Microsoft VS Code/Code.exe"));
		AddFromEnvironmentDirectory(TEXT("ProgramFiles"), TEXT("Microsoft VS Code/bin/code.cmd"));
		AddFromEnvironmentDirectory(TEXT("ProgramFiles(x86)"), TEXT("Microsoft VS Code/Code.exe"));
		AddFromEnvironmentDirectory(TEXT("ProgramFiles(x86)"), TEXT("Microsoft VS Code/bin/code.cmd"));

		const FString PathEnvironment = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
		TArray<FString> PathEntries;
		PathEnvironment.ParseIntoArray(PathEntries, TEXT(";"), true);
		for (FString PathEntry : PathEntries)
		{
			PathEntry.TrimStartAndEndInline();
			if (PathEntry.IsEmpty())
			{
				continue;
			}
			AddExistingFileCandidate(Candidates, FPaths::Combine(PathEntry, TEXT("code.cmd")));
			AddExistingFileCandidate(Candidates, FPaths::Combine(PathEntry, TEXT("Code.exe")));
		}
		return Candidates;
	}

	bool LaunchVSCodeWorkspace(const FString& InWorkspaceFilePath)
	{
		for (const FString& Candidate : FindVSCodeExecutableCandidates())
		{
			FProcHandle ProcessHandle;
			if (Candidate.EndsWith(TEXT(".cmd"), ESearchCase::IgnoreCase))
			{
				FString CmdExe = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
				if (CmdExe.IsEmpty())
				{
					CmdExe = TEXT("C:/Windows/System32/cmd.exe");
				}
				const FString Parameters = FString::Printf(TEXT("/C \"\"%s\" %s\""),
					*Candidate, *QuoteProcessArgument(InWorkspaceFilePath));
				ProcessHandle = FPlatformProcess::CreateProc(*CmdExe, *Parameters, true, true, true, nullptr, 0, nullptr, nullptr);
			}
			else
			{
				ProcessHandle = FPlatformProcess::CreateProc(*Candidate,
					*QuoteProcessArgument(InWorkspaceFilePath), true, false, false, nullptr, 0, nullptr, nullptr);
			}
			if (ProcessHandle.IsValid())
			{
				FPlatformProcess::CloseProc(ProcessHandle);
				return true;
			}
		}
		return false;
	}

	bool LaunchTextFileWithNotepad(const FString& InFilePath)
	{
		TArray<FString> Candidates;
		const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
		AddExistingFileCandidate(Candidates, FPaths::Combine(SystemRoot, TEXT("System32/notepad.exe")));
		Candidates.Add(TEXT("notepad.exe"));

		for (const FString& Candidate : Candidates)
		{
			FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*Candidate,
				*QuoteProcessArgument(InFilePath), true, false, false, nullptr, 0, nullptr, nullptr);
			if (ProcessHandle.IsValid())
			{
				FPlatformProcess::CloseProc(ProcessHandle);
				return true;
			}
		}
		return false;
	}

	void Notify(const FText& InMessage, const bool bSuccess)
	{
		FNotificationInfo Info(InMessage);
		Info.ExpireDuration = 4.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

FString FDreamUIWorkspaceService::BuildWorkspaceJson(const TArray<FDreamUISourceRoot>& InRoots,
	const FString& InWorkspaceDirectory)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Folders;
	for (const FDreamUISourceRoot& SourceRoot : InRoots)
	{
		TSharedPtr<FJsonObject> Folder = MakeShared<FJsonObject>();
		if (SourceRoot.RootToken.IsEmpty())
		{
			// The workspace file lives in the project's DUI/ itself: "." keeps VSCode's folder
			// identity (and per-folder settings) stable however the project directory moves.
			Folder->SetStringField(TEXT("name"), TEXT("DreamUI Source"));
			Folder->SetStringField(TEXT("path"), TEXT("."));
		}
		else
		{
			FString FolderPath = SourceRoot.Directory;
			FPaths::NormalizeDirectoryName(FolderPath);
			FString Relative = FolderPath;
			// A root on another drive has no relative form on Windows; an absolute path is still
			// a valid workspace folder.
			if (!FPaths::MakePathRelativeTo(Relative, *(InWorkspaceDirectory + TEXT("/"))))
			{
				Relative = FolderPath;
			}
			Folder->SetStringField(TEXT("name"), FString::Printf(TEXT("Plugin: %s"), *SourceRoot.RootToken));
			Folder->SetStringField(TEXT("path"), Relative);
		}
		Folders.Add(MakeShared<FJsonValueObject>(Folder));
	}
	Root->SetArrayField(TEXT("folders"), Folders);

	TSharedPtr<FJsonObject> Associations = MakeShared<FJsonObject>();
	Associations->SetStringField(TEXT("*.dui"), TEXT("dui"));
	TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
	Settings->SetObjectField(TEXT("files.associations"), Associations);
	Root->SetObjectField(TEXT("settings"), Settings);

	// The one line that closes the loop for a fresh machine: open the workspace, VSCode offers
	// the extension that understands it. Under the id it has had since 0.9.2 -- this line still
	// named the old one after the rename, so a machine that took the offer got the copy nothing
	// updates any more, and with it a screen of diagnostics the compiler no longer raises. The old
	// id is said to be unwanted too, so VSCode stops offering it to a workspace that already has it
	// recommended from an earlier file.
	TArray<TSharedPtr<FJsonValue>> Recommendations;
	Recommendations.Add(MakeShared<FJsonValueString>(ExtensionId));
	TArray<TSharedPtr<FJsonValue>> Unwanted;
	Unwanted.Add(MakeShared<FJsonValueString>(OldExtensionId));
	TSharedPtr<FJsonObject> Extensions = MakeShared<FJsonObject>();
	Extensions->SetArrayField(TEXT("recommendations"), Recommendations);
	Extensions->SetArrayField(TEXT("unwantedRecommendations"), Unwanted);
	Root->SetObjectField(TEXT("extensions"), Extensions);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Root, Writer);
	return Serialized;
}

TArray<FString> FDreamUIWorkspaceService::FindOldExtensionCopies(const FString& InExtensionsDirectory)
{
	// VSCode unpacks an extension into `<publisher>.<name>-<version>`. Any publisher: a .vsix installed from a local
	// build can carry another one, and is the same stale copy.
	TArray<FString> Found;
	const FString OldName = FString(OldExtensionId).RightChop(FString(OldExtensionId).Find(TEXT(".")) + 1) + TEXT("-");
	IFileManager::Get().IterateDirectory(*InExtensionsDirectory, [&Found, &OldName](const TCHAR* InPath, bool bInIsDirectory)
	{
		const FString Name = FPaths::GetCleanFilename(InPath);
		int32 Dot = INDEX_NONE;
		if (bInIsDirectory && Name.FindChar(TEXT('.'), Dot) && Name.Mid(Dot + 1).StartsWith(OldName, ESearchCase::IgnoreCase))
		{
			Found.Add(Name);
		}
		return true;
	});
	Found.Sort();
	return Found;
}

void FDreamUIWorkspaceService::WarnAboutOldExtensionCopies()
{
	// Said here as well as by the extension itself (it warns about an old copy at start-up), because the case that
	// mattered was the old copy ALONE: with no new extension installed there is nothing on that side to say anything,
	// and every .dui opens to diagnostics the compiler stopped raising long ago.
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (Home.IsEmpty())
	{
		return;
	}
	const TArray<FString> Copies = FindOldExtensionCopies(FPaths::Combine(Home, TEXT(".vscode"), TEXT("extensions")));
	if (Copies.IsEmpty())
	{
		return;
	}
	UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d VSCode has the .dui extension under its old id installed (%s); uninstall it ('code --uninstall-extension %s') and install %s."),
		ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FString::Join(Copies, TEXT(", ")), OldExtensionId, ExtensionId);
	FNotificationInfo Info(FText::FromString(FString::Printf(
		TEXT("VSCode has the old DreamUI extension installed (%s). Uninstall it and install %s, or .dui files show diagnostics the compiler no longer raises."),
		*Copies[0], ExtensionId)));
	Info.ExpireDuration = 12.0f;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

bool FDreamUIWorkspaceService::WriteWorkspaceFile(FString& OutWorkspaceFilePath, FString& OutError)
{
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	if (!IFileManager::Get().MakeDirectory(*ProjectRoot, /*Tree*/true))
	{
		OutError = FString::Printf(TEXT("could not create '%s'"), *ProjectRoot);
		return false;
	}

	// This line is where a project's DUI/ directory usually comes into existence, and the watcher
	// registered its roots at module startup -- over the directories that existed THEN. Without this,
	// the very first workspace a project opens is one whose saves reach nothing for the rest of the
	// session: the author edits, saves, and the class never rebuilds, with nothing anywhere saying
	// why. Idempotent, so the ordinary case (the directory was already there and already watched)
	// costs a lookup.
	FDreamUISourceWatcher::EnsureWatching(ProjectRoot);

	const FString Serialized = BuildWorkspaceJson(DreamUIPaths::GetSourceRoots(), ProjectRoot);
	const FString WorkspaceFilePath = FPaths::Combine(ProjectRoot, TEXT("DreamUI.code-workspace"));
	if (!FFileHelper::SaveStringToFile(Serialized, *WorkspaceFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write '%s'"), *WorkspaceFilePath);
		return false;
	}
	OutWorkspaceFilePath = WorkspaceFilePath;
	return true;
}

void FDreamUIWorkspaceService::OpenWorkspace()
{
	using namespace DreamUIWorkspaceLocal;

	// Fresh completion data the moment the editor opens over there.
	FDreamUISymbolExport::ExportNow();

	FString WorkspaceFilePath;
	FString Error;
	if (!WriteWorkspaceFile(WorkspaceFilePath, Error))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d DreamUI workspace: %s"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Error);
		Notify(FText::FromString(FString::Printf(TEXT("DreamUI failed to create workspace: %s"), *Error)), false);
		return;
	}

	if (LaunchVSCodeWorkspace(WorkspaceFilePath))
	{
		Notify(FText::FromString(FString::Printf(TEXT("Opened DreamUI workspace in VSCode: %s"), *WorkspaceFilePath)), true);
		WarnAboutOldExtensionCopies();
		return;
	}
	if (FPlatformProcess::LaunchFileInDefaultExternalApplication(*WorkspaceFilePath, nullptr, ELaunchVerb::Edit, false))
	{
		Notify(FText::FromString(FString::Printf(TEXT("Opened DreamUI workspace: %s"), *WorkspaceFilePath)), true);
		return;
	}
	if (LaunchTextFileWithNotepad(WorkspaceFilePath))
	{
		Notify(FText::FromString(FString::Printf(TEXT("Opened DreamUI workspace in Notepad: %s"), *WorkspaceFilePath)), true);
		return;
	}
	Notify(FText::FromString(FString::Printf(TEXT("DreamUI could not open workspace: %s"), *WorkspaceFilePath)), false);
}
