// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITextWriteBack.h"

// FTextProperty is dereferenced when deciding whether a value is an FText literal; inside a unity
// blob a neighbour always had it.
#include "UObject/TextProperty.h"
#include "Text/DreamUIReflectionPolicy.h"
#include "Core/Components/DreamVisual.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"

#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUIValueFormat.h"

#include "Editor.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamUITextWriteBack"

// -------------------------------------------------------------------------------------------------
// The document registry
// -------------------------------------------------------------------------------------------------

namespace DreamUIDocumentRegistryLocal
{
	struct FEntry
	{
		/**
		 * Weak, so the map never keeps a document alive. See FDreamUIDocumentRegistry's comment: a
		 * strong map would hand a reopened file the document from the last session, still holding
		 * text and a disk hash from before whatever happened to the file in between.
		 */
		TWeakObjectPtr<UDreamUIDocument> Document;
		/** How many handles are out. The entry goes when this reaches zero. */
		int32 RefCount = 0;
	};

	/**
	 * Keyed on the normalised path. TMap<FString, …> hashes and compares case insensitively, which
	 * is what we want on Windows, where `Login.dui` and `login.dui` are one file.
	 */
	TMap<FString, FEntry>& GetMap()
	{
		static TMap<FString, FEntry> Map;
		return Map;
	}
}

FString FDreamUIDocumentRegistry::NormalizePath(const FString& InFilePath)
{
	if (InFilePath.IsEmpty())
	{
		return FString();
	}
	// Three passes, each closing one way for the same file to arrive under two keys: relative
	// against the process cwd, backslashes from a Windows dialog, and `..` from a path assembled by
	// concatenation. The key is also the path the document is opened with, so this has to stay a
	// SPELLING change and never a path change.
	//
	// FPaths::RemoveDuplicateSlashes is deliberately NOT the fourth. It collapses a leading `//` as
	// readily as an interior one (Paths.cpp:1384 starts at the first "//" wherever it is), so on a
	// project served from a UNC share every document would be opened on a path with the share name
	// eaten. A doubled slash mid-path costs one extra registry entry; that costs every file.
	FString Path = FPaths::ConvertRelativePathToFull(InFilePath);
	FPaths::NormalizeFilename(Path);
	FPaths::CollapseRelativeDirectories(Path);
	return Path;
}

UDreamUIDocument* FDreamUIDocumentRegistry::Find(const FString& InFilePath)
{
	using namespace DreamUIDocumentRegistryLocal;

	const FString Key = NormalizePath(InFilePath);
	if (FEntry* Entry = GetMap().Find(Key))
	{
		return Entry->Document.Get();
	}
	return nullptr;
}

int32 FDreamUIDocumentRegistry::NumTracked()
{
	using namespace DreamUIDocumentRegistryLocal;

	// Prunes as it counts. An entry whose document has been collected is not tracking anything, and
	// a count that included one would make "every editor closed" look like a leak in a test.
	for (auto It = GetMap().CreateIterator(); It; ++It)
	{
		if (!It.Value().Document.IsValid())
		{
			It.RemoveCurrent();
		}
	}
	return GetMap().Num();
}

UDreamUIDocument* FDreamUIDocumentRegistry::Acquire(const FString& InFilePath, FString& OutError)
{
	using namespace DreamUIDocumentRegistryLocal;

	OutError.Reset();
	const FString Key = NormalizePath(InFilePath);
	if (Key.IsEmpty())
	{
		OutError = TEXT("no file path was given");
		return nullptr;
	}

	if (FEntry* Existing = GetMap().Find(Key))
	{
		if (UDreamUIDocument* Document = Existing->Document.Get())
		{
			++Existing->RefCount;
			return Document;
		}
		// The document was collected while the count still said somebody held it. Impossible while
		// every user holds a strong reference through a handle, which is why this drops the entry
		// rather than trying to reconcile the number: a count nobody can explain is worse than a
		// fresh start, and the fresh start re-reads the file, which is the honest state anyway.
		GetMap().Remove(Key);
	}

	UDreamUIDocument* Created = UDreamUIDocument::CreateFromFile(nullptr, Key, OutError);
	if (Created == nullptr)
	{
		return nullptr;
	}

	FEntry& Entry = GetMap().FindOrAdd(Key);
	Entry.Document = Created;
	Entry.RefCount = 1;
	return Created;
}

