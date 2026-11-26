// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "DreamUISourceSettings.generated.h"

/**
 * Where the project's .dui sources live.
 *
 * Its own class, in its own ini, rather than a field on UDreamUISettings: every Dream plugin keeps its source directory in
 * `Config/DefaultDreamSource.ini`, one section each, so that Project Settings shows them together under "Dream Source"
 * and the VSCode extensions -- which cannot ask the engine -- have exactly one file to read. The section name
 * (`[/Script/DreamGUI.DreamUISourceSettings]`) and the key are a contract with dreamgui-language-support; see
 * Plan/DreamSource/dreamsource-layout-spec.md section 11.
 *
 * Resolved by DreamUIPaths, never read directly: the legacy `DUI/` directory still answers when the configured one does
 * not exist, and only DreamUIPaths knows that rule.
 */
UCLASS(Config = DreamSource, DefaultConfig, meta = (DisplayName = "UI"))
class DREAMGUI_API UDreamUISourceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamUISourceSettings();

	/**
	 * The directory holding the project's .dui files, relative to the project directory. A plugin's own sources are not
	 * configurable: they live in `<Plugin>/DreamSource/UI`.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Source", meta = (RelativeToGameDir))
	FDirectoryPath SourceDirectory;

	/** The default value, `DreamSource/UI`. */
	static const TCHAR* GetDefaultSourceDirectory();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Dream Source"); }
	virtual FName GetSectionName() const override { return TEXT("UI"); }
};
