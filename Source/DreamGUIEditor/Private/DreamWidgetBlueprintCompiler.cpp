// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetBlueprintCompiler.h"
#include "DreamWidgetBlueprint.h"

#include "Animation/DreamUISequence.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectIterator.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
// A `->` route may name an FDreamUIEventDelegate as well as a multicast delegate; see the event half
// of ValidateWidgetBindings.
#include "Event/DreamUIEventDelegate.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUIValueFormat.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "DreamGUIEditorSubsystem.h"
#include "Text/DreamUIExpressionThunks.h"
#include "Text/DreamUISourceWatcher.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/TimelineTemplate.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "MovieScene.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "KismetCompilerMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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

namespace DreamWidgetTextMembersLocal
{
	/**
	 * Whether a prop of type InDeclared can be the parent's property of type InExisting -- the C++ base that already
	 * declares what the file names, which the file then uses instead of declaring a second one.
	 *
	 * As loose as a host writing the value and a binding reading it can afford and no looser: a Number is any real
	 * (a native float is what most of them are, and the binding converts), an Asset any object reference and a Class
	 * any class reference, soft ones included (the file's type names no class to be stricter with). Everything else
	 * is the same pin or nothing.
	 */
	bool ArePropTypesCompatible(const FEdGraphPinType& InDeclared, const FEdGraphPinType& InExisting)
	{
		if (InDeclared.ContainerType != InExisting.ContainerType)
		{
			return false;
		}
		const FName Category = InDeclared.PinCategory;
		if (Category == UEdGraphSchema_K2::PC_Object)
		{
			return InExisting.PinCategory == UEdGraphSchema_K2::PC_Object || InExisting.PinCategory == UEdGraphSchema_K2::PC_SoftObject;
		}
		if (Category == UEdGraphSchema_K2::PC_Class)
		{
			return InExisting.PinCategory == UEdGraphSchema_K2::PC_Class || InExisting.PinCategory == UEdGraphSchema_K2::PC_SoftClass;
		}
		if (Category != InExisting.PinCategory)
		{
			return false;
		}
		if (Category == UEdGraphSchema_K2::PC_Struct || Category == UEdGraphSchema_K2::PC_Byte)
		{
			return InDeclared.PinSubCategoryObject == InExisting.PinSubCategoryObject;
		}
		return true;
	}

	/**
	 * Whether the parent's dispatcher InExisting is the `events` entry the file declares: the same parameters, in
	 * order, under the rule ArePropTypesCompatible keeps for a single value -- except that an object parameter must be
	 * of the very class, since a host's handler is checked against the signature as it is, not as the file says it.
	 */
	bool IsCompatibleDispatcher(const FMulticastDelegateProperty* InExisting, const TArray<FBPVariableDescription>& InParameters)
	{
		const UFunction* Signature = InExisting != nullptr ? InExisting->SignatureFunction.Get() : nullptr;
		if (Signature == nullptr)
		{
			return false;
		}
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		int32 Index = 0;
		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
		{
			FEdGraphPinType ExistingType;
			if (It->HasAnyPropertyFlags(CPF_ReturnParm) || !InParameters.IsValidIndex(Index)
				|| !K2Schema->ConvertPropertyToPinType(*It, ExistingType))
			{
				return false;
			}
			const FEdGraphPinType& Declared = InParameters[Index].VarType;
			const bool bObject = Declared.PinCategory == UEdGraphSchema_K2::PC_Object || Declared.PinCategory == UEdGraphSchema_K2::PC_Class;
			if (bObject ? (ExistingType.PinCategory != Declared.PinCategory || ExistingType.PinSubCategoryObject != Declared.PinSubCategoryObject)
				: !ArePropTypesCompatible(Declared, ExistingType))
			{
				return false;
			}
		}
		return Index == InParameters.Num();
	}

	/**
	 * Whether a graph can be made under InName inside the Blueprint.
	 *
	 * Asked before a dispatcher's signature graph is, because FBlueprintEditorUtils::CreateNewGraph does not refuse a
	 * taken name: it renames whatever graph holds it out of the way -- the author's own, silently -- and a non-graph
	 * object of that name (the authoring tree, a timeline template) is a rename onto an existing object, which stops
	 * the editor. A graph the Blueprint no longer holds anywhere is free: that is a previous compile's signature
	 * graph on its way to the collector, and moving it aside is all CreateNewGraph will do to it.
	 */
	bool IsGraphNameFree(UBlueprint* InBlueprint, const FName InName)
	{
		UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), InBlueprint, InName);
		if (Existing == nullptr)
		{
			return true;
		}
		UEdGraph* ExistingGraph = Cast<UEdGraph>(Existing);
		if (ExistingGraph == nullptr)
		{
			return false;
		}
		TArray<UEdGraph*> LiveGraphs;
		InBlueprint->GetAllGraphs(LiveGraphs);
		return !LiveGraphs.Contains(ExistingGraph);
	}

	/**
	 * A `props` default as the text a Blueprint variable's default is: what ImportText of that property reads back.
	 *
	 * Checked against the declared type here rather than left to the import, which on a mismatch only warns -- and
	 * warns about a property, in the class defaults, with nothing pointing back at the line that wrote the value.
	 */
	bool FormatPropDefault(const FDreamUIPropDecl& InProp, const FEdGraphPinType& InPinType, const FDreamUIAst& InAst,
		const FString& InLocalizationNamespace, FString& OutDefault, FString& OutReason)
	{
		OutDefault.Reset();
		FDreamUIValue Value = InProp.DefaultValue.GetValue();
		if (Value.Kind == EDreamUIValueKind::ResourceRef)
		{
			// `Color Tint = @Accent` -- the entry's own literal, as a `<-` expression takes it.
			const FDreamUIResource* Entry = InAst.FindResource(Value.Raw);
			if (Entry == nullptr)
			{
				OutReason = FString::Printf(TEXT("'@%s' names no entry in a resources block"), *Value.Raw);
				return false;
			}
			Value = Entry->Value;
		}

		const FName Category = InPinType.PinCategory;
		const UObject* SubObject = InPinType.PinSubCategoryObject.Get();
		if (Category == UEdGraphSchema_K2::PC_Text)
		{
			if (Value.Kind != EDreamUIValueKind::String)
			{
				OutReason = TEXT("a Text default is a quoted string");
				return false;
			}
			// Localizable, under the namespace the builder keys the file's other strings by and a key of the prop's
			// own (or the one `@key(...)` pins): a component's default label is text a translator has to see.
			const FString Key = Value.LocalizationKeyOverride.IsEmpty()
				? FString::Printf(TEXT("props.%s"), *InProp.Name) : Value.LocalizationKeyOverride;
			FTextStringHelper::WriteToBuffer(OutDefault, FText::AsLocalizable_Advanced(InLocalizationNamespace, Key, FString(Value.Raw)));
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_String)
		{
			if (Value.Kind != EDreamUIValueKind::String)
			{
				OutReason = TEXT("a String default is a quoted string");
				return false;
			}
			OutDefault = Value.Raw;
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Real || Category == UEdGraphSchema_K2::PC_Int)
		{
			double Number = 0.0;
			if (Value.Kind != EDreamUIValueKind::Number || !LexTryParseString(Number, *Value.Raw))
			{
				OutReason = FString::Printf(TEXT("'%s' is not a number"), *Value.Raw);
				return false;
			}
			if (Category == UEdGraphSchema_K2::PC_Real)
			{
				// The lexer's number text is ImportText's number text; no reformatting to drift on.
				OutDefault = Value.Raw;
				return true;
			}
			if (Number != FMath::RoundToDouble(Number) || Number < static_cast<double>(MIN_int32) || Number > static_cast<double>(MAX_int32))
			{
				OutReason = FString::Printf(TEXT("'%s' is not a whole number an Integer can hold"), *Value.Raw);
				return false;
			}
			OutDefault = FString::FromInt(static_cast<int32>(Number));
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Boolean)
		{
			if (Value.Kind != EDreamUIValueKind::Identifier
				|| !(Value.Raw.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Raw.Equals(TEXT("false"), ESearchCase::IgnoreCase)))
			{
				OutReason = TEXT("a Bool default is true or false");
				return false;
			}
			OutDefault = Value.Raw.Equals(TEXT("true"), ESearchCase::IgnoreCase) ? TEXT("true") : TEXT("false");
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Struct && SubObject == TBaseStructure<FLinearColor>::Get())
		{
			FLinearColor Color = FLinearColor::White;
			if (Value.Kind != EDreamUIValueKind::HexColor || !DreamUIValueFormat::ParseColorHex(Value.Raw, Color))
			{
				OutReason = TEXT("a Color default is a hex colour, as in #FF6600");
				return false;
			}
			TBaseStructure<FLinearColor>::Get()->ExportText(OutDefault, &Color, nullptr, nullptr, PPF_None, nullptr);
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Struct && SubObject == TBaseStructure<FVector2D>::Get())
		{
			FVector2D Vector = FVector2D::ZeroVector;
			if (Value.Kind != EDreamUIValueKind::Tuple || Value.Elements.Num() != 2
				|| !LexTryParseString(Vector.X, *Value.Elements[0]) || !LexTryParseString(Vector.Y, *Value.Elements[1]))
			{
				OutReason = TEXT("a Vector2 default is a pair, as in (4, 8)");
				return false;
			}
			TBaseStructure<FVector2D>::Get()->ExportText(OutDefault, &Vector, nullptr, nullptr, PPF_None, nullptr);
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class)
		{
			if (Value.Kind != EDreamUIValueKind::AssetPath && Value.Kind != EDreamUIValueKind::String)
			{
				OutReason = Category == UEdGraphSchema_K2::PC_Object
					? TEXT("an Asset default is an object path, as in /Game/UI/T_Icon")
					: TEXT("a Class default is a class path, as in /Script/Engine.Actor or /Game/UI/WBP_Row");
				return false;
			}
			// `/Game/UI/T_Icon` is how a path is written in a .dui and every other place an author types one; the
			// object it means is the package's asset of the same name.
			FString ObjectPath = Value.Raw.TrimStartAndEnd();
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath += TEXT(".") + FPackageName::GetShortName(ObjectPath);
			}
			// Loaded now, which is what the builder does with any asset a line names: the compiler copies an object
			// default onto the class defaults by finding the object, and finds only loaded ones.
			const UObject* Loaded = nullptr;
			if (Category == UEdGraphSchema_K2::PC_Object)
			{
				Loaded = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			else
			{
				Loaded = LoadObject<UClass>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (Loaded == nullptr && !ObjectPath.EndsWith(TEXT("_C")))
				{
					// A Blueprint named by its asset path: the class is the asset's `_C`.
					Loaded = LoadObject<UClass>(nullptr, *(ObjectPath + TEXT("_C")), nullptr, LOAD_NoWarn | LOAD_Quiet);
				}
			}
			if (Loaded == nullptr)
			{
				OutReason = FString::Printf(TEXT("nothing %s loads from '%s'"),
					Category == UEdGraphSchema_K2::PC_Object ? TEXT("at all") : TEXT("that is a class"), *Value.Raw);
				return false;
			}
			OutDefault = Loaded->GetPathName();
			return true;
		}
		if (Category == UEdGraphSchema_K2::PC_Byte)
		{
			const UEnum* Enum = Cast<UEnum>(SubObject);
			const int32 Index = Enum != nullptr && Value.Kind == EDreamUIValueKind::Identifier
				? Enum->GetIndexByNameString(Value.Raw) : INDEX_NONE;
			if (Index == INDEX_NONE)
			{
				OutReason = FString::Printf(TEXT("'%s' is not a value of %s"), *Value.Raw, *GetNameSafe(Enum));
				return false;
			}
			// The short name, which is how an enum default is spelled everywhere a Blueprint keeps one.
			OutDefault = Enum->GetNameStringByIndex(Index);
			return true;
		}
		OutReason = TEXT("internal: no default spelling for this type");
		return false;
	}
}

bool FDreamWidgetBlueprintCompilerContext::IsAuthoredMemberName(const FName InName) const
{
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, InName) != INDEX_NONE)
	{
		return true;
	}
	for (const TArray<TObjectPtr<UEdGraph>>* Graphs : { &Blueprint->FunctionGraphs, &Blueprint->MacroGraphs, &Blueprint->UbergraphPages })
	{
		for (const UEdGraph* Graph : *Graphs)
		{
			if (Graph != nullptr && Graph->GetFName() == InName)
			{
				return true;
			}
		}
	}
	for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
	{
		if (Timeline != nullptr && Timeline->GetVariableName() == InName)
		{
			return true;
		}
	}
	// And the functions no graph is named after -- a custom event in the event graph is a function of the class all
	// the same. Asked of last compile's skeleton, which is the only place they are listed; a member added since is
	// met by the compile itself, as a duplicate.
	return Blueprint->SkeletonGeneratedClass != nullptr
		&& Blueprint->SkeletonGeneratedClass->FindFunctionByName(InName, EIncludeSuperFlag::ExcludeSuper) != nullptr;
}

UEdGraph* FDreamWidgetBlueprintCompilerContext::DeclareDispatcher(const FName InName, const TArray<FBPVariableDescription>& InParameters)
{
	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr || InName.IsNone() || !DreamWidgetTextMembersLocal::IsGraphNameFree(Blueprint, InName))
	{
		return nullptr;
	}

	// The signature graph, as FBlueprintEditor::OnAddNewDelegate makes one -- an entry node whose output pins are the
	// parameters, named after the dispatcher, in DelegateSignatureGraphs -- and with two differences, both because
	// nobody authored it. It is TRANSIENT: a save writes the array slot as empty and the load drops it
	// (FBlueprintEditorUtils::PurgeNullGraphs), so the asset never holds a graph this compile will not make again.
	// And it is not editable: a parameter added in the details panel would be a change the next compile discards.
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, InName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	Graph->SetFlags(RF_Transient);
	Graph->bEditable = false;

	// Its reference set after Finalize, so the node grows no pins from whatever last compile's skeleton resolves the
	// name to; the parameters are the file's, and only the file's.
	FGraphNodeCreator<UK2Node_FunctionEntry> EntryCreator(*Graph);
	UK2Node_FunctionEntry* Entry = EntryCreator.CreateNode(/*bSelectNewNode*/false);
	EntryCreator.Finalize();
	Entry->FunctionReference.SetExternalMember(InName, nullptr);
	Entry->AddExtraFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
	for (const FBPVariableDescription& Parameter : InParameters)
	{
		Entry->CreateUserDefinedPin(Parameter.VarName, Parameter.VarType, EGPD_Output);
	}
	Blueprint->DelegateSignatureGraphs.Add(Graph);
	DispatcherSignatureGraphs.Emplace(Graph);

	// The variable, as FBlueprintEditorUtils::AddMemberVariable makes a dispatcher's: a multicast delegate pin with no
	// signature reference -- the compiler points the property at `<Name>__DelegateSignature`, the function the graph
	// above becomes -- assignable and callable, so hosts bind it and this class raises it like any Event Dispatcher.
	FBPVariableDescription Dispatcher;
	Dispatcher.VarName = InName;
	Dispatcher.VarGuid = FGuid::NewDeterministicGuid(InName.ToString());
	Dispatcher.VarType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	Dispatcher.FriendlyName = InName.ToString();
	Dispatcher.PropertyFlags = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance | CPF_BlueprintAssignable | CPF_BlueprintCallable;
	// Its own family in My Blueprint, beside "Props" and "Resources": what the file declared, apart from what the
	// author did.
	Dispatcher.Category = FText::FromString(TEXT("Events"));
	DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(Dispatcher));
	return Graph;
}

