// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIDatabase.h"

#include "DreamGUIEditorModule.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "SQLitePreparedStatement.h"

namespace DreamUIDatabaseLocal
{
	FString Producer()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		return FString::Printf(TEXT("DreamGUI@%s"), Plugin.IsValid() ? *Plugin->GetDescriptor().VersionName : TEXT("unknown"));
	}
}

FDreamUIDatabase::~FDreamUIDatabase()
{
	Close();
}

bool FDreamUIDatabase::Open(const FString& InPath)
{
	Close();
	Path = InPath;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(InPath), /*Tree*/true);
	if (!Database.Open(*InPath, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Could not open '%s': %s"),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InPath, *Database.GetLastError());
		return false;
	}
	// busy_timeout first: the extension may be mid-read, and every statement after this one should wait it out rather
	// than fail on SQLITE_BUSY.
	Database.Execute(TEXT("PRAGMA busy_timeout=5000;"));
	Database.Execute(TEXT("PRAGMA journal_mode=WAL;"));
	Database.Execute(TEXT("PRAGMA synchronous=NORMAL;"));
	if (!EnsureSchema())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
			*DescribeError(TEXT("creating the schema")));
		Close();
		return false;
	}
	return true;
}

void FDreamUIDatabase::Close()
{
	if (Database.IsValid())
	{
		Database.Close();
	}
}

bool FDreamUIDatabase::EnsureSchema()
{
	if (!Database.Execute(TEXT("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);")))
	{
		return false;
	}
	const FString Existing = GetMeta(TEXT("schema"));
	if (!Existing.IsEmpty() && Existing != FString::FromInt(SchemaVersion))
	{
		UE_LOG(DreamGUIEditor, Display, TEXT("[%s].%d '%s' has schema %s; this build writes %d. Rebuilding it."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Path, *Existing, SchemaVersion);
		Database.Execute(TEXT("DROP TABLE IF EXISTS symbols;"));
		Database.Execute(TEXT("DROP TABLE IF EXISTS diagnostics;"));
		Database.Execute(TEXT("DELETE FROM meta;"));
	}
	const bool bCreated =
		Database.Execute(TEXT("CREATE TABLE IF NOT EXISTS symbols(section TEXT NOT NULL, name TEXT NOT NULL, json TEXT NOT NULL, ")
			TEXT("PRIMARY KEY(section, name)) WITHOUT ROWID;"))
		&& Database.Execute(TEXT("CREATE TABLE IF NOT EXISTS diagnostics(path TEXT PRIMARY KEY, compiled_at_utc TEXT NOT NULL, ")
			TEXT("json TEXT NOT NULL);"));
	if (!bCreated)
	{
		return false;
	}
	if (Existing != FString::FromInt(SchemaVersion))
	{
		SetMeta(TEXT("schema"), FString::FromInt(SchemaVersion));
	}
	const FString Producer = DreamUIDatabaseLocal::Producer();
	if (GetMeta(TEXT("producer")) != Producer)
	{
		SetMeta(TEXT("producer"), Producer);
	}
	return true;
}

bool FDreamUIDatabase::Begin()
{
	return Database.Execute(TEXT("BEGIN IMMEDIATE;"));
}

bool FDreamUIDatabase::Commit()
{
	return Database.Execute(TEXT("COMMIT;"));
}

void FDreamUIDatabase::Rollback()
{
	Database.Execute(TEXT("ROLLBACK;"));
}

FString FDreamUIDatabase::GetMeta(const TCHAR* InKey)
{
	FString Value;
	FSQLitePreparedStatement Statement(Database, TEXT("SELECT value FROM meta WHERE key = ?1;"));
	if (Statement.IsValid() && Statement.SetBindingValueByIndex(1, FString(InKey))
		&& Statement.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		Statement.GetColumnValueByIndex(0, Value);
	}
	return Value;
}

bool FDreamUIDatabase::SetMeta(const TCHAR* InKey, const FString& InValue)
{
	FSQLitePreparedStatement Statement(Database, TEXT("INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2);"));
	return Statement.IsValid()
		&& Statement.SetBindingValueByIndex(1, FString(InKey))
		&& Statement.SetBindingValueByIndex(2, InValue)
		&& Statement.Step() == ESQLitePreparedStatementStepResult::Done;
}

bool FDreamUIDatabase::Bump(const TCHAR* InKey, const TCHAR* InUpdatedKey)
{
	const auto Next = [this](const TCHAR* InName)
	{
		const int64 Current = FCString::Atoi64(*GetMeta(InName));
		return SetMeta(InName, LexToString(Current + 1));
	};
	return Next(InKey) && Next(TEXT("revision")) && SetMeta(InUpdatedKey, FDateTime::UtcNow().ToIso8601());
}

FString FDreamUIDatabase::DescribeError(const TCHAR* InWhat) const
{
	return FString::Printf(TEXT("'%s': %s failed: %s"), *Path, InWhat, *Database.GetLastError());
}