void FDreamUIDocumentRegistry::Release(const FString& InFilePath)
{
	using namespace DreamUIDocumentRegistryLocal;

	const FString Key = NormalizePath(InFilePath);
	if (FEntry* Entry = GetMap().Find(Key))
	{
		if (--Entry->RefCount <= 0)
		{
			GetMap().Remove(Key);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// The handle
// -------------------------------------------------------------------------------------------------

FDreamUIDocumentHandle::~FDreamUIDocumentHandle()
{
	Reset();
}

FDreamUIDocumentHandle::FDreamUIDocumentHandle(FDreamUIDocumentHandle&& InOther)
	: FilePath(MoveTemp(InOther.FilePath))
	, Document(MoveTemp(InOther.Document))
{
	// The moved-from handle must not release: the reference moved with the pointer, and a path left
	// behind would take the count down while this handle still holds the document.
	InOther.FilePath.Reset();
}

FDreamUIDocumentHandle& FDreamUIDocumentHandle::operator=(FDreamUIDocumentHandle&& InOther)
{
	if (this != &InOther)
	{
		Reset();
		FilePath = MoveTemp(InOther.FilePath);
		Document = MoveTemp(InOther.Document);
		InOther.FilePath.Reset();
	}
	return *this;
}

FDreamUIDocumentHandle FDreamUIDocumentHandle::Open(const FString& InFilePath, FString& OutError)
{
	FDreamUIDocumentHandle Handle;

	const FString Key = FDreamUIDocumentRegistry::NormalizePath(InFilePath);
	UDreamUIDocument* Document = FDreamUIDocumentRegistry::Acquire(Key, OutError);
	if (Document == nullptr)
	{
		return Handle;
	}

	Handle.FilePath = Key;
	Handle.Document.Reset(Document);
	return Handle;
}

void FDreamUIDocumentHandle::Reset()
{
	if (!FilePath.IsEmpty())
	{
		FDreamUIDocumentRegistry::Release(FilePath);
		FilePath.Reset();
	}
	Document.Reset();
}

// -------------------------------------------------------------------------------------------------
// Values: the live half of a `.dui` line
// -------------------------------------------------------------------------------------------------

namespace DreamUIWriteBackLocal
{
	/**
	 * The five escapes the lexer resolves, written back so that what is printed reads as what it was.
	 *
	 * The backslash goes first, or escaping the quote would then have its own backslash escaped and
	 * every string would grow one on each save. The newline pair is not optional even though it looks
	 * exotic: FDreamUITextPatcher refuses a value that spans lines, so an FText containing one would
	 * simply never write back, silently, forever.
	 */
	FString QuoteString(const FString& InValue)
	{
		FString Escaped = InValue;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"), ESearchCase::CaseSensitive);
		Escaped.ReplaceInline(TEXT("\t"), TEXT("\\t"), ESearchCase::CaseSensitive);
		return TEXT("\"") + Escaped + TEXT("\"");
	}

	/**
	 * One number, in the shortest text that reads back as the same value.
	 *
	 * Forwards to the runtime's printer rather than holding a second one. It WAS a copy, because the
	 * body was file-static in DreamGUI and only the short forms were exported -- and a copy is the
	 * one thing this must not be: a designer compares "the tree, printed" against "the file", so two
	 * printers that round differently report a change on a value nobody touched, every flush,
	 * forever. Neither module's tests could see it; each round-trips through its own copy and passes.
	 */
	FString PrintScalar(const double InValue, const bool bSinglePrecision)
	{
		return DreamUIValueFormat::PrintScalar(InValue, bSinglePrecision);
	}

	UEnum* GetEnumForProperty(const FProperty* InProperty)
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
	 * A live value as the `.dui` literal that would produce it, or false when it has no spelling.
	 *
	 * The cases and their ORDER mirror FDreamUITextBuilder's WriteValue, because this has to be its
	 * inverse: a printer that reached for ImportText where the builder reaches for a short form
	 * would write `(X=400.000000,Y=240.000000)` into a file whose grammar has no such literal, and
	 * the file would save and refuse to reopen.
	 *
	 * False is not a failure to report. It means "the language cannot say this", which is true of an
	 * object reference, a plain FVector, an array and a map, and the right response is to leave that
	 * line alone: writing a plausible wrong literal into the author's file is the one outcome worth
	 * more than a missing feature. The one place it matters is the details panel, which will show
	 * such a property as editable until P6 greys it out -- an edit there is silently not persisted,
	 * which is why it is on the handover list rather than buried here.
	 *
	 * OutValueIsUnrepresentable splits that false in two, and the split is what gives DUI7003 a place
	 * to be raised from. "This TYPE has no literal" (an array, a node reference) is a property of the
	 * schema, true forever, and nobody wants it in the Problems panel on every flush. "This VALUE has
	 * no literal" -- a NaN in a float, a byte in an enum the enum does not declare -- is a state
	 * somebody should chase: the row is grey, the drag does nothing, and before this the only trace
	 * was one Output Log line. Set only for the second kind; left alone for the first.
	 */
	/**
	 * The node ids of the tree whose value is being printed, for the one literal that needs the tree to be spelled: a
	 * node reference, written as the bare id of the node it points at. Set for the length of a value pass by CollectEdits
	 * (one map per side); null everywhere else -- a details-panel probe, a resource -- where no tree is in sight.
	 */
	const TMap<FString, UDreamWidget*>* GPrintNodeIds = nullptr;
	/** The two trees' ids for the value pass under way, which CompareAndAppend hands to GPrintNodeIds side by side. */
	const TMap<FString, UDreamWidget*>* GComparisonLiveIds = nullptr;
	const TMap<FString, UDreamWidget*>* GComparisonTextIds = nullptr;

	/**
	 * Set for the length of CollectUnwritableDifferences: where a value pass says what it could not print. Null for every
	 * flush -- a value with no spelling is left alone there, as it always was, and only a conversion has to hear of it.
	 */
	TArray<FString>* GUnwritableSink = nullptr;

	/**
	 * Whether two values of a property with no spelling differ in a way a conversion would lose. An object that is no
	 * asset -- a widget's own subobject, an object made at run time -- is a different object in every tree by
	 * construction, so two of them count as the same; anything else is compared as the engine compares it.
	 */
	bool DiffersWithoutSpelling(const FProperty* InLeaf, const void* InLive, const void* InText)
	{
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(InLeaf))
		{
			const UObject* Live = AsObject->GetObjectPropertyValue(InLive);
			const UObject* Text = AsObject->GetObjectPropertyValue(InText);
			if ((Live == nullptr) != (Text == nullptr))
			{
				return true;
			}
			if (Live == nullptr || (!Live->IsAsset() && !Text->IsAsset()))
			{
				return false;
			}
			return Live != Text;
		}
		return !InLeaf->Identical(InLive, InText, PPF_None);
	}

	/** The node an object a node reference can hold belongs to: itself, its visual, a behaviour on it. */
	const UDreamWidget* NodeOfReferencedObject(const UObject* InObject)
	{
		if (const UDreamWidget* AsWidget = Cast<UDreamWidget>(InObject))
		{
			return AsWidget;
		}
		if (const UDreamWidgetSubObjectBehaviour* AsSubObject = Cast<UDreamWidgetSubObjectBehaviour>(InObject))
		{
			return AsSubObject->GetWidget();
		}
		if (const UDreamUIBehaviour* AsBehaviour = Cast<UDreamUIBehaviour>(InObject))
		{
			return AsBehaviour->GetWidget();
		}
		return nullptr;
	}

	bool PrintLiteral(const FProperty* InLeaf, const void* InValuePtr, FString& OutText,
		bool* OutValueIsUnrepresentable = nullptr);

	/**
	 * A name key as a map is written by hand -- `{ Hover: #FFCC00 }`, not `{ "Hover": #FFCC00 }` -- when it is one word the
	 * builder reads back as that name. Anything else (a space, a leading digit, a word the language already means) stays
	 * quoted.
	 */
	FString BareNameKey(const FString& InQuoted)
	{
		if (InQuoted.Len() < 3 || !InQuoted.StartsWith(TEXT("\"")) || !InQuoted.EndsWith(TEXT("\"")))
		{
			return InQuoted;
		}
		const FString Word = InQuoted.Mid(1, InQuoted.Len() - 2);
		auto IsWordChar = [](TCHAR InChar)
		{
			return FChar::IsDigit(InChar) || InChar == TEXT('_') || (InChar >= TEXT('a') && InChar <= TEXT('z'))
				|| (InChar >= TEXT('A') && InChar <= TEXT('Z')) || InChar > 0x7F;
		};
		if (FChar::IsDigit(Word[0]) || Word.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Word.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return InQuoted;
		}
		for (const TCHAR Char : Word)
		{
			if (!IsWordChar(Char))
			{
				return InQuoted;
			}
		}
		return Word;
	}

	/**
	 * One element of a container. A struct with no short form is written the way the builder reads one there -- a quoted
	 * string it hands the engine's importer, `Style.DropShadows = ["(Angle=120,Distance=6)"]` -- with only the fields that
	 * differ from the struct's defaults, so two equal values print alike and an untouched field is not spelled. Anything
	 * else prints as a leaf does.
	 */
	bool PrintContainerElement(const FProperty* InElement, const void* InValuePtr, FString& OutText, bool* OutValueIsUnrepresentable);

	/**
	 * A struct as the map of its fields the builder reads -- `{ "Lamp", Tag: Lamp, bEnabled: false }`: the fields that
	 * differ from the struct's defaults, in declaration order, the primary field (DreamGUI_PrimaryField) first and with no
	 * key. False when a field has no spelling of its own; the caller then keeps the engine's quoted form.
	 *
	 * Only for a struct that names a primary field, which is the struct saying it was made to be written this way. The
	 * rest keep the quoted form they always had: a colour field would be quantised through a hex code here, where the
	 * engine's text keeps every bit of it.
	 */
	bool PrintStructAsMap(const FStructProperty* InStruct, const void* InValuePtr, FString& OutText, bool* OutValueIsUnrepresentable)
	{
		const UScriptStruct* Struct = InStruct->Struct;
		FString PrimaryField;
#if WITH_METADATA
		PrimaryField = Struct->GetMetaData(TEXT("DreamGUI_PrimaryField"));
#endif
		if (PrimaryField.IsEmpty())
		{
			return false;
		}
		void* Defaults = FMemory::Malloc(FMath::Max(Struct->GetStructureSize(), 1), Struct->GetMinAlignment());
		Struct->InitializeStruct(Defaults);
		ON_SCOPE_EXIT
		{
			Struct->DestroyStruct(Defaults);
			FMemory::Free(Defaults);
		};
		FString Primary;
		TArray<FString> Entries;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Field = *It;
			if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
				|| Field->Identical_InContainer(InValuePtr, Defaults))
			{
				continue;
			}
			FString FieldText;
			if (!PrintContainerElement(Field, Field->ContainerPtrToValuePtr<void>(InValuePtr), FieldText, OutValueIsUnrepresentable))
			{
				return false;
			}
			if (CastField<FNameProperty>(Field) != nullptr)
			{
				FieldText = BareNameKey(FieldText);
			}
			if (Field->GetName() == PrimaryField)
			{
				Primary = MoveTemp(FieldText);
				continue;
			}
			Entries.Add(FString::Printf(TEXT("%s: %s"), *Field->GetName(), *FieldText));
		}
		if (!Primary.IsEmpty())
		{
			Entries.Insert(MoveTemp(Primary), 0);
		}
		OutText = Entries.IsEmpty() ? FString(TEXT("{}")) : FString::Printf(TEXT("{ %s }"), *FString::Join(Entries, TEXT(", ")));
		return true;
	}

	bool PrintContainerElement(const FProperty* InElement, const void* InValuePtr, FString& OutText, bool* OutValueIsUnrepresentable)
	{
		if (PrintLiteral(InElement, InValuePtr, OutText, OutValueIsUnrepresentable))
		{
			return true;
		}
		const FStructProperty* AsStruct = CastField<FStructProperty>(InElement);
		if (AsStruct == nullptr || AsStruct->Struct == nullptr || DreamUIValueFormat::HasShortForm(InElement)
			|| (OutValueIsUnrepresentable != nullptr && *OutValueIsUnrepresentable))
		{
			return false;
		}
		if (PrintStructAsMap(AsStruct, InValuePtr, OutText, OutValueIsUnrepresentable))
		{
			return true;
		}
		const UScriptStruct* Struct = AsStruct->Struct;
		void* Defaults = FMemory::Malloc(FMath::Max(Struct->GetStructureSize(), 1), Struct->GetMinAlignment());
		Struct->InitializeStruct(Defaults);
		FString Exported;
		AsStruct->ExportTextItem_Direct(Exported, InValuePtr, Defaults, nullptr, PPF_None);
		Struct->DestroyStruct(Defaults);
		FMemory::Free(Defaults);
		if (Exported.IsEmpty())
		{
			// Every field at its default: the importer reads an empty list of fields as that.
			Exported = TEXT("()");
		}
		OutText = QuoteString(Exported);
		return true;
	}

	/**
	 * A container's entries as they print: a list's elements in order, a set's elements and a map's `key: value`s in
	 * the order the container holds them, with each entry's identity (the element, the key) in OutKeys. False when an
	 * entry has no spelling -- then neither has the container.
	 */
	bool PrintContainerEntries(const FProperty* InLeaf, const void* InValuePtr, TArray<FString>& OutKeys,
		TArray<FString>& OutEntries, bool* OutValueIsUnrepresentable)
	{
		OutKeys.Reset();
		OutEntries.Reset();
		auto IsNodeElement = [](const FProperty* InElement)
		{
			const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(InElement);
			return AsObject != nullptr && FDreamUITextBuilder::IsNodeReferenceProperty(AsObject);
		};
		if (const FArrayProperty* AsArray = CastField<FArrayProperty>(InLeaf))
		{
			FScriptArrayHelper Helper(AsArray, InValuePtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				FString Element;
				if (!PrintContainerElement(AsArray->Inner, Helper.GetRawPtr(Index), Element, OutValueIsUnrepresentable))
				{
					return false;
				}
				OutKeys.Add(Element);
				OutEntries.Add(MoveTemp(Element));
			}
			return true;
		}
		if (const FSetProperty* AsSet = CastField<FSetProperty>(InLeaf))
		{
			// The builder refuses a set of nodes (a scratch address cannot take a node id), so the printer does too.
			if (IsNodeElement(AsSet->ElementProp))
			{
				return false;
			}
			FScriptSetHelper Helper(AsSet, InValuePtr);
			for (int32 Index = 0, Left = Helper.Num(); Left > 0; ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				--Left;
				FString Element;
				if (!PrintContainerElement(AsSet->ElementProp, Helper.GetElementPtr(Index), Element, OutValueIsUnrepresentable))
				{
					return false;
				}
				OutKeys.Add(Element);
				OutEntries.Add(MoveTemp(Element));
			}
			return true;
		}
		if (const FMapProperty* AsMap = CastField<FMapProperty>(InLeaf))
		{
			if (IsNodeElement(AsMap->KeyProp) || IsNodeElement(AsMap->ValueProp))
			{
				return false;
			}
			FScriptMapHelper Helper(AsMap, InValuePtr);
			for (int32 Index = 0, Left = Helper.Num(); Left > 0; ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				--Left;
				FString Key;
				FString Value;
				if (!PrintLiteral(AsMap->KeyProp, Helper.GetKeyPtr(Index), Key, OutValueIsUnrepresentable)
					|| !PrintContainerElement(AsMap->ValueProp, Helper.GetValuePtr(Index), Value, OutValueIsUnrepresentable))
				{
					return false;
				}
				if (CastField<FNameProperty>(AsMap->KeyProp) != nullptr)
				{
					Key = BareNameKey(Key);
				}
				OutEntries.Add(FString::Printf(TEXT("%s: %s"), *Key, *Value));
				OutKeys.Add(MoveTemp(Key));
			}
			return true;
		}
		return false;
	}

	FString JoinContainer(const FProperty* InLeaf, const TArray<FString>& InEntries)
	{
		const bool bMap = CastField<FMapProperty>(InLeaf) != nullptr;
		if (InEntries.IsEmpty())
		{
			return bMap ? TEXT("{}") : TEXT("[]");
		}
		return bMap ? FString::Printf(TEXT("{ %s }"), *FString::Join(InEntries, TEXT(", ")))
			: FString::Printf(TEXT("[%s]"), *FString::Join(InEntries, TEXT(", ")));
	}

	bool PrintLiteral(const FProperty* InLeaf, const void* InValuePtr, FString& OutText,
		bool* OutValueIsUnrepresentable)
	{
		if (InLeaf == nullptr || InValuePtr == nullptr)
		{
			return false;
		}

		// FText first, exactly as the builder reads it first: what a `.dui` records is the SOURCE
		// string, and the namespace and key are derived from the node id. Printing ToString() would
		// write the translation back into the file the moment the editor ran in another culture.
		if (const FTextProperty* AsText = CastField<FTextProperty>(InLeaf))
		{
			const FText& Value = AsText->GetPropertyValue(InValuePtr);
			const FString* Source = FTextInspector::GetSourceString(Value);
			OutText = QuoteString(Source != nullptr ? *Source : Value.ToString());
			return true;
		}

		if (CastField<FArrayProperty>(InLeaf) != nullptr || CastField<FSetProperty>(InLeaf) != nullptr
			|| CastField<FMapProperty>(InLeaf) != nullptr)
		{
			TArray<FString> Keys;
			TArray<FString> Entries;
			if (!PrintContainerEntries(InLeaf, InValuePtr, Keys, Entries, OutValueIsUnrepresentable))
			{
				return false;
			}
			OutText = JoinContainer(InLeaf, Entries);
			return true;
		}

		if (DreamUIValueFormat::HasShortForm(InLeaf))
		{
			// The short-form printer refuses exactly one thing: a non-finite component. That is the
			// VALUE kind of refusal, so it earns the flag.
			return DreamUIValueFormat::Print(InLeaf, InValuePtr, OutText, OutValueIsUnrepresentable);
		}

		if (UEnum* Enum = GetEnumForProperty(InLeaf))
		{
			const int64 Value = CastField<FEnumProperty>(InLeaf) != nullptr
				? CastField<FEnumProperty>(InLeaf)->GetUnderlyingProperty()->GetSignedIntPropertyValue(InValuePtr)
				: CastFieldChecked<FByteProperty>(InLeaf)->GetSignedIntPropertyValue(InValuePtr);
			// The short spelling -- `Left`, not `EDreamAlign::Left` -- which is what an author writes
			// and what UEnum::GetValueByNameString reads back.
			const FString Name = Enum->GetNameStringByValue(Value);
			if (!Name.IsEmpty())
			{
				OutText = Name;
				return true;
			}
			// No identifier for it: a flag combination, since the grammar has no '|'. The NUMBER is
			// still a spelling the language reads, and WriteValue validates one against this same
			// UEnum with IsValidEnumValueOrBitfield -- so a combination round trips instead of being
			// the one property class the designer could never write back at all. Refused only when
			// the number is not even a combination of declared flags, which is a value nobody can
			// spell and therefore a DUI7003.
			if (Enum->IsValidEnumValueOrBitfield(Value))
			{
				OutText = LexToString(Value);
				return true;
			}
			if (OutValueIsUnrepresentable != nullptr)
			{
				*OutValueIsUnrepresentable = true;
			}
			return false;
		}

		if (const FBoolProperty* AsBool = CastField<FBoolProperty>(InLeaf))
		{
			OutText = AsBool->GetPropertyValue(InValuePtr) ? TEXT("true") : TEXT("false");
			return true;
		}

		if (const FNumericProperty* AsNumeric = CastField<FNumericProperty>(InLeaf))
		{
			if (AsNumeric->IsFloatingPoint())
			{
				const double Value = AsNumeric->GetFloatingPointPropertyValue(InValuePtr);
				if (!FMath::IsFinite(Value))
				{
					// DUI7003: printf's "inf"/"nan" would lex as a bare identifier and fail the next
					// compile on a line nobody edited. No spelling means no edit; the row greys.
					// Flagged rather than logged from here now -- the caller has the bag and the
					// node, so the refusal reaches the Problems panel with a file and a line instead
					// of one Output Log entry an author has no reason to be reading.
					if (OutValueIsUnrepresentable != nullptr)
					{
						*OutValueIsUnrepresentable = true;
					}
					return false;
				}
				OutText = PrintScalar(Value, InLeaf->IsA<FFloatProperty>());
				return true;
			}
			// Integers have one spelling and LexToString is exact for all of them.
			OutText = AsNumeric->GetNumericPropertyValueToString(InValuePtr);
			return !OutText.IsEmpty();
		}

		if (const FStrProperty* AsStr = CastField<FStrProperty>(InLeaf))
		{
			OutText = QuoteString(AsStr->GetPropertyValue(InValuePtr));
			return true;
		}

		if (const FNameProperty* AsName = CastField<FNameProperty>(InLeaf))
		{
			// Quoted, like the builder reads it: an FName written bare would lex as an identifier and
			// then land on the enum branch of WriteValue, which has no enum to look it up in.
			OutText = QuoteString(AsName->GetPropertyValue(InValuePtr).ToString());
			return true;
		}

		// Object references, soft before hard because FSoftObjectProperty IS an FObjectPropertyBase.
		// Both print the asset path, quoted -- a path contains dots and slashes the lexer would
		// otherwise have opinions about -- and both read back through WriteValue's own branches.
		// Printing is what un-greys the font and texture rows: CanSpellAsLiteral asks by printing.
		if (const FSoftObjectProperty* AsSoft = CastField<FSoftObjectProperty>(InLeaf))
		{
			const FSoftObjectPtr& Value = AsSoft->GetPropertyValue(InValuePtr);
			if (!Value.IsNull() && !Value.ToSoftObjectPath().GetSubPathString().IsEmpty())
			{
				// A subobject, not an asset -- see the hard branch below.
				return false;
			}
			OutText = Value.IsNull() ? TEXT("None") : QuoteString(Value.ToSoftObjectPath().ToString());
			return true;
		}
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(InLeaf))
		{
			// A node reference (the widget, its visual, a behaviour) has no literal spelling from
			// HERE: its .dui form is a bare node id, which needs the tree for context this printer
			// does not have. Printing the object path instead is how
			// 'Content = "/Game/UI/....:DreamWidgetTree_0.TrackList_EachContent"' once got swept
			// into a file -- a subobject path the next compile's LoadObject cannot resolve against
			// the class being rebuilt, taking the whole hierarchy down (DUI5001 -> DUI6002).
			if (FDreamUITextBuilder::IsNodeReferenceProperty(AsObject))
			{
				// Spelled with the tree when there is one: the bare id of the node the reference points at, which the
				// builder reads back as that node, its visual or its behaviour by the property's own type. A node this
				// tree does not hold under that id -- one inside another file's component -- has no id to write here.
				const UObject* Referenced = AsObject->GetObjectPropertyValue(InValuePtr);
				if (Referenced == nullptr)
				{
					OutText = TEXT("None");
					return true;
				}
				const UDreamWidget* Node = NodeOfReferencedObject(Referenced);
				if (GPrintNodeIds == nullptr || Node == nullptr || GPrintNodeIds->FindRef(Node->GetDisplayName()) != Node)
				{
					return false;
				}
				OutText = Node->GetDisplayName();
				return true;
			}
			const UObject* Value = AsObject->GetObjectPropertyValue(InValuePtr);
			// The second fence, for object properties outside the node-reference family: a
			// subobject path is an instance's name, not an asset's, and it is not stable across
			// compiles. No spelling; the row greys, the sweep skips.
			if (Value != nullptr && !FSoftObjectPath(Value).GetSubPathString().IsEmpty())
			{
				return false;
			}
			// None bare, not quoted: it is the spelling WriteValue's null branch reads, and an FName
			// would have claimed the quotes first anyway.
			OutText = Value != nullptr ? QuoteString(FSoftObjectPath(Value).ToString()) : TEXT("None");
			return true;
		}

		return false;
	}

	/**
	 * Whether a property can take a value from text. Forwards to the builder's rule.
	 *
	 * It WAS a copy, minus the message. Three places have to agree about this -- the builder that
	 * refuses the write, the panel that greys the row, and this walk that skips the property -- and
	 * a copy that drifts offers an edit the compiler then drops, silently.
	 */
	bool IsWritableFromText(const FProperty* InProperty)
	{
		FString Unused;
		return FDreamUITextBuilder::IsWritableFromText(InProperty, Unused);
	}

	/** Where one dotted path landed: which object owns it, which leaf it is, and where the bytes are. */
	struct FResolvedValue
	{
		const UObject* Owner = nullptr;
		const FProperty* Leaf = nullptr;
		const void* ValuePtr = nullptr;
	};

	/**
	 * Walk a dotted path from the first candidate that declares its head.
	 *
	 * The candidate ORDER is the builder's (the widget, then its visual) and has to stay it: that
	 * order is what makes a bare `Text` on a Text node mean the visual's Text, and a copy that tried
	 * the visual first would compare a different property than the one the file writes.
	 */
	bool ResolveValue(const FString& InPath, TConstArrayView<const UObject*> InCandidates, FResolvedValue& OutValue)
	{
		TArray<FString> Segments;
		InPath.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			return false;
		}

		const UObject* Owner = nullptr;
		FProperty* Head = nullptr;
		for (const UObject* Candidate : InCandidates)
		{
			if (!IsValid(Candidate))
			{
				continue;
			}
			if (FProperty* Found = FindFProperty<FProperty>(Candidate->GetClass(), *Segments[0]))
			{
				Owner = Candidate;
				Head = Found;
				break;
			}
		}
		if (Head == nullptr)
		{
			return false;
		}

		const FProperty* Leaf = Head;
		const void* ValuePtr = Head->ContainerPtrToValuePtr<void>(Owner);

		for (int32 SegmentIndex = 1; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf);
			if (AsStruct == nullptr)
			{
				return false;
			}
			FProperty* Sub = FindFProperty<FProperty>(AsStruct->Struct, *Segments[SegmentIndex]);
			if (Sub == nullptr)
			{
				return false;
			}
			ValuePtr = Sub->ContainerPtrToValuePtr<void>(ValuePtr);
			Leaf = Sub;
		}

		OutValue.Owner = Owner;
		OutValue.Leaf = Leaf;
		OutValue.ValuePtr = ValuePtr;
		return true;
	}

	/**
	 * The object on InWidget a `+ Class` line built, when nothing in InClaimed already answers for it.
	 *
	 * Exact class, not IsA: two `+` blocks of related classes would otherwise both claim the first one, and the
	 * second's properties would be compared against the wrong object.
	 */
	const UObject* FindComponentObject(const UDreamWidget* InWidget, const UClass* InComponentClass,
		const TSet<const UObject*>& InClaimed)
	{
		if (!IsValid(InWidget) || InComponentClass == nullptr)
		{
			return nullptr;
		}
		const UObject* Found = nullptr;
		if (InComponentClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
		{
			Found = InWidget->GetLayoutContainer();
		}
		else if (InComponentClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
		{
			Found = InWidget->GetLayoutSelf();
		}
		else
		{
			for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (IsValid(Behaviour) && Behaviour->GetClass() == InComponentClass && !InClaimed.Contains(Behaviour))
				{
					Found = Behaviour;
					break;
				}
			}
		}
		if (Found == nullptr || Found->GetClass() != InComponentClass || InClaimed.Contains(Found))
		{
			return nullptr;
		}
		return Found;
	}

	/**
	 * The objects the node's `+` blocks produced, in the order the author wrote them.
	 *
	 * NOT UDreamWidget::GetAllComponents() read straight through, and the difference is the whole
	 * reason this exists: a panel adds the behaviours its layout requires, so the array holds objects
	 * the file never mentions and every ordinal after the first of them is off by one. Since
	 * FDreamUIPropertyEdit::ComponentIndex is defined as the index into FDreamUINode::Components --
	 * the author's count -- the walk has to start from the AST and look the object up, never the
	 * other way round.
	 *
	 * Null entries are kept rather than skipped, so the array index and the ordinal stay the same
	 * number even when one `+` block failed to resolve.
	 */
	void CollectComponentObjectsInAuthorOrder(const FDreamUINode& InNode, const UDreamWidget* InWidget,
		TArray<const UObject*>& OutObjects)
	{
		OutObjects.Reset();
		if (!IsValid(InWidget))
		{
			OutObjects.AddZeroed(InNode.Components.Num());
			return;
		}

		TSet<const UObject*> Claimed;
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			const UObject* Found = FindComponentObject(InWidget,
				FDreamUITextBuilder::ResolveComponentClass(Component.ClassName), Claimed);
			if (Found != nullptr)
			{
				Claimed.Add(Found);
			}
			OutObjects.Add(Found);
		}
	}

	/**
	 * Every authored widget of a tree, by the one name the language has.
	 *
	 * To the nested boundary and no further. A nested widget blueprint hangs its own contents off
	 * itself as children, and those widgets are named by ANOTHER file's node ids -- walking into one
	 * would pair this file's `Title` with a button's inner `Title` and write one into the other.
	 */
	void MapWidgetsByNodeId(UDreamWidgetTree* InTree, TMap<FString, UDreamWidget*>& OutMap)
	{
		OutMap.Reset();
		if (InTree == nullptr || !IsValid(InTree->RootWidget))
		{
			return;
		}

		TArray<UDreamWidget*> Widgets;
		CollectDreamWidgetsToNestedBoundary(InTree->RootWidget, Widgets);
		for (UDreamWidget* Widget : Widgets)
		{
			// A transient widget is run time's, not the file's: the copies a `for` makes of its template are the case,
			// and one that carried the template's name would be paired with the template's node in its place -- its
			// per-item values then written into the template's lines, and a copy's visibility over the template's.
			if (!IsValid(Widget) || Widget->GetDisplayName().IsEmpty() || Widget->HasAnyFlags(RF_Transient))
			{
				continue;
			}
			// First wins. Duplicate ids are DUI3001 and the file would not have parsed, so this only
			// arbitrates for a tree that came from somewhere other than a `.dui`.
			if (!OutMap.Contains(Widget->GetDisplayName()))
			{
				OutMap.Add(Widget->GetDisplayName(), Widget);
			}
		}
	}

	/** One property considered for one flush: the same path, on both trees, against one destination. */
	struct FComparison
	{
		FString NodeId;
		EDreamUIPatchTarget Target = EDreamUIPatchTarget::Node;
		int32 ComponentIndex = INDEX_NONE;
		FString PropertyName;
		TArray<const UObject*> LiveCandidates;
		TArray<const UObject*> TextCandidates;
		/** The node's own line, so a DUI7003 raised about this property names a place in the file. */
		FDreamUISourceLocation Location;
	};

	/**
	 * Decide one property, and append an edit only when the file does not already say it.
	 *
	 * THE COMPARISON IS BETWEEN TWO PRINTED FORMS, and both of them are printed here rather than one
	 * being read out of the file. That is the point of the whole design: what the file says is
	 * whatever a rebuild from it produced (InTextCandidates come from a tree built by the real
	 * builder), so `(400,240)` and `(400, 240)` are one value, a property that the file leaves to a
	 * style compares against the STYLE's value, and a property the file never mentions compares
	 * against the class default -- all without this file knowing what a style or a default is.
	 *
	 * Printed forms rather than FProperty::Identical, for two reasons that are both silent failures:
	 * FTextProperty::Identical compares text identity, not the source string, so every localised
	 * property would report a change on every flush; and FLinearColor's short form is quantised, so
	 * a picked colour is never bit-equal to the colour its own hex reads back as and would report a
	 * change forever. Printing folds both away, once.
	 */
	void CompareAndAppend(const FComparison& InComparison, TArray<FDreamUIPropertyEdit>& OutEdits,
		FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FResolvedValue Live;
		FResolvedValue Text;
		if (!ResolveValue(InComparison.PropertyName, InComparison.LiveCandidates, Live)
			|| !ResolveValue(InComparison.PropertyName, InComparison.TextCandidates, Text))
		{
			// A name the reflection of one side does not have. The builder already reported it as
			// UnknownProperty when it built the reference tree; saying it a second time here would
			// double every such message on every flush.
			return;
		}

		// Different leaves means the two sides resolved onto different objects, which can only happen
		// if the trees disagree about a widget's class. Comparing them would be comparing two
		// unrelated properties that happen to share a name.
		if (Live.Leaf != Text.Leaf || !IsWritableFromText(Live.Leaf))
		{
			return;
		}

		FString LiveText;
		FString TextText;
		bool bLiveIsUnrepresentable = false;
		bool bPrinted = false;
		{
			TGuardValue<const TMap<FString, UDreamWidget*>*> LiveIds(GPrintNodeIds, GComparisonLiveIds);
			bPrinted = PrintLiteral(Live.Leaf, Live.ValuePtr, LiveText, &bLiveIsUnrepresentable);
		}
		if (bPrinted)
		{
			TGuardValue<const TMap<FString, UDreamWidget*>*> TextIds(GPrintNodeIds, GComparisonTextIds);
			bPrinted = PrintLiteral(Text.Leaf, Text.ValuePtr, TextText);
		}
		// A set and a map are the same value in any order, and a TSet rebuilt from the file need not iterate in the
		// file's order: compared as entries, and when they do differ, written in the order the file already has them
		// with what the live value adds after -- so an edit is a change to the line, not a reshuffle of it.
		if (bPrinted && (CastField<FSetProperty>(Live.Leaf) != nullptr || CastField<FMapProperty>(Live.Leaf) != nullptr))
		{
			TArray<FString> LiveKeys;
			TArray<FString> LiveEntries;
			TArray<FString> TextKeys;
			TArray<FString> TextEntries;
			{
				TGuardValue<const TMap<FString, UDreamWidget*>*> LiveIds(GPrintNodeIds, GComparisonLiveIds);
				PrintContainerEntries(Live.Leaf, Live.ValuePtr, LiveKeys, LiveEntries, nullptr);
			}
			{
				TGuardValue<const TMap<FString, UDreamWidget*>*> TextIds(GPrintNodeIds, GComparisonTextIds);
				PrintContainerEntries(Text.Leaf, Text.ValuePtr, TextKeys, TextEntries, nullptr);
			}
			TArray<FString> SortedLive = LiveEntries;
			TArray<FString> SortedText = TextEntries;
			SortedLive.Sort();
			SortedText.Sort();
			if (SortedLive == SortedText)
			{
				return;
			}
			TArray<FString> Ordered;
			for (const FString& Key : TextKeys)
			{
				const int32 Found = LiveKeys.IndexOfByKey(Key);
				if (Found != INDEX_NONE)
				{
					Ordered.Add(LiveEntries[Found]);
				}
			}
			for (int32 Index = 0; Index < LiveKeys.Num(); ++Index)
			{
				if (!TextKeys.Contains(LiveKeys[Index]))
				{
					Ordered.Add(LiveEntries[Index]);
				}
			}
			LiveText = JoinContainer(Live.Leaf, Ordered);
		}
		if (!bPrinted)
		{
			// No spelling for this type. Leaving the line alone is the answer; see PrintLiteral.
			// The exception is a live value with no spelling of its OWN -- a NaN, an undeclared enum
			// byte. That is the code DUI7003 was reserved for and never once raised from: the row
			// greys, the drag does nothing, and until now the only trace was an Output Log line.
			// Said once per flush per property, at the node's line, and never for the ordinary
			// "this type has no literal" case, which is true of half the reflection sweep.
			if (bLiveIsUnrepresentable)
			{
				OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchValueNotRepresentable, InComparison.Location,
					FString::Printf(TEXT("'%s' on '%s' holds a value this language cannot spell (a non-finite number, or an enum byte the enum does not declare), so its line was left untouched"),
						*InComparison.PropertyName, *InComparison.NodeId));
			}
			if (GUnwritableSink != nullptr && (bLiveIsUnrepresentable || DiffersWithoutSpelling(Live.Leaf, Live.ValuePtr, Text.ValuePtr)))
			{
				GUnwritableSink->AddUnique(FString::Printf(TEXT("%s.%s"), *InComparison.NodeId, *InComparison.PropertyName));
			}
			return;
		}

		if (LiveText.Equals(TextText, ESearchCase::CaseSensitive))
		{
			return;
		}

		FDreamUIPropertyEdit& Edit = OutEdits.AddDefaulted_GetRef();
		Edit.NodeId = InComparison.NodeId;
		Edit.Target = InComparison.Target;
		Edit.ComponentIndex = InComparison.ComponentIndex;
		Edit.PropertyName = InComparison.PropertyName;
		Edit.NewValueText = MoveTemp(LiveText);
	}

	/**
	 * The `resources` block against the class defaults it was compiled into.
	 *
	 * The reference side is the entry's own literal, materialised through the LIVE property's type
	 * -- parse into a scratch value, print, compare printed forms -- so the comparison inherits every
	 * folding the node passes rely on: a colour's sRGB quantisation, a double's shortest spelling.
	 * Building a second CDO to read the reference from would be the honest-looking alternative, and
	 * it would drag a Blueprint compile into every flush.
	 *
	 * An entry with no property on the class is skipped in silence: the class has not been compiled
	 * since the entry was added, or the compile refused it -- both already reported where they
	 * happened, and neither is something a flush can act on.
	 */
	void CollectResourceEdits(const FDreamUIAst& InAst, const UObject* InLiveDefaults,
		TArray<FDreamUIPropertyEdit>& OutEdits, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		if (InLiveDefaults == nullptr)
		{
			return;
		}
		for (const FDreamUIResource& Entry : InAst.Resources)
		{
			const FProperty* Property = InLiveDefaults->GetClass()->FindPropertyByName(FName(*Entry.Name));
			if (Property == nullptr)
			{
				continue;
			}

			void* Scratch = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Scratch);

			bool bFilled = false;
			if (DreamUIValueFormat::HasShortForm(Property))
			{
				// Colour and Vector2 entries: the short-form parser reads the same literal kinds the
				// entry holds (a hex, a 2-tuple).
				bFilled = DreamUIValueFormat::Parse(Property, Entry.Value, Scratch);
			}
			else if (const FNumericProperty* AsNumeric = CastField<FNumericProperty>(Property))
			{
				double Number = 0.0;
				bFilled = AsNumeric->IsFloatingPoint() && LexTryParseString(Number, *Entry.Value.Raw);
				if (bFilled)
				{
					AsNumeric->SetFloatingPointPropertyValue(Scratch, Number);
				}
			}
			else if (const FStrProperty* AsString = CastField<FStrProperty>(Property))
			{
				AsString->SetPropertyValue(Scratch, Entry.Value.Raw);
				bFilled = true;
			}
			else if (const FSoftObjectProperty* AsSoft = CastField<FSoftObjectProperty>(Property))
			{
				AsSoft->SetPropertyValue(Scratch, FSoftObjectPtr(FSoftObjectPath(Entry.Value.Raw)));
				bFilled = true;
			}

			FString LiveText;
			FString ReferenceText;
			bool bLiveIsUnrepresentable = false;
			const bool bComparable = bFilled
				&& PrintLiteral(Property, Property->ContainerPtrToValuePtr<const void>(InLiveDefaults), LiveText,
					&bLiveIsUnrepresentable)
				&& PrintLiteral(Property, Scratch, ReferenceText);

			Property->DestroyValue(Scratch);
			FMemory::Free(Scratch);

			if (bLiveIsUnrepresentable)
			{
				// The entry's own line, which is where the reader has to go: a resource compiles into
				// a class variable, so a NaN in it came from the Class Defaults panel and the `.dui`
				// line is the thing that would have to change.
				OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchValueNotRepresentable, Entry.Location,
					FString::Printf(TEXT("resource '%s' holds a value this language cannot spell, so its line was left untouched"),
						*Entry.Name));
			}
			if (!bComparable || LiveText.Equals(ReferenceText, ESearchCase::CaseSensitive))
			{
				continue;
			}

			FDreamUIPropertyEdit& Edit = OutEdits.AddDefaulted_GetRef();
			Edit.Target = EDreamUIPatchTarget::Resource;
			Edit.PropertyName = Entry.Name;
			Edit.NewValueText = MoveTemp(LiveText);
		}
	}

	/**
	 * Whether a path, as a `.dui` writes one, names this class.
	 *
	 * Three spellings reach a Blueprint class and all three are in use: the package (`/Game/UI/WBP_Row`, what an author
	 * types and what the builder loads), the Blueprint asset (`/Game/UI/WBP_Row.WBP_Row`, what Copy Reference gives) and
	 * the generated class (`/Game/UI/WBP_Row.WBP_Row_C`). A native class has one, its script path, and a bare package
	 * there names a module, never a class.
	 */
	bool PathNamesClass(const FString& InPath, const UClass* InClass)
	{
		const FString Path = InPath.TrimStartAndEnd();
		if (InClass == nullptr || Path.IsEmpty())
		{
			return false;
		}
		const FString ClassPath = InClass->GetPathName();
		if (Path.Equals(ClassPath, ESearchCase::IgnoreCase))
		{
			return true;
		}
		FString AssetPath = ClassPath;
		if (AssetPath.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive) && Path.Equals(AssetPath, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return !ClassPath.StartsWith(TEXT("/Script/")) && !Path.Contains(TEXT("."))
			&& Path.Equals(InClass->GetPackage()->GetName(), ESearchCase::IgnoreCase);
	}

	/**
	 * The short name this file already has for a widget class, when it has one.
	 *
	 * A `use … as` alias first -- the file's own, then those its libraries brought, which is the order FindComponentAlias
	 * reads them in, and only an alias that lookup would actually answer with (a shadowed one names another class here).
	 * Then an `Asset` resource, written `@Name`, the older spelling of the same thing. A file that says `Row` for a class
	 * on fifty lines would otherwise get the class's path on the fifty-first, from the designer -- correct, and the one
	 * line in the file a reader has to stop at.
	 */
	FString FindShortNameForClass(const FDreamUIAst& InAst, const UClass* InClass)
	{
		auto AliasNamesClass = [&InAst, InClass](const FDreamUIComponentAlias& InAlias)
		{
			if (InAst.FindComponentAlias(InAlias.Alias) != &InAlias)
			{
				return false;
			}
			if (!InAlias.ClassPath.IsEmpty())
			{
				return PathNamesClass(InAlias.ClassPath, InClass);
			}
			// An alias of a .dui with no `class` line: the editor knows which Blueprint reads that file.
			const TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
			return !InAlias.SourcePath.IsEmpty() && Resolver && Resolver(InAlias.SourcePath) == InClass;
		};
		for (const FDreamUIComponentAlias& Alias : InAst.ComponentAliases)
		{
			if (AliasNamesClass(Alias))
			{
				return Alias.Alias;
			}
		}
		for (const FDreamUIComponentAlias& Alias : InAst.ImportedComponentAliases)
		{
			if (AliasNamesClass(Alias))
			{
				return Alias.Alias;
			}
		}

		// `@Name` takes one word after the '@' -- a namespaced entry (`nier.Row`) has no spelling as a type.
		auto ResourceNamesClass = [&InAst, InClass](const FDreamUIResource& InEntry)
		{
			return InEntry.TypeName.Equals(TEXT("Asset"), ESearchCase::IgnoreCase)
				&& (InEntry.Value.Kind == EDreamUIValueKind::AssetPath || InEntry.Value.Kind == EDreamUIValueKind::String)
				&& !InEntry.Name.Contains(TEXT("."))
				&& InAst.FindResource(InEntry.Name) == &InEntry
				&& PathNamesClass(InEntry.Value.Raw, InClass);
		};
		for (const FDreamUIResource& Entry : InAst.Resources)
		{
			if (ResourceNamesClass(Entry))
			{
				return TEXT("@") + Entry.Name;
			}
		}
		for (const FDreamUIResource& Entry : InAst.ImportedResources)
		{
			if (ResourceNamesClass(Entry))
			{
				return TEXT("@") + Entry.Name;
			}
		}
		return FString();
	}

	/**
	 * The node type that builds this layout container, verified against the builder's own answer, or empty.
	 *
	 * The builder's prefix search run backwards -- `DreamLayoutContainerVerticalBox` tried as `VerticalBox` -- and kept
	 * only when FindContainerClassForType answers with this very class, so a name is never written that the next
	 * compile reads as something else.
	 */
	FString FindContainerTypeName(const UClass* InContainerClass)
	{
		if (InContainerClass == nullptr)
		{
			return FString();
		}
		static const TCHAR* Prefixes[] = { TEXT("DreamLayoutContainer"), TEXT("Dream"), TEXT("UI") };
		const FString Full = InContainerClass->GetName();
		for (const TCHAR* Prefix : Prefixes)
		{
			FString Candidate = Full;
			if (Candidate.RemoveFromStart(Prefix, ESearchCase::CaseSensitive) && !Candidate.IsEmpty()
				&& FDreamUITextBuilder::FindContainerClassForType(Candidate) == InContainerClass)
			{
				return Candidate;
			}
		}
		return FDreamUITextBuilder::FindContainerClassForType(Full) == InContainerClass ? Full : FString();
	}

	/**
	 * How a `+` line names this class, verified against the builder's own reading of it, or empty.
	 *
	 * Same discipline as FindContainerTypeName: the candidates are tried and kept only when
	 * ResolveComponentClass answers with this very class, so a written line always reads back as what
	 * was meant.
	 */
	FString FindComponentSpelling(const UClass* InClass)
	{
		if (InClass == nullptr)
		{
			return FString();
		}
		const FString Container = FindContainerTypeName(InClass);
		if (!Container.IsEmpty() && FDreamUITextBuilder::ResolveComponentClass(Container) == InClass)
		{
			return Container;
		}
		const FString Full = InClass->GetName();
		TArray<FString> Candidates{ Full };
		static const TCHAR* Prefixes[] = { TEXT("DreamLayoutContainer"), TEXT("DreamLayoutSelf"), TEXT("DreamLayout"), TEXT("Dream") };
		for (const TCHAR* Prefix : Prefixes)
		{
			FString Candidate = Full;
			if (Candidate.RemoveFromStart(Prefix, ESearchCase::CaseSensitive) && !Candidate.IsEmpty())
			{
				Candidates.Add(MoveTemp(Candidate));
			}
		}
		for (const FString& Candidate : Candidates)
		{
			if (FDreamUITextBuilder::ResolveComponentClass(Candidate) == InClass)
			{
				return Candidate;
			}
		}
		return FString();
	}

	/**
	 * The objects on a widget a `+ Class { }` line builds: its behaviours, its layout container and its
	 * layout self. Not the visual or the panel slot, which the type and `@slot` lines own, and nothing
	 * transient -- run time's additions are nobody's line.
	 */
	void CollectAuthorableComponentObjects(const UDreamWidget* InWidget, TArray<const UObject*>& OutObjects)
	{
		OutObjects.Reset();
		if (!IsValid(InWidget))
		{
			return;
		}
		for (const UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			// The animation host is the animation editor's, carried across compiles by display name; the
			// grammar has no way to author what it holds, so a line for it would be an empty shell.
			if (IsValid(Behaviour) && !Behaviour->HasAnyFlags(RF_Transient)
				&& !Behaviour->IsA<UDreamWidgetAnimationComponent>())
			{
				OutObjects.Add(Behaviour);
			}
		}
		if (const UObject* Container = InWidget->GetLayoutContainer(); IsValid(Container) && !Container->HasAnyFlags(RF_Transient))
		{
			OutObjects.Add(Container);
		}
		if (const UObject* Self = InWidget->GetLayoutSelf(); IsValid(Self) && !Self->HasAnyFlags(RF_Transient))
		{
			OutObjects.Add(Self);
		}
	}

	/** The node with this id in InAst, or null -- matched as the patcher matches ids, ignoring case. */
	const FDreamUINode* FindAstNode(const FDreamUIAst& InAst, const FString& InId)
	{
		const FDreamUINode* Found = nullptr;
		if (!InId.IsEmpty())
		{
			InAst.ForEachNode([&Found, &InId](const FDreamUINode& InNode)
			{
				if (Found == nullptr && !InNode.Id.IsEmpty() && InNode.Id.Equals(InId, ESearchCase::IgnoreCase))
				{
					Found = &InNode;
				}
			});
		}
		return Found;
	}

	/**
	 * meta=(DreamGUI_Replaces="A,B"): the older properties a property replaced. Their lines go from a node when it is
	 * written there, so that the file never says one thing two ways: a rect block's gradient, written once it has stops, takes
	 * the place of its two-colour gradient's lines.
	 */
	const FName MetaReplaces(TEXT("DreamGUI_Replaces"));

	/** The property a value edit's head names, on what its node makes (the widget, its visual, its panel) or its `+` block. */
	const FProperty* FindEditedHead(const FDreamUINode& InNode, const FDreamUIPropertyEdit& InEdit, const TMap<FString, UDreamWidget*>& InLiveById)
	{
		FString Head = InEdit.PropertyName;
		int32 Dot = INDEX_NONE;
		if (Head.FindChar(TEXT('.'), Dot))
		{
			Head.LeftInline(Dot);
		}
		if (InEdit.Target == EDreamUIPatchTarget::Component)
		{
			const UClass* Class = InNode.Components.IsValidIndex(InEdit.ComponentIndex)
				? FDreamUITextBuilder::ResolveComponentClass(InNode.Components[InEdit.ComponentIndex].ClassName) : nullptr;
			return Class != nullptr ? FindFProperty<FProperty>(Class, *Head) : nullptr;
		}
		const UDreamWidget* Live = nullptr;
		for (const TPair<FString, UDreamWidget*>& Entry : InLiveById)
		{
			if (Entry.Key.Equals(InEdit.NodeId, ESearchCase::IgnoreCase))
			{
				Live = Entry.Value;
				break;
			}
		}
		if (!IsValid(Live))
		{
			return nullptr;
		}
		const UObject* Candidates[] = { Live, Live->GetVisual(), Live->GetLayoutContainer() };
		for (const UObject* Candidate : Candidates)
		{
			if (Candidate != nullptr)
			{
				if (const FProperty* Found = FindFProperty<FProperty>(Candidate->GetClass(), *Head))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	/**
	 * The lines InEdits' values replace (MetaReplaces), as removals: for each edit whose property names properties it took
	 * the place of, every line of its node -- or of its `+` block -- that writes one of them.
	 */
	void CollectReplacedLines(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree, const TArray<FDreamUIPropertyEdit>& InEdits,
		TArray<FDreamUIStructuralEdit>& OutRemovals)
	{
		TMap<FString, UDreamWidget*> LiveById;
		bool bMapped = false;
		TSet<FString> Seen;
		for (const FDreamUIPropertyEdit& Edit : InEdits)
		{
			if (Edit.Target == EDreamUIPatchTarget::Slot)
			{
				continue;
			}
			const FDreamUINode* Node = FindAstNode(InAst, Edit.NodeId);
			if (Node == nullptr)
			{
				continue;
			}
			if (!bMapped)
			{
				MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveById);
				bMapped = true;
			}
			const FProperty* Head = FindEditedHead(*Node, Edit, LiveById);
			if (Head == nullptr || !Head->HasMetaData(MetaReplaces))
			{
				continue;
			}
			const bool bComponent = Edit.Target == EDreamUIPatchTarget::Component;
			const TArray<FDreamUIProperty>* Lines = bComponent
				? (Node->Components.IsValidIndex(Edit.ComponentIndex) ? &Node->Components[Edit.ComponentIndex].Properties : nullptr)
				: &Node->Properties;
			if (Lines == nullptr)
			{
				continue;
			}
			TArray<FString> Replaced;
			Head->GetMetaData(MetaReplaces).ParseIntoArray(Replaced, TEXT(","));
			for (FString& Name : Replaced)
			{
				Name.TrimStartAndEndInline();
			}
			for (const FDreamUIProperty& Line : *Lines)
			{
				if (Line.bSynthesized || Line.bRowCell)
				{
					continue;
				}
				FString LineHead = Line.Name;
				int32 LineDot = INDEX_NONE;
				if (LineHead.FindChar(TEXT('.'), LineDot))
				{
					LineHead.LeftInline(LineDot);
				}
				if (!Replaced.ContainsByPredicate([&LineHead](const FString& InName) { return InName.Equals(LineHead, ESearchCase::IgnoreCase); }))
				{
					continue;
				}
				const FString Signature = FString::Printf(TEXT("%s|%d|%d|%s"), *Node->Id, bComponent ? 1 : 0, Edit.ComponentIndex, *Line.Name);
				if (Seen.Contains(Signature))
				{
					continue;
				}
				Seen.Add(Signature);
				FDreamUIStructuralEdit& Remove = OutRemovals.AddDefaulted_GetRef();
				Remove.Kind = EDreamUIStructuralEditKind::RemoveProperty;
				Remove.NodeId = Node->Id;
				Remove.PropertyName = Line.Name;
				Remove.PropertyTarget = bComponent ? EDreamUIPatchTarget::Component : EDreamUIPatchTarget::Node;
				Remove.ComponentIndex = bComponent ? Edit.ComponentIndex : INDEX_NONE;
			}
		}
	}

	const FDreamUINode* FindAstParentWithin(const FDreamUINode& InScope, const FDreamUINode* InChild)
	{
		for (const FDreamUINode& Child : InScope.Children)
		{
			if (&Child == InChild)
			{
				return &InScope;
			}
			if (const FDreamUINode* Found = FindAstParentWithin(Child, InChild))
			{
				return Found;
			}
		}
		return nullptr;
	}

	bool IsAddressableChild(const FDreamUINode& InNode)
	{
		return InNode.Kind == EDreamUINodeKind::Widget || InNode.Kind == EDreamUINodeKind::NamedSlot;
	}

	/** Where a live widget stands, in the terms a structural edit is placed by. */
	struct FPlacement
	{
		/** The parent's id, empty for the root. */
		FString ParentId;
		/** The named slot of a component instance the widget fills, empty for the instance's own block. */
		FString FillSlotName;
		/** The authored sibling it stands before, empty for last. */
		FString BeforeId;
		/** The block it belongs in: the parent's node, or the fill of the slot. Null when the text has none. */
		const FDreamUINode* Destination = nullptr;
	};

	/**
	 * Read off the live tree, against the text it is about to be written into: which block the widget belongs in,
	 * and which authored sibling it stands in front of there. The neighbour is the first sibling after it that the
	 * destination block declares -- a widget this same flush has not written yet, a loop's copies and the builder's
	 * own widgets are skipped -- so the place is one the text can say.
	 */
	FPlacement ResolvePlacement(const UDreamWidget* InLiveWidget, const UDreamWidgetTree* InLiveTree, const FDreamUIAst& InAst)
	{
		FPlacement Placement;
		const UDreamWidget* Parent = IsValid(InLiveWidget) ? InLiveWidget->GetParent() : nullptr;
		if (!IsValid(Parent))
		{
			return Placement;
		}
		Placement.ParentId = Parent == InLiveTree->RootWidget ? FString() : Parent->GetDisplayName();
		// Content of a component instance hangs off the instance whichever slot it fills; the slot is in the instance's
		// NamedSlotContent, and only for a NAMED slot -- the default one is nesting alone. Written as an ordinary child,
		// named-slot content would be default-slot content on the next compile.
		if (const UDreamUserWidget* Instance = Cast<UDreamUserWidget>(Parent))
		{
			TArray<FName> SlotNames;
			UDreamUserWidget::CollectDeclaredSlotNames(Instance->GetClass(), SlotNames);
			const FName DefaultSlot = Instance->GetDefaultSlotName();
			for (const FName& SlotName : SlotNames)
			{
				if (SlotName != DefaultSlot && Instance->GetContentForNamedSlot(SlotName) == InLiveWidget)
				{
					Placement.FillSlotName = SlotName.ToString();
					break;
				}
			}
		}
		const FDreamUINode* ParentNode = Placement.ParentId.IsEmpty()
			? (InAst.bHasRoot ? &InAst.Root : nullptr) : FindAstNode(InAst, Placement.ParentId);
		Placement.Destination = ParentNode;
		if (ParentNode != nullptr && !Placement.FillSlotName.IsEmpty())
		{
			Placement.Destination = ParentNode->Children.FindByPredicate([&Placement](const FDreamUINode& InChild)
			{
				return InChild.Kind == EDreamUINodeKind::NamedSlot && InChild.bFillsSlot
					&& InChild.Id.Equals(Placement.FillSlotName, ESearchCase::IgnoreCase);
			});
		}
		if (Placement.Destination == nullptr)
		{
			return Placement;
		}
		const TArray<UDreamWidget*>& Siblings = Parent->GetChildren();
		const int32 Index = Siblings.IndexOfByKey(InLiveWidget);
		for (int32 Next = Index + 1; Index != INDEX_NONE && Next < Siblings.Num(); ++Next)
		{
			const UDreamWidget* Sibling = Siblings[Next];
			if (!IsValid(Sibling) || Sibling->HasAnyFlags(RF_Transient))
			{
				continue;
			}
			const FString SiblingId = Sibling->GetDisplayName();
			if (Placement.Destination->Children.ContainsByPredicate([&SiblingId](const FDreamUINode& InChild)
			{
				return IsAddressableChild(InChild) && InChild.Id.Equals(SiblingId, ESearchCase::IgnoreCase);
			}))
			{
				Placement.BeforeId = SiblingId;
				break;
			}
		}
		return Placement;
	}

	/** Whether the text already has InNode where Placement says it stands: same block, same next sibling. */
	bool IsAlreadyPlaced(const FDreamUIAst& InAst, const FDreamUINode& InNode, const FPlacement& InPlacement)
	{
		const FDreamUINode* TextParent = InAst.bHasRoot ? FindAstParentWithin(InAst.Root, &InNode) : nullptr;
		if (TextParent == nullptr || TextParent != InPlacement.Destination)
		{
			return false;
		}
		const int32 Index = TextParent->Children.IndexOfByPredicate([&InNode](const FDreamUINode& InChild) { return &InChild == &InNode; });
		FString NextId;
		for (int32 Next = Index + 1; Index != INDEX_NONE && Next < TextParent->Children.Num(); ++Next)
		{
			if (IsAddressableChild(TextParent->Children[Next]))
			{
				NextId = TextParent->Children[Next].Id;
				break;
			}
		}
		return NextId.Equals(InPlacement.BeforeId, ESearchCase::IgnoreCase);
	}

	/**
	 * How a `.dui` would write this widget's TYPE: the file's own short name for the class, a built-in tag, a layout
	 * container, or the class path it nests.
	 *
	 * Through FDreamUITextBuilder::GetVisualTags rather than a table of its own, for the reason that
	 * function was exported: the completion list, the compiler and this all have to offer exactly the
	 * tags the builder accepts, and a second copy is how "the designer wrote RectBlock and the
	 * compile rejected it" happens. Container names go through FindContainerClassForType for the same
	 * reason.
	 */
	FString DescribeWidgetForText(const UDreamWidget* InWidget, const FDreamUIAst& InAst)
	{
		if (!IsValid(InWidget))
		{
			return FString();
		}
		if (InWidget->IsA<UDreamUserWidget>())
		{
			const FString ShortName = FindShortNameForClass(InAst, InWidget->GetClass());
			if (!ShortName.IsEmpty())
			{
				return ShortName;
			}
			// A native control by its tag -- `Native.Button` -- when one is declared: a class path would
			// name the module the class happens to live in, and the file written today has to read back
			// after the class has moved to another. Nothing but a tag is stable across that.
			if (InWidget->GetClass()->HasAnyClassFlags(CLASS_Native))
			{
				const FString Tag = FDreamUIWidgetRegistry::FindTagForClass(InWidget->GetClass());
				if (Tag.Contains(TEXT(".")))
				{
					return Tag;
				}
			}
			// A nested widget blueprint is named by its asset path, `_C` stripped -- the spelling the
			// parser reads back and ResolveNodeClasses loads.
			FString Path = InWidget->GetClass()->GetPathName();
			Path.RemoveFromEnd(TEXT("_C"));
			return Path;
		}
		const UDreamVisual* Visual = InWidget->GetVisual();
		const UClass* VisualClass = Visual != nullptr ? Visual->GetClass() : nullptr;
		if (VisualClass == nullptr)
		{
			// A plain widget that lays its children out -- what the palette's Vertical Box makes -- is written as the
			// container (`VerticalBox Column { }`), which is the one spelling that keeps the container: `Widget Column`
			// would build a widget without it, and the value pass would then have no object to write its spacing onto.
			if (const UDreamLayoutContainer* Container = InWidget->GetLayoutContainer())
			{
				const FString ContainerType = FindContainerTypeName(Container->GetClass());
				if (!ContainerType.IsEmpty())
				{
					return ContainerType;
				}
			}
		}
		TArray<TPair<FString, UClass*>> Tags;
		FDreamUITextBuilder::GetVisualTags(Tags);
		for (const TPair<FString, UClass*>& Tag : Tags)
		{
			if (Tag.Value == VisualClass)
			{
				return Tag.Key;
			}
		}
		// A visual no tag names. `Widget` is wrong about the visual and right about everything else,
		// and the compile will say so on the line this produced rather than silently building
		// something different -- which is the better of the two ways to be wrong here.
		return TEXT("Widget");
	}

	/** Add a property path once, keeping the order it was first seen in. */
	void AddCandidateName(const FString& InName, TArray<FString>& InOutNames, TSet<FString>& InOutSeen)
	{
		if (InName.IsEmpty() || InOutSeen.Contains(InName))
		{
			return;
		}
		InOutSeen.Add(InName);
		InOutNames.Add(InName);
	}
}

// -------------------------------------------------------------------------------------------------
// The pure half
// -------------------------------------------------------------------------------------------------

namespace DreamUIWriteBackDirtyLocal
{
	/**
	 * One property, spelled the way both sides can agree on: the node id and the property's HEAD --
	 * the first segment of its path. The reporters know the property they migrated (`AnchorData`,
	 * `Padding`, `Brush`), never which leaf of it changed; the flush asks about leaves
	 * (`AnchorData.SizeDelta`, `Padding.Left`, `Brush.TintColor`). Keyed on the leaf, a report and a
	 * question about the same edit never met, and every struct-valued edit the designer made was
	 * filtered out of the file and reverted by the next compile.
	 *
	 * Nor which object on the node: a slot's property is reported from the slot, which the details
	 * panel resolves to the node, and a behaviour by its place among the widget's components, which
	 * is not its place among the file's `+` lines. The node and the head are what both sides know.
	 * Wider than it was by the object on the node, which costs nothing the set exists for: what it
	 * keeps out is a value that differs because a layout computed it, and that is never under the
	 * head of a property somebody edited.
	 *
	 * A string rather than a struct with a hash, because the set is tiny (a gesture dirties one to
	 * three properties), it is read once per flush, and the alternative is a GetTypeHash nobody would
	 * ever have a reason to read.
	 */
	FString MakeKey(const FString& InNodeId, EDreamUIPatchTarget /*InTarget*/, int32 /*InComponentIndex*/,
		const FString& InPropertyName)
	{
		int32 Dot = INDEX_NONE;
		const FString Head = InPropertyName.FindChar(TEXT('.'), Dot) ? InPropertyName.Left(Dot) : InPropertyName;
		return FString::Printf(TEXT("%s|%s"), *InNodeId, *Head);
	}

	/**
	 * Per tree, weakly keyed, pruned as it is read.
	 *
	 * Weak because nothing here should keep an authoring tree alive: a designer that closes drops its
	 * tree and this map must not be the reason it survives. Pruning on read rather than on a callback
	 * for the same reason the document registry prunes in NumTracked -- there is no moment anybody
	 * could be trusted to call.
	 */
	TMap<TWeakObjectPtr<const UDreamWidgetTree>, TSet<FString>>& GetDirtyMap()
	{
		static TMap<TWeakObjectPtr<const UDreamWidgetTree>, TSet<FString>> Map;
		for (auto It = Map.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		return Map;
	}
}

bool FDreamUITextWriteBack::CanSpellAsLiteral(const FProperty* InLeaf, const void* InValuePtr)
{
	// Printing IS the question -- there is no cheaper predicate that stays true, because whether a
	// value has a spelling depends on the value for colours and on the type for everything else.
	FString Unused;
	return InLeaf != nullptr && InValuePtr != nullptr
		&& DreamUIWriteBackLocal::PrintLiteral(InLeaf, InValuePtr, Unused);
}

UDreamWidgetTree* FDreamUITextWriteBack::BuildReferenceTree(const FString& InText, FDreamUIAst& OutAst,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	// With the real import reader, deliberately: the readerless overload makes every `use` line an
	// ImportFailed, so the pre-flight parse of any file that imports its styles "failed" and the
	// write-back refused to write -- a details edit on such a file went nowhere, with only a log
	// line to say so. The compiler and the watcher parse with this same reader; the reference tree
	// must see the file the way they do.
	if (!FDreamUISourceFile::Parse(InText, OutDiagnostics.SourceName, OutAst, OutDiagnostics,
		FDreamUISourceFile::MakeFileImportReader()))
	{
		return nullptr;
	}

	// Discarded on purpose. Bindings belong to the compiler, and the only thing they mean here is
	// "this property has no literal in the file" -- which the tree already expresses, by holding the
	// class default for it.
	TArray<FDreamWidgetPropertyBinding> Bindings;
	// The `each` sink is NOT discardable, and leaving it null is what made a designer edit inside a
	// loop body disappear. Without it BuildEachLoop never runs: the block degrades to a DUI5007 that
	// this bag swallows, the template widget is never constructed, and CollectEdits -- which walks
	// loop bodies like any other children -- finds the node in the AST, finds it in the LIVE tree,
	// and finds nothing in this one, so it returns before producing a single edit. Every cell
	// property the author dragged looked applied in the preview and was written back over by the
	// next compile. The builder is given the same sink the real compile gives it, so the reference
	// tree is the same tree; the bindings themselves are dropped here like the property ones.
	TArray<FDreamWidgetEachBinding> EachBindings;
	return FDreamUITextBuilder::Build(OutAst, GetTransientPackage(), OutDiagnostics, Bindings,
		/*OutEventBindings*/nullptr, &EachBindings);
}

void FDreamUITextWriteBack::CollectEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
	const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits,
	FDreamUIDiagnosticBag& OutDiagnostics, bool bInUseDirtySet, const TSet<FString>* InSweepNodeIds,
	TArray<FDreamUIStructuralEdit>* OutStyleOverrides)
{
	using namespace DreamUIWriteBackLocal;

	if (InLiveTree == nullptr || InTextTree == nullptr || !InAst.bHasRoot)
	{
		return;
	}

	// What the designer says it touched, when anything did. Null means nobody reported, and then the
	// sweep runs exactly as it always has -- see NoteDirtyProperty for why that is the fallback and
	// not the bug.
	const TSet<FString>* Dirty = bInUseDirtySet
		? DreamUIWriteBackDirtyLocal::GetDirtyMap().Find(InLiveTree) : nullptr;
	if (Dirty != nullptr && Dirty->IsEmpty())
	{
		Dirty = nullptr;
	}
	auto KeepName = [Dirty, InSweepNodeIds](const FString& InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const FString& InName)
	{
		// After a change of shape: the nodes just written are swept whole, the rest say only what was
		// reported. See the header for why there is no sweep to fall back on here.
		if (InSweepNodeIds != nullptr)
		{
			return InSweepNodeIds->Contains(InNodeId)
				|| (Dirty != nullptr
					&& Dirty->Contains(DreamUIWriteBackDirtyLocal::MakeKey(InNodeId, InTarget, InComponentIndex, InName)));
		}
		return Dirty == nullptr
			|| Dirty->Contains(DreamUIWriteBackDirtyLocal::MakeKey(InNodeId, InTarget, InComponentIndex, InName));
	};

	// Const only ever came off a read: the collectors and FindFProperty want non-const containers,
	// and nothing below writes to either tree. The trees are the caller's and stay untouched.
	TMap<FString, UDreamWidget*> LiveWidgets;
	TMap<FString, UDreamWidget*> TextWidgets;
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveWidgets);
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InTextTree), TextWidgets);
	// For a node reference to be printed as the id of the node it points at, on each side (see GPrintNodeIds).
	TGuardValue<const TMap<FString, UDreamWidget*>*> LiveIdsForThisPass(GComparisonLiveIds, &LiveWidgets);
	TGuardValue<const TMap<FString, UDreamWidget*>*> TextIdsForThisPass(GComparisonTextIds, &TextWidgets);

	// What FindContainerClassForType answered per node type, for this one walk: it is a class search, and a file names
	// the same handful of types on every row.
	TMap<FString, UClass*> ContainerClassesByType;
	auto ContainerClassFor = [&ContainerClassesByType](const FString& InTypeName) -> UClass*
	{
		if (UClass* const* Found = ContainerClassesByType.Find(InTypeName))
		{
			return *Found;
		}
		return ContainerClassesByType.Add(InTypeName, FDreamUITextBuilder::FindContainerClassForType(InTypeName));
	};

	InAst.ForEachNode([&](const FDreamUINode& InNode)
	{
		// A loop node is not built at all -- its body's widgets are the template's, which ForEachNode
		// reaches on their own. A slot DECLARATION is a widget of this file like any other (a bare one
		// has no block, which the patcher says if anything on it was edited); a slot FILL is not: its
		// id pairs with the slot widget the COMPONENT declares, whose values are that file's to write.
		const bool bWidgetOfThisFile = InNode.Kind == EDreamUINodeKind::Widget
			|| (InNode.Kind == EDreamUINodeKind::NamedSlot && !InNode.bFillsSlot);
		if (!bWidgetOfThisFile || InNode.Id.IsEmpty())
		{
			return;
		}

		UDreamWidget* const* LiveFound = LiveWidgets.Find(InNode.Id);
		UDreamWidget* const* TextFound = TextWidgets.Find(InNode.Id);
		if (LiveFound == nullptr || TextFound == nullptr)
		{
			// The live tree has no widget for this node. Normal for a designer that has not
			// regenerated yet, and for anything the builder dropped; either way there is no value to
			// mirror and inventing one would write the reference tree's own defaults into the file.
			return;
		}

		UDreamWidget* LiveWidget = *LiveFound;
		UDreamWidget* TextWidget = *TextFound;
		if (!IsValid(LiveWidget) || !IsValid(TextWidget) || LiveWidget->GetClass() != TextWidget->GetClass())
		{
			return;
		}

		// The objects the node's own `+` lines built, paired by the author's count. Three passes below
		// need to know which objects those are: the `+` pass writes them, and the container and style
		// passes must leave them to it.
		TArray<const UObject*> LiveComponentObjects;
		TArray<const UObject*> TextComponentObjects;
		CollectComponentObjectsInAuthorOrder(InNode, LiveWidget, LiveComponentObjects);
		CollectComponentObjectsInAuthorOrder(InNode, TextWidget, TextComponentObjects);

		// A layout container the node's TYPE names (`VerticalBox Column { Spacing = 29 }`): its lines
		// are the node's own, bare, so its values are compared in the bare pass and written as bare
		// lines -- there is no `+` block to write them into, and writing one would be a second
		// container (DUI5022). Only when it really is the container the type built, on both trees.
		const UObject* LiveTypeContainer = nullptr;
		const UObject* TextTypeContainer = nullptr;
		if (InNode.Kind == EDreamUINodeKind::Widget)
		{
			if (const UClass* TypeContainerClass = ContainerClassFor(InNode.TypeName))
			{
				const UDreamLayoutContainer* LiveContainer = LiveWidget->GetLayoutContainer();
				const UDreamLayoutContainer* TextContainer = TextWidget->GetLayoutContainer();
				if (LiveContainer != nullptr && TextContainer != nullptr
					&& LiveContainer->GetClass() == TypeContainerClass && TextContainer->GetClass() == TypeContainerClass
					&& !LiveComponentObjects.Contains(LiveContainer))
				{
					LiveTypeContainer = LiveContainer;
					TextTypeContainer = TextContainer;
				}
			}
		}

		TArray<const FDreamUIStyle*> StyleChain;
		// The builder's order (FDreamUIAst::CollectStyleChain); empty on a cycle, where the builder applies nothing either.
		InAst.CollectStyleChain(InNode.StyleRefs, StyleChain);

		// ---- bare `Name = Value`: the widget, its visual, the container its type names ----------
		{
			FComparison Comparison;
			Comparison.NodeId = InNode.Id;
			Comparison.Location = InNode.Location;
			Comparison.Target = EDreamUIPatchTarget::Node;
			// The builder's order, which decides where a name lands when two of them declare it.
			Comparison.LiveCandidates = { LiveWidget, LiveWidget->GetVisual(), LiveTypeContainer };
			Comparison.TextCandidates = { TextWidget, TextWidget->GetVisual(), TextTypeContainer };

			TArray<FString> Names;
			TSet<FString> Seen;
			// The style's names count as names the file mentions for this node, so a designer edit to
			// a styled property produces an override ON THE NODE. Never a write into the style: that
			// block is shared, and one drag would move every other node using it.
			for (const FDreamUIStyle* Style : StyleChain)
			{
				for (const FDreamUIProperty& Property : Style->Properties)
				{
					AddCandidateName(Property.Name, Names, Seen);
				}
			}
			for (const FDreamUIProperty& Property : InNode.Properties)
			{
				// A `<-` line is offered too, and deliberately. Its reference value is the class
				// default (the builder writes no value for a binding), so an untouched bound property
				// produces nothing -- and an edited one produces an edit the patcher refuses by name,
				// which is the only way the user ever hears that their drag did not stick.
				AddCandidateName(Property.Name, Names, Seen);
			}
			// Every property the policy can write, on the widget and on its visual -- derived from
			// reflection, so a property added to either class next month is synced the day it exists
			// instead of waiting for somebody to extend a list. The file-written and style names above
			// still go first, so a line the author placed keeps its own spot.
			for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(LiveWidget->GetClass()))
			{
				AddCandidateName(Path, Names, Seen);
			}
			if (const UDreamVisual* Visual = LiveWidget->GetVisual())
			{
				for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(Visual->GetClass()))
				{
					AddCandidateName(Path, Names, Seen);
				}
			}
			if (LiveTypeContainer != nullptr)
			{
				for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(LiveTypeContainer->GetClass()))
				{
					AddCandidateName(Path, Names, Seen);
				}
			}

			// `Shown` and Visibility are ONE value under two names: Shown keeps nothing of its own, it
			// reads and writes Visibility. Compared under both, one hidden widget is two edits -- a
			// `Visibility = Collapsed` and a `Shown = false` written one under the other, each
			// restating the other. The sweep never lists Shown (it is DuiHidden for that reason), but
			// a file that spells it puts it in the list above by name. So exactly one face is
			// compared: Shown where the file spells it (`Shown = false`, `Shown <- HasSave()`, or the
			// `Shown` an `if` gives its branches, which the patcher then refuses with the `if`'s
			// line), Visibility everywhere else.
			auto SpellsShown = [](const TArray<FDreamUIProperty>& InProperties)
			{
				return InProperties.ContainsByPredicate([](const FDreamUIProperty& InProperty) { return InProperty.Name == TEXT("Shown"); });
			};
			bool bFileSpellsShown = SpellsShown(InNode.Properties);
			for (const FDreamUIStyle* Style : StyleChain)
			{
				bFileSpellsShown |= SpellsShown(Style->Properties);
			}
			const FString SkippedFace = bFileSpellsShown ? TEXT("Visibility") : TEXT("Shown");

			for (const FString& Name : Names)
			{
				if (Name == SkippedFace)
				{
					continue;
				}
				// The dirty set narrows the sweep when somebody reported; see NoteDirtyProperty. The
				// list above is still built in full, so the ORDER a name would be written in does not
				// depend on what happened to be touched this session. A report of either face of the
				// visibility keeps the face that is compared: the details panel reports Visibility,
				// and a node that spells Shown is asked about Shown.
				const bool bReported = KeepName(InNode.Id, EDreamUIPatchTarget::Node, INDEX_NONE, Name)
					|| (Name == TEXT("Shown") && KeepName(InNode.Id, EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Visibility")))
					|| (Name == TEXT("Visibility") && KeepName(InNode.Id, EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Shown")));
				if (!bReported)
				{
					continue;
				}
				Comparison.PropertyName = Name;
				CompareAndAppend(Comparison, OutEdits, OutDiagnostics);
			}
		}

		// ---- `@slot Name = Value`: the panel slot the PARENT's layout handed out -----------------
		//
		// Not gated on the node having written any: the slot is where alignment and padding live, and
		// those are the controls a designer reaches for FIRST -- on a node whose text never mentioned
		// them. Comparing only what the file already says would mean "you can change a value the file
		// mentions, and silently lose one it does not", which is the worse half of both worlds.
		//
		// This mirrors what the bare-name pass does with GetGeometryPropertyPaths: an unconditional
		// set of the properties a designer edits, so an untouched one produces nothing and an edited
		// one produces an inserted line.
		if (IsValid(LiveWidget->GetPanelSlot()) && IsValid(TextWidget->GetPanelSlot()))
		{
			FComparison Comparison;
			Comparison.NodeId = InNode.Id;
			Comparison.Location = InNode.Location;
			Comparison.Target = EDreamUIPatchTarget::Slot;
			Comparison.LiveCandidates = { LiveWidget->GetPanelSlot() };
			Comparison.TextCandidates = { TextWidget->GetPanelSlot() };

			TArray<FString> Names;
			TSet<FString> Seen;
			for (const FDreamUIProperty& Property : InNode.SlotProperties)
			{
				AddCandidateName(Property.Name, Names, Seen);
			}
			for (const FString& Name : DreamUIReflection::GetWritableLeafPaths(LiveWidget->GetPanelSlot()->GetClass()))
			{
				AddCandidateName(Name, Names, Seen);
			}
			for (const FString& Name : Names)
			{
				if (!KeepName(InNode.Id, EDreamUIPatchTarget::Slot, INDEX_NONE, Name))
				{
					continue;
				}
				Comparison.PropertyName = Name;
				CompareAndAppend(Comparison, OutEdits, OutDiagnostics);
			}
		}

		// ---- `+ Class { … }`: one destination per authored block, by ordinal --------------------
		if (InNode.Components.Num() > 0)
		{
			const TArray<const UObject*>& LiveObjects = LiveComponentObjects;
			const TArray<const UObject*>& TextObjects = TextComponentObjects;

			for (int32 ComponentIndex = 0; ComponentIndex < InNode.Components.Num(); ++ComponentIndex)
			{
				if (!LiveObjects.IsValidIndex(ComponentIndex) || !TextObjects.IsValidIndex(ComponentIndex)
					|| LiveObjects[ComponentIndex] == nullptr || TextObjects[ComponentIndex] == nullptr)
				{
					continue;
				}

				FComparison Comparison;
				Comparison.NodeId = InNode.Id;
				Comparison.Location = InNode.Location;
				Comparison.Target = EDreamUIPatchTarget::Component;
				Comparison.ComponentIndex = ComponentIndex;
				Comparison.LiveCandidates = { LiveObjects[ComponentIndex] };
				Comparison.TextCandidates = { TextObjects[ComponentIndex] };

				TArray<FString> Names;
				TSet<FString> Seen;
				for (const FDreamUIProperty& Property : InNode.Components[ComponentIndex].Properties)
				{
					AddCandidateName(Property.Name, Names, Seen);
				}
				// The sweep, which this pass never had: components used to compare only what the file
				// already wrote, so the FIRST edit of any behaviour property was the one that died --
				// the exact off-by-one the slot pass's comment warned about, standing one block away.
				for (const FString& Name : DreamUIReflection::GetWritableLeafPaths(LiveObjects[ComponentIndex]->GetClass()))
				{
					AddCandidateName(Name, Names, Seen);
				}
				for (const FString& Name : Names)
				{
					// A `+` line the shape pass has just written is swept whole, as a new node is.
					if (!KeepName(InNode.Id, EDreamUIPatchTarget::Component, ComponentIndex, Name)
						&& !(InSweepNodeIds != nullptr
							&& InSweepNodeIds->Contains(InNode.Id + TEXT("+") + InNode.Components[ComponentIndex].ClassName)))
					{
						continue;
					}
					Comparison.PropertyName = Name;
					CompareAndAppend(Comparison, OutEdits, OutDiagnostics);
				}
			}
		}

		// ---- `+ Class { … }` lines of the STYLE: compared, never written ------------------------
		//
		// A style may carry components (`style RowColumn { + VerticalBox { Spacing = 15 } }`), and the
		// node wearing it has the objects they built without a line of its own for them. Untouched, both
		// trees agree -- the reference tree is built by the same builder from the same style -- so
		// nothing is written, which is the duplication this pass must never cause. Edited, the value
		// has no home this pass may write: the style is shared, and a `+` block of the node's own is a
		// change of shape. So the difference is SAID (DUI7005), with the line that would hold it.
		//
		// A component the node also writes is the same object (the style's values first, the node's
		// after) and is the `+` pass's above; the container the node's type names is the bare pass's.
		if (StyleChain.Num() > 0)
		{
			TSet<const UObject*> LiveClaimed;
			TSet<const UObject*> TextClaimed;
			LiveClaimed.Append(LiveComponentObjects);
			TextClaimed.Append(TextComponentObjects);
			if (LiveTypeContainer != nullptr)
			{
				LiveClaimed.Add(LiveTypeContainer);
				TextClaimed.Add(TextTypeContainer);
			}
			for (const FDreamUIStyle* Style : StyleChain)
			{
				for (const FDreamUIComponent& Component : Style->Components)
				{
					const UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
					const UObject* LiveObject = FindComponentObject(LiveWidget, ComponentClass, LiveClaimed);
					const UObject* TextObject = FindComponentObject(TextWidget, ComponentClass, TextClaimed);
					if (LiveObject == nullptr || TextObject == nullptr)
					{
						continue;
					}
					LiveClaimed.Add(LiveObject);
					TextClaimed.Add(TextObject);

					FComparison Comparison;
					Comparison.NodeId = InNode.Id;
					Comparison.Location = InNode.Location;
					Comparison.Target = EDreamUIPatchTarget::Component;
					Comparison.LiveCandidates = { LiveObject };
					Comparison.TextCandidates = { TextObject };

					TArray<FDreamUIPropertyEdit> Differences;
					for (const FString& Name : DreamUIReflection::GetWritableLeafPaths(LiveObject->GetClass()))
					{
						if (!KeepName(InNode.Id, EDreamUIPatchTarget::Component, INDEX_NONE, Name))
						{
							continue;
						}
						Comparison.PropertyName = Name;
						CompareAndAppend(Comparison, Differences, OutDiagnostics);
					}
					if (OutStyleOverrides != nullptr && !Differences.IsEmpty())
					{
						// The node's own `+ Class` line is where its value goes: the builder applies the style's line and
						// then the node's to the same object, so a line on the node overrides for this node alone.
						const bool bAsked = OutStyleOverrides->ContainsByPredicate([&InNode, &Component](const FDreamUIStructuralEdit& InEdit)
						{
							return InEdit.NodeId == InNode.Id && InEdit.ComponentClassName == Component.ClassName;
						});
						if (!bAsked)
						{
							FDreamUIStructuralEdit& Override = OutStyleOverrides->AddDefaulted_GetRef();
							Override.Kind = EDreamUIStructuralEditKind::InsertComponent;
							Override.NodeId = InNode.Id;
							Override.ComponentClassName = Component.ClassName;
						}
						continue;
					}
					for (const FDreamUIPropertyEdit& Difference : Differences)
					{
						OutDiagnostics.AddError(EDreamUIDiagnosticCode::PatchStyleComponentNotWritable, InNode.Location,
							FString::Printf(TEXT("'%s' on '%s' comes from the '+ %s' of style '%s' (line %d), which is shared, and the node has no '+ %s' of its own to hold the change: write '+ %s { %s = %s }' on the node, or change the style"),
								*Difference.PropertyName, *InNode.Id, *Component.ClassName, *Style->Name, Component.Location.Line,
								*Component.ClassName, *Component.ClassName, *Difference.PropertyName, *Difference.NewValueText));
					}
				}
			}
		}
	});

	// Almost silent, and the exception is the one refusal this pass genuinely owns. Everything else
	// worth saying is already said on one side of it or the other: the builder reported the unknown
	// properties and unloadable assets while making the reference tree, and the patcher reports every
	// refusal by name and location while applying what comes out. A complaint raised here for those
	// would be a third voice for causes those two already own, and it would repeat on every flush for
	// the life of the file.
	//
	// DUI7003 is not one of those. "The live value has no spelling in this language" is visible ONLY
	// here -- the builder never saw the value and the patcher is never handed an edit for it -- which
	// is why the code sat declared, documented and raised from nowhere while a NaN in a property just
	// greyed a row. See CompareAndAppend.
}

