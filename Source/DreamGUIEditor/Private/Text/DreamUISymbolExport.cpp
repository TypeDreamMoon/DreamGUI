// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISymbolExport.h"

#include "DreamGUIEditorModule.h"
#include "Core/DreamUIBehaviour.h"
// FindFieldId: whether a view model member announces itself.
#include "Core/DreamUIBindingObserver.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"
// DescribeEvents lists FDreamUIEventDelegate properties as well as multicast delegates.
#include "Event/DreamUIEventDelegate.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUIReflectionPolicy.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUIValueFormat.h"
#include "ViewModel/DreamViewModel.h"

// UWidget, to leave UMG's widgets out of the view models: they announce their fields too, and are never one.
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "INotifyFieldValueChanged.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace DreamUISymbolExportLocal
{
	FDelegateHandle GStartupHandle;

	FAutoConsoleCommand GExportCommand(
		TEXT("DreamUI.ExportSymbols"),
		TEXT("Rewrites DUI/.dui-symbols.json, the completion data the VSCode extension reads."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			const FString Written = FDreamUISymbolExport::ExportNow();
			UE_LOG(DreamGUIEditor, Display, TEXT("DreamUI.ExportSymbols: %s"),
				Written.IsEmpty() ? TEXT("no project DUI/ directory, nothing written") : *Written);
		}));

	/** The leaf FProperty a dotted path names on a class, or null. Mirrors the builder's walk. */
	const FProperty* ResolveLeaf(const UStruct* InScope, const FString& InPath)
	{
		TArray<FString> Segments;
		InPath.ParseIntoArray(Segments, TEXT("."));
		const FProperty* Property = nullptr;
		const UStruct* Scope = InScope;
		for (const FString& Segment : Segments)
		{
			Property = Scope != nullptr ? FindFProperty<FProperty>(Scope, *Segment) : nullptr;
			if (Property == nullptr)
			{
				return nullptr;
			}
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			Scope = AsStruct != nullptr ? AsStruct->Struct : nullptr;
		}
		return Property;
	}

	/** UEnum for a property, matching the builder's own resolution. */
	UEnum* EnumOf(const FProperty* InProperty)
	{
		if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(InProperty))
		{
			return AsEnum->GetEnum();
		}
		if (const FByteProperty* AsByte = CastField<FByteProperty>(InProperty))
		{
			return AsByte->Enum;
		}
		return nullptr;
	}

	/**
	 * The leaf's VALUE on a default object, walked down the same dotted path ResolveLeaf walks
	 * down the types. Null when any segment fails, which the caller treats as "no default".
	 */
	const void* ResolveLeafValue(const UStruct* InScope, const UObject* InDefaults, const FString& InPath)
	{
		TArray<FString> Segments;
		InPath.ParseIntoArray(Segments, TEXT("."));
		const UStruct* Scope = InScope;
		const void* Container = InDefaults;
		const void* ValuePtr = nullptr;
		for (const FString& Segment : Segments)
		{
			const FProperty* Property = Scope != nullptr ? FindFProperty<FProperty>(Scope, *Segment) : nullptr;
			if (Property == nullptr || Container == nullptr)
			{
				return nullptr;
			}
			ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			Scope = AsStruct != nullptr ? AsStruct->Struct : nullptr;
			Container = ValuePtr;
		}
		return ValuePtr;
	}

	/**
	 * One class's writable leaves, as the extension wants them: name, a type word, an enum ref --
	 * and, when a default object is handed in, the property's tooltip and its default value in
	 * this language's own spelling. Hover text over there is only as good as what lands here.
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeProperties(const UStruct* InScope,
		TMap<FString, TSharedPtr<FJsonObject>>& InOutEnums, const UObject* InDefaults = nullptr)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(InScope))
		{
			const FProperty* Leaf = ResolveLeaf(InScope, Path);
			if (Leaf == nullptr)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Path);
			Entry->SetStringField(TEXT("type"), Leaf->GetCPPType());
			const FString Tooltip = Leaf->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty())
			{
				Entry->SetStringField(TEXT("tooltip"), Tooltip);
			}
			if (InDefaults != nullptr)
			{
				// Printed through the same formatter the compiler reads with, so a hover's
				// "default = (0, 28)" is a value the author can paste straight into the file.
				if (const void* ValuePtr = ResolveLeafValue(InScope, InDefaults, Path))
				{
					FString Printed;
					if (DreamUIValueFormat::Print(Leaf, ValuePtr, Printed))
					{
						Entry->SetStringField(TEXT("default"), Printed);
					}
				}
			}
			if (const UEnum* Enum = EnumOf(Leaf))
			{
				const FString EnumName = Enum->GetName();
				Entry->SetStringField(TEXT("enum"), EnumName);
				if (!InOutEnums.Contains(EnumName))
				{
					TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
					TArray<TSharedPtr<FJsonValue>> Names;
					// NumEnums-1: the trailing _MAX UHT invents is not a value an author can write.
					for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
					{
						Names.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
					}
					Values->SetArrayField(TEXT("values"), Names);
					InOutEnums.Add(EnumName, Values);
				}
			}
			else if (DreamUIValueFormat::HasShortForm(Leaf))
			{
				// A gradient's short form is its CSS in a quoted string, not a colour.
				const EDreamUIValueKind LiteralKind = DreamUIValueFormat::GetShortFormLiteralKind(Leaf);
				const int32 Arity = DreamUIValueFormat::GetExpectedTupleArity(Leaf);
				Entry->SetStringField(TEXT("literal"), LiteralKind == EDreamUIValueKind::String ? FString(TEXT("string"))
					: Arity != INDEX_NONE ? FString::Printf(TEXT("tuple%d"), Arity) : FString(TEXT("color")));
			}
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Out;
	}

	/**
	 * Event names on a class: what `->` can route.
	 *
	 * Both kinds, matching DreamUITextBuilder's own test. `Controls/*` declares BlueprintAssignable
	 * dynamic multicast delegates; the older `Interaction/*` behaviours declare FDreamUIEventDelegate
	 * struct properties, and completion that offered only the first left half the plugin's events
	 * looking unroutable in VSCode even after they became routable.
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeEvents(const UStruct* InScope)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (TFieldIterator<FProperty> It(InScope); It; ++It)
		{
			const bool bIsAssignableDelegate = CastField<FMulticastDelegateProperty>(*It) != nullptr
				&& It->HasAnyPropertyFlags(CPF_BlueprintAssignable);
			const FStructProperty* AsDreamEvent = CastField<FStructProperty>(*It);
			const bool bIsDreamEvent = AsDreamEvent != nullptr
				&& AsDreamEvent->Struct == FDreamUIEventDelegate::StaticStruct()
				&& AsDreamEvent->HasAnyPropertyFlags(CPF_Edit);
			// And the single-cast delegates the builder routes too -- `OnPicked = Handler` -- listed again, on their own,
			// by DescribeSingleCastEvents, so completion can offer the operator each kind takes.
			const bool bIsSingleCast = CastField<FDelegateProperty>(*It) != nullptr;
			if (bIsAssignableDelegate || bIsDreamEvent || bIsSingleCast)
			{
				Out.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
		}
		return Out;
	}

	/**
	 * Which of DescribeEvents' names hold ONE listener: a single-cast delegate takes `=` (and `->`), never `+=`; every
	 * other event takes `+=` (and `->`), never `=`. A second list rather than a kind on each name, so the `events` arrays
	 * the extension already reads keep their shape.
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeSingleCastEvents(const UStruct* InScope)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (TFieldIterator<FProperty> It(InScope); It; ++It)
		{
			if (CastField<FDelegateProperty>(*It) != nullptr)
			{
				Out.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
		}
		return Out;
	}

	/** `events`, and `singleCastEvents` when there are any -- most classes have none, and the file stays as it was for them. */
	void SetEventFields(const TSharedPtr<FJsonObject>& InOutEntry, const UStruct* InScope,
		const TCHAR* InEventsField = TEXT("events"), const TCHAR* InSingleCastField = TEXT("singleCastEvents"))
	{
		InOutEntry->SetArrayField(InEventsField, DescribeEvents(InScope));
		TArray<TSharedPtr<FJsonValue>> SingleCast = DescribeSingleCastEvents(InScope);
		if (SingleCast.Num() > 0)
		{
			InOutEntry->SetArrayField(InSingleCastField, SingleCast);
		}
	}

	/**
	 * The shortest spelling ResolveComponentClass resolves back to this class -- the builder's own
	 * prefix scheme run in reverse and verified, so completion never offers a name the compiler
	 * would then refuse.
	 */
	FString ShortComponentName(UClass* InClass)
	{
		const FString Full = InClass->GetName();
		static const TCHAR* Prefixes[] =
		{
			TEXT("DreamLayoutContainer"), TEXT("DreamLayoutSelf"), TEXT("Dream"), TEXT("UI")
		};
		for (const TCHAR* Prefix : Prefixes)
		{
			FString Candidate = Full;
			if (Candidate.RemoveFromStart(Prefix) && !Candidate.IsEmpty())
			{
				if (FDreamUITextBuilder::ResolveComponentClass(Candidate) == InClass)
				{
					return Candidate;
				}
			}
		}
		if (FDreamUITextBuilder::ResolveComponentClass(Full) == InClass)
		{
			return Full;
		}
		return FString();
	}

	/**
	 * The node type a layout container is written as (`VerticalBox`), or empty when the builder would not read one back
	 * as this class. The same reverse-and-verify as ShortComponentName, against the builder's judge of what a TYPE is --
	 * which is narrower than what `+` takes: a behaviour is a component, never a node type.
	 */
	FString ContainerTypeName(UClass* InClass)
	{
		static const TCHAR* Prefixes[] = { TEXT("DreamLayoutContainer"), TEXT("Dream"), TEXT("UI") };
		const FString Full = InClass->GetName();
		for (const TCHAR* Prefix : Prefixes)
		{
			FString Candidate = Full;
			if (Candidate.RemoveFromStart(Prefix, ESearchCase::CaseSensitive) && !Candidate.IsEmpty()
				&& FDreamUITextBuilder::FindContainerClassForType(Candidate) == InClass)
			{
				return Candidate;
			}
		}
		return FDreamUITextBuilder::FindContainerClassForType(Full) == InClass ? Full : FString();
	}

	/**
	 * Every keyword of the grammar, for completion and highlighting. Kept here as a list, and that is the one table in
	 * this file: keywords are the parser's, which has no reflection to export them from. Contextual ones are in it too
	 * (`as` only means something after `use`, `default` after a slot's name, `emit` after `->`, `new` / `global` /
	 * `parent` after the `=` of a `viewmodels` line), because what an editor colours is the word, and a word that is
	 * sometimes a keyword is still worth colouring there.
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeKeywords()
	{
		static const TCHAR* Keywords[] =
		{
			// file scope
			TEXT("class"), TEXT("use"), TEXT("as"), TEXT("resources"), TEXT("style"), TEXT("timeline"), TEXT("external"),
			TEXT("props"), TEXT("events"), TEXT("viewmodels"),
			// after the '=' of a viewmodels line
			TEXT("new"), TEXT("global"), TEXT("parent"),
			// inside a node
			TEXT("slot"), TEXT("default"), TEXT("for"), TEXT("each"), TEXT("in"), TEXT("rows"), TEXT("if"), TEXT("else"), TEXT("was"),
			// after an arrow, and inside a timeline
			TEXT("emit"), TEXT("ease"), TEXT("duration"), TEXT("loop"),
		};
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TCHAR* Keyword : Keywords)
		{
			Out.Add(MakeShared<FJsonValueString>(Keyword));
		}
		return Out;
	}

	/**
	 * `Shown`, made sure of. It is how `Shown <- HasSave()` is written and what an `if` binds on its branches, so
	 * completion must offer it -- and the reflective sweep does not list it: it is DuiHidden, a face over Visibility the
	 * write-back must not write as a second copy of one value. What the sweep may write and what a file may say are two
	 * lists here, and this is the one name on the second and not the first.
	 */
	void EnsureShownListed(TArray<TSharedPtr<FJsonValue>>& InOutWidgetProperties)
	{
		for (const TSharedPtr<FJsonValue>& Value : InOutWidgetProperties)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Object) && Object != nullptr
				&& (*Object)->GetStringField(TEXT("name")) == TEXT("Shown"))
			{
				return;
			}
		}
		const FProperty* Shown = FindFProperty<FProperty>(UDreamWidget::StaticClass(), TEXT("Shown"));
		if (Shown == nullptr)
		{
			return;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), TEXT("Shown"));
		Entry->SetStringField(TEXT("type"), Shown->GetCPPType());
		const FString Tooltip = Shown->GetToolTipText().ToString();
		if (!Tooltip.IsEmpty())
		{
			Entry->SetStringField(TEXT("tooltip"), Tooltip);
		}
		InOutWidgetProperties.Add(MakeShared<FJsonValueObject>(Entry));
	}

	// ---- view models ----------------------------------------------------------------------------------------------

	/**
	 * A member's type as a Blueprint pin would name it -- `Text`, `Float`, `Integer`, `Bool`, `Object<ItemVM>`,
	 * `Array<Object<ItemVM>>` -- which is how an author thinks of what a member path reaches, and what the extension
	 * shows on hover. Anything without a pin word of its own falls back to its C++ type.
	 */
	FString DescribePinType(const FProperty* InProperty)
	{
		if (InProperty == nullptr)
		{
			return TEXT("Void");
		}
		if (const FArrayProperty* AsArray = CastField<FArrayProperty>(InProperty))
		{
			return FString::Printf(TEXT("Array<%s>"), *DescribePinType(AsArray->Inner));
		}
		if (const FSetProperty* AsSet = CastField<FSetProperty>(InProperty))
		{
			return FString::Printf(TEXT("Set<%s>"), *DescribePinType(AsSet->GetElementProperty()));
		}
		if (const FMapProperty* AsMap = CastField<FMapProperty>(InProperty))
		{
			return FString::Printf(TEXT("Map<%s, %s>"), *DescribePinType(AsMap->GetKeyProperty()), *DescribePinType(AsMap->GetValueProperty()));
		}
		if (InProperty->IsA<FBoolProperty>())
		{
			return TEXT("Bool");
		}
		if (InProperty->IsA<FFloatProperty>() || InProperty->IsA<FDoubleProperty>())
		{
			return TEXT("Float");
		}
		if (InProperty->IsA<FIntProperty>())
		{
			return TEXT("Integer");
		}
		if (InProperty->IsA<FInt64Property>())
		{
			return TEXT("Integer64");
		}
		if (const UEnum* Enum = EnumOf(InProperty))
		{
			return FString::Printf(TEXT("Enum<%s>"), *Enum->GetName());
		}
		if (InProperty->IsA<FByteProperty>())
		{
			return TEXT("Byte");
		}
		if (InProperty->IsA<FTextProperty>())
		{
			return TEXT("Text");
		}
		if (InProperty->IsA<FStrProperty>())
		{
			return TEXT("String");
		}
		if (InProperty->IsA<FNameProperty>())
		{
			return TEXT("Name");
		}
		if (const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty))
		{
			return FString::Printf(TEXT("Struct<%s>"), AsStruct->Struct != nullptr ? *AsStruct->Struct->GetName() : TEXT("None"));
		}
		// The class families before the object one they derive from.
		if (const FClassProperty* AsClass = CastField<FClassProperty>(InProperty))
		{
			return FString::Printf(TEXT("Class<%s>"), AsClass->MetaClass != nullptr ? *AsClass->MetaClass->GetName() : TEXT("Object"));
		}
		if (const FSoftClassProperty* AsSoftClass = CastField<FSoftClassProperty>(InProperty))
		{
			return FString::Printf(TEXT("SoftClass<%s>"), AsSoftClass->MetaClass != nullptr ? *AsSoftClass->MetaClass->GetName() : TEXT("Object"));
		}
		if (const FSoftObjectProperty* AsSoft = CastField<FSoftObjectProperty>(InProperty))
		{
			return FString::Printf(TEXT("SoftObject<%s>"), AsSoft->PropertyClass != nullptr ? *AsSoft->PropertyClass->GetName() : TEXT("Object"));
		}
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(InProperty))
		{
			return FString::Printf(TEXT("Object<%s>"), AsObject->PropertyClass != nullptr ? *AsObject->PropertyClass->GetName() : TEXT("Object"));
		}
		if (const FInterfaceProperty* AsInterface = CastField<FInterfaceProperty>(InProperty))
		{
			return FString::Printf(TEXT("Interface<%s>"), AsInterface->InterfaceClass != nullptr ? *AsInterface->InterfaceClass->GetName() : TEXT("Interface"));
		}
		if (InProperty->IsA<FMulticastDelegateProperty>())
		{
			return TEXT("MulticastDelegate");
		}
		if (InProperty->IsA<FDelegateProperty>())
		{
			return TEXT("Delegate");
		}
		return InProperty->GetCPPType();
	}

	/** A function's parameter that a caller passes: not its return value, and not an output that is not also an input. */
	bool IsInputParameter(const FProperty* InParameter)
	{
		return InParameter->HasAnyPropertyFlags(CPF_Parm) && !InParameter->HasAnyPropertyFlags(CPF_ReturnParm)
			&& (!InParameter->HasAnyPropertyFlags(CPF_OutParm) || InParameter->HasAnyPropertyFlags(CPF_ReferenceParm));
	}

	/**
	 * Whether `<-> Path.InMember` could write InMember back without the property itself being writable: InClass has a
	 * BlueprintCallable `Set<Member>` taking one value -- the setter the compiler's two-way lowering prefers, and the
	 * one way a read-only property becomes a two-way one.
	 */
	bool HasOneValueSetter(const UClass* InClass, const FString& InMember)
	{
		const FString SetterName = TEXT("Set") + InMember;
		if (SetterName.Len() >= NAME_SIZE)
		{
			return false;
		}
		const UFunction* Setter = InClass->FindFunctionByName(FName(*SetterName));
		if (Setter == nullptr || !Setter->HasAnyFunctionFlags(FUNC_BlueprintCallable))
		{
			return false;
		}
		int32 Inputs = 0;
		for (TFieldIterator<FProperty> It(Setter); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			Inputs += IsInputParameter(*It) ? 1 : 0;
		}
		return Inputs == 1;
	}

	/** True for the classes UE leaves behind a recompile or a reload, which are nobody's to name. */
	bool IsTransitionalClass(const UClass* InClass)
	{
		const FString Name = InClass->GetName();
		for (const TCHAR* Prefix : { TEXT("SKEL_"), TEXT("REINST_"), TEXT("TRASHCLASS_"), TEXT("TRASH_"), TEXT("HOTRELOADED_"), TEXT("PLACEHOLDER-CLASS") })
		{
			if (Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * What a `viewmodels` line can name: a UDreamViewModel, or any class that announces its fields
	 * (INotifyFieldValueChanged) -- UE's own UMVVMViewModelBase among them. Never a widget, though widgets announce
	 * theirs too: a view model is the object a widget SHOWS, and offering the widgets would bury the few real ones under
	 * every widget class there is. Abstract classes stay: a host-given or global view model may be declared by its base.
	 */
	bool IsViewModelClass(const UClass* InClass)
	{
		if (InClass == nullptr
			|| InClass->HasAnyClassFlags(CLASS_Interface | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| IsTransitionalClass(InClass))
		{
			return false;
		}
		if (InClass->IsChildOf(UWidget::StaticClass()) || InClass->IsChildOf(UDreamWidget::StaticClass()))
		{
			return false;
		}
		return InClass->IsChildOf(UDreamViewModel::StaticClass())
			|| InClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass());
	}

	/**
	 * Every member a member path can reach on a view model class, keyed by name and sorted: the BlueprintVisible
	 * properties and the BlueprintCallable / BlueprintPure functions -- the reach the compiler checks a path against
	 * (MemberPathNotFound). Each says whether it announces itself (fieldNotify: a binding through it subscribes rather
	 * than polls) and, for a property, whether `<->` can write it back.
	 */
	TSharedPtr<FJsonObject> DescribeViewModelMembers(const UClass* InClass)
	{
		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Members;
		TSet<FString> Seen;

		for (TFieldIterator<FProperty> It(InClass); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible) || Property->HasAnyPropertyFlags(CPF_Deprecated))
			{
				continue;
			}
			const FString Name = Property->GetName();
			TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
			Member->SetStringField(TEXT("kind"), TEXT("property"));
			Member->SetStringField(TEXT("type"), DescribePinType(Property));
			Member->SetBoolField(TEXT("fieldNotify"), DreamUIBindingPath::FindFieldId(InClass, Property->GetFName()).IsValid());
			Member->SetBoolField(TEXT("writable"), !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly) || HasOneValueSetter(InClass, Name));
			const FString Tooltip = Property->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty())
			{
				Member->SetStringField(TEXT("tooltip"), Tooltip);
			}
			Seen.Add(Name);
			Members.Emplace(Name, Member);
		}

		for (TFieldIterator<UFunction> It(InClass); It; ++It)
		{
			const UFunction* Function = *It;
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) || Function->HasAnyFunctionFlags(FUNC_Delegate))
			{
				continue;
			}
			// The plumbing a graph reaches through wrapper nodes, never by its own name: `K2_` functions and anything
			// marked BlueprintInternalUseOnly (UDreamViewModel's own delegate helpers are the former).
			const FString Name = Function->GetName();
			if (Name.StartsWith(TEXT("K2_"), ESearchCase::CaseSensitive) || Function->HasMetaData(TEXT("BlueprintInternalUseOnly"))
				|| Seen.Contains(Name))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Member = MakeShared<FJsonObject>();
			Member->SetStringField(TEXT("kind"), TEXT("function"));
			Member->SetStringField(TEXT("type"), DescribePinType(Function->GetReturnProperty()));
			Member->SetBoolField(TEXT("fieldNotify"), DreamUIBindingPath::FindFieldId(InClass, Function->GetFName()).IsValid());
			Member->SetBoolField(TEXT("writable"), false);
			TArray<TSharedPtr<FJsonValue>> Params;
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && ParamIt->HasAnyPropertyFlags(CPF_Parm); ++ParamIt)
			{
				if (!IsInputParameter(*ParamIt))
				{
					continue;
				}
				TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				Param->SetStringField(TEXT("name"), ParamIt->GetName());
				Param->SetStringField(TEXT("type"), DescribePinType(*ParamIt));
				Params.Add(MakeShared<FJsonValueObject>(Param));
			}
			Member->SetArrayField(TEXT("params"), Params);
			const FString Tooltip = Function->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty())
			{
				Member->SetStringField(TEXT("tooltip"), Tooltip);
			}
			Seen.Add(Name);
			Members.Emplace(Name, Member);
		}

		// Sorted, so the file comes out the same whatever order reflection or loading put the members in.
		Members.Sort([](const TPair<FString, TSharedPtr<FJsonObject>>& InA, const TPair<FString, TSharedPtr<FJsonObject>>& InB)
		{
			return InA.Key < InB.Key;
		});
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Member : Members)
		{
			Out->SetObjectField(Member.Key, Member.Value);
		}
		return Out;
	}
}

