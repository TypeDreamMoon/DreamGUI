// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabSaveVerification.h"

#if WITH_EDITOR

#include "Core/Components/LexLayout.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/WidgetSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace LexUIPrefabSystem
{

namespace SaveVerificationLocal
{
	struct FNodeRecord
	{
		/** Hierarchical path of the owning widget, e.g. "$/[0]Header/[2]Title". Stable across both worlds. */
		FString Path;
		/** Which object on that widget this record describes: widget/visual/layout/layoutSelf/slot/component[i]. */
		FString Kind;
		UObject* Object = nullptr;

		FString Key() const { return Path + TEXT("|") + Kind; }
	};

	void CollectHierarchy(ULexWidget* Widget, const FString& Path, const bool bIsRoot, TArray<FNodeRecord>& Out)
	{
		if (!IsValid(Widget))
		{
			return;
		}
		Out.Add({ Path, TEXT("widget"), Widget });
		if (ULexVisual* Visual = Widget->GetVisual(); IsValid(Visual))
		{
			Out.Add({ Path, TEXT("visual"), Visual });
		}
		if (ULexLayoutContainer* Layout = Widget->GetLayoutContainer(); IsValid(Layout))
		{
			Out.Add({ Path, TEXT("layout"), Layout });
		}
		if (ULexLayoutSelf* LayoutSelf = Widget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			Out.Add({ Path, TEXT("layoutSelf"), LayoutSelf });
		}
		// The root's panel slot belongs to whatever panel hosts the prefab instance outside this asset,
		// so it is context, not payload.
		if (!bIsRoot)
		{
			if (ULexPanelSlot* Slot = Widget->GetPanelSlot(); IsValid(Slot))
			{
				Out.Add({ Path, TEXT("slot"), Slot });
			}
		}
		const TArray<ULexUIBehaviour*> Components = Widget->GetComponents(ULexUIBehaviour::StaticClass());
		int32 ComponentIndex = 0;
		for (ULexUIBehaviour* Component : Components)
		{
			// Transient companions (input handlers, editor helpers) are never serialized; skip them so a
			// runtime-only object does not read as "lost by the save".
			if (IsValid(Component) && !Component->HasAnyFlags(RF_Transient) && !Component->GetClass()->HasAnyClassFlags(CLASS_Transient))
			{
				Out.Add({ Path, FString::Printf(TEXT("component[%d]:%s"), ComponentIndex, *Component->GetClass()->GetName()), Component });
				++ComponentIndex;
			}
		}
		const TArray<ULexWidget*>& Children = Widget->GetChildren();
		for (int32 i = 0; i < Children.Num(); i++)
		{
			if (IsValid(Children[i]))
			{
				CollectHierarchy(Children[i],
					FString::Printf(TEXT("%s/[%d]%s"), *Path, i, *Children[i]->GetDisplayName()), false, Out);
			}
		}
	}

	/**
	 * Object paths differ between the two worlds by construction, so equality must be checked against
	 * canonical tokens. Longest path first: a component's path contains its widget's path as a prefix, and
	 * replacing the shorter one first would leave mismatching half-canonical tokens on the two sides.
	 */
	TArray<TPair<FString, FString>> BuildCanonicalPathMap(const TArray<FNodeRecord>& Records)
	{
		TArray<TPair<FString, FString>> Result;
		for (int32 i = 0; i < Records.Num(); i++)
		{
			if (Records[i].Object)
			{
				Result.Emplace(Records[i].Object->GetPathName(), FString::Printf(TEXT("$obj%d"), i));
			}
		}
		Result.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
		{
			return A.Key.Len() > B.Key.Len();
		});
		return Result;
	}

	FString CanonicalizeValue(FString Value, const TArray<TPair<FString, FString>>& CanonicalPaths)
	{
		for (const TPair<FString, FString>& Pair : CanonicalPaths)
		{
			Value.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
		}
		return Value;
	}

	FString JsonValueToString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("<absent>");
		}
		// Wrap in an object so scalar values can be serialized too.
		const TSharedRef<FJsonObject> Wrapper = MakeShared<FJsonObject>();
		Wrapper->SetField(TEXT("v"), Value);
		FString Result;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
		FJsonSerializer::Serialize(Wrapper, Writer);
		return Result;
	}

	TSharedPtr<FJsonObject> ObjectToJson(UObject* Object)
	{
		const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		constexpr int64 SkipFlags = CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient
			| CPF_Deprecated | CPF_SkipSerialization | CPF_TextExportTransient;
		if (!FJsonObjectConverter::UStructToJsonObject(Object->GetClass(), Object, JsonObject, 0, SkipFlags))
		{
			return nullptr;
		}
		return JsonObject;
	}

	FString Truncate(const FString& Value)
	{
		constexpr int32 MaxLen = 160;
		return Value.Len() <= MaxLen ? Value : Value.Left(MaxLen) + TEXT("...");
	}

	void CompareObjectPair(const FNodeRecord& Source, const FNodeRecord& Reloaded
		, const TArray<TPair<FString, FString>>& SourceCanonicalPaths
		, const TArray<TPair<FString, FString>>& ReloadedCanonicalPaths
		, const bool bIsRoot
		, TArray<FString>& OutPropertyDifferences)
	{
		const TSharedPtr<FJsonObject> SourceJson = ObjectToJson(Source.Object);
		const TSharedPtr<FJsonObject> ReloadedJson = ObjectToJson(Reloaded.Object);
		if (!SourceJson.IsValid() || !ReloadedJson.IsValid())
		{
			OutPropertyDifferences.Add(FString::Printf(TEXT("%s %s: could not extract properties for comparison"),
				*Source.Path, *Source.Kind));
			return;
		}

		// The root widget's attachment describes where the editing scene put the instance, not what the
		// asset stores; a reloaded root is parentless by construction.
		static const TSet<FString> RootIgnoredKeys = {
			TEXT("parent"),
		};
		// Two families of keys are not payload:
		//  - relative transform is a cache derived from AnchorData by the layout pass;
		//  - panelSlot/visual/layoutContainer/layoutSelf are Instanced sub-objects that the JSON converter
		//    inlines here, duplicating the dedicated per-object records (which carry the proper rules).
		static const TSet<FString> WidgetIgnoredKeys = {
			TEXT("relativeLocation"), TEXT("relativeRotation"), TEXT("relativeScale"),
			TEXT("panelSlot"), TEXT("visual"), TEXT("layoutContainer"), TEXT("layoutSelf"),
		};
		// Whether (and how) a panel pass has arranged the widget is runtime state, not asset content:
		// saves normalize it to "nothing applied", and the verification world may legitimately run its
		// own layout pass right after loading, flipping it back.
		static const TSet<FString> SlotIgnoredKeys = {
			TEXT("bLayoutGeometryApplied"), TEXT("layoutGeometryControlMask"),
		};

		TSet<FString> Keys;
		for (const auto& Pair : SourceJson->Values)
		{
			Keys.Add(FString(Pair.Key.ToView()));
		}
		for (const auto& Pair : ReloadedJson->Values)
		{
			Keys.Add(FString(Pair.Key.ToView()));
		}
		// When the verification world's own layout pass has re-arranged the reloaded widget, its live
		// AnchorData is arranged output again while the (normalized) source holds authored values — the
		// authored payload is still fully compared through the slot's authoredAnchorData.
		bool bReloadedWasRearranged = false;
		if (const ULexWidget* ReloadedWidget = Cast<ULexWidget>(Reloaded.Object))
		{
			const ULexPanelSlot* ReloadedSlot = ReloadedWidget->GetPanelSlot();
			bReloadedWasRearranged = IsValid(ReloadedSlot) && ReloadedSlot->HasLayoutGeometryApplied();
		}

		for (const FString& Key : Keys)
		{
			if (Source.Kind == TEXT("widget")
				&& (WidgetIgnoredKeys.Contains(Key) || (bIsRoot && RootIgnoredKeys.Contains(Key))
					|| (bReloadedWasRearranged && Key == TEXT("anchorData"))))
			{
				continue;
			}
			if (Source.Kind == TEXT("slot") && SlotIgnoredKeys.Contains(Key))
			{
				continue;
			}
			const FString SourceValue = CanonicalizeValue(JsonValueToString(SourceJson->TryGetField(Key)), SourceCanonicalPaths);
			const FString ReloadedValue = CanonicalizeValue(JsonValueToString(ReloadedJson->TryGetField(Key)), ReloadedCanonicalPaths);
			if (!SourceValue.Equals(ReloadedValue, ESearchCase::CaseSensitive))
			{
				OutPropertyDifferences.Add(FString::Printf(TEXT("%s %s.%s: saved=%s reloaded=%s"),
					*Source.Path, *Source.Kind, *Key, *Truncate(SourceValue), *Truncate(ReloadedValue)));
			}
		}
	}
}