// -------------------------------------------------------------------------------------------------
// The dirty set, and the structural half of a flush
// -------------------------------------------------------------------------------------------------

void FDreamUITextWriteBack::NoteDirtyProperty(const UDreamWidgetTree* InTree, const FString& InNodeId,
	EDreamUIPatchTarget InTarget, int32 InComponentIndex, const FString& InPropertyName)
{
	if (InTree == nullptr || InNodeId.IsEmpty() || InPropertyName.IsEmpty())
	{
		return;
	}
	DreamUIWriteBackDirtyLocal::GetDirtyMap().FindOrAdd(InTree).Add(
		DreamUIWriteBackDirtyLocal::MakeKey(InNodeId, InTarget, InComponentIndex, InPropertyName));
}

void FDreamUITextWriteBack::ClearDirtyProperties(const UDreamWidgetTree* InTree)
{
	if (InTree != nullptr)
	{
		DreamUIWriteBackDirtyLocal::GetDirtyMap().Remove(InTree);
	}
}

namespace DreamUIWriteBackQueueLocal
{
	/** Per tree, weakly keyed, for the reasons GetDirtyMap gives. In the order the edits were made. */
	TMap<TWeakObjectPtr<const UDreamWidgetTree>, TArray<FDreamUIStructuralEdit>>& GetQueueMap()
	{
		static TMap<TWeakObjectPtr<const UDreamWidgetTree>, TArray<FDreamUIStructuralEdit>> Map;
		for (auto It = Map.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		return Map;
	}
}

void FDreamUITextWriteBack::QueueStructuralEdit(const UDreamWidgetTree* InTree, const FDreamUIStructuralEdit& InEdit)
{
	if (InTree != nullptr)
	{
		DreamUIWriteBackQueueLocal::GetQueueMap().FindOrAdd(InTree).Add(InEdit);
	}
}

void FDreamUITextWriteBack::ClearQueuedStructuralEdits(const UDreamWidgetTree* InTree)
{
	if (InTree != nullptr)
	{
		DreamUIWriteBackQueueLocal::GetQueueMap().Remove(InTree);
	}
}

int32 FDreamUITextWriteBack::NumQueuedStructuralEdits(const UDreamWidgetTree* InTree)
{
	const TArray<FDreamUIStructuralEdit>* Found = InTree != nullptr
		? DreamUIWriteBackQueueLocal::GetQueueMap().Find(InTree) : nullptr;
	return Found != nullptr ? Found->Num() : 0;
}

int32 FDreamUITextWriteBack::NumDirtyProperties(const UDreamWidgetTree* InTree)
{
	const TSet<FString>* Found = InTree != nullptr
		? DreamUIWriteBackDirtyLocal::GetDirtyMap().Find(InTree) : nullptr;
	return Found != nullptr ? Found->Num() : 0;
}

void FDreamUITextWriteBack::CollectStructuralEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
	const UDreamWidgetTree* InTextTree, TArray<FDreamUIStructuralEdit>& OutEdits,
	bool bInIncludeRemovals, const TSet<FString>* InSkipInsertIds, const TSet<FString>* InComponentCheckIds,
	TSet<FString>* OutSweepKeys, TArray<FString>* OutDropped, TArray<FString>* OutUnsettled, const TSet<FString>* InAttempted)
{
	using namespace DreamUIWriteBackLocal;

	if (InLiveTree == nullptr || InTextTree == nullptr || !InAst.bHasRoot)
	{
		return;
	}

	TMap<FString, UDreamWidget*> LiveWidgets;
	TMap<FString, UDreamWidget*> TextWidgets;
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveWidgets);
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InTextTree), TextWidgets);

	// Only nodes the FILE declares can be removed or reordered: a widget the builder synthesised is
	// in both trees and is nobody's line to move.
	TSet<FString> AuthoredIds;
	InAst.ForEachNode([&AuthoredIds](const FDreamUINode& InNode)
	{
		if (InNode.Kind != EDreamUINodeKind::Widget && InNode.Kind != EDreamUINodeKind::NamedSlot)
		{
			return;
		}
		if (!InNode.Id.IsEmpty())
		{
			AuthoredIds.Add(InNode.Id);
		}
	});

	// ---- removed: in the file, built into the reference tree, gone from the live one --------------
	//
	// Outermost only. A node's text holds its children's, so removing a subtree is ONE removal: a second
	// one for a child would cover characters the first already takes, and the patcher refuses the two
	// as a pair -- which is how deleting anything with children used to write nothing at all.
	if (bInIncludeRemovals)
	{
		// Whether anything under a node is still alive in the live tree. Such a node is NOT removed even when it has
		// gone itself: its text holds the living widget's, and taking it out would delete a widget the author kept.
		// That is the shape a rename the queue was not told about takes (the old id gone, its children still there
		// under the new one), and the shape of a move out of a deleted parent the queue has not written yet -- both
		// left for the residue to report rather than settled by losing the subtree.
		TFunction<bool(const FDreamUINode&)> HoldsLiveDescendant;
		HoldsLiveDescendant = [&](const FDreamUINode& InNode)
		{
			for (const FDreamUINode& Child : InNode.Children)
			{
				if ((!Child.Id.IsEmpty() && LiveWidgets.Contains(Child.Id)) || HoldsLiveDescendant(Child))
				{
					return true;
				}
			}
			return false;
		};
		TFunction<void(const FDreamUINode&, bool)> VisitForRemovals;
		VisitForRemovals = [&](const FDreamUINode& InNode, bool bInAncestorRemoved)
		{
			bool bRemoved = bInAncestorRemoved;
			// A slot FILL is not a widget of this file: its id is the name of a slot the component declares,
			// and the widget the trees hold under that name is the component's. Nothing here removes it.
			if (!bInAncestorRemoved && !InNode.Id.IsEmpty() && !InNode.bFillsSlot
				&& (InNode.Kind == EDreamUINodeKind::Widget || InNode.Kind == EDreamUINodeKind::NamedSlot)
				&& TextWidgets.Contains(InNode.Id) && !LiveWidgets.Contains(InNode.Id)
				&& !HoldsLiveDescendant(InNode))
			{
				FDreamUIStructuralEdit& Edit = OutEdits.AddDefaulted_GetRef();
				Edit.Kind = EDreamUIStructuralEditKind::RemoveNode;
				Edit.NodeId = InNode.Id;
				bRemoved = true;
			}
			for (const FDreamUINode& Child : InNode.Children)
			{
				VisitForRemovals(Child, bRemoved);
			}
		};
		VisitForRemovals(InAst.Root, /*bInAncestorRemoved*/false);
	}

	// ---- added: on the live tree and on neither the file nor the reference tree -------------------
	//
	// Only under a parent the file ALREADY declares. A child of a node being inserted in this same
	// batch has no parent in InAst to be planned against, so it waits for the next round, when the
	// text has been read back with its parent in it (ProduceText). The walk is depth-first from the
	// root, so siblings are planned in the order the live tree holds them.
	TArray<UDreamWidget*> LiveWidgetsInOrder;
	if (IsValid(InLiveTree->RootWidget))
	{
		UDreamWidget::CollectChildrenWidgets(InLiveTree->RootWidget, LiveWidgetsInOrder, /*IncludeTarget*/true);
	}
	for (UDreamWidget* LiveWidget : LiveWidgetsInOrder)
	{
		// Transient is run time's: the copies a `for` makes of its template stand among the host
		// panel's children -- a parent the file declares -- and without this every copy would be
		// written into the file as a node of its own, once per item, on every flush.
		if (!IsValid(LiveWidget) || LiveWidget->HasAnyFlags(RF_Transient))
		{
			continue;
		}
		const FString Id = LiveWidget->GetDisplayName();
		if (Id.IsEmpty() || TextWidgets.Contains(Id) || AuthoredIds.Contains(Id))
		{
			continue;
		}
		UDreamWidget* Parent = LiveWidget->GetParent();
		if (!IsValid(Parent))
		{
			// A new ROOT is not an insert, it is a different file. Nothing sensible to write.
			continue;
		}
		if (InSkipInsertIds != nullptr && InSkipInsertIds->Contains(Id))
		{
			continue;
		}
		const FString ParentId = Parent->GetDisplayName();
		if (!AuthoredIds.Contains(ParentId))
		{
			// The parent is a synthesised widget (an `each` content holder), a node the file does not
			// declare, or one this flush has not written yet. Writing into the first two would be writing
			// into machinery the author never typed; the third is the next round's.
			continue;
		}

		FDreamUIStructuralEdit& Edit = OutEdits.AddDefaulted_GetRef();
		Edit.Kind = EDreamUIStructuralEditKind::InsertNode;
		// Placed by neighbour (the authored sibling it stands before) and by the named slot it fills, which the
		// patcher writes into the instance's `slot Name { … }` fill.
		const FPlacement Placement = ResolvePlacement(LiveWidget, InLiveTree, InAst);
		Edit.ParentId = Placement.ParentId;
		Edit.NewId = Id;
		Edit.BeforeId = Placement.BeforeId;
		Edit.FillSlotName = Placement.FillSlotName;
		Edit.TypeName = DescribeWidgetForText(LiveWidget, InAst);
	}

	// ---- the `+` lines of a node this flush has just written -------------------------------------
	//
	// A node is inserted as `Type Id { }`, and the type spells the visual and, for a plain widget, its
	// layout container -- nothing else. A pasted button's behaviours, or a container on a widget that
	// also has a visual, have no line until one is written here, and without it the value pass would
	// have no object to write their settings onto and the next compile would drop them. Matched by
	// class, as many times as the live node carries one: anything the text node already builds (from
	// its type, or a behaviour the builder adds with it) is in both and cancels out.
	if (InComponentCheckIds != nullptr)
	{
		for (const FString& Id : *InComponentCheckIds)
		{
			const UDreamWidget* LiveWidget = LiveWidgets.FindRef(Id);
			const UDreamWidget* TextWidget = TextWidgets.FindRef(Id);
			if (!IsValid(LiveWidget) || !IsValid(TextWidget))
			{
				continue;
			}
			TArray<const UObject*> TextObjects;
			CollectAuthorableComponentObjects(TextWidget, TextObjects);
			TArray<const UObject*> LiveObjects;
			CollectAuthorableComponentObjects(LiveWidget, LiveObjects);
			for (const UObject* LiveObject : LiveObjects)
			{
				const int32 Match = TextObjects.IndexOfByPredicate([LiveObject](const UObject* InCandidate)
				{
					return InCandidate != nullptr && InCandidate->GetClass() == LiveObject->GetClass();
				});
				if (Match != INDEX_NONE)
				{
					TextObjects[Match] = nullptr;
					continue;
				}
				const FString Spelling = FindComponentSpelling(LiveObject->GetClass());
				if (Spelling.IsEmpty())
				{
					// No name the builder reads back as this class: writing a guess would be a line the
					// next compile rejects. The value pass then has nothing to write onto, which is the
					// same loss without a broken file.
					continue;
				}
				FDreamUIStructuralEdit& Edit = OutEdits.AddDefaulted_GetRef();
				Edit.Kind = EDreamUIStructuralEditKind::InsertComponent;
				Edit.NodeId = Id;
				Edit.ComponentClassName = Spelling;
			}
		}
	}

	// ---- the type and the `+` lines of a node both trees have --------------------------------------
	//
	// What the designer changes about a node without adding or removing one: a visual swapped for
	// another, a panel replaced, a behaviour added, taken off or moved. Read off the two trees like
	// everything above, paired by class, so whatever both build alike cancels out.
	auto Emit = [&OutEdits, InAttempted](FDreamUIStructuralEdit&& InEdit) -> bool
	{
		if (InAttempted != nullptr && InAttempted->Contains(MakeEditSignature(InEdit)))
		{
			return false;
		}
		OutEdits.Add(MoveTemp(InEdit));
		return true;
	};
	auto ResolvesOn = [](const FString& InName, TConstArrayView<const UClass*> InClasses)
	{
		int32 Dot = INDEX_NONE;
		const FString Head = InName.FindChar(TEXT('.'), Dot) ? InName.Left(Dot) : InName;
		for (const UClass* Class : InClasses)
		{
			if (Class != nullptr && FindFProperty<FProperty>(Class, FName(*Head)) != nullptr)
			{
				return true;
			}
		}
		return false;
	};
	InAst.ForEachNode([&](const FDreamUINode& InNode)
	{
		if (InNode.Kind != EDreamUINodeKind::Widget || InNode.Id.IsEmpty() || !InNode.RowKey.IsEmpty()
			|| (InComponentCheckIds != nullptr && InComponentCheckIds->Contains(InNode.Id)))
		{
			return;
		}
		const UDreamWidget* Live = LiveWidgets.FindRef(InNode.Id);
		const UDreamWidget* Text = TextWidgets.FindRef(InNode.Id);
		if (!IsValid(Live) || !IsValid(Text))
		{
			return;
		}

		// The type: what the visual is, which widget blueprint a nested instance is, and -- when the type is a
		// panel's name (`VerticalBox Column`) -- which panel it is.
		const UClass* LiveVisual = Live->GetVisual() != nullptr ? Live->GetVisual()->GetClass() : nullptr;
		const UClass* TextVisual = Text->GetVisual() != nullptr ? Text->GetVisual()->GetClass() : nullptr;
		const UClass* LiveContainer = Live->GetLayoutContainer() != nullptr ? Live->GetLayoutContainer()->GetClass() : nullptr;
		const UClass* TextContainer = Text->GetLayoutContainer() != nullptr ? Text->GetLayoutContainer()->GetClass() : nullptr;
		const bool bTypeIsContainer = FDreamUITextBuilder::FindContainerClassForType(InNode.TypeName) != nullptr;

		// Another panel converts every child's slot to its own slot class, and an `@slot` line the new class has no
		// property for would stop the file building. Those go with the panel change, and are said.
		if (LiveContainer != TextContainer)
		{
			for (const FDreamUINode& Child : InNode.Children)
			{
				const UDreamWidget* LiveChild = Child.Id.IsEmpty() ? nullptr : LiveWidgets.FindRef(Child.Id);
				const UClass* SlotClass = IsValid(LiveChild) && LiveChild->GetPanelSlot() != nullptr ? LiveChild->GetPanelSlot()->GetClass() : nullptr;
				if (SlotClass == nullptr || !Child.RowKey.IsEmpty())
				{
					continue;
				}
				const UClass* SlotCandidates[] = { SlotClass };
				for (const FDreamUIProperty& Line : Child.SlotProperties)
				{
					if (Line.bSynthesized || Line.bRowCell || ResolvesOn(Line.Name, MakeArrayView(SlotCandidates)))
					{
						continue;
					}
					FDreamUIStructuralEdit Remove;
					Remove.Kind = EDreamUIStructuralEditKind::RemoveProperty;
					Remove.NodeId = Child.Id;
					Remove.PropertyName = Line.Name;
					Remove.PropertyTarget = EDreamUIPatchTarget::Slot;
					if (Emit(MoveTemp(Remove)) && OutDropped != nullptr)
					{
						OutDropped->Add(FString::Printf(TEXT("%s.@slot %s = %s"), *Child.Id, *Line.Name, *Line.Value.Raw));
					}
				}
			}
		}

		const bool bRetype = LiveVisual != TextVisual
			|| (Live->IsA<UDreamUserWidget>() && Live->GetClass() != Text->GetClass())
			|| (bTypeIsContainer && LiveContainer != TextContainer);
		if (bRetype)
		{
			const FString NewType = DescribeWidgetForText(Live, InAst);
			if (!NewType.IsEmpty() && !NewType.Equals(InNode.TypeName, ESearchCase::CaseSensitive))
			{
				FDreamUIStructuralEdit Retype;
				Retype.Kind = EDreamUIStructuralEditKind::RetypeNode;
				Retype.NodeId = InNode.Id;
				Retype.TypeName = NewType;
				if (Emit(MoveTemp(Retype)))
				{
					if (OutSweepKeys != nullptr)
					{
						OutSweepKeys->Add(InNode.Id);
					}
					// A line the new type has no property for would stop the file building; it goes, and is said.
					const UClass* NewTypeContainer = FDreamUITextBuilder::FindContainerClassForType(NewType) != nullptr ? LiveContainer : nullptr;
					const UClass* Candidates[] = { Live->GetClass(), LiveVisual, NewTypeContainer };
					for (const FDreamUIProperty& Property : InNode.Properties)
					{
						if (Property.bSynthesized || Property.bRowCell || ResolvesOn(Property.Name, MakeArrayView(Candidates)))
						{
							continue;
						}
						FDreamUIStructuralEdit Remove;
						Remove.Kind = EDreamUIStructuralEditKind::RemoveProperty;
						Remove.NodeId = InNode.Id;
						Remove.PropertyName = Property.Name;
						if (Emit(MoveTemp(Remove)) && OutDropped != nullptr)
						{
							OutDropped->Add(FString::Printf(TEXT("%s.%s = %s"), *InNode.Id, *Property.Name, *Property.Value.Raw));
						}
					}
					return;
				}
			}
		}

		// The `+` lines: paired by class with what the node now holds. A type that names the panel owns the panel.
		TArray<const UObject*> LineObjects;
		CollectComponentObjectsInAuthorOrder(InNode, Text, LineObjects);
		TArray<const UObject*> LiveObjects;
		CollectAuthorableComponentObjects(Live, LiveObjects);
		TArray<const UObject*> TextObjects;
		CollectAuthorableComponentObjects(Text, TextObjects);
		if (bTypeIsContainer)
		{
			auto IsContainer = [](const UObject* InObject) { return InObject != nullptr && InObject->IsA<UDreamLayoutContainer>(); };
			LiveObjects.RemoveAll(IsContainer);
			TextObjects.RemoveAll(IsContainer);
		}
		TArray<const UObject*> UnpairedLive = LiveObjects;
		TArray<const UObject*> UnpairedText;
		for (const UObject* TextObject : TextObjects)
		{
			const int32 Pair = UnpairedLive.IndexOfByPredicate([TextObject](const UObject* InLive) { return InLive->GetClass() == TextObject->GetClass(); });
			if (Pair != INDEX_NONE)
			{
				UnpairedLive.RemoveAt(Pair);
			}
			else
			{
				UnpairedText.Add(TextObject);
			}
		}

		bool bLinesChanged = false;
		TArray<const UClass*> LiveClasses;
		for (const UObject* Object : LiveObjects)
		{
			LiveClasses.Add(Object->GetClass());
		}
		LiveClasses.Add(Live->GetClass());
		LiveClasses.Add(LiveVisual);
		for (const UObject* Gone : UnpairedText)
		{
			const int32 Line = LineObjects.IndexOfByKey(Gone);
			if (Line == INDEX_NONE)
			{
				if (OutUnsettled != nullptr)
				{
					OutUnsettled->AddUnique(FString::Printf(TEXT("%s: + %s"), *InNode.Id, *GetNameSafe(Gone->GetClass())));
				}
				continue;
			}
			FDreamUIStructuralEdit Remove;
			Remove.Kind = EDreamUIStructuralEditKind::RemoveComponent;
			Remove.NodeId = InNode.Id;
			Remove.ComponentIndex = Line;
			Remove.ComponentClassName = InNode.Components[Line].ClassName;
			if (Emit(MoveTemp(Remove)))
			{
				bLinesChanged = true;
				// A value of the block the node's objects can no longer take is lost with the block, and said. One
				// they can take is written again by the value pass, under the line that holds it now.
				for (const FDreamUIProperty& Property : InNode.Components[Line].Properties)
				{
					if (!ResolvesOn(Property.Name, LiveClasses) && OutDropped != nullptr)
					{
						OutDropped->Add(FString::Printf(TEXT("%s.%s = %s"), *InNode.Id, *Property.Name, *Property.Value.Raw));
					}
				}
			}
		}
		for (const UObject* Added : UnpairedLive)
		{
			const FString Spelling = FindComponentSpelling(Added->GetClass());
			if (Spelling.IsEmpty())
			{
				continue;
			}
			FDreamUIStructuralEdit Insert;
			Insert.Kind = EDreamUIStructuralEditKind::InsertComponent;
			Insert.NodeId = InNode.Id;
			Insert.ComponentClassName = Spelling;
			if (Emit(MoveTemp(Insert)))
			{
				bLinesChanged = true;
				if (OutSweepKeys != nullptr)
				{
					OutSweepKeys->Add(InNode.Id + TEXT("+") + Spelling);
				}
			}
		}
		if (bLinesChanged)
		{
			return;
		}

		// The order of the behaviour lines, against the order the live node holds those behaviours in. One move a
		// round: the first place they disagree gets the behaviour the live node has there.
		TArray<int32> BehaviourLines;
		TArray<const UClass*> TextOrder;
		for (int32 Line = 0; Line < LineObjects.Num(); ++Line)
		{
			if (LineObjects[Line] != nullptr && LineObjects[Line]->IsA<UDreamUIBehaviour>())
			{
				BehaviourLines.Add(Line);
				TextOrder.Add(LineObjects[Line]->GetClass());
			}
		}
		TArray<const UClass*> Remaining = TextOrder;
		TArray<const UClass*> LiveOrder;
		for (const UDreamUIBehaviour* Behaviour : Live->GetAllComponents())
		{
			if (IsValid(Behaviour) && Remaining.RemoveSingle(Behaviour->GetClass()) > 0)
			{
				LiveOrder.Add(Behaviour->GetClass());
			}
		}
		if (LiveOrder.Num() != TextOrder.Num())
		{
			return;
		}
		for (int32 Place = 0; Place < TextOrder.Num(); ++Place)
		{
			if (TextOrder[Place] == LiveOrder[Place])
			{
				continue;
			}
			for (int32 From = Place + 1; From < TextOrder.Num(); ++From)
			{
				if (TextOrder[From] == LiveOrder[Place])
				{
					FDreamUIStructuralEdit Move;
					Move.Kind = EDreamUIStructuralEditKind::MoveComponent;
					Move.NodeId = InNode.Id;
					Move.ComponentIndex = BehaviourLines[From];
					Move.ChildIndex = BehaviourLines[Place];
					Move.ComponentClassName = InNode.Components[BehaviourLines[From]].ClassName;
					Emit(MoveTemp(Move));
					break;
				}
			}
			break;
		}
	});
}

