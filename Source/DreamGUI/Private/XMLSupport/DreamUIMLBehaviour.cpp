// Fill out your copyright notice in the Description page of Project Settings.


#include "XMLSupport/DreamUIMLBehaviour.h"

#include "DreamGUI.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Misc/Paths.h"
#include "XMLSupport/DreamUIML.h"

UDreamUIMLBehaviour::UDreamUIMLBehaviour()
{
#if WITH_EDITORONLY_DATA
	DefaultRenderMode = EDreamRenderMode::ScreenSpaceOverlay;
#endif
}

void UDreamUIMLBehaviour::GetUIMLData(FString& XAMLFilePath, UDreamUIMLResource*& XAMLResource) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveGetUIMLData(XAMLFilePath, XAMLResource);
	}
}

FString UDreamUIMLBehaviour::ResolveUIMLPath(const FString& InPath)
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

UDreamUIMLBehaviour* UDreamUIMLBehaviour::CreateByClass(TSubclassOf<UDreamUIMLBehaviour> Class, UWorld* World
	, UDreamWidget* Parent, UDreamUIMLResource* Resources, bool IsSubTemplate
	, const TFunction<void(const TArray<UDreamWidget*>&)>& OnAllWidgetsCreated)
{
	FString XAMLFilePath;
	UDreamUIMLResource* DefaultResources = nullptr;
	GetDefault<UDreamUIMLBehaviour>(Class)->GetUIMLData(XAMLFilePath, DefaultResources);
	XAMLFilePath = ResolveUIMLPath(XAMLFilePath);
	if (XAMLFilePath.IsEmpty()) return nullptr;
	if (Resources == nullptr) Resources = DefaultResources;

	UE_LOG(DreamGUI, Log, TEXT("[%s].%d - Loading XAML: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *XAMLFilePath);
	
	return FDreamUIMLUtils(IsSubTemplate, OnAllWidgetsCreated).LoadFromFile(World, Parent, Class, Resources, XAMLFilePath);
}
