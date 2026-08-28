// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetBlueprintCompiler.h"
#include "DreamWidgetBlueprint.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"

#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "KismetCompilerMisc.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "DreamWidgetBlueprintCompiler"

FDreamWidgetBlueprintCompilerContext::FDreamWidgetBlueprintCompilerContext(
	UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
	: Super(InBlueprint, InMessageLog, InCompileOptions)
{
}

FDreamWidgetBlueprintCompilerContext::~FDreamWidgetBlueprintCompilerContext() = default;

UDreamWidgetBlueprint* FDreamWidgetBlueprintCompilerContext::DreamWidgetBlueprint() const
{
	return Cast<UDreamWidgetBlueprint>(Blueprint);
}

FName FDreamWidgetBlueprintCompilerContext::MakeWidgetVariableName(const UDreamWidget* InWidget)
{
	// Deliberately a one-line delegation. The runtime resolves bindings with this exact function, and
	// the two agreeing is the entire contract between compile time and run time.
	return UDreamWidgetTree::MakeWidgetVariableName(InWidget);
}

void FDreamWidgetBlueprintCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	NewDreamWidgetClass = FindObject<UDreamWidgetGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);
	if (NewDreamWidgetClass == nullptr)
	{
		NewDreamWidgetClass = NewObject<UDreamWidgetGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
	}
	else
	{
		// It existed but was not linked into the Blueprint yet, which load ordering can produce.
		FBlueprintCompileReinstancer::Create(NewDreamWidgetClass);
	}
	NewClass = NewDreamWidgetClass;
}

void FDreamWidgetBlueprintCompilerContext::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	NewDreamWidgetClass = CastChecked<UDreamWidgetGeneratedClass>(ClassToUse);
}

void FDreamWidgetBlueprintCompilerContext::EnsureProperGeneratedClass(UClass*& InOutTargetClass)
{
	if (InOutTargetClass != nullptr && !((UObject*)InOutTargetClass)->IsA(UDreamWidgetGeneratedClass::StaticClass()))
	{
		// An asset reparented into this Blueprint type carries a class of the wrong kind; it cannot
		// hold a widget-tree archetype, so it is discarded rather than compiled into.
		FKismetCompilerUtilities::ConsignToOblivion(InOutTargetClass, Blueprint->bIsRegeneratingOnLoad);
		InOutTargetClass = nullptr;
	}
}

void FDreamWidgetBlueprintCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOutOldCDO)
{
	Super::CleanAndSanitizeClass(ClassToClean, InOutOldCDO);

	// The previous archetype is deliberately not destroyed here: FinishCompilingClass patches the new
	// one over it so any loader export still pointing at the old object resolves to the replacement.
	if (UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(ClassToClean))
	{
		DreamClass->SetWidgetTreeArchetype(nullptr);
	}
}

