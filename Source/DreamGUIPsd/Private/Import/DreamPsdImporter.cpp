// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Import/DreamPsdImporter.h"
#include "Import/DreamPsdImportSettings.h"
#include "Psd/DreamPsdReader.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "TextureCompiler.h"
#include "Text/DreamUIPaths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamPsdImport, Log, All);

// ---------------------------------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------------------------------

UDreamPsdImportSettings::UDreamPsdImportSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("DreamGUI Layered Document Import");
}

FDreamPsdMapOptions UDreamPsdImportSettings::MakeMapOptions(const FString& InDocumentName, const FString& InTextureFolder) const
{
	FDreamPsdMapOptions O;
	O.DocumentName = InDocumentName;
	O.TextureFolder = InTextureFolder;
	for (const TPair<FString, TSoftObjectPtr<UDreamUIFontData_BaseObject>>& Entry : FontMap)
	{
		if (!Entry.Value.IsNull())
		{
			O.FontMap.Add(Entry.Key, Entry.Value.ToSoftObjectPath().ToString());
		}
	}
	O.DefaultFontPath = DefaultFont.IsNull() ? FString() : DefaultFont.ToSoftObjectPath().ToString();
	O.bRasterizeText = bRasterizeText;
	O.bImportHiddenLayers = bImportHiddenLayers;
	O.bTrimTransparentPixels = bTrimTransparentPixels;
	O.bNamingConventions = bNamingConventions;
	O.bBakeLayerMasks = bBakeLayerMasks;
	O.MaskFeatherScale = MaskFeatherScale;
	O.MaxGradientTextureSize = MaxGradientTextureSize;
	return O;
}

// ---------------------------------------------------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------------------------------------------------

FString FDreamPsdImportReport::ToString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%s %s"), bSucceeded ? TEXT("Imported") : TEXT("Did not import"), *DuiFile));
	if (!BlueprintPath.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Blueprint: %s"), *BlueprintPath));
	}
	Lines.Add(Map.Summarize());
	for (const FDreamPsdMessage& M : Messages.Items)
	{
		Lines.Add(M.ToString());
	}
	for (const FDreamPsdMessage& M : Map.Messages.Items)
	{
		if (M.Severity != FDreamPsdMessage::ESeverity::Info)
		{
			Lines.Add(M.ToString());
		}
	}
	for (const FString& C : Merge.Conflicts)
	{
		Lines.Add(TEXT("conflict: ") + C);
	}
	for (const FString& E : Merge.Errors)
	{
		Lines.Add(TEXT("error: ") + E);
	}
	if (UnusedTextures.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("No longer used (kept): %s"), *FString::Join(UnusedTextures, TEXT(", "))));
	}
	for (const FString& C : CompileMessages)
	{
		Lines.Add(TEXT("compile: ") + C);
	}
	return FString::Join(Lines, TEXT("\n"));
}

// ---------------------------------------------------------------------------------------------------------------------
// The import
// ---------------------------------------------------------------------------------------------------------------------

namespace DreamPsdImporterLocal
{
	bool SavePackage(UPackage* InPackage, UObject* InAsset, FDreamPsdMessages& OutMessages)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(InPackage->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(InPackage, InAsset, *Filename, Args))
		{
			OutMessages.Error(FString::Printf(TEXT("could not save %s"), *Filename));
			return false;
		}
		return true;
	}

	bool WriteText(const FString& InFile, const FString& InText, FDreamPsdMessages& OutMessages)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(InFile), true);
		if (!FFileHelper::SaveStringToFile(InText, *InFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutMessages.Error(FString::Printf(TEXT("could not write %s (is it read-only?)"), *InFile));
			return false;
		}
		return true;
	}

	FString ClassPathOf(const FString& InPackage)
	{
		return InPackage;
	}
}

FString DreamPsdImporter::DefaultDuiFile(const FString& InBlueprintPackage)
{
	FString Relative = InBlueprintPackage;
	Relative.RemoveFromStart(TEXT("/"));
	int32 Slash = INDEX_NONE;
	if (Relative.FindChar(TEXT('/'), Slash))
	{
		Relative.RightChopInline(Slash + 1);
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName, Relative + DreamUIPaths::SourceExtension));
}

