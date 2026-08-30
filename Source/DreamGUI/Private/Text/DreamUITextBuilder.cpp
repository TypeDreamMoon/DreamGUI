// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITextBuilder.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIValueFormat.h"

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamBackgroundBlur.h"
#include "Core/Components/DreamBackgroundPixelate.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamPixelSort.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"

#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

/*
 * Why this file writes properties through reflection rather than through setters.
 *
 * The tree built here is never registered and never ticked: it is a class template, and everything a
 * setter exists to do -- invalidate layout, mark a canvas, wake a behaviour -- is work on a LIVE
 * hierarchy that has not happened yet and would be thrown away if it had. Instancing copies the
 * serialized property, so the reflected write is the one that survives to the instance; a setter call
 * on top of it would be, at best, a no-op on an object with no world. UDreamWidget::OnRegister exists
 * precisely because the prefab loader has always written straight into memory, and says so.
 *
 * Bindings are the opposite case and go through the setter for the opposite reason -- they fire on a
 * live widget every frame. See FDreamWidgetPropertyBinding.
 */

namespace DreamUITextBuilderLocal
{
	/**
	 * Levenshtein, plain and small.
	 *
	 * Only ever run against one class's property list when a name has ALREADY failed to resolve, so
	 * its cost is paid once per mistake and never on a good file. The suggestion is the entire point
	 * of the diagnostic: "no property FontSizee" is a message an author has to go read a header to
	 * act on, and "did you mean FontSize" is one they do not.
	 */
	int32 EditDistance(const FString& InA, const FString& InB)
	{
		const int32 LenA = InA.Len();
		const int32 LenB = InB.Len();
		if (LenA == 0) { return LenB; }
		if (LenB == 0) { return LenA; }

		TArray<int32> Previous;
		TArray<int32> Current;
		Previous.SetNumUninitialized(LenB + 1);
		Current.SetNumUninitialized(LenB + 1);
		for (int32 j = 0; j <= LenB; j++)
		{
			Previous[j] = j;
		}
		for (int32 i = 1; i <= LenA; i++)
		{
			Current[0] = i;
			for (int32 j = 1; j <= LenB; j++)
			{
				// Case-insensitive: the mistakes worth suggesting for are overwhelmingly a wrong
				// capital, and an author who wrote `fontsize` wants FontSize offered, not withheld.
				const int32 Cost = FChar::ToLower(InA[i - 1]) == FChar::ToLower(InB[j - 1]) ? 0 : 1;
				Current[j] = FMath::Min3(Current[j - 1] + 1, Previous[j] + 1, Previous[j - 1] + Cost);
			}
			Swap(Previous, Current);
		}
		return Previous[LenB];
	}

