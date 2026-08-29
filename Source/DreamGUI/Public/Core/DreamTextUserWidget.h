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
	 * Relative paths are rooted at a `DUI/` source directory; see DreamUIPaths.h for the rule.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DreamGUI", meta = (FilePathFilter = "dui"))
	FFilePath SourceFile;

	/**
	 * An authored path as an absolute filename. Empty in, empty out.
	 *
	 * Kept as a static on this class even though DreamUIPaths::Resolve is where the rule now lives,
	 * because this is the name every caller already asks -- the compiler off a CDO, the designer's
	 * write-back, the watcher. One forwarding line is cheaper than the failure the alternative
	 * invites, which is a second implementation of "what does a relative path mean" and the
	 * "the editor opened it and the compiler could not find it" that follows.
	 */
	static FString ResolveDuiFilePath(const FString& InPath);

#if WITH_EDITOR
	/**
	 * Turns a picked path back into a portable one.
	 *
	 * The file picker hands back an absolute path, and an absolute path stored in an asset is a path
	 * that works on one machine. Nobody notices until someone else opens the project, at which point
	 * the class compiles to an empty hierarchy and the error names a drive letter they do not have.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
