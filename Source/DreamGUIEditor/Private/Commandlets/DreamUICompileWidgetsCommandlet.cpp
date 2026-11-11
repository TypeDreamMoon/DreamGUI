// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Commandlets/DreamUICompileWidgetsCommandlet.h"

#include "DataFactory/DreamWidgetBlueprintFactory.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamTextUserWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace DreamUICompileWidgetsLocal
{
	struct FEntry
	{
		FString Asset;   // /Game/UI/WBP_X (package path, with or without the .Name suffix)
		FString Parent;  // /Script/Module.Class, empty = UDreamTextUserWidget
		FString Source;  // the .dui, relative to DUI/ or absolute; empty = keep what the Blueprint names
	};

	FString PackageOf(const FString& InAsset)
	{
		FString Package = InAsset;
		int32 Dot = INDEX_NONE;
		if (Package.FindLastChar(TEXT('.'), Dot))
		{
			Package.LeftInline(Dot);
		}
		return Package;
	}

	bool ReadManifest(const FString& InPath, TArray<FEntry>& OutEntries)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InPath))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("Cannot read the manifest '%s'."), *InPath);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("'%s' is not valid JSON: %s"), *InPath, *Reader->GetErrorMessage());
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Widgets = nullptr;
		if (!Root->TryGetArrayField(TEXT("widgets"), Widgets))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("'%s' has no \"widgets\" array."), *InPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Widgets)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			FEntry Entry;
			(*Object)->TryGetStringField(TEXT("asset"), Entry.Asset);
			(*Object)->TryGetStringField(TEXT("parent"), Entry.Parent);
			(*Object)->TryGetStringField(TEXT("source"), Entry.Source);
			if (Entry.Asset.IsEmpty())
			{
				UE_LOG(DreamGUIEditor, Error, TEXT("'%s': an entry has no \"asset\"."), *InPath);
				return false;
			}
			OutEntries.Add(MoveTemp(Entry));
		}
		return true;
	}

	/** Every compiler message, at its own severity, prefixed with the Blueprint's name. */
	void LogResults(const UBlueprint* InBlueprint, const FCompilerResultsLog& InResults)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			const FString Line = FString::Printf(TEXT("%s: %s"), *GetNameSafe(InBlueprint), *Message->ToText().ToString());
			switch (Message->GetSeverity())
			{
			case EMessageSeverity::Error:
				UE_LOG(DreamGUIEditor, Error, TEXT("%s"), *Line);
				break;
			case EMessageSeverity::Warning:
			case EMessageSeverity::PerformanceWarning:
				UE_LOG(DreamGUIEditor, Warning, TEXT("%s"), *Line);
				break;
			default:
				UE_LOG(DreamGUIEditor, Display, TEXT("%s"), *Line);
				break;
			}
		}
	}

	UDreamWidgetBlueprint* LoadOrCreate(const FEntry& InEntry, UClass* InParent, bool& bOutCreated)
	{
		bOutCreated = false;
		const FString PackagePath = PackageOf(InEntry.Asset);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (FPackageName::DoesPackageExist(PackagePath))
		{
			return LoadObject<UDreamWidgetBlueprint>(nullptr, *(PackagePath + TEXT(".") + AssetName));
		}
		UDreamWidgetBlueprintFactory* Factory = NewObject<UDreamWidgetBlueprintFactory>();
		Factory->ParentClass = InParent;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Created = AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackagePath),
			UDreamWidgetBlueprint::StaticClass(), Factory);
		bOutCreated = Created != nullptr;
		return Cast<UDreamWidgetBlueprint>(Created);
	}

	/** Compiles, logs every message, and answers whether it compiled without an error. */
	bool Compile(UDreamWidgetBlueprint* InBlueprint)
	{
		FCompilerResultsLog Results;
		Results.bSilentMode = false;
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		LogResults(InBlueprint, Results);
		return Results.NumErrors == 0 && InBlueprint->Status != BS_Error;
	}
}

