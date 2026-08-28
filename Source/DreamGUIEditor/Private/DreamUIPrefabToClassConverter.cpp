// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIPrefabToClassConverter.h"
#include "DreamWidgetBlueprint.h"

#include "PrefabSystem/DreamUIPrefab.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"

#include "Engine/World.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabToClass"

namespace DreamUIPrefabToClass
{
	namespace Private
	{
		/** A throwaway world to load the prefab into. Conversion reads data; it must not touch a real one. */
		struct FScopedConversionWorld
		{
			UWorld* World = nullptr;
			FScopedConversionWorld() { World = UWorld::CreateWorld(EWorldType::None, false); }
			~FScopedConversionWorld() { if (World) { World->DestroyWorld(false); } }
		};

		/**
		 * Load the prefab into a tree of its own.
		 *
		 * LoadPrefabInEditor rather than LoadPrefab: this reads the authored data and must not run
		 * Awake or any other runtime lifecycle. It takes the outer directly, so the tree is handed in
		 * rather than minted for us -- which is the whole reason this is a couple of lines instead of
		 * a reimplementation of the loader.
		 */
		UDreamWidgetTree* LoadPrefabIntoTree(UDreamUIPrefab* InPrefab, UWorld* InWorld)
		{
			UDreamWidgetTree* Tree = NewObject<UDreamWidgetTree>(InWorld);
			UDreamWidget* Root = InPrefab->LoadPrefabInEditor(InWorld, Tree, nullptr);
			if (!IsValid(Root))
			{
				return nullptr;
			}
			Tree->RootWidget = Root;
			return Tree;
		}

		/**
		 * Tear down a hierarchy loaded for conversion.
		 *
		 * LoadPrefabInEditor registers what it builds, and a registered widget that reaches
		 * BeginDestroy without its owner having destroyed it logs an error and cleans up after itself.
		 * Destroying the world is not enough -- the widgets are not the world's to destroy. Conversion
		 * loads several hierarchies per run, so leaving them is a stream of errors, not one.
		 */
		void DestroyLoadedTree(UDreamWidgetTree* InTree)
		{
			if (InTree != nullptr && IsValid(InTree->RootWidget))
			{
				InTree->RootWidget->DestroyWidget();
			}
		}

		/** "Root/Header/Caption" -- identity for comparison, since object names here are generated. */
		void CollectPaths(const UDreamWidget* InWidget, const FString& InPrefix, TArray<FString>& OutPaths, TArray<const UDreamWidget*>& OutWidgets)
		{
			const FString Path = InPrefix.IsEmpty() ? InWidget->GetDisplayName() : InPrefix + TEXT("/") + InWidget->GetDisplayName();
			OutPaths.Add(Path);
			OutWidgets.Add(InWidget);
			for (const UDreamWidget* Child : InWidget->GetChildren())
			{
				if (Child != nullptr)
				{
					CollectPaths(Child, Path, OutPaths, OutWidgets);
				}
			}
		}