TSharedRef<FJsonObject> FDreamUISymbolExport::DescribeViewModels()
{
	using namespace DreamUISymbolExportLocal;

	TArray<UClass*> Classes;
	TMap<FString, int32> NameCounts;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (IsViewModelClass(*It))
		{
			Classes.Add(*It);
			++NameCounts.FindOrAdd(It->GetName());
		}
	}

	// Keyed by the reflected name, which is how a `viewmodels` line writes the type -- unless two loaded classes share
	// it, and then by path, which is then the only spelling that resolves (ViewModelClassUnknown names the clash).
	TArray<TPair<FString, UClass*>> Keyed;
	for (UClass* Class : Classes)
	{
		const FString Name = Class->GetName();
		Keyed.Emplace(NameCounts.FindRef(Name) > 1 ? Class->GetPathName() : Name, Class);
	}
	// Sorted, so the file is the same from one editor session to the next whatever order the classes loaded in.
	Keyed.Sort([](const TPair<FString, UClass*>& InA, const TPair<FString, UClass*>& InB) { return InA.Key < InB.Key; });

	TSharedRef<FJsonObject> ViewModels = MakeShared<FJsonObject>();
	for (const TPair<FString, UClass*>& Entry : Keyed)
	{
		const UClass* Class = Entry.Value;
		TSharedPtr<FJsonObject> Described = MakeShared<FJsonObject>();
		Described->SetStringField(TEXT("class"), Class->GetPathName());
		const FString ClassTooltip = Class->GetToolTipText().ToString();
		if (!ClassTooltip.IsEmpty())
		{
			Described->SetStringField(TEXT("tooltip"), ClassTooltip);
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract))
		{
			// `= new` cannot make one (ViewModelSourceInvalid); every other source can hold a subclass.
			Described->SetBoolField(TEXT("abstract"), true);
		}
		Described->SetObjectField(TEXT("members"), DescribeViewModelMembers(Class));
		ViewModels->SetObjectField(Entry.Key, Described);
	}
	return ViewModels;
}

