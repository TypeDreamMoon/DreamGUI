// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUIBlueprintConverter.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetViewModelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DreamUIBlueprintConverter"

namespace DreamUIConverterLocal
{
	/** Widgets of a tree in one fixed order: the root, then depth first. Two trees duplicated from each other walk alike. */
	TArray<UDreamWidget*> WalkTree(UDreamWidget* InRoot)
	{
		TArray<UDreamWidget*> Widgets;
		if (IsValid(InRoot))
		{
			UDreamWidget::CollectChildrenWidgets(InRoot, Widgets, /*IncludeTarget*/true);
		}
		return Widgets;
	}

	/**
	 * The id each widget is written with: the compiler's name for it, so the class keeps every variable and every key
	 * that names it. Two widgets with one name (the compiler gave only the first a variable) get the second a suffix.
	 */
	TArray<FString> MakeNodeIds(const TArray<UDreamWidget*>& InWidgets)
	{
		TArray<FString> Ids;
		TSet<FString> Taken;
		for (const UDreamWidget* Widget : InWidgets)
		{
			FString Base = UDreamWidgetTree::MakeWidgetVariableName(Widget).ToString();
			if (Base.IsEmpty() || Base == TEXT("None"))
			{
				Base = TEXT("Widget");
			}
			FString Id = Base;
			for (int32 Bump = 1; Taken.Contains(Id.ToLower()); ++Bump)
			{
				Id = FString::Printf(TEXT("%s_%d"), *Base, Bump);
			}
			Taken.Add(Id.ToLower());
			Ids.Add(Id);
		}
		return Ids;
	}

	bool ParseText(const FString& InText, const FString& InSourceName, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		OutDiagnostics.SourceName = InSourceName;
		return FDreamUISourceFile::Parse(InText, InSourceName, OutAst, OutDiagnostics, FDreamUISourceFile::MakeFileImportReader());
	}

	FString FirstError(const FDreamUIDiagnosticBag& InDiagnostics)
	{
		for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
		{
			if (Diagnostic.IsError())
			{
				return Diagnostic.ToString();
			}
		}
		return FString();
	}

	/**
	 * Where a statement about InName on InOwner goes in InNode -- the node's own `+` block for a behaviour the node
	 * writes, else the node's line when that line reaches it (widget, visual, behaviours in the builder's order) --
	 * or false when no line of this file can name it.
	 */
	bool PlaceStatement(const FDreamUINode& InNode, UDreamWidget* InWidget, const UObject* InOwner, FName InName,
		FDreamUIStructuralEdit& OutEdit)
	{
		OutEdit.NodeId = InNode.Id;
		OutEdit.PropertyName = InName.ToString();
		OutEdit.PropertyTarget = EDreamUIPatchTarget::Node;
		OutEdit.ComponentIndex = INDEX_NONE;
		if (InOwner == InWidget)
		{
			return true;
		}
		if (Cast<UDreamUIBehaviour>(InOwner) != nullptr)
		{
			const int32 Line = FDreamUITextWriteBack::FindComponentLine(InNode, InWidget, InOwner);
			if (Line != INDEX_NONE)
			{
				OutEdit.PropertyTarget = EDreamUIPatchTarget::Component;
				OutEdit.ComponentIndex = Line;
				return true;
			}
		}
		TArray<const UObject*> Reached{ InWidget };
		if (const UDreamVisual* Visual = InWidget->GetVisual())
		{
			Reached.Add(Visual);
		}
		for (const UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			if (IsValid(Behaviour))
			{
				Reached.Add(Behaviour);
			}
		}
		const UObject* const* First = Reached.FindByPredicate([InName](const UObject* InCandidate)
		{
			return InCandidate->GetClass()->FindPropertyByName(InName) != nullptr;
		});
		return First != nullptr && *First == InOwner;
	}

	/** The object a binding or route names on a widget of the converted tree. */
	const UObject* ResolveOwner(UDreamWidget* InWidget, EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex)
	{
		switch (InTarget)
		{
		case EDreamWidgetBindingTarget::Widget:
			return InWidget;
		case EDreamWidgetBindingTarget::Visual:
			return InWidget->GetVisual();
		case EDreamWidgetBindingTarget::Behaviour:
			return InWidget->GetAllComponents().IsValidIndex(InBehaviourIndex) ? InWidget->GetAllComponents()[InBehaviourIndex] : nullptr;
		default:
			return nullptr;
		}
	}

