// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUILocalizationManifest.h"

#include "Text/DreamUIPaths.h"
#include "Text/DreamUITextBuilder.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DreamUILocalizationManifestLocal
{
	FString Quote(const FString& InText)
	{
		FString Out;
		Out.Reserve(InText.Len() + 2);
		Out.AppendChar(TEXT('"'));
		for (const TCHAR Char : InText)
		{
			switch (Char)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), (uint32)Char);
				}
				else
				{
					Out.AppendChar(Char);
				}
				break;
			}
		}
		Out.AppendChar(TEXT('"'));
		return Out;
	}

	void AddMember(TArray<FString>& InOutMembers, const TCHAR* InName, const FString& InValue)
	{
		InOutMembers.Add(FString::Printf(TEXT("\"%s\": %s"), InName, *Quote(InValue)));
	}
}

bool DreamUILocalizationManifest::GetPaths(const FString& InSourcePath, FString& OutManifestPath, FString& OutSource, FString& OutRootToken)
{
	FString Source = FPaths::ConvertRelativePathToFull(InSourcePath);
	FPaths::NormalizeFilename(Source);
	for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
	{
		FString Directory = Root.Directory;
		FPaths::NormalizeFilename(Directory);
		if (!Directory.EndsWith(TEXT("/")))
		{
			Directory += TEXT("/");
		}
		if (!Source.StartsWith(Directory, ESearchCase::IgnoreCase))
		{
			continue;
		}
		// The folder that holds DUI/: the project's, or the plugin's.
		const FString Owner = FPaths::GetPath(Directory.LeftChop(1));
		OutSource = FString(DreamUIPaths::SourceDirectoryName) / Source.RightChop(Directory.Len());
		OutRootToken = Root.RootToken;
		OutManifestPath = Owner / TEXT("Intermediate/DLoc/extract/DreamGUI") / (OutSource + TEXT(".json"));
		return true;
	}
	return false;
}

FString DreamUILocalizationManifest::ToJson(const FString& InSource, const FString& InRootToken, const FDreamUILocalization& InLocalization)
{
	using namespace DreamUILocalizationManifestLocal;
	FString Json = TEXT("{\n");
	Json += TEXT("  \"version\": 1,\n");
	Json += TEXT("  \"provider\": \"DreamGUI\",\n");
	if (!InRootToken.IsEmpty())
	{
		Json += FString::Printf(TEXT("  \"root\": %s,\n"), *Quote(InRootToken));
	}
	Json += FString::Printf(TEXT("  \"source\": %s,\n"), *Quote(InSource));
	Json += FString::Printf(TEXT("  \"namespace\": %s,\n"), *Quote(InLocalization.Namespace));
	if (InLocalization.Texts.Num() == 0)
	{
		Json += TEXT("  \"entries\": []\n}\n");
		return Json;
	}
	Json += TEXT("  \"entries\": [\n");
	for (int32 Index = 0; Index < InLocalization.Texts.Num(); ++Index)
	{
		const FDreamUILocalizableText& Text = InLocalization.Texts[Index];
		TArray<FString> Members;
		AddMember(Members, TEXT("key"), Text.Key);
		AddMember(Members, TEXT("src"), Text.Source);
		AddMember(Members, TEXT("at"), Text.NodePath);
		if (!Text.Note.IsEmpty())
		{
			AddMember(Members, TEXT("note"), Text.Note);
		}
		if (Text.Max >= 0)
		{
			Members.Add(FString::Printf(TEXT("\"max\": %d"), Text.Max));
		}
		if (!Text.NodeType.IsEmpty())
		{
			AddMember(Members, TEXT("kind"), Text.NodeType);
		}
		if (!Text.RenamedFrom.IsEmpty())
		{
			AddMember(Members, TEXT("renamedFrom"), Text.RenamedFrom);
		}
		Json += TEXT("    { ") + FString::Join(Members, TEXT(", ")) + (Index + 1 < InLocalization.Texts.Num() ? TEXT(" },\n") : TEXT(" }\n"));
	}
	Json += TEXT("  ]\n}\n");
	return Json;
}

bool DreamUILocalizationManifest::Write(const FString& InSourcePath, const FDreamUILocalization& InLocalization)
{
	FString ManifestPath;
	FString Source;
	FString RootToken;
	if (!GetPaths(InSourcePath, ManifestPath, Source, RootToken))
	{
		return false;
	}
	const FString Json = ToJson(Source, RootToken, InLocalization);
	FString Existing;
	if (IFileManager::Get().FileExists(*ManifestPath) && FFileHelper::LoadFileToString(Existing, *ManifestPath) && Existing == Json)
	{
		return true;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ManifestPath), /*Tree*/ true);
	return FFileHelper::SaveStringToFile(Json, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
