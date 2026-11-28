// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SQLiteDatabase.h"

/**
 * `Intermediate/DreamSource/ui.db`: what this editor tells editors that are not this one -- the symbols the compiler
 * knows and the diagnostics its compiles raised. It replaces `DUI/.dui-symbols.json` and `DUI/.dui-diagnostics.json`.
 *
 * The schema is a contract with dreamgui-language-support and lives in Plan/DreamSource/dreamsource-layout-spec.md
 * section 11.3. In short:
 *
 *   meta(key, value)                     schema, producer, revision, symbols_revision, diagnostics_revision, *_updated_utc
 *   symbols(section, name, json)         one row per member of an object section, one row (name '') for anything else
 *   diagnostics(path, compiled_at_utc, json)
 *
 * Opened for one write and closed again, never held: closing the last connection is what folds the WAL back into the
 * file, and a reader that copies the file whole (an older extension build) only sees what has been folded in. WAL still
 * matters while the connection is open -- a reader is never handed a half-written page.
 *
 * A schema this build does not know is dropped and recreated in place, never deleted: on Windows a reader holding the
 * file open makes a delete fail, and everything in here rebuilds by itself anyway.
 */
class DREAMGUIEDITOR_API FDreamUIDatabase
{
public:
	/** The schema this build writes. Bumped whenever a table changes shape; readers compare against meta.schema. */
	static constexpr int32 SchemaVersion = 1;

	FDreamUIDatabase() = default;
	~FDreamUIDatabase();

	FDreamUIDatabase(const FDreamUIDatabase&) = delete;
	FDreamUIDatabase& operator=(const FDreamUIDatabase&) = delete;

	/** Opens (creating the directory and file), sets the pragmas, makes sure the schema is current. False, logged, on failure. */
	bool Open(const FString& InPath);
	void Close();

	FSQLiteDatabase& Get() { return Database; }

	bool Begin();
	bool Commit();
	void Rollback();

	/** Empty when the key is not there. */
	FString GetMeta(const TCHAR* InKey);
	bool SetMeta(const TCHAR* InKey, const FString& InValue);

	/** Adds one to InKey and to `revision`, and stamps InUpdatedKey with now. Inside the caller's transaction. */
	bool Bump(const TCHAR* InKey, const TCHAR* InUpdatedKey);

	/** Statement + error, for a log line. */
	FString DescribeError(const TCHAR* InWhat) const;

private:
	bool EnsureSchema();

	FSQLiteDatabase Database;
	FString Path;
};
