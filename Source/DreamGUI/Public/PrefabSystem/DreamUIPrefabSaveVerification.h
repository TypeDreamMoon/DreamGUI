// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class UDreamUIPrefab;
class UDreamWidget;

namespace DreamUIPrefabSystem
{
	/**
	 * Result of re-reading a freshly saved prefab payload and comparing it against the live hierarchy.
	 *
	 * Structural differences (an object missing, extra, or of the wrong class after reload) mean the payload
	 * does not represent what the user sees and must never reach disk. Property differences mean the payload
	 * loads back with drifted values — historically benign more often than fatal, so they are reported and
	 * only escalate to a save failure by opt-in (UDreamUISettings::bBlockPrefabSaveOnPropertyDrift).
	 */
	struct DREAMGUI_API FDreamUIPrefabSaveVerificationResult
	{
		bool bStructureMatches = true;
		TArray<FString> StructuralDifferences;
		TArray<FString> PropertyDifferences;
	};

	/**
	 * Deserialize the prefab's just-written editor payload (BinaryData) into a throwaway world and compare the
	 * reloaded hierarchy against InSourceRoot: object-by-object (widgets, visuals, layouts, panel slots,
	 * behaviour components), then property-by-property on each aligned pair.
	 *
	 * This exists because the serializer historically failed silently: nested objects were dropped, FText came
	 * back empty, sub-prefab slot data reverted — all discovered days later. Verifying at save time turns
	 * "the asset quietly lost data" into an immediate, attributable save failure.
	 */
	DREAMGUI_API FDreamUIPrefabSaveVerificationResult VerifyPrefabSaveRoundTrip(UDreamUIPrefab* InPrefab, UDreamWidget* InSourceRoot);

	/**
	 * Copy of every asset field the editor-side serializer writes, so a save whose payload fails verification
	 * can be rolled back and the asset (and whatever is on disk) keeps its previous good state.
	 */
	struct DREAMGUI_API FDreamUIPrefabEditorPayloadSnapshot
	{
		void Capture(const UDreamUIPrefab* InPrefab);
		void Restore(UDreamUIPrefab* InPrefab) const;

	private:
		TArray<uint8> BinaryData;
		FDateTime CreateTime;
		TArray<TObjectPtr<UObject>> ReferenceAssetList;
		TArray<TObjectPtr<UClass>> ReferenceClassList;
		TArray<FName> ReferenceNameList;
		TArray<FText> ReferenceTextList;
		uint16 PrefabVersion = 0;
		uint16 PrefabSchemaVersion = 0;
		uint16 EngineMajorVersion = 0;
		uint16 EngineMinorVersion = 0;
		uint16 EnginePatchVersion = 0;
		int32 ArchiveVersion = 0;
		int32 ArchiveVersionUE5 = 0;
		int32 ArchiveLicenseeVer = 0;
		uint32 ArEngineNetVer = 0;
		uint32 ArGameNetVer = 0;
	};
}

#endif