FString FDreamUITextWriteBack::MakeEditSignature(const FDreamUIStructuralEdit& InEdit)
{
	return FString::Printf(TEXT("%d|%s|%s|%s|%s|%d|%d|%s|%d"), static_cast<int32>(InEdit.Kind), *InEdit.NodeId, *InEdit.NewId,
		*InEdit.TypeName, *InEdit.ComponentClassName, InEdit.ComponentIndex, InEdit.ChildIndex, *InEdit.PropertyName,
		static_cast<int32>(InEdit.PropertyTarget));
}

int32 FDreamUITextWriteBack::FindComponentLine(const FDreamUINode& InNode, const UDreamWidget* InWidget, const UObject* InComponent)
{
	if (!IsValid(InWidget) || InComponent == nullptr)
	{
		return INDEX_NONE;
	}
	TArray<const UObject*> LineObjects;
	DreamUIWriteBackLocal::CollectComponentObjectsInAuthorOrder(InNode, InWidget, LineObjects);
	return LineObjects.IndexOfByKey(InComponent);
}

FString FDreamUITextWriteBack::DescribeComponentForText(const UClass* InClass)
{
	return DreamUIWriteBackLocal::FindComponentSpelling(InClass);
}

FString FDreamUITextWriteBack::DescribeWidgetForText(const UDreamWidget* InWidget, const FDreamUIAst& InAst)
{
	return DreamUIWriteBackLocal::DescribeWidgetForText(InWidget, InAst);
}