FLexUIPrefabSaveVerificationResult VerifyPrefabSaveRoundTrip(ULexUIPrefab* InPrefab, ULexWidget* InSourceRoot)
{
	using namespace SaveVerificationLocal;

	FLexUIPrefabSaveVerificationResult Result;
	if (!IsValid(InPrefab) || !IsValid(InSourceRoot))
	{
		Result.bStructureMatches = false;
		Result.StructuralDifferences.Add(TEXT("No prefab asset or source hierarchy to verify."));
		return Result;
	}

	UWorld* VerifyWorld = UWorld::CreateWorld(EWorldType::None, false);
	if (!VerifyWorld)
	{
		Result.bStructureMatches = false;
		Result.StructuralDifferences.Add(TEXT("Could not create a verification world."));
		return Result;
	}

	ULexWidget* ReloadedRoot = nullptr;
	{
		TMap<FGuid, TObjectPtr<UObject>> MapGuidToObjects;
		TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> SubPrefabMap;
		ReloadedRoot = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::LoadPrefabWithExistingObjects(
			VerifyWorld, VerifyWorld, InPrefab, nullptr, MapGuidToObjects, SubPrefabMap);
	}
	if (!IsValid(ReloadedRoot))
	{
		Result.bStructureMatches = false;
		Result.StructuralDifferences.Add(TEXT("The just-saved payload failed to deserialize at all."));
	}
	else
	{
		TArray<FNodeRecord> SourceRecords;
		TArray<FNodeRecord> ReloadedRecords;
		CollectHierarchy(InSourceRoot, TEXT("$"), true, SourceRecords);
		CollectHierarchy(ReloadedRoot, TEXT("$"), true, ReloadedRecords);

		TMap<FString, FNodeRecord> ReloadedByKey;
		for (const FNodeRecord& Record : ReloadedRecords)
		{
			ReloadedByKey.Add(Record.Key(), Record);
		}
		TSet<FString> MatchedKeys;

		const TArray<TPair<FString, FString>> SourceCanonicalPaths = BuildCanonicalPathMap(SourceRecords);
		const TArray<TPair<FString, FString>> ReloadedCanonicalPaths = BuildCanonicalPathMap(ReloadedRecords);

		for (const FNodeRecord& Source : SourceRecords)
		{
			const FNodeRecord* Reloaded = ReloadedByKey.Find(Source.Key());
			if (!Reloaded)
			{
				Result.bStructureMatches = false;
				Result.StructuralDifferences.Add(FString::Printf(TEXT("%s %s (%s) is MISSING after reload — the saved payload lost it."),
					*Source.Path, *Source.Kind, *Source.Object->GetClass()->GetName()));
				continue;
			}
			MatchedKeys.Add(Source.Key());
			if (Source.Object->GetClass() != Reloaded->Object->GetClass())
			{
				Result.bStructureMatches = false;
				Result.StructuralDifferences.Add(FString::Printf(TEXT("%s %s changed class after reload: %s -> %s."),
					*Source.Path, *Source.Kind,
					*Source.Object->GetClass()->GetName(), *Reloaded->Object->GetClass()->GetName()));
				continue;
			}
			const bool bIsRoot = Source.Path == TEXT("$");
			CompareObjectPair(Source, *Reloaded, SourceCanonicalPaths, ReloadedCanonicalPaths, bIsRoot,
				Result.PropertyDifferences);
		}
		for (const FNodeRecord& Reloaded : ReloadedRecords)
		{
			if (!MatchedKeys.Contains(Reloaded.Key()))
			{
				Result.bStructureMatches = false;
				Result.StructuralDifferences.Add(FString::Printf(TEXT("%s %s (%s) appeared after reload but does not exist in the edited hierarchy."),
					*Reloaded.Path, *Reloaded.Kind, *Reloaded.Object->GetClass()->GetName()));
			}
		}

		ReloadedRoot->DestroyWidget();
	}

	VerifyWorld->DestroyWorld(false);
	return Result;
}

