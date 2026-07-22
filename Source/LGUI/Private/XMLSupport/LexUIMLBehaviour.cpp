// Fill out your copyright notice in the Description page of Project Settings.


#include "XMLSupport/LexUIMLBehaviour.h"

#include "LGUI.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "Misc/Paths.h"
#include "XMLSupport/LexUIML.h"

ULexUIMLBehaviour::ULexUIMLBehaviour()
{
	DefaultRenderMode = ELexRenderMode::ScreenSpaceOverlay;
}

void ULexUIMLBehaviour::GetUIMLData(FString& XAMLFilePath, ULexUIMLResource*& XAMLResource) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveGetUIMLData(XAMLFilePath, XAMLResource);
	}
}

FString ULexUIMLBehaviour::ResolveUIMLPath(const FString& InPath)
{
	FString ResolvedPath = InPath.TrimStartAndEnd();
	if (ResolvedPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectContentDir(), ResolvedPath);
	}
	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	FPaths::NormalizeFilename(ResolvedPath);
	return ResolvedPath;
}

ULexUIMLBehaviour* ULexUIMLBehaviour::CreateByClass(TSubclassOf<ULexUIMLBehaviour> Class, UWorld* World
	, ULexWidget* Parent, ULexUIMLResource* Resources, bool IsSubTemplate
	, const TFunction<void(const TArray<ULexWidget*>&)>& OnAllWidgetsCreated)
{
	FString XAMLFilePath;
	ULexUIMLResource* DefaultResources = nullptr;
	GetDefault<ULexUIMLBehaviour>(Class)->GetUIMLData(XAMLFilePath, DefaultResources);
	XAMLFilePath = ResolveUIMLPath(XAMLFilePath);
	if (XAMLFilePath.IsEmpty()) return nullptr;
	if (Resources == nullptr) Resources = DefaultResources;

	UE_LOG(LGUI, Log, TEXT("[%s].%d - Loading XAML: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *XAMLFilePath);
	
	return FLexUIMLUtils(IsSubTemplate, OnAllWidgetsCreated).LoadFromFile(World, Parent, Class, Resources, XAMLFilePath);
}