void FDreamWidgetBlueprintCompilerContext::SaveSubObjectsFromCleanAndSanitizeClass(FSubobjectCollection& SubObjectsToSave, UBlueprintGeneratedClass* ClassToClean)
{
	Super::SaveSubObjectsFromCleanAndSanitizeClass(SubObjectsToSave, ClassToClean);

	check(ClassToClean == NewClass);
	NewDreamWidgetClass = CastChecked<UDreamWidgetGeneratedClass>((UObject*)NewClass);
	OldWidgetTree = NewDreamWidgetClass->GetWidgetTreeArchetype();

	// The Blueprint's authoring tree has to survive the sub-object blitz. It is not the class's copy --
	// it is what the designer edits -- and letting it get renamed out from under the asset produces
	// load errors on the next open rather than an immediately visible failure.
	if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
	{
		if (IsValid(DreamBlueprint->WidgetTree))
		{
			SubObjectsToSave.AddObject(DreamBlueprint->WidgetTree);
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::PopulateBlueprintGeneratedVariables()
{
	Super::PopulateBlueprintGeneratedVariables();

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint != nullptr)
	{
		TArray<UDreamWidget*> SourceWidgets;
		DreamBlueprint->GetAllSourceWidgets(SourceWidgets);

		// One member variable per authored widget, named by the shared rule. Declaring them here is
		// what makes a widget reachable from the graph AND what the runtime binds against -- the same
		// names, from the same function, which is the point.
		TSet<FName> DeclaredNames;
		for (const UDreamWidget* Widget : SourceWidgets)
		{
			const FName VariableName = MakeWidgetVariableName(Widget);
			if (VariableName.IsNone())
			{
				continue;
			}
			// Two widgets sharing a display name would silently collapse into one variable, and which
			// widget it ends up bound to would depend on tree order. Name it instead of picking.
			if (DeclaredNames.Contains(VariableName))
			{
				MessageLog.Warning(*FText::Format(
					LOCTEXT("DuplicateWidgetVariableName", "More than one widget is named \"{0}\"; only the first is exposed as a variable. Rename one of them."),
					FText::FromName(VariableName)).ToString());
				continue;
			}
			DeclaredNames.Add(VariableName);

			// A parent class that already declares this binding wins: a subclass re-declaring it would
			// shadow the parent's property and leave the parent's own code bound to nothing.
			if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(VariableName) != nullptr)
			{
				continue;
			}

			UClass* WidgetClass = Widget->GetClass();
			if (UBlueprintGeneratedClass* WidgetBlueprintClass = Cast<UBlueprintGeneratedClass>(WidgetClass))
			{
				// Recompiling a dependent asset otherwise captures a stale REINST class here.
				WidgetClass = WidgetBlueprintClass->GetAuthoritativeClass();
			}

			FBPVariableDescription WidgetVariable;
			WidgetVariable.VarName = VariableName;
			// Derived from the name rather than stored: it stays stable across recompiles and across
			// machines with nothing to keep in sync. Renaming a widget changes it, but a name-keyed
			// map (which is what UMG stores) has exactly that property too, so nothing is given up.
			WidgetVariable.VarGuid = FGuid::NewDeterministicGuid(VariableName.ToString());
			WidgetVariable.VarType = FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, WidgetClass, EPinContainerType::None, false, FEdGraphTerminalType());
			WidgetVariable.FriendlyName = Widget->GetDisplayName();
			WidgetVariable.PropertyFlags = (CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_RepSkip | CPF_Transient | CPF_DuplicateTransient);
			WidgetVariable.SetMetaData(TEXT("Category"), *DreamBlueprint->GetName());

			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(WidgetVariable));
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::UpdateGeneratedClassWidgetTree(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetGeneratedClass* InClass)
{
	if (!IsValid(InBlueprint->WidgetTree))
	{
		return;
	}

	// A duplicate, never the authoring object itself. The class's archetype is instanced from on every
	// CreateDreamWidget; handing it the object the designer is editing would let an edit mutate the
	// template every live instance was built from, mid-session.
	const EObjectFlags PreviousFlags = InBlueprint->WidgetTree->GetFlags();
	InBlueprint->WidgetTree->ClearFlags(RF_ArchetypeObject);

	FObjectDuplicationParameters DupParams(InBlueprint->WidgetTree, InClass);
	DupParams.DestName = InBlueprint->WidgetTree->GetFName();
	DupParams.FlagMask = RF_AllFlags & ~RF_DefaultSubObject;
	DupParams.PortFlags |= PPF_DuplicateVerbatim;

	UDreamWidgetTree* NewWidgetTree = Cast<UDreamWidgetTree>(StaticDuplicateObjectEx(DupParams));
	InBlueprint->WidgetTree->SetFlags(PreviousFlags);

	if (NewWidgetTree != nullptr)
	{
		// Parent is DuplicateTransient, so the copy arrives structurally complete with empty
		// back-pointers. The archetype is walked by name during binding, so they have to be there.
		NewWidgetTree->RebuildParentLinks();
	}
	InClass->SetWidgetTreeArchetype(NewWidgetTree);

	if (OldWidgetTree != nullptr && NewWidgetTree != nullptr)
	{
		// An export still pointing at the previous archetype must resolve to the replacement, or a
		// dependent asset loaded mid-recompile keeps a tree that no class owns.
		FLinkerLoad::PRIVATE_PatchNewObjectIntoExport(OldWidgetTree, NewWidgetTree);
	}
	OldWidgetTree = nullptr;
}

void FDreamWidgetBlueprintCompilerContext::ValidateWidgetBindings(UClass* InClass)
{
	UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(InClass);
	UDreamWidgetTree* Archetype = DreamClass != nullptr ? UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(DreamClass) : nullptr;
	if (Archetype == nullptr)
	{
		// No hierarchy at all is a legitimate state (logic-only class, or nothing authored yet).
		// Reporting every native binding as broken here would bury the real errors.
		return;
	}

	// The reason the class model is worth the trouble. A native subclass declaring a widget binding
	// that no widget answers used to fail at RUN time, as a null, after a save had already dropped it.
	// Here it is an error at compile time, on the asset, with the name in the message.
	//
	// Only properties that SAY they are bindings, via meta=(BindWidget). Raising an error means
	// asserting intent, and intent cannot be inferred from the shape of a property: the first attempt
	// here treated "transient and widget-typed" as the marker and promptly flagged
	// UDreamWidget::Parent, which is both of those and is not a binding. A widget-typed member with no
	// marker is somebody's own reference and none of this pass's business.
	for (TFieldIterator<FObjectPropertyBase> It(InClass, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		FObjectPropertyBase* Property = *It;
		if (Property->PropertyClass == nullptr || !Property->PropertyClass->IsChildOf(UDreamWidget::StaticClass()))
		{
			continue;
		}
		if (!Property->HasMetaData(TEXT("BindWidget")))
		{
			continue;
		}

		const UDreamWidget* Match = Archetype->FindWidgetByVariableName(Property->GetFName());
		if (Match == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingNotFound", "The binding \"{0}\" expects a widget of that name, and this hierarchy has none."),
				FText::FromName(Property->GetFName())).ToString());
		}
		else if (!Match->IsA(Property->PropertyClass))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingWrongType", "The binding \"{0}\" expects {1}, but the widget of that name is {2}."),
				FText::FromName(Property->GetFName()),
				FText::FromString(Property->PropertyClass->GetName()),
				FText::FromString(Match->GetClass()->GetName())).ToString());
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::FinishCompilingClass(UClass* Class)
{
	if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
	{
		if (UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(Class))
		{
			UpdateGeneratedClassWidgetTree(DreamBlueprint, DreamClass);
		}
	}

	Super::FinishCompilingClass(Class);

	// After the base pass, so the properties being checked against actually exist on the class.
	ValidateWidgetBindings(Class);
}

#undef LOCTEXT_NAMESPACE