bool FDreamUITextWriteBack::ProduceText(const FString& InText, const UDreamWidgetTree* InLiveTree,
	FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamUIPropertyEdit>* OutEdits,
	const UObject* InLiveDefaults, FDreamUIStructuralResidue* OutResidue)
{
	OutText = InText;
	if (OutEdits != nullptr)
	{
		OutEdits->Reset();
	}
	if (OutResidue != nullptr)
	{
		OutResidue->Reset();
	}
	if (InLiveTree == nullptr)
	{
		return true;
	}

	FDreamUIAst Ast;
	// Rooted for the whole comparison. The reference tree is outered to the transient package, so
	// nothing but this pointer keeps it -- and a collection in the middle of the walk would leave the
	// comparison reading freed widgets.
	TStrongObjectPtr<UDreamWidgetTree> TextTree(BuildReferenceTree(InText, Ast, OutDiagnostics));
	if (!TextTree.IsValid())
	{
		// No baseline, so no write. Without one every property looks changed, and a flush would
		// rewrite the whole file on top of a parse error the author is in the middle of fixing.
		return false;
	}

	// ---- phase one: SHAPE ---------------------------------------------------------------------
	//
	// Structure before values, and with its own parse in between, because a structural splice moves
	// every location after it -- the property pass has to be planned against the file it will land
	// in, not the one that came in. This is the "a caller that wants two edits re-parses in between"
	// rule from FDreamUITextPatcher's own comment, and this is the caller.
	//
	// In rounds, because a subtree is written outermost-in and a child's insert can only be planned
	// against a text that already holds its parent. Each round collects against the text as it now is,
	// applies, and reads the result back; the loop ends on a round with nothing to do, or one whose
	// edits were all refused (the patcher has said why). Bounded by the nesting the parser accepts,
	// one level per round, plus one round for the `+` lines of the deepest nodes.
	FString Working = InText;
	TSet<FString> WrittenIds;
	TSet<FString> RefusedInserts;
	TSet<FString> ComponentCheckIds;
	// Edits on existing nodes a round already tried (never emitted twice), what retypes and removed blocks took
	// out, and what the latest round could not take out of the file.
	TSet<FString> Attempted;
	TArray<FString> Dropped;
	TArray<FString> Unsettled;
	bool bShapeChanged = false;

	// The edits a gesture queued (QueueStructuralEdit): renames and moves, which the trees cannot be read for.
	// Each is applied on its own against the text as it stands -- the next one's locations are only right in the
	// text the last one produced -- and one that cannot be planned yet (a move into a node the shape pass has not
	// written) waits for the next round. Its refusal is kept, and said only if it never lands.
	struct FPendingEdit
	{
		FDreamUIStructuralEdit Edit;
		TArray<FDreamUIDiagnostic> Refusal;
	};
	TArray<FPendingEdit> Pending;
	if (const TArray<FDreamUIStructuralEdit>* Queued = DreamUIWriteBackQueueLocal::GetQueueMap().Find(InLiveTree))
	{
		for (const FDreamUIStructuralEdit& Edit : *Queued)
		{
			Pending.Add({ Edit, {} });
		}
	}
	TMap<FString, UDreamWidget*> LiveById;
	if (!Pending.IsEmpty())
	{
		DreamUIWriteBackLocal::MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveById);
	}
	// False only when an edit produced a text that does not build; then nothing is written at all.
	auto ApplyPending = [&]() -> bool
	{
		using namespace DreamUIWriteBackLocal;
		for (int32 Index = 0; Index < Pending.Num();)
		{
			FDreamUIStructuralEdit Edit = Pending[Index].Edit;
			if (Edit.Kind == EDreamUIStructuralEditKind::MoveNode)
			{
				// Placed where the widget stands NOW, so the caller only had to say which node moved.
				if (const UDreamWidget* Live = LiveById.FindRef(Edit.NodeId))
				{
					const FPlacement Placement = ResolvePlacement(Live, InLiveTree, Ast);
					Edit.ParentId = Placement.ParentId;
					Edit.FillSlotName = Placement.FillSlotName;
					Edit.BeforeId = Placement.BeforeId;
					const FDreamUINode* TextNode = FindAstNode(Ast, Edit.NodeId);
					if (TextNode != nullptr && Placement.Destination != nullptr && IsAlreadyPlaced(Ast, *TextNode, Placement))
					{
						// Moved back where it was, or written there by an earlier edit: nothing to do.
						Pending.RemoveAt(Index);
						continue;
					}
				}
			}

			const FString BeforeEdit = Working;
			FDreamUIDiagnosticBag Attempt;
			Attempt.SourceName = OutDiagnostics.SourceName;
			const bool bPlanned = FDreamUITextPatcher::ApplyStructuralEdits(Working, Ast, MakeArrayView(&Edit, 1), Attempt);
			if (Working.Equals(BeforeEdit, ESearchCase::CaseSensitive))
			{
				if (bPlanned && !Attempt.HasErrors())
				{
					// The file already says it (a statement written twice): done, and nothing to report.
					Pending.RemoveAt(Index);
					continue;
				}
				Pending[Index].Refusal = Attempt.Diagnostics;
				++Index;
				continue;
			}

			FDreamUIAst Rebuilt;
			FDreamUIDiagnosticBag ReadBack;
			ReadBack.SourceName = OutDiagnostics.SourceName;
			TStrongObjectPtr<UDreamWidgetTree> RebuiltTree(BuildReferenceTree(Working, Rebuilt, ReadBack));
			if (!RebuiltTree.IsValid())
			{
				for (FDreamUIDiagnostic& Diagnostic : ReadBack.Diagnostics)
				{
					OutDiagnostics.Add(MoveTemp(Diagnostic));
				}
				return false;
			}
			Ast = MoveTemp(Rebuilt);
			TextTree = MoveTemp(RebuiltTree);
			// A binding, a route or a value line written or taken out is not a change of shape: the value pass stays the
			// dirty set's (or the sweep's), as it is for the overrides below.
			if (Edit.Kind != EDreamUIStructuralEditKind::SetStatement && Edit.Kind != EDreamUIStructuralEditKind::RemoveProperty)
			{
				bShapeChanged = true;
			}
			Pending.RemoveAt(Index);
		}
		return true;
	};

	for (int32 Round = 0; Round <= DreamUIAst::MaxNestingDepth + 1; ++Round)
	{
		if (!ApplyPending())
		{
			OutText = InText;
			return false;
		}

		TArray<FDreamUIStructuralEdit> StructuralEdits;
		Unsettled.Reset();
		// WrittenIds doubles as the sweep set: the shape pass adds a retyped node and `Id+Class` of a new `+` line.
		CollectStructuralEdits(Ast, InLiveTree, TextTree.Get(), StructuralEdits,
			/*bInIncludeRemovals*/Round == 0, &RefusedInserts, &ComponentCheckIds, &WrittenIds, &Dropped, &Unsettled, &Attempted);
		// Each node's `+` lines are asked for once, in the round right after the node was written.
		ComponentCheckIds.Reset();
		if (StructuralEdits.IsEmpty())
		{
			break;
		}
		for (const FDreamUIStructuralEdit& Edit : StructuralEdits)
		{
			Attempted.Add(MakeEditSignature(Edit));
		}

		const FString BeforeRound = Working;
		FDreamUITextPatcher::ApplyStructuralEdits(Working, Ast, StructuralEdits, OutDiagnostics);
		if (Working.Equals(BeforeRound, ESearchCase::CaseSensitive))
		{
			break;
		}

		// Its own bag: the builder repeats every warning the file carries on every read, and one copy of
		// those per round would bury the one that matters. Kept only when the read fails.
		FDreamUIAst Rebuilt;
		FDreamUIDiagnosticBag RoundDiagnostics;
		RoundDiagnostics.SourceName = OutDiagnostics.SourceName;
		TStrongObjectPtr<UDreamWidgetTree> RebuiltTree(BuildReferenceTree(Working, Rebuilt, RoundDiagnostics));
		if (!RebuiltTree.IsValid())
		{
			// The shape pass produced a file that does not build. Nothing is written at all -- half a
			// structural change is worse than none, and the diagnostics say which line went wrong.
			for (FDreamUIDiagnostic& Diagnostic : RoundDiagnostics.Diagnostics)
			{
				OutDiagnostics.Add(MoveTemp(Diagnostic));
			}
			OutText = InText;
			return false;
		}

		// Which inserts landed: those are swept whole by the value pass and get their `+` lines next
		// round. One that did not land is not asked again -- it would be refused for the same reason.
		TSet<FString> NowDeclared;
		Rebuilt.ForEachNode([&NowDeclared](const FDreamUINode& InNode)
		{
			if (!InNode.Id.IsEmpty())
			{
				NowDeclared.Add(InNode.Id);
			}
		});
		for (const FDreamUIStructuralEdit& Edit : StructuralEdits)
		{
			if (Edit.Kind != EDreamUIStructuralEditKind::InsertNode)
			{
				continue;
			}
			if (NowDeclared.Contains(Edit.NewId))
			{
				WrittenIds.Add(Edit.NewId);
				ComponentCheckIds.Add(Edit.NewId);
			}
			else
			{
				RefusedInserts.Add(Edit.NewId);
			}
		}

		Ast = MoveTemp(Rebuilt);
		TextTree = MoveTemp(RebuiltTree);
		bShapeChanged = true;
	}

	// The last chance for a queued edit the rounds made room for; what still does not land is said.
	if (!ApplyPending())
	{
		OutText = InText;
		return false;
	}
	for (FPendingEdit& Unapplied : Pending)
	{
		for (FDreamUIDiagnostic& Diagnostic : Unapplied.Refusal)
		{
			OutDiagnostics.Add(MoveTemp(Diagnostic));
		}
	}

	if (OutResidue != nullptr)
	{
		// Read off the same two maps the shape pass pairs by, so the residue is exactly the disagreement
		// that pass could not settle -- and empty for every flush whose shape already matched.
		TMap<FString, UDreamWidget*> LiveWidgets;
		TMap<FString, UDreamWidget*> TextWidgets;
		DreamUIWriteBackLocal::MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveWidgets);
		DreamUIWriteBackLocal::MapWidgetsByNodeId(TextTree.Get(), TextWidgets);
		for (const TPair<FString, UDreamWidget*>& Pair : LiveWidgets)
		{
			if (!TextWidgets.Contains(Pair.Key))
			{
				OutResidue->Unwritten.Add(Pair.Key);
			}
		}
		for (const TPair<FString, UDreamWidget*>& Pair : TextWidgets)
		{
			if (!LiveWidgets.Contains(Pair.Key))
			{
				OutResidue->Unremoved.Add(Pair.Key);
			}
			// Under another parent: a move the text did not take, which the compile is about to put back.
			else if (const UDreamWidget* Live = LiveWidgets.FindRef(Pair.Key))
			{
				const UDreamWidget* LiveParent = Live->GetParent();
				const UDreamWidget* TextParent = Pair.Value->GetParent();
				if (IsValid(LiveParent) && IsValid(TextParent)
					&& !LiveParent->GetDisplayName().Equals(TextParent->GetDisplayName(), ESearchCase::IgnoreCase))
				{
					OutResidue->Unmoved.Add(Pair.Key);
				}
			}
		}
		OutResidue->Unsettled = Unsettled;
		OutResidue->Dropped = Dropped;
	}

	// ---- between the two: OVERRIDES of a style's values ----------------------------------------------
	//
	// A value a node takes from its style's `+ Class` line has no line of its own to change. Asked of the value pass,
	// such a change becomes a `+ Class { }` line on the node, written here, and the value pass below then writes the
	// changed values into it: the node overrides the style for itself, and every other node wearing it is untouched.
	TArray<FDreamUIPropertyEdit> Probe;
	{
		FDreamUIDiagnosticBag ProbeDiagnostics;
		TArray<FDreamUIStructuralEdit> Overrides;
		CollectEdits(Ast, InLiveTree, TextTree.Get(), Probe, ProbeDiagnostics, /*bInUseDirtySet*/true,
			bShapeChanged ? &WrittenIds : nullptr, &Overrides);
		if (!Overrides.IsEmpty())
		{
			const FString BeforeOverrides = Working;
			FDreamUITextPatcher::ApplyStructuralEdits(Working, Ast, Overrides, OutDiagnostics);
			if (!Working.Equals(BeforeOverrides, ESearchCase::CaseSensitive))
			{
				FDreamUIAst Rebuilt;
				FDreamUIDiagnosticBag ReadBack;
				ReadBack.SourceName = OutDiagnostics.SourceName;
				TStrongObjectPtr<UDreamWidgetTree> RebuiltTree(BuildReferenceTree(Working, Rebuilt, ReadBack));
				if (!RebuiltTree.IsValid())
				{
					for (FDreamUIDiagnostic& Diagnostic : ReadBack.Diagnostics)
					{
						OutDiagnostics.Add(MoveTemp(Diagnostic));
					}
					OutText = InText;
					return false;
				}
				Ast = MoveTemp(Rebuilt);
				TextTree = MoveTemp(RebuiltTree);
				// Not a change of shape: the value that asked for the line is still the dirty set's (or the sweep's), and
				// so is every other value of this flush. Only after a shape pass, which narrows the value pass to the
				// nodes it wrote, does the new line join those.
				if (bShapeChanged)
				{
					for (const FDreamUIStructuralEdit& Override : Overrides)
					{
						WrittenIds.Add(Override.NodeId + TEXT("+") + Override.ComponentClassName);
					}
				}
			}
		}
	}

	// ---- and the lines a value replaces -------------------------------------------------------------
	//
	// A value whose property replaced older ones (meta=(DreamGUI_Replaces)) takes their lines out of its node as it is
	// written -- a rect block's two-colour gradient's lines go when its gradient is first written with stops -- so the file
	// never says one thing two ways. Planned from the probe above, which names properties rather than places, against the
	// text as it now is, and read back before the value pass is planned against it.
	{
		TArray<FDreamUIStructuralEdit> Replaced;
		DreamUIWriteBackLocal::CollectReplacedLines(Ast, InLiveTree, Probe, Replaced);
		if (!Replaced.IsEmpty())
		{
			const FString BeforeRemovals = Working;
			FDreamUITextPatcher::ApplyStructuralEdits(Working, Ast, Replaced, OutDiagnostics);
			if (!Working.Equals(BeforeRemovals, ESearchCase::CaseSensitive))
			{
				FDreamUIAst Rebuilt;
				FDreamUIDiagnosticBag ReadBack;
				ReadBack.SourceName = OutDiagnostics.SourceName;
				TStrongObjectPtr<UDreamWidgetTree> RebuiltTree(BuildReferenceTree(Working, Rebuilt, ReadBack));
				if (!RebuiltTree.IsValid())
				{
					for (FDreamUIDiagnostic& Diagnostic : ReadBack.Diagnostics)
					{
						OutDiagnostics.Add(MoveTemp(Diagnostic));
					}
					OutText = InText;
					return false;
				}
				Ast = MoveTemp(Rebuilt);
				TextTree = MoveTemp(RebuiltTree);
			}
		}
	}

	// ---- phase two: VALUES ----------------------------------------------------------------------
	TArray<FDreamUIPropertyEdit> Edits;
	// After a change of shape, the nodes just written are swept whole -- they have no dirty entries,
	// and every value they hold is one the author made -- while every other node writes only what was
	// reported. A plain value flush is the dirty set over the sweep, exactly as before.
	CollectEdits(Ast, InLiveTree, TextTree.Get(), Edits, OutDiagnostics,
		/*bInUseDirtySet*/true, bShapeChanged ? &WrittenIds : nullptr);
	DreamUIWriteBackLocal::CollectResourceEdits(Ast, InLiveDefaults, Edits, OutDiagnostics);
	if (OutEdits != nullptr)
	{
		*OutEdits = Edits;
	}
	if (Edits.IsEmpty())
	{
		OutText = MoveTemp(Working);
		return true;
	}

	// One batch, because every location in Ast describes Working as it is right now and the first
	// splice invalidates the ones after it. SetProperties plans them all against this one state and
	// applies them backwards; its false only means something was refused, and the rest still landed.
	FDreamUITextPatcher::SetProperties(Working, Ast, Edits, OutDiagnostics);

	// And read back before it goes anywhere, as the shape pass's result is. A splice the patcher got wrong -- two blocks
	// written onto one node, a block that took in the statement after it -- was written to disk as it came out, and the
	// author met it as a file that no longer opened. Its own bag: a file that does build says nothing new here.
	{
		FDreamUIAst Check;
		FDreamUIDiagnosticBag CheckDiagnostics;
		TStrongObjectPtr<UDreamWidgetTree> CheckTree(BuildReferenceTree(Working, Check, CheckDiagnostics));
		if (!CheckTree.IsValid())
		{
			for (FDreamUIDiagnostic& Diagnostic : CheckDiagnostics.Diagnostics)
			{
				OutDiagnostics.Add(MoveTemp(Diagnostic));
			}
			OutText = InText;
			return false;
		}
	}
	OutText = MoveTemp(Working);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The wired half
