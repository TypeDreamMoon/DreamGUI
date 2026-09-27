// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIScriptPackages.h"

#include "UObject/CoreRedirects.h"

namespace DreamUIScriptPackagesLocal
{
	TArray<FName>& Packages()
	{
		static TArray<FName> Registered;
		return Registered;
	}
}

void DreamUI::RegisterRuntimeScriptPackage(FName InPackageName)
{
	check(IsInGameThread());
	if (!InPackageName.IsNone())
	{
		DreamUIScriptPackagesLocal::Packages().AddUnique(InPackageName);
	}
}

void DreamUI::UnregisterRuntimeScriptPackage(FName InPackageName)
{
	check(IsInGameThread());
	DreamUIScriptPackagesLocal::Packages().Remove(InPackageName);
}

TArray<FName> DreamUI::GetRuntimeScriptPackages()
{
	return DreamUIScriptPackagesLocal::Packages();
}

bool DreamUI::IsRuntimeScriptPackage(FName InPackageName)
{
	return DreamUIScriptPackagesLocal::Packages().Contains(InPackageName);
}

bool DreamUI::IsInRuntimeScriptPackage(FStringView InObjectPath)
{
	// The package is everything before the first '.'; a path without one names a package, not an object in it.
	int32 Dot = INDEX_NONE;
	if (!InObjectPath.FindChar(TEXT('.'), Dot))
	{
		return false;
	}
	// FNAME_Find: a query adds nothing to the name table, and a name nobody made is no registered package.
	return IsRuntimeScriptPackage(FName(InObjectPath.Left(Dot), FNAME_Find));
}

FString DreamUI::ApplyTypeRedirects(ECoreRedirectFlags InType, const FString& InObjectPath)
{
	const FCoreRedirectObjectName OldName(InObjectPath);
	const FCoreRedirectObjectName NewName = FCoreRedirects::GetRedirectedName(InType, OldName);
	return NewName == OldName ? InObjectPath : NewName.ToString();
}