	/** One statement into InOutText, re-read before each so its locations describe the text it lands in. */
	bool ApplyStatement(FString& InOutText, const FString& InSourceName, const FDreamUIStructuralEdit& InEdit, FString& OutWhy)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		if (!ParseText(InOutText, InSourceName, Ast, Diagnostics))
		{
			OutWhy = FirstError(Diagnostics);
			return false;
		}
		FString Updated = InOutText;
		if (!FDreamUITextPatcher::ApplyStructuralEdits(Updated, Ast, MakeArrayView(&InEdit, 1), Diagnostics))
		{
			OutWhy = FirstError(Diagnostics);
			return false;
		}
		InOutText = Updated;
		return true;
	}

	FString DescribeViewModelSource(const FDreamWidgetViewModelSlot& InSlot)
	{
		const FString Named = InSlot.SourceName.IsNone() ? FString() : FString::Printf(TEXT(" \"%s\""), *InSlot.SourceName.ToString());
		switch (InSlot.Source)
		{
		case EDreamViewModelSource::New:
			return TEXT(" = new");
		case EDreamViewModelSource::Global:
			return TEXT(" = global") + Named;
		case EDreamViewModelSource::Parent:
			return TEXT(" = parent") + Named;
		default:
			return FString();
		}
	}

	/**
	 * The Blueprint's tree, copied into a tree of its own and renamed to the ids it will be written with -- what the
	 * write-back is handed as the tree the designer just edited. The Blueprint itself is not touched.
	 */
	struct FWorkingCopy
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		TArray<UDreamWidget*> Originals;
		TArray<UDreamWidget*> Copies;
		TArray<FString> Ids;

		bool Make(UDreamWidgetBlueprint* InBlueprint)
		{
			UDreamWidget* SourceRoot = InBlueprint->WidgetTree->RootWidget;
			Tree.Reset(NewObject<UDreamWidgetTree>(GetTransientPackage()));
			Tree->RootWidget = UDreamWidget::DuplicateSubtree(Tree.Get(), SourceRoot);
			Tree->RebuildParentLinks();
			Originals = WalkTree(SourceRoot);
			Copies = WalkTree(Tree->RootWidget);
			if (Originals.Num() != Copies.Num())
			{
				return false;
			}
			Ids = MakeNodeIds(Originals);
			for (int32 Index = 0; Index < Copies.Num(); ++Index)
			{
				Copies[Index]->SetDisplayName(Ids[Index]);
			}
			return true;
		}

		UDreamWidget* FindCopyByVariableName(FName InVariableName) const
		{
			for (int32 Index = 0; Index < Originals.Num(); ++Index)
			{
				if (UDreamWidgetTree::MakeWidgetVariableName(Originals[Index]) == InVariableName)
				{
					return Copies[Index];
				}
			}
			return nullptr;
		}
	};

	/** The whole text for this Blueprint, and what it could not hold. False when there is no text to be had at all. */
	bool MakeText(UDreamWidgetBlueprint* InBlueprint, FWorkingCopy& InCopy, const FString& InSourceName, FString& OutText,
		TArray<FString>& OutResidue, TArray<FString>& OutErrors)
	{
		UDreamWidget* Root = InCopy.Tree->RootWidget;
		const FDreamUIAst NoAst;
		// The root's header: a new root is no insert, so the write-back would write nothing for it.
		FString Text = FString::Printf(TEXT("// %s\nclass %s\n\n%s %s {\n}\n"),
			*InSourceName, *InBlueprint->GetPathName(), *FDreamUITextWriteBack::DescribeWidgetForText(Root, NoAst), *Root->GetDisplayName());

		// Rounds until the text stops changing: the first writes the shape and the new nodes' values, the next the
		// root's own (a change of shape narrows the value pass to the nodes it wrote).
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = InSourceName;
		FDreamUIStructuralResidue Residue;
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			FString Updated;
			FDreamUIDiagnosticBag PassDiagnostics;
			PassDiagnostics.SourceName = InSourceName;
			Residue.Reset();
			if (!FDreamUITextWriteBack::ProduceText(Text, InCopy.Tree.Get(), Updated, PassDiagnostics, nullptr, nullptr, &Residue))
			{
				OutErrors.Add(FString::Printf(TEXT("the file being made stopped building: %s"), *FirstError(PassDiagnostics)));
				return false;
			}
			Diagnostics = MoveTemp(PassDiagnostics);
			if (Updated.Equals(Text, ESearchCase::CaseSensitive))
			{
				break;
			}
			Text = MoveTemp(Updated);
		}
		for (const FString& Id : Residue.Unwritten)
		{
			OutResidue.Add(FString::Printf(TEXT("%s: the node could not be written"), *Id));
		}
		for (const FString& Entry : Residue.Unsettled)
		{
			OutResidue.Add(FString::Printf(TEXT("%s: no line could hold it"), *Entry));
		}
		for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
		{
			if (Diagnostic.IsError())
			{
				OutResidue.AddUnique(Diagnostic.Message);
			}
		}

		// Bindings and routes, as the statements the language spells them with.
		auto WriteStatement = [&](FName InWidgetName, EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex, FName InName,
			const FString& InStatement, const FString& InWhat)
		{
			UDreamWidget* Widget = InCopy.FindCopyByVariableName(InWidgetName);
			const UObject* Owner = Widget != nullptr ? ResolveOwner(Widget, InTarget, InBehaviourIndex) : nullptr;
			FDreamUIAst Ast;
			FDreamUIDiagnosticBag ParseDiagnostics;
			const FDreamUINode* Node = nullptr;
			if (Owner != nullptr && ParseText(Text, InSourceName, Ast, ParseDiagnostics))
			{
				Ast.ForEachNode([&Node, Widget](const FDreamUINode& InNode)
				{
					if (Node == nullptr && InNode.Id.Equals(Widget->GetDisplayName(), ESearchCase::IgnoreCase))
					{
						Node = &InNode;
					}
				});
			}
			FDreamUIStructuralEdit Edit;
			FString Why;
			if (Node == nullptr || !PlaceStatement(*Node, Widget, Owner, InName, Edit))
			{
				OutResidue.Add(FString::Printf(TEXT("%s: no line of the file reaches it"), *InWhat));
				return;
			}
			Edit.Kind = EDreamUIStructuralEditKind::SetStatement;
			Edit.Statement = InStatement;
			if (!ApplyStatement(Text, InSourceName, Edit, Why))
			{
				OutResidue.Add(FString::Printf(TEXT("%s: %s"), *InWhat, *Why));
			}
		};
		for (const FDreamWidgetPropertyBinding& Binding : InBlueprint->PropertyBindings)
		{
			WriteStatement(Binding.WidgetName, Binding.Target, Binding.BehaviourIndex, Binding.PropertyName,
				FString::Printf(TEXT("<- %s()"), *Binding.FunctionName.ToString()),
				FString::Printf(TEXT("%s.%s <- %s()"), *Binding.WidgetName.ToString(), *Binding.PropertyName.ToString(), *Binding.FunctionName.ToString()));
		}
		for (const FDreamWidgetEventBinding& Route : InBlueprint->EventBindings)
		{
			WriteStatement(Route.WidgetName, Route.Target, Route.BehaviourIndex, Route.EventName,
				FString::Printf(TEXT("-> %s"), *Route.FunctionName.ToString()),
				FString::Printf(TEXT("%s.%s -> %s"), *Route.WidgetName.ToString(), *Route.EventName.ToString(), *Route.FunctionName.ToString()));
		}
		for (const FDreamWidgetEachBinding& Each : InBlueprint->EachBindings)
		{
			(void)Each;
			OutResidue.AddUnique(TEXT("an 'each' binding authored on the asset: write it as an 'each' block by hand"));
		}

		// View models, and the animations the asset keeps: the file lists every one, Sequencer keeps editing them.
		if (InBlueprint->ViewModelSlots.Num() > 0)
		{
			Text += TEXT("\nviewmodels {\n");
			for (const FDreamWidgetViewModelSlot& Slot : InBlueprint->ViewModelSlots)
			{
				if (Slot.Class == nullptr)
				{
					OutResidue.Add(FString::Printf(TEXT("view model %s: its class is missing"), *Slot.VariableName.ToString()));
					continue;
				}
				Text += FString::Printf(TEXT("    %s %s%s\n"), *Slot.Class->GetPathName(), *Slot.VariableName.ToString(), *DescribeViewModelSource(Slot));
			}
			Text += TEXT("}\n");
		}
		TArray<FString> Timelines;
		for (UDreamWidget* Widget : InCopy.Copies)
		{
			const UDreamWidgetAnimationComponent* Animator = Widget->GetComponent<UDreamWidgetAnimationComponent>();
			if (Animator == nullptr)
			{
				continue;
			}
			for (const UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
			{
				if (IsValid(Animation) && !Animation->IsLanguageOwned() && !Animation->GetDisplayNameString().IsEmpty())
				{
					Timelines.AddUnique(Animation->GetDisplayNameString());
				}
			}
		}
		if (Timelines.Num() > 0)
		{
			Text += TEXT("\n");
			for (const FString& Name : Timelines)
			{
				Text += FString::Printf(TEXT("timeline %s external\n"), *Name);
			}
		}
		OutText = MoveTemp(Text);
		return true;
	}

	/** Where the tree built from InText differs from InLive: the shape, the values a line could say, the ones none can. */
	void Compare(const FString& InText, const FString& InSourceName, const UDreamWidgetTree* InLive, const UDreamWidgetTree* InBuilt,
		TArray<FString>& OutDifferences)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		if (!ParseText(InText, InSourceName, Ast, Diagnostics))
		{
			OutDifferences.Add(FString::Printf(TEXT("the file does not parse: %s"), *FirstError(Diagnostics)));
			return;
		}
		TArray<FDreamUIStructuralEdit> Shape;
		FDreamUITextWriteBack::CollectStructuralEdits(Ast, InLive, InBuilt, Shape);
		for (const FDreamUIStructuralEdit& Edit : Shape)
		{
			OutDifferences.Add(FString::Printf(TEXT("%s: the shape differs (%s)"),
				*(Edit.NodeId.IsEmpty() ? Edit.NewId : Edit.NodeId), *FDreamUITextWriteBack::MakeEditSignature(Edit)));
		}
		TArray<FDreamUIPropertyEdit> Values;
		TArray<FString> Unwritable;
		FDreamUITextWriteBack::CollectUnwritableDifferences(Ast, InLive, InBuilt, Values, Unwritable, Diagnostics);
		for (const FDreamUIPropertyEdit& Edit : Values)
		{
			OutDifferences.AddUnique(FString::Printf(TEXT("%s.%s: the file says something else (it should be %s)"),
				*Edit.NodeId, *Edit.PropertyName, *Edit.NewValueText));
		}
		for (const FString& Entry : Unwritable)
		{
			OutDifferences.AddUnique(FString::Printf(TEXT("%s: the value has no spelling in a .dui"), *Entry));
		}
	}

	bool CheckBlueprint(const UDreamWidgetBlueprint* InBlueprint, TArray<FString>& OutErrors)
	{
		if (!IsValid(InBlueprint) || !IsValid(InBlueprint->WidgetTree) || !IsValid(InBlueprint->WidgetTree->RootWidget))
		{
			OutErrors.Add(TEXT("the Blueprint has no hierarchy to convert"));
			return false;
		}
		if (DreamUITextAuthoring::IsTextAuthored(InBlueprint))
		{
			OutErrors.Add(FString::Printf(TEXT("it is authored in '%s' already"), *DreamUITextAuthoring::GetAuthoredSourceFileName(InBlueprint)));
			return false;
		}
		const UClass* Parent = InBlueprint->ParentClass;
		if (Parent == nullptr)
		{
			OutErrors.Add(TEXT("it has no parent class"));
			return false;
		}
		if (Parent != UDreamUserWidget::StaticClass() && !Parent->IsChildOf(UDreamTextUserWidget::StaticClass()))
		{
			OutErrors.Add(FString::Printf(TEXT("its parent class %s is neither UDreamUserWidget nor a UDreamTextUserWidget; make %s derive from UDreamTextUserWidget, then convert"),
				*Parent->GetName(), *Parent->GetName()));
			return false;
		}
		return true;
	}

	/** The backup: a copy of the asset beside it, `<Name>_PreDui`, made before anything is changed. */
	UObject* MakeBackup(UDreamWidgetBlueprint* InBlueprint, FString& OutError)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		const FString PackagePath = FPackageName::GetLongPackagePath(InBlueprint->GetOutermost()->GetName());
		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(PackagePath / (InBlueprint->GetName() + TEXT("_PreDui")), FString(), PackageName, AssetName);
		// By hand rather than through IAssetTools::DuplicateAsset, which declines some mount points (/Temp among them) without
		// a word: the Blueprint duplicated into a package of its own, its class regenerated by its own PostDuplicate.
		UPackage* Package = CreatePackage(*PackageName);
		UObject* Backup = Package != nullptr ? DuplicateObject<UDreamWidgetBlueprint>(InBlueprint, Package, FName(*AssetName)) : nullptr;
		if (Backup == nullptr)
		{
			OutError = FString::Printf(TEXT("the backup '%s' could not be made"), *AssetName);
			return nullptr;
		}
		Backup->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		Backup->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Backup);
		return Backup;
	}

	/** The source watcher sees saves to a file only under a root it watches; a DUI folder the conversion made is new. */
	void WatchTheRootHolding(const FString& InFilePath)
	{
		FString BestDirectory;
		for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
		{
			if (FPaths::IsUnderDirectory(InFilePath, Root.Directory) && Root.Directory.Len() > BestDirectory.Len())
			{
				BestDirectory = Root.Directory;
			}
		}
		if (!BestDirectory.IsEmpty())
		{
			FDreamUISourceWatcher::EnsureWatching(BestDirectory);
		}
	}
}