// -------------------------------------------------------------------------------------------------

TSharedPtr<FDreamUITextWriteBack> FDreamUITextWriteBack::Create(const FString& InAbsoluteFilePath,
	const TSharedPtr<FDreamWidgetPreviewHost>& InHost, FString& OutError)
{
	OutError.Reset();

	// MakeShareable rather than MakeShared: the constructor is private so that nothing can own one
	// of these by value. It hands out delegates bound to itself, so it has to be a shared pointer.
	TSharedPtr<FDreamUITextWriteBack> WriteBack = MakeShareable(new FDreamUITextWriteBack());
	WriteBack->DocumentHandle = FDreamUIDocumentHandle::Open(InAbsoluteFilePath, OutError);
	if (!WriteBack->DocumentHandle.IsValid())
	{
		return nullptr;
	}

	// After the shared pointer exists, never in the constructor: AddSP needs AsShared().
	WriteBack->Initialize(InHost);
	return WriteBack;
}

FDreamUITextWriteBack::~FDreamUITextWriteBack()
{
	if (const TSharedPtr<FDreamWidgetPreviewHost> Pinned = Host.Pin())
	{
		if (TemplateChangedHandle.IsValid())
		{
			Pinned->OnTemplateChanged.Remove(TemplateChangedHandle);
		}
	}
	if (UDreamUIDocument* Document = DocumentHandle.Get())
	{
		if (TextChangedHandle.IsValid())
		{
			Document->OnTextChanged().Remove(TextChangedHandle);
		}
	}
	if (DeferredRebuildTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(DeferredRebuildTickerHandle);
	}
}