void FDreamWidgetBlueprintCompilerContext::DeclareTextMembers(const FDreamUIAst& InAst, FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamWidgetTextMembersLocal;

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr)
	{
		return;
	}
	bTextMembersDeclared = true;
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	UClass* ParentClass = Blueprint->ParentClass;

	// The names the file's own hierarchy takes -- every widget gets a class variable of its name, an unnamed one
	// included -- by the rule the variables are declared with. A host's `slot Detail { … }` is not one of them: it
	// fills a slot of a component and makes no widget called Detail.
	TMap<FName, FString> WidgetIds;
	InAst.ForEachNode([&WidgetIds](const FDreamUINode& InNode)
	{
		if (InNode.Id.IsEmpty() || (InNode.Kind == EDreamUINodeKind::NamedSlot && InNode.bFillsSlot))
		{
			return;
		}
		WidgetIds.Add(FName(*UDreamWidgetTree::SanitizeIdentifier(InNode.Id)), InNode.Id);
	});
	// And the resources, each a class variable of its own name, own and imported alike.
	TSet<FString> ResourceNames;
	for (const FDreamUIResource& Resource : InAst.Resources)
	{
		ResourceNames.Add(Resource.Name);
	}
	for (const FDreamUIResource& Resource : InAst.ImportedResources)
	{
		ResourceNames.Add(Resource.Name);
	}

	// Why InName is not free for a member the file declares, ready to follow a colon; empty when it is free. The
	// parent's PROPERTIES are not asked here: a prop or an event of the same name and type is the parent's, used
	// rather than redeclared, and each loop below decides that for itself.
	auto DescribeTaken = [this, &WidgetIds, &ResourceNames, ParentClass](const FName InName) -> FString
	{
		if (const FString* Id = WidgetIds.Find(InName))
		{
			return FString::Printf(TEXT("the widget '%s' is called that, and every widget has a class variable of its name"), **Id);
		}
		if (ResourceNames.Contains(InName.ToString()))
		{
			return TEXT("a resources entry is called that, and each one is a class variable of its own");
		}
		if (IsAuthoredMemberName(InName))
		{
			return TEXT("this Blueprint declares a member of its own by that name");
		}
		if (ParentClass != nullptr && ParentClass->FindFunctionByName(InName) != nullptr)
		{
			return FString::Printf(TEXT("%s has a function of that name"), *ParentClass->GetName());
		}
		return FString();
	};

	// The namespace the builder keys this file's strings by, so a prop's default text sits beside them.
	const FString LocalizationNamespace = InAst.ClassPath.IsEmpty() ? OutDiagnostics.SourceName : InAst.ClassPath;

	for (const FDreamUIPropDecl& Prop : InAst.Props)
	{
		const FName Name(*Prop.Name);
		if (Name.IsNone() || TextMemberNames.Contains(Name))
		{
			// A second line of one name is the parser's DuplicateProp, which keeps the first.
			continue;
		}

		FEdGraphPinType PinType;
		FString Reason;
		if (!DreamUIExpressionThunks::MakeDeclaredPinType(Prop.TypeName, Prop.EnumPath, PinType, Reason))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::PropTypeUnknown, Prop.Location,
				FString::Printf(TEXT("prop '%s' cannot be declared: %s"), *Prop.Name, *Reason));
			continue;
		}
		const FString Taken = DescribeTaken(Name);
		if (!Taken.IsEmpty())
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::PropNameTaken, Prop.Location,
				FString::Printf(TEXT("prop '%s' cannot be declared: %s. Rename one of them."), *Prop.Name, *Taken));
			continue;
		}

		FString DefaultValue;
		if (Prop.DefaultValue.IsSet() && !FormatPropDefault(Prop, PinType, InAst, LocalizationNamespace, DefaultValue, Reason))
		{
			// Said, and the prop still declared without it: a missing default fails nothing else, while a missing
			// variable would fail every binding and every host line that names it.
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::PropDefaultInvalid,
				Prop.DefaultValue->Location.IsValid() ? Prop.DefaultValue->Location : Prop.Location,
				FString::Printf(TEXT("the default of prop '%s %s' cannot be used: %s"), *Prop.TypeName, *Prop.Name, *Reason));
			DefaultValue.Reset();
		}

		if (const FProperty* Inherited = ParentClass != nullptr ? ParentClass->FindPropertyByName(Name) : nullptr)
		{
			// The C++ base already has it: the file is describing that property, not declaring a second one. A second
			// would shadow the parent's, and the parent's own code would go on reading a value no host can set.
			FEdGraphPinType InheritedType;
			if (K2Schema->ConvertPropertyToPinType(Inherited, InheritedType) && ArePropTypesCompatible(PinType, InheritedType))
			{
				TextMemberNames.Add(Name);
				if (!DefaultValue.IsEmpty())
				{
					InheritedPropDefaults.Emplace(Name, DefaultValue);
				}
				continue;
			}
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::PropNameTaken, Prop.Location, FString::Printf(
				TEXT("prop '%s' is declared %s, and %s already has a '%s' of another type (%s). Declare it with that type to use the parent's, or rename it."),
				*Prop.Name, *Prop.TypeName, *ParentClass->GetName(), *Prop.Name, *UEdGraphSchema_K2::TypeToText(InheritedType).ToString()));
			continue;
		}

		FBPVariableDescription Variable;
		Variable.VarName = Name;
		// From the name, like every generated variable here: stable across compiles and machines, nothing to store.
		Variable.VarGuid = FGuid::NewDeterministicGuid(Name.ToString());
		Variable.VarType = PinType;
		Variable.FriendlyName = Prop.Name;
		// For HOSTS to set and this file to bind, which is the opposite of a resource: editable on every instance
		// (a host's `Label = "Language"` is an instance edit), read-write in graphs, and offered when a graph spawns
		// the widget. FieldNotify, so a graph setting it is heard by every `<->` that mirrors it.
		Variable.PropertyFlags = CPF_Edit | CPF_BlueprintVisible;
		Variable.Category = FText::FromString(TEXT("Props"));
		Variable.SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		Variable.SetMetaData(FBlueprintMetadata::MD_FieldNotify, TEXT(""));
		Variable.DefaultValue = MoveTemp(DefaultValue);
		DreamBlueprint->GeneratedVariables.Add(Variable);
		DeclaredPropVariables.Add(MoveTemp(Variable));
		TextMemberNames.Add(Name);
	}

	TSet<FName> SeenEvents;
	for (const FDreamUIEventDecl& Event : InAst.Events)
	{
		const FName Name(*Event.Name);
		bool bAlreadySeen = false;
		SeenEvents.Add(Name, &bAlreadySeen);
		if (Name.IsNone() || bAlreadySeen)
		{
			// The parser's DuplicateEvent, which keeps the first.
			continue;
		}
		auto Refuse = [this, &OutDiagnostics, &Event](const EDreamUIDiagnosticCode InCode, const FDreamUISourceLocation& InLocation, FString InMessage)
		{
			OutDiagnostics.AddError(InCode, InLocation, MoveTemp(InMessage));
			// So an `emit` of it is not reported a second time, from further away, as an event that does not exist.
			RefusedEventNames.Add(Event.Name);
		};

		FString Taken = TextMemberNames.Contains(Name) ? FString(TEXT("a prop of this file is called that")) : DescribeTaken(Name);
		if (Taken.IsEmpty() && !IsGraphNameFree(Blueprint, Name))
		{
			Taken = TEXT("the Blueprint already holds an object of that name, which the dispatcher's signature graph would have to replace");
		}
		if (!Taken.IsEmpty())
		{
			Refuse(EDreamUIDiagnosticCode::EventNameTaken, Event.Location,
				FString::Printf(TEXT("event '%s' cannot be declared: %s. Rename one of them."), *Event.Name, *Taken));
			continue;
		}

		TArray<FBPVariableDescription> Parameters;
		bool bParametersKnown = true;
		for (const FDreamUIEventParam& Parameter : Event.Params)
		{
			FEdGraphPinType PinType;
			FString Reason;
			if (!DreamUIExpressionThunks::MakeDeclaredPinType(Parameter.TypeName, Parameter.EnumPath, PinType, Reason))
			{
				Refuse(EDreamUIDiagnosticCode::PropTypeUnknown, Parameter.Location,
					FString::Printf(TEXT("parameter '%s' of event '%s' cannot be declared: %s"), *Parameter.Name, *Event.Name, *Reason));
				bParametersKnown = false;
				continue;
			}
			FBPVariableDescription& Declared = Parameters.AddDefaulted_GetRef();
			Declared.VarName = FName(*Parameter.Name);
			Declared.VarType = PinType;
			Declared.FriendlyName = Parameter.Name;
		}
		if (!bParametersKnown)
		{
			continue;
		}

		if (const FProperty* Inherited = ParentClass != nullptr ? ParentClass->FindPropertyByName(Name) : nullptr)
		{
			// A C++ base's dispatcher of these parameters is this event already, as a base's UPROPERTY is a prop.
			if (IsCompatibleDispatcher(CastField<FMulticastDelegateProperty>(Inherited), Parameters))
			{
				TextMemberNames.Add(Name);
				continue;
			}
			Refuse(EDreamUIDiagnosticCode::EventNameTaken, Event.Location, FString::Printf(
				TEXT("event '%s' cannot be declared: %s already has a '%s', and it is not a dispatcher taking %d parameter(s) of these types"),
				*Event.Name, *ParentClass->GetName(), *Event.Name, Parameters.Num()));
			continue;
		}

		if (DeclareDispatcher(Name, Parameters) == nullptr)
		{
			Refuse(EDreamUIDiagnosticCode::EventNameTaken, Event.Location,
				FString::Printf(TEXT("internal: the dispatcher '%s' could not be made"), *Event.Name));
			continue;
		}
		FDreamWidgetTextDispatcher& Declared = DeclaredDispatchers.AddDefaulted_GetRef();
		Declared.Name = Name;
		Declared.Parameters = MoveTemp(Parameters);
		TextMemberNames.Add(Name);
	}
}

void FDreamWidgetBlueprintCompilerContext::CreateFunctionList()
{
	// AddUnique: a skeleton-only job has no conform pass to take them out, and they are still in the array.
	for (const TStrongObjectPtr<UEdGraph>& Graph : DispatcherSignatureGraphs)
	{
		if (Graph.IsValid())
		{
			Blueprint->DelegateSignatureGraphs.AddUnique(Graph.Get());
		}
	}
	Super::CreateFunctionList();
}

void FDreamWidgetBlueprintCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	Super::CopyTermDefaultsToDefaultObject(DefaultObject);

	if (DefaultObject == nullptr)
	{
		return;
	}
	for (const TPair<FName, FString>& Inherited : InheritedPropDefaults)
	{
		const FProperty* Property = DefaultObject->GetClass()->FindPropertyByName(Inherited.Key);
		if (Property == nullptr)
		{
			continue;
		}
		if (!FBlueprintEditorUtils::PropertyValueFromString(Property, Inherited.Value, reinterpret_cast<uint8*>(DefaultObject), DefaultObject))
		{
			MessageLog.Warning(*FText::Format(
				LOCTEXT("InheritedPropDefaultNotApplied", "The .dui's default for \"{0}\" could not be written onto {1}'s property of that name."),
				FText::FromName(Inherited.Key), FText::FromString(GetNameSafe(DefaultObject->GetClass()->GetSuperClass()))).ToString());
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::BuildWidgetTreeFromTextSource(FDreamUIDiagnosticBag& OutDiagnostics)
{
	// Until a file is named, there is none: nothing built, and nothing kept from an earlier build.
	TextSourceOutcome = ETextSourceOutcome::NoSource;
	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr)
	{
		return;
	}

	// Edits mirrored into the template but not yet flushed into the .dui die here otherwise: this
	// function reads the FILE and rebuilds the tree from it, and the designer triggers a skeleton
	// compile on every structural edit -- so an unflushed property edit would be rebuilt away
	// moments after it was made. Flushing at the top of the read is the one chokepoint every
	// compile type passes through (the pre-compile broadcast fires a stage too late, after this
	// runs). Free when the template is clean; the flush's own write is invisible to the source
	// watcher through the own-write hash. Skipped while a transaction is being applied: the flush
	// opens a transaction of its own, and an undo application is no place to start one.
	//
	// And skipped a second way, which is the one that could destroy somebody's work: when this
	// compile was caused by the FILE changing rather than by the designer. FlushTemplateChanges
	// diffs the template tree -- built from the text as it was -- against a reference tree built
	// from the text as it IS, so every difference the outside edit introduced reads as a designer
	// edit and gets patched back OUT of the document and saved. The compile that was supposed to
	// pick up an external change would instead revert it, silently, and the author's next clue
	// would be a file that keeps undoing itself. On that path the file wins and the preview's
	// unflushed values are what has to go; they are said out loud rather than dropped quietly.
	if (!GIsTransacting)
	{
		if (FDreamWidgetBlueprintEditor* OpenEditor = FDreamWidgetBlueprintEditor::FindEditorForBlueprint(DreamBlueprint))
		{
			if (const TSharedPtr<FDreamWidgetPreviewHost> Host = OpenEditor->GetPreviewHost())
			{
				if (!FDreamUISourceWatcher::IsCompilingFromExternalChange())
				{
					Host->FlushTemplateChanges();
				}
				else if (Host->IsTemplateDirty())
				{
					MessageLog.Warning(*FString::Printf(
						TEXT("The .dui behind \"%s\" changed outside the editor, so the file wins: designer edits that had not been written back to it are gone. Make them again on top of the new text."),
						*GetNameSafe(DreamBlueprint)));
				}
			}
		}
	}

	// SourceFile is a CLASS DEFAULT, so the CDO is where it is read from -- and the CDO still
	// standing at this point in the compile is the previous one, which is exactly the object carrying
	// what the author typed into the Class Defaults panel. CleanAndSanitizeClass has not run yet, and
	// the new CDO that eventually replaces this one has these values copied onto it, so the path
	// survives every recompile without being stored anywhere but where the author put it.
	//
	// The parent's default is the fallback and not a second source: it answers the two moments the
	// generated class has no CDO to ask -- a Blueprint that has never been compiled -- and it is how a
	// native subclass that hardcodes its own .dui works at all.
	const UDreamTextUserWidget* Defaults = nullptr;
	if (DreamBlueprint->GeneratedClass != nullptr)
	{
		Defaults = Cast<UDreamTextUserWidget>(DreamBlueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false));
	}
	if (Defaults == nullptr && DreamBlueprint->ParentClass != nullptr)
	{
		Defaults = Cast<UDreamTextUserWidget>(DreamBlueprint->ParentClass->GetDefaultObject(/*bCreateIfNeeded*/false));
	}
	if (Defaults == nullptr)
	{
		return;
	}

	const FString AuthoredPath = Defaults->SourceFile.FilePath.TrimStartAndEnd();
	if (AuthoredPath.IsEmpty())
	{
		// The negative control, and the single most important line in this function. A widget
		// blueprint that names no .dui has to come out of this compile byte for byte what it would
		// have been before the text pipeline existed: same tree object, same bindings, same messages.
		return;
	}

	// A file is named from here on, and every return below that installs nothing keeps the previous hierarchy --
	// and with it the resource variables the last good read declared (PopulateBlueprintGeneratedVariables).
	TextSourceOutcome = ETextSourceOutcome::KeptPrevious;

	bool bRootTokenResolved = true;
	const FString ResolvedPath = DreamUIPaths::Resolve(AuthoredPath, &bRootTokenResolved);
	// The path, not the leaf name. Every diagnostic below is prefixed with this, in the layout an
	// editor turns into a jump -- "C:/Proj/DUI/Login.dui(12,5): error DUI2001: ..." -- and a bare
	// "Login.dui" is a string a message log cannot do anything with.
	OutDiagnostics.SourceName = ResolvedPath;

	FString SourceText;
	if (!FFileHelper::LoadFileToString(SourceText, *ResolvedPath))
	{
		// Both spellings, deliberately: the author wrote AuthoredPath and will go looking for that,
		// while the file that is missing is at ResolvedPath. A message naming only one of them cannot
		// tell "you misspelled it" apart from "your relative path resolved somewhere you did not
		// expect" -- and a relative path is now SEARCHED across roots, so the resolved one is merely
		// where the search gave up. The roots themselves are what a reader needs to see, because the
		// most likely cause of this error is a file sitting somewhere that is not a root at all.
		FString Message = FString::Printf(
			TEXT("Source File is '%s', and there is no readable file at '%s'"), *AuthoredPath, *ResolvedPath);
		if (!bRootTokenResolved)
		{
			// `Plugin.X:...` where no enabled plugin X has a DUI directory. The resolved path names
			// the PROJECT's root, which is nowhere the author wrote -- so the plugin is named here
			// instead of leaving a reader to wonder why the message quotes a folder they never
			// mentioned. This was the last silent half of the plugin-qualified spelling.
			FString Token;
			AuthoredPath.Split(TEXT(":"), &Token, nullptr);
			Message += FString::Printf(
				TEXT(". '%s' names no enabled plugin with a %s directory, so the path above is the project's own root standing in"),
				*Token, DreamUIPaths::SourceDirectoryName);
		}
		else if (FPaths::IsRelative(AuthoredPath))
		{
			TArray<FString> RootDirectories;
			for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
			{
				RootDirectories.Add(Root.Directory);
			}
			Message += RootDirectories.Num() > 0
				? FString::Printf(TEXT(". Searched: %s"), *FString::Join(RootDirectories, TEXT(", ")))
				: FString::Printf(TEXT(". No %s directory exists in this project or any enabled plugin"),
					DreamUIPaths::SourceDirectoryName);
		}
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::SourceFileUnreadable, FDreamUISourceLocation(), Message);
		return;
	}

	FDreamUIAst Ast;
	if (!FDreamUISourceFile::Parse(SourceText, ResolvedPath, Ast, OutDiagnostics, FDreamUISourceFile::MakeFileImportReader()))
	{
		// The previous hierarchy is left exactly where it is. A file that will not parse says nothing
		// about what the class should contain, and blanking the tree here would turn one typo into a
		// designer with nothing in it and a graph full of missing-variable errors that all point away
		// from the actual mistake. The compile still fails: the parse errors are already in the bag.
		return;
	}

	// The `class` line, checked against the asset actually being compiled. A WARNING: the line's
	// job is a stable localization namespace, so a wrong one drifts keys rather than breaking the
	// build -- and a file deliberately shared by a native parent across subclasses legitimately
	// matches none of them, which an error would forbid. Both spellings of the same asset pass
	// ("/Game/UI/WBP_X" and "/Game/UI/WBP_X.WBP_X").
	if (!Ast.ClassPath.IsEmpty() && !Ast.ClassPath.StartsWith(TEXT("/Script/")))
	{
		const FString PackageName = DreamBlueprint->GetOutermost()->GetName();
		FString Claimed = Ast.ClassPath;
		int32 DotIndex;
		if (Claimed.FindLastChar(TEXT('.'), DotIndex))
		{
			Claimed.LeftInline(DotIndex);
		}
		if (!Claimed.Equals(PackageName, ESearchCase::IgnoreCase))
		{
			OutDiagnostics.AddWarning(EDreamUIDiagnosticCode::ClassPathMismatch, Ast.ClassPathLocation,
				FString::Printf(TEXT("this file says it compiles into '%s', but it is being compiled into '%s' -- localization keys will use the name in the file"),
					*Ast.ClassPath, *PackageName));
		}
	}

	// The members the file declares for the class -- `props` and `events` -- the moment it has parsed: the thunk pass
	// below lowers `Text <- Label` into a getter of the variable declared here, and checks every `-> emit` against the
	// dispatchers this accepted. A file that parsed declares them even when its tree then fails to build; they are
	// what it says, and keeping last time's instead would be the one place the file stopped winning.
	DeclareTextMembers(Ast, OutDiagnostics);

	TArray<FDreamWidgetPropertyBinding> Bindings;
	TArray<FDreamWidgetEventBinding> EventBindings;
	TArray<FDreamWidgetEachBinding> EachBindings;
	// Outered to the Blueprint, which is where UDreamWidgetBlueprint::GetOrCreateWidgetTree puts the
	// hand-authored one. That is not a detail: FinishCompilingClass duplicates THIS object onto the
	// generated class as the archetype, and SaveSubObjectsFromCleanAndSanitizeClass keeps it alive
	// through the sanitize pass by name -- both were written against a tree that lives on the asset.
	// The dependency edges, republished every parse: a saved style library recompiles its wearers.
	FDreamUISourceWatcher::NoteImports(ResolvedPath, Ast.Imports);

	// Expression bindings lower into generated pure functions here, BEFORE the builder reads the
	// AST: each `<- expr` line's BindingFunction is rewritten in place to its thunk's name, so the
	// builder, the runtime and every migration see exactly the one-name shape they always did. An
	// expression the generator refuses reports DUI5011 into the bag and clears its binding.
	//
	// `-> emit` routes are named in the same pass -- the handler's name goes into the line's EventHandler, which is
	// all the builder needs to record it as an ordinary route -- and given their bodies after the build, below.
	TArray<DreamUIExpressionThunks::FEmitRoute> EmitRoutes;
	DreamUIExpressionThunks::Generate(DreamBlueprint, Ast, OutDiagnostics, &EmitRoutes, &RefusedEventNames);

	UDreamWidgetTree* NewTree = FDreamUITextBuilder::Build(Ast, DreamBlueprint, OutDiagnostics, Bindings, &EventBindings, &EachBindings);
	if (!IsValid(NewTree) || !IsValid(NewTree->RootWidget))
	{
		// Its own code even though the builder has already said why. The builder reports a CAUSE (this
		// node names a type nothing resolves); this reports the OUTCOME (there is nothing to compile
		// into the class), and they are different facts that a reader acts on differently. It also
		// means this branch can never fail silently, whatever a future builder decides to return.
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::EmptyTree, Ast.Root.Location,
			TEXT("the file parsed but produced no hierarchy, so there is nothing to compile into this class"));
		return;
	}

	// The emit handlers' bodies, now that there is a tree to find each route's source event on: a handler has to take
	// exactly what that event sends, and which event it is -- a widget's, its visual's, a behaviour's, of whatever
	// class the node's type came to -- was the builder's to work out. Still inside STAGE V, so the skeleton the
	// compilation manager makes next declares them and every compile after this one finds them by name.
	DreamUIExpressionThunks::GenerateEmitHandlers(DreamBlueprint, Ast, EmitRoutes, NewTree, EventBindings, OutDiagnostics);

	// The same flags GetOrCreateWidgetTree hands the hand-authored tree, and matched on purpose:
	// everything downstream -- the duplicate onto the class, the designer's preview host, the
	// transaction buffer -- was written against that object, and a tree that differs from it only in
	// its flags is the kind of difference that surfaces three files away as "undo does nothing here".
	NewTree->SetFlags(RF_Transactional);

	// Animations ride across the rebuild. The grammar cannot author a sequence, so everything in a
	// SequenceArray was made in the animation editor and lives nowhere but the tree this compile is
	// about to drop -- without this carry, one compile deletes the author's animation work. Matched
	// by display name because that is the model animation bindings themselves resolve through, and
	// carried BEFORE MigrateRenamedWidgets so `(was: OldId)` clauses rewrite the carried paths.
	// AddComponentByTemplate goes through FObjectInstancingGraph, and SequenceArray is Instanced, so
	// the sequences are re-homed rather than pointer-shared with an object headed for the reaper.
	if (IsValid(DreamBlueprint->WidgetTree) && IsValid(DreamBlueprint->WidgetTree->RootWidget))
	{
		TMap<FString, UDreamWidget*> NewWidgetsByDisplayName;
		TArray<UDreamWidget*> NewWidgets;
		UDreamWidget::CollectChildrenWidgets(NewTree->RootWidget, NewWidgets, /*IncludeTarget*/true);
		for (UDreamWidget* NewWidget : NewWidgets)
		{
			NewWidgetsByDisplayName.Add(NewWidget->GetDisplayName(), NewWidget);
		}

		// `(was: OldId)` -- the clause that exists so a rename carries everything the old name owned,
		// and which this carry was the one leg not consulting. Matched by NEW name alone, a renamed
		// node found nothing, its animations died with the old tree, and the warning below told the
		// author to write the very clause they had already written. Read off the AST rather than the
		// tree because the clause is a fact about the FILE; the tree only ever keeps the name that
		// won. Keyed by FString, so the lookup is case-insensitive exactly like MigrateRenamedWidgets'.
		TMap<FString, FString> NewIdByOldId;
		Ast.ForEachNode([&NewIdByOldId](const FDreamUINode& InNode)
		{
			if (!InNode.WasId.IsEmpty() && !InNode.Id.IsEmpty())
			{
				NewIdByOldId.Add(InNode.WasId, InNode.Id);
			}
		});

		// The language-owned animations the file has handed over: `timeline <Name> external` written where
		// the block was is the way out the read-only animation editor names ("hand the animation to
		// Sequencer for good"), and the keys the block held are on the animation the previous compile
		// built and nowhere else. Those are carried and become the editor's from here on. Every other
		// language-owned animation is the file's: rebuilt a moment ago when the file still declares it,
		// and gone with the block when it does not.
		TSet<FString> HandedOverTimelines;
		for (const FDreamUITimeline& Timeline : Ast.Timelines)
		{
			if (Timeline.bExternal)
			{
				HandedOverTimelines.Add(Timeline.Name);
			}
		}

		TArray<UDreamWidget*> OldWidgets;
		UDreamWidget::CollectChildrenWidgets(DreamBlueprint->WidgetTree->RootWidget, OldWidgets, /*IncludeTarget*/true);
		for (UDreamWidget* OldWidget : OldWidgets)
		{
			UDreamWidgetAnimationComponent* OldAnimator = IsValid(OldWidget) ? OldWidget->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
			if (OldAnimator == nullptr || OldAnimator->GetSequenceArray().Num() == 0)
			{
				continue;
			}
			// The root hosts the animations in practice, and a renamed root has no name to match, so
			// root pairs with root regardless of what either is called.
			UDreamWidget* NewHome = nullptr;
			if (OldWidget == DreamBlueprint->WidgetTree->RootWidget)
			{
				NewHome = NewTree->RootWidget.Get();
			}
			else
			{
				const FString OldName = OldWidget->GetDisplayName();
				NewHome = NewWidgetsByDisplayName.FindRef(OldName);
				if (NewHome == nullptr)
				{
					// Nothing answers to the old name, so ask the file whether it was renamed.
					if (const FString* RenamedTo = NewIdByOldId.Find(OldName))
					{
						NewHome = NewWidgetsByDisplayName.FindRef(*RenamedTo);
					}
				}
			}
			if (!IsValid(NewHome))
			{
				// No widget by that name in the new file: the sequences stay with the old tree and
				// die with it. Said out loud, not silently.
				MessageLog.Warning(*FString::Printf(
					TEXT("Animations on widget '%s' could not be carried across the .dui rebuild: no widget with that name in the new hierarchy. Rename with (was: %s) to keep them."),
					*OldWidget->GetDisplayName(), *OldWidget->GetDisplayName()));
				continue;
			}

			// The new home MAY already animate, now that `timeline` blocks build animations of their
			// own -- and when it does, the whole-component re-home below would throw the file's
			// animations away and put the previous compile's back. So the carry is per ANIMATION
			// whenever the two have to coexist, and language-owned ones are never carried unless the
			// file has handed them over: the file rebuilt the rest a moment ago, and adopting the old
			// copy would overwrite what the author just wrote with what they wrote last time.
			if (UDreamWidgetAnimationComponent* NewAnimator = NewHome->GetComponent<UDreamWidgetAnimationComponent>())
			{
				for (UDreamWidgetAnimation* OldAnimation : OldAnimator->GetSequenceArray())
				{
					if (!IsValid(OldAnimation))
					{
						continue;
					}
					const bool bHandedOver = OldAnimation->IsLanguageOwned()
						&& HandedOverTimelines.Contains(OldAnimation->GetDisplayNameString());
					if (OldAnimation->IsLanguageOwned() && !bHandedOver)
					{
						continue;
					}
					// A name the file has just claimed for a timeline wins: the text is the truth,
					// and two animations of one name is a class variable nobody can address.
					const FString Name = OldAnimation->GetDisplayNameString();
					if (NewAnimator->GetSequenceByDisplayName(Name) != nullptr)
					{
						MessageLog.Warning(*FString::Printf(
							TEXT("Animation \"%s\" was made in the animation editor and the .dui now declares a timeline of the same name. The file wins; rename one of them to keep both."),
							*Name));
						continue;
					}
					if (bHandedOver)
					{
						// On the old copy, which the tree this compile drops is the only holder of:
						// AdoptAnimation refuses a language-owned source, and from here on this one is not.
						OldAnimation->SetLanguageOwned(false);
					}
					NewAnimator->AdoptAnimation(OldAnimation);
				}
				continue;
			}

			// Nothing on the new home, so the whole component moves -- and then gives back what the
			// file owns. A language-owned sequence does reach this branch: when the file stops
			// declaring timelines the builder makes no component, so the old one, timelines and all,
			// was copied here whole, and every one of them then survived every later compile as a
			// read-only animation and a class variable that nothing in the file could remove.
			UDreamWidgetAnimationComponent* Carried =
				Cast<UDreamWidgetAnimationComponent>(NewHome->AddComponentByTemplate(OldAnimator));
			if (Carried != nullptr)
			{
				for (int32 Index = Carried->GetSequenceArray().Num() - 1; Index >= 0; --Index)
				{
					UDreamWidgetAnimation* CarriedAnimation = Carried->GetSequenceArray()[Index];
					if (!IsValid(CarriedAnimation) || !CarriedAnimation->IsLanguageOwned())
					{
						continue;
					}
					if (HandedOverTimelines.Contains(CarriedAnimation->GetDisplayNameString()))
					{
						CarriedAnimation->SetLanguageOwned(false);
					}
					else
					{
						Carried->DeleteAnimationByIndex(Index);
					}
				}
			}
		}
	}

	// The `external` manifest, checked both ways. The proposal's second layer exists so that "what
	// animations does this class have" is answerable from the FILE -- which is only true if the file
	// lists the ones it does not contain. Warnings rather than errors on both halves: an animation
	// that is not listed still works, and a listed one that is missing is a file describing a plan
	// rather than a state. Either way the author is told which name is out of step.
	if (IsValid(NewTree) && IsValid(NewTree->RootWidget))
	{
		TSet<FString> DeclaredExternal;
		for (const FDreamUITimeline& Timeline : Ast.Timelines)
		{
			if (Timeline.bExternal)
			{
				DeclaredExternal.Add(Timeline.Name);
			}
		}

		TSet<FString> LiveAnimations;
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(NewTree->RootWidget, Widgets, /*IncludeTarget*/true);
		for (UDreamWidget* Widget : Widgets)
		{
			UDreamWidgetAnimationComponent* Animator = IsValid(Widget) ? Widget->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
			if (Animator == nullptr)
			{
				continue;
			}
			for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
			{
				if (!IsValid(Animation) || Animation->IsLanguageOwned())
				{
					continue;
				}
				LiveAnimations.Add(Animation->GetDisplayNameString());
				if (!DeclaredExternal.Contains(Animation->GetDisplayNameString()))
				{
					MessageLog.Warning(*FString::Printf(
						TEXT("Animation \"%s\" was made in the animation editor and this .dui does not mention it. Add \"timeline %s external\" so the file lists every animation the class has."),
						*Animation->GetDisplayNameString(), *Animation->GetDisplayNameString()));
				}
			}
		}
		for (const FString& Name : DeclaredExternal)
		{
			if (!LiveAnimations.Contains(Name))
			{
				MessageLog.Warning(*FString::Printf(
					TEXT("\"timeline %s external\" names an animation this class does not have. Make it in the animation editor, or delete the line."),
					*Name));
			}
		}
	}

	// The one field the text owns. Replaced rather than merged: the .dui is the whole hierarchy, so
	// anything still in the old tree is by definition not in the file any more.
	//
	// Recorded by the transaction this compile runs inside, when there is one: an animation added, deleted or renamed in
	// the panel compiles the skeleton inside its own undo step, and a swap the step did not hold left Ctrl+Z restoring a
	// tree nobody used any more. Without marking the package: outside a transaction this is a compile, not an edit.
	DreamBlueprint->Modify(/*bAlwaysMarkDirty*/ false);
	DreamBlueprint->WidgetTree = NewTree;
	// And the resources ride along for PopulateBlueprintGeneratedVariables, which declares one class
	// variable per entry a few lines after this function returns.
	// The file's own entries, and then the ones a `use` brought in.
	//
	// The imported half is new, and the rule it replaces was written down as deliberate: "imported
	// resources become nobody's class variables". The reasoning was that a variable belongs to one
	// class and an imported entry belongs to a library -- which is true and is not an argument for
	// declaring nothing. `@Accent` RESOLVES through the import chain on every line that writes it, so
	// a file could already spell an imported resource everywhere except in the one place a designer
	// or a graph could see it. Two importers each getting their own variable is not a conflict:
	// they are two classes, the entries are constants, and each compiles the value it read.
	//
	// Local first and appended only when the name is free, so FindResource's shadowing rule -- a
	// local declaration wins over an imported one -- is the rule the variables follow too.
	TextResources = Ast.Resources;
	for (const FDreamUIResource& Imported : Ast.ImportedResources)
	{
		const bool bShadowed = TextResources.ContainsByPredicate(
			[&Imported](const FDreamUIResource& InExisting) { return InExisting.Name == Imported.Name; });
		if (!bShadowed)
		{
			TextResources.Add(Imported);
		}
	}
	TextSourceOutcome = ETextSourceOutcome::Built;
	// The widgets the author did not name, whose variables the declarations further down hide: read off the AST, which
	// is the only place that knows a node's id was made up rather than written, and only now that the tree built from
	// it is the one the class keeps.
	HiddenWidgetVariableNames.Reset();
	Ast.ForEachNode([this](const FDreamUINode& InNode)
	{
		if (InNode.bAnonymous && !InNode.Id.IsEmpty())
		{
			HiddenWidgetVariableNames.Add(FName(*UDreamWidgetTree::SanitizeIdentifier(InNode.Id)));
		}
	});
	// And the bindings alongside it, for the same reason -- `<-` lines live in the same file. These
	// are the AUTHORED list; CompilePropertyBindings resolves them onto the class at the end of the
	// compile and reports the ones that cannot be honoured, which is how a .dui naming a function the
	// Blueprint does not declare becomes an error here rather than a null at run time.
	DreamBlueprint->PropertyBindings = MoveTemp(Bindings);
	DreamBlueprint->EventBindings = MoveTemp(EventBindings);
	DreamBlueprint->EachBindings = MoveTemp(EachBindings);

	// Last, with the new hierarchy in place: `(was: OldId)` moves what the OLD name still owns onto
	// the new one. See MigrateRenamedWidgets for why after the install and not before.
	MigrateRenamedWidgets(Ast, ResolvedPath, OutDiagnostics);
}