	/** The nearest property name in InScopes, or empty when nothing is near enough to be a guess. */
	FString SuggestNearestProperty(const FString& InName, TConstArrayView<const UStruct*> InScopes)
	{
		FString Best;
		int32 BestDistance = MAX_int32;
		for (const UStruct* Scope : InScopes)
		{
			if (Scope == nullptr)
			{
				continue;
			}
			for (TFieldIterator<FProperty> It(Scope); It; ++It)
			{
				const FString Candidate = It->GetName();
				const int32 Distance = EditDistance(InName, Candidate);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Candidate;
				}
			}
		}
		// A third of the name may be wrong before the suggestion stops being one. Without a ceiling
		// every miss produces a nearest match, and "did you mean bAutoSize" under `Colour` is worse
		// than saying nothing: the author stops trusting the suggestions and reads them all.
		const int32 Ceiling = FMath::Max(2, InName.Len() / 3);
		return BestDistance <= Ceiling ? Best : FString();
	}

	FString SuggestNearestEnumValue(const FString& InName, const UEnum* InEnum)
	{
		if (InEnum == nullptr)
		{
			return FString();
		}
		FString Best;
		int32 BestDistance = MAX_int32;
		// NumEnums() includes the generated _MAX entry, which is never something an author meant.
		for (int32 i = 0; i < InEnum->NumEnums() - 1; i++)
		{
			FString Candidate = InEnum->GetNameStringByIndex(i);
			const int32 Distance = EditDistance(InName, Candidate);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = MoveTemp(Candidate);
			}
		}
		const int32 Ceiling = FMath::Max(2, InName.Len() / 3);
		return BestDistance <= Ceiling ? Best : FString();
	}

	/** " (did you mean 'FontSize'?)", or nothing. Kept out of the message sites so they stay readable. */
	FString FormatSuggestion(const FString& InSuggestion)
	{
		return InSuggestion.IsEmpty() ? FString() : FString::Printf(TEXT(" (did you mean '%s'?)"), *InSuggestion);
	}

	/**
	 * The built-in tags, and the UDreamVisual each one creates. `Widget` is in it with a null class:
	 * it is a known tag whose answer is "no visual", which is a different fact from an unknown tag.
	 *
	 * A function-local static rather than a file-scope one because the values are StaticClass()
	 * pointers, and a file-scope array would be built during static initialisation -- before UObject
	 * bootstrapping, which is where that reliably turns into a null entry nobody can explain.
	 *
	 * UDreamCustomMesh is deliberately absent: it draws whatever a UDreamUICustomMeshSource hands it,
	 * and the language has no way to hand it one, so a `CustomMesh` tag would only ever produce a
	 * widget that draws nothing and no message saying why.
	 */
	TConstArrayView<TPair<const TCHAR*, UClass*>> GetVisualTagTable()
	{
		static const TArray<TPair<const TCHAR*, UClass*>> Table =
		{
			{ TEXT("Widget"),             nullptr },
			{ TEXT("Image"),              UDreamImage::StaticClass() },
			{ TEXT("Text"),               UDreamText::StaticClass() },
			{ TEXT("Texture"),            UDreamTexture::StaticClass() },
			{ TEXT("Sprite"),             UDreamSprite::StaticClass() },
			// HEADLESS HAZARD. UDreamWidget::CreateNewVisual calls Call_OnRegister unconditionally, and
			// UDreamRectBlock::OnRegister does `check(RectBlockData != nullptr)` after loading it from
			// UDreamGUISettings, then registers a data-texture buffer. Build a RectBlock node in a
			// commandlet whose project settings do not carry DefaultRectBlockData and it asserts --
			// not a diagnostic, an assert, with the .dui nowhere in the callstack. Whatever runs the
			// compile without an editor has to keep that setting valid, or teach this table to skip
			// visuals that need one. The tag stays: it is a real visual and authors want it.
			{ TEXT("RectBlock"),          UDreamRectBlock::StaticClass() },
			// Draws nothing and still takes raycasts -- the invisible hit area every UI needs, and the
			// one thing a plain `Widget` cannot be, having no visual to hit-test against at all.
			{ TEXT("Empty"),              UDreamVisualEmpty::StaticClass() },
			{ TEXT("BackgroundBlur"),     UDreamBackgroundBlur::StaticClass() },
			{ TEXT("BackgroundPixelate"), UDreamBackgroundPixelate::StaticClass() },
			{ TEXT("PixelSort"),          UDreamPixelSort::StaticClass() },
		};
		return Table;
	}

	/**
	 * Details-panel display names, and what to write instead. NOT an alias system.
	 *
	 * Nothing here is accepted -- the write still fails and the code is still UnknownProperty. The
	 * table only changes the MESSAGE, because these are the misses an author is guaranteed to make
	 * and the ones the nearest-match suggestion is worst at. The details panel labels UDreamWidget's
	 * geometry mirrors Width, Height and Anchor Left/Right/Top/Bottom, so that is what an author
	 * copies; the reflected names are AnimatableWidth and friends, far enough away in edit distance
	 * that no suggestion is offered, and transient anyway -- so even spelt correctly they would be
	 * refused as PropertyNotWritable. Two dead ends in a row, and the real answer (AnchorData) shares
	 * no letters with either.
	 *
	 * Accepting `Width` as a synonym was the alternative and is the wrong trade: it would be a second
	 * naming scheme over reflection, which every other name in the language is free of, and the .dui
	 * and the details panel would start disagreeing the first time a DisplayName was edited.
	 */
	const TCHAR* FindWriteItLikeThisHint(const FString& InName)
	{
		if (InName == TEXT("Width") || InName == TEXT("Height"))
		{
			return TEXT("write the size as 'AnchorData.SizeDelta = (w, h)'");
		}
		if (InName == TEXT("AnchorLeft") || InName == TEXT("AnchorRight")
			|| InName == TEXT("AnchorTop") || InName == TEXT("AnchorBottom"))
		{
			return TEXT("write the anchors as 'AnchorData.AnchorMin' and 'AnchorData.AnchorMax'");
		}
		return nullptr;
	}

	/** The tag whose visual declares InName, when one does. Only ever asked after a name has failed. */
	const TCHAR* FindTagWhoseVisualDeclares(const FString& InName)
	{
		for (const TPair<const TCHAR*, UClass*>& Entry : GetVisualTagTable())
		{
			if (Entry.Value != nullptr && FindFProperty<FProperty>(Entry.Value, *InName) != nullptr)
			{
				return Entry.Key;
			}
		}
		return nullptr;
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
	 * Whether text is allowed to write this property at all.
	 *
	 * Transient is the one that matters and the reason this check exists: UDreamWidget declares
	 * Width, Height and the four AnchorOffsets as transient mirrors of AnchorData, so `Width = 400`
	 * resolves, writes, reads back correctly for the rest of the build, and is gone the moment the
	 * class is saved. Silently. That is exactly the failure the whole text pipeline was built to make
	 * impossible, so it is an error naming the property, not a value that quietly does not stick.
	 *
	 * Instanced references are refused for a different reason: Visual, LayoutContainer, PanelSlot and
	 * Children ARE the object graph, and a text file reassigning one would leave a tree whose
	 * structure and whose Children array disagree. Those are attached with `+` and with nesting, and
	 * never assigned.
	 */
	bool IsWritableFromText(const FProperty* InProperty, FString& OutReason)
	{
		if (InProperty->HasAnyPropertyFlags(CPF_Deprecated))
		{
			OutReason = TEXT("it is deprecated");
			return false;
		}
		if (InProperty->HasAnyPropertyFlags(CPF_Transient) && !InProperty->HasSetter())
		{
			// The refusal is about persistence, so a transient WITH a native setter is exempt: such a
			// property is a mirror whose setter derives the value that does serialize, and WriteValue
			// routes it through that setter. RelativeRotationEuler is the case -- the rotation an author
			// writes lands in the quaternion, and refusing it here was refusing the only spelling a
			// rotation has.
			OutReason = TEXT("it is transient -- nothing written to it would survive being saved");
			return false;
		}
		if (InProperty->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			OutReason = TEXT("it holds part of the widget's object graph, which is authored by nesting and by '+', not assigned");
			return false;
		}
		if (InProperty->IsA<FDelegateProperty>() || InProperty->IsA<FMulticastDelegateProperty>())
		{
			OutReason = TEXT("it is a delegate, which has no text form");
			return false;
		}
		return true;
	}

	/**
	 * A resolved `Name = …` destination: which object, which leaf FProperty, and where its bytes are.
	 *
	 * BindingTarget and BehaviourIndex ride along even for an assignment because resolving a property
	 * and resolving a binding are the SAME walk -- the only difference is what happens at the end.
	 * Splitting them into two functions is how the two would come to disagree about where `Text`
	 * lives, which is a binding that reports success and drives nothing.
	 */
	struct FResolvedDestination
	{
		UObject* Owner = nullptr;
		FProperty* HeadProperty = nullptr;
		FProperty* LeafProperty = nullptr;
		void* LeafValuePtr = nullptr;
		/** True when the author wrote a dotted path, so the leaf is inside a struct rather than on Owner. */
		bool bNested = false;
		/** False when no EDreamWidgetBindingTarget can name Owner -- a panel slot, a layout container. */
		bool bBindable = false;
		EDreamWidgetBindingTarget BindingTarget = EDreamWidgetBindingTarget::Widget;
		int32 BehaviourIndex = INDEX_NONE;
		/** What separates this destination from the node's other ones in a localization key. See MakeLocalizationKey. */
		FString LocalizationDiscriminator;
	};

	struct FBuildContext
	{
		const FDreamUIAst* Ast = nullptr;
		UDreamWidgetTree* Tree = nullptr;
		FDreamUIDiagnosticBag* Diagnostics = nullptr;
		TArray<FDreamWidgetPropertyBinding>* Bindings = nullptr;
		/** ClassPath, or the source name when the file declares no class. Fixed for the whole build. */
		FString LocalizationNamespace;
	};

	/** One candidate object a bare property name may resolve against, with the binding target that names it. */
	struct FDestinationCandidate
	{
		UObject* Object = nullptr;
		EDreamWidgetBindingTarget Target = EDreamWidgetBindingTarget::Widget;
		int32 BehaviourIndex = INDEX_NONE;
		bool bBindable = true;
		/** Empty for the widget and its visual; see MakeLocalizationKey for why those two share. */
		FString LocalizationDiscriminator;
	};

	/**
	 * Walk InProperty.Name -- one segment or many -- from the first candidate that declares its head.
	 *
	 * Candidates are tried in order and the FIRST that has the head property wins, which is what makes
	 * `Text` on a Text node mean the visual's Text without the author saying so. Ambiguity is resolved
	 * by that order rather than reported: a widget property and a visual property of the same name is
	 * not a mistake in the file, and the widget's is the one that is always present.
	 */
	bool ResolveDestination(const FDreamUIProperty& InProperty, TConstArrayView<FDestinationCandidate> InCandidates,
		const FString& InDestinationDescription, FBuildContext& InContext, FResolvedDestination& OutDestination,
		bool bInSuggestVisualTag = false)
	{
		TArray<FString> Segments;
		InProperty.Name.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() == 0)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownProperty, InProperty.Location,
				TEXT("a property line with no name"));
			return false;
		}

		TArray<const UStruct*> SearchedScopes;
		for (const FDestinationCandidate& Candidate : InCandidates)
		{
			if (!IsValid(Candidate.Object))
			{
				continue;
			}
			SearchedScopes.Add(Candidate.Object->GetClass());
			FProperty* Head = FindFProperty<FProperty>(Candidate.Object->GetClass(), *Segments[0]);
			if (Head == nullptr)
			{
				continue;
			}
			OutDestination.Owner = Candidate.Object;
			OutDestination.HeadProperty = Head;
			OutDestination.BindingTarget = Candidate.Target;
			OutDestination.BehaviourIndex = Candidate.BehaviourIndex;
			OutDestination.bBindable = Candidate.bBindable;
			OutDestination.LocalizationDiscriminator = Candidate.LocalizationDiscriminator;
			break;
		}

		if (OutDestination.HeadProperty == nullptr)
		{
			// "No property named FontSize" is true and useless when the real mistake is that the node
			// is a Widget and FontSize belongs to a Text. Naming the tag that WOULD have it turns a
			// hunt through headers into a one-character edit, so it is worth its own code.
			if (const TCHAR* Tag = bInSuggestVisualTag ? FindTagWhoseVisualDeclares(Segments[0]) : nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NoVisualForProperty, InProperty.Location,
					FString::Printf(TEXT("'%s' belongs to the visual a '%s' node creates, and %s has no such visual"),
						*Segments[0], Tag, *InDestinationDescription));
				return false;
			}
			// Still UnknownProperty: the name really does not exist. Only the advice changes, and only
			// for the handful of names the details panel shows differently from reflection.
			const TCHAR* Hint = FindWriteItLikeThisHint(Segments[0]);
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownProperty, InProperty.Location,
				Hint != nullptr
					? FString::Printf(TEXT("'%s' is a details-panel label, not a property -- %s"), *Segments[0], Hint)
					: FString::Printf(TEXT("no property named '%s' on %s%s"), *Segments[0], *InDestinationDescription,
						*FormatSuggestion(SuggestNearestProperty(Segments[0], SearchedScopes))));
			return false;
		}

		OutDestination.LeafProperty = OutDestination.HeadProperty;
		OutDestination.LeafValuePtr = OutDestination.HeadProperty->ContainerPtrToValuePtr<void>(OutDestination.Owner);
		OutDestination.bNested = Segments.Num() > 1;

		for (int32 SegmentIndex = 1; SegmentIndex < Segments.Num(); SegmentIndex++)
		{
			const FStructProperty* AsStruct = CastField<FStructProperty>(OutDestination.LeafProperty);
			if (AsStruct == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownPropertyPathSegment, InProperty.Location,
					FString::Printf(TEXT("'%s' in '%s' is not a struct, so '%s' cannot be reached through it"),
						*Segments[SegmentIndex - 1], *InProperty.Name, *Segments[SegmentIndex]));
				return false;
			}
			const UStruct* SubScope = AsStruct->Struct;
			FProperty* Sub = FindFProperty<FProperty>(SubScope, *Segments[SegmentIndex]);
			if (Sub == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownPropertyPathSegment, InProperty.Location,
					FString::Printf(TEXT("'%s' has no field named '%s'%s"), *Segments[SegmentIndex - 1], *Segments[SegmentIndex],
						*FormatSuggestion(SuggestNearestProperty(Segments[SegmentIndex], { SubScope }))));
				return false;
			}
			OutDestination.LeafValuePtr = Sub->ContainerPtrToValuePtr<void>(OutDestination.LeafValuePtr);
			OutDestination.LeafProperty = Sub;
		}
		return true;
	}

	/**
	 * "/Game/UI/WBP_Save" -> its generated class.
	 *
	 * The author writes the ASSET path, because that is what they see in the content browser and what
	 * every other tool in the project accepts. Only "/Game/UI/WBP_Save.WBP_Save_C" actually loads, so
	 * that spelling is derived here rather than demanded of the file; a language that made people type
	 * `_C` would be teaching them an implementation detail of the Blueprint compiler.
	 */
	UClass* ResolveWidgetClassFromPath(const FString& InPath)
	{
		if (InPath.StartsWith(TEXT("/Script/")))
		{
			return UClass::TryFindTypeSlowSafe<UClass>(InPath);
		}
		FString ObjectPath = InPath;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			ObjectPath += TEXT(".") + FPackageName::GetShortName(InPath);
		}
		if (!ObjectPath.EndsWith(TEXT("_C")))
		{
			ObjectPath += TEXT("_C");
		}
		if (UClass* Generated = LoadObject<UClass>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Generated;
		}
		// A path that already named a class object exactly, so the _C guess above was wrong.
		return LoadObject<UClass>(nullptr, *InPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/**
	 * The key a translator sees: node id, then which object on that node, then the property.
	 *
	 * The widget and its visual deliberately share the undiscriminated form. Almost every node has
	 * exactly one FText and it is on one of those two, and a key is something a human reads in a
	 * spreadsheet next to the string -- `Title.Text` earns its place, `Title.Widget.Text` does not.
	 * The collision that leaves (the same FText name on both the widget and its visual) does not
	 * occur in the library today and would be one property rename away from being reported.
	 *
	 * Behaviours and panel slots DO carry a discriminator, because there the collision is real: a
	 * node can have several behaviours, and `@slot` reaches a third object entirely, so without one
	 * two different strings would take turns overwriting the same entry with nothing to see.
	 */
	FString MakeLocalizationKey(const FDreamUINode& InNode, const FDreamUIProperty& InProperty,
		const FString& InDiscriminator)
	{
		if (!InProperty.Value.LocalizationKeyOverride.IsEmpty())
		{
			return InProperty.Value.LocalizationKeyOverride;
		}
		return InDiscriminator.IsEmpty()
			? InNode.Id + TEXT(".") + InProperty.Name
			: InNode.Id + TEXT(".") + InDiscriminator + TEXT(".") + InProperty.Name;
	}

	/** "an identifier", "a tuple of 2" -- what the author actually wrote, for a mismatch message. */
	FString DescribeValueKind(const FDreamUIValue& InValue)
	{
		switch (InValue.Kind)
		{
		case EDreamUIValueKind::Identifier: return TEXT("a bare identifier");
		case EDreamUIValueKind::Number:     return TEXT("a number");
		case EDreamUIValueKind::String:     return TEXT("a string");
		case EDreamUIValueKind::Tuple:      return FString::Printf(TEXT("a tuple of %d"), InValue.Elements.Num());
		case EDreamUIValueKind::HexColor:   return TEXT("a hex colour");
		case EDreamUIValueKind::AssetPath:  return TEXT("an asset path");
		}
		return TEXT("a value");
	}

	void RaiseTypeMismatch(const FDreamUIProperty& InProperty, const FProperty* InLeaf, FBuildContext& InContext)
	{
		InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, InProperty.Value.Location,
			FString::Printf(TEXT("'%s' is %s, which cannot produce a %s"), *InProperty.Name,
				*DescribeValueKind(InProperty.Value), *InLeaf->GetCPPType()));
	}

	/**
	 * Put one literal into one live value.
	 *
	 * Order matters and is not arbitrary. FText comes first because localization is a decision about
	 * the DESTINATION, not about the literal, and ImportText would happily produce a culture-invariant
	 * FText that looks right in the editor and ships untranslatable. Short forms come next because
	 * they are the only spellings that are not the reflected one. Everything after that is
	 * ImportText_Direct, which is always correct and merely verbose.
	 */
	bool WriteValue(const FResolvedDestination& InDestination, const FDreamUINode& InNode,
		const FDreamUIProperty& InProperty, FBuildContext& InContext)
	{
		FProperty* Leaf = InDestination.LeafProperty;
		void* ValuePtr = InDestination.LeafValuePtr;
		const FDreamUIValue& Value = InProperty.Value;

		if (const FTextProperty* AsText = CastField<FTextProperty>(Leaf))
		{
			// Quoted, always: an unquoted word on an FText would be a label nobody can translate, and
			// letting it through once means a project full of them by the time anyone notices.
			if (Value.Kind != EDreamUIValueKind::String)
			{
				RaiseTypeMismatch(InProperty, Leaf, InContext);
				return false;
			}
			FString SourceString = Value.Raw;
			AsText->SetPropertyValue(ValuePtr, FText::AsLocalizable_Advanced(InContext.LocalizationNamespace,
				MakeLocalizationKey(InNode, InProperty, InDestination.LocalizationDiscriminator), MoveTemp(SourceString)));
			return true;
		}

		if (DreamUIValueFormat::HasShortForm(Leaf))
		{
			const int32 ExpectedArity = DreamUIValueFormat::GetExpectedTupleArity(Leaf);
			if (Value.Kind == EDreamUIValueKind::Tuple && ExpectedArity != INDEX_NONE && Value.Elements.Num() != ExpectedArity)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::TupleArityMismatch, Value.Location,
					FString::Printf(TEXT("'%s' takes %d values, not %d"), *InProperty.Name, ExpectedArity, Value.Elements.Num()));
				return false;
			}
			// Through the property's native setter when it has one and the destination is the property
			// itself rather than a leaf inside it. The property this exists for is RelativeRotationEuler:
			// a transient mirror whose setter derives the serialized quaternion. A raw write puts the
			// rotation in a field that dies with serialization and leaves the quat -- the value instances
			// are actually built from -- at identity: the preview would rotate and the cooked game would
			// not. Routed generically rather than by property name, because the next Setter-backed
			// property authored in a .dui would hit the same wall and nothing would say so.
			if (!InDestination.bNested && Leaf->HasSetter())
			{
				void* Scratch = FMemory::Malloc(Leaf->GetSize(), Leaf->GetMinAlignment());
				Leaf->InitializeValue(Scratch);
				const bool bParsed = DreamUIValueFormat::Parse(Leaf, Value, Scratch);
				if (bParsed)
				{
					Leaf->SetValue_InContainer(InDestination.Owner, Scratch);
				}
				Leaf->DestroyValue(Scratch);
				FMemory::Free(Scratch);
				if (!bParsed)
				{
					RaiseTypeMismatch(InProperty, Leaf, InContext);
					return false;
				}
				return true;
			}
			if (!DreamUIValueFormat::Parse(Leaf, Value, ValuePtr))
			{
				RaiseTypeMismatch(InProperty, Leaf, InContext);
				return false;
			}
			return true;
		}

		if (UEnum* Enum = GetEnumForProperty(Leaf))
		{
			if (Value.Kind != EDreamUIValueKind::Identifier && Value.Kind != EDreamUIValueKind::Number)
			{
				RaiseTypeMismatch(InProperty, Leaf, InContext);
				return false;
			}
			if (Value.Kind == EDreamUIValueKind::Identifier)
			{
				const int64 EnumValue = Enum->GetValueByNameString(Value.Raw);
				if (EnumValue == INDEX_NONE)
				{
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownEnumValue, Value.Location,
						FString::Printf(TEXT("'%s' is not a value of %s%s"), *Value.Raw, *Enum->GetName(),
							*FormatSuggestion(SuggestNearestEnumValue(Value.Raw, Enum))));
					return false;
				}
				if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Leaf))
				{
					AsEnum->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
				}
				else
				{
					CastFieldChecked<FByteProperty>(Leaf)->SetIntPropertyValue(ValuePtr, EnumValue);
				}
				return true;
			}
		}

		// A quoted string is the author's exact bytes, delimiters already stripped. ImportText would
		// re-interpret escapes and stop at its own delimiters, so these two go in directly.
		if (Value.Kind == EDreamUIValueKind::String)
		{
			if (const FStrProperty* AsStr = CastField<FStrProperty>(Leaf))
			{
				AsStr->SetPropertyValue(ValuePtr, Value.Raw);
				return true;
			}
			if (const FNameProperty* AsName = CastField<FNameProperty>(Leaf))
			{
				AsName->SetPropertyValue(ValuePtr, FName(*Value.Raw));
				return true;
			}
		}

		if (const FSoftObjectProperty* AsSoft = CastField<FSoftObjectProperty>(Leaf))
		{
			// Stored, never loaded: not loading is what SOFT means, and a class template that loaded
			// its soft references would drag every referenced asset into memory at compile time --
			// the exact cost the author chose this property type to avoid. The flip side is stated
			// rather than hidden: a misspelled path is not caught here, it is caught wherever the
			// game first resolves it. FSoftClassProperty comes through this branch too.
			if (Value.Kind == EDreamUIValueKind::Tuple || Value.Kind == EDreamUIValueKind::HexColor)
			{
				RaiseTypeMismatch(InProperty, Leaf, InContext);
				return false;
			}
			const bool bIsNone = Value.Raw.IsEmpty() || Value.Raw == TEXT("None");
			AsSoft->SetPropertyValue(ValuePtr,
				FSoftObjectPtr(bIsNone ? FSoftObjectPath() : FSoftObjectPath(Value.Raw)));
			return true;
		}

		if (const FObjectProperty* AsObject = CastField<FObjectProperty>(Leaf))
		{
			if (Value.Raw.IsEmpty() || Value.Raw == TEXT("None"))
			{
				AsObject->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			// Loaded rather than soft-referenced: a class template holds the same hard reference an
			// author dragging the asset into the details panel would, and the cook has to see it.
			UObject* Loaded = AsObject->IsA<FClassProperty>()
				? (UObject*)ResolveWidgetClassFromPath(Value.Raw)
				: LoadObject<UObject>(nullptr, *Value.Raw, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Loaded == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::AssetNotFound, Value.Location,
					FString::Printf(TEXT("'%s' could not be loaded for '%s'"), *Value.Raw, *InProperty.Name));
				return false;
			}
			if (!Loaded->IsA(AsObject->PropertyClass))
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Value.Location,
					FString::Printf(TEXT("'%s' is a %s, and '%s' takes a %s"), *Value.Raw, *Loaded->GetClass()->GetName(),
						*InProperty.Name, *AsObject->PropertyClass->GetName()));
				return false;
			}
			AsObject->SetObjectPropertyValue(ValuePtr, Loaded);
			return true;
		}

		// Tuples and hex colours only ever meant a short form. Reaching here means the destination has
		// none, and no amount of ImportText will make `(400, 240)` into a float.
		if (Value.Kind == EDreamUIValueKind::Tuple || Value.Kind == EDreamUIValueKind::HexColor)
		{
			RaiseTypeMismatch(InProperty, Leaf, InContext);
			return false;
		}

		// The engine's own importer swallows the error text unless it is given somewhere to put it, and
		// GWarn would spray the log with a message the diagnostic bag is about to report properly.
		FStringOutputDevice ImportErrors;
		if (Leaf->ImportText_Direct(*Value.Raw, ValuePtr, InDestination.Owner, PPF_None, &ImportErrors) == nullptr)
		{
			FString Message = FString::Printf(TEXT("'%s' cannot be read as a %s for '%s'"), *Value.Raw,
				*Leaf->GetCPPType(), *InProperty.Name);
			if (!ImportErrors.IsEmpty())
			{
				Message += TEXT(": ") + ImportErrors.TrimStartAndEnd();
			}
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Value.Location, MoveTemp(Message));
			return false;
		}
		return true;
	}

	/**
	 * Turn `Text <- GetTitleText()` into the compile-time record of it.
	 *
	 * The function is recorded by name and NOT checked: at this point the class that would declare it
	 * does not exist yet -- the compiler is about to build it from this very tree -- so the only
	 * honest thing to do is write the name down. BindingFunctionNotFound belongs to the compiler,
	 * which is the first stage that can tell.
	 */
	bool AddBinding(const FResolvedDestination& InDestination, UDreamWidget* InWidget,
		const FDreamUIProperty& InProperty, FBuildContext& InContext)
	{
		// Both of these are 5008 rather than 5005, and the split is the whole reason 5008 exists: what
		// is wrong here is the KIND of destination, not the property. Told "no setter", a reader goes
		// and writes one, and it still cannot be bound.
		if (!InDestination.bBindable)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingTargetNotSupported, InProperty.Location,
				FString::Printf(TEXT("'%s' lives on %s, which no binding can name -- EDreamWidgetBindingTarget reaches the widget, its visual and its behaviours only"),
					*InProperty.Name, *InDestination.Owner->GetClass()->GetName()));
			return false;
		}
		if (InDestination.bNested)
		{
			// A setter exists for a property, never for a field inside one: SetAnchorData takes the
			// whole struct, so driving AnchorData.SizeDelta would mean reading, patching and writing
			// back every frame -- a different feature, and one nothing downstream can express.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingTargetNotSupported, InProperty.Location,
				FString::Printf(TEXT("'%s' is a field inside a struct, and only whole properties can be bound"),
					*InProperty.Name));
			return false;
		}

		UFunction* Setter = FindDreamWidgetSetterFor(InDestination.Owner->GetClass(), InDestination.LeafProperty);
		if (Setter == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingTargetHasNoSetter, InProperty.Location,
				FString::Printf(TEXT("'%s' on %s has no %s to drive it, so it cannot be bound"), *InProperty.Name,
					*InDestination.Owner->GetClass()->GetName(),
					*MakeDreamWidgetSetterName(InDestination.LeafProperty).ToString()));
			return false;
		}

		FDreamWidgetPropertyBinding Binding;
		// Never sanitized a second time here. UDreamWidgetTree::MakeWidgetVariableName is the one
		// implementation the runtime resolves bindings with, and a private copy that differs by one
		// character is precisely how a binding reports success and comes back null.
		Binding.WidgetName = UDreamWidgetTree::MakeWidgetVariableName(InWidget);
		Binding.Target = InDestination.BindingTarget;
		Binding.BehaviourIndex = InDestination.BehaviourIndex;
		Binding.PropertyName = InDestination.LeafProperty->GetFName();
		Binding.SetterName = Setter->GetFName();
		// The parser already hands over a bare identifier -- `()` is grammar, not part of the name --
		// so the trim is only for the other caller this struct has: an editor or a test building an
		// FDreamUIProperty by hand, which naturally writes what the author would have typed.
		FString FunctionName = InProperty.BindingFunction.TrimStartAndEnd();
		FunctionName.RemoveFromEnd(TEXT("()"));
		Binding.FunctionName = FName(*FunctionName.TrimStartAndEnd());
		InContext.Bindings->Add(Binding);
		return true;
	}

	/** One `Name = Value` or `Name <- Func()` against a set of candidate destinations. */
	void ApplyProperty(const FDreamUINode& InNode, const FDreamUIProperty& InProperty, UDreamWidget* InWidget,
		TConstArrayView<FDestinationCandidate> InCandidates, const FString& InDestinationDescription,
		FBuildContext& InContext, bool bInSuggestVisualTag = false)
	{
		FResolvedDestination Destination;
		if (!ResolveDestination(InProperty, InCandidates, InDestinationDescription, InContext, Destination, bInSuggestVisualTag))
		{
			return;
		}
		if (InProperty.IsBinding())
		{
			AddBinding(Destination, InWidget, InProperty, InContext);
			return;
		}
		FString Reason;
		if (!IsWritableFromText(Destination.LeafProperty, Reason))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::PropertyNotWritable, InProperty.Location,
				FString::Printf(TEXT("'%s' cannot be written from a .dui because %s"), *InProperty.Name, *Reason));
			return;
		}
		WriteValue(Destination, InNode, InProperty, InContext);
	}
}