void FDreamUITextWriteBack::Initialize(const TSharedPtr<FDreamWidgetPreviewHost>& InHost)
{
	Host = InHost;
	if (InHost.IsValid())
	{
		// The HOST, not the two write paths. There are already two ways to write the template and
		// there will be a third; a subscription on the aggregation point covers it automatically,
		// and one hung on the call sites would silently miss it.
		TemplateChangedHandle = InHost->OnTemplateChanged.AddSP(this, &FDreamUITextWriteBack::OnTemplateChanged);
	}
	if (UDreamUIDocument* Document = DocumentHandle.Get())
	{
		TextChangedHandle = Document->OnTextChanged().AddSP(this, &FDreamUITextWriteBack::OnDocumentTextChanged);
	}
}

void FDreamUITextWriteBack::OnTemplateChanged()
{
	FString Error;
	if (!Flush(Error) && !Error.IsEmpty())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Error);
	}
}

bool FDreamUITextWriteBack::Flush(FString& OutError)
{
	OutError.Reset();

	const TSharedPtr<FDreamWidgetPreviewHost> Pinned = Host.Pin();
	if (!Pinned.IsValid() || !IsValid(Pinned->GetBlueprint()))
	{
		// Not an error. A write-back outliving its host, or created without one, has nothing to
		// mirror -- and reporting that on every flush point would bury the failures that matter.
		return true;
	}
	// The class defaults ride along, because the resources block compiles into them: an author who
	// edits a resource in the Class Defaults panel has made a change only the CDO knows about.
	const UObject* LiveDefaults = nullptr;
	if (const UDreamWidgetBlueprint* Blueprint = Pinned->GetBlueprint())
	{
		if (Blueprint->GeneratedClass != nullptr)
		{
			LiveDefaults = Blueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false);
		}
	}
	return FlushTree(Pinned->GetBlueprint()->WidgetTree, OutError, LiveDefaults);
}