FString FDreamUIConversionReport::ToString() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%s -> %s%s"), *BlueprintPath, *TargetFile, bConverted ? TEXT(" (converted)") : TEXT("")));
	for (const FString& Error : Errors)
	{
		Lines.Add(TEXT("  cannot convert: ") + Error);
	}
	for (const FString& Entry : Residue)
	{
		Lines.Add(TEXT("  not in the file: ") + Entry);
	}
	for (const FString& Entry : Mismatches)
	{
		Lines.Add(TEXT("  differs after compiling: ") + Entry);
	}
	if (!BackupPath.IsEmpty())
	{
		Lines.Add(TEXT("  backup: ") + BackupPath);
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString FDreamUIBlueprintConverter::GetDefaultTargetFile(const UDreamWidgetBlueprint* InBlueprint)
{
	if (!IsValid(InBlueprint))
	{
		return FString();
	}
	// The path under the mount point, the mount point itself dropped: /Game/UI/WBP_Login -> UI/WBP_Login.
	FString Relative = InBlueprint->GetOutermost()->GetName();
	Relative.RemoveFromStart(TEXT("/"));
	int32 Slash = INDEX_NONE;
	if (Relative.FindChar(TEXT('/'), Slash))
	{
		Relative.RightChopInline(Slash + 1);
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName,
		Relative + DreamUIPaths::SourceExtension));
}