TArray<UTexture2D*> DreamPsdImporter::MakeTextures(const TArray<FDreamPsdTextureRequest>& InRequests, bool bInSave, FDreamPsdMessages& OutMessages)
{
	TArray<UTexture2D*> Made;
	for (const FDreamPsdTextureRequest& R : InRequests)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(R.ObjectPath);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UTexture2D* Texture = FindObject<UTexture2D>(Package, *R.Name);
		const bool bNew = Texture == nullptr;
		if (bNew)
		{
			Texture = NewObject<UTexture2D>(Package, *R.Name, RF_Public | RF_Standalone | RF_Transactional);
		}
		else
		{
			Texture->PreEditChange(nullptr);
		}
		if (R.bGray)
		{
			Texture->Source.Init(R.Gray.Width, R.Gray.Height, 1, 1, TSF_G8, R.Gray.Pixels.GetData());
			Texture->CompressionSettings = TC_Grayscale;
			Texture->SRGB = false;
		}
		else
		{
			// FColor is laid out B, G, R, A: the source format's own order.
			Texture->Source.Init(R.Image.Width, R.Image.Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(R.Image.Pixels.GetData()));
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->SRGB = true;
		}
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->AddressX = R.bTile ? TA_Wrap : TA_Clamp;
		Texture->AddressY = R.bTile ? TA_Wrap : TA_Clamp;
		Texture->PostEditChange();
		if (bNew)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		Package->MarkPackageDirty();
		Made.Add(Texture);
	}
	if (Made.Num() > 0)
	{
		TArray<UTexture*> ToFinish(Made);
		FTextureCompilingManager::Get().FinishCompilation(ToFinish);
	}
	if (bInSave)
	{
		for (UTexture2D* Texture : Made)
		{
			DreamPsdImporterLocal::SavePackage(Texture->GetOutermost(), Texture, OutMessages);
		}
	}
	return Made;
}

bool DreamPsdImporter::Import(const FDreamPsdImportRequest& InRequest, FDreamPsdImportReport& Out)
{
	using namespace DreamPsdImporterLocal;
	Out = FDreamPsdImportReport();
	FDreamPsdDocument Document;
	if (!DreamPsdReader::ReadFile(InRequest.SourceFile, Document, Out.Messages))
	{
		return false;
	}
	const FString Name = InRequest.Name.IsEmpty() ? DreamPsdDui::AssetSafe(FPaths::GetBaseFilename(InRequest.SourceFile), TEXT("Document")) : InRequest.Name;
	const UDreamPsdImportSettings* Settings = GetDefault<UDreamPsdImportSettings>();
	FString Folder = InRequest.AssetFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	const FString BlueprintPackage = InRequest.BlueprintPackage.IsEmpty() ? FString::Printf(TEXT("%s/%s%s"), *Folder, *Settings->BlueprintPrefix, *Name) : InRequest.BlueprintPackage;
	const FString BlueprintName = FPackageName::GetShortName(BlueprintPackage);
	const FString TextureFolder = FString::Printf(TEXT("%s/%s"), *FPackageName::GetLongPackagePath(BlueprintPackage), *Name);
	Out.DuiFile = InRequest.DuiFile.IsEmpty() ? DefaultDuiFile(BlueprintPackage) : FPaths::ConvertRelativePathToFull(InRequest.DuiFile);
	Out.BlueprintPath = BlueprintPackage + TEXT(".") + BlueprintName;

	FDreamPsdMapOptions Options = InRequest.MapOptions.IsSet() ? InRequest.MapOptions.GetValue() : Settings->MakeMapOptions(Name, TextureFolder);
	Options.DocumentName = Name;
	Options.TextureFolder = TextureFolder;
	if (Options.DefaultFontPath.IsEmpty())
	{
		if (UDreamUIFontData_BaseObject* Font = UDreamUIFontData_BaseObject::GetDefaultFont())
		{
			Options.DefaultFontPath = Font->GetPathName();
		}
	}
	FDreamPsdMapper Mapper(Document, Options);
	if (!Mapper.Map(Out.Map))
	{
		return false;
	}

	// The .dui: fresh, or the previous import's brought up to this one.
	const FString ManifestFile = FDreamPsdImportManifest::PathFor(Out.DuiFile);
	FDreamPsdImportManifest Last;
	bool bHasLast = false;
	{
		FString Json;
		bHasLast = FFileHelper::LoadFileToString(Json, *ManifestFile) && FDreamPsdImportManifest::FromJson(Json, Last);
	}
	FString OldText;
	if (FFileHelper::LoadFileToString(OldText, *Out.DuiFile))
	{
		Out.bReimport = true;
		if (!DreamPsdDuiMerge::Merge(OldText, bHasLast ? &Last : nullptr, Out.Map.Root, Out.DuiText, Out.Merge))
		{
			Out.Messages.Error(TEXT("the existing .dui could not be merged into; nothing was written"));
			return false;
		}
		if (!bHasLast)
		{
			Out.Messages.Warn(TEXT("the .dui has no manifest from a previous import: every line the importer writes was rewritten, and none it wrote before was taken out"));
		}
	}
	else
	{
		Out.DuiText = DreamPsdDui::Print(Out.Map.Root, ClassPathOf(BlueprintPackage), FPaths::GetCleanFilename(InRequest.SourceFile));
	}

	FDreamPsdImportManifest Manifest = FDreamPsdImportManifest::FromTree(Out.Map.Root);
	Manifest.SourceFile = InRequest.SourceFile;
	Manifest.TextureFolder = TextureFolder;
	Manifest.BlueprintPath = Out.BlueprintPath;
	for (const FDreamPsdTextureRequest& T : Out.Map.Textures)
	{
		Manifest.Textures.AddUnique(T.ObjectPath);
		Out.Textures.AddUnique(T.ObjectPath);
	}
	if (bHasLast)
	{
		for (const FString& T : Last.Textures)
		{
			if (!Manifest.Textures.Contains(T))
			{
				Out.UnusedTextures.Add(T);
			}
		}
	}
	if (InRequest.bDryRun)
	{
		Out.bSucceeded = true;
		return true;
	}

	MakeTextures(Out.Map.Textures, InRequest.bSave, Out.Messages);
	if (!WriteText(Out.DuiFile, Out.DuiText, Out.Messages) || !WriteText(ManifestFile, Manifest.ToJson(), Out.Messages))
	{
		return false;
	}

	if (InRequest.bCreateBlueprint)
	{
		UPackage* Package = CreatePackage(*BlueprintPackage);
		Package->FullyLoad();
		UDreamWidgetBlueprint* Blueprint = FindObject<UDreamWidgetBlueprint>(Package, *BlueprintName);
		if (Blueprint == nullptr)
		{
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDreamTextUserWidget::StaticClass(), Package,
				FName(*BlueprintName), BPTYPE_Normal, UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				Out.Messages.Error(FString::Printf(TEXT("could not make the Blueprint %s"), *BlueprintPackage));
				return false;
			}
			FAssetRegistryModule::AssetCreated(Blueprint);
			DreamUITextAuthoring::SetAuthoredSourcePath(Blueprint, Out.DuiFile);
		}
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error || Message->GetSeverity() == EMessageSeverity::Warning)
			{
				Out.CompileMessages.Add(Message->ToText().ToString());
			}
		}
		Package->MarkPackageDirty();
		if (InRequest.bSave)
		{
			SavePackage(Package, Blueprint, Out.Messages);
		}
		if (Results.NumErrors > 0)
		{
			Out.Messages.Error(FString::Printf(TEXT("the Blueprint does not compile from the .dui (%d errors)"), Results.NumErrors));
		}
	}
	Out.bSucceeded = !Out.Messages.HasErrors();
	UE_LOG(LogDreamPsdImport, Log, TEXT("%s"), *Out.ToString());
	return Out.bSucceeded;
}