void FDreamWidgetBlueprintCompilerContext::ReportTextDiagnostics(const FDreamUIDiagnosticBag& InDiagnostics)
{
	for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
	{
		// ToString() verbatim, never a reworded copy. "File(Line,Col): severity DUInnnn: text" is
		// what makes a message log line something a reader can double-click, and the file and line
		// are the entire reason a text pipeline is better than a binary one.
		//
		// Passed as the format string with no varargs, which is safe on purpose: FCompilerResultsLog
		// only looks for @@ when arguments follow it, so a diagnostic quoting a .dui that happens to
		// contain one is printed rather than eaten.
		if (Diagnostic.IsError())
		{
			MessageLog.Error(*Diagnostic.ToString());
		}
		else
		{
			MessageLog.Warning(*Diagnostic.ToString());
		}
	}
}

namespace DreamWidgetRenameMigrationLocal
{
	/** "Login.dui(12,5): " -- the prefix that makes a message log line something a reader can jump from. */
	FString SourcePrefix(const FString& InSourceName, const FDreamUISourceLocation& InLocation)
	{
		// Hand-built rather than routed through FDreamUIDiagnostic::ToString, and now only for the
		// NOTES: the three conflict errors and the graph refusal have taken their DUInnnn codes
		// (3010-3013) and go through the bag, which prints this same layout and also reaches the
		// mailbox. A note has no severity the bag can carry and no code the table wants to spend, so
		// it keeps the hand-built prefix -- the FILE and LINE are the half that has to be right,
		// because that is the whole argument for a text pipeline over a binary one.
		FString Prefix = InSourceName;
		if (InLocation.IsValid())
		{
			Prefix += FString::Printf(TEXT("(%d,%d)"), InLocation.Line, InLocation.Column);
		}
		return Prefix.IsEmpty() ? FString() : Prefix + TEXT(": ");
	}

	/**
	 * Whether anything on this node will be localized under a key derived from its id.
	 *
	 * A string literal is the only value kind that can become an FText, and the builder keys one as
	 * `<id>.<Property>` unless the author wrote `@key(...)`. So a rename silently re-keys every one of
	 * them, and no fixup in this file can carry that across: the translations are not in the asset,
	 * they are in the localization archive next to it. All the compiler can do is say so at the one
	 * moment the author is looking at the rename.
	 */
	bool HasIdDerivedLocalizationKey(const FDreamUINode& InNode, const FDreamUIAst& InAst)
	{
		auto AnyUnkeyedString = [](const TArray<FDreamUIProperty>& InProperties)
		{
			for (const FDreamUIProperty& Property : InProperties)
			{
				if (!Property.IsBinding()
					&& Property.Value.Kind == EDreamUIValueKind::String
					&& Property.Value.LocalizationKeyOverride.IsEmpty())
				{
					return true;
				}
			}
			return false;
		};

		if (AnyUnkeyedString(InNode.Properties) || AnyUnkeyedString(InNode.SlotProperties))
		{
			return true;
		}
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			if (AnyUnkeyedString(Component.Properties))
			{
				return true;
			}
		}
		// A string can also reach the node THROUGH the style it wears -- the builder keys those by
		// this node's id exactly the same way, so a rename orphans them exactly the same way. The
		// walk mirrors the builder's chain (base upward, cycle-guarded); a broken chain just stops,
		// because the builder already reported it and this predicate only decides whether to hint.
		const FDreamUIStyle* Style = InNode.StyleName.IsEmpty() ? nullptr : InAst.FindStyle(InNode.StyleName);
		TSet<const FDreamUIStyle*> Visited;
		while (Style != nullptr && !Visited.Contains(Style))
		{
			Visited.Add(Style);
			if (AnyUnkeyedString(Style->Properties))
			{
				return true;
			}
			Style = Style->BaseName.IsEmpty() ? nullptr : InAst.FindStyle(Style->BaseName);
		}
		return false;
	}
}

int32 FDreamWidgetBlueprintCompilerContext::MigrateVariableReferences(UDreamWidgetBlueprint* InBlueprint,
	FName InOldVariableName, FName InNewVariableName, FString& OutRefusal)
{
	OutRefusal.Reset();
	if (InBlueprint == nullptr || InOldVariableName.IsNone() || InNewVariableName.IsNone() || InOldVariableName == InNewVariableName)
	{
		return 0;
	}

	// The graph leg matches by NAME ONLY. Everything below is about the one way that can do harm:
	// if something OTHER than what was renamed already answers to the old name, this would move
	// its references too -- silently, in a graph nobody has open.
	if (FBlueprintEditorUtils::FindNewVariableIndex(InBlueprint, InOldVariableName) != INDEX_NONE)
	{
		OutRefusal = FString::Printf(
			TEXT("this Blueprint declares a variable of its own called \"%s\", and a graph reference to that name cannot be told apart from one to what was renamed"),
			*InOldVariableName.ToString());
	}
	else if (InBlueprint->ParentClass != nullptr
		&& InBlueprint->ParentClass->FindPropertyByName(InOldVariableName) != nullptr)
	{
		// Not hypothetical: PopulateBlueprintGeneratedVariables deliberately skips a widget or an
		// animation whose name the parent already declares, so one called this never had a variable
		// of its own for anything to reference. The references belong to the parent's member.
		OutRefusal = FString::Printf(
			TEXT("\"%s\" is a member of the parent class %s, so the graph references to it are not this Blueprint's to move"),
			*InOldVariableName.ToString(), *InBlueprint->ParentClass->GetName());
	}

	TArray<UEdGraph*> AllGraphs;
	InBlueprint->GetAllGraphs(AllGraphs);

	if (OutRefusal.IsEmpty())
	{
		// Function-local variables, which FindNewVariableIndex does not see: they live on the
		// function entry node, not on the Blueprint. HandleVariableRenamed would happily repoint a
		// local variable reference of the same name while leaving the DECLARATION alone, which is
		// a graph that stops compiling with an error naming a variable the author never typed.
		for (const UEdGraph* Graph : AllGraphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
				if (Entry == nullptr)
				{
					continue;
				}
				for (const FBPVariableDescription& Local : Entry->LocalVariables)
				{
					if (Local.VarName == InOldVariableName)
					{
						OutRefusal = FString::Printf(
							TEXT("\"%s\" is also a local variable in \"%s\""),
							*InOldVariableName.ToString(), *Graph->GetName());
						break;
					}
				}
			}
		}
	}

	if (OutRefusal.IsEmpty())
	{
		// Nor onto a name one of the Blueprint's own members already answers to: the references would leave what was
		// renamed for that member, and the compile that refuses the clash comes after the nodes have moved.
		if (FBlueprintEditorUtils::FindNewVariableIndex(InBlueprint, InNewVariableName) != INDEX_NONE)
		{
			OutRefusal = FString::Printf(TEXT("this Blueprint already declares a variable called \"%s\""), *InNewVariableName.ToString());
		}
		else if (InBlueprint->FunctionGraphs.ContainsByPredicate([InNewVariableName](const UEdGraph* Graph)
			{
				return Graph != nullptr && Graph->GetFName() == InNewVariableName;
			}))
		{
			OutRefusal = FString::Printf(TEXT("this Blueprint already has a function called \"%s\""), *InNewVariableName.ToString());
		}
		else
		{
			for (const UEdGraph* Graph : AllGraphs)
			{
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
					if (Entry != nullptr && Entry->LocalVariables.ContainsByPredicate([InNewVariableName](const FBPVariableDescription& Local)
						{
							return Local.VarName == InNewVariableName;
						}))
					{
						OutRefusal = FString::Printf(TEXT("\"%s\" is already a local variable in \"%s\""),
							*InNewVariableName.ToString(), *Graph->GetName());
						break;
					}
				}
			}
		}
	}

	if (!OutRefusal.IsEmpty())
	{
		return 0;
	}

	// Counted before the replace, because ReplaceVariableReferences reports nothing. The same
	// question RenameVariableReferencesInGraph asks internally to decide whether it changed
	// anything, asked here so the note can say how much moved -- and so "nothing moved" can be
	// told apart from "it ran", which is the whole of the already-migrated case.
	//
	// This Blueprint's graphs only. A dependent Blueprint's references are fixed by the call
	// below and deliberately not counted: reaching into other assets to tally them would mean
	// walking every loaded Blueprint twice for a number nobody acts on.
	int32 GraphReferences = 0;
	for (const UEdGraph* Graph : AllGraphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UK2Node* K2Node = Cast<UK2Node>(Node);
			if (K2Node != nullptr && K2Node->ReferencesVariable(InOldVariableName, nullptr))
			{
				++GraphReferences;
			}
		}
	}

	if (InBlueprint->GeneratedClass != nullptr)
	{
		// Guarded on the class, not for safety -- a null one makes every node's scope check
		// fail and the whole call a no-op -- but on cost: ReplaceVariableReferences walks
		// every loaded UBlueprint to find dependents, and doing that to achieve nothing on
		// every compile of a Blueprint that has never been compiled is a poor trade.
		FBlueprintEditorUtils::ReplaceVariableReferences(InBlueprint, InOldVariableName, InNewVariableName);
	}
	return GraphReferences;
}

FDreamWidgetBlueprintCompilerContext::FWidgetRenameMigration
FDreamWidgetBlueprintCompilerContext::MigrateWidgetRename(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId, const FString& InNewId)
{
	FWidgetRenameMigration Result;
	if (InBlueprint == nullptr || InOldId.IsEmpty() || InNewId.IsEmpty())
	{
		return Result;
	}

	// Through the shared rule and never a second copy of it. The class declares its members with
	// MakeWidgetVariableName and the runtime resolves bindings with the same function; a rename that
	// computed the name any other way would move graph references onto a variable nothing declares.
	const FName OldVariableName(*UDreamWidgetTree::SanitizeIdentifier(InOldId));
	const FName NewVariableName(*UDreamWidgetTree::SanitizeIdentifier(InNewId));
	if (OldVariableName == NewVariableName)
	{
		// FName comparison, so this also catches `(was: okbtn)` on a node called OkBtn. Nothing below
		// would be actively wrong, but every leg would be asked to replace a name with itself and the
		// animation leg would count that as work done.
		return Result;
	}

	// --- 1. The graph. -----------------------------------------------------------------------
	//
	// FBlueprintEditorUtils::ReplaceVariableReferences, which is what UMG's own widget rename calls
	// (WidgetBlueprintOperationUtils.cpp, FWidgetBlueprintOperationUtils::RenameWidget). It walks
	// every graph of this Blueprint AND of every Blueprint that depends on it, letting each K2Node
	// repoint its own FMemberReference -- which is the part that cannot be done from outside, because
	// a variable get, a variable set, an event with a bound delegate and a component-bound node each
	// store the reference differently.
	//
	// Safe to call mid-compile, and specifically at this stage: it dirties the Blueprint through
	// MarkBlueprintAsModified, which early-outs while bBeingCompiled is set (BlueprintEditorUtils.cpp
	// :1924) -- and bBeingCompiled goes up at STAGE IV, one stage before the hook this runs under.
	// The node's own Modify() still records the change and dirties the package -- for the length of
	// the compile only: the compilation manager puts every compiled package's dirty flag back the way
	// it found it (BlueprintCompilationManager.cpp:1906), dependents' included, so MigrateRenamedWidgets
	// marks the packages it changed dirty again once the compile is over.
	//
	// One more thing had to be true for this stage to work, and it is worth writing down because it
	// is not obvious and it is what would silently undo the rename: HandleVariableRenamed moves the
	// NAME and leaves the node's MemberGuid alone, and FMemberReference::ResolveMember will happily
	// rename a reference BACK if that stale guid still matches a variable (MemberReference.cpp:468).
	// Our widget variables carry FGuid::NewDeterministicGuid(name), so the old id's guid is a real
	// guid that a lookup could hit -- except that the only place such a lookup reads is
	// UBlueprint::GeneratedVariables, and ResetAndPopulateBlueprintGeneratedVariables emptied that
	// list immediately before this hook. By the time it is refilled it holds the NEW names, whose
	// guids the old one does not match. Moving this fixup anywhere later in the compile reopens that
	// window.
	Result.GraphReferences = MigrateVariableReferences(InBlueprint, OldVariableName, NewVariableName, Result.GraphRefusal);

	// --- 2. The authored property bindings. --------------------------------------------------
	//
	// A `<-` line's WidgetName IS the variable name, so this is the same rename spelled in a second
	// place. Under a .dui the whole list was just rebuilt from the file and therefore already says
	// the new name, which makes this leg a no-op TODAY -- and it is here anyway, because the list is
	// a persistent field of the asset that the details panel can also write to. A binding that
	// survives a build (a hybrid asset, or the day the builder merges instead of replacing) has to
	// come through a rename, and a fixup that is missing on that day is a null at run time.
	{
		bool bModifiedBlueprint = false;
		for (FDreamWidgetPropertyBinding& Binding : InBlueprint->PropertyBindings)
		{
			if (Binding.WidgetName == OldVariableName)
			{
				if (!bModifiedBlueprint)
				{
					// Before the write, so the transaction records the value being replaced.
					InBlueprint->Modify();
					bModifiedBlueprint = true;
				}
				Binding.WidgetName = NewVariableName;
				++Result.PropertyBindings;
			}
		}
	}

	// --- 3 and 4. The animation paths: see MigrateWidgetRenamePaths. ----------------------------------
	MigrateWidgetRenamePaths(InBlueprint, InOldId, InNewId, Result);

	return Result;
}