		/** Property-level comparison of one matched pair. Appends a line per difference. */
		void CompareWidgetValues(const UDreamWidget* InLeft, const UDreamWidget* InRight, const FString& InPath, TArray<FString>& OutDifferences)
		{
			if (InLeft->GetClass() != InRight->GetClass())
			{
				OutDifferences.Add(FString::Printf(TEXT("%s: class %s vs %s"), *InPath, *InLeft->GetClass()->GetName(), *InRight->GetClass()->GetName()));
				return;
			}

			for (TFieldIterator<FProperty> It(InLeft->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				FProperty* Property = *It;
				// Transient properties are derived state, not authored data, and comparing them would
				// report differences that mean nothing (Parent, cached transforms, registration bits).
				if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
				{
					continue;
				}

				const void* LeftValue = Property->ContainerPtrToValuePtr<void>(InLeft);
				const void* RightValue = Property->ContainerPtrToValuePtr<void>(InRight);

				// Object references are compared for shape only -- see the header. The two hierarchies
				// are different objects, so identity can never match and would drown the real report.
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* LeftObject = ObjectProperty->GetObjectPropertyValue(LeftValue);
					const UObject* RightObject = ObjectProperty->GetObjectPropertyValue(RightValue);
					if ((LeftObject == nullptr) != (RightObject == nullptr))
					{
						OutDifferences.Add(FString::Printf(TEXT("%s.%s: one side is null"), *InPath, *Property->GetName()));
					}
					else if (LeftObject != nullptr && LeftObject->GetClass() != RightObject->GetClass())
					{
						OutDifferences.Add(FString::Printf(TEXT("%s.%s: %s vs %s"), *InPath, *Property->GetName(),
							*LeftObject->GetClass()->GetName(), *RightObject->GetClass()->GetName()));
					}
					continue;
				}
				// An array of objects is the hierarchy itself (Children) or something shaped like it;
				// its structure is already covered by the path comparison.
				if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
				{
					if (CastField<FObjectPropertyBase>(ArrayProperty->Inner) != nullptr)
					{
						continue;
					}
				}

				if (!Property->Identical(LeftValue, RightValue, PPF_None))
				{
					OutDifferences.Add(FString::Printf(TEXT("%s.%s differs"), *InPath, *Property->GetName()));
				}
			}
		}
	}

	FString FConversionResult::ToString() const
	{
		TStringBuilder<512> Builder;
		Builder.Appendf(TEXT("%s: %d widgets"), Blueprint != nullptr ? *Blueprint->GetName() : TEXT("<failed>"), WidgetCount);
		for (const FString& Warning : Warnings)
		{
			Builder.Appendf(TEXT("\n  warning: %s"), *Warning);
		}
		for (const FString& Error : Errors)
		{
			Builder.Appendf(TEXT("\n  error: %s"), *Error);
		}
		return Builder.ToString();
	}