bool DreamPsdImporter::CanReimport(const UDreamWidgetBlueprint* InBlueprint)
{
	const FString Source = InBlueprint != nullptr ? DreamUITextAuthoring::GetAuthoredSourcePath(InBlueprint) : FString();
	if (Source.IsEmpty())
	{
		return false;
	}
	const FString File = UDreamTextUserWidget::ResolveDuiFilePath(Source);
	return FPaths::FileExists(FDreamPsdImportManifest::PathFor(File));
}

bool DreamPsdImporter::Reimport(UDreamWidgetBlueprint* InBlueprint, FDreamPsdImportReport& Out)
{
	Out = FDreamPsdImportReport();
	if (!CanReimport(InBlueprint))
	{
		Out.Messages.Error(TEXT("the Blueprint's .dui was not made by an import: there is no manifest beside it"));
		return false;
	}
	const FString File = UDreamTextUserWidget::ResolveDuiFilePath(DreamUITextAuthoring::GetAuthoredSourcePath(InBlueprint));
	FString Json;
	FDreamPsdImportManifest Manifest;
	if (!FFileHelper::LoadFileToString(Json, *FDreamPsdImportManifest::PathFor(File)) || !FDreamPsdImportManifest::FromJson(Json, Manifest))
	{
		Out.Messages.Error(TEXT("the import's manifest does not read"));
		return false;
	}
	if (!FPaths::FileExists(Manifest.SourceFile))
	{
		Out.Messages.Error(FString::Printf(TEXT("the document %s is not there any more"), *Manifest.SourceFile));
		return false;
	}
	FDreamPsdImportRequest Request;
	Request.SourceFile = Manifest.SourceFile;
	Request.DuiFile = File;
	Request.BlueprintPackage = InBlueprint->GetOutermost()->GetName();
	Request.AssetFolder = FPackageName::GetLongPackagePath(Request.BlueprintPackage);
	Request.Name = FPaths::GetCleanFilename(Manifest.TextureFolder);
	return Import(Request, Out);
}