void FDreamWidgetBlueprintCompilerContext::MigrateWidgetRenamePaths(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId,
	const FString& InNewId, FWidgetRenameMigration& OutResult)
{
	if (InBlueprint == nullptr || InOldId.IsEmpty() || InNewId.IsEmpty() || InOldId == InNewId)
	{
		return;
	}

	// --- 3. The embedded animation paths. ----------------------------------------------------
	//
	// The third identity, and the one that was silent before P0: an animation binding is a '/'-joined
	// chain of DISPLAY names from the widget owning the animation down to the widget being driven, so
	// a rename anywhere along that chain leaves the path naming a widget that no longer exists.
	// Worse than a null -- playback falls back to the stored pointer and every instance in the game
	// animates the class template's widget, successfully and off-screen.
	//
	// The tree walked is the one the compile KEEPS. Under a .dui that tree was rebuilt from the file
	// moments ago and carries no animations at all, because the text grammar cannot author a sequence
	// and the builder does not carry components across a rebuild -- so this leg, like the one above,
	// is defensive under text authoring today. It is written against a tree rather than against the
	// text pipeline for exactly that reason: the day animations survive a rebuild, or the day a
	// hand-authored hierarchy gets a rename clause of its own, this is already the right code.
	{
		UDreamWidgetTree* Tree = InBlueprint->WidgetTree;
		if (IsValid(Tree))
		{
			Tree->ForEachWidget([&OutResult, &InOldId, &InNewId](UDreamWidget* ContextWidget)
			{
				for (UDreamUIBehaviour* Component : ContextWidget->GetAllComponents())
				{
					UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
					if (Animator == nullptr)
					{
						continue;
					}
					for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
					{
						if (IsValid(Animation))
						{
							// The ids, not the variable names: a path is built from GetDisplayName and
							// resolved by comparing against GetDisplayName. The two spellings agree
							// for everything the parser accepts as an id -- its identifier charset is
							// exactly what SanitizeIdentifier keeps -- but they are DIFFERENT RULES,
							// and feeding the sanitized name to a display-name comparison is the kind
							// of "works until someone widens the charset" that this file avoids by
							// deriving each from the id separately.
							OutResult.AnimationBindings += Animation->RenameWidgetPathSegment(InOldId, InNewId);
						}
					}
				}
			});
		}
	}

	// --- 4. Standalone sequence assets authored against this class. --------------------------
	//
	// The fourth identity, and the fourth silent channel: a UDreamUISequence lives in its own
	// package, binds widgets by the same '/'-joined display names, and the class compile cannot
	// otherwise see it. PreviewWidgetClass is what makes the reach SAFE -- the sequence itself says
	// which class it is authored against, so this never blind-renames a segment in some other UI's
	// sequence that happens to reuse the id. Loaded sequences only: loading packages mid-compile is
	// its own hazard, and the unloaded ones get named in a warning by the caller instead.
	{
		for (TObjectIterator<UDreamUISequence> It; It; ++It)
		{
			UDreamUISequence* Sequence = *It;
			if (!IsValid(Sequence) || Sequence->PreviewWidgetClass.IsNull())
			{
				continue;
			}
			const UClass* SequenceClass = Sequence->PreviewWidgetClass.Get();
			const bool bMatches =
				(SequenceClass != nullptr
					&& (SequenceClass == InBlueprint->GeneratedClass || SequenceClass == InBlueprint->SkeletonGeneratedClass))
				|| (InBlueprint->GeneratedClass != nullptr
					&& Sequence->PreviewWidgetClass.ToSoftObjectPath() == FSoftObjectPath(InBlueprint->GeneratedClass));
			if (bMatches)
			{
				OutResult.ExternalSequenceBindings += Sequence->RenameWidgetPathSegments(InOldId, InNewId);
			}
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::MigrateRenamedWidgets(const FDreamUIAst& InAst, const FString& InSourceName,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamWidgetRenameMigrationLocal;

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr)
	{
		return;
	}

	// Every live id first, in its own pass. Walking once and checking as we go would only ever see
	// the ids ABOVE each `(was: ...)`, so `Text OkBtn (was: OkLabel)` written before a node still
	// called OkLabel would migrate and the same pair written the other way round would not: one file,
	// two answers, decided by line order.
	//
	// Keyed by FString, which TMap compares case-insensitively -- the same rule the parser's
	// duplicate-id check uses, and the right one, because these ids become FName member variables
	// that would collide anyway.
	TMap<FString, FDreamUISourceLocation> LiveIds;
	InAst.ForEachNode([&LiveIds](const FDreamUINode& Node)
	{
		if (!Node.Id.IsEmpty())
		{
			LiveIds.Add(Node.Id, Node.Location);
		}
	});

	TArray<const FDreamUINode*> Renames;
	TMap<FString, FDreamUISourceLocation> ClaimedOldIds;
	bool bFileContradictsItself = false;

	// Raised into the BAG rather than straight onto the message log, which is what makes them
	// reachable to anyone but this window. DUI3010-3013 were declared with the rest of the table and
	// had no raise site anywhere in the plugin: these three refusals and the graph one below were
	// bare MessageLog calls, so the mailbox the VSCode extension reads carried only the parser's and
	// the builder's verdicts and said a file was CLEAN while the compile it came from had failed.
	// The prefix comes off with them -- FDreamUIDiagnostic::ToString builds the same
	// "file(line,col): severity CODE: " layout, and ReportTextDiagnostics prints every one of these
	// a few lines after this function returns.
	InAst.ForEachNode([&LiveIds, &Renames, &ClaimedOldIds, &bFileContradictsItself, &OutDiagnostics](const FDreamUINode& Node)
	{
		if (Node.WasId.IsEmpty())
		{
			return;
		}

		if (Node.Id.Equals(Node.WasId, ESearchCase::IgnoreCase))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::SelfRename, Node.Location, FString::Printf(
				TEXT("\"%s\" names itself as its own old id. A rename clause says what a node USED to be called, so a node that was already called this has nothing to migrate; delete the '(was: %s)'."),
				*Node.Id, *Node.WasId));
			bFileContradictsItself = true;
			return;
		}

		if (const FDreamUISourceLocation* Live = LiveIds.Find(Node.WasId))
		{
			// Both names alive at once. Refused rather than guessed at, and this is the case the
			// guessing would be worst for: if the old name were migrated onto the new node anyway,
			// every reference would move OFF the node that still legitimately carries that name.
			// The author has renamed one node and created another with the old name in one edit, and
			// only they know which of the two their graph meant.
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::RenameOldIdStillInUse, Node.Location, FString::Printf(
				TEXT("\"%s\" says it was called \"%s\", but a node on line %d is still called that. Rename that one first, or drop the '(was: %s)' -- with both names in the file there is no way to tell which node a reference to \"%s\" meant."),
				*Node.Id, *Node.WasId, Live->Line, *Node.WasId, *Node.WasId));
			bFileContradictsItself = true;
			return;
		}

		if (const FDreamUISourceLocation* First = ClaimedOldIds.Find(Node.WasId))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::DuplicateWasId, Node.Location, FString::Printf(
				TEXT("\"%s\" says it was called \"%s\", and so does the node on line %d. One old name cannot become two new ones; keep the clause on whichever node inherits the references and delete the other."),
				*Node.Id, *Node.WasId, First->Line));
			bFileContradictsItself = true;
			return;
		}

		ClaimedOldIds.Add(Node.WasId, Node.Location);
		Renames.Add(&Node);
	});

	if (bFileContradictsItself)
	{
		// All or nothing. A half-applied set of renames leaves the asset in a state neither version
		// of the file describes, and the next compile then starts from that instead of from what the
		// author wrote -- so the second run of a broken file does different damage than the first.
		// The compile is already failing on the errors above; nothing here has to fail it again.
		return;
	}
	// No clause in the file: nothing to migrate, and no hop for an unloaded sequence asset to have
	// missed -- the scan at the end warned on every compile of every .dui whose class one references.
	if (Renames.Num() == 0)
	{
		return;
	}

	// What a rename writes into the graphs is an edit to those assets, and the compilation manager does
	// not keep it: every package it compiles gets its dirty flag back the way it found it, dependents'
	// included. So the packages are asked now, while their flags are still the author's, and the ones
	// this migration dirties are marked again when the compile is over -- otherwise nothing prompts for
	// the save, the note below says the line can be deleted, and the next session finds the graphs
	// naming the old variable with nothing left to migrate them.
	TArray<UPackage*> CleanDependentPackages;
	{
		TArray<UBlueprint*> Dependents;
		FBlueprintEditorUtils::FindDependentBlueprints(DreamBlueprint, Dependents);
		for (const UBlueprint* Dependent : Dependents)
		{
			UPackage* DependentPackage = Dependent != nullptr ? Dependent->GetOutermost() : nullptr;
			if (DependentPackage != nullptr && !DependentPackage->IsDirty())
			{
				CleanDependentPackages.AddUnique(DependentPackage);
			}
		}
	}
	bool bMigratedThisAsset = false;

	// One hop and no chain: each clause is applied against the asset as it stands, and the result is
	// never fed back in. `A (was: B)` while a previous version said `B (was: C)` migrates B to A and
	// leaves anything still on C where it is. Nothing enforces the ordering because nothing has to --
	// a rename whose new name is another rename's old name is exactly the "still called that" error
	// above, since every new name is a live id -- but the restriction is written down anyway, because
	// this codebase has already lost a day to assuming a redirect follows a second hop when
	// CoreRedirects, which also applies exactly once, does not.
	for (const FDreamUINode* Node : Renames)
	{
		const FWidgetRenameMigration Migration = MigrateWidgetRename(DreamBlueprint, Node->WasId, Node->Id);
		bMigratedThisAsset |= Migration.GraphReferences + Migration.PropertyBindings + Migration.AnimationBindings > 0;

		if (!Migration.GraphRefusal.IsEmpty())
		{
			// A warning and not an error: the other two legs ran, the hierarchy is fine, and the
			// author gets to decide whether the collision is a mistake or a name they meant to reuse.
			// Failing the compile here would block a build over a graph that may not reference the
			// name at all.
			OutDiagnostics.AddWarning(EDreamUIDiagnosticCode::RenameGraphReferenceAmbiguous, Node->Location, FString::Printf(
				TEXT("\"%s\" could not take the graph references from \"%s\": %s. Repoint them by hand, or rename the other one."),
				*Node->Id, *Node->WasId, *Migration.GraphRefusal));
		}

		// The localization key moved with the id and cannot be brought along: the translations live
		// in the localization archive, not in this asset. Said only when the node actually has a
		// string the builder would key, so an ordinary rename does not carry a paragraph about it.
		// A rename that re-keys localized strings deserves a WARNING of its own, not just the note's
		// appended sentence: notes are the first thing filtered out of a compile log, and orphaned
		// translations surface weeks later as English text in a shipped build.
		if (HasIdDerivedLocalizationKey(*Node, InAst))
		{
			MessageLog.Warning(*FString::Printf(
				TEXT("%sRenaming \"%s\" -> \"%s\" re-keys its localized strings (styles it wears included): translations recorded against \"%s.<property>\" are orphaned unless @key(\"%s.<property>\") pins them."),
				*SourcePrefix(InSourceName, Node->Location), *Node->WasId, *Node->Id, *Node->WasId, *Node->WasId));
		}

		const FString LocalizationHint = HasIdDerivedLocalizationKey(*Node, InAst)
			? FString::Printf(
				TEXT(" Its localized strings are keyed by id, so translations recorded against \"%s.<property>\" are NOT carried over -- write @key(\"%s.<property>\") on those lines to keep them."),
				*Node->WasId, *Node->WasId)
			: FString();

		if (Migration.Total() > 0)
		{
			MessageLog.Note(*FString::Printf(
				TEXT("%s\"%s\" took over from \"%s\": %d graph reference(s), %d property binding(s), %d animation path(s), %d sequence-asset binding(s). That is done on the asset, and it has to be saved to stay: once it is, the '(was: %s)' line has served its purpose and can be deleted.%s"),
				*SourcePrefix(InSourceName, Node->Location), *Node->Id, *Node->WasId,
				Migration.GraphReferences, Migration.PropertyBindings, Migration.AnimationBindings, Migration.ExternalSequenceBindings,
				*Node->WasId, *LocalizationHint));
			if (Migration.ExternalSequenceBindings > 0)
			{
				MessageLog.Warning(*FString::Printf(
					TEXT("%sThe rename \"%s\" -> \"%s\" rewrote %d binding(s) in loaded sequence ASSETS. Those assets are dirty and unsaved -- save them, or the migration exists only in memory."),
					*SourcePrefix(InSourceName, Node->Location), *Node->WasId, *Node->Id, Migration.ExternalSequenceBindings));
			}
		}
		else
		{
			// The ordinary steady state, and deliberately not a warning: an author who migrated last
			// compile and has not deleted the line yet has done nothing wrong. It is still worth one
			// quiet line, because "this clause now does nothing" is the only signal that says the
			// line is safe to remove.
			MessageLog.Note(*FString::Printf(
				TEXT("%s\"%s\" found nothing still named \"%s\", so this rename has already been applied. The '(was: %s)' line can be deleted.%s"),
				*SourcePrefix(InSourceName, Node->Location), *Node->Id, *Node->WasId, *Node->WasId,
				*LocalizationHint));
		}
	}

	{
		// This asset when anything on it moved: its graph, its bindings or its animation paths. A
		// dependent when the migration is what dirtied it -- one that was dirty before keeps that flag
		// through the compile anyway. Sequence assets are not compiled, so they keep theirs (and are
		// warned about above). Without an editor there is no compile end to wait for, and nothing to
		// prompt.
		TArray<UPackage*> ChangedPackages;
		if (bMigratedThisAsset)
		{
			ChangedPackages.Add(DreamBlueprint->GetOutermost());
		}
		for (UPackage* DependentPackage : CleanDependentPackages)
		{
			if (DependentPackage->IsDirty())
			{
				ChangedPackages.AddUnique(DependentPackage);
			}
		}
		UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get();
		if (EditorSubsystem != nullptr && ChangedPackages.Num() > 0)
		{
			EditorSubsystem->MarkPackagesDirtyWhenCompileEnds(ChangedPackages);
		}
	}

	// Sequence assets that reference this Blueprint but are NOT loaded could not take the hop --
	// leg 4 deliberately touches loaded objects only. Name them once, so "the map's sequence broke a
	// week after the rename" becomes "the compile told me which assets to open" instead. A registry
	// that is still scanning stays silent rather than crying wolf.
	{
		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		if (AssetRegistry != nullptr && !AssetRegistry->IsLoadingAssets())
		{
			TArray<FName> Referencers;
			AssetRegistry->GetReferencers(DreamBlueprint->GetOutermost()->GetFName(), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			TArray<FString> UnmigratedSequencePackages;
			for (const FName Referencer : Referencers)
			{
				if (FindPackage(nullptr, *Referencer.ToString()) != nullptr)
				{
					// Loaded: leg 4 already reached it through the object iterator.
					continue;
				}
				TArray<FAssetData> Assets;
				AssetRegistry->GetAssetsByPackageName(Referencer, Assets);
				for (const FAssetData& Asset : Assets)
				{
					if (Asset.AssetClassPath == UDreamUISequence::StaticClass()->GetClassPathName())
					{
						UnmigratedSequencePackages.Add(Referencer.ToString());
						break;
					}
				}
			}
			if (UnmigratedSequencePackages.Num() > 0)
			{
				MessageLog.Warning(*FString::Printf(
					TEXT("%d unloaded sequence asset(s) reference this class and did NOT take the rename hop: %s. Open them and recompile this Blueprint while the '(was:)' line is still in the file."),
					UnmigratedSequencePackages.Num(), *FString::Join(UnmigratedSequencePackages, TEXT(", "))));
			}
		}
	}
}

namespace DreamWidgetAuthoredHierarchy
{
	/** Defined below, beside ResolveAuthoringArchetype; the animation-claim check in the variable pass asks it too. */
	bool IsUnauthoredPlaceholder(const UDreamWidgetTree* InTree);
}