UClass* FDreamUITextBuilder::FindVisualClassForTag(const FString& InTag, bool& bOutIsKnownTag)
{
	for (const TPair<const TCHAR*, UClass*>& Entry : DreamUITextBuilderLocal::GetVisualTagTable())
	{
		if (InTag == Entry.Key)
		{
			bOutIsKnownTag = true;
			return Entry.Value;
		}
	}
	bOutIsKnownTag = false;
	return nullptr;
}

bool FDreamUITextBuilder::IsWritableFromText(const FProperty* InProperty, FString& OutReason)
{
	// A forwarder, so the rule keeps exactly one body while the local callers above keep the
	// unqualified name they already use.
	return DreamUITextBuilderLocal::IsWritableFromText(InProperty, OutReason);
}

UClass* FDreamUITextBuilder::ResolveComponentClass(const FString& InClassName)
{
	const FString Name = InClassName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		return nullptr;
	}

	UClass* Found = nullptr;
	if (Name.StartsWith(TEXT("/")))
	{
		Found = UClass::TryFindTypeSlowSafe<UClass>(Name);
		if (Found == nullptr)
		{
			Found = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
	else
	{
		// Prefixes rather than an alias table. `Canvas` finds UDreamCanvas, `Button` finds UUIButton
		// and `VerticalBox` finds UDreamLayoutContainerVerticalBox without any of them being written
		// down anywhere, so adding a behaviour to the library adds it to the language -- a table
		// would be a second place to remember, and the one that gets forgotten. UIML's four
		// hand-written aliases all fall out of this.
		//
		// Longest last only matters for reading: the names in each family are distinct, so no input
		// resolves under two prefixes.
		static const TCHAR* Prefixes[] =
		{
			TEXT(""), TEXT("Dream"), TEXT("UI"), TEXT("DreamLayoutContainer"), TEXT("DreamLayoutSelf")
		};
		for (const TCHAR* Prefix : Prefixes)
		{
			Found = UClass::TryFindTypeSlowSafe<UClass>(FString::Printf(TEXT("/Script/DreamGUI.%s%s"), Prefix, *Name));
			if (Found != nullptr)
			{
				break;
			}
		}
		if (Found == nullptr)
		{
			// A behaviour from the game module or another plugin. Last, because it is the slow lookup
			// and the ambiguous one, and native-first so a Blueprint of the same name never wins.
			Found = FindFirstObjectSafe<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
		}
	}

	if (Found == nullptr || Found->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	// Layout containers are accepted alongside behaviours, and that is a deliberate widening of what
	// `+` means. They are not UDreamUIBehaviour -- they are UDreamWidgetSubObjectBehaviour, a separate
	// hierarchy -- so without this the language cannot produce a panel at all, which in turn means no
	// child ever gets a UDreamPanelSlot and every `@slot` line in every file resolves to
	// NoPanelSlotForProperty. A diagnostic that is always right is one nobody can act on.
	return Found->IsChildOf(UDreamUIBehaviour::StaticClass())
		|| Found->IsChildOf(UDreamLayoutContainer::StaticClass())
		|| Found->IsChildOf(UDreamLayoutSelf::StaticClass())
		? Found : nullptr;
}

namespace DreamUITextBuilderLocal
{
	UDreamWidget* BuildNode(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext);

	/** `+ Xxx { … }` -- create the sub-object and write its properties onto IT, not onto the widget. */
	void BuildComponents(const FDreamUINode& InNode, UDreamWidget* InWidget, FBuildContext& InContext)
	{
		// Everything is created before anything is written, because creating one can change the
		// indices of the others: a panel layout container pulls in its required behaviours
		// (SyncRequiredBehavioursForLayoutContainer), and a binding recorded against a stale index
		// would drive whichever behaviour happened to land there instead.
		TArray<UObject*> Created;
		Created.Reserve(InNode.Components.Num());
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
			if (ComponentClass == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownBehaviourClass, Component.Location,
					FString::Printf(TEXT("'%s' is not a behaviour or layout this widget can carry"), *Component.ClassName));
				Created.Add(nullptr);
				continue;
			}
			if (ComponentClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
			{
				UDreamLayoutContainer* Previous = InWidget->GetLayoutContainer();
				UDreamLayoutContainer* Container = InWidget->CreateNewLayoutContainer(ComponentClass);
				InWidget->SyncRequiredBehavioursForLayoutContainer(Previous, Container);
				Created.Add(Container);
			}
			else if (ComponentClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
			{
				Created.Add(InWidget->CreateNewLayoutSelf(ComponentClass));
			}
			else
			{
				Created.Add(InWidget->AddComponent(ComponentClass));
			}
		}

		for (int32 ComponentIndex = 0; ComponentIndex < InNode.Components.Num(); ComponentIndex++)
		{
			UObject* Object = Created[ComponentIndex];
			if (!IsValid(Object))
			{
				continue;
			}
			const FDreamUIComponent& Component = InNode.Components[ComponentIndex];
			FDestinationCandidate Candidate;
			Candidate.Object = Object;
			Candidate.Target = EDreamWidgetBindingTarget::Behaviour;
			// Read back rather than assumed: see the note above about required behaviours. A layout
			// container is not in Components at all, so this stays INDEX_NONE and the destination
			// reports itself unbindable -- which it is, EDreamWidgetBindingTarget cannot name one.
			if (UDreamUIBehaviour* AsBehaviour = Cast<UDreamUIBehaviour>(Object))
			{
				Candidate.BehaviourIndex = InWidget->GetAllComponents().Find(AsBehaviour);
			}
			Candidate.bBindable = Candidate.BehaviourIndex != INDEX_NONE;
			// The AUTHORED ordinal, not BehaviourIndex: a layout container is not in Components at all
			// and would key as -1, and a panel that pulls in its required behaviours would shift every
			// index after it -- silently re-keying strings that nobody edited. This one counts the `+`
			// lines in the file, which is also what the author would count.
			Candidate.LocalizationDiscriminator = FString::Printf(TEXT("%s_%d"),
				*Object->GetClass()->GetName(), ComponentIndex);
			const FString Description = FString::Printf(TEXT("behaviour '%s'"), *Component.ClassName);
			for (const FDreamUIProperty& Property : Component.Properties)
			{
				ApplyProperty(InNode, Property, InWidget, { Candidate }, Description, InContext);
			}
		}
	}

	/** `@slot Padding = (8, 8, 8, 8)` -- the child's own UDreamPanelSlot, which the PARENT's layout gives it. */
	void BuildSlotProperties(const FDreamUINode& InNode, UDreamWidget* InWidget, FBuildContext& InContext)
	{
		if (InNode.SlotProperties.Num() == 0)
		{
			return;
		}
		UDreamPanelSlot* Slot = InWidget->GetPanelSlot();
		if (!IsValid(Slot))
		{
			// EnsurePanelSlotForChild does this at registration, which an authoring tree never reaches,
			// so the slot has to be minted here or the author's padding would be written to nothing and
			// then created empty on the first instance. Same condition it uses: only a panel layout
			// hands out slots, and the Dream flex box and grid arrange children without any.
			UDreamWidget* Parent = InWidget->GetParent();
			if (IsValid(Parent) && Parent->HasPanelSlots())
			{
				Slot = InWidget->CreateNewPanelSlot<UDreamPanelSlot>();
			}
		}
		if (!IsValid(Slot))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NoPanelSlotForProperty, InNode.Location,
				FString::Printf(TEXT("'%s' has @slot properties, but its parent lays out no panel slots"), *InNode.Id));
			return;
		}
		FDestinationCandidate Candidate;
		Candidate.Object = Slot;
		// A panel slot is not a UDreamWidget, a UDreamVisual or a behaviour, so nothing in
		// EDreamWidgetBindingTarget names it. Assignments land fine; `<-` is refused, loudly.
		Candidate.bBindable = false;
		// Unnumbered, unlike a behaviour's: a widget has exactly one panel slot, ever.
		Candidate.LocalizationDiscriminator = TEXT("Slot");
		for (const FDreamUIProperty& Property : InNode.SlotProperties)
		{
			ApplyProperty(InNode, Property, InWidget, { Candidate }, TEXT("the panel slot"), InContext);
		}
	}

	/** Style first, node second. The whole point of a style is that the node gets the last word. */
	void BuildProperties(const FDreamUINode& InNode, UDreamWidget* InWidget, FBuildContext& InContext)
	{
		TArray<FDestinationCandidate> Candidates;
		{
			FDestinationCandidate WidgetCandidate;
			WidgetCandidate.Object = InWidget;
			WidgetCandidate.Target = EDreamWidgetBindingTarget::Widget;
			Candidates.Add(WidgetCandidate);
		}
		if (UDreamVisual* Visual = InWidget->GetVisual())
		{
			FDestinationCandidate VisualCandidate;
			VisualCandidate.Object = Visual;
			VisualCandidate.Target = EDreamWidgetBindingTarget::Visual;
			Candidates.Add(VisualCandidate);
		}
		const FString Description = InWidget->GetVisual() != nullptr
			? FString::Printf(TEXT("'%s' or its %s"), *InNode.Id, *InWidget->GetVisual()->GetClass()->GetName())
			: FString::Printf(TEXT("'%s'"), *InNode.Id);

		if (!InNode.StyleName.IsEmpty())
		{
			if (const FDreamUIStyle* Style = InContext.Ast->FindStyle(InNode.StyleName))
			{
				for (const FDreamUIProperty& Property : Style->Properties)
				{
					ApplyProperty(InNode, Property, InWidget, Candidates, Description, InContext, true);
				}
			}
			else
			{
				// Deliberately the second place this is checked -- FDreamUISourceFile catches it too,
				// and in the normal pipeline the parse fails first so this never fires. It stays
				// because the builder's other caller is an AST built by hand (the designer, a test),
				// and there the alternative is applying no style and saying nothing.
				//
				// DuplicateNodeId is NOT mirrored the same way, and the difference is what makes this
				// defensible rather than a habit: duplicate ids are a property of the whole FILE, so
				// checking them here would mean the builder keeping its own id set to answer a
				// question it is not the authority on. "Is this style declared" is one lookup in the
				// AST the builder was handed.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownStyle, InNode.Location,
					FString::Printf(TEXT("no style named '%s' is declared in this file"), *InNode.StyleName));
			}
		}

		for (const FDreamUIProperty& Property : InNode.Properties)
		{
			// Only here is the visual-tag hint meaningful: a bare name is the one destination that
			// depends on the node's TYPE, which is the thing the author would have to change.
			ApplyProperty(InNode, Property, InWidget, Candidates, Description, InContext, true);
		}
	}

	/**
	 * Which UDreamWidget subclass and which UDreamVisual this node asks for.
	 *
	 * Returns false only when nothing can be created; a null visual class with a true return is the
	 * ordinary `Widget` case, not a failure.
	 */
	bool ResolveNodeClasses(const FDreamUINode& InNode, FBuildContext& InContext, UClass*& OutWidgetClass, UClass*& OutVisualClass)
	{
		OutWidgetClass = UDreamWidget::StaticClass();
		OutVisualClass = nullptr;

		if (InNode.Kind == EDreamUINodeKind::NamedSlot)
		{
			return true;
		}
		if (InNode.TypeName.StartsWith(TEXT("/")))
		{
			UClass* Loaded = ResolveWidgetClassFromPath(InNode.TypeName);
			if (Loaded == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::AssetNotFound, InNode.Location,
					FString::Printf(TEXT("'%s' could not be loaded"), *InNode.TypeName));
				return false;
			}
			if (!Loaded->IsChildOf(UDreamUserWidget::StaticClass()))
			{
				// A widget blueprint is what nesting means here: the node becomes an instance whose
				// contents come from its own class. A plain UDreamWidget subclass has no class-level
				// hierarchy to expand and would silently place an empty node.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NotAUserWidgetClass, InNode.Location,
					FString::Printf(TEXT("'%s' is a %s, and a nested node must be a DreamUI user widget"),
						*InNode.TypeName, *Loaded->GetName()));
				return false;
			}
			OutWidgetClass = Loaded;
			return true;
		}

		bool bIsKnownTag = false;
		OutVisualClass = FDreamUITextBuilder::FindVisualClassForTag(InNode.TypeName, bIsKnownTag);
		if (!bIsKnownTag)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownNodeType, InNode.Location,
				FString::Printf(TEXT("'%s' is neither a built-in tag nor an asset path (those start with '/')"), *InNode.TypeName));
			return false;
		}
		return true;
	}

	UDreamWidget* BuildNode(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext)
	{
		if (InNode.Kind == EDreamUINodeKind::ForLoop || InNode.Kind == EDreamUINodeKind::EachLoop)
		{
			// A warning rather than an error: the rest of the file is still a tree worth building, and
			// an author previewing a screen wants to see the parts that do work.
			InContext.Diagnostics->AddWarning(EDreamUIDiagnosticCode::LoopNotExpanded, InNode.Location,
				FString::Printf(TEXT("'%s' loops are parsed but not yet expanded, so everything under this one was skipped"),
					InNode.Kind == EDreamUINodeKind::ForLoop ? TEXT("for") : TEXT("each")));
			return nullptr;
		}

		UClass* WidgetClass = nullptr;
		UClass* VisualClass = nullptr;
		if (!ResolveNodeClasses(InNode, InContext, WidgetClass, VisualClass))
		{
			return nullptr;
		}

		// THE rule. Not NewObject<UDreamWidget>(World, …), which is what UIML did and why a UIML
		// hierarchy is flat-outered to the world, owned by nothing, and can never become a class
		// template, enter the designer, or carry a binding. See UDreamWidgetTree's class comment.
		UDreamWidget* Widget = InContext.Tree->ConstructWidget(WidgetClass);
		if (!IsValid(Widget))
		{
			// Not reachable today -- ResolveNodeClasses has already established the class is valid and
			// concrete, and ConstructWidget only refuses a null one. It reuses UnknownNodeType rather
			// than earning a code of its own precisely because it has no cause a reader could act on:
			// a new code here would be a docs page saying "this should not happen".
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownNodeType, InNode.Location,
				FString::Printf(TEXT("'%s' could not be constructed as a widget"), *InNode.TypeName));
			return nullptr;
		}
		// The one name the language has. Everything else -- the member variable the compiler declares,
		// the key a binding resolves through, the localization key -- is derived from it, by
		// MakeWidgetVariableName and never by a second copy of that walk.
		Widget->SetDisplayName(InNode.Id);

		if (VisualClass != nullptr)
		{
			Widget->CreateNewVisual(VisualClass);
		}
		if (InNode.Kind == EDreamUINodeKind::NamedSlot)
		{
			// The slot's name IS the widget's display name (UDreamNamedSlot::GetSlotName), so the
			// SetDisplayName above is what named it; there is nothing further to configure.
			Widget->AddComponent<UDreamNamedSlot>();
		}

		if (InParent != nullptr)
		{
			// Attached before any property is written: false keeps the relative transform, and doing
			// it first means the panel-slot lookup below sees the parent it will actually have.
			//
			// Try, not Set, because the refusal is real and silent otherwise: a `slot` declaration and
			// a ContentWidget both cap their children at one, so a second child under either is
			// dropped on the floor by SetParent with nothing said. The tree would build, look right in
			// a structural test, and be missing a widget.
			if (!Widget->TrySetParent(InParent, false))
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ParentRefusedChild, InNode.Location,
					FString::Printf(TEXT("'%s' refused '%s' as a child -- it is full (%d children at most) or the nesting is circular"),
						*InParent->GetDisplayName(), *InNode.Id, InParent->GetMaxChildrenCapacity()));
				return nullptr;
			}
		}

		// Components before children, because a `+ VerticalBox` on THIS node is what decides whether
		// the children get panel slots at all.
		BuildComponents(InNode, Widget, InContext);
		BuildProperties(InNode, Widget, InContext);
		BuildSlotProperties(InNode, Widget, InContext);

		// The slot a panel parent hands out is minted by TrySetParent above, which is BEFORE this
		// node's AnchorData was written -- so the geometry it captured is the class default, not what
		// the file says. CaptureAuthoredGeometry latches on first call and the registration path will
		// not re-take it, so an authored size would be quietly replaced by 100x100 the first time a
		// panel arranged the instance. Forced, because that latch is exactly what has to be broken.
		if (UDreamPanelSlot* Slot = Widget->GetPanelSlot())
		{
			Slot->CaptureAuthoredGeometry(true);
		}

		for (const FDreamUINode& Child : InNode.Children)
		{
			BuildNode(Child, Widget, InContext);
		}
		return Widget;
	}
}