void FDreamUISymbolExport::Register()
{
	using namespace DreamUISymbolExportLocal;
	// OnPostEngineInit rather than module startup: the dump reads every reflected class, and the
	// class set is only whole once every module has loaded.
	GStartupHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([] { ExportNow(); });
}

void FDreamUISymbolExport::Unregister()
{
	using namespace DreamUISymbolExportLocal;
	if (GStartupHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(GStartupHandle);
		GStartupHandle.Reset();
	}
}

FString FDreamUISymbolExport::ExportNow()
{
	using namespace DreamUISymbolExportLocal;

	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), DreamUIPaths::SourceDirectoryName));
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		return FString();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	// 2: `viewModels`, the `viewmodels` keywords, and `singleCastEvents` beside the event lists.
	Root->SetNumberField(TEXT("version"), 2);
	TMap<FString, TSharedPtr<FJsonObject>> Enums;

	// ---- tags, from the builder's own table
	TSharedPtr<FJsonObject> Tags = MakeShared<FJsonObject>();
	TArray<TPair<FString, UClass*>> TagTable;
	FDreamUITextBuilder::GetVisualTags(TagTable);
	for (const TPair<FString, UClass*>& Tag : TagTable)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		// Which of the three kinds of node type an entry is: a visual's tag, a layout container, or a registered widget.
		// The extension reads properties the same way for all three; the kind is for what it shows beside them.
		Entry->SetStringField(TEXT("kind"), TEXT("visual"));
		if (Tag.Value != nullptr)
		{
			Entry->SetStringField(TEXT("class"), Tag.Value->GetName());
			const FString ClassTooltip = Tag.Value->GetToolTipText().ToString();
			if (!ClassTooltip.IsEmpty())
			{
				Entry->SetStringField(TEXT("tooltip"), ClassTooltip);
			}
			Entry->SetArrayField(TEXT("properties"),
				DescribeProperties(Tag.Value, Enums, Tag.Value->GetDefaultObject()));
			SetEventFields(Entry, Tag.Value);
		}
		Tags->SetObjectField(Tag.Key, Entry);
	}
	// ---- and the SCOPED tags, from the widget registry the DECLARE macro fills
	//
	// A second table, and it had never been exported: the extension learned the primitives from
	// GetVisualTags and nothing at all about `Native.Button`, so completion offered none of the
	// seventeen controls and every one of them read as an unknown tag. Keyed "Scope.Name", which is
	// exactly how a .dui spells it and how the extension looks it up.
	TArray<FDreamUIWidgetRegistry::FEntry> Registered;
	FDreamUIWidgetRegistry::GetAllEntries(Registered);
	for (const FDreamUIWidgetRegistry::FEntry& Entry : Registered)
	{
		UClass* Class = Entry.ClassGetter != nullptr ? Entry.ClassGetter() : nullptr;
		if (Class == nullptr)
		{
			continue;
		}
		TSharedPtr<FJsonObject> TagEntry = MakeShared<FJsonObject>();
		TagEntry->SetStringField(TEXT("kind"), TEXT("widget"));
		TagEntry->SetStringField(TEXT("class"), Class->GetName());
		const FString ClassTooltip = Class->GetToolTipText().ToString();
		if (!ClassTooltip.IsEmpty())
		{
			TagEntry->SetStringField(TEXT("tooltip"), ClassTooltip);
		}
		TagEntry->SetArrayField(TEXT("properties"),
			DescribeProperties(Class, Enums, Class->GetDefaultObject()));
		SetEventFields(TagEntry, Class);
		Tags->SetObjectField(FString::Printf(TEXT("%s.%s"),
			*Entry.Scope.ToString(), *Entry.Name.ToString()), TagEntry);
	}
	// ---- and the layout containers, which are node types too (`VerticalBox Column { Spacing = 29 }`)
	//
	// Under the name the builder reads as the type -- FindContainerClassForType is the judge, so completion offers
	// exactly the containers a compile accepts -- with the CONTAINER's properties, which is what such a node's own lines
	// set beyond the widget's. A name a visual tag already has stays the visual's: the tag table is what the builder
	// asks first.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UDreamLayoutContainer::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown))
		{
			continue;
		}
		const FString TypeName = ContainerTypeName(Class);
		if (TypeName.IsEmpty() || Tags->HasField(TypeName))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("kind"), TEXT("container"));
		Entry->SetStringField(TEXT("class"), Class->GetName());
		const FString ClassTooltip = Class->GetToolTipText().ToString();
		if (!ClassTooltip.IsEmpty())
		{
			Entry->SetStringField(TEXT("tooltip"), ClassTooltip);
		}
		Entry->SetArrayField(TEXT("properties"), DescribeProperties(Class, Enums, Class->GetDefaultObject()));
		SetEventFields(Entry, Class);
		Tags->SetObjectField(TypeName, Entry);
	}

	Root->SetObjectField(TEXT("tags"), Tags);

	// ---- the words of the grammar itself
	Root->SetArrayField(TEXT("keywords"), DescribeKeywords());
	{
		// What may follow an '@' at the start of a line, besides a resource used as a node type: the slot line or block,
		// and its shorthand. `@key("…")` follows a value and is listed for the same completion.
		TArray<TSharedPtr<FJsonValue>> Annotations;
		for (const TCHAR* Annotation : { TEXT("slot"), TEXT("fill"), TEXT("key") })
		{
			Annotations.Add(MakeShared<FJsonValueString>(Annotation));
		}
		Root->SetArrayField(TEXT("annotations"), Annotations);
	}

	// ---- the two classes every node line can address regardless of tag
	TArray<TSharedPtr<FJsonValue>> WidgetProperties = DescribeProperties(UDreamWidget::StaticClass(), Enums,
		UDreamWidget::StaticClass()->GetDefaultObject());
	EnsureShownListed(WidgetProperties);
	Root->SetArrayField(TEXT("widgetProperties"), WidgetProperties);
	SetEventFields(Root, UDreamWidget::StaticClass(), TEXT("widgetEvents"), TEXT("widgetSingleCastEvents"));
	Root->SetArrayField(TEXT("slotProperties"), DescribeProperties(UDreamPanelSlot::StaticClass(), Enums,
		UDreamPanelSlot::StaticClass()->GetDefaultObject()));

	// ---- components: everything `+` can attach, under the shortest name the compiler resolves
	TSharedPtr<FJsonObject> Components = MakeShared<FJsonObject>();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		const bool bAttachable = Class->IsChildOf(UDreamUIBehaviour::StaticClass())
			|| Class->IsChildOf(UDreamLayout::StaticClass());
		if (!bAttachable
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown))
		{
			continue;
		}
		const FString Short = ShortComponentName(Class);
		if (Short.IsEmpty())
		{
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), Class->GetName());
		const FString ClassTooltip = Class->GetToolTipText().ToString();
		if (!ClassTooltip.IsEmpty())
		{
			Entry->SetStringField(TEXT("tooltip"), ClassTooltip);
		}
		Entry->SetArrayField(TEXT("properties"), DescribeProperties(Class, Enums, Class->GetDefaultObject()));
		SetEventFields(Entry, Class);
		Components->SetObjectField(Short, Entry);
	}
	Root->SetObjectField(TEXT("components"), Components);

	// ---- view models: what a `viewmodels` line can name, and what a member path can reach on each
	const TSharedPtr<FJsonObject> ViewModels = DescribeViewModels();
	Root->SetObjectField(TEXT("viewModels"), ViewModels);

	// ---- enums referenced above, and the resource types the grammar's one typed corner takes
	TSharedPtr<FJsonObject> EnumsObject = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Enum : Enums)
	{
		EnumsObject->SetObjectField(Enum.Key, Enum.Value);
	}
	Root->SetObjectField(TEXT("enums"), EnumsObject);

	TArray<TSharedPtr<FJsonValue>> ResourceTypes;
	for (const TCHAR* Type : { TEXT("Color"), TEXT("Number"), TEXT("Vector2"), TEXT("String"), TEXT("Asset") })
	{
		ResourceTypes.Add(MakeShared<FJsonValueString>(Type));
	}
	Root->SetArrayField(TEXT("resourceTypes"), ResourceTypes);

	// The types a `props` line and an `events` parameter take (FDreamUIPropDecl); `Enum` is followed by the enum's path.
	TArray<TSharedPtr<FJsonValue>> PropTypes;
	for (const TCHAR* Type : { TEXT("Text"), TEXT("String"), TEXT("Number"), TEXT("Integer"), TEXT("Bool"), TEXT("Color"),
		TEXT("Vector2"), TEXT("Asset"), TEXT("Class"), TEXT("Enum") })
	{
		PropTypes.Add(MakeShared<FJsonValueString>(Type));
	}
	Root->SetArrayField(TEXT("propTypes"), PropTypes);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString FilePath = FPaths::Combine(Directory, TEXT(".dui-symbols.json"));
	if (!FFileHelper::SaveStringToFile(Serialized, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Could not write '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
		return FString();
	}
	UE_LOG(DreamGUIEditor, Display, TEXT("[%s].%d Wrote '%s'."),
		ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FilePath);
	return FilePath;
}