void FDreamWidgetBlueprintCompilerContext::PopulateBlueprintGeneratedVariables()
{
	Super::PopulateBlueprintGeneratedVariables();

	// The hierarchy has to BE the one the text says before anything counts what is in it, and the
	// walk below is the count. See the header for why this and not PreCompile: the list built here is
	// consumed twice later (the skeleton at STAGE VIII, the generated class at STAGE XII) and never
	// rebuilt in between, so a tree installed after this point is a tree the class declares the
	// PREVIOUS compile's variables for.
	//
	// One bag for the whole read, reported in one place afterwards, rather than a MessageLog call at
	// each failure site. The front end reports every mistake in a file rather than stopping at the
	// first -- a .dui is usually written whole, by a model, and five round trips for five typos is
	// what that design avoids -- and a MessageLog call at each site is how somebody eventually puts a
	// `return` next to one and quietly restores the early exit.
	// The member, not a local: FinishCompilingClass adds to this same bag half a compile later and
	// re-deposits it. See FDreamWidgetBlueprintCompilerContext::TextDiagnostics.
	TextDiagnostics.Reset();

	// What the file declares for the class is declared afresh by every compile, as every other generated variable is.
	TextMemberNames.Reset();
	RefusedEventNames.Reset();
	DeclaredPropVariables.Reset();
	DeclaredDispatchers.Reset();
	InheritedPropDefaults.Reset();
	DispatcherSignatureGraphs.Reset();
	HiddenWidgetVariableNames.Reset();
	bTextMembersDeclared = false;
	// And the dispatchers' signature graphs a previous compile made leave the Blueprint before this one makes its own,
	// by the engine's own rule: a signature graph whose name no NewVariables entry carries is one the conform pass
	// throws out at STAGE IX anyway (FBlueprintEditorUtils::ConformDelegateSignatureGraphs). Every dispatcher an author
	// made in the Blueprint editor has its entry, so none of theirs is touched; only a skeleton-only compile, which
	// skips that pass, leaves the previous generation in the array, and this is where it goes.
	Blueprint->DelegateSignatureGraphs.RemoveAll([this](const TObjectPtr<UEdGraph>& InGraph)
	{
		return InGraph == nullptr || FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, InGraph->GetFName()) == INDEX_NONE;
	});

	BuildWidgetTreeFromTextSource(TextDiagnostics);
	ReportTextDiagnostics(TextDiagnostics);
	// The same bag, delivered to editors that are not this one. A clean compile deposits an empty
	// entry on purpose: over there, that is what clears the file's squiggles.
	FDreamUIDiagnosticsMailbox::Deposit(TextDiagnostics);

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint != nullptr)
	{
		TArray<UDreamWidget*> SourceWidgets;
		DreamBlueprint->GetAllSourceWidgets(SourceWidgets);

		// The names this Blueprint already gives to members of its own: the variables the author
		// declared, its functions and its timelines. A generated variable of one of those names is a
		// second member of that name -- nothing in the class layout refuses one -- and the class finds
		// whichever was created first, which is the generated one, so every node the author wrote
		// against their own member read a widget instead. Refused here, by name, before anything is
		// declared.
		TMap<FName, FText> AuthoredMemberKinds;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			AuthoredMemberKinds.Add(Variable.VarName, LOCTEXT("AuthoredVariableKind", "a variable"));
		}
		for (const UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
		{
			if (FunctionGraph != nullptr)
			{
				AuthoredMemberKinds.Add(FunctionGraph->GetFName(), LOCTEXT("AuthoredFunctionKind", "a function"));
			}
		}
		for (const UTimelineTemplate* Timeline : Blueprint->Timelines)
		{
			if (Timeline != nullptr)
			{
				AuthoredMemberKinds.Add(Timeline->GetVariableName(), LOCTEXT("AuthoredTimelineKind", "a timeline"));
			}
		}
		auto IsTakenByAuthoredMember = [this, &AuthoredMemberKinds](const FName InVariableName, const FText& InGeneratedKind)
		{
			const FText* AuthoredKind = AuthoredMemberKinds.Find(InVariableName);
			if (AuthoredKind == nullptr)
			{
				return false;
			}
			MessageLog.Error(*FText::Format(
				LOCTEXT("GeneratedNameTaken", "\"{0}\" is the name of {1} this Blueprint declares, so the {2} of that name gets no variable: two members of one name leave every graph node reading whichever one the class finds first. Rename one of them."),
				FText::FromName(InVariableName), *AuthoredKind, InGeneratedKind).ToString());
			return true;
		};

		if (TextSourceOutcome == ETextSourceOutcome::KeptPrevious)
		{
			// The hierarchy is the last good read's, and so are the widgets in it the author never named.
			HiddenWidgetVariableNames.Append(DreamBlueprint->LastGoodHiddenWidgetNames);

			if (!bTextMembersDeclared)
			{
				// The file did not parse, so it declared nothing this compile -- and the hierarchy kept from the last
				// good read binds that read's props and raises its events: `Text <- Label` is a getter of Label, an
				// emit handler calls its dispatcher. They are declared again with it, as the resources are below and
				// under the same refusals, or every one of those fails with an error pointing away from the file.
				TSet<FName> WidgetNames;
				for (const UDreamWidget* Widget : SourceWidgets)
				{
					WidgetNames.Add(MakeWidgetVariableName(Widget));
				}
				auto IsKeptNameFree = [this, &WidgetNames, &IsTakenByAuthoredMember](const FName InName, const FText& InKind)
				{
					return !InName.IsNone()
						&& !WidgetNames.Contains(InName)
						&& !TextMemberNames.Contains(InName)
						&& !(Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(InName) != nullptr)
						&& !IsTakenByAuthoredMember(InName, InKind);
				};
				TArray<FString> KeptNames;
				for (const FBPVariableDescription& Kept : DreamBlueprint->LastGoodPropVariables)
				{
					if (IsKeptNameFree(Kept.VarName, LOCTEXT("GeneratedPropKind", "prop")))
					{
						DreamBlueprint->GeneratedVariables.Add(Kept);
						TextMemberNames.Add(Kept.VarName);
						KeptNames.Add(Kept.VarName.ToString());
					}
				}
				for (const FDreamWidgetTextDispatcher& Kept : DreamBlueprint->LastGoodDispatchers)
				{
					if (IsKeptNameFree(Kept.Name, LOCTEXT("GeneratedEventKind", "event dispatcher"))
						&& DeclareDispatcher(Kept.Name, Kept.Parameters) != nullptr)
					{
						TextMemberNames.Add(Kept.Name);
						KeptNames.Add(Kept.Name.ToString());
					}
				}
				if (KeptNames.Num() > 0)
				{
					MessageLog.Warning(*FText::Format(
						LOCTEXT("KeptTextMembers", "The .dui did not parse, so this class keeps the props and events its last good read declared ({0}), with the defaults they had then. They follow the file again once the errors above are fixed."),
						FText::FromString(FString::Join(KeptNames, TEXT(", ")))).ToString());
				}
			}
		}

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
			if (TextMemberNames.Contains(VariableName))
			{
				// A prop or an event of the file holds the name. Refused at the declaration when the tree was built
				// from the same file (PropNameTaken, EventNameTaken), so this is only reachable while the file does
				// not build and the tree is an older one -- a compile that is failing already. One member per name
				// is still the rule; the file's declaration is the newer word.
				continue;
			}
			// Two widgets sharing a display name would silently collapse into one variable, and which
			// widget it ends up bound to would depend on tree order. Name it instead of picking.
			// It costs more than a variable, too: an animation binding addresses a widget by its
			// display-name path, so two SIBLINGS under one name make every track through them
			// ambiguous and the first one found wins there as well.
			if (DeclaredNames.Contains(VariableName))
			{
				MessageLog.Warning(*FText::Format(
					LOCTEXT("DuplicateWidgetVariableName", "More than one widget is named \"{0}\"; only the first is exposed as a variable, and an animation bound to that name under the same parent would drive the first one too. Rename one of them."),
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
			if (IsTakenByAuthoredMember(VariableName, LOCTEXT("GeneratedWidgetKind", "widget")))
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
			if (HiddenWidgetVariableNames.Contains(VariableName))
			{
				// A widget the author did not name (`Text { Text = "Status" }`). It still gets its variable: the run
				// time finds the widget of every binding and every route through one, by name. But the name is one the
				// parser made up, which changes when the file's shape does, so nothing the author writes may come to
				// depend on it -- not visible to graphs, which keeps it out of My Blueprint's list as well, and not
				// editable anywhere.
				WidgetVariable.PropertyFlags = (CPF_RepSkip | CPF_Transient | CPF_DuplicateTransient);
			}
			// One category for the whole family, not one named after the asset. Every variable in
			// here was generated from a widget rather than declared by the author, and that is the
			// distinction the panel should draw -- an asset-named category puts a heading above a
			// list whose every member belongs to that asset anyway, and pushes the author's own
			// variables into "Default" beside it. UMG groups bound widgets the same way.
			WidgetVariable.SetMetaData(TEXT("Category"), TEXT("Widget"));

			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(WidgetVariable));
		}

		// One member variable per authored animation, the way UMG exposes UWidgetAnimations: the
		// graph drags the animation in and hands it to Play Animation, instead of addressing it by
		// a display-name string that silently goes stale on rename. The runtime binds the INSTANCED
		// animation to this property by the same shared name rule (see
		// UDreamWidgetGeneratedClass' initialization), because playing the archetype's copy would
		// animate a tree nobody is looking at.
		//
		// Written as a pair of lambdas rather than one loop, because the same declaration has to
		// happen for animations reached three different ways, and every one of them is an animation
		// the graph should be able to drag in: the embedded animations of a component on a widget in
		// the tree, the standalone sequence ASSETS the same component references, and the components
		// of the class's own defaults -- an animation component may sit on the user widget itself,
		// which is not part of the widget tree at all and which SourceWidgets therefore never lists.
		TSet<FName> DeclaredAnimationNames;
		auto DeclareAnimationVariable = [&](UClass* AnimationClass, const FName VariableName, const FString& FriendlyName)
		{
			if (VariableName.IsNone())
			{
				return;
			}
			if (DeclaredNames.Contains(VariableName))
			{
				MessageLog.Warning(*FText::Format(
					LOCTEXT("DuplicateAnimationVariableName", "\"{0}\" already names a widget or another animation; only the first is exposed as a variable. Rename one of them."),
					FText::FromName(VariableName)).ToString());
				return;
			}
			if (TextMemberNames.Contains(VariableName))
			{
				// The file's props and events are declared before anything else, and a second member of one name is a
				// class every graph node reads wrongly.
				MessageLog.Warning(*FText::Format(
					LOCTEXT("AnimationNameIsATextMember", "\"{0}\" is a prop or an event the .dui declares, so the animation of that name gets no variable. Rename one of them."),
					FText::FromName(VariableName)).ToString());
				return;
			}
			DeclaredNames.Add(VariableName);
			// Recorded before the parent-class check below skips the declaration, because a parent that
			// already declares this name is exactly the BindWidgetAnim case: the animation IS here, and
			// the validation further down asks this set whether the promise was kept.
			DeclaredAnimationNames.Add(VariableName);

			if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(VariableName) != nullptr)
			{
				return;
			}
			if (IsTakenByAuthoredMember(VariableName, LOCTEXT("GeneratedAnimationKind", "animation")))
			{
				return;
			}

			FBPVariableDescription AnimationVariable;
			AnimationVariable.VarName = VariableName;
			AnimationVariable.VarGuid = FGuid::NewDeterministicGuid(VariableName.ToString());
			AnimationVariable.VarType = FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, AnimationClass, EPinContainerType::None, false, FEdGraphTerminalType());
			AnimationVariable.FriendlyName = FriendlyName;
			AnimationVariable.PropertyFlags = (CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_RepSkip | CPF_Transient | CPF_DuplicateTransient);
			// Its own family, split from "Widget" the way UMG splits them: what the panel is
			// telling the author is which generated handles animate rather than lay out.
			AnimationVariable.SetMetaData(TEXT("Category"), TEXT("Animations"));

			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(AnimationVariable));
		};
		auto DeclareAnimationVariablesOn = [&](const UDreamWidget* Widget)
		{
			if (!IsValid(Widget))
			{
				return;
			}
			for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
			{
				UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
				if (Animator == nullptr)
				{
					continue;
				}
				for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
				{
					if (IsValid(Animation))
					{
						DeclareAnimationVariable(UDreamWidgetAnimation::StaticClass(),
							UDreamWidgetTree::MakeAnimationVariableName(Animation), Animation->GetDisplayNameString());
					}
				}
				// A standalone sequence asset is addressed by its ASSET name -- the name
				// PlayAnimationByDisplayName falls back to, and the only name an asset has. Renaming the
				// asset changes the variable, exactly as renaming a widget does; that is the same
				// contract the rest of this function is built on rather than a new one.
				for (const TObjectPtr<UDreamUISequence>& Asset : Animator->GetSequenceAssets())
				{
					if (IsValid(Asset))
					{
						DeclareAnimationVariable(UDreamUISequence::StaticClass(),
							FName(*UDreamWidgetTree::SanitizeIdentifier(Asset->GetName())), Asset->GetName());
					}
				}
			}
		};
		for (const UDreamWidget* Widget : SourceWidgets)
		{
			DeclareAnimationVariablesOn(Widget);
		}
		{
			// The class's own defaults: where a component placed on the user widget itself lives at
			// authoring time. The class being compiled if its defaults are up (they are, until the
			// CDO is regenerated much later in the compile), otherwise the parent's, which is where a
			// native base class's components sit.
			const UDreamWidget* ClassDefaultsWidget = nullptr;
			if (Blueprint->GeneratedClass != nullptr)
			{
				ClassDefaultsWidget = Cast<UDreamWidget>(Blueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false));
			}
			if (ClassDefaultsWidget == nullptr && Blueprint->ParentClass != nullptr)
			{
				ClassDefaultsWidget = Cast<UDreamWidget>(Blueprint->ParentClass->GetDefaultObject(/*bCreateIfNeeded*/false));
			}
			DeclareAnimationVariablesOn(ClassDefaultsWidget);
		}

		// meta=(BindDreamWidgetAnim), the animation half of the widget-binding claim that
		// ValidateWidgetBindings checks (and spelled this framework's way for the same reason: see
		// UDreamWidgetGeneratedClass::BindWidgetMetaName). A property a parent class declares with
		// that tag is a CONTRACT: the class's own code plays that animation and expects the subclass
		// to author one under that name. Nothing could check it before -- the declaration loop above
		// simply skips a name the parent already declares, whether or not an animation answers to it
		// -- so the class ran with a null and the author found out at play time. Checked here rather
		// than beside ValidateWidgetBindings because this is where the set of animation names exists:
		// it includes the ones on the class's own defaults, which no widget tree contains.
		//
		// Against the hierarchy this class actually GETS, the rule ResolveAuthoringArchetype states for
		// every other binding check: its own when it authors one, the nearest ancestor's when it does
		// not. Asked of this Blueprint's own tree alone, a subclass that only adds logic -- whose tree is
		// the placeholder every designer open puts there -- was told the animation its parent authored,
		// the one every instance of it is built with, was missing.
		//
		// And not at all when nothing in the chain has authored a hierarchy yet, which is the exemption
		// ValidateWidgetBindings makes for the same reason: a Blueprint is COMPILED the moment it is
		// created, before its author has put anything in it, and reporting every claim the parent class
		// makes as broken at that moment is noise in front of an empty asset. That moment includes the
		// factory's compile, whose root is a placeholder too.
		const UDreamWidgetTree* AnimatedHierarchy = DreamWidgetAuthoredHierarchy::IsUnauthoredPlaceholder(DreamBlueprint->WidgetTree)
			? UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Blueprint->ParentClass)
			: DreamBlueprint->WidgetTree.Get();
		if (Blueprint->ParentClass != nullptr && AnimatedHierarchy != nullptr)
		{
			// The names the declarations above collected -- this class's own tree and its defaults --
			// and, for an inherited hierarchy, the ones every instance of this class is built with.
			TSet<FName> AvailableAnimationNames = DeclaredAnimationNames;
			if (AnimatedHierarchy != DreamBlueprint->WidgetTree.Get())
			{
				AnimatedHierarchy->ForEachWidget([&AvailableAnimationNames](UDreamWidget* Widget)
				{
					for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
					{
						const UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
						if (Animator == nullptr)
						{
							continue;
						}
						for (const UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
						{
							if (IsValid(Animation))
							{
								AvailableAnimationNames.Add(UDreamWidgetTree::MakeAnimationVariableName(Animation));
							}
						}
						for (const TObjectPtr<UDreamUISequence>& Asset : Animator->GetSequenceAssets())
						{
							if (IsValid(Asset))
							{
								AvailableAnimationNames.Add(FName(*UDreamWidgetTree::SanitizeIdentifier(Asset->GetName())));
							}
						}
					}
				});
			}
			for (TFieldIterator<FObjectPropertyBase> PropertyIt(Blueprint->ParentClass, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				const bool bRequired = PropertyIt->HasMetaData(UDreamWidgetGeneratedClass::BindWidgetAnimMetaName);
				if (!bRequired && !PropertyIt->HasMetaData(UDreamWidgetGeneratedClass::BindWidgetAnimOptionalMetaName))
				{
					continue;
				}
				if (PropertyIt->PropertyClass == nullptr || !PropertyIt->PropertyClass->IsChildOf(UMovieSceneSequence::StaticClass()))
				{
					MessageLog.Error(*FText::Format(
						LOCTEXT("BindWidgetAnimWrongType", "\"{0}\" is declared meta=(BindDreamWidgetAnim) but cannot hold an animation; only an animation-typed property can be bound to one."),
						FText::FromName(PropertyIt->GetFName())).ToString());
					continue;
				}
				if (bRequired && !AvailableAnimationNames.Contains(PropertyIt->GetFName()))
				{
					MessageLog.Error(*FText::Format(
						LOCTEXT("BindWidgetAnimMissing", "\"{0}\" is declared meta=(BindDreamWidgetAnim), so this hierarchy must contain an animation named \"{0}\", and it has none. Rename an animation to match, drop the specifier, or mark it BindDreamWidgetAnimOptional."),
						FText::FromName(PropertyIt->GetFName())).ToString());
				}
			}
		}

				// One class variable per `resources` entry, which is what makes the block editable from the
		// Class Defaults panel and readable from the graph. DefaultValue is rebuilt from the file on
		// EVERY compile and the compiler applies it onto the final CDO after the old CDO's values
		// have been copied over -- so the FILE wins each compile, by construction. A panel edit is
		// not lost because the write-back carries it into the file the moment it is committed; a
		// panel edit made with the write-back broken is lost at the next compile, which is the same
		// contract every widget property in the designer already lives under.
		TArray<FBPVariableDescription> DeclaredResourceVariables;
		for (const FDreamUIResource& Entry : TextResources)
		{
			const FName VariableName(*Entry.Name);
			if (VariableName.IsNone() || DeclaredNames.Contains(VariableName) || TextMemberNames.Contains(VariableName))
			{
				// A resource sharing a widget's name would be two variables fighting for one slot.
				// The widget won above; the file's own duplicate-name diagnostics cover the rest. A prop or
				// an event of the name cannot have been declared -- PropNameTaken and EventNameTaken refuse
				// a resource's name -- so this half only matters for a kept, older set of them.
				continue;
			}
			if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(VariableName) != nullptr)
			{
				continue;
			}
			if (IsTakenByAuthoredMember(VariableName, LOCTEXT("GeneratedResourceKind", "resource")))
			{
				continue;
			}

			FEdGraphPinType PinType;
			FString DefaultValue;
			if (Entry.TypeName.Equals(TEXT("Number"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double,
					nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				// The lexer's number text is ImportText's number text; no reformatting to drift on.
				DefaultValue = Entry.Value.Raw;
			}
			else if (Entry.TypeName.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, NAME_None,
					TBaseStructure<FLinearColor>::Get(), EPinContainerType::None, false, FEdGraphTerminalType());
				FLinearColor Color = FLinearColor::White;
				DreamUIValueFormat::ParseColorHex(Entry.Value.Raw, Color);
				TBaseStructure<FLinearColor>::Get()->ExportText(DefaultValue, &Color, nullptr, nullptr, PPF_None, nullptr);
			}
			else if (Entry.TypeName.Equals(TEXT("Vector2"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, NAME_None,
					TBaseStructure<FVector2D>::Get(), EPinContainerType::None, false, FEdGraphTerminalType());
				FVector2D Vector = FVector2D::ZeroVector;
				if (Entry.Value.Elements.Num() == 2)
				{
					LexTryParseString(Vector.X, *Entry.Value.Elements[0]);
					LexTryParseString(Vector.Y, *Entry.Value.Elements[1]);
				}
				TBaseStructure<FVector2D>::Get()->ExportText(DefaultValue, &Vector, nullptr, nullptr, PPF_None, nullptr);
			}
			else if (Entry.TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_String, NAME_None,
					nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				DefaultValue = Entry.Value.Raw;
			}
			else if (Entry.TypeName.Equals(TEXT("Asset"), ESearchCase::IgnoreCase))
			{
				// A soft OBJECT pin, not a path struct: the Class Defaults panel then offers the
				// asset picker, which is the whole reason an author would edit a resource there.
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_SoftObject, NAME_None,
					UObject::StaticClass(), EPinContainerType::None, false, FEdGraphTerminalType());
				DefaultValue = Entry.Value.Raw;
			}
			else
			{
				// The builder already refused the entry (DUI4008); declaring a variable for it anyway
				// would put an untyped slot on the class for a value nothing can fill.
				continue;
			}
			DeclaredNames.Add(VariableName);

			FBPVariableDescription ResourceVariable;
			ResourceVariable.VarName = VariableName;
			ResourceVariable.VarGuid = FGuid::NewDeterministicGuid(VariableName.ToString());
			ResourceVariable.VarType = MoveTemp(PinType);
			ResourceVariable.FriendlyName = Entry.Name;
			// Editable on the CLASS, read-only in graphs and on instances: the block is a table of
			// constants, and an instance override would be a value the file cannot see and the next
			// compile cannot preserve.
			ResourceVariable.PropertyFlags =
				(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_DisableEditOnInstance);
			ResourceVariable.Category = FText::FromString(TEXT("Resources"));
			ResourceVariable.DefaultValue = MoveTemp(DefaultValue);
			DeclaredResourceVariables.Add(ResourceVariable);
			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(ResourceVariable));
		}

		if (TextSourceOutcome == ETextSourceOutcome::KeptPrevious)
		{
			// The file did not build into a hierarchy, so the previous hierarchy stays -- and its resource
			// variables stay with it. The compiler emptied every generated variable before this one began
			// declaring, so without this every `resources` entry left the class, and every graph node
			// reading one failed with an error pointing away from the one mistake in the file. Declared
			// under the same refusals as a fresh entry: a name a widget, the parent or the author's own
			// member has taken since is not taken back.
			TArray<FString> KeptNames;
			for (const FBPVariableDescription& Kept : DreamBlueprint->LastGoodResourceVariables)
			{
				if (Kept.VarName.IsNone() || DeclaredNames.Contains(Kept.VarName) || TextMemberNames.Contains(Kept.VarName))
				{
					continue;
				}
				if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(Kept.VarName) != nullptr)
				{
					continue;
				}
				if (IsTakenByAuthoredMember(Kept.VarName, LOCTEXT("GeneratedResourceKind", "resource")))
				{
					continue;
				}
				DeclaredNames.Add(Kept.VarName);
				DreamBlueprint->GeneratedVariables.Add(Kept);
				KeptNames.Add(Kept.VarName.ToString());
			}
			if (KeptNames.Num() > 0)
			{
				// Said, so a value edited in the file meanwhile is not mistaken for one the class has taken.
				MessageLog.Warning(*FText::Format(
					LOCTEXT("KeptResourceVariables", "The .dui did not build, so this class keeps the resource variables its last good read declared ({0}), with the values they had then. They follow the file again once the errors above are fixed."),
					FText::FromString(FString::Join(KeptNames, TEXT(", ")))).ToString());
			}
		}
		else if (TextSourceOutcome == ETextSourceOutcome::Built)
		{
			// What the next compile falls back on, should the file stop building.
			DreamBlueprint->LastGoodResourceVariables = MoveTemp(DeclaredResourceVariables);
			DreamBlueprint->LastGoodPropVariables = DeclaredPropVariables;
			DreamBlueprint->LastGoodDispatchers = DeclaredDispatchers;
			DreamBlueprint->LastGoodHiddenWidgetNames = HiddenWidgetVariableNames.Array();
		}
		else
		{
			// The class names no .dui any more, so there is no hierarchy of the file's to keep members for.
			if (DreamBlueprint->LastGoodResourceVariables.Num() > 0)
			{
				DreamBlueprint->LastGoodResourceVariables.Reset();
			}
			if (DreamBlueprint->LastGoodPropVariables.Num() > 0 || DreamBlueprint->LastGoodDispatchers.Num() > 0
				|| DreamBlueprint->LastGoodHiddenWidgetNames.Num() > 0)
			{
				DreamBlueprint->LastGoodPropVariables.Reset();
				DreamBlueprint->LastGoodDispatchers.Reset();
				DreamBlueprint->LastGoodHiddenWidgetNames.Reset();
			}
		}
	}
}

