// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "UObject/SoftObjectPath.h"
#include "DreamTextUserWidget.generated.h"

/**
 * A user widget whose hierarchy is authored as text: one class, one .dui file.
 *
 * Nothing here builds anything. The file is read by FDreamWidgetBlueprintCompilerContext, which
 * parses it, builds the tree and installs it as the Blueprint's authored hierarchy before the class
 * declares its variables -- so from that point on this is an ordinary UDreamUserWidget in every
 * respect: the same generated class, the same member variable per widget, the same property bindings,
 * the same designer. The only thing this class adds is the pointer to where the tree came from.
 *
 * That is the whole design: text replaces the AUTHORING step, not the runtime. A class built from a
 * .dui and a class built by dragging widgets around are the same kind of class, which is what lets
 * everything already written against widget blueprints -- the preview host, the hierarchy panel, the
 * animation tracks, the nesting rules -- keep working without knowing this class exists.
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable, DisplayName = "DreamUI Text User Widget")
class DREAMGUI_API UDreamTextUserWidget : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * The .dui this class's hierarchy comes from. A class default, and it has to be one.
	 *
	 * EditDefaultsOnly rather than EditAnywhere, and this is a fork rather than a preference: one
	 * class, one tree. The file is read at COMPILE time, which is the only moment that can declare a
	 * member variable per widget, resolve a binding against the functions the class declares, check
	 * any of it, or hand the designer a hierarchy to draw. Make it instance-editable and two
	 * instances of one class can name two different files, which forces the tree to be built at run
	 * time instead -- and the variables, the compile-time checks and the designer all go with it.
	 *
	 * If run-time data-driven layout is wanted it belongs on a SECOND entry point whose reduced
	 * capability is stated out loud, never on this property pretending to do both. A widget that only
	 * tells you whether it has variables once it is running is the failure this whole pipeline exists
	 * to remove.
	 *
	 * Relative paths are rooted at the project's Content directory; see ResolveDuiFilePath.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI", meta = (FilePathFilter = "dui"))
	FFilePath DUI_File_Path;

	/**
	 * An authored path as an absolute filename: relative ones are rooted at the project's Content
	 * directory, absolute ones are left alone. Empty in, empty out.
	 *
	 * Content rather than the project directory because that is where UI source has always lived here
	 * (UIML resolved its XAML the same way), and because a path that is relative to the project root
	 * would have to start with "Content/" in every single file.
	 *
	 * Static so the compiler can resolve a path off a CDO without an instance, and so anything else
	 * that has to find the same file -- a watcher, the write-back patcher, a validation commandlet --
	 * asks this rather than keeping its own idea of what a relative path means. Two implementations of
	 * this rule is how "the editor opened it and the compiler could not find it" happens.
	 */
	static FString ResolveDuiFilePath(const FString& InPath);
};
