// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "DreamWidgetGeneratedClass.generated.h"

class UDreamUserWidget;
class UDreamWidget;
class UDreamWidgetTree;

/**
 * The class a DreamUI hierarchy compiles into -- UMG's UWidgetBlueprintGeneratedClass, for DreamUI.
 *
 * It carries the authored hierarchy as a template on the class itself, which is the whole point: a
 * prefab's hierarchy lived in a binary blob that only its own deserializer could read, so it could
 * never be a type. Here it is an ordinary object graph hanging off the class, instanced per instance
 * by FObjectInstancingGraph like any other archetype.
 */
UCLASS()
class DREAMGUI_API UDreamWidgetGeneratedClass : public UBlueprintGeneratedClass
{
	GENERATED_BODY()

public:
	/** The authored hierarchy this class instantiates. Null on a class that inherits its parent's. */
	UDreamWidgetTree* GetWidgetTreeArchetype() const { return WidgetTree; }

	/** The property bindings the compiler resolved for this class. Not inherited: see the getter below. */
	const TArray<FDreamWidgetPropertyBinding>& GetPropertyBindings() const { return PropertyBindings; }

	/**
	 * Every binding that applies to an instance of InClass, its ancestors' included.
	 *
	 * Bindings accumulate down the chain where the tree does not: a subclass that adds none still has
	 * to honour its parent's, and one that adds some does not replace them.
	 */
	static void CollectPropertyBindings(const UClass* InClass, TArray<FDreamWidgetPropertyBinding>& OutBindings);

#if WITH_EDITOR
	/** Compiler-only: hand the class the tree it will instance. */
	void SetWidgetTreeArchetype(UDreamWidgetTree* InWidgetTree);
	/** Compiler-only: hand the class the bindings it resolved. */
	void SetPropertyBindings(TArray<FDreamWidgetPropertyBinding> InBindings);
#endif

	/**
	 * The nearest class at or above InClass that declares a tree of its own.
	 *
	 * A subclass that only adds logic declares none, and must instance its parent's -- otherwise
	 * subclassing a screen to change one function would silently produce an empty screen.
	 */
	static UDreamWidgetTree* FindWidgetTreeArchetype(const UClass* InClass);

	/** Build InUserWidget's contents from this class. */
	void InitializeWidget(UDreamUserWidget* InUserWidget) const;

	/**
	 * Instance InWidgetTreeArchetype into InUserWidget and resolve the by-name bindings.
	 *
	 * Static and archetype-taking so a class that inherited its tree can pass the ancestor's, and so
	 * the whole step is testable without compiling a Blueprint. Mirrors
	 * UWidgetBlueprintGeneratedClass::InitializeWidgetStatic, whose three steps this follows:
	 * instance the tree, bind each widget to the same-named class property, then hand over.
	 */
	static void InitializeWidgetStatic(UDreamUserWidget* InUserWidget, const UClass* InClass, UDreamWidgetTree* InWidgetTreeArchetype);

	virtual void PurgeClass(bool bRecompilingOnLoad) override;

private:
	/**
	 * Persistent -- this is the class's data. DuplicateTransient because a duplicated class is about to
	 * be recompiled and will be handed a freshly generated tree; carrying the old one over would leave
	 * a copy pointing at an archetype nobody owns. Same reasoning as UMG's.
	 */
	UPROPERTY(DuplicateTransient)
	TObjectPtr<UDreamWidgetTree> WidgetTree = nullptr;

	/** Persistent and DuplicateTransient for the same reasons as WidgetTree. */
	UPROPERTY(DuplicateTransient)
	TArray<FDreamWidgetPropertyBinding> PropertyBindings;
};