UDreamWidgetTree* FDreamUITextBuilder::Build(const FDreamUIAst& InAst, UObject* InOuter,
	FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamWidgetPropertyBinding>& OutBindings)
{
	using namespace DreamUITextBuilderLocal;

	// Counted rather than tested, so a bag the parser already put errors in does not make this build
	// look failed. "Did the build work" and "is the file clean" are different questions and the
	// caller asks both.
	const int32 ErrorsBefore = OutDiagnostics.NumErrors();

	if (!InAst.bHasRoot)
	{
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::NothingToBuild, InAst.ClassPathLocation,
			TEXT("there is no root node to build"));
		return nullptr;
	}

	FBuildContext Context;
	Context.Ast = &InAst;
	Context.Diagnostics = &OutDiagnostics;
	Context.Bindings = &OutBindings;
	// The namespace a translator sees. ClassPath makes it stable across a rename of the .dui file,
	// which the source name would not; the source name is the fallback for a file that has not been
	// given a class yet, which is every file in an editor preview before it is first compiled.
	Context.LocalizationNamespace = InAst.ClassPath.IsEmpty() ? OutDiagnostics.SourceName : InAst.ClassPath;
	Context.Tree = NewObject<UDreamWidgetTree>(InOuter != nullptr ? InOuter : (UObject*)GetTransientPackage());

	Context.Tree->RootWidget = BuildNode(InAst.Root, nullptr, Context);
	if (!IsValid(Context.Tree->RootWidget))
	{
		return nullptr;
	}
	// TrySetParent already set every back-pointer on the way down. This is the belt to that braces:
	// the tree's invariant is that Parent is derivable from Children, and a builder that leaves it
	// almost-true hands the designer a tree whose GetParent() is null in one place nobody looks.
	Context.Tree->RebuildParentLinks();

	return OutDiagnostics.NumErrors() > ErrorsBefore ? nullptr : Context.Tree;
}
