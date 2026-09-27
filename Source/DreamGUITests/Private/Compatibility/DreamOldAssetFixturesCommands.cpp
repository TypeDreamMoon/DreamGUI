// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamOldAssetFixtures.h"

#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"

/*
 * The console commands that make the old-asset fixtures and their snapshot. See DreamOldAssetFixtures.h.
 *
 *     UnrealEditor-Cmd.exe <host>.uproject -ExecCmds="DreamGUI.OldAssetFixtures.Write Exit" -unattended -nullrhi
 *     UnrealEditor-Cmd.exe <host>.uproject -ExecCmds="DreamGUI.OldAssetFixtures.Snapshot Exit" -unattended -nullrhi
 *
 * Console commands rather than a commandlet: this module loads at PostEngineInit, and a commandlet's class
 * is looked up before that phase, so it would never be found. With "Exit" the editor quits once the command
 * has finished. Each ends by logging one line, "DreamGUI.OldAssetFixtures: <command> succeeded" or "...
 * failed", which is what a script waiting on it looks for.
 */
DEFINE_LOG_CATEGORY_STATIC(LogDreamGUIOldAssetFixtures, Log, All);

namespace DreamOldAssetFixturesCommandsLocal
{
	void Finish(const TCHAR* InCommand, bool bInSucceeded, const TArray<FString>& InArgs, const TArray<FString>& InLog)
	{
		for (const FString& Line : InLog)
		{
			UE_LOG(LogDreamGUIOldAssetFixtures, Display, TEXT("%s"), *Line);
		}
		if (bInSucceeded)
		{
			UE_LOG(LogDreamGUIOldAssetFixtures, Display, TEXT("DreamGUI.OldAssetFixtures: %s succeeded"), InCommand);
		}
		else
		{
			UE_LOG(LogDreamGUIOldAssetFixtures, Error, TEXT("DreamGUI.OldAssetFixtures: %s failed"), InCommand);
		}
		if (InArgs.ContainsByPredicate([](const FString& InArg) { return InArg.Equals(TEXT("Exit"), ESearchCase::IgnoreCase); }))
		{
			RequestEngineExit(FString::Printf(TEXT("DreamGUI.OldAssetFixtures %s finished"), InCommand));
		}
	}

	void Write(const TArray<FString>& InArgs)
	{
		TArray<FString> Log;
		const bool bOk = DreamOldAssetFixtures::WriteAll(Log);
		Finish(TEXT("Write"), bOk, InArgs, Log);
	}

	void Snapshot(const TArray<FString>& InArgs)
	{
		TArray<FString> Log;
		const bool bOk = DreamOldAssetFixtures::WriteSnapshot(Log);
		Finish(TEXT("Snapshot"), bOk, InArgs, Log);
	}
}

static FAutoConsoleCommand GDreamOldAssetFixturesWrite(
	TEXT("DreamGUI.OldAssetFixtures.Write"),
	TEXT("Make the old-asset fixtures in /Game/DreamGUIFixtures and save them; refuses when any of them exists. Pass Exit to quit afterwards."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DreamOldAssetFixturesCommandsLocal::Write));

static FAutoConsoleCommand GDreamOldAssetFixturesSnapshot(
	TEXT("DreamGUI.OldAssetFixtures.Snapshot"),
	TEXT("Load the old-asset fixtures, compile them, and write what they hold to the snapshot beside them. Pass Exit to quit afterwards."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DreamOldAssetFixturesCommandsLocal::Snapshot));