UDreamUICompileWidgetsCommandlet::UDreamUICompileWidgetsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UDreamUICompileWidgetsCommandlet::Main(const FString& Params)
{
	using namespace DreamUICompileWidgetsLocal;

	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(/*bSynchronousSearch*/true);

	TArray<FEntry> Entries;
	if (const FString* Manifest = Values.Find(TEXT("Manifest")))
	{
		FString ManifestPath = *Manifest;
		if (FPaths::IsRelative(ManifestPath))
		{
			ManifestPath = FPaths::Combine(FPaths::ProjectDir(), ManifestPath);
		}
		if (!ReadManifest(ManifestPath, Entries))
		{
			return 1;
		}
	}
	else if (Switches.Contains(TEXT("All")))
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(UDreamWidgetBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		if (const FString* Path = Values.Find(TEXT("Path")))
		{
			Filter.PackagePaths.Add(FName(**Path));
			Filter.bRecursivePaths = true;
		}
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const FAssetData& Asset : Found)
		{
			Entries.Add({ Asset.PackageName.ToString(), FString(), FString() });
		}
	}
	else
	{
		UE_LOG(DreamGUIEditor, Error, TEXT("Nothing to compile: pass -Manifest=<file.json> or -All [-Path=/Game/UI]."));
		return 1;
	}

	if (const FString* Only = Values.Find(TEXT("Only")))
	{
		TArray<FString> Names;
		Only->ParseIntoArray(Names, TEXT(","), /*InCullEmpty*/true);
		Entries.RemoveAll([&Names](const FEntry& Entry)
		{
			return !Names.Contains(FPackageName::GetLongPackageAssetName(PackageOf(Entry.Asset)));
		});
	}

	const bool bAll = Switches.Contains(TEXT("All")) && !Values.Contains(TEXT("Manifest"));
	int32 Compiled = 0;
	int32 Failed = 0;
	for (const FEntry& Entry : Entries)
	{
		UClass* Parent = nullptr;
		if (!bAll)
		{
			const FString ParentPath = Entry.Parent.IsEmpty() ? UDreamTextUserWidget::StaticClass()->GetPathName() : Entry.Parent;
			Parent = LoadObject<UClass>(nullptr, *ParentPath);
			if (Parent == nullptr || !Parent->IsChildOf(UDreamTextUserWidget::StaticClass()))
			{
				UE_LOG(DreamGUIEditor, Error, TEXT("%s: parent '%s' %s."), *Entry.Asset, *ParentPath,
					Parent == nullptr ? TEXT("does not exist (is the module that declares it built?)")
					                  : TEXT("is not a UDreamTextUserWidget, so it cannot be built from a .dui"));
				++Failed;
				continue;
			}
		}

		bool bCreated = false;
		UDreamWidgetBlueprint* Blueprint = LoadOrCreate(Entry, Parent, bCreated);
		if (Blueprint == nullptr)
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("%s: not a DreamUI Widget Blueprint, or it could not be created."), *Entry.Asset);
			++Failed;
			continue;
		}

		bool bOk = true;
		if (Parent != nullptr && Blueprint->ParentClass != Parent)
		{
			UE_LOG(DreamGUIEditor, Display, TEXT("%s: reparenting from %s to %s."), *Blueprint->GetName(),
				*GetNameSafe(Blueprint->ParentClass), *Parent->GetName());
			Blueprint->ParentClass = Parent;
			FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			// The class has to exist under the new parent before its default object can be told which file it is.
			bOk = Compile(Blueprint);
		}

		if (bOk && !Entry.Source.IsEmpty() && DreamUITextAuthoring::GetAuthoredSourcePath(Blueprint) != Entry.Source)
		{
			if (!DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Entry.Source))
			{
				UE_LOG(DreamGUIEditor, Error, TEXT("%s: its class cannot name a .dui."), *Blueprint->GetName());
				bOk = false;
			}
		}
		if (bOk && !DreamUITextAuthoring::IsTextAuthored(Blueprint))
		{
			if (bAll)
			{
				continue; // a hand-authored Blueprint met by -All: not this commandlet's to compile
			}
			UE_LOG(DreamGUIEditor, Error, TEXT("%s: names no .dui; give the entry a \"source\"."), *Blueprint->GetName());
			bOk = false;
		}

		bOk = bOk && Compile(Blueprint);
		if (bOk && !UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetOutermost() }, /*bOnlyDirty*/false))
		{
			UE_LOG(DreamGUIEditor, Error, TEXT("%s: compiled, but the package could not be saved."), *Blueprint->GetName());
			bOk = false;
		}

		UE_LOG(DreamGUIEditor, Display, TEXT("%s: %s%s"), *Blueprint->GetName(), bCreated ? TEXT("created, ") : TEXT(""),
			bOk ? TEXT("compiled and saved") : TEXT("FAILED"));
		bOk ? ++Compiled : ++Failed;
	}

	UE_LOG(DreamGUIEditor, Display, TEXT("DreamUICompileWidgets: %d compiled, %d failed, of %d."), Compiled, Failed, Entries.Num());
	return Failed > 0 ? 1 : 0;
}
