// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITextWriteBack.h"

// FTextProperty is dereferenced when deciding whether a value is an FText literal; inside a unity
// blob a neighbour always had it.
#include "UObject/TextProperty.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"

#include "Core/DreamUIBehaviour.h"
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
	 */
	bool PrintLiteral(const FProperty* InLeaf, const void* InValuePtr, FString& OutText)
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

		if (DreamUIValueFormat::HasShortForm(InLeaf))
		{
			return DreamUIValueFormat::Print(InLeaf, InValuePtr, OutText);
		}

		if (UEnum* Enum = GetEnumForProperty(InLeaf))
		{
			const int64 Value = CastField<FEnumProperty>(InLeaf) != nullptr
				? CastField<FEnumProperty>(InLeaf)->GetUnderlyingProperty()->GetSignedIntPropertyValue(InValuePtr)
				: CastFieldChecked<FByteProperty>(InLeaf)->GetSignedIntPropertyValue(InValuePtr);
			// The short spelling -- `Left`, not `EDreamAlign::Left` -- which is what an author writes
			// and what UEnum::GetValueByNameString reads back. Empty means the number is not one of
			// the enum's entries (a flag combination, or a byte somebody set directly), and there is
			// no identifier for it.
			const FString Name = Enum->GetNameStringByValue(Value);
			if (Name.IsEmpty())
			{
				return false;
			}
			OutText = Name;
			return true;
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
				OutText = PrintScalar(AsNumeric->GetFloatingPointPropertyValue(InValuePtr),
					InLeaf->IsA<FFloatProperty>());
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
			UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
			const UObject* Found = nullptr;

			if (ComponentClass != nullptr)
			{
				if (ComponentClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
				{
					Found = InWidget->GetLayoutContainer();
				}
				else if (ComponentClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
				{
					Found = InWidget->GetLayoutSelf();
				}
				else
				{
					for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
					{
						if (IsValid(Behaviour) && Behaviour->GetClass() == ComponentClass && !Claimed.Contains(Behaviour))
						{
							Found = Behaviour;
							break;
						}
					}
				}
			}

			// Exact class, not IsA: two `+` blocks of related classes would otherwise both claim the
			// first one, and the second's properties would be compared against the wrong object.
			if (Found != nullptr && Found->GetClass() != ComponentClass)
			{
				Found = nullptr;
			}
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
			if (!IsValid(Widget) || Widget->GetDisplayName().IsEmpty())
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
	void CompareAndAppend(const FComparison& InComparison, TArray<FDreamUIPropertyEdit>& OutEdits)
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
		if (!PrintLiteral(Live.Leaf, Live.ValuePtr, LiveText) || !PrintLiteral(Text.Leaf, Text.ValuePtr, TextText))
		{
			// No spelling for this type. Leaving the line alone is the answer; see PrintLiteral.
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

bool FDreamUITextWriteBack::CanSpellAsLiteral(const FProperty* InLeaf, const void* InValuePtr)
{
	// Printing IS the question -- there is no cheaper predicate that stays true, because whether a
	// value has a spelling depends on the value for colours and on the type for everything else.
	FString Unused;
	return InLeaf != nullptr && InValuePtr != nullptr
		&& DreamUIWriteBackLocal::PrintLiteral(InLeaf, InValuePtr, Unused);
}

const TArray<FString>& FDreamUITextWriteBack::GetGeometryPropertyPaths()
{
	// Leaves, not `AnchorData` whole: a nested struct is written with dotted paths and only its
	// leaves ever appear as a value, which is the same rule DreamUIValueFormat's header states for
	// why FDreamUIAnchorData has no short form of its own.
	static const TArray<FString> Paths =
	{
		TEXT("AnchorData.Pivot"),
		TEXT("AnchorData.AnchorMin"),
		TEXT("AnchorData.AnchorMax"),
		TEXT("AnchorData.AnchoredPosition"),
		TEXT("AnchorData.SizeDelta"),
		// The euler, not the quat: the quat has no spelling, and the euler is the authored face of the
		// same rotation -- kept in step by SetRelativeRotationEuler in one direction and by
		// PostEditChangeProperty in the other. RelativeLocation is deliberately absent from this list
		// too: its setter recomputes the anchors, and the anchors above already carry the position.
		TEXT("RelativeRotationEuler"),
		TEXT("RelativeScale"),
	};
	return Paths;
}

const TArray<FString>& FDreamUITextWriteBack::GetPanelSlotPropertyNames()
{
	// The counterpart of GetGeometryPropertyPaths for the panel slot: what a designer edits on a
	// child of a layout, whether or not the .dui already mentions it.
	//
	// An allowlist rather than "every property on UDreamPanelSlot", because the slot also carries the
	// layout's OUTPUT -- AuthoredAnchorData, bLayoutGeometryApplied, LayoutGeometryControlMask and
	// the cached geometry are results, not authoring. Writing those into the file would put a
	// computed value in a source document and then argue with the layout that computed it.
	static const TArray<FString> Names =
	{
		TEXT("Padding"),
		TEXT("HorizontalAlignment"),
		TEXT("VerticalAlignment"),
		TEXT("SizeRule"),
		TEXT("FillWeight"),
		// Grid placement. Meaningless under any other layout, and harmless there: the comparison
		// runs against the same defaults on both sides, so an untouched one produces nothing.
		TEXT("Row"),
		TEXT("Column"),
		TEXT("RowSpan"),
		TEXT("ColumnSpan"),
	};
	return Names;
}

UDreamWidgetTree* FDreamUITextWriteBack::BuildReferenceTree(const FString& InText, FDreamUIAst& OutAst,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	if (!FDreamUISourceFile::Parse(InText, OutDiagnostics.SourceName, OutAst, OutDiagnostics))
	{
		return nullptr;
	}

	// Discarded on purpose. Bindings belong to the compiler, and the only thing they mean here is
	// "this property has no literal in the file" -- which the tree already expresses, by holding the
	// class default for it.
	TArray<FDreamWidgetPropertyBinding> Bindings;
	return FDreamUITextBuilder::Build(OutAst, GetTransientPackage(), OutDiagnostics, Bindings);
}

void FDreamUITextWriteBack::CollectEdits(const FDreamUIAst& InAst, const UDreamWidgetTree* InLiveTree,
	const UDreamWidgetTree* InTextTree, TArray<FDreamUIPropertyEdit>& OutEdits,
	FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamUIWriteBackLocal;

	if (InLiveTree == nullptr || InTextTree == nullptr || !InAst.bHasRoot)
	{
		return;
	}

	// Const only ever came off a read: the collectors and FindFProperty want non-const containers,
	// and nothing below writes to either tree. The trees are the caller's and stay untouched.
	TMap<FString, UDreamWidget*> LiveWidgets;
	TMap<FString, UDreamWidget*> TextWidgets;
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InLiveTree), LiveWidgets);
	MapWidgetsByNodeId(const_cast<UDreamWidgetTree*>(InTextTree), TextWidgets);

	InAst.ForEachNode([&](const FDreamUINode& InNode)
	{
		// A `slot Footer` has no block to write into (the patcher refuses one outright), and a loop
		// node is not built at all -- its body's widgets do not exist on either tree yet.
		if (InNode.Kind != EDreamUINodeKind::Widget || InNode.Id.IsEmpty())
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

		// ---- bare `Name = Value`: the widget, or its visual -------------------------------------
		{
			FComparison Comparison;
			Comparison.NodeId = InNode.Id;
			Comparison.Target = EDreamUIPatchTarget::Node;
			Comparison.LiveCandidates = { LiveWidget, LiveWidget->GetVisual() };
			Comparison.TextCandidates = { TextWidget, TextWidget->GetVisual() };

			TArray<FString> Names;
			TSet<FString> Seen;
			// The style's names count as names the file mentions for this node, so a designer edit to
			// a styled property produces an override ON THE NODE. Never a write into the style: that
			// block is shared, and one drag would move every other node using it.
			if (const FDreamUIStyle* Style = InNode.StyleName.IsEmpty() ? nullptr : InAst.FindStyle(InNode.StyleName))
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
			for (const FString& Path : GetGeometryPropertyPaths())
			{
				AddCandidateName(Path, Names, Seen);
			}

			for (const FString& Name : Names)
			{
				Comparison.PropertyName = Name;
				CompareAndAppend(Comparison, OutEdits);
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
			Comparison.Target = EDreamUIPatchTarget::Slot;
			Comparison.LiveCandidates = { LiveWidget->GetPanelSlot() };
			Comparison.TextCandidates = { TextWidget->GetPanelSlot() };

			TArray<FString> Names;
			TSet<FString> Seen;
			for (const FDreamUIProperty& Property : InNode.SlotProperties)
			{
				AddCandidateName(Property.Name, Names, Seen);
			}
			for (const FString& Name : GetPanelSlotPropertyNames())
			{
				AddCandidateName(Name, Names, Seen);
			}
			for (const FString& Name : Names)
			{
				Comparison.PropertyName = Name;
				CompareAndAppend(Comparison, OutEdits);
			}
		}

		// ---- `+ Class { … }`: one destination per authored block, by ordinal --------------------
		if (InNode.Components.Num() > 0)
		{
			TArray<const UObject*> LiveObjects;
			TArray<const UObject*> TextObjects;
			CollectComponentObjectsInAuthorOrder(InNode, LiveWidget, LiveObjects);
			CollectComponentObjectsInAuthorOrder(InNode, TextWidget, TextObjects);

			for (int32 ComponentIndex = 0; ComponentIndex < InNode.Components.Num(); ++ComponentIndex)
			{
				if (!LiveObjects.IsValidIndex(ComponentIndex) || !TextObjects.IsValidIndex(ComponentIndex)
					|| LiveObjects[ComponentIndex] == nullptr || TextObjects[ComponentIndex] == nullptr)
				{
					continue;
				}

				FComparison Comparison;
				Comparison.NodeId = InNode.Id;
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
				for (const FString& Name : Names)
				{
					Comparison.PropertyName = Name;
					CompareAndAppend(Comparison, OutEdits);
				}
			}
		}
	});

	// Deliberately silent. Everything worth saying about this pass is already said on one side of it
	// or the other: the builder reported the unknown properties and unloadable assets while making
	// the reference tree, and the patcher reports every refusal by name and location while applying
	// what comes out. A complaint raised here would be a third voice for causes those two already
	// own, and it would repeat on every flush for the life of the file. The parameter stays so that
	// the day this DOES have a refusal of its own -- a node whose class changed under a live tree,
	// say -- adding it is not a signature change through every caller.
	(void)OutDiagnostics;
}

bool FDreamUITextWriteBack::ProduceText(const FString& InText, const UDreamWidgetTree* InLiveTree,
	FString& OutText, FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamUIPropertyEdit>* OutEdits)
{
	OutText = InText;
	if (OutEdits != nullptr)
	{
		OutEdits->Reset();
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

	TArray<FDreamUIPropertyEdit> Edits;
	CollectEdits(Ast, InLiveTree, TextTree.Get(), Edits, OutDiagnostics);
	if (OutEdits != nullptr)
	{
		*OutEdits = Edits;
	}
	if (Edits.IsEmpty())
	{
		return true;
	}

	// One batch, because every location in Ast describes InText as it is right now and the first
	// splice invalidates the ones after it. SetProperties plans them all against this one state and
	// applies them backwards; its false only means something was refused, and the rest still landed.
	FString Patched = InText;
	FDreamUITextPatcher::SetProperties(Patched, Ast, Edits, OutDiagnostics);
	OutText = MoveTemp(Patched);
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
	return FlushTree(Pinned->GetBlueprint()->WidgetTree, OutError);
}

bool FDreamUITextWriteBack::FlushTree(const UDreamWidgetTree* InLiveTree, FString& OutError)
{
	OutError.Reset();
	LastDiagnostics.Reset();
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
	if (!ProduceText(Current, InLiveTree, Updated, LastDiagnostics, &Edits))
	{
		OutError = FString::Printf(
			TEXT("'%s' does not currently parse and build, so nothing was written back: %s"),
			*LastDiagnostics.SourceName, *LastDiagnostics.ToString());
		return false;
	}
	LastEditCount = Edits.Num();

	if (Updated.Equals(Current, ESearchCase::CaseSensitive))
	{
		// THE CASE THIS CLASS IS MOSTLY FOR. Not an early-out for speed: calling SetContent here
		// would put an entry on the undo stack that describes no change (so Ctrl+Z appears to do
		// nothing and the user presses it again, losing the edit before), and it would write the
		// file, so an editor sitting idle would feed the DirectoryWatcher a stream of its own writes.
		//
		// Reached on every flush of a hand-written file whose values the designer has not changed --
		// which is what opening one is -- and reaching it is why the author's first `.dui` diff is
		// empty instead of a page of renormalised lines.
		return true;
	}

	// Opened only now, after the text is known to differ. A nested Begin folds into an outer
	// transaction by a counter (TransBuffer.h), so this is right whether or not the details panel
	// already has one open -- and one flush is one entry either way, which is what makes a gesture
	// undo in a single Ctrl+Z.
	FScopedTransaction Transaction(LOCTEXT("DreamUIWriteBackTransaction", "Edit DreamUI Text"));

	// So the broadcast our own SetContent causes is not read as somebody else's edit and answered
	// with a regeneration of the tree we just derived this text from.
	TGuardValue<bool> WritingBack(bIsWritingBack, true);

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
