// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidgetBlueprint;

/** What one conversion found, did, and could not do -- the preflight report and the result in one. */
struct DREAMGUIEDITOR_API FDreamUIConversionReport
{
	/** The Blueprint, by path. */
	FString BlueprintPath;
	/** The `.dui` it is (or would be) written to, absolute. */
	FString TargetFile;
	/** The text the conversion writes. */
	FString Text;
	/**
	 * What the file will not hold, one line each -- `Node.Property: why`, a binding or route with no line, a node the
	 * file could not take. Lost by a conversion that goes ahead; a conversion with any is refused unless the caller says
	 * the loss is allowed.
	 */
	TArray<FString> Residue;
	/** Why the conversion cannot happen at all: the class, the target file, a file that does not build. */
	TArray<FString> Errors;
	/** After the compile: where the tree rebuilt from the file differs from the tree it was converted from. */
	TArray<FString> Mismatches;
	/** The backup made before anything was changed (`WBP_X_PreDui`), by path. */
	FString BackupPath;
	/** The Blueprint now builds its hierarchy from TargetFile. */
	bool bConverted = false;

	bool CanConvert() const { return Errors.IsEmpty(); }

	/** The report as lines a log, a dialog or a commandlet prints. */
	FString ToString() const;
};

/**
 * Turns a hand-authored DreamUI Widget Blueprint into one authored in a `.dui`.
 *
 * NO SECOND PRINTER. The file is made by the write-back the designer already uses: a file holding only the root (whose
 * header the converter writes, a new root being no insert), and the Blueprint's tree handed to a flush as the tree the
 * designer just edited -- every node inserted, every `+` line inferred, every value swept -- then its bindings and
 * routes written as statements and its animations listed as `timeline X external`. So a converted file and a file the
 * designer writes later follow one set of rules, and nothing about the two can drift apart.
 *
 * The node ids are the compiler's names for the widgets (UDreamWidgetTree::MakeWidgetVariableName), so the class keeps
 * every variable, binding key and graph reference it had; the Blueprint's widgets are renamed to them before the
 * compile, which is also what lets the compile carry the animations across by name.
 *
 * The event graph, functions and variables stay in the Blueprint. A Blueprint whose parent is UDreamUserWidget is
 * reparented to UDreamTextUserWidget (its subclass, so the graph is unaffected); one whose parent is any other class
 * that is not a UDreamTextUserWidget is refused -- its C++ base has to derive from UDreamTextUserWidget first.
 */
struct DREAMGUIEDITOR_API FDreamUIBlueprintConverter
{
	/** `<Project>/DUI/<path under the mount point>/<AssetName>.dui` -- /Game/UI/WBP_Login becomes DUI/UI/WBP_Login.dui. */
	static FString GetDefaultTargetFile(const UDreamWidgetBlueprint* InBlueprint);

	/**
	 * Make the text and everything that would be lost, and change nothing. OutReport.Errors says why it cannot be
	 * converted; OutReport.Residue what would not reach the file. InTargetFile empty means the default.
	 */
	static bool Preflight(UDreamWidgetBlueprint* InBlueprint, const FString& InTargetFile, FDreamUIConversionReport& OutReport);

	/**
	 * Convert: preflight, refuse on an error -- and on residue unless bInAllowLoss -- then back the asset up as
	 * `<Name>_PreDui` beside it, write the file (never over an existing one), reparent when the parent is
	 * UDreamUserWidget, point the class at the file and compile, and compare the rebuilt tree with the one it came from
	 * (OutReport.Mismatches). Not undoable: the backup is the way back.
	 */
	static bool Convert(UDreamWidgetBlueprint* InBlueprint, const FString& InTargetFile, bool bInAllowLoss,
		FDreamUIConversionReport& OutReport);

	/**
	 * The editor's way in, for one or several Blueprints: each preflighted, residue shown and confirmed per Blueprint,
	 * converted to its default file, and the outcome said once at the end. What the Content Browser and the designer's
	 * toolbar call.
	 */
	static void ConvertWithConfirmation(const TArray<UDreamWidgetBlueprint*>& InBlueprints);

	/** Whether a Blueprint is one a conversion applies to: a DreamUI Widget Blueprint with a hierarchy and no `.dui`. */
	static bool IsConvertible(const UDreamWidgetBlueprint* InBlueprint);
};
