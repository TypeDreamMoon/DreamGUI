// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamPsdImportFactory.h"
#include "DreamWidgetBlueprint.h"
#include "Import/DreamPsdImporter.h"
#include "Import/DreamPsdImportSettings.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

UDreamPsdImportFactory::UDreamPsdImportFactory()
{
	SupportedClass = UDreamWidgetBlueprint::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("psd;Layered document, as a DreamUI widget"));
	Formats.Add(TEXT("psb;Large layered document, as a DreamUI widget"));
	// Ahead of the texture factory, but only for the files FactoryCanImport takes.
	ImportPriority = DefaultImportPriority + 10;
}

bool UDreamPsdImportFactory::FactoryCanImport(const FString& InFilename)
{
	const FString Extension = FPaths::GetExtension(InFilename).ToLower();
	return GetDefault<UDreamPsdImportSettings>()->bHandleContentBrowserImport && (Extension == TEXT("psd") || Extension == TEXT("psb"));
}

UObject* UDreamPsdImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFilename,
	const TCHAR* InParms, FFeedbackContext* InWarn, bool& bOutOperationCanceled)
{
	FDreamPsdImportRequest Request;
	Request.SourceFile = InFilename;
	Request.BlueprintPackage = InParent->GetOutermost()->GetName();
	Request.AssetFolder = FPackageName::GetLongPackagePath(Request.BlueprintPackage);
	Request.Name = FPaths::GetBaseFilename(InFilename);
	FDreamPsdImportReport Report;
	DreamPsdImporter::Import(Request, Report);
	if (InWarn != nullptr)
	{
		InWarn->Log(Report.ToString());
	}
	return LoadObject<UDreamWidgetBlueprint>(nullptr, *Report.BlueprintPath);
}