bool FDreamUITextWriteBack::ApplyStatementEdit(const FDreamUIStructuralEdit& InEdit, FString& OutError)
{
	OutError.Reset();
	LastDiagnostics.Reset();
	UDreamUIDocument* Document = DocumentHandle.Get();
	if (Document == nullptr)
	{
		OutError = TEXT("this write-back has no document");
		return false;
	}
	LastDiagnostics.SourceName = FPaths::GetCleanFilename(GetFilePath());

	const FString Current = Document->GetContent();
	FDreamUIAst Ast;
	if (!FDreamUISourceFile::Parse(Current, LastDiagnostics.SourceName, Ast, LastDiagnostics, FDreamUISourceFile::MakeFileImportReader()))
	{
		OutError = FString::Printf(TEXT("'%s' does not parse, so nothing was written into it"), *LastDiagnostics.SourceName);
		return false;
	}
	FString Updated = Current;
	const bool bPlanned = FDreamUITextPatcher::ApplyStructuralEdits(Updated, Ast, MakeArrayView(&InEdit, 1), LastDiagnostics);
	if (!bPlanned)
	{
		const FDreamUIDiagnostic* First = LastDiagnostics.Diagnostics.FindByPredicate(
			[](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.IsError(); });
		OutError = First != nullptr ? First->ToString() : TEXT("the patcher refused the line");
		return false;
	}
	return ApplyTextChange(Updated, OutError);
}

FString FDreamUITextWriteBack::GetDocumentText() const
{
	const UDreamUIDocument* Document = DocumentHandle.Get();
	return Document != nullptr ? Document->GetContent() : FString();
}

bool FDreamUITextWriteBack::ApplyTextChange(const FString& InNewText, FString& OutError)
{
	OutError.Reset();
	UDreamUIDocument* Document = DocumentHandle.Get();
	if (Document == nullptr)
	{
		OutError = TEXT("this write-back has no document");
		return false;
	}
	if (InNewText.Equals(Document->GetContent(), ESearchCase::CaseSensitive))
	{
		return true;
	}
	// Read back before it is written: whatever made the new text, it has to leave a file that still parses.
	{
		FDreamUIAst ReadBack;
		FDreamUIDiagnosticBag ReadBackDiagnostics;
		ReadBackDiagnostics.SourceName = FPaths::GetCleanFilename(GetFilePath());
		if (!FDreamUISourceFile::Parse(InNewText, ReadBackDiagnostics.SourceName, ReadBack, ReadBackDiagnostics, FDreamUISourceFile::MakeFileImportReader()))
		{
			const FDreamUIDiagnostic* First = ReadBackDiagnostics.Diagnostics.FindByPredicate(
				[](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.IsError(); });
			OutError = First != nullptr ? First->ToString() : TEXT("the written file would not parse");
			return false;
		}
	}

	FScopedTransaction Transaction(LOCTEXT("DreamUIStatementTransaction", "Edit DreamUI Text"));
	TGuardValue<bool> WritingBack(bIsWritingBack, true);
	const bool bSet = Document->SetContent(InNewText, OutError);
	if (Document->GetContent().Equals(InNewText, ESearchCase::CaseSensitive))
	{
		++WriteCount;
	}
	return bSet;
}

bool FDreamUITextWriteBack::PrintValueLiteral(const FProperty* InLeaf, const void* InValuePtr, FString& OutText)
{
	return DreamUIWriteBackLocal::PrintLiteral(InLeaf, InValuePtr, OutText, nullptr);
}

void FDreamUITextWriteBack::CollectUnwritableDifferences(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
	const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits, TArray<FString>& OutUnwritable,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	TGuardValue<TArray<FString>*> Sink(DreamUIWriteBackLocal::GUnwritableSink, &OutUnwritable);
	CollectEdits(InAst, InLiveTree, InTextTree, OutEdits, OutDiagnostics, /*bInUseDirtySet*/false);
}

bool FDreamUITextWriteBack::FlushTree(const UDreamWidgetTree* InLiveTree, FString& OutError,
	const UObject* InLiveDefaults)
{
	OutError.Reset();
	LastDiagnostics.Reset();
	LastResidue.Reset();
	LastEditCount = 0;

	UDreamUIDocument* Document = DocumentHandle.Get();
	if (Document == nullptr)
	{
		OutError = TEXT("this write-back has no document");
		return false;
	}
	if (InLiveTree == nullptr || !IsValid(InLiveTree->RootWidget))
	{
		return true;
	}

	// The file name, so a refusal reads "Login.dui(12,9): error DUI7001: …" like every other
	// diagnostic in the pipeline rather than naming nothing.
	LastDiagnostics.SourceName = FPaths::GetCleanFilename(GetFilePath());

	const FString Current = Document->GetContent();
	FString Updated;
	TArray<FDreamUIPropertyEdit> Edits;
	const bool bProduced = ProduceText(Current, InLiveTree, Updated, LastDiagnostics, &Edits, InLiveDefaults, &LastResidue);
	// Consumed by this attempt, whether it landed or not. What did not land is in the residue and the bag, and the
	// compile that follows rebuilds the tree from the file -- a queued edit kept past it would describe a tree that
	// is gone, and be applied again to the next flush's text.
	ClearQueuedStructuralEdits(InLiveTree);
	if (!bProduced)
	{
		OutError = FString::Printf(
			TEXT("'%s' does not currently parse and build, so nothing was written back: %s"),
			*LastDiagnostics.SourceName, *LastDiagnostics.ToString());
		return false;
	}
	LastEditCount = Edits.Num();

	// A flush that SUCCEEDED can still have refused things -- a patch target that moved, a value with
	// no spelling -- and those landed in a bag nothing reads: GetLastDiagnostics has no callers, so a
	// DUI7001 or a DUI7003 inside an otherwise working flush was written down and shown to nobody,
	// which is the same silence the code table exists to end.
	//
	// The 7xxx band ONLY. The bag also carries whatever the reference-tree build had to say, and
	// those causes belong to the compiler, which reports them into the message log and the mailbox
	// where a reader can act on them; repeating them here would put a copy of every compile warning
	// in the Output Log on every gesture the designer ends.
	for (const FDreamUIDiagnostic& Diagnostic : LastDiagnostics.Diagnostics)
	{
		if (static_cast<int32>(Diagnostic.Code) >= 7000)
		{
			UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d %s"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Diagnostic.ToString());
		}
	}

	if (Updated.Equals(Current, ESearchCase::CaseSensitive))
	{
		// THE CASE THIS CLASS IS MOSTLY FOR. Not an early-out for speed: calling SetContent here
		// would put an entry on the undo stack that describes no change (so Ctrl+Z appears to do
		// nothing and the user presses it again, losing the edit before), and it would write the
		// file, so an editor sitting idle would feed the DirectoryWatcher a stream of its own writes.
		//
		// Reached on every flush of a hand-written file whose values the designer has not changed --
		// which is what opening one is -- and reaching it is why the author's first `.dui` diff is
		// empty instead of a page of renormalised lines. A refused disk write is still owed even
		// though the document already has the latest text; retry it without another transaction.
		return !Document->HasUnflushedWrite() || Document->FlushToDisk(OutError);
	}

	// Opened only now, after the text is known to differ. A nested Begin folds into an outer
	// transaction by a counter (TransBuffer.h), so this is right whether or not the details panel
	// already has one open -- and one flush is one entry either way, which is what makes a gesture
	// undo in a single Ctrl+Z.
	FScopedTransaction Transaction(LOCTEXT("DreamUIWriteBackTransaction", "Edit DreamUI Text"));

	// So the broadcast our own SetContent causes is not read as somebody else's edit and answered
	// with a regeneration of the tree we just derived this text from.
	TGuardValue<bool> WritingBack(bIsWritingBack, true);

	// Consumed. The set describes what has happened SINCE the last write, so carrying it past one
	// would make a later flush write values nobody touched that time -- which is the sweep's
	// behaviour, and the whole point of the set is not to have it.
	ClearDirtyProperties(InLiveTree);

	const bool bSet = Document->SetContent(Updated, OutError);
	if (Document->GetContent().Equals(Updated, ESearchCase::CaseSensitive))
	{
		// Counted on the DOCUMENT changing, not on the disk write succeeding: a read-only file keeps
		// the edit and owes the write (UDreamUIDocument::HasUnflushedWrite), and that is still one
		// write-back as far as anything watching this number is concerned.
		++WriteCount;
	}
	return bSet;
}

void FDreamUITextWriteBack::OnDocumentTextChanged(EDreamUIDocumentChangeReason InReason)
{
	if (bIsWritingBack)
	{
		// Our own write coming back at us. Regenerating from it could only reproduce the tree the
		// text was derived from, and doing it mid-gesture would pull the widget out from under the
		// handle moving it.
		return;
	}

	// Undo is the one that cannot be answered where it is heard. UDreamUIDocument broadcasts from
	// PostEditUndo, which runs inside FTransaction::Apply's loop over the transaction's objects --
	// so the widgets whose values this text describes may not have been restored yet, and a tree
	// rebuilt now is overwritten property by property by the rest of the restore. Nothing inside the
	// handler can tell that happened; the tree simply ends up matching neither state.
	RequestRebuild(InReason, InReason == EDreamUIDocumentChangeReason::UndoRedo);
}

void FDreamUITextWriteBack::RequestRebuild(EDreamUIDocumentChangeReason InReason, bool bInDeferred)
{
	if (!bInDeferred)
	{
		RebuildRequestedDelegate.Broadcast(InReason);
		return;
	}

	PendingRebuildReason = InReason;
	if (bRebuildPending)
	{
		// One transaction can restore this document more than once (an undo of an undo of a batch).
		// One tick, one rebuild: the text after Apply finishes is the only one worth building from.
		return;
	}
	bRebuildPending = true;

	DeferredRebuildTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("DreamUITextWriteBackDeferredRebuild"), 0.0f,
		[WeakThis = TWeakPtr<FDreamUITextWriteBack>(AsShared())](float) -> bool
		{
			if (const TSharedPtr<FDreamUITextWriteBack> Pinned = WeakThis.Pin())
			{
				Pinned->ProcessDeferredRebuild();
			}
			// One shot. The next undo adds its own.
			return false;
		});
}

void FDreamUITextWriteBack::ProcessDeferredRebuild()
{
	if (!bRebuildPending)
	{
		return;
	}
	bRebuildPending = false;

	if (DeferredRebuildTickerHandle.IsValid())
	{
		// Removed even when this IS the ticker's own call: FTSTicker handles removal from inside a
		// tick, and the alternative is a handle that outlives its delegate and cancels a LATER one.
		FTSTicker::RemoveTicker(DeferredRebuildTickerHandle);
		DeferredRebuildTickerHandle.Reset();
	}

	RebuildRequestedDelegate.Broadcast(PendingRebuildReason);
}

#undef LOCTEXT_NAMESPACE