	FConversionResult ConvertPrefab(UDreamUIPrefab* InPrefab, const FString& InTargetPackageName)
	{
		using namespace Private;

		FConversionResult Result;
		if (!IsValid(InPrefab))
		{
			Result.Errors.Add(TEXT("No prefab to convert."));
			return Result;
		}
		if (InTargetPackageName.IsEmpty())
		{
			Result.Errors.Add(TEXT("No target package name."));
			return Result;
		}

		FScopedConversionWorld ConversionWorld;
		UDreamWidgetTree* SourceTree = LoadPrefabIntoTree(InPrefab, ConversionWorld.World);
		if (SourceTree == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("Prefab '%s' produced no hierarchy when loaded."), *InPrefab->GetPathName()));
			return Result;
		}

		UPackage* Package = CreatePackage(*InTargetPackageName);
		if (Package == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create package '%s'."), *InTargetPackageName));
			return Result;
		}
		const FString AssetName = FPackageName::GetShortName(InTargetPackageName);

		UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UDreamUserWidget::StaticClass(), Package, FName(*AssetName), BPTYPE_Normal,
			UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		if (Blueprint == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create a Blueprint at '%s'."), *InTargetPackageName));
			return Result;
		}

		// A duplicate into the Blueprint, not the loaded objects themselves: the loaded hierarchy lives
		// in a world that is about to be destroyed, and the Blueprint has to own what it holds.
		UDreamWidgetTree* AuthoringTree = DuplicateObject<UDreamWidgetTree>(SourceTree, Blueprint);
		// The copy is taken; the loaded originals have served their purpose and are registered, so
		// they have to be torn down explicitly rather than left for the world's destruction.
		DestroyLoadedTree(SourceTree);
		if (AuthoringTree == nullptr || !IsValid(AuthoringTree->RootWidget))
		{
			Result.Errors.Add(TEXT("The hierarchy did not survive being copied into the Blueprint."));
			return Result;
		}
		// Parent is DuplicateTransient, so the copy arrives with empty back-pointers; the compiler
		// walks this tree by name and would see a hierarchy of orphans without this.
		AuthoringTree->RebuildParentLinks();
		Blueprint->WidgetTree = AuthoringTree;
		Result.WidgetCount = AuthoringTree->CountWidgets();

		// Display names are the variable names, so a collision is a variable that silently binds to
		// whichever widget came first. Worth reporting on the way in rather than as a compiler warning
		// on an asset the user did not author by hand.
		{
			TSet<FName> SeenNames;
			AuthoringTree->ForEachWidget([&SeenNames, &Result](UDreamWidget* Widget)
			{
				const FName VariableName = UDreamWidgetTree::MakeWidgetVariableName(Widget);
				bool bAlreadySeen = false;
				SeenNames.Add(VariableName, &bAlreadySeen);
				if (bAlreadySeen)
				{
					Result.Warnings.Add(FString::Printf(TEXT("More than one widget is named '%s'; only the first becomes a variable."), *VariableName.ToString()));
				}
			});
		}

		// SkipGarbageCollection: a migration converts assets by the hundred, and collecting after every
		// one is both slow and disruptive -- it drags unrelated pending garbage down mid-conversion,
		// which surfaces as other people's teardown warnings attributed to whatever is running.
		FCompilerResultsLog CompileResults;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &CompileResults);
		if (CompileResults.NumErrors > 0)
		{
			Result.Errors.Add(FString::Printf(TEXT("The converted Blueprint did not compile (%d errors)."), CompileResults.NumErrors));
		}

		FAssetRegistryModule::AssetCreated(Blueprint);
		Package->MarkPackageDirty();

		Result.Blueprint = Blueprint;
		return Result;
	}

	bool VerifyFidelity(UDreamUIPrefab* InPrefab, UClass* InGeneratedClass, UWorld* InWorld, TArray<FString>& OutDifferences)
	{
		using namespace Private;

		OutDifferences.Reset();
		if (!IsValid(InPrefab) || InGeneratedClass == nullptr || !IsValid(InWorld))
		{
			OutDifferences.Add(TEXT("Nothing to compare."));
			return false;
		}

		UDreamWidgetTree* PrefabTree = LoadPrefabIntoTree(InPrefab, InWorld);
		if (PrefabTree == nullptr)
		{
			OutDifferences.Add(TEXT("The prefab produced no hierarchy."));
			return false;
		}

		UDreamUserWidget* Instance = CreateDreamWidget(InWorld, InGeneratedClass);
		if (!IsValid(Instance) || Instance->GetContentRoot() == nullptr)
		{
			OutDifferences.Add(TEXT("The class produced no hierarchy."));
			return false;
		}

		TArray<FString> PrefabPaths, ClassPaths;
		TArray<const UDreamWidget*> PrefabWidgets, ClassWidgets;
		CollectPaths(PrefabTree->RootWidget, FString(), PrefabPaths, PrefabWidgets);
		CollectPaths(Instance->GetContentRoot(), FString(), ClassPaths, ClassWidgets);

		// Report what is missing and what is extra separately. "The counts differ" is not actionable;
		// a name is.
		for (const FString& PrefabPath : PrefabPaths)
		{
			if (!ClassPaths.Contains(PrefabPath))
			{
				OutDifferences.Add(FString::Printf(TEXT("missing from the class: %s"), *PrefabPath));
			}
		}
		for (const FString& ClassPath : ClassPaths)
		{
			if (!PrefabPaths.Contains(ClassPath))
			{
				OutDifferences.Add(FString::Printf(TEXT("only in the class: %s"), *ClassPath));
			}
		}

		for (int32 Index = 0; Index < PrefabPaths.Num(); Index++)
		{
			const int32 ClassIndex = ClassPaths.IndexOfByKey(PrefabPaths[Index]);
			if (ClassIndex != INDEX_NONE)
			{
				CompareWidgetValues(PrefabWidgets[Index], ClassWidgets[ClassIndex], PrefabPaths[Index], OutDifferences);
			}
		}

		// Both sides are registered hierarchies in the caller's world; neither belongs to it.
		DestroyLoadedTree(PrefabTree);
		if (IsValid(Instance))
		{
			Instance->DestroyWidget();
		}

		return OutDifferences.IsEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
