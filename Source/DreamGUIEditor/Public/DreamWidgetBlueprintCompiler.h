// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"

class UDreamWidgetBlueprint;
class UDreamWidgetGeneratedClass;
class UDreamWidgetTree;

/**
 * Compiles a DreamUI hierarchy into a class.
 *
 * The work is the part Kismet does not already do: declare one member variable per authored widget,
 * copy the authored hierarchy onto the generated class as its archetype, and refuse a binding whose
 * widget is gone. Everything else -- functions, the graph, the CDO -- is FKismetCompilerContext's.
 *
 * Registered through FKismetCompilerContext::RegisterCompilerForBP, which is independent of any
 * editor: the asset opens in the stock Blueprint editor and compiles through the stock button. A
 * DreamUI designer surface can be added later without this file changing.
 */
class DREAMGUIEDITOR_API FDreamWidgetBlueprintCompilerContext : public FKismetCompilerContext
{
protected:
	using Super = FKismetCompilerContext;

public:
	FDreamWidgetBlueprintCompilerContext(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	virtual ~FDreamWidgetBlueprintCompilerContext() override;

	/** The variable name a widget is exposed under. Delegates to the runtime rule; never reimplement it. */
	static FName MakeWidgetVariableName(const UDreamWidget* InWidget);

protected:
	// FKismetCompilerContext
	virtual void SpawnNewClass(const FString& NewClassName) override;
	virtual void OnNewClassSet(UBlueprintGeneratedClass* ClassToUse) override;
	virtual void EnsureProperGeneratedClass(UClass*& InOutTargetClass) override;
	virtual void CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOutOldCDO) override;
	virtual void SaveSubObjectsFromCleanAndSanitizeClass(FSubobjectCollection& SubObjectsToSave, UBlueprintGeneratedClass* ClassToClean) override;
	/**
	 * Declare one member variable per authored widget.
	 *
	 * This hook and not CreateClassVariablesFromBlueprint: the base resets GeneratedVariables
	 * immediately before calling this and then turns the list into properties, so a description added
	 * anywhere else is either wiped or too late.
	 */
	virtual void PopulateBlueprintGeneratedVariables() override;
	virtual void FinishCompilingClass(UClass* Class) override;
	// End FKismetCompilerContext

	UDreamWidgetBlueprint* DreamWidgetBlueprint() const;

private:
	/** Duplicate the authored hierarchy onto the class. Editing the archetype in place would mutate live templates. */
	void UpdateGeneratedClassWidgetTree(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetGeneratedClass* InClass);

	/**
	 * Report every property that declares a widget binding no widget answers.
	 *
	 * This is the whole reason the class model is worth the trouble: under prefabs the same mistake
	 * surfaced at runtime as a null, after a save had already dropped it.
	 */
	void ValidateWidgetBindings(UClass* InClass);

	UDreamWidgetGeneratedClass* NewDreamWidgetClass = nullptr;
	/** The class's previous archetype, kept across the sanitize pass so it can be patched over. */
	UDreamWidgetTree* OldWidgetTree = nullptr;
};