namespace DreamWidgetAuthoredHierarchy
{
	/**
	 * Whether a tree is the placeholder the editor puts in every asset, rather than an authored one.
	 *
	 * UDreamWidgetBlueprint::GetOrCreateWidgetTree(bEnsureRootWidget = true) runs when an asset is
	 * created AND every time a designer opens one, and it makes a tree holding one bare widget named
	 * "Root". Nothing about that says the author declared a hierarchy -- it exists so a fresh asset
	 * has something to drop onto.
	 *
	 * Anything at all on the root makes it authored: a child, a visual, a layout, a behaviour. The
	 * root's own name and geometry are deliberately NOT part of the test, because opening a designer
	 * is allowed to rename or place the placeholder without that meaning the author declared one.
	 */
	bool IsUnauthoredPlaceholder(const UDreamWidgetTree* InTree)
	{
		if (!IsValid(InTree))
		{
			return true;
		}
		const UDreamWidget* Root = InTree->RootWidget.Get();
		if (!IsValid(Root))
		{
			return true;
		}
		return Root->GetChildrenCount() == 0
			&& Root->GetVisual() == nullptr
			&& Root->GetLayoutContainer() == nullptr
			&& Root->GetLayoutSelf() == nullptr
			&& Root->GetAllComponents().Num() == 0;
	}

	/**
	 * The hierarchy this class actually gets -- its own when it declares one, its parent's when it
	 * does not.
	 *
	 * One function, because the compiler used to answer this question in two places and disagree.
	 * The binding stages asked only "does this Blueprint have a WidgetTree object", which after
	 * GetOrCreateWidgetTree is always yes; ValidateWidgetBindings walked the parent chain. So a
	 * subclass that only adds logic had its bindings validated against the parent's widgets and
	 * compiled against an empty placeholder, which is the rule UDreamWidgetGeneratedClass::
	 * FindWidgetTreeArchetype states at runtime.
	 */
	UDreamWidgetTree* ResolveAuthoringArchetype(const UDreamWidgetBlueprint* InBlueprint, const UClass* InClass)
	{
		if (InBlueprint != nullptr && !IsUnauthoredPlaceholder(InBlueprint->WidgetTree))
		{
			return InBlueprint->WidgetTree;
		}
		return InClass != nullptr
			? UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass->GetSuperClass())
			: nullptr;
	}
}

