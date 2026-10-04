// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISymbolExport.h"

#include "DreamGUIEditorModule.h"
#include "Core/DreamUIBehaviour.h"
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

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
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
			if (bIsAssignableDelegate || bIsDreamEvent)
			{
				Out.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
		}
		return Out;
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
	 * (`as` only means something after `use`, `default` after a slot's name, `emit` after `->`), because what an editor
	 * colours is the word, and a word that is sometimes a keyword is still worth colouring there.
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeKeywords()
	{
		static const TCHAR* Keywords[] =
		{
			// file scope
			TEXT("class"), TEXT("use"), TEXT("as"), TEXT("resources"), TEXT("style"), TEXT("timeline"), TEXT("external"),
			TEXT("props"), TEXT("events"),
			// inside a node
			TEXT("slot"), TEXT("default"), TEXT("for"), TEXT("each"), TEXT("in"), TEXT("if"), TEXT("else"), TEXT("was"),
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
	Root->SetNumberField(TEXT("version"), 1);
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
			Entry->SetArrayField(TEXT("events"), DescribeEvents(Tag.Value));
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
		TagEntry->SetArrayField(TEXT("events"), DescribeEvents(Class));
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
		Entry->SetArrayField(TEXT("events"), DescribeEvents(Class));
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
	Root->SetArrayField(TEXT("widgetEvents"), DescribeEvents(UDreamWidget::StaticClass()));
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
		Entry->SetArrayField(TEXT("events"), DescribeEvents(Class));
		Components->SetObjectField(Short, Entry);
	}
	Root->SetObjectField(TEXT("components"), Components);

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