bool FDreamUIBlueprintConverter::IsConvertible(const UDreamWidgetBlueprint* InBlueprint)
{
	TArray<FString> Errors;
	return DreamUIConverterLocal::CheckBlueprint(InBlueprint, Errors);
}

bool FDreamUIBlueprintConverter::Preflight(UDreamWidgetBlueprint* InBlueprint, const FString& InTargetFile, FDreamUIConversionReport& OutReport)
{
	using namespace DreamUIConverterLocal;
	OutReport = FDreamUIConversionReport();
	OutReport.BlueprintPath = GetPathNameSafe(InBlueprint);
	OutReport.TargetFile = InTargetFile.IsEmpty() ? GetDefaultTargetFile(InBlueprint) : FPaths::ConvertRelativePathToFull(InTargetFile);
	if (!CheckBlueprint(InBlueprint, OutReport.Errors))
	{
		return false;
	}
	if (FPaths::FileExists(OutReport.TargetFile))
	{
		OutReport.Errors.Add(FString::Printf(TEXT("'%s' exists already, and a conversion never writes over a file"), *OutReport.TargetFile));
		return false;
	}

	FWorkingCopy Copy;
	if (!Copy.Make(InBlueprint))
	{
		OutReport.Errors.Add(TEXT("the hierarchy could not be copied to be converted"));
		return false;
	}
	const FString SourceName = FPaths::GetCleanFilename(OutReport.TargetFile);
	if (!MakeText(InBlueprint, Copy, SourceName, OutReport.Text, OutReport.Residue, OutReport.Errors))
	{
		return false;
	}

	// What the file builds, against what it came from: anything else it does not hold is residue too.
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	Diagnostics.SourceName = SourceName;
	TStrongObjectPtr<UDreamWidgetTree> Built(FDreamUITextWriteBack::BuildReferenceTree(OutReport.Text, Ast, Diagnostics));
	if (!Built.IsValid())
	{
		OutReport.Errors.Add(FString::Printf(TEXT("the file made for it does not build: %s"), *FirstError(Diagnostics)));
		return false;
	}
	TArray<FString> Differences;
	Compare(OutReport.Text, SourceName, Copy.Tree.Get(), Built.Get(), Differences);
	for (const FString& Difference : Differences)
	{
		OutReport.Residue.AddUnique(Difference);
	}
	return true;
}