void FLexUIPrefabEditorPayloadSnapshot::Capture(const ULexUIPrefab* InPrefab)
{
	BinaryData = InPrefab->BinaryData;
	CreateTime = InPrefab->CreateTime;
	ReferenceAssetList = InPrefab->ReferenceAssetList;
	ReferenceClassList = InPrefab->ReferenceClassList;
	ReferenceNameList = InPrefab->ReferenceNameList;
	ReferenceTextList = InPrefab->ReferenceTextList;
	PrefabVersion = InPrefab->PrefabVersion;
	PrefabSchemaVersion = InPrefab->PrefabSchemaVersion;
	EngineMajorVersion = InPrefab->EngineMajorVersion;
	EngineMinorVersion = InPrefab->EngineMinorVersion;
	EnginePatchVersion = InPrefab->EnginePatchVersion;
	ArchiveVersion = InPrefab->ArchiveVersion;
	ArchiveVersionUE5 = InPrefab->ArchiveVersionUE5;
	ArchiveLicenseeVer = InPrefab->ArchiveLicenseeVer;
	ArEngineNetVer = InPrefab->ArEngineNetVer;
	ArGameNetVer = InPrefab->ArGameNetVer;
}

void FLexUIPrefabEditorPayloadSnapshot::Restore(ULexUIPrefab* InPrefab) const
{
	InPrefab->BinaryData = BinaryData;
	InPrefab->CreateTime = CreateTime;
	InPrefab->ReferenceAssetList = ReferenceAssetList;
	InPrefab->ReferenceClassList = ReferenceClassList;
	InPrefab->ReferenceNameList = ReferenceNameList;
	InPrefab->ReferenceTextList = ReferenceTextList;
	InPrefab->PrefabVersion = PrefabVersion;
	InPrefab->PrefabSchemaVersion = PrefabSchemaVersion;
	InPrefab->EngineMajorVersion = EngineMajorVersion;
	InPrefab->EngineMinorVersion = EngineMinorVersion;
	InPrefab->EnginePatchVersion = EnginePatchVersion;
	InPrefab->ArchiveVersion = ArchiveVersion;
	InPrefab->ArchiveVersionUE5 = ArchiveVersionUE5;
	InPrefab->ArchiveLicenseeVer = ArchiveLicenseeVer;
	InPrefab->ArEngineNetVer = ArEngineNetVer;
	InPrefab->ArGameNetVer = ArGameNetVer;
}

}

#endif