void FDreamWidgetBlueprintCompilerContext::UpdateGeneratedClassWidgetTree(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetGeneratedClass* InClass)
{
	// The canvas the author laid this hierarchy out on. Editor-only data that a world-space host needs
	// at runtime -- it has no viewport to stretch to, so the authored size is how big the hierarchy
	// lands in the world -- which is why the class carries a copy of it.
	//
	// Written first, before either return below: a subclass that authored no hierarchy still opened a
	// designer at some canvas, and both the empty-tree case and the inherit-the-parent's-tree case are
	// hierarchies someone sized on purpose.
	InClass->SetDesignSize(InBlueprint->DesignerData.CanvasSize);

	if (!IsValid(InBlueprint->WidgetTree))
	{
		return;
	}

	// A subclass that authored NOTHING must not shadow its parent's hierarchy.
	//
	// The runtime already decided what such a subclass means: FindWidgetTreeArchetype walks up until
	// it finds a class that declares a tree, and DreamGUI.UserWidget.ASubclassWithNoTemplateInstancesItsParents
	// pins it. The editor made that state unreachable -- the placeholder tree above is created on
	// asset creation AND on every designer open -- so every subclass owned a tree holding one bare
	// "Root", this function made it the class's archetype, and the subclass instantiated as an empty
	// screen with every binding the PARENT declared silently failing to resolve against it.
	//
	// Declare nothing and inherit; put anything in the tree and it overrides, whole. That is the one
	// rule, and it is now the same rule in the editor and at runtime.
	if (DreamWidgetAuthoredHierarchy::IsUnauthoredPlaceholder(InBlueprint->WidgetTree)
		&& UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass->GetSuperClass()) != nullptr)
	{
		InClass->SetWidgetTreeArchetype(nullptr);
		OldWidgetTree = nullptr;
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

namespace DreamWidgetBindingDiagnostics
{
	/**
	 * The .dui position a binding recorded when the builder made it, or nothing when it came from
	 * somewhere with no text behind it (the designer's Bind button, a hand-built struct).
	 *
	 * A template over the two binding structs rather than a shared base: they are USTRUCTs whose
	 * layouts are serialized into assets, and inventing a base to share two ints would change both
	 * on disk to save four lines here.
	 */
	template <typename BindingType>
	FDreamUISourceLocation AuthoredLocation(const BindingType& InBinding)
	{
#if WITH_EDITORONLY_DATA
		return FDreamUISourceLocation(InBinding.SourceLine, InBinding.SourceColumn);
#else
		return FDreamUISourceLocation();
#endif
	}
}

void FDreamWidgetBlueprintCompilerContext::CompilePropertyBindings(UClass* InClass)
{
	using DreamWidgetBindingDiagnostics::AuthoredLocation;

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(InClass);
	if (DreamBlueprint == nullptr || GeneratedClass == nullptr)
	{
		return;
	}
	// Skeleton-only compiles do not carry data onto the class; see ValidateWidgetBindings.
	if (CompileOptions.CompileType == EKismetCompileType::SkeletonOnly)
	{
		return;
	}

	// Counted so the mailbox is only rewritten when this stage actually had something to say. Every
	// diagnostic raised below is one the earlier deposit could not have carried: the class that
	// declares these functions did not exist when the file was read.
	const int32 TextDiagnosticsBefore = TextDiagnostics.Diagnostics.Num();

	// The hierarchy this class will actually get, parent chain included -- the same answer
	// ValidateWidgetBindings gives. Asking only about this Blueprint's own WidgetTree reported every
	// binding a logic-only subclass inherited as "this hierarchy has none", against a placeholder
	// tree that is not a hierarchy at all.
	UDreamWidgetTree* Archetype = DreamWidgetAuthoredHierarchy::ResolveAuthoringArchetype(DreamBlueprint, InClass);
	TArray<FDreamWidgetPropertyBinding> Resolved;
	for (const FDreamWidgetPropertyBinding& Authored : DreamBlueprint->PropertyBindings)
	{
		const UDreamWidget* TargetWidget = Archetype != nullptr
			? Archetype->FindWidgetByVariableName(Authored.WidgetName) : nullptr;
		if (TargetWidget == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingWidgetNotFound", "The binding on \"{0}\" expects a widget named \"{1}\", and this hierarchy has none."),
				FText::FromName(Authored.PropertyName), FText::FromName(Authored.WidgetName)).ToString());
			continue;
		}
		// The same resolver the runtime uses: the compiler must check the object the runtime will
		// actually write to, or a binding passes here and finds nothing there.
		const UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Authored.Target, Authored.BehaviourIndex);
		if (Target == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingTargetNotFound", "The binding on \"{0}.{1}\" points at something \"{0}\" does not have."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName)).ToString());
			continue;
		}
		const FProperty* TargetProperty = Target->GetClass()->FindPropertyByName(Authored.PropertyName);
		if (TargetProperty == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingPropertyNotFound", "\"{0}\" has no property named \"{1}\" to bind."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName)).ToString());
			continue;
		}
		UFunction* Setter = FindDreamWidgetSetterFor(Target->GetClass(), TargetProperty);
		// A user widget's variable with no setter -- a component's `props` entry -- is the one property the runtime
		// writes directly (see FDreamWidgetPropertyBinding::SetterName); the builder let only that shape through.
		const bool bWritesDirectly = Setter == nullptr && Authored.NotifyField.IsNone()
			&& Authored.Target == EDreamWidgetBindingTarget::Widget && Target->IsA(UDreamUserWidget::StaticClass());
		if (Setter == nullptr && !bWritesDirectly)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingNoSetter", "\"{0}\" cannot be bound: {1} exposes no setter for it, so a bound value would be written but never take effect."),
				FText::FromName(Authored.PropertyName), FText::FromString(Target->GetClass()->GetName())).ToString());
			continue;
		}
		UFunction* SourceFunction = InClass->FindFunctionByName(Authored.FunctionName);
		if (SourceFunction == nullptr)
		{
			// Into the bag as well as the log, and this is the whole of DUI5004's compile-stage half:
			// the builder writes a binding's function name down without checking it, because the
			// class that would declare it is the one this compile is building. Reported only here
			// meant the mailbox said the file was clean while the compile that produced it failed.
			// The line comes off the binding: this stage has no AST, so the position the builder
			// copied onto the record is the only one there will ever be.
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingFunctionNotFound", "The binding on \"{0}.{1}\" calls \"{2}\", and this Blueprint has no such function."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName),
				FText::FromName(Authored.FunctionName)).ToString());
			TextDiagnostics.AddError(EDreamUIDiagnosticCode::BindingFunctionNotFound, AuthoredLocation(Authored),
				FString::Printf(TEXT("the binding on '%s.%s' calls '%s', and this Blueprint has no such function"),
					*Authored.WidgetName.ToString(), *Authored.PropertyName.ToString(), *Authored.FunctionName.ToString()));
			continue;
		}
		const FProperty* ReturnProperty = SourceFunction->GetReturnProperty();
		// CanDreamWidgetBoundValueConvert, not SameType: two plain numeric types bind whatever their
		// width. The strict comparison refused a project's own `double`, `int64` or `uint8` property
		// outright -- and since `<-` lowers into a thunk whose real return is narrowed to float (K2
		// computes reals in double, every bindable real here is a float), that was most of them. The
		// runtime converts through the same pair of functions, so nothing accepted here is copied by
		// a memcpy that would read the wrong width.
		if (SourceFunction->NumParms != 1 || ReturnProperty == nullptr
			|| !CanDreamWidgetBoundValueConvert(ReturnProperty, TargetProperty))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingFunctionWrongShape", "\"{0}\" has to take no arguments and return the type of \"{1}.{2}\" to bind to it."),
				FText::FromName(Authored.FunctionName), FText::FromName(Authored.WidgetName),
				FText::FromName(Authored.PropertyName)).ToString());
			// The same code as the missing one, because 5004 covers both by design ("a function the
			// class does not declare, OR that takes parameters") and the reader's move is identical.
			TextDiagnostics.AddError(EDreamUIDiagnosticCode::BindingFunctionNotFound, AuthoredLocation(Authored),
				FString::Printf(TEXT("'%s' has to take no arguments and return the type of '%s.%s' to bind to it"),
					*Authored.FunctionName.ToString(), *Authored.WidgetName.ToString(), *Authored.PropertyName.ToString()));
			continue;
		}

		FDreamWidgetPropertyBinding& Entry = Resolved.AddDefaulted_GetRef();
		Entry.WidgetName = Authored.WidgetName;
		Entry.Target = Authored.Target;
		Entry.BehaviourIndex = Authored.BehaviourIndex;
		Entry.PropertyName = Authored.PropertyName;
		Entry.FunctionName = Authored.FunctionName;
		Entry.SetterName = Setter != nullptr ? Setter->GetFName() : NAME_None;
		Entry.NotifyField = Authored.NotifyField;
		if (!Authored.NotifyField.IsNone() && Setter != nullptr)
		{
			// The forward half of a `<->` pushes through the silent setter when there is one: the
			// full setter fires the control's changed event, which is the reverse route, which
			// writes the variable this push just read -- the echo dies here, not in a hope that
			// every control early-outs on an equal value.
			const FName SilentSetterName(*(Setter->GetFName().ToString() + TEXT("WithoutNotify")));
			if (Target->GetClass()->FindFunctionByName(SilentSetterName) != nullptr)
			{
				Entry.SetterName = SilentSetterName;
			}
		}
	}
	GeneratedClass->SetPropertyBindings(MoveTemp(Resolved));

	// The event half, with the check only this stage can make: the handler is a function on the
	// class being compiled, and the builder could not see that class. The signature test is the
	// delegate's own -- a handler that takes what the event sends, no more.
	TArray<FDreamWidgetEventBinding> ResolvedEvents;
	for (const FDreamWidgetEventBinding& Authored : DreamBlueprint->EventBindings)
	{
		const UDreamWidget* TargetWidget = Archetype != nullptr
			? Archetype->FindWidgetByVariableName(Authored.WidgetName) : nullptr;
		if (TargetWidget == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventWidgetNotFound", "The event route on \"{0}\" expects a widget named \"{1}\", and this hierarchy has none."),
				FText::FromName(Authored.EventName), FText::FromName(Authored.WidgetName)).ToString());
			continue;
		}
		const UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Authored.Target, Authored.BehaviourIndex);
		const FProperty* EventProperty = Target != nullptr ? Target->GetClass()->FindPropertyByName(Authored.EventName) : nullptr;
		// The plugin has two kinds of event property and a route may name either. `Controls/*` declares
		// BlueprintAssignable multicast delegates; the older `Interaction/*` behaviours declare
		// FDreamUIEventDelegate structs, and a route onto one of those used to be rejected here -- so
		// that whole family had no canonical way to be handled and the per-instance legacy list was
		// the only option. No new syntax: `OnClick -> Confirm` always parsed, it just never compiled.
		const FStructProperty* StructEvent = CastField<FStructProperty>(EventProperty);
		if (StructEvent != nullptr && StructEvent->Struct != FDreamUIEventDelegate::StaticStruct())
		{
			StructEvent = nullptr;
		}
		const FMulticastDelegateProperty* Event = CastField<FMulticastDelegateProperty>(EventProperty);
		if (Event == nullptr && StructEvent == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventNotOnTarget", "\"{0}\" has no event named \"{1}\" to route."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.EventName)).ToString());
			continue;
		}
		const UFunction* Handler = InClass->FindFunctionByName(Authored.FunctionName);
		if (Handler == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventHandlerNotFound", "\"{0}.{1}\" routes to \"{2}\", and this Blueprint has no such function."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.EventName),
				FText::FromName(Authored.FunctionName)).ToString());
			TextDiagnostics.AddError(EDreamUIDiagnosticCode::EventHandlerNotFound, AuthoredLocation(Authored),
				FString::Printf(TEXT("'%s.%s' routes to '%s', and this Blueprint has no such function"),
					*Authored.WidgetName.ToString(), *Authored.EventName.ToString(), *Authored.FunctionName.ToString()));
			continue;
		}
		if (StructEvent != nullptr)
		{
			// The struct's own shape test: an FDreamUIEventDelegate declares the single value it fires
			// with, and IsStillSupported is the same question the runtime asks before it calls.
			const FDreamUIEventDelegate* EventValue = StructEvent->ContainerPtrToValuePtr<FDreamUIEventDelegate>(Target);
			const EDreamUIEventDelegateParameterType NativeParameterType = EventValue != nullptr
				? EventValue->GetNativeParameterType() : EDreamUIEventDelegateParameterType::None;
			if (!UDreamUIEventDelegateParameterHelper::IsStillSupported(const_cast<UFunction*>(Handler), NativeParameterType))
			{
				MessageLog.Error(*FText::Format(
					LOCTEXT("EventHandlerWrongShape", "\"{0}\" cannot handle \"{1}.{2}\": its parameters do not match the event's."),
					FText::FromName(Authored.FunctionName), FText::FromName(Authored.WidgetName),
					FText::FromName(Authored.EventName)).ToString());
				TextDiagnostics.AddError(EDreamUIDiagnosticCode::EventHandlerSignatureMismatch, AuthoredLocation(Authored),
					FString::Printf(TEXT("'%s' cannot handle '%s.%s': its parameters do not match the event's"),
						*Authored.FunctionName.ToString(), *Authored.WidgetName.ToString(), *Authored.EventName.ToString()));
				continue;
			}
			ResolvedEvents.Add(Authored);
			continue;
		}
		if (Event->SignatureFunction != nullptr && !Handler->IsSignatureCompatibleWith(Event->SignatureFunction))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventHandlerWrongShape", "\"{0}\" cannot handle \"{1}.{2}\": its parameters do not match the event's."),
				FText::FromName(Authored.FunctionName), FText::FromName(Authored.WidgetName),
				FText::FromName(Authored.EventName)).ToString());
			TextDiagnostics.AddError(EDreamUIDiagnosticCode::EventHandlerSignatureMismatch, AuthoredLocation(Authored),
				FString::Printf(TEXT("'%s' cannot handle '%s.%s': its parameters do not match the event's"),
					*Authored.FunctionName.ToString(), *Authored.WidgetName.ToString(), *Authored.EventName.ToString()));
			continue;
		}
		ResolvedEvents.Add(Authored);
	}
	GeneratedClass->SetEventBindings(MoveTemp(ResolvedEvents));

	// The `each` half, with the one check only this stage can make: the SOURCE lives on the class
	// being compiled -- a nullary function returning TArray of objects, or such an array variable.
	// The per-cell setters were vetted by the builder against the template's real classes, and the
	// host's view is a runtime fact the resolve checks again.
	//
	// Into the bag as well as the log, on the same grounds as 5004 and 6004 above and after the same
	// omission: these three were reported to the MessageLog alone, so a .dui whose `each` named a
	// function nobody had written yet failed its compile while the mailbox -- which is the only
	// channel VSCode reads -- went on saying the file was clean.
	TArray<FDreamWidgetEachBinding> ResolvedEach;
	for (const FDreamWidgetEachBinding& Authored : DreamBlueprint->EachBindings)
	{
		// The keyword the author wrote: a `for` is recorded as the same binding, run in the panel itself, and a
		// message calling it an `each` sends the reader looking for a block the file does not have.
		const TCHAR* Keyword = Authored.bInPanel ? TEXT("for") : TEXT("each");
		const FArrayProperty* ItemsProperty = nullptr;
		if (Authored.bSourceIsFunction)
		{
			const UFunction* Source = InClass->FindFunctionByName(Authored.SourceName);
			if (Source == nullptr || Source->NumParms != 1)
			{
				MessageLog.Error(*FString::Printf(
					TEXT("The '%s %s in %s()' block needs a no-argument function of that name on this Blueprint."),
					Keyword, *Authored.LoopVariable.ToString(), *Authored.SourceName.ToString()));
				// NumParms == 1 is "the return value and nothing else". A function that is there but
				// the wrong shape is as unusable as one that is missing, and the fix is the same act
				// -- so both are 6006, and the message carries which of the two it was. Not spelled
				// "takes arguments": NumParms == 0 is a function that returns nothing and takes
				// nothing, which fails here too and takes no arguments at all.
				TextDiagnostics.AddError(EDreamUIDiagnosticCode::EachSourceNotFound, AuthoredLocation(Authored),
					FString::Printf(TEXT("the '%s %s in %s()' block needs a no-argument function of that name, %s"),
						Keyword, *Authored.LoopVariable.ToString(), *Authored.SourceName.ToString(),
						Source == nullptr
							? TEXT("and this Blueprint declares none")
							: TEXT("and this one does not take zero arguments and return a value")));
				continue;
			}
			ItemsProperty = CastField<FArrayProperty>(Source->GetReturnProperty());
		}
		else
		{
			ItemsProperty = FindFProperty<FArrayProperty>(InClass, Authored.SourceName);
			if (ItemsProperty == nullptr)
			{
				MessageLog.Error(*FString::Printf(
					TEXT("The '%s %s in %s' block needs an array variable of that name on this Blueprint."),
					Keyword, *Authored.LoopVariable.ToString(), *Authored.SourceName.ToString()));
				// FindFProperty<FArrayProperty> answers null for "no such variable" AND for "that
				// variable is not an array", which the reader cannot tell apart from the code alone
				// -- so the message covers both and 6007 is kept for the case where an array WAS
				// found and its elements are wrong.
				TextDiagnostics.AddError(EDreamUIDiagnosticCode::EachSourceNotFound, AuthoredLocation(Authored),
					FString::Printf(TEXT("the '%s %s in %s' block needs an array variable of that name on this Blueprint"),
						Keyword, *Authored.LoopVariable.ToString(), *Authored.SourceName.ToString()));
				continue;
			}
		}
		if (ItemsProperty == nullptr || CastField<FObjectPropertyBase>(ItemsProperty->Inner) == nullptr)
		{
			MessageLog.Error(*FString::Printf(
				TEXT("'%s' must supply an array of OBJECTS -- the item bindings read members off each element by reflection."),
				*Authored.SourceName.ToString()));
			TextDiagnostics.AddError(EDreamUIDiagnosticCode::EachSourceNotObjectArray, AuthoredLocation(Authored),
				FString::Printf(TEXT("'%s' must supply an array of OBJECTS -- the item bindings read members off each element by reflection"),
					*Authored.SourceName.ToString()));
			continue;
		}
		ResolvedEach.Add(Authored);
	}
	GeneratedClass->SetEachBindings(MoveTemp(ResolvedEach));

	if (TextDiagnostics.Diagnostics.Num() != TextDiagnosticsBefore)
	{
		// Re-deposited with the WHOLE bag, not just what this stage added: the mailbox keys an entry
		// by file and replaces it outright, so a second deposit of only these would erase the parse's
		// and the builder's. A no-op for a Blueprint that names no .dui -- that bag has no source
		// name, and Deposit takes that as "none of the mailbox's business".
		FDreamUIDiagnosticsMailbox::Deposit(TextDiagnostics);
	}
}

void FDreamWidgetBlueprintCompilerContext::ValidateWidgetBindings(UClass* InClass)
{
	// The AUTHORING tree, not the class's copy of it. They are the same thing on a full compile -- the
	// copy was made a few lines ago -- but a skeleton-only compile deliberately does not make one, and
	// reading the class there would report every binding as broken on every keystroke in the designer.
	// Nothing authored here: a subclass that only adds logic inherits its parent's hierarchy, and its
	// bindings have to be checked against that. Same function CompilePropertyBindings uses, because
	// the two answering differently is what this pair of stages got wrong.
	UDreamWidgetTree* Archetype = DreamWidgetAuthoredHierarchy::ResolveAuthoringArchetype(DreamWidgetBlueprint(), InClass);
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
	// Only properties that SAY they are bindings, via meta=(BindDreamWidget). Raising an error means
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
		if (!Property->HasMetaData(UDreamWidgetGeneratedClass::BindWidgetMetaName))
		{
			continue;
		}

		const UDreamWidget* Match = Archetype->FindWidgetByVariableName(Property->GetFName());
		if (Match == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingNotFound", "\"{0}\" is declared meta=(BindDreamWidget), so this hierarchy must contain a widget of that name, and it has none. Rename a widget to match, or drop the specifier."),
				FText::FromName(Property->GetFName())).ToString());
		}
		else if (!Match->IsA(Property->PropertyClass))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingWrongType", "\"{0}\" is declared meta=(BindDreamWidget) as {1}, but the widget of that name is {2}."),
				FText::FromName(Property->GetFName()),
				FText::FromString(Property->PropertyClass->GetName()),
				FText::FromString(Match->GetClass()->GetName())).ToString());
		}
	}

	ValidateNamedSlotBindings(Archetype);
	ValidateAnimationBindings(Archetype);
}

void FDreamWidgetBlueprintCompilerContext::ValidateNamedSlotBindings(UDreamWidgetTree* InArchetype)
{
	if (!IsValid(InArchetype))
	{
		return;
	}
	// Every nested widget blueprint instance in this hierarchy, checked against the slots its own
	// class declares. A slot the class removed or renamed leaves the host still holding content for a
	// name nobody answers, and the runtime's only options are to drop it or to guess -- so it is
	// reported here, on the asset that can fix it, with both names in the message.
	InArchetype->ForEachWidget([this](UDreamWidget* Widget)
	{
		UDreamUserWidget* Nested = Cast<UDreamUserWidget>(Widget);
		if (Nested == nullptr || Nested->NamedSlotContent.Num() == 0)
		{
			return;
		}
		// The class overload: it asks the archetype exactly as this line used to, and then asks a
		// native control for the slots it declares in code. Without that half, every slot bound on a
		// placed native control compiled to an error saying the class declares no such slot.
		TArray<FName> Declared;
		UDreamUserWidget::CollectDeclaredSlotNames(Nested->GetClass(), Declared);
		for (const TPair<FName, TObjectPtr<UDreamWidget>>& Binding : Nested->NamedSlotContent)
		{
			if (!Declared.Contains(Binding.Key))
			{
				MessageLog.Error(*FText::Format(
					LOCTEXT("NamedSlotNotDeclared", "\"{0}\" has content bound to a slot named \"{1}\", and {2} declares no such slot."),
					FText::FromString(Nested->GetDisplayName()),
					FText::FromName(Binding.Key),
					FText::FromString(Nested->GetClass()->GetName())).ToString());
			}
		}
	});
}

void FDreamWidgetBlueprintCompilerContext::ValidateAnimationBindings(UDreamWidgetTree* InArchetype)
{
	if (!IsValid(InArchetype))
	{
		return;
	}
	// Embedded animations only -- the ones living in a sequence component on a widget of THIS
	// hierarchy, whose paths were recorded against that widget. UDreamUISequence assets on the same
	// component are deliberately left alone: they are authored against their own PreviewWidgetClass
	// and being reusable across widget classes is the point of them, so a path that this hierarchy
	// cannot walk says nothing about the asset.
	InArchetype->ForEachWidget([this](UDreamWidget* ContextWidget)
	{
		for (UDreamUIBehaviour* Component : ContextWidget->GetAllComponents())
		{
			UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
			if (Animator == nullptr)
			{
				continue;
			}
			for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
			{
				if (!IsValid(Animation))
				{
					continue;
				}
				TArray<TPair<FGuid, FString>> Unresolvable;
				// The context is the widget the component hangs off, matching the context
				// BindPossessableObject recorded against and the one playback resolves through.
				Animation->GetUnresolvableBindingPaths(ContextWidget, Unresolvable);
				UMovieScene* MovieScene = Animation->GetMovieScene();
				for (const TPair<FGuid, FString>& Broken : Unresolvable)
				{
					// The possessable's name is what the author sees as a track label. Falling back
					// to the raw guid is not much, but a message that names neither the track nor a
					// path would leave somebody diffing bindings by hand.
					FMovieScenePossessable* Possessable = MovieScene != nullptr ? MovieScene->FindPossessable(Broken.Key) : nullptr;
					const FString TrackName = Possessable != nullptr ? Possessable->GetName() : Broken.Key.ToString(EGuidFormats::DigitsWithHyphens);

					if (Broken.Value.IsEmpty())
					{
						MessageLog.Error(*FText::Format(
							LOCTEXT("AnimationBindingHasNoPath", "Animation \"{0}\" binds \"{1}\" without recording a widget path, so nothing but the authoring hierarchy will ever resolve it. Rebind that track against \"{2}\"."),
							FText::FromString(Animation->GetDisplayNameString()),
							FText::FromString(TrackName),
							FText::FromString(ContextWidget->GetDisplayName())).ToString());
					}
					else
					{
						MessageLog.Error(*FText::Format(
							LOCTEXT("AnimationBindingPathNotFound", "Animation \"{0}\" drives \"{1}\" through the path \"{2}\", and \"{3}\" has nothing there. A widget's display name is also its animation path, so renaming one leaves every track bound to it naming a widget that no longer exists. Rename the widget back, or rebind the track."),
							FText::FromString(Animation->GetDisplayNameString()),
							FText::FromString(TrackName),
							FText::FromString(Broken.Value),
							FText::FromString(ContextWidget->GetDisplayName())).ToString());
					}
				}
			}
		}
	});
}

void FDreamWidgetBlueprintCompilerContext::FinishCompilingClass(UClass* Class)
{
	// A skeleton compile exists to give the graph its members back as fast as possible, and the
	// designer triggers one on every structural edit -- every drag, every delete. Duplicating the
	// whole hierarchy onto a class nobody instantiates would put that cost on each of them, and would
	// leave an archetype on the skeleton class that only invites something to read the wrong one.
	// UMG skips the same work for the same reason.
	const bool bIsSkeletonOnly = CompileOptions.CompileType == EKismetCompileType::SkeletonOnly;
	if (!bIsSkeletonOnly)
	{
		if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
		{
			if (UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(Class))
			{
				UpdateGeneratedClassWidgetTree(DreamBlueprint, DreamClass);
			}
		}
	}

	Super::FinishCompilingClass(Class);

	// After the base pass, so the properties being checked against actually exist on the class.
	ValidateWidgetBindings(Class);
	CompilePropertyBindings(Class);
}

#undef LOCTEXT_NAMESPACE