bool FDreamUIBlueprintConverter::Convert(UDreamWidgetBlueprint* InBlueprint, const FString& InTargetFile, bool bInAllowLoss,
	FDreamUIConversionReport& OutReport)
{
	using namespace DreamUIConverterLocal;
	if (!Preflight(InBlueprint, InTargetFile, OutReport))
	{
		return false;
	}
	if (!OutReport.Residue.IsEmpty() && !bInAllowLoss)
	{
		OutReport.Errors.Add(FString::Printf(TEXT("%d thing(s) would not reach the file; convert with the loss allowed to go ahead"), OutReport.Residue.Num()));
		return false;
	}

	// The way back, before anything is changed.
	FString Error;
	if (UObject* Backup = MakeBackup(InBlueprint, Error))
	{
		OutReport.BackupPath = Backup->GetPathName();
	}
	else
	{
		OutReport.Errors.Add(Error);
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutReport.TargetFile), /*Tree*/true);
	if (!FFileHelper::SaveStringToFile(OutReport.Text, *OutReport.TargetFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutReport.Errors.Add(FString::Printf(TEXT("'%s' could not be written"), *OutReport.TargetFile));
		return false;
	}
	WatchTheRootHolding(OutReport.TargetFile);

	// The copy the file was made from, kept for the comparison after the compile.
	FWorkingCopy Copy;
	Copy.Make(InBlueprint);

	// The widgets renamed to the ids the file writes them with: the compile carries their animations across by name.
	InBlueprint->Modify();
	for (int32 Index = 0; Index < Copy.Originals.Num(); ++Index)
	{
		if (Copy.Originals[Index]->GetDisplayName() != Copy.Ids[Index])
		{
			Copy.Originals[Index]->Modify();
			Copy.Originals[Index]->SetDisplayName(Copy.Ids[Index]);
		}
	}

	if (InBlueprint->ParentClass == UDreamUserWidget::StaticClass())
	{
		// Its subclass, so the graph, the variables and the functions are untouched by the change.
		InBlueprint->ParentClass = UDreamTextUserWidget::StaticClass();
		FBlueprintEditorUtils::RefreshAllNodes(InBlueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}
	if (!DreamUITextAuthoring::SetAuthoredSourcePath(InBlueprint, OutReport.TargetFile))
	{
		OutReport.Errors.Add(TEXT("the class could not be pointed at the file; set it with Set Source File..."));
		return false;
	}
	OutReport.bConverted = true;
	InBlueprint->MarkPackageDirty();

	// The tree the compile rebuilt from the file, against the one it was converted from.
	if (IsValid(InBlueprint->WidgetTree))
	{
		Compare(OutReport.Text, FPaths::GetCleanFilename(OutReport.TargetFile), Copy.Tree.Get(), InBlueprint->WidgetTree, OutReport.Mismatches);
	}
	return true;
}

void FDreamUIBlueprintConverter::ConvertWithConfirmation(const TArray<UDreamWidgetBlueprint*>& InBlueprints)
{
	TArray<FString> Summary;
	int32 Converted = 0;
	for (UDreamWidgetBlueprint* Blueprint : InBlueprints)
	{
		FDreamUIConversionReport Report;
		Preflight(Blueprint, FString(), Report);
		bool bAllowLoss = false;
		if (Report.CanConvert() && !Report.Residue.IsEmpty())
		{
			// The author decides, per Blueprint, with the list in front of them.
			const FText Question = FText::Format(LOCTEXT("ConfirmLoss",
				"Converting '{0}' to {1} will not keep the following:\n\n{2}\n\nConvert anyway? A backup of the asset is made first."),
				FText::FromString(GetNameSafe(Blueprint)), FText::FromString(FPaths::GetCleanFilename(Report.TargetFile)),
				FText::FromString(FString::Join(Report.Residue, TEXT("\n"))));
			if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
			{
				Summary.Add(FString::Printf(TEXT("%s: skipped"), *GetNameSafe(Blueprint)));
				continue;
			}
			bAllowLoss = true;
		}
		if (Report.CanConvert())
		{
			Convert(Blueprint, FString(), bAllowLoss, Report);
		}
		Converted += Report.bConverted ? 1 : 0;
		Summary.Add(Report.ToString());
		UE_LOG(DreamGUIEditor, Display, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Report.ToString());
	}
	if (Summary.Num() == 0)
	{
		return;
	}
	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("ConversionSummary", "Converted {0} of {1}.\n\n{2}"),
		FText::AsNumber(Converted), FText::AsNumber(InBlueprints.Num()), FText::FromString(FString::Join(Summary, TEXT("\n\n")))));
}

#undef LOCTEXT_NAMESPACE
