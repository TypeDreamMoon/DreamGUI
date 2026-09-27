// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPackage;

/**
 * Assets saved by the plugin as it was before its classes began moving between modules: the old assets
 * every later step has to keep loading.
 *
 * They live in the test host's content, /Game/DreamGUIFixtures, which every host copies from
 * Tools/TestHost/Template/Content; the shipped plugin carries none of them. What they hold:
 *
 *   WBP_FixturePalette   every entry the designer's palette offers, placed the way the palette places it
 *   WBP_FixtureBindings  event bindings to a widget, to a visual and to a behaviour, and a property binding
 *   WBP_FixtureAnimated  an animation driving a float and a vector property of a child
 *   WBP_FixtureNested    the two above placed as nested user widgets, and a control with its slot filled
 *   L_FixtureWorld       a level: a world widget showing the nested one, the plugin's event system actor,
 *                        and a raycaster on an actor of the level's own
 *
 * The snapshot beside them records every saved object and every property it holds at a value other than
 * its default, as the code that saved the files read them back. After a class has moved, the same
 * description of the same files has to come out the same once the snapshot's script paths have been
 * carried through the redirects -- which is what a move that loses nothing means.
 *
 * The console commands DreamGUI.OldAssetFixtures.Write and .Snapshot make them and the snapshot
 * (DreamOldAssetFixturesCommands.cpp); the snapshot and the tests read a fixture back through
 * DreamPackageLoadCheck::LoadAndCompile. Writing refuses to replace a fixture that exists: these files are
 * worth something only because of when they were saved.
 */
namespace DreamOldAssetFixtures
{
	/** "/Game/DreamGUIFixtures". */
	extern const TCHAR* const Directory;

	/** The fixture packages, in the order they are made: whatever nests another comes after it. */
	const TArray<FString>& PackageNames();

	/** The snapshot's file, next to the packages in the project's content. */
	FString SnapshotFilename();

	/** Make every fixture and save it. OutLog says what was made, or why not. */
	bool WriteAll(TArray<FString>& OutLog);

	/**
	 * Read every fixture back, compile it, and write what it holds to SnapshotFilename(). Refuses -- writes
	 * nothing -- when a fixture does not come back clean. OutLog says what happened.
	 */
	bool WriteSnapshot(TArray<FString>& OutLog);

	/**
	 * What InPackage saves, one line per object and one per property that differs from the object's
	 * archetype, in a stable order. For a widget Blueprint: its parent class, its functions, its property
	 * bindings and its whole widget tree. For a level: the actors of its persistent level that are not
	 * the level's own furniture, and their components.
	 */
	FString Describe(const UPackage* InPackage);

	/** InText with every script type path in it carried through the CoreRedirects: class, then struct, then enum. */
	FString ApplyRedirects(const FString& InText);

	/** The snapshot's blocks by package name, with its comment lines dropped. */
	TMap<FString, FString> ParseSnapshot(const FString& InSnapshot);
}
