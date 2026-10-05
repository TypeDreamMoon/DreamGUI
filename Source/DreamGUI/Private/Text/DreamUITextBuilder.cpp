// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUITextBuilder.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIValueFormat.h"

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
// A `->` route may name an FDreamUIEventDelegate as well as a multicast delegate.
#include "Event/DreamUIEventDelegate.h"
#include "Core/DreamUIEachBindingHandler.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamUIAnimEventTrack.h"
#include "DreamTweener.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneStringChannel.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "UObject/CoreRedirects.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneDoubleSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

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

	/**
	 * The nearest node id in a built tree, for a `Prop = SomeNode` line that named nothing.
	 *
	 * Over DISPLAY names rather than variable names, because the display name is the id the author
	 * typed and the one they have to correct; the sanitized form is an implementation detail they
	 * never see. Only ever asked after a lookup has already failed, like the two above.
	 */
	FString SuggestNearestNodeId(const FString& InName, const UDreamWidgetTree* InTree)
	{
		if (InTree == nullptr)
		{
			return FString();
		}
		FString Best;
		int32 BestDistance = MAX_int32;
		InTree->ForEachWidget([&Best, &BestDistance, &InName](UDreamWidget* Widget)
		{
			const int32 Distance = EditDistance(InName, Widget->GetDisplayName());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Widget->GetDisplayName();
			}
		});
		const int32 Ceiling = FMath::Max(2, InName.Len() / 3);
		return BestDistance <= Ceiling ? Best : FString();
	}

	/** " (did you mean 'FontSize'?)", or nothing. Kept out of the message sites so they stay readable. */
	FString FormatSuggestion(const FString& InSuggestion)
	{
		return InSuggestion.IsEmpty() ? FString() : FString::Printf(TEXT(" (did you mean '%s'?)"), *InSuggestion);
	}

	/**
	 * True when InText can become an FName without taking the editor down with it.
	 *
	 * FName does not REFUSE a string of NAME_SIZE characters or more -- it calls checkf(false)
	 * (UnrealNames.cpp, FindOrStoreString) -- so every conversion of AUTHORED text has to be gated.
	 * The lexer already refuses an over-long identifier (DUI1006), which covers everything that
	 * arrives through a parse; these guards are for the builder's other callers, which assemble an
	 * FDreamUINode by hand (the designer, the tests), and for the one authored value that is not an
	 * identifier at all: a quoted string written onto an FName property.
	 */
	bool IsNameLengthLegal(const FString& InText)
	{
		return InText.Len() < NAME_SIZE;
	}

	/** The first 32 characters and an ellipsis -- long enough to recognise, short enough to read. */
	FString EllipsizeName(const FString& InText)
	{
		return InText.Len() <= 32 ? InText : (InText.Left(32) + TEXT("..."));
	}

	/**
	 * An error about a DECLARATION, stamped with the file that declares it rather than the file
	 * being compiled.
	 *
	 * A `use` merges another file's styles and resources into this AST wholesale, so their Locations
	 * count lines in a file the importer has never opened. Reported through the bag's own name they
	 * came out as "Login.dui(4,2): ..." pointing at whatever happens to be on line 4 of Login.dui --
	 * a position that looks authoritative and is not. FDreamUIDiagnosticBag::Add falls back to the
	 * bag's name when this one is empty, so a hand-built AST behaves exactly as it did before.
	 */
	void AddErrorIn(FDreamUIDiagnosticBag& InBag, const FString& InSourceName, EDreamUIDiagnosticCode InCode,
		const FDreamUISourceLocation& InLocation, FString InMessage)
	{
		FDreamUIDiagnostic Diagnostic(InCode, InLocation, MoveTemp(InMessage), EDreamUISeverity::Error);
		Diagnostic.SourceName = InSourceName;
		InBag.Add(MoveTemp(Diagnostic));
	}

	/**
	 * The built-in tags, and the UDreamVisual each one creates -- now asked of the registry the
	 * DECLARE_DREAM_GUI_VISUAL macro fills, rather than kept as a list here.
	 *
	 * This used to BE the list, and the list is why the language knew nine visuals: the plugin ships
	 * twenty, and the other eleven were placeable from the palette and unspellable in a `.dui` with
	 * nothing anywhere saying so. A declaration next to each class cannot drift from the class the
	 * way a table in a different module can.
	 *
	 * Rebuilt per call rather than cached in a static: registrations arrive during static
	 * initialisation, and a cache built on the first call would be correct only if that call came
	 * after the last registration -- which is a link-order bet. The list is twenty entries long and
	 * every caller is a diagnostic or an export.
	 */
	TArray<TPair<FName, UClass*>> GetVisualTagTable()
	{
		TArray<TPair<FName, UClass*>> Table;
		FDreamUIWidgetRegistry::GetVisualEntries(Table);
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

	/**
	 * The tag whose visual declares InName, when one does. Only ever asked after a name has failed.
	 *
	 * FIRST match, over a list the registry sorts by tag name. Sorted rather than in registration
	 * order because registration order is static-initialisation order across translation units --
	 * a link-order detail -- and a hint that says `Sprite` on one build and `Texture` on the next
	 * teaches an author something that is not true.
	 */
	FString FindTagWhoseVisualDeclares(const FString& InName)
	{
		for (const TPair<FName, UClass*>& Entry : GetVisualTagTable())
		{
			if (Entry.Value != nullptr && FindFProperty<FProperty>(Entry.Value, *InName) != nullptr)
			{
				return Entry.Key.ToString();
			}
		}
		return FString();
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
		// A weak reference is never a containment edge: it keeps nothing alive, so writing one cannot
		// make the structure and the Children array disagree. It carries the flag anyway, because
		// UDreamVisual is DefaultToInstanced and UHT sets CPF_InstancedReference from the class rather
		// than from the pointer -- which is why UUIToggle::ToggleTransitionTarget, a property whose whole
		// job is to POINT AT another node's visual, was refused as if it owned one.
		if (InProperty->HasAnyPropertyFlags(CPF_InstancedReference) && !InProperty->IsA<FWeakObjectProperty>())
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

	/**
	 * One `Prop = SomeNode` line, held until the whole tree exists.
	 *
	 * A node is regularly referenced from ABOVE its own declaration -- a toggle at the top of a file
	 * pointing at the check mark nested three levels below it -- and properties are written on the
	 * way DOWN the tree, so at the moment the line is read its target usually has not been built. The
	 * destination is fully resolved here (that part only needs the object being written to, which
	 * does exist); only the lookup and the write wait. See ResolveNodeReferences.
	 *
	 * LeafValuePtr is an address inside a UObject, which never moves, and no object a property was
	 * resolved against is destroyed later in the walk -- a node that fails gives up before any of its
	 * properties are applied.
	 */
	struct FPendingNodeReference
	{
		const FObjectPropertyBase* Property = nullptr;
		void* LeafValuePtr = nullptr;
		/** The id exactly as written: sanitized for the lookup, quoted verbatim in the message. */
		FString NodeId;
		/** For the message only -- the destination above is already resolved. */
		FString PropertyName;
		FDreamUISourceLocation Location;
	};

	struct FBuildContext
	{
		const FDreamUIAst* Ast = nullptr;
		UDreamWidgetTree* Tree = nullptr;
		FDreamUIDiagnosticBag* Diagnostics = nullptr;
		TArray<FDreamWidgetPropertyBinding>* Bindings = nullptr;
		/** Null when the caller has no use for events -- a reference tree, most tests. */
		TArray<FDreamWidgetEventBinding>* EventBindings = nullptr;
		/** Null likewise; an `each` met without it degrades to the parsed-not-built warning. */
		TArray<FDreamWidgetEachBinding>* EachBindings = nullptr;
		/** Non-null while building an `each` body: item-scoped bindings divert into it. */
		FDreamWidgetEachBinding* ActiveEach = nullptr;
		FString ActiveLoopVariable;
		/** Filled during the walk, drained by ResolveNodeReferences once the tree is whole. */
		TArray<FPendingNodeReference> NodeReferences;
		/** ClassPath, or the source name when the file declares no class. Fixed for the whole build. */
		FString LocalizationNamespace;
		/** The first `slot … default` met, so a second one can say where the first is. */
		const FDreamUINode* DefaultSlotNode = nullptr;
		/** Component aliases already resolved to a class, so a family used on fifty rows asks the editor once. */
		TMap<FString, UClass*> ResolvedAliasClasses;
		/** What FindContainerClassForType answered per node type, for the same reason: it is a search, not a lookup. */
		TMap<FString, UClass*> ContainerClassesByType;
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
			const FString Tag = bInSuggestVisualTag ? FindTagWhoseVisualDeclares(Segments[0]) : FString();
			if (!Tag.IsEmpty())
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NoVisualForProperty, InProperty.Location,
					FString::Printf(TEXT("'%s' belongs to the visual a '%s' node creates, and %s has no such visual"),
						*Segments[0], *Tag, *InDestinationDescription));
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
	 * True, with the error said, when InPath is too long to load: a load splits it into names, and a name past NAME_SIZE
	 * stops the editor rather than fail (see EDreamUIDiagnosticCode::AssetPathTooLong). The lexer cuts a path written bare;
	 * this is for one that arrives as a string.
	 */
	bool RefuseOverlongPath(const FString& InPath, const FDreamUISourceLocation& InLocation, FDreamUIDiagnosticBag* InDiagnostics)
	{
		if (InPath.Len() < NAME_SIZE)
		{
			return false;
		}
		if (InDiagnostics != nullptr)
		{
			InDiagnostics->AddError(EDreamUIDiagnosticCode::AssetPathTooLong, InLocation,
				FString::Printf(TEXT("a path of %d characters is longer than a load can take (%d)"), InPath.Len(), NAME_SIZE - 1));
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
		// The spelling derived below gives a path with no object name its short name plus _C as one: a path near what a name
		// holds would make that name longer than one can be, so it is not looked up at all.
		if (InPath.Len() >= NAME_SIZE - 3)
		{
			return nullptr;
		}
		if (InPath.StartsWith(TEXT("/Script/")))
		{
			// Redirected first: the lookup takes the path as written, and a class that has moved to
			// another module or been renamed answers only to where it is now.
			return UClass::TryFindTypeSlowSafe<UClass>(DreamUI::ApplyTypeRedirects(ECoreRedirectFlags::Type_Class, InPath));
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
	/**
	 * The value a `@Name` stands for, or null with a diagnostic.
	 *
	 * Substitution happens HERE, at the use site, rather than in a prepass that rewrites the AST:
	 * the patcher owns the AST's raw text and byte offsets, and an AST whose values had been
	 * replaced under it would splice literals into the wrong columns. What the entry declares and
	 * what it holds are checked against each other too -- `Color Accent = 8` fails on the ENTRY's
	 * line, once, instead of as a shape mismatch on every line that says `@Accent`.
	 */
	const FDreamUIValue* ResolveResourceRef(const FDreamUIValue& InValue, const FDreamUIProperty& InProperty,
		FBuildContext& InContext)
	{
		const FDreamUIResource* Resource = InContext.Ast != nullptr
			? InContext.Ast->FindResource(InValue.Raw) : nullptr;
		if (Resource == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownResource, InValue.Location,
				FString::Printf(TEXT("'@%s' names no entry in a resources block"), *InValue.Raw));
			return nullptr;
		}

		// The declared type against the entry's own literal. A small closed table, one shape each,
		// because an entry is authored without a destination and this is its only chance to be
		// checked where the mistake was made.
		struct FResourceShape { const TCHAR* TypeName; EDreamUIValueKind Kind; int32 TupleArity; };
		static const FResourceShape Shapes[] =
		{
			{ TEXT("Color"),   EDreamUIValueKind::HexColor, INDEX_NONE },
			{ TEXT("Number"),  EDreamUIValueKind::Number,   INDEX_NONE },
			{ TEXT("Vector2"), EDreamUIValueKind::Tuple,    2 },
			{ TEXT("String"),  EDreamUIValueKind::String,   INDEX_NONE },
			{ TEXT("Asset"),   EDreamUIValueKind::AssetPath, INDEX_NONE },
		};
		const FResourceShape* Shape = nullptr;
		for (const FResourceShape& Candidate : Shapes)
		{
			if (Resource->TypeName.Equals(Candidate.TypeName, ESearchCase::IgnoreCase))
			{
				Shape = &Candidate;
				break;
			}
		}
		if (Shape == nullptr)
		{
			// Reported against the file that DECLARES the entry: `use` can have brought it in from a
			// style library, and Location counts lines there, not here.
			AddErrorIn(*InContext.Diagnostics, Resource->SourceName, EDreamUIDiagnosticCode::ResourceTypeMismatch, Resource->Location,
				FString::Printf(TEXT("'%s' is not a resource type; write Color, Number, Vector2, String or Asset"),
					*Resource->TypeName));
			return nullptr;
		}
		// A quoted path is accepted where an Asset is declared: both spell an FSoftObjectPath, and
		// refusing the quoted one would make copy-pasting from the editor's Copy Reference a syntax
		// exercise.
		const bool bShapeMatches = Resource->Value.Kind == Shape->Kind
			|| (Shape->Kind == EDreamUIValueKind::AssetPath && Resource->Value.Kind == EDreamUIValueKind::String);
		if (!bShapeMatches
			|| (Shape->TupleArity != INDEX_NONE && Resource->Value.Elements.Num() != Shape->TupleArity))
		{
			AddErrorIn(*InContext.Diagnostics, Resource->SourceName, EDreamUIDiagnosticCode::ResourceTypeMismatch, Resource->Location,
				FString::Printf(TEXT("resource '%s' is declared %s, but its value is not one"),
					*Resource->Name, *Resource->TypeName));
			return nullptr;
		}
		(void)InProperty;
		return &Resource->Value;
	}

	/**
	 * Whether a bare name written on this property means a node in this file rather than an asset.
	 *
	 * Exactly the two class hierarchies the language can NAME: a node builds a UDreamWidget, that
	 * widget may own a UDreamVisual, and nothing else in a tree has an identity the file wrote down.
	 * Deliberately not widened to every object property -- on a UTexture2D* the only thing a bare
	 * name can be is a mistyped asset path, and reporting that as a missing node would send the
	 * reader looking for a node they never meant to write.
	 */
	bool IsNodeReferenceProperty(const FObjectPropertyBase* InProperty)
	{
		const UClass* Wanted = InProperty->PropertyClass;
		// The three things a node can supply: itself, its visual, and a behaviour on it. Nothing else in
		// the library is a per-node object, and each of the three is named by the property's own type --
		// UDreamWidget, UDreamVisual and UDreamUIBehaviour share no ancestor below UObject, so no value
		// is ambiguous about which was meant. Behaviours are here because a whole family of properties
		// exists only to name one on a sibling: UUISelectable's six explicit-navigation members and
		// UUIToggle::ToggleGroup.
		return Wanted != nullptr
			&& (Wanted->IsChildOf(UDreamWidget::StaticClass())
				|| Wanted->IsChildOf(UDreamVisual::StaticClass())
				|| Wanted->IsChildOf(UDreamUIBehaviour::StaticClass()));
	}

	bool WriteValue(const FResolvedDestination& InDestination, const FDreamUINode& InNode,
		const FDreamUIProperty& InProperty, FBuildContext& InContext)
	{
		FProperty* Leaf = InDestination.LeafProperty;
		void* ValuePtr = InDestination.LeafValuePtr;
		const FDreamUIValue& Authored = InProperty.Value;

		// A TRANSIENT property with a native setter is a face over something else -- `Shown` over Visibility, the
		// Animatable mirrors over AnchorData -- and its own field is exactly what does not survive a save. IsWritableFromText
		// lets such a property through because of the setter, so the write has to actually go through it, whatever
		// the type: the short-form branch below always did for its types, and every other branch wrote the field,
		// which made `Shown = false` compile green and leave the widget visible. Parsed into a scratch value by this
		// same function (marked nested, so it writes the scratch and does not come back here), then handed over.
		// Not for an object reference, whose write may be deferred to a pass that holds on to the address.
		if (!InDestination.bNested && Leaf->HasSetter() && Leaf->HasAnyPropertyFlags(CPF_Transient)
			&& CastField<FObjectPropertyBase>(Leaf) == nullptr)
		{
			void* Scratch = FMemory::Malloc(Leaf->GetSize(), Leaf->GetMinAlignment());
			Leaf->InitializeValue(Scratch);
			Leaf->GetValue_InContainer(InDestination.Owner, Scratch);
			FResolvedDestination IntoScratch = InDestination;
			IntoScratch.LeafValuePtr = Scratch;
			IntoScratch.bNested = true;
			const bool bWritten = WriteValue(IntoScratch, InNode, InProperty, InContext);
			if (bWritten)
			{
				Leaf->SetValue_InContainer(InDestination.Owner, Scratch);
			}
			Leaf->DestroyValue(Scratch);
			FMemory::Free(Scratch);
			return bWritten;
		}

		// `@Name` is resolved before any type branch looks at the value, so every branch below --
		// FText localization included -- sees the same literal it would have seen written inline.
		// The one thing kept from the use site is its LOCATION: a mismatch against the destination
		// should point at the line that said `@Accent`, not into the resources block.
		FDreamUIValue Substituted;
		if (Authored.Kind == EDreamUIValueKind::ResourceRef)
		{
			const FDreamUIValue* Entry = ResolveResourceRef(Authored, InProperty, InContext);
			if (Entry == nullptr)
			{
				return false;
			}
			Substituted = *Entry;
			Substituted.Location = Authored.Location;
		}
		const FDreamUIValue& Value = Authored.Kind == EDreamUIValueKind::ResourceRef ? Substituted : Authored;

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
					// A flags enum gets the extra sentence, because for one of those the reader's
					// next thought is "then how do I write A and B" and the answer is not obvious:
					// the grammar has no '|' (a single pipe is DUI1001) and is not getting one --
					// adding an operator is a language decision nobody has taken. The NUMBER is the
					// spelling, it is validated against this same enum, and the write-back prints it
					// back, so the round trip is whole even though the spelling is not pretty.
					const bool bIsFlags = Enum->HasAnyEnumFlags(EEnumFlags::Flags);
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownEnumValue, Value.Location,
						FString::Printf(TEXT("'%s' is not a value of %s%s%s"), *Value.Raw, *Enum->GetName(),
							*FormatSuggestion(SuggestNearestEnumValue(Value.Raw, Enum)),
							bIsFlags
								? TEXT(". This is a flags enum and the grammar has no '|', so a combination is written as the number its flags add up to")
								: TEXT("")));
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

			// A NUMBER on an enum, which used to fall straight past this whole block into
			// ImportText_Direct -- and that writes any integer at all, so `Visibility = 99` compiled
			// green and produced a widget in a state the enum does not have. Checked here rather than
			// left to the importer for the same reason the identifier spelling is: this is the one
			// stage that knows both the literal and the UEnum it is landing on.
			//
			// OrBitfield, not IsValidEnumValue: a flags enum has no identifier for `A|B` -- the
			// grammar has no '|' -- so a number is the ONLY spelling a combination has, and refusing
			// it would make those properties unwritable rather than merely unvalidated. For an enum
			// that is not a bitfield the two are the same call.
			//
			// Spelled out rather than handed to LexTryParseString, which answers true for "2.5" (it
			// only reports a failure when the parse produced zero from no zero digit) -- and a real
			// where an enum belongs is not a state the author meant, so truncating it would pick one
			// for them.
			const FString Trimmed = Value.Raw.TrimStartAndEnd();
			bool bIsInteger = !Trimmed.IsEmpty();
			for (int32 Index = 0; bIsInteger && Index < Trimmed.Len(); ++Index)
			{
				const TCHAR Char = Trimmed[Index];
				if (Index == 0 && (Char == TEXT('-') || Char == TEXT('+')))
				{
					bIsInteger = Trimmed.Len() > 1;
					continue;
				}
				bIsInteger = FChar::IsDigit(Char);
			}
			if (!bIsInteger)
			{
				RaiseTypeMismatch(InProperty, Leaf, InContext);
				return false;
			}
			const int64 Numeric = FCString::Atoi64(*Trimmed);
			if (!Enum->IsValidEnumValueOrBitfield(Numeric))
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownEnumValue, Value.Location,
					FString::Printf(TEXT("%s declares no value %lld; write one of its names instead"),
						*Enum->GetName(), Numeric));
				return false;
			}
			if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Leaf))
			{
				AsEnum->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Numeric);
			}
			else
			{
				CastFieldChecked<FByteProperty>(Leaf)->SetIntPropertyValue(ValuePtr, Numeric);
			}
			return true;
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
				// The one authored value that reaches an FName without having been an identifier
				// first, so the lexer's length rule never saw it. See IsNameLengthLegal.
				if (!IsNameLengthLegal(Value.Raw))
				{
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Value.Location,
						FString::Printf(TEXT("'%s' is %d characters long, and '%s' is a name, which holds at most %d"),
							*EllipsizeName(Value.Raw), Value.Raw.Len(), *InProperty.Name, NAME_SIZE - 1));
					return false;
				}
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
			if (!bIsNone && RefuseOverlongPath(Value.Raw, Value.Location, InContext.Diagnostics))
			{
				return false;
			}
			AsSoft->SetPropertyValue(ValuePtr,
				FSoftObjectPtr(bIsNone ? FSoftObjectPath() : FSoftObjectPath(Value.Raw)));
			return true;
		}

		// FObjectPropertyBase, not FObjectProperty. TObjectPtr, TWeakObjectPtr and TLazyObjectPtr are
		// SIBLINGS under that base, not a chain, so the narrower cast silently excluded every weak
		// reference in the library -- UUISelectable::TransitionTarget, UUIToggle::ToggleTransitionTarget
		// and UUISlider::Fill/Handle are all TWeakObjectPtr. They did not fail outright, which is why
		// it went unnoticed: they fell past here to ImportText_Direct, which does resolve an asset
		// path, but reports a miss as ValueTypeMismatch instead of AssetNotFound and -- worse --
		// searches every loaded package by bare name (ParseObjectPropertyValue defaults
		// bAllowAnyPackage to true), so a typo could bind to whatever object happened to share it.
		// The write-back has read this family through FObjectPropertyBase all along; this side was
		// the odd one out. FSoftObjectProperty is a sibling too and is claimed by the branch above,
		// which is why that one has to stay ahead of this one.
		if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Leaf))
		{
			if (Value.Raw.IsEmpty() || Value.Raw == TEXT("None"))
			{
				AsObject->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			// A widget- or visual-typed property is the one destination where a bare name can mean
			// something this same file declares, so it is read as a node id. No new syntax is needed
			// to tell the two apart: an asset path always begins with '/' and a node id never can,
			// being required to be a C++ identifier (InvalidNodeId).
			//
			// Recorded rather than resolved, and that is the whole difficulty: the node named may be
			// declared BELOW the line naming it -- a toggle pointing at its own check mark is the
			// motivating case -- and this walk is what is still building the file. See
			// FPendingNodeReference.
			if (IsNodeReferenceProperty(AsObject) && !Value.Raw.StartsWith(TEXT("/")))
			{
				FPendingNodeReference& Pending = InContext.NodeReferences.AddDefaulted_GetRef();
				Pending.Property = AsObject;
				Pending.LeafValuePtr = ValuePtr;
				Pending.NodeId = Value.Raw;
				Pending.PropertyName = InProperty.Name;
				Pending.Location = Value.Location;
				return true;
			}
			// Loaded rather than soft-referenced: a class template holds the same hard reference an
			// author dragging the asset into the details panel would, and the cook has to see it.
			if (RefuseOverlongPath(Value.Raw, Value.Location, InContext.Diagnostics))
			{
				return false;
			}
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
			// The check above cannot see a TSubclassOf's bound at all, and that is not a subtlety of
			// this code -- it is how FClassProperty is shaped. PropertyClass on one is UClass, for
			// EVERY TSubclassOf<T> there is, so `Loaded->IsA(PropertyClass)` asks "is this a class",
			// which any class that loaded already is. The T lives in MetaClass and nowhere else, so
			// without this `WidgetClass = /Game/FX/M_Glow_C` compiled green and put a material's
			// class into a property that will be instanced as a widget.
			//
			// Only when MetaClass is set: a bare `UClass*` UPROPERTY leaves it null and means exactly
			// what it says, any class, and inventing a bound for it would refuse what the details
			// panel accepts.
			if (const FClassProperty* AsClass = CastField<FClassProperty>(Leaf))
			{
				const UClass* Resolved = Cast<UClass>(Loaded);
				if (AsClass->MetaClass != nullptr && (Resolved == nullptr || !Resolved->IsChildOf(AsClass->MetaClass)))
				{
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Value.Location,
						FString::Printf(TEXT("'%s' is not a %s, and '%s' holds a subclass of %s"),
							*Value.Raw, *AsClass->MetaClass->GetName(), *InProperty.Name,
							*AsClass->MetaClass->GetName()));
					return false;
				}
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
		// Inside an `each` body, a binding whose source is `<LoopVar>.<Member>` is not a class
		// binding at all -- it is a per-CELL write, recorded on the each and applied at SetCell
		// time. Detected before the empty-name guard below, because these arrive as un-lowered
		// expressions on purpose: the thunk pass skips loop bodies.
		FString EachItemMember;
		if (InContext.ActiveEach != nullptr
			&& InProperty.BindingExpression.IsSet()
			&& InProperty.BindingExpression.GetValue().Kind == FDreamUIExpression::EKind::VariableRef)
		{
			const FString& Symbol = InProperty.BindingExpression.GetValue().Symbol;
			const FString Prefix = InContext.ActiveLoopVariable + TEXT(".");
			if (Symbol.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				EachItemMember = Symbol.Mid(Prefix.Len());
				if (EachItemMember.IsEmpty() || EachItemMember.Contains(TEXT(".")))
				{
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingExpressionUnsupported, InProperty.Location,
						FString::Printf(TEXT("'%s' reads more than one member deep; an item binding is '%s.Member'"),
							*Symbol, *InContext.ActiveLoopVariable));
					return false;
				}
			}
		}

		if (InProperty.BindingFunction.IsEmpty() && EachItemMember.IsEmpty())
		{
			if (InContext.ActiveEach != nullptr)
			{
				// Inside a loop body the un-lowered expression is not a stage that has not run yet --
				// it is a stage that never will. DreamUIExpressionThunks::Generate deliberately skips
				// loop bodies (a generated function would ask the CLASS for a name only the iteration
				// has), so the ONLY source shape an `each` supports is the single hop `Item.Member`
				// handled above. Everything else used to fall through this return and be dropped: the
				// file compiled green and the property was simply never driven, which is the exact
				// silent failure this pipeline exists to remove.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported, InProperty.Location,
					FString::Printf(TEXT("'%s' cannot be driven from inside a '%s': the body supports '%s.Member' only, so an expression or a '<->' has nowhere to be compiled to. Move the logic into the item's own class, or bind it outside the loop."),
						*InProperty.Name, InContext.ActiveEach->bInPanel ? TEXT("for") : TEXT("each"), *InContext.ActiveLoopVariable));
				return false;
			}
			// An expression binding the compiler has not lowered yet: the real compile rewrites
			// BindingFunction to the generated thunk's name before Build runs, so reaching here
			// means this is a consumer that builds a raw AST -- the write-back's reference tree,
			// a test. Recording a nameless binding would resolve to nothing at runtime and trip
			// the compiler's not-found check with an empty name; skipping records nothing, which
			// is the truth about an un-lowered expression.
			return false;
		}
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
		// The one place a missing setter is not the end of it: a property of a user widget -- a component's `props`
		// variable above all, which is a Blueprint variable and so never has a SetX. The runtime writes it straight
		// into the instance and broadcasts its FieldNotify field; the component's own bindings on it are what show the
		// change, which is everything a setter would have done. Two places may do that: a host's class binding
		// (UDreamUserWidget::EvaluateBinding) and a `for` body's item write (UDreamUIForAdapter, which also re-runs the
		// copy's bindings). Not a `<->`, whose silent push needs a setter; not an `each`, whose list views call setters
		// only. A native widget, visual or behaviour without a setter still cannot be bound: a raw write into one
		// repaints nothing.
		const bool bWritesDirectly = Setter == nullptr && InProperty.TwoWayProperty.IsEmpty()
			&& (EachItemMember.IsEmpty() ? InContext.ActiveEach == nullptr : InContext.ActiveEach->bInPanel)
			&& InDestination.BindingTarget == EDreamWidgetBindingTarget::Widget
			&& InDestination.Owner->IsA(UDreamUserWidget::StaticClass());
		if (Setter == nullptr && !bWritesDirectly)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingTargetHasNoSetter, InProperty.Location,
				FString::Printf(TEXT("'%s' on %s has no %s to drive it, so it cannot be bound"), *InProperty.Name,
					*InDestination.Owner->GetClass()->GetName(),
					*MakeDreamWidgetSetterName(InDestination.LeafProperty).ToString()));
			return false;
		}

		if (!EachItemMember.IsEmpty())
		{
			// The per-cell record, with the setter the ordinary resolution above already vetted.
			FDreamWidgetEntryBinding& Entry = InContext.ActiveEach->EntryBindings.AddDefaulted_GetRef();
			Entry.TargetWidgetDisplayName = FName(*InWidget->GetDisplayName());
			Entry.Target = InDestination.BindingTarget;
			Entry.BehaviourIndex = InDestination.BehaviourIndex;
			Entry.PropertyName = InDestination.LeafProperty->GetFName();
			// None for the direct write above, which is how the `for` adapter tells the two apart.
			Entry.SetterName = Setter != nullptr ? Setter->GetFName() : NAME_None;
			Entry.ItemMember = FName(*EachItemMember);
			return true;
		}

		FDreamWidgetPropertyBinding Binding;
#if WITH_EDITORONLY_DATA
		// The one place this position exists. DUI5004 is raised by the Blueprint compile, which runs
		// after this AST is gone and holds only the binding list -- so a line number that is not
		// copied here is a line number that stage can never have.
		Binding.SourceLine = InProperty.Location.Line;
		Binding.SourceColumn = InProperty.Location.Column;
#endif
		// Never sanitized a second time here. UDreamWidgetTree::MakeWidgetVariableName is the one
		// implementation the runtime resolves bindings with, and a private copy that differs by one
		// character is precisely how a binding reports success and comes back null.
		Binding.WidgetName = UDreamWidgetTree::MakeWidgetVariableName(InWidget);
		Binding.Target = InDestination.BindingTarget;
		Binding.BehaviourIndex = InDestination.BehaviourIndex;
		Binding.PropertyName = InDestination.LeafProperty->GetFName();
		// None for the direct write above.
		Binding.SetterName = Setter != nullptr ? Setter->GetFName() : NAME_None;
		if (!InProperty.TwoWayProperty.IsEmpty() && Setter != nullptr)
		{
			// The forward half of a `<->`: remember the variable for the runtime's subscription,
			// and push through the silent setter when the control offers one -- see NotifyField.
			Binding.NotifyField = FName(*InProperty.TwoWayProperty);
			const FName SilentSetterName(*(Setter->GetFName().ToString() + TEXT("WithoutNotify")));
			if (InDestination.Owner->GetClass()->FindFunctionByName(SilentSetterName) != nullptr)
			{
				Binding.SetterName = SilentSetterName;
			}
		}
		// The parser already hands over a bare identifier -- `()` is grammar, not part of the name --
		// so the trim is only for the other caller this struct has: an editor or a test building an
		// FDreamUIProperty by hand, which naturally writes what the author would have typed.
		FString FunctionName = InProperty.BindingFunction.TrimStartAndEnd();
		FunctionName.RemoveFromEnd(TEXT("()"));
		FunctionName.TrimStartAndEndInline();
		if (!IsNameLengthLegal(FunctionName))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingFunctionNotFound, InProperty.Location,
				FString::Printf(TEXT("'%s' is %d characters long, and a function name holds at most %d"),
					*EllipsizeName(FunctionName), FunctionName.Len(), NAME_SIZE - 1));
			return false;
		}
		Binding.FunctionName = FName(*FunctionName);

		// What the source reads, as the compiler's thunk pass recorded it when it lowered the line: the paths the run time
		// watches instead of looking FunctionName or NotifyField up as a field of the widget. Copied as they are -- the
		// flags included, since "recorded and empty" (a constant) and "never recorded" (an older compile, a bare `F()`
		// nobody lowered) are different instructions to the run time, and only the pass that read the expression knows
		// which one this is.
		Binding.bDependenciesRecorded = InProperty.bBindingDependenciesRecorded;
		Binding.bDependenciesComplete = InProperty.bBindingDependenciesComplete;
		for (const TArray<FString>& Path : InProperty.BindingDependencies)
		{
			FDreamWidgetBindingPath Dependency;
			for (const FString& Segment : Path)
			{
				// Gated like every authored name that becomes an FName (IsNameLengthLegal). A parsed file cannot reach
				// this -- the lexer cuts every word short of the limit -- but a hand-built line can.
				if (Segment.IsEmpty() || !IsNameLengthLegal(Segment))
				{
					Dependency.Segments.Reset();
					break;
				}
				Dependency.Segments.Add(FName(*Segment));
			}
			if (Dependency.Segments.Num() == 0)
			{
				// A path nothing can watch: the binding polls instead, which is never wrong, only slower.
				Binding.bDependenciesComplete = false;
				continue;
			}
			Binding.Dependencies.Add(MoveTemp(Dependency));
		}

		InContext.Bindings->Add(Binding);
		return true;
	}

	/**
	 * Whether a property is something an event line could mean -- any delegate, or an FDreamUIEventDelegate -- before the
	 * stricter question of whether a route can name it (AddEventBinding). Decides only that a single word after `=` is a
	 * handler rather than a value; AddEventBinding then says, with the line, when it is not an event a route can bind.
	 */
	bool IsEventProperty(const FProperty* InProperty)
	{
		if (InProperty == nullptr)
		{
			return false;
		}
		if (InProperty->IsA<FDelegateProperty>() || InProperty->IsA<FMulticastDelegateProperty>())
		{
			return true;
		}
		const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty);
		return AsStruct != nullptr && AsStruct->Struct == FDreamUIEventDelegate::StaticStruct();
	}

	/** `->`, `+=` or `=`, as the line wrote it, for a message to quote. */
	const TCHAR* RouteOperatorSpelling(EDreamUIRouteOperator InOperator)
	{
		switch (InOperator)
		{
		case EDreamUIRouteOperator::Append: return TEXT("+=");
		case EDreamUIRouteOperator::Assign: return TEXT("=");
		default:                            return TEXT("->");
		}
	}

	/** What a route line routes to, as written after its operator: `Handler`, `emit Picked(…)`, `Item.Use()`. For messages. */
	FString DescribeRouteTarget(const FDreamUIProperty& InProperty)
	{
		if (!InProperty.RouteTarget.IsEmpty())
		{
			return InProperty.bRouteHasArgumentList
				? FString::Printf(TEXT("%s(%s)"), *InProperty.RouteTarget, InProperty.RouteArguments.Num() > 0 ? TEXT("…") : TEXT(""))
				: InProperty.RouteTarget;
		}
		if (!InProperty.EmitEvent.IsEmpty())
		{
			return FString::Printf(TEXT("emit %s"), *InProperty.EmitEvent);
		}
		return InProperty.EventHandler;
	}

	/**
	 * Whether an event line's operator suits the event it names -- the author's word for what happens to the event's
	 * OTHER listeners, held to it (EDreamUIRouteOperator). `+=` adds one more, so it needs an event that can hold more
	 * than one; `=` takes the one slot, so it needs an event that has exactly one -- on a multicast event it would have to
	 * throw away whatever the control itself and the Blueprint graph bound. `->` takes either, as it always has.
	 * False, reported as RouteOperatorMismatch, and the route is then not recorded: binding it the other way would do
	 * something the line does not say.
	 */
	bool CheckRouteOperator(const FResolvedDestination& InDestination, const FDreamUIProperty& InProperty, bool bInSingleCast,
		FBuildContext& InContext)
	{
		const FString OwnerName = InDestination.Owner->GetClass()->GetName();
		if (InProperty.RouteOperator == EDreamUIRouteOperator::Append && bInSingleCast)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::RouteOperatorMismatch, InProperty.Location,
				FString::Printf(TEXT("'%s += %s' adds a listener, and '%s' on %s is a single-cast delegate, which holds one -- write '%s = %s' (or '->')"),
					*InProperty.Name, *DescribeRouteTarget(InProperty), *InProperty.Name, *OwnerName,
					*InProperty.Name, *DescribeRouteTarget(InProperty)));
			return false;
		}
		if (InProperty.RouteOperator == EDreamUIRouteOperator::Assign && !bInSingleCast)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::RouteOperatorMismatch, InProperty.Location,
				FString::Printf(TEXT("'%s = %s' would be the only listener, and '%s' on %s is a multicast event others listen to as well -- write '%s += %s' (or '->')"),
					*InProperty.Name, *DescribeRouteTarget(InProperty), *InProperty.Name, *OwnerName,
					*InProperty.Name, *DescribeRouteTarget(InProperty)));
			return false;
		}
		return true;
	}

	/**
	 * `OnClicked -> Item.Use()` inside a loop body: the event of THIS copy's widget, routed to a function of THIS copy's
	 * item. Nothing on the class can be bound for it -- the class has no item -- so it is recorded on the loop
	 * (FDreamWidgetEntryRoute) for its adapter to bind per copy, or per cell, as the copy or cell gets its item.
	 *
	 * Only that one shape. The loop variable and one function, nothing deeper (`Item.Owner.Use` would need a graph to
	 * reach, and the body has none -- the thunk pass never looks inside a loop), and no arguments: `-> Item.Use()` calls
	 * it with nothing, `-> Item.Use` hands it what the event sends, and anything an argument list could say beyond that
	 * would be an expression with nowhere to be compiled to. Everything else is LoopBodyBindingUnsupported.
	 */
	bool AddEntryRoute(const FResolvedDestination& InDestination, UDreamWidget* InWidget, const FDreamUIProperty& InProperty,
		FBuildContext& InContext)
	{
		TArray<FString> Segments;
		InProperty.RouteTarget.ParseIntoArray(Segments, TEXT("."));
		const bool bItemRoute = Segments.Num() == 2
			&& Segments[0].Equals(InContext.ActiveLoopVariable, ESearchCase::CaseSensitive)
			&& InProperty.RouteArguments.Num() == 0;
		if (!bItemRoute)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported, InProperty.Location,
				FString::Printf(TEXT("'%s %s %s' cannot be routed from inside a '%s': a loop body routes an event only to a function of its item, '%s.Func' (handing it what the event sends) or '%s.Func()' (calling it with nothing)"),
					*InProperty.Name, RouteOperatorSpelling(InProperty.RouteOperator), *DescribeRouteTarget(InProperty),
					InContext.ActiveEach->bInPanel ? TEXT("for") : TEXT("each"),
					*InContext.ActiveLoopVariable, *InContext.ActiveLoopVariable));
			return false;
		}
		if (!IsNameLengthLegal(Segments[1]))
		{
			// A parsed file cannot get here (the lexer cuts every word short of the limit); a hand-built line can.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::LoopBodyBindingUnsupported, InProperty.Location,
				FString::Printf(TEXT("'%s' is %d characters long, and a function name holds at most %d"),
					*EllipsizeName(Segments[1]), Segments[1].Len(), NAME_SIZE - 1));
			return false;
		}

		FDreamWidgetEntryRoute& Route = InContext.ActiveEach->EntryRoutes.AddDefaulted_GetRef();
		// By display name, as an entry binding names its widget: copies keep it, and a variable name they do not have.
		Route.TargetWidgetDisplayName = FName(*InWidget->GetDisplayName());
		Route.Target = InDestination.BindingTarget;
		Route.BehaviourIndex = InDestination.BehaviourIndex;
		Route.EventName = InDestination.LeafProperty->GetFName();
		Route.ItemFunction = FName(*Segments[1]);
		Route.bCallWithoutArguments = InProperty.bRouteHasArgumentList;
#if WITH_EDITORONLY_DATA
		// LoopItemRouteMismatch is the compiler's, which runs after this AST is gone: the line has to travel with the record.
		Route.SourceLine = InProperty.Location.Line;
		Route.SourceColumn = InProperty.Location.Column;
#endif
		return true;
	}

	/**
	 * One event line -- `Event -> Handler`, `Event += Handler`, `Event = Handler`, and the emit and member-path forms
	 * of each: checked here for the half the AST can answer, recorded for the compiler to check the other half (the
	 * handler lives on a class this build cannot see).
	 */
	bool AddEventBinding(const FResolvedDestination& InDestination, UDreamWidget* InWidget,
		const FDreamUIProperty& InProperty, FBuildContext& InContext)
	{
		if (!InDestination.bBindable)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::BindingTargetNotSupported, InProperty.Location,
				FString::Printf(TEXT("'%s' lives on %s, which no binding can name -- EDreamWidgetBindingTarget reaches the widget, its visual and its behaviours only"),
					*InProperty.Name, *InDestination.Owner->GetClass()->GetName()));
			return false;
		}
		if (InDestination.bNested)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EventNotFound, InProperty.Location,
				FString::Printf(TEXT("'%s' is a path into a struct, and an event is always a whole property"), *InProperty.Name));
			return false;
		}
		// Either kind of event this plugin has, because a `->` route reaches both.
		//
		// DYNAMIC multicast, specifically, for the first: it is the kind a UFUNCTION binds to by name
		// and the kind UHT gives BlueprintAssignable events, and the two facts are why every
		// OnSomething in the `Controls/` family is one. A plain FMulticastDelegateProperty cannot be
		// bound from a name.
		//
		// The second is an FDreamUIEventDelegate, which the older `Interaction/` behaviours declare
		// instead (UIButton::OnClick, UISlider::OnValueChanged). Routes onto those used to be refused
		// here, which left that whole family with no canonical way to be handled and kept its
		// per-instance legacy event list alive as a second, competing mechanism.
		// UDreamUserWidget::BindEventBindings attaches to either.
		//
		// And a third, which holds ONE listener: a single-cast dynamic delegate (FDelegateProperty), the shape a
		// "call this when…" callback takes rather than an event many parties hear. It is the one `=` is for, and `->`
		// reaches it too; `+=` cannot (CheckRouteOperator).
		const bool bIsAssignableDelegate = CastField<FMulticastDelegateProperty>(InDestination.LeafProperty) != nullptr
			&& InDestination.LeafProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable);
		const FStructProperty* AsDreamEvent = CastField<FStructProperty>(InDestination.LeafProperty);
		const bool bIsDreamEvent = AsDreamEvent != nullptr
			&& AsDreamEvent->Struct == FDreamUIEventDelegate::StaticStruct()
			&& AsDreamEvent->HasAnyPropertyFlags(CPF_Edit);
		const bool bIsSingleCast = CastField<FDelegateProperty>(InDestination.LeafProperty) != nullptr;
		if (!bIsAssignableDelegate && !bIsDreamEvent && !bIsSingleCast)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EventNotFound, InProperty.Location,
				FString::Printf(TEXT("'%s' on %s is not an event (a BlueprintAssignable dynamic multicast delegate, a dynamic delegate, or an editable DreamUIEventDelegate)"),
					*InProperty.Name, *InDestination.Owner->GetClass()->GetName()));
			return false;
		}
		if (!CheckRouteOperator(InDestination, InProperty, bIsSingleCast, InContext))
		{
			return false;
		}
		if (InContext.ActiveEach != nullptr && !InProperty.RouteTarget.IsEmpty())
		{
			// Inside a loop body a member route is never the thunk pass's -- that pass does not look in here -- so it
			// arrives with no handler, and is either a route to the item or nothing a loop body can hold.
			return AddEntryRoute(InDestination, InWidget, InProperty, InContext);
		}
		if (InProperty.EventHandler.TrimStartAndEnd().IsEmpty())
		{
			// `OnClicked -> emit Picked(Index)` that the compiler's thunk pass has not lowered yet: the handler it
			// generates is what this route will call, and until it exists there is no name to record. The same
			// answer an un-lowered `<-` expression gets in AddBinding, for the same callers -- the write-back's
			// reference tree, a test -- and for the same reason: a nameless route would bind to nothing at run time
			// and trip the compiler's not-found check with an empty name. The checks above still ran, so an emit
			// on something that is not an event is still reported.
			//
			// `OnClicked -> Settings.Apply()` outside a loop body arrives here the same way when the pass refused it
			// (it has said why, at the line) or never ran: a member route is lowered exactly as an emit is, and an
			// ordinary route, recorded below, once it has been.
			return false;
		}
		if (InContext.EventBindings == nullptr)
		{
			// A caller that asked for no events still gets the CHECKS above; only the recording is
			// skipped. The reference tree is such a caller, and its file may legally carry `->`.
			return true;
		}

		const FString HandlerName = InProperty.EventHandler.TrimStartAndEnd();
		if (!IsNameLengthLegal(HandlerName))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EventNotFound, InProperty.Location,
				FString::Printf(TEXT("'%s' is %d characters long, and a handler name holds at most %d"),
					*EllipsizeName(HandlerName), HandlerName.Len(), NAME_SIZE - 1));
			return false;
		}

		FDreamWidgetEventBinding Binding;
		Binding.WidgetName = UDreamWidgetTree::MakeWidgetVariableName(InWidget);
		Binding.Target = InDestination.BindingTarget;
		Binding.BehaviourIndex = InDestination.BehaviourIndex;
		Binding.EventName = InDestination.LeafProperty->GetFName();
		Binding.FunctionName = FName(*HandlerName);
#if WITH_EDITORONLY_DATA
		// Same reason as the `<-` half: DUI6004 and DUI6005 are raised by the Blueprint compile,
		// which is the only stage that can see the handler and the last one that could see this line.
		Binding.SourceLine = InProperty.Location.Line;
		Binding.SourceColumn = InProperty.Location.Column;
#endif
		InContext.EventBindings->Add(Binding);
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
		if (InProperty.IsEventBinding())
		{
			AddEventBinding(Destination, InWidget, InProperty, InContext);
			return;
		}
		if (InProperty.IsBinding())
		{
			AddBinding(Destination, InWidget, InProperty, InContext);
			return;
		}
		if (InProperty.Value.Kind == EDreamUIValueKind::Identifier && IsEventProperty(Destination.LeafProperty))
		{
			// `OnPicked = HandlePick` -- the parser cannot tell a handler from an enum value (`HorizontalAlignment =
			// Fill` is the same three tokens), so a single word after `=` arrives as a value, and only here, where the
			// destination turns out to be an event, is it read as the route it is: the `=` route, the one listener of a
			// single-cast delegate (EDreamUIRouteOperator::Assign). Nothing that was a value before changes: an event has
			// no text form, and every such line used to be refused (PropertyNotWritable, or a mismatch on the struct).
			FDreamUIProperty AsRoute = InProperty;
			AsRoute.EventHandler = InProperty.Value.Raw;
			AsRoute.RouteOperator = EDreamUIRouteOperator::Assign;
			AsRoute.Value = FDreamUIValue();
			AddEventBinding(Destination, InWidget, AsRoute, InContext);
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

void FDreamUITextBuilder::GetVisualTags(TArray<TPair<FString, UClass*>>& OutTags)
{
	OutTags.Reset();
	for (const TPair<FName, UClass*>& Entry : DreamUITextBuilderLocal::GetVisualTagTable())
	{
		OutTags.Emplace(Entry.Key.ToString(), Entry.Value);
	}
}

UClass* FDreamUITextBuilder::FindVisualClassForTag(const FString& InTag, bool& bOutIsKnownTag)
{
	return FDreamUIWidgetRegistry::ResolveVisual(FName(*InTag), bOutIsKnownTag);
}

bool FDreamUITextBuilder::IsWritableFromText(const FProperty* InProperty, FString& OutReason)
{
	// A forwarder, so the rule keeps exactly one body while the local callers above keep the
	// unqualified name they already use.
	return DreamUITextBuilderLocal::IsWritableFromText(InProperty, OutReason);
}

bool FDreamUITextBuilder::IsNodeReferenceProperty(const FObjectPropertyBase* InProperty)
{
	// Same forwarder arrangement, same reason: one body, two callers that must never disagree.
	return InProperty != nullptr && DreamUITextBuilderLocal::IsNodeReferenceProperty(InProperty);
}

UClass* FDreamUITextBuilder::ResolveComponentClass(const FString& InClassName)
{
	const FString Name = InClassName.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		return nullptr;
	}

	// What `+` can mean, applied PER CANDIDATE rather than once at the end. The prefix families
	// stopped being distinct the day the control library arrived: `Slider` names both UDreamSlider
	// (a control, which `+` cannot carry) and UUISlider (the behaviour the author meant), and taking
	// the first class that merely exists resolved `+ Slider` to the wrong one and failed every file
	// that says it. Resolution is the first name that could BE a component.
	//
	// Layout containers are accepted alongside behaviours, and that is a deliberate widening of what
	// `+` means. They are not UDreamUIBehaviour -- they are UDreamWidgetSubObjectBehaviour, a separate
	// hierarchy -- so without this the language cannot produce a panel at all, which in turn means no
	// child ever gets a UDreamPanelSlot and every `@slot` line in every file resolves to
	// NoPanelSlotForProperty. A diagnostic that is always right is one nobody can act on.
	auto AsComponentClass = [](UClass* InFound) -> UClass*
	{
		if (InFound == nullptr || InFound->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return nullptr;
		}
		return InFound->IsChildOf(UDreamUIBehaviour::StaticClass())
			|| InFound->IsChildOf(UDreamLayoutContainer::StaticClass())
			|| InFound->IsChildOf(UDreamLayoutSelf::StaticClass())
			? InFound : nullptr;
	};

	if (Name.StartsWith(TEXT("/")))
	{
		const FString Redirected = DreamUI::ApplyTypeRedirects(ECoreRedirectFlags::Type_Class, Name);
		UClass* Found = UClass::TryFindTypeSlowSafe<UClass>(Redirected);
		if (Found == nullptr)
		{
			Found = LoadObject<UClass>(nullptr, *Redirected, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		return AsComponentClass(Found);
	}

	// Prefixes rather than an alias table. `Canvas` finds UDreamCanvas, `Button` finds UUIButton
	// and `VerticalBox` finds UDreamLayoutContainerVerticalBox without any of them being written
	// down anywhere, so adding a behaviour to the library adds it to the language -- a table
	// would be a second place to remember, and the one that gets forgotten. UIML's four
	// hand-written aliases all fall out of this.
	static const TCHAR* Prefixes[] =
	{
		TEXT(""), TEXT("Dream"), TEXT("UI"), TEXT("DreamLayoutContainer"), TEXT("DreamLayoutSelf")
	};
	//
	// In every runtime module of the plugin, prefix by prefix: all of them are searched for the plain
	// name before any is searched for `Dream` + name, so which module a class lives in never decides
	// which of two spellings wins.
	const TArray<FName> Packages = DreamUI::GetRuntimeScriptPackages();
	for (const TCHAR* Prefix : Prefixes)
	{
		for (const FName Package : Packages)
		{
			if (UClass* Found = AsComponentClass(UClass::TryFindTypeSlowSafe<UClass>(
				FString::Printf(TEXT("%s.%s%s"), *Package.ToString(), Prefix, *Name))))
			{
				return Found;
			}
		}
	}
	// A behaviour from the game module or another plugin. Last, because it is the slow lookup
	// and the ambiguous one, and native-first so a Blueprint of the same name never wins.
	return AsComponentClass(FindFirstObjectSafe<UClass>(*Name, EFindFirstObjectOptions::NativeFirst));
}

TFunction<UClass*(const FString& InResolvedSourcePath)>& FDreamUITextBuilder::SourceClassResolver()
{
	static TFunction<UClass*(const FString&)> Resolver;
	return Resolver;
}

UClass* FDreamUITextBuilder::FindContainerClassForType(const FString& InTypeName)
{
	// A bare name only: a dotted type is a registry tag or a namespaced alias, a path is a class, `@` is a resource.
	// The same prefix search `+` uses, so `VerticalBox` as a type and `+ VerticalBox` as a component are one class.
	if (InTypeName.IsEmpty() || InTypeName.StartsWith(TEXT("/")) || InTypeName.StartsWith(TEXT("@")) || InTypeName.Contains(TEXT(".")))
	{
		return nullptr;
	}
	UClass* Found = ResolveComponentClass(InTypeName);
	return Found != nullptr && Found->IsChildOf(UDreamLayoutContainer::StaticClass()) ? Found : nullptr;
}

namespace DreamUITextBuilderLocal
{
	UDreamWidget* BuildNode(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext);

	/**
	 * The chain of styles a node wears, BASE first: assignment order is override order, the same rule that lets the
	 * node's own lines override the style's.
	 *
	 * Walked from the named style upward and handed back reversed. A base that comes back around is a cycle, refused at
	 * the node so every node wearing the broken style says so, and then nothing of it applies. A base declared nowhere
	 * is reported against the style that names it and the walk stops there, so what was found still applies.
	 *
	 * Resolved once per node, because three things read it -- the node's properties, its components and its `@slot`
	 * lines -- and resolving it in each would report one broken chain three times.
	 */
	void ResolveStyleChain(const FDreamUINode& InNode, FBuildContext& InContext, TArray<const FDreamUIStyle*>& OutBaseFirst)
	{
		OutBaseFirst.Reset();
		if (InNode.StyleName.IsEmpty())
		{
			return;
		}
		const FDreamUIStyle* Style = InContext.Ast->FindStyle(InNode.StyleName);
		if (Style == nullptr)
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
			return;
		}
		TArray<const FDreamUIStyle*> Chain;
		TSet<const FDreamUIStyle*> Visited;
		for (const FDreamUIStyle* Link = Style; Link != nullptr;)
		{
			if (Visited.Contains(Link))
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::StyleCycle, InNode.Location,
					FString::Printf(TEXT("style '%s' inherits itself through its bases, so nothing was applied"), *Style->Name));
				return;
			}
			Visited.Add(Link);
			Chain.Add(Link);
			if (Link->BaseName.IsEmpty())
			{
				break;
			}
			// Already the library's own when the style came in under a namespace: the parser renames a library style's
			// base along with the style (`nier.Danger : nier.Label`), so the plain lookup is the right one here too.
			const FDreamUIStyle* Base = InContext.Ast->FindStyle(Link->BaseName);
			if (Base == nullptr)
			{
				// Link->Location is a line in the file that DECLARES the style, which an
				// import chain makes a different file from the one being compiled.
				AddErrorIn(*InContext.Diagnostics, Link->SourceName, EDreamUIDiagnosticCode::UnknownStyle, Link->Location,
					FString::Printf(TEXT("style '%s' inherits '%s', which is declared nowhere it can see"),
						*Link->Name, *Link->BaseName));
				break;
			}
			Link = Base;
		}
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
		{
			OutBaseFirst.Add(Chain[Index]);
		}
	}

	/** An error about a line a style wrote, stamped with the file that declares the style (see AddErrorIn); InStyle null is a line of the node's own. */
	void AddErrorAtLine(FBuildContext& InContext, const FDreamUIStyle* InStyle, EDreamUIDiagnosticCode InCode,
		const FDreamUISourceLocation& InLocation, FString InMessage)
	{
		if (InStyle != nullptr)
		{
			AddErrorIn(*InContext.Diagnostics, InStyle->SourceName, InCode, InLocation, MoveTemp(InMessage));
		}
		else
		{
			InContext.Diagnostics->AddError(InCode, InLocation, MoveTemp(InMessage));
		}
	}

	/**
	 * One component a node will carry, and every `+` line that writes onto it, in the order they apply.
	 *
	 * Several lines can be one component. A style's `+ VerticalBox { Spacing = 15 }` and the node's own `+ VerticalBox
	 * { Padding = … }` are the SAME container, the style's values first and the node's after -- exactly how a style's
	 * bare properties and the node's relate. Without that a style could not set a default the node then adjusts, and a
	 * node wearing the style would carry two containers, the second silently replacing the first.
	 */
	struct FPlannedComponent
	{
		UClass* Class = nullptr;
		/** The lines writing onto it: its styles' (base first), then the node's. */
		TArray<const FDreamUIComponent*> Lines;
		/** The style each line was written in, parallel to Lines; null for a line of the node's own. */
		TArray<const FDreamUIStyle*> LineStyles;
		/**
		 * A `+` line of the node itself is among Lines. A second node line of the same class is then a second
		 * component, which is what two `+` lines on one node always meant -- the merge is between a style and the node,
		 * never a reinterpretation of a node's own list.
		 */
		bool bHasNodeLine = false;
		/** The node's TYPE named this container (`VerticalBox Column { … }`); no `+` line created it. */
		bool bFromNodeType = false;
	};

	/**
	 * `+ Xxx { … }` -- create the sub-objects and write their properties onto THEM, not onto the widget.
	 *
	 * What a node carries, in creation order: the container its type names, the components its styles add (base
	 * first), and its own `+` lines -- a style's line or the node's joining an earlier entry of the same class (see
	 * FPlannedComponent). The container a type names comes first so that it is in place before anything that might ask
	 * for one, exactly as an author writing `Widget X { + VerticalBox {} … }` would have put it.
	 */
	void BuildComponents(const FDreamUINode& InNode, TConstArrayView<const FDreamUIStyle*> InStyles, UClass* InTypeContainerClass,
		UDreamWidget* InWidget, FBuildContext& InContext)
	{
		TArray<FPlannedComponent> Plan;
		if (InTypeContainerClass != nullptr)
		{
			FPlannedComponent& FromType = Plan.AddDefaulted_GetRef();
			FromType.Class = InTypeContainerClass;
			FromType.bFromNodeType = true;
		}
		for (const FDreamUIStyle* Style : InStyles)
		{
			for (const FDreamUIComponent& Component : Style->Components)
			{
				UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
				if (ComponentClass == nullptr)
				{
					AddErrorIn(*InContext.Diagnostics, Style->SourceName, EDreamUIDiagnosticCode::UnknownBehaviourClass, Component.Location,
						FString::Printf(TEXT("'%s' in style '%s' is not a behaviour or layout a widget can carry"),
							*Component.ClassName, *Style->Name));
					continue;
				}
				FPlannedComponent* Same = Plan.FindByPredicate([ComponentClass](const FPlannedComponent& Entry)
				{
					return Entry.Class == ComponentClass;
				});
				if (Same == nullptr)
				{
					Same = &Plan.AddDefaulted_GetRef();
					Same->Class = ComponentClass;
				}
				Same->Lines.Add(&Component);
				Same->LineStyles.Add(Style);
			}
		}
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Component.ClassName);
			if (ComponentClass == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownBehaviourClass, Component.Location,
					FString::Printf(TEXT("'%s' is not a behaviour or layout this widget can carry"), *Component.ClassName));
				continue;
			}
			if (InTypeContainerClass != nullptr && ComponentClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
			{
				// Refused even when it is the same class: the type already said it, and a second spelling of one
				// container is two places for its values to disagree.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::SecondLayoutContainer, Component.Location,
					FString::Printf(TEXT("'%s' is a %s by its type, so it is laid out already -- write the container's properties on the node itself, or make it a 'Widget' to lay it out with '+ %s'"),
						*InNode.Id, *InNode.TypeName, *Component.ClassName));
				continue;
			}
			FPlannedComponent* Same = Plan.FindByPredicate([ComponentClass](const FPlannedComponent& Entry)
			{
				return Entry.Class == ComponentClass && !Entry.bHasNodeLine && !Entry.bFromNodeType;
			});
			if (Same == nullptr)
			{
				Same = &Plan.AddDefaulted_GetRef();
				Same->Class = ComponentClass;
			}
			Same->Lines.Add(&Component);
			Same->LineStyles.Add(nullptr);
			Same->bHasNodeLine = true;
		}

		// One container per widget. A widget lays its children out one way, and CreateNewLayoutContainer REPLACES the
		// container a widget has -- so a second one used to win over the first with nothing anywhere saying so, and the
		// first one's values went with it. Two reach here only as two different classes (a style's VerticalBox and the
		// node's HorizontalBox) or as two `+` lines of the node's own.
		UClass* FirstContainerClass = nullptr;
		for (int32 Index = 0; Index < Plan.Num();)
		{
			const FPlannedComponent& Entry = Plan[Index];
			if (!Entry.Class->IsChildOf(UDreamLayoutContainer::StaticClass()))
			{
				++Index;
				continue;
			}
			if (FirstContainerClass == nullptr)
			{
				FirstContainerClass = Entry.Class;
				++Index;
				continue;
			}
			if (Entry.Lines.Num() > 0)
			{
				const FDreamUIStyle* Style = Entry.LineStyles[0];
				AddErrorAtLine(InContext, Style, EDreamUIDiagnosticCode::SecondLayoutContainer, Entry.Lines[0]->Location,
					FString::Printf(TEXT("'%s' would be a second layout container on '%s'%s, which %s already lays out -- a widget arranges its children one way; nest another widget for the other"),
						*Entry.Lines[0]->ClassName, *InNode.Id,
						Style != nullptr ? *FString::Printf(TEXT(" (from style '%s')"), *Style->Name) : TEXT(""),
						*FirstContainerClass->GetName()));
			}
			Plan.RemoveAt(Index, 1, EAllowShrinking::No);
		}

		// Everything is created before anything is written, because creating one can change the
		// indices of the others: a panel layout container pulls in its required behaviours
		// (SyncRequiredBehavioursForLayoutContainer), and a binding recorded against a stale index
		// would drive whichever behaviour happened to land there instead.
		TArray<UObject*> Created;
		Created.Reserve(Plan.Num());
		for (const FPlannedComponent& Entry : Plan)
		{
			if (Entry.Class->IsChildOf(UDreamLayoutContainer::StaticClass()))
			{
				UDreamLayoutContainer* Previous = InWidget->GetLayoutContainer();
				UDreamLayoutContainer* Container = InWidget->CreateNewLayoutContainer(Entry.Class);
				InWidget->SyncRequiredBehavioursForLayoutContainer(Previous, Container);
				Created.Add(Container);
			}
			else if (Entry.Class->IsChildOf(UDreamLayoutSelf::StaticClass()))
			{
				Created.Add(InWidget->CreateNewLayoutSelf(Entry.Class));
			}
			else
			{
				Created.Add(InWidget->AddComponent(Entry.Class));
			}
		}

		for (int32 ComponentIndex = 0; ComponentIndex < Plan.Num(); ComponentIndex++)
		{
			UObject* Object = Created[ComponentIndex];
			if (!IsValid(Object))
			{
				continue;
			}
			const FPlannedComponent& Entry = Plan[ComponentIndex];
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
			// index after it -- silently re-keying strings that nobody edited. This one counts the
			// planned components, which for a node with no style components and no container type are
			// exactly its `+` lines, as the author would count them.
			Candidate.LocalizationDiscriminator = FString::Printf(TEXT("%s_%d"),
				*Object->GetClass()->GetName(), ComponentIndex);
			for (int32 LineIndex = 0; LineIndex < Entry.Lines.Num(); ++LineIndex)
			{
				const FDreamUIComponent& Line = *Entry.Lines[LineIndex];
				const FString Description = FString::Printf(TEXT("behaviour '%s'"), *Line.ClassName);
				for (const FDreamUIProperty& Property : Line.Properties)
				{
					ApplyProperty(InNode, Property, InWidget, { Candidate }, Description, InContext);
				}
			}
		}
	}

	/**
	 * `@slot Padding = (8, 8, 8, 8)` -- the child's own UDreamPanelSlot, which the PARENT's layout gives it.
	 * The lines of its styles first (base first), then its own, which win.
	 */
	void BuildSlotProperties(const FDreamUINode& InNode, TConstArrayView<const FDreamUIStyle*> InStyles, UDreamWidget* InWidget,
		FBuildContext& InContext)
	{
		bool bHasLines = InNode.SlotProperties.Num() > 0;
		for (const FDreamUIStyle* Style : InStyles)
		{
			bHasLines |= Style->SlotProperties.Num() > 0;
		}
		if (!bHasLines)
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
			//
			// With one more case: a child of a component instance. Everything a host nests on an instance is content
			// for one of its slots -- the instance's own contents live in its class, never in the host's tree -- and
			// Initialize moves it there, into whatever panel that slot carries; registration keeps a slot the child
			// already has (EnsurePanelSlotForChild). Refusing here refused `@slot` on every row a host hands a list
			// component, which is where a row's fill and padding matter most.
			UDreamWidget* Parent = InWidget->GetParent();
			if (IsValid(Parent) && (Parent->HasPanelSlots() || Parent->IsA<UDreamUserWidget>()))
			{
				Slot = InWidget->CreateNewPanelSlot<UDreamPanelSlot>();
			}
		}
		if (!IsValid(Slot))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NoPanelSlotForProperty, InNode.Location,
				FString::Printf(TEXT("'%s' has @slot properties%s, but its parent lays out no panel slots"), *InNode.Id,
					InNode.SlotProperties.Num() == 0 ? TEXT(" from its style") : TEXT("")));
			return;
		}
		FDestinationCandidate Candidate;
		Candidate.Object = Slot;
		// A panel slot is not a UDreamWidget, a UDreamVisual or a behaviour, so nothing in
		// EDreamWidgetBindingTarget names it. Assignments land fine; `<-` is refused, loudly.
		Candidate.bBindable = false;
		// Unnumbered, unlike a behaviour's: a widget has exactly one panel slot, ever.
		Candidate.LocalizationDiscriminator = TEXT("Slot");
		for (const FDreamUIStyle* Style : InStyles)
		{
			for (const FDreamUIProperty& Property : Style->SlotProperties)
			{
				ApplyProperty(InNode, Property, InWidget, { Candidate }, TEXT("the panel slot"), InContext);
			}
		}
		for (const FDreamUIProperty& Property : InNode.SlotProperties)
		{
			ApplyProperty(InNode, Property, InWidget, { Candidate }, TEXT("the panel slot"), InContext);
		}
	}

	/** Style first, node second. The whole point of a style is that the node gets the last word. */
	void BuildProperties(const FDreamUINode& InNode, TConstArrayView<const FDreamUIStyle*> InStyles, bool bInTypedAsContainer,
		UDreamWidget* InWidget, FBuildContext& InContext)
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
		// A node whose type is its container (`VerticalBox Column { Spacing = 29 }`) IS that container to its author,
		// so its bare lines reach it -- which they could not before, a layout container being neither the widget, its
		// visual nor a behaviour (it is a UDreamWidgetSubObjectBehaviour, a hierarchy of its own). After the widget, so
		// the widget's own property wins on a name both have, the same rule the visual follows; ahead of the
		// behaviours, because the type the author wrote is the container. Only for a node TYPED as one: a plain
		// widget's `+ VerticalBox { … }` keeps its values inside its block, as it always has. Never bindable --
		// EDreamWidgetBindingTarget cannot name a container -- and keyed like the component entry that created it,
		// which for a typed node is always the first.
		UDreamLayoutContainer* TypeContainer = bInTypedAsContainer ? InWidget->GetLayoutContainer() : nullptr;
		if (IsValid(TypeContainer))
		{
			FDestinationCandidate ContainerCandidate;
			ContainerCandidate.Object = TypeContainer;
			ContainerCandidate.bBindable = false;
			ContainerCandidate.LocalizationDiscriminator = FString::Printf(TEXT("%s_0"), *TypeContainer->GetClass()->GetName());
			Candidates.Add(ContainerCandidate);
		}
		// The behaviours, after the widget and its visual so those keep shadowing on a name clash.
		// EDreamWidgetBindingTarget::Behaviour and its resolver existed all along -- the runtime and
		// the compiler both walk it -- but node-level lines never offered behaviours as candidates,
		// so `bIsOn <- F()` on a toggle-carrying node died in DUI4001 while the machinery to serve
		// it sat finished one layer down. The discriminator matches the component write path's, so
		// a localized string keys the same whichever spelling put it there.
		{
			const TArray<UDreamUIBehaviour*>& Behaviours = InWidget->GetAllComponents();
			for (int32 BehaviourIndex = 0; BehaviourIndex < Behaviours.Num(); ++BehaviourIndex)
			{
				if (!IsValid(Behaviours[BehaviourIndex]))
				{
					continue;
				}
				FDestinationCandidate BehaviourCandidate;
				BehaviourCandidate.Object = Behaviours[BehaviourIndex];
				BehaviourCandidate.Target = EDreamWidgetBindingTarget::Behaviour;
				BehaviourCandidate.BehaviourIndex = BehaviourIndex;
				BehaviourCandidate.LocalizationDiscriminator = FString::Printf(TEXT("%s_%d"),
					*Behaviours[BehaviourIndex]->GetClass()->GetName(), BehaviourIndex);
				Candidates.Add(BehaviourCandidate);
			}
		}
		const FString Description = InWidget->GetVisual() != nullptr
			? FString::Printf(TEXT("'%s' or its %s"), *InNode.Id, *InWidget->GetVisual()->GetClass()->GetName())
			: IsValid(TypeContainer)
				? FString::Printf(TEXT("'%s' or its %s"), *InNode.Id, *InNode.TypeName)
				: FString::Printf(TEXT("'%s'"), *InNode.Id);

		for (const FDreamUIStyle* Style : InStyles)
		{
			for (const FDreamUIProperty& Property : Style->Properties)
			{
				ApplyProperty(InNode, Property, InWidget, Candidates, Description, InContext, true);
			}
		}

		for (const FDreamUIProperty& Property : InNode.Properties)
		{
			// Only here is the visual-tag hint meaningful: a bare name is the one destination that
			// depends on the node's TYPE, which is the thing the author would have to change.
			ApplyProperty(InNode, Property, InWidget, Candidates, Description, InContext, true);
		}
	}

	/** The container class a node type names, asked once per spelling per build: see FBuildContext::ContainerClassesByType. */
	UClass* FindContainerClassForTypeOnce(const FString& InTypeName, FBuildContext& InContext)
	{
		if (UClass* const* Known = InContext.ContainerClassesByType.Find(InTypeName))
		{
			return *Known;
		}
		UClass* Found = FDreamUITextBuilder::FindContainerClassForType(InTypeName);
		InContext.ContainerClassesByType.Add(InTypeName, Found);
		return Found;
	}

	/**
	 * The class a component alias names (`use … as Row`), into OutWidgetClass.
	 *
	 * The path when the alias has one -- `use /Game/UI/WBP_Row as Row`, or the imported file's `class` line -- through
	 * the same resolution a node typed `/Game/...` gets. For a file, failing that, the editor's answer for it
	 * (SourceClassResolver: the Blueprint whose Source File it is), which only the editor can give. Failures are
	 * reported at the NODE, the place the build stopped, and name where the alias was declared, which is what the
	 * author has to change -- in another file, when a library brought it.
	 */
	bool ResolveAliasClass(const FDreamUIComponentAlias& InAlias, const FDreamUINode& InNode, FBuildContext& InContext,
		UClass*& OutWidgetClass)
	{
		if (UClass* const* Known = InContext.ResolvedAliasClasses.Find(InAlias.Alias))
		{
			OutWidgetClass = *Known;
			return true;
		}
		const FString DeclaredAt = InAlias.SourceName.IsEmpty()
			? FString::Printf(TEXT("line %d"), InAlias.Location.Line)
			: FString::Printf(TEXT("%s(%d)"), *InAlias.SourceName, InAlias.Location.Line);

		UClass* Resolved = !InAlias.ClassPath.IsEmpty() ? ResolveWidgetClassFromPath(InAlias.ClassPath) : nullptr;
		// The editor's answer as well when a file's `class` line names a Blueprint that does not exist yet: the line
		// says where the class WILL be, and a Blueprint elsewhere that already reads the file is the class it is now.
		const TFunction<UClass*(const FString&)>& Resolver = FDreamUITextBuilder::SourceClassResolver();
		if (Resolved == nullptr && !InAlias.SourcePath.IsEmpty() && Resolver)
		{
			Resolved = Resolver(InAlias.SourcePath);
		}
		if (Resolved == nullptr)
		{
			FString Unresolved;
			if (!InAlias.ClassPath.IsEmpty())
			{
				Unresolved = InAlias.SourcePath.IsEmpty()
					? FString::Printf(TEXT("'%s' (declared at %s) names '%s', which loads no class"),
						*InAlias.Alias, *DeclaredAt, *InAlias.ClassPath)
					: FString::Printf(TEXT("'%s' (declared at %s) names '%s', whose class '%s' loads nothing yet -- compile that file into it first"),
						*InAlias.Alias, *DeclaredAt, *InAlias.SourcePath, *InAlias.ClassPath);
			}
			else if (!InAlias.SourcePath.IsEmpty())
			{
				Unresolved = Resolver
					? FString::Printf(TEXT("'%s' (declared at %s) names '%s', which has no 'class' line, and no widget Blueprint uses it as its Source File -- give the file a 'class' line, or compile it into a Blueprint first"),
						*InAlias.Alias, *DeclaredAt, *InAlias.SourcePath)
					: FString::Printf(TEXT("'%s' (declared at %s) names '%s', which has no 'class' line, and this build has no editor to ask which Blueprint is made from it -- give the file a 'class' line"),
						*InAlias.Alias, *DeclaredAt, *InAlias.SourcePath);
			}
			else
			{
				Unresolved = FString::Printf(TEXT("'%s' (declared at %s) names neither a class nor a file"), *InAlias.Alias, *DeclaredAt);
			}
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ComponentAliasUnresolved, InNode.Location, MoveTemp(Unresolved));
			return false;
		}
		if (!Resolved->IsChildOf(UDreamUserWidget::StaticClass()) || Resolved->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			// The same refusal a path written as the type gets, and for the same reason: a node typed by a class is an
			// instance whose contents come from that class, which only a user widget has -- and only a concrete one can
			// be placed at all.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NotAUserWidgetClass, InNode.Location,
				FString::Printf(TEXT("'%s' (declared at %s) names %s, and a node typed by a component must be a concrete DreamUI user widget"),
					*InAlias.Alias, *DeclaredAt, *Resolved->GetPathName()));
			return false;
		}
		InContext.ResolvedAliasClasses.Add(InAlias.Alias, Resolved);
		OutWidgetClass = Resolved;
		return true;
	}

	/**
	 * Which UDreamWidget subclass, which UDreamVisual, and which layout container this node asks for.
	 *
	 * Returns false only when nothing can be created; a null visual class with a true return is the
	 * ordinary `Widget` case, not a failure.
	 *
	 * The order is the language's precedence, and it is not arbitrary: a built-in tag, then a layout container's name,
	 * then a component alias -- so an alias can never change what `Text` or `VerticalBox` means, which is why one that
	 * tries is an error (AliasShadowsBuiltIn) rather than a silent loser -- then `@Name`, a registry tag, an asset path.
	 * Aliases sit ahead of the registry because a namespaced one is spelt like a tag (`nier.Row`, `Native.Button`).
	 */
	bool ResolveNodeClasses(const FDreamUINode& InNode, FBuildContext& InContext, UClass*& OutWidgetClass, UClass*& OutVisualClass,
		UClass*& OutContainerClass)
	{
		OutWidgetClass = UDreamWidget::StaticClass();
		OutVisualClass = nullptr;
		OutContainerClass = nullptr;

		if (InNode.Kind == EDreamUINodeKind::NamedSlot)
		{
			return true;
		}
		const FString& TypeName = InNode.TypeName;

		// A bare word -- no path, no resource, no scope -- is the only spelling a tag or a container has. Only one an
		// FName can hold is asked about: the tag lookup makes one, and one past NAME_SIZE stops the editor rather than
		// fail (see IsNameLengthLegal); the lexer cuts such a word, so this is for an AST built by hand.
		const bool bBareName = !TypeName.IsEmpty() && !TypeName.StartsWith(TEXT("/")) && !TypeName.StartsWith(TEXT("@"))
			&& !TypeName.Contains(TEXT("."));
		if (bBareName && IsNameLengthLegal(TypeName))
		{
			bool bIsKnownTag = false;
			OutVisualClass = FDreamUITextBuilder::FindVisualClassForTag(TypeName, bIsKnownTag);
			if (bIsKnownTag)
			{
				return true;
			}
			// `VerticalBox Categories { Spacing = 29 }` -- a plain widget carrying that container. BuildComponents puts it
			// in place and BuildProperties lets the node's lines reach it.
			OutContainerClass = FindContainerClassForTypeOnce(TypeName, InContext);
			if (OutContainerClass != nullptr)
			{
				return true;
			}
		}

		// `Row Row1 { }` or `nier.Row Row1 { }` -- a class named by `use … as`, this file's or one a library brought.
		if (!TypeName.StartsWith(TEXT("/")) && !TypeName.StartsWith(TEXT("@")) && InContext.Ast != nullptr)
		{
			if (const FDreamUIComponentAlias* Alias = InContext.Ast->FindComponentAlias(TypeName))
			{
				return ResolveAliasClass(*Alias, InNode, InContext, OutWidgetClass);
			}
		}

		// A user widget class at an asset path, `/Game/UI/WBP_Card` or `/Script/Module.Class`: written as the type, or
		// named by an Asset resource (`@Card`).
		auto ResolveWidgetClassAt = [&InNode, &InContext, &OutWidgetClass](const FString& InPath)
		{
			UClass* Loaded = ResolveWidgetClassFromPath(InPath);
			if (Loaded == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::AssetNotFound, InNode.Location,
					FString::Printf(TEXT("'%s' could not be loaded"), *InPath));
				return false;
			}
			if (!Loaded->IsChildOf(UDreamUserWidget::StaticClass()))
			{
				// A widget blueprint is what nesting means here: the node becomes an instance whose
				// contents come from its own class. A plain UDreamWidget subclass has no class-level
				// hierarchy to expand and would silently place an empty node.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NotAUserWidgetClass, InNode.Location,
					FString::Printf(TEXT("'%s' is a %s, and a nested node must be a DreamUI user widget"),
						*InPath, *Loaded->GetName()));
				return false;
			}
			OutWidgetClass = Loaded;
			return true;
		};
		// `@Row` -- the class an Asset entry of a resources block names, this file's or one a `use` brought in: a family of
		// components is named once, in the library that styles it, and each screen writes `@Row Row1 { }` instead of the
		// asset path on every line.
		if (TypeName.StartsWith(TEXT("@")))
		{
			const FString ResourceName = TypeName.Mid(1);
			const FDreamUIResource* Resource = InContext.Ast != nullptr ? InContext.Ast->FindResource(ResourceName) : nullptr;
			if (Resource == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownResource, InNode.Location,
					FString::Printf(TEXT("'%s' names no entry in a resources block; a node type written with '@' is an Asset resource, as in 'Asset %s = /Game/UI/WBP_%s'"),
						*TypeName, *ResourceName, *ResourceName));
				return false;
			}
			const bool bIsAsset = Resource->TypeName.Equals(TEXT("Asset"), ESearchCase::IgnoreCase)
				&& (Resource->Value.Kind == EDreamUIValueKind::AssetPath || Resource->Value.Kind == EDreamUIValueKind::String);
			if (!bIsAsset)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ResourceTypeMismatch, InNode.Location,
					FString::Printf(TEXT("'%s' is a node type, so resource '%s' must be an Asset naming a widget class, not a %s"),
						*TypeName, *ResourceName, *Resource->TypeName));
				return false;
			}
			return ResolveWidgetClassAt(Resource->Value.Raw);
		}
		// `Native.Toggle` -- a scoped tag, resolved through the widget registry. What it accepts is
		// exactly what DECLARE_DREAM_GUI_WIDGET declared, so the language never carries a list of the
		// library's controls -- or of anyone else's: a project plugin registering under its own scope
		// is in the language the moment it links.
		int32 DotIndex = INDEX_NONE;
		if (!TypeName.StartsWith(TEXT("/")) && TypeName.FindChar(TEXT('.'), DotIndex))
		{
			const FString ScopeText = TypeName.Left(DotIndex);
			const FName Scope(*ScopeText);
			const FName Name(*TypeName.Mid(DotIndex + 1));
			UClass* Registered = FDreamUIWidgetRegistry::Resolve(Scope, Name);
			if (Registered == nullptr || !Registered->IsChildOf(UDreamUserWidget::StaticClass())
				|| Registered->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
			{
				// A scope that is one of this file's namespaces is a library the author imported, not a registry
				// scope, and "nothing is registered under 'nier'" would send them to C++ for a typo in a .dui.
				// The aliases the library does bring are the useful list.
				if (InContext.Ast != nullptr && FDreamUIWidgetRegistry::NamesInScope(Scope).Num() == 0
					&& InContext.Ast->Namespaces.Contains(ScopeText))
				{
					FString Offered;
					const FString Prefix = ScopeText + TEXT(".");
					for (const FDreamUIComponentAlias& Alias : InContext.Ast->ImportedComponentAliases)
					{
						if (Alias.Alias.StartsWith(Prefix, ESearchCase::IgnoreCase))
						{
							Offered += (Offered.IsEmpty() ? TEXT("") : TEXT(", "));
							Offered += Alias.Alias;
						}
					}
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownNodeType, InNode.Location,
						Offered.IsEmpty()
							? FString::Printf(TEXT("'%s' names no component -- the library imported as '%s' declares none ('use … as Name' in it does)"),
								*TypeName, *ScopeText)
							: FString::Printf(TEXT("'%s' names no component -- the library imported as '%s' declares: %s"),
								*TypeName, *ScopeText, *Offered));
					return false;
				}
				TArray<FName> Known = FDreamUIWidgetRegistry::NamesInScope(Scope);
				FString KnownList;
				for (const FName& KnownName : Known)
				{
					KnownList += (KnownList.IsEmpty() ? TEXT("") : TEXT(", "));
					KnownList += FString::Printf(TEXT("%s.%s"), *Scope.ToString(), *KnownName.ToString());
				}
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownNodeType, InNode.Location,
					KnownList.IsEmpty()
						? FString::Printf(TEXT("'%s' names no registered widget -- nothing is declared under scope '%s' (DECLARE_DREAM_GUI_WIDGET registers one)"),
							*TypeName, *Scope.ToString())
						: FString::Printf(TEXT("'%s' names no registered widget -- scope '%s' declares: %s"),
							*TypeName, *Scope.ToString(), *KnownList));
				return false;
			}
			OutWidgetClass = Registered;
			return true;
		}

		if (TypeName.StartsWith(TEXT("/")))
		{
			return ResolveWidgetClassAt(TypeName);
		}

		InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownNodeType, InNode.Location,
			FString::Printf(TEXT("'%s' is not a built-in tag, a layout container, a component named by 'use … as', or an asset path (those start with '/')"),
				*EllipsizeName(TypeName)));
		return false;
	}

	/**
	 * A `use … as` name that a built-in tag or a layout container already answers to, reported once at its declaration.
	 *
	 * Resolution asks the built-ins first (ResolveNodeClasses), so such an alias could never be written as a type --
	 * `Text Title { }` would go on meaning the visual tag -- and the alias would sit there doing nothing while looking
	 * like it worked. Checked here rather than in the parser because only reflection knows which words are containers.
	 * A namespaced alias (`nier.Text`) is spelt apart from every built-in and is never one.
	 */
	void CheckAliasesAgainstBuiltIns(FBuildContext& InContext)
	{
		TSet<FString> Reported;
		auto Check = [&InContext, &Reported](const FDreamUIComponentAlias& InAlias)
		{
			const FString& Name = InAlias.Alias;
			if (Name.IsEmpty() || Name.Contains(TEXT(".")) || !IsNameLengthLegal(Name) || Reported.Contains(Name))
			{
				return;
			}
			bool bIsKnownTag = false;
			FDreamUITextBuilder::FindVisualClassForTag(Name, bIsKnownTag);
			UClass* Container = bIsKnownTag ? nullptr : FindContainerClassForTypeOnce(Name, InContext);
			if (!bIsKnownTag && Container == nullptr)
			{
				return;
			}
			Reported.Add(Name);
			AddErrorIn(*InContext.Diagnostics, InAlias.SourceName, EDreamUIDiagnosticCode::AliasShadowsBuiltIn, InAlias.Location,
				FString::Printf(TEXT("'%s' is already %s, which a node type means before any alias -- choose another name"),
					*Name, bIsKnownTag ? TEXT("a built-in tag") : *FString::Printf(TEXT("a layout container (%s)"), *Container->GetName())));
		};
		for (const FDreamUIComponentAlias& Alias : InContext.Ast->ComponentAliases)
		{
			Check(Alias);
		}
		for (const FDreamUIComponentAlias& Alias : InContext.Ast->ImportedComponentAliases)
		{
			Check(Alias);
		}
	}

	/** What a host needs to know about one slot a component class declares, read off the class and never an instance. */
	struct FDeclaredSlot
	{
		/** Content a host nests without naming a slot goes here; so does a fill of it, by nesting alone. */
		bool bIsDefault = false;
		/** Whether bAcceptsSeveral below was read, which only an archetype-built class allows. */
		bool bCapacityKnown = false;
		bool bAcceptsSeveral = false;
	};

	/**
	 * The default slot and the capacity of InSlotName, from the class.
	 *
	 * The default from the class default object first -- a native control that overrides GetDefaultSlotName answers
	 * there -- then from the archetype's `slot … default` flag, which is what the base implementation reads off an
	 * INSTANCE's tree and a class default object does not have. The capacity only from an archetype: a native control
	 * makes its slots when an instance initializes, so there is nothing to read it off before one exists, and the run
	 * time says so (AdoptUnslottedChildren) when one refuses.
	 */
	FDeclaredSlot DescribeDeclaredSlot(const UClass* InClass, FName InSlotName)
	{
		FDeclaredSlot Described;
		const UDreamUserWidget* Defaults = InClass != nullptr ? InClass->GetDefaultObject<UDreamUserWidget>() : nullptr;
		FName DefaultSlot = Defaults != nullptr ? Defaults->GetDefaultSlotName() : NAME_None;
		if (const UDreamWidgetTree* Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass))
		{
			Archetype->ForEachWidget([&Described, &DefaultSlot, InSlotName](UDreamWidget* Widget)
			{
				const UDreamNamedSlot* Slot = IsValid(Widget) ? Widget->GetComponent<UDreamNamedSlot>() : nullptr;
				if (Slot == nullptr)
				{
					return;
				}
				if (DefaultSlot.IsNone() && Slot->bIsDefaultSlot)
				{
					DefaultSlot = Slot->GetSlotName();
				}
				if (!Described.bCapacityKnown && Slot->GetSlotName() == InSlotName)
				{
					Described.bCapacityKnown = true;
					Described.bAcceptsSeveral = Slot->bAcceptsSeveral;
				}
			});
		}
		Described.bIsDefault = !InSlotName.IsNone() && DefaultSlot == InSlotName;
		return Described;
	}

	/**
	 * `ListPage Page2 { slot Detail { Text Note { … } } }` -- the host filling one of the component's slots by name.
	 *
	 * No widget of its own: the fill is an address, not a node. Its children are built as the host's content for that
	 * slot, in the shape the designer writes for a drop into a slot row -- children of the instance in the host's tree,
	 * and for any slot but the default one, the instance's NamedSlotContent naming them -- so Initialize hangs them
	 * under the slot (AttachNamedSlotContent). A fill of the DEFAULT slot is nesting alone, as the designer has it too:
	 * AdoptUnslottedChildren takes every unbound child there, and a binding on top would be a second record of one fact.
	 *
	 * Several children reach only the default slot, and only one that takes several: NamedSlotContent maps a name to
	 * ONE widget, so a named slot is filled with one (a container, when the content is several), whatever the hole
	 * would hold.
	 */
	UDreamWidget* BuildSlotFill(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext)
	{
		UDreamUserWidget* Instance = Cast<UDreamUserWidget>(InParent);
		if (Instance == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::SlotFillOutsideComponent, InNode.Location,
				InParent == nullptr
					? FString::Printf(TEXT("'slot %s { … }' fills a slot of a component, and the root sits in none"), *InNode.Id)
					: FString::Printf(TEXT("'slot %s { … }' fills a slot of a component, and '%s' is a %s, which opens none -- nest the content directly, or declare a slot with 'slot %s' and no children"),
						*InNode.Id, *InParent->GetDisplayName(), *InParent->GetClass()->GetName(), *InNode.Id));
			return nullptr;
		}
		if (InNode.Properties.Num() > 0 || InNode.SlotProperties.Num() > 0 || InNode.Components.Num() > 0 || !InNode.StyleName.IsEmpty())
		{
			// The parser refuses this shape; an AST built by hand reaches here with it, and there is nothing these
			// lines could be written on -- the slot widget is the component's, in another file.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::MalformedSlotDeclaration, InNode.Location,
				FString::Printf(TEXT("'slot %s { … }' fills a slot and holds content only -- the slot itself belongs to %s, which sets it up"),
					*InNode.Id, *Instance->GetClass()->GetName()));
			return nullptr;
		}
		if (InNode.Id.IsEmpty() || !IsNameLengthLegal(InNode.Id))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownSlotToFill, InNode.Location,
				TEXT("a slot to fill is named by its id, and this one has none an FName can hold"));
			return nullptr;
		}

		const FName SlotName(*InNode.Id);
		TArray<FName> Declared;
		UDreamUserWidget::CollectDeclaredSlotNames(Instance->GetClass(), Declared);
		if (!Declared.Contains(SlotName))
		{
			FString DeclaredList;
			FString Nearest;
			int32 NearestDistance = MAX_int32;
			for (const FName& Name : Declared)
			{
				const FString Candidate = Name.ToString();
				DeclaredList += (DeclaredList.IsEmpty() ? TEXT("") : TEXT(", "));
				DeclaredList += Candidate;
				const int32 Distance = EditDistance(InNode.Id, Candidate);
				if (Distance < NearestDistance)
				{
					NearestDistance = Distance;
					Nearest = Candidate;
				}
			}
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::UnknownSlotToFill, InNode.Location,
				DeclaredList.IsEmpty()
					? FString::Printf(TEXT("%s declares no slots, so '%s' has nothing to fill"), *Instance->GetClass()->GetName(), *InNode.Id)
					: FString::Printf(TEXT("%s declares no slot named '%s'%s -- its slots are: %s"), *Instance->GetClass()->GetName(), *InNode.Id,
						*FormatSuggestion(NearestDistance <= FMath::Max(2, InNode.Id.Len() / 3) ? Nearest : FString()), *DeclaredList));
			return nullptr;
		}

		const FDeclaredSlot Slot = DescribeDeclaredSlot(Instance->GetClass(), SlotName);
		const int32 ContentCount = InNode.Children.Num();
		if (ContentCount > 1 && !Slot.bIsDefault)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ParentRefusedChild, InNode.Children[1].Location,
				FString::Printf(TEXT("slot '%s' of %s is filled by name, and a named slot holds one widget -- put these %d in one container (for example 'VerticalBox { … }') inside the slot"),
					*InNode.Id, *Instance->GetClass()->GetName(), ContentCount));
			return nullptr;
		}
		if (ContentCount > 1 && Slot.bCapacityKnown && !Slot.bAcceptsSeveral)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ParentRefusedChild, InNode.Children[1].Location,
				FString::Printf(TEXT("slot '%s' of %s holds one widget, and this fills it with %d -- put them in one container, or give the slot a layout ('slot %s default { + VerticalBox { } }') so it takes several"),
					*InNode.Id, *Instance->GetClass()->GetName(), ContentCount, *InNode.Id));
			return nullptr;
		}
		if (!Slot.bIsDefault && Instance->GetContentForNamedSlot(SlotName) != nullptr)
		{
			// A second fill would rebind the name and leave the first content an unbound child, which the default
			// slot would then quietly adopt -- content showing up somewhere the file never put it.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ParentRefusedChild, InNode.Location,
				FString::Printf(TEXT("slot '%s' of '%s' is already filled; a named slot holds one widget"), *InNode.Id, *Instance->GetDisplayName()));
			return nullptr;
		}

		for (const FDreamUINode& Child : InNode.Children)
		{
			UDreamWidget* Content = BuildNode(Child, Instance, InContext);
			if (!IsValid(Content) || Slot.bIsDefault || Child.Kind != EDreamUINodeKind::Widget)
			{
				continue;
			}
			if (!Instance->SetContentForNamedSlot(SlotName, Content))
			{
				// Not reachable from a file -- the content is this tree's and under the instance, which is all the
				// binding asks -- but a refusal otherwise leaves the content an unbound child, adopted by the default slot.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ParentRefusedChild, Child.Location,
					FString::Printf(TEXT("'%s' could not be bound into slot '%s' of '%s'"), *Child.Id, *InNode.Id, *Instance->GetDisplayName()));
			}
		}
		return nullptr;
	}

	UDreamWidget* BuildEachLoop(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext);
	UDreamWidget* BuildForLoop(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext);

	/**
	 * Where a loop's items come from, onto its record. `in Items` / `in GetItems()` is SourceName alone, written exactly
	 * as every loop before member paths wrote it; `in Inventory.Items` / `in Inventory.Filtered()` is every segment in
	 * SourcePath and the last one, the array or the function on the object reached, in SourceName as well -- which
	 * bSourceIsFunction already describes, the parser having read it off the parentheses after that last segment.
	 * False, reported, for a path with a segment no name can hold; the caller drops the loop.
	 */
	bool RecordLoopSource(const FDreamUINode& InNode, FDreamWidgetEachBinding& OutBinding, FBuildContext& InContext)
	{
		TArray<FString> Segments;
		InNode.LoopSourceFunction.ParseIntoArray(Segments, TEXT("."));
		if (Segments.Num() <= 1)
		{
			OutBinding.SourceName = FName(*InNode.LoopSourceFunction);
			return true;
		}
		for (const FString& Segment : Segments)
		{
			// Gated like every authored name that becomes an FName. The lexer keeps each word of a parsed path short of
			// the limit; this is for an AST put together by hand.
			if (!IsNameLengthLegal(Segment))
			{
				InContext.Diagnostics->AddError(InNode.Kind == EDreamUINodeKind::ForLoop ? EDreamUIDiagnosticCode::ForMisplaced
					: EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
					FString::Printf(TEXT("'%s' in this loop's source is %d characters long, and a name holds at most %d"),
						*EllipsizeName(Segment), Segment.Len(), NAME_SIZE - 1));
				return false;
			}
			OutBinding.SourcePath.Add(FName(*Segment));
		}
		OutBinding.SourceName = OutBinding.SourcePath.Last();
		return true;
	}

	UDreamWidget* BuildNode(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext)
	{
		if (InNode.Kind == EDreamUINodeKind::EachLoop && InContext.EachBindings != nullptr)
		{
			return BuildEachLoop(InNode, InParent, InContext);
		}
		if (InNode.Kind == EDreamUINodeKind::ForLoop)
		{
			// A `for` is recorded where an `each` is -- one FDreamWidgetEachBinding, bInPanel set -- because it is the
			// same compiled shape: a template, a source and the item writes. Only the run time differs. So it needs the
			// same sink, and without one it degrades the same way an `each` does just below: a warning, for a caller
			// that builds a raw AST (most tests) and wants the rest of the file. A compile always offers the sink.
			if (InContext.EachBindings != nullptr)
			{
				return BuildForLoop(InNode, InParent, InContext);
			}
			InContext.Diagnostics->AddWarning(EDreamUIDiagnosticCode::LoopNotExpanded, InNode.Location,
				TEXT("this caller offered nowhere to record a 'for', so everything under this one was skipped"));
			return nullptr;
		}
		if (InNode.Kind == EDreamUINodeKind::EachLoop)
		{
			// The same warning as a `for` just above, for the same reason: a block only reaches here when the CALLER
			// offered nowhere to record it -- a hand-built AST, most tests. The rest of the file is a tree worth
			// building, and an author previewing a screen wants to see the parts that do work.
			InContext.Diagnostics->AddWarning(EDreamUIDiagnosticCode::LoopNotExpanded, InNode.Location,
				TEXT("this caller offered nowhere to record an 'each', so everything under this one was skipped"));
			return nullptr;
		}

		if (InNode.Kind == EDreamUINodeKind::NamedSlot && InNode.bFillsSlot)
		{
			// The host filling a component's slot: an address, not a node, so none of what follows applies.
			return BuildSlotFill(InNode, InParent, InContext);
		}

		UClass* WidgetClass = nullptr;
		UClass* VisualClass = nullptr;
		UClass* TypeContainerClass = nullptr;
		if (!ResolveNodeClasses(InNode, InContext, WidgetClass, VisualClass, TypeContainerClass))
		{
			return nullptr;
		}

		if (!IsNameLengthLegal(InNode.Id))
		{
			// Refused rather than truncated: the id IS the identity -- object name, member variable,
			// binding key and localization key at once -- and a shortened one would silently be a
			// different node from the one the file describes. A parse raises DUI1006 for this at the
			// token, so this is the same rule for the callers that assemble a node by hand.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::InvalidNodeId, InNode.Location,
				FString::Printf(TEXT("'%s' is %d characters long, and a node id holds at most %d"),
					*EllipsizeName(InNode.Id), InNode.Id.Len(), NAME_SIZE - 1));
			return nullptr;
		}

		// THE rule. Not NewObject<UDreamWidget>(World, …), which is what UIML did and why a UIML
		// hierarchy is flat-outered to the world, owned by nothing, and can never become a class
		// template, enter the designer, or carry a binding. See UDreamWidgetTree's class comment.
		//
		// Name and guid both derive from the node id, so every compile of the same file births the
		// same identities: designer state is keyed by object FName and preview pairing by guid, and
		// with random identities both reset on every rebuild while the .uasset churns for source
		// control. MakeUniqueObjectName keeps a duplicate id from tripping NewObject's fatal name
		// collision -- deterministic anyway, because build order is AST order -- and the guid is
		// hashed from the RESOLVED name so even that duplicate pair gets distinct identities. Salted
		// with the localization namespace (the class path): a guid derived from the bare id would
		// recreate exactly the cross-asset collision guids were introduced to fix.
		FName WidgetName = NAME_None;
		FGuid WidgetGuid;
		if (!InNode.Id.IsEmpty())
		{
			// The bare id, verbatim, whenever it is free -- MakeUniqueObjectName is not tried first
			// because it ALWAYS numbers ("Root" becomes "Root_0"), and the whole point is that the
			// object is named exactly what the author typed. The occupied case is error recovery
			// only: duplicate ids are a build error upstream (DuplicateNodeId).
			WidgetName = FName(*InNode.Id);
			if (StaticFindObjectFast(nullptr, InContext.Tree, WidgetName) != nullptr)
			{
				WidgetName = MakeUniqueObjectName(InContext.Tree, WidgetClass, WidgetName);
			}
			WidgetGuid = FGuid::NewDeterministicGuid(InContext.LocalizationNamespace + TEXT("/") + WidgetName.ToString());
		}
		UDreamWidget* Widget = InContext.Tree->ConstructWidget(WidgetClass, WidgetName, WidgetGuid);
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
		UDreamNamedSlot* DeclaredSlot = nullptr;
		if (InNode.Kind == EDreamUINodeKind::NamedSlot)
		{
			// The slot's name IS the widget's display name (UDreamNamedSlot::GetSlotName), so the
			// SetDisplayName above is what named it.
			DeclaredSlot = Widget->AddComponent<UDreamNamedSlot>();
			if (InNode.bDefaultSlot && IsValid(DeclaredSlot))
			{
				// `slot Rows default` -- what GetDefaultSlotName answers for a class that does not override it. One per
				// tree: content nested without a slot name has one place to go, and a second default would make which
				// one depend on tree order. The parser says so too; this is the same rule for a tree built by hand.
				if (InContext.DefaultSlotNode != nullptr)
				{
					InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::MultipleDefaultSlots, InNode.Location,
						FString::Printf(TEXT("'%s' is marked default, and so is '%s' (line %d) -- a class has one default slot"),
							*InNode.Id, *InContext.DefaultSlotNode->Id, InContext.DefaultSlotNode->Location.Line));
				}
				else
				{
					InContext.DefaultSlotNode = &InNode;
					DeclaredSlot->bIsDefaultSlot = true;
				}
			}
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
		TArray<const FDreamUIStyle*> Styles;
		ResolveStyleChain(InNode, InContext, Styles);
		BuildComponents(InNode, Styles, TypeContainerClass, Widget, InContext);
		if (IsValid(DeclaredSlot) && IsValid(Widget->GetLayoutContainer()))
		{
			// `slot Rows { + VerticalBox { Spacing = 15 } }` -- a hole with a layout is one that arranges what it is
			// given, so it takes several (the host's rows, each laid out by it). Set before the node's own lines, so an
			// author who writes `bAcceptsSeveral = false` on it still has the last word.
			DeclaredSlot->bAcceptsSeveral = true;
		}
		BuildProperties(InNode, Styles, TypeContainerClass != nullptr, Widget, InContext);
		BuildSlotProperties(InNode, Styles, Widget, InContext);

		// Every `AnchorData.X = ...` line above wrote its field in place -- a field inside a struct has no setter
		// (see AddBinding) -- and TrySetParent had already worked the widget's size, edge offsets and placement out
		// from the class default by then. Nothing written in place told it otherwise, so the widget went on
		// reporting 100 x 100, the capture below took that for the authored size every panel measures this child
		// by, and the children built next stretched against it. Handed back whole through SetAnchorData, the
		// struct is worked out again exactly as a setter call from code would have it.
		Widget->SetAnchorData(FDreamUIAnchorData(Widget->GetAnchorData()));

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

	UDreamWidget* BuildEachLoop(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext)
	{
		if (InContext.ActiveEach != nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
				FString::Printf(TEXT("an 'each' cannot nest inside %s"),
					InContext.ActiveEach->bInPanel ? TEXT("a 'for'") : TEXT("another 'each'")));
			return nullptr;
		}
		if (InParent == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
				TEXT("an 'each' lives inside the widget whose list it fills; it cannot be the root"));
			return nullptr;
		}
		// The list views are the control library's, reached through the handler it registers.
		const IDreamUIEachBindingHandler* Handler = DreamUI::GetEachBindingHandler();
		if (Handler == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
				TEXT("an 'each' fills a list view, and no module that has list views is loaded"));
			return nullptr;
		}
		// The host contract: the ENCLOSING widget carries the list view, configured however the
		// author likes -- the `each` only supplies the template and the data. Auto-adding a view
		// here would mean auto-choosing its scroll direction, content and bars, which are layout
		// decisions this block has no words for.
		if (!Handler->HasListView(InParent))
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
				FString::Printf(TEXT("'%s' has no UIListView/UIRecyclableScrollView behaviour for this 'each' to fill -- add one with '+ UIListView { }'"),
					*InParent->GetDisplayName()));
			return nullptr;
		}

		const FDreamUINode* TemplateNode = nullptr;
		for (const FDreamUINode& Child : InNode.Children)
		{
			if (Child.Kind != EDreamUINodeKind::Widget || TemplateNode != nullptr)
			{
				TemplateNode = nullptr;
				break;
			}
			TemplateNode = &Child;
		}
		if (TemplateNode == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::EachMisplaced, InNode.Location,
				TEXT("an 'each' body holds exactly one template widget -- the thing repeated per item"));
			return nullptr;
		}

		FDreamWidgetEachBinding& Each = InContext.EachBindings->AddDefaulted_GetRef();
		Each.HostWidgetName = UDreamWidgetTree::MakeWidgetVariableName(InParent);
		if (!RecordLoopSource(InNode, Each, InContext))
		{
			InContext.EachBindings->Pop();
			return nullptr;
		}
		Each.bSourceIsFunction = InNode.bLoopSourceIsFunction;
		Each.LoopVariable = FName(*InNode.LoopVariable);
#if WITH_EDITORONLY_DATA
		// The `each` HEADER's position, not the template's: DUI6006 and DUI6007 are both complaints
		// about the source named on this line, and pointing an author at the widget inside the block
		// would send them to read the one part of it that is fine.
		Each.SourceLine = InNode.Location.Line;
		Each.SourceColumn = InNode.Location.Column;
#endif

		// Nested `each` is refused above, so no sibling can grow EachBindings under this pointer
		// while it is live -- the reallocation that would otherwise dangle it cannot happen.
		InContext.ActiveEach = &Each;
		InContext.ActiveLoopVariable = InNode.LoopVariable;
		UDreamWidget* Template = BuildNode(*TemplateNode, InParent, InContext);
		InContext.ActiveEach = nullptr;
		InContext.ActiveLoopVariable.Reset();

		if (!IsValid(Template))
		{
			InContext.EachBindings->Pop();
			return nullptr;
		}
		Each.TemplateWidgetName = UDreamWidgetTree::MakeWidgetVariableName(Template);

		// The runtime prerequisites the walkthrough caught the view silently returning without:
		// a cell marker on the template, InitializeOnDataSource's CONTENT widget under the host (the
		// thing scrolling moves and sizes), and exactly one scroll axis -- the scroll-view default is
		// both. The language has no words for any of them, so they are supplied here: the handler
		// marks the template and settles the axis (vertical when the author configured no single
		// axis; one who set an axis keeps it), and the builder synthesizes the content.
		const bool bVertical = Handler->PrepareHost(InParent, Template);
		FString ContentName = InParent->GetDisplayName() + TEXT("_EachContent");
		if (ContentName.Len() >= NAME_SIZE)
		{
			// An id at the most a name holds makes this one longer than that, and an FName past NAME_SIZE stops the editor.
			// Cut, with the whole spelling's hash, so the same id keeps the same content name.
			ContentName = FString::Printf(TEXT("%s_%08X_EachContent"), *ContentName.Left(NAME_SIZE - 32), FCrc::StrCrc32(*ContentName));
		}
		UDreamWidget* Content = InContext.Tree->ConstructWidget(UDreamWidget::StaticClass(), FName(*ContentName),
			FGuid::NewDeterministicGuid(InContext.LocalizationNamespace + TEXT("/") + ContentName));
		if (IsValid(Content))
		{
			Content->SetDisplayName(ContentName);
			FDreamUIAnchorData ContentAnchors;
			if (bVertical)
			{
				// Stretch across the top: the view drives the height from the item count.
				ContentAnchors.AnchorMin = FVector2D(0.0, 1.0);
				ContentAnchors.AnchorMax = FVector2D(1.0, 1.0);
				ContentAnchors.Pivot = FVector2D(0.5, 1.0);
			}
			else
			{
				ContentAnchors.AnchorMin = FVector2D(0.0, 0.0);
				ContentAnchors.AnchorMax = FVector2D(0.0, 1.0);
				ContentAnchors.Pivot = FVector2D(0.0, 0.5);
			}
			ContentAnchors.AnchoredPosition = FVector2D::ZeroVector;
			ContentAnchors.SizeDelta = FVector2D::ZeroVector;
			Content->SetAnchorData(ContentAnchors);
			Content->TrySetParent(InParent, /*bKeepWorldPosition*/false);
			Template->TrySetParent(Content, /*bKeepWorldPosition*/false);
			Handler->SetContent(InParent, Content);
			// By name too: the pointer above lives in the archetype and does not survive into
			// instances -- ResolveEachBindings re-aims it per instance through this.
			Each.ContentWidgetName = UDreamWidgetTree::MakeWidgetVariableName(Content);
		}
		return Template;
	}

	/**
	 * `for Option in GetOptions() { Row { Label <- Option.Label } }` -- one copy of the body per item, made at run
	 * time inside the enclosing widget itself.
	 *
	 * What is built here is exactly one widget: the TEMPLATE, as an ordinary child of the host, at the place the
	 * `for` was written among the host's children. The run time (UDreamUIForAdapter) collapses it and puts the copies
	 * right after it, so "where the `for` was written" is where the rows appear, between whatever siblings surround
	 * it -- a header above, a footer below. Nothing else is synthesized: an `each` needs a content widget because a
	 * list view scrolls one, and a `for` has no list view; the host's own layout container arranges the copies like
	 * any other children.
	 *
	 * Recorded as an FDreamWidgetEachBinding with bInPanel set, built through the same item-binding path an `each`
	 * uses (ActiveEach), so `Prop <- Option.Member` lines inside the body become entry bindings and anything richer
	 * is refused with the same LoopBodyBindingUnsupported. The compiler's source checks (DUI6006, DUI6007) read the
	 * same array and so cover a `for` without knowing it is one.
	 */
	UDreamWidget* BuildForLoop(const FDreamUINode& InNode, UDreamWidget* InParent, FBuildContext& InContext)
	{
		if (InContext.ActiveEach != nullptr)
		{
			// One level of repetition per template. A `for` in a `for` would need a copy of the inner adapter per
			// outer copy, and the item writes of the inner one would name a variable the outer copy does not have;
			// the way to nest is a component whose own file has the inner `for`, fed through a `props` variable.
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ForMisplaced, InNode.Location,
				FString::Printf(TEXT("a 'for' cannot sit inside a '%s': repeat a component whose own file has the inner 'for', and hand it the inner list through one of its props"),
					InContext.ActiveEach->bInPanel ? TEXT("for") : TEXT("each")));
			return nullptr;
		}
		if (InParent == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ForMisplaced, InNode.Location,
				TEXT("a 'for' repeats its body inside the widget it is written in, so it cannot be the root"));
			return nullptr;
		}
		// A host that holds a fixed number of children -- a scroll box's single content, a content widget -- has no
		// room for a number of copies only the data decides. Said here rather than found at run time, where the
		// copies past the limit would be refused one by one with nothing in the file to point at.
		const int32 Capacity = InParent->GetMaxChildrenCapacity();
		if (Capacity != INDEX_NONE)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ForMisplaced, InNode.Location,
				FString::Printf(TEXT("'%s' holds at most %d %s, and a 'for' adds one per item -- put a container such as 'VerticalBox' inside it and the 'for' in that"),
					*InParent->GetDisplayName(), Capacity, Capacity == 1 ? TEXT("child") : TEXT("children")));
			return nullptr;
		}

		const FDreamUINode* TemplateNode = nullptr;
		for (const FDreamUINode& Child : InNode.Children)
		{
			if (Child.Kind != EDreamUINodeKind::Widget || TemplateNode != nullptr)
			{
				TemplateNode = nullptr;
				break;
			}
			TemplateNode = &Child;
		}
		if (TemplateNode == nullptr)
		{
			InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ForMisplaced, InNode.Location,
				TEXT("a 'for' body holds exactly one widget -- the thing repeated per item; wrap several in one container"));
			return nullptr;
		}

		FDreamWidgetEachBinding& ForBinding = InContext.EachBindings->AddDefaulted_GetRef();
		ForBinding.bInPanel = true;
		ForBinding.HostWidgetName = UDreamWidgetTree::MakeWidgetVariableName(InParent);
		if (!RecordLoopSource(InNode, ForBinding, InContext))
		{
			InContext.EachBindings->Pop();
			return nullptr;
		}
		ForBinding.bSourceIsFunction = InNode.bLoopSourceIsFunction;
		ForBinding.LoopVariable = FName(*InNode.LoopVariable);
#if WITH_EDITORONLY_DATA
		// The header's position, as for an `each`: DUI6006 and DUI6007 are about the source named on that line.
		ForBinding.SourceLine = InNode.Location.Line;
		ForBinding.SourceColumn = InNode.Location.Column;
#endif

		// Set before the body is built, because AddBinding reads it: it is what lets an item write land on a
		// component's setter-less property (see bWritesDirectly there). The pointer stays valid for the same reason
		// the `each` one does -- anything that would add to EachBindings under it is refused above.
		InContext.ActiveEach = &ForBinding;
		InContext.ActiveLoopVariable = InNode.LoopVariable;
		UDreamWidget* Template = BuildNode(*TemplateNode, InParent, InContext);
		InContext.ActiveEach = nullptr;
		InContext.ActiveLoopVariable.Reset();

		if (!IsValid(Template))
		{
			InContext.EachBindings->Pop();
			return nullptr;
		}
		ForBinding.TemplateWidgetName = UDreamWidgetTree::MakeWidgetVariableName(Template);
		return Template;
	}

	/**
	 * Every `Prop = SomeNode` line the walk deferred, now that every node it could name exists.
	 *
	 * One flat pass, not a fixpoint: a node reference resolves to a WIDGET, never to another pending
	 * reference, so nothing here can unblock anything else here. In collection order, so a node line
	 * still overrides the style line that wrote the same property -- assignment order is override
	 * order, and deferring these must not quietly stop being true of them.
	 */
	void ResolveNodeReferences(FBuildContext& InContext)
	{
		for (const FPendingNodeReference& Pending : InContext.NodeReferences)
		{
			// The tree's own lookup, sanitized with the tree's own function. The author writes a node
			// id and what the tree matches on is that id sanitized into an identifier, and a private
			// copy of that walk is exactly how `Ok Btn` becomes a member variable for the compiler and
			// comes back null here -- the same trap MakeWidgetVariableName exists to close.
			UDreamWidget* Named = InContext.Tree->FindWidgetByVariableName(
				FName(*UDreamWidgetTree::SanitizeIdentifier(Pending.NodeId)));
			if (!IsValid(Named))
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::NodeReferenceNotFound, Pending.Location,
					FString::Printf(TEXT("'%s' names no node in this file, so '%s' has nothing to point at%s"),
						*Pending.NodeId, *Pending.PropertyName,
						*FormatSuggestion(SuggestNearestNodeId(Pending.NodeId, InContext.Tree))));
				continue;
			}

			UClass* Wanted = Pending.Property->PropertyClass;
			// A choice, not a fallback: UDreamWidget, UDreamVisual and UDreamUIBehaviour share no
			// ancestor below UObject, so the property's type says unambiguously which part of the named
			// node the author meant. Coercing to the visual is what lets a transition be aimed at
			// `CheckMark` rather than at some second name for the same node.
			UObject* Resolved = nullptr;
			const TCHAR* MissingPartAdvice = nullptr;
			if (Wanted->IsChildOf(UDreamVisual::StaticClass()))
			{
				Resolved = Named->GetVisual();
				MissingPartAdvice = TEXT("give that node a visual tag such as 'Image'");
			}
			else if (Wanted->IsChildOf(UDreamUIBehaviour::StaticClass()))
			{
				// A property typed as the behaviour base can name, through MustImplement, the interface
				// its behaviour has to implement instead -- the scroll box's Scrollbar is one. The part of
				// the node meant is then the behaviour that implements it, not whichever comes first. The
				// metadata is editor-only, and so is the one caller that builds from text.
				UClass* Interface = nullptr;
#if WITH_EDITORONLY_DATA
				if (const FString* MustImplement = Pending.Property->FindMetaData(TEXT("MustImplement")))
				{
					Interface = FindObject<UClass>(nullptr, **MustImplement);
				}
#endif
				Resolved = Interface != nullptr ? Named->GetComponentByInterface(Interface) : Named->GetComponent(Wanted);
				MissingPartAdvice = TEXT("add that behaviour to the node with '+'");
			}
			else
			{
				Resolved = Named;
			}
			if (Resolved == nullptr)
			{
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Pending.Location,
					FString::Printf(TEXT("'%s' has no %s, and '%s' takes one -- %s"),
						*Pending.NodeId, *Wanted->GetName(), *Pending.PropertyName, MissingPartAdvice));
				continue;
			}
			if (!Resolved->IsA(Wanted))
			{
				// The same code and the same sentence an asset gets when it loads and turns out to be
				// the wrong class. The reader's move is identical, so the code is.
				InContext.Diagnostics->AddError(EDreamUIDiagnosticCode::ValueTypeMismatch, Pending.Location,
					FString::Printf(TEXT("'%s' is a %s, and '%s' takes a %s"), *Pending.NodeId,
						*Resolved->GetClass()->GetName(), *Pending.PropertyName, *Wanted->GetName()));
				continue;
			}
			// This write lands in the class TEMPLATE, and a non-Instanced object reference is copied
			// verbatim into every instance -- InitializeWidgetStatic instances the tree with an
			// FObjectInstancingGraph, which re-aims only properties carrying CPF_InstancedReference.
			// Left at that, every instance's toggle would drive the ARCHETYPE's visual: one object
			// shared by all of them and never drawn. What makes this write correct is the pass that
			// runs one layer up -- RetargetIntraTreeReferences, called from InitializeWidgetStatic --
			// which re-resolves any pointer still naming the template against the instance's own tree.
			Pending.Property->SetObjectPropertyValue(Pending.LeafValuePtr, Resolved);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Timelines
	//
	// The proposal's layer one, materialised. A `timeline` block becomes one UDreamWidgetAnimation in
	// the root's UDreamWidgetAnimationComponent, marked language-owned, rebuilt from the text on every
	// compile exactly as the tree is -- so the file stays the single truth and nothing in the editor
	// can write a value the file does not contain.
	//
	// WHY EVERY EASED SEGMENT IS SAMPLED INTO LINEAR KEYS. The proposal's first load-bearing fact is
	// that FRichCurve tangents are STORED data, so a text form has two choices: write the four
	// numbers (unwritable by hand, and a lie about what an author edited) or write a NAME and derive
	// the curve. Deriving it as a pair of endpoint tangents is only right for the curves a single
	// cubic can be -- Elastic, Back and Bounce overshoot and oscillate, and a cubic silently smooths
	// them into something else. Sampling at the sequence's own display rate is right for ALL of them,
	// costs a handful of keys, needs no per-family table to drift, and is regenerated identically
	// from one word on every compile. The curve library is called for the values, so there is exactly
	// one definition of what `ease OutBounce` means in this plugin.
	// ---------------------------------------------------------------------------------------------

	/** The tick resolution and display rate every language-owned timeline is built at. */
	constexpr int32 TimelineTickResolution = 24000;
	constexpr int32 TimelineDisplayRate = 60;

	FFrameNumber TimelineTimeToFrame(double InSeconds)
	{
		return FFrameNumber(static_cast<int32>(FMath::RoundToDouble(InSeconds * static_cast<double>(TimelineTickResolution))));
	}

	/**
	 * The node a track line's '/'-separated path names, or null.
	 *
	 * An empty path is the animation's own host -- the same "" a binding records for the context
	 * widget -- and every step after that is a DISPLAY NAME, matched case insensitively because ids
	 * are FNames downstream and the parser already refuses two that differ only in case.
	 */
	UDreamWidget* FindWidgetByNodePath(UDreamWidget* InHost, const FString& InPath)
	{
		if (!IsValid(InHost))
		{
			return nullptr;
		}
		if (InPath.IsEmpty())
		{
			return InHost;
		}
		TArray<FString> Segments;
		InPath.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty*/true);

		UDreamWidget* Current = InHost;
		for (const FString& Segment : Segments)
		{
			UDreamWidget* Next = nullptr;
			for (UDreamWidget* Child : Current->GetChildren())
			{
				if (IsValid(Child) && Child->GetDisplayName() == Segment)
				{
					Next = Child;
					break;
				}
			}
			if (Next == nullptr)
			{
				return nullptr;
			}
			Current = Next;
		}
		return Current;
	}

	/** What a timeline track line resolved to: the object to possess, and the property path on it. */
	struct FResolvedTimelineTarget
	{
		UObject* Object = nullptr;
		/** The HEAD property, which is the one Sequencer's Interp rule is about. */
		FProperty* HeadProperty = nullptr;
		FProperty* LeafProperty = nullptr;
		/** The dotted path as MovieScene wants it, which is the author's spelling verbatim. */
		FString PropertyPath;
	};

	/**
	 * The property a track line's head segment names on this class: the FIELD name, or the label the
	 * animation editor puts on the row.
	 *
	 * The second reading is not a convenience, it is what makes the geometry animatable at all.
	 * UDreamWidget carries six Interp mirrors of AnchorData that exist for precisely this -- their
	 * own comment says "Interp mirrors of AnchorData for Sequencer" -- and they are FIELDS called
	 * `AnimatableWidth` and `AnimatableHeight` wearing DisplayNames "Width" and "Height". The row an
	 * author sees in the animation editor says Width; so does the details panel; so, therefore, does
	 * a timeline line.
	 *
	 * This does NOT contradict the ruling that `Width = 400` on an assignment line is DUI4001 with a
	 * hint pointing at `AnchorData.SizeDelta`. An assignment writes a PROPERTY and a timeline drives a
	 * TRACK, and those are two questions with two naming systems that both already exist in the
	 * engine. Writing the anchor block from a track is not even possible -- a struct has no property
	 * track -- which is why the mirrors were added in the first place.
	 *
	 * Restricted to Interp properties so an alias can only ever name something animatable, and so a
	 * DisplayName on some unrelated property cannot shadow a real field name. WITH_EDITORONLY_DATA,
	 * because metadata is: a .dui compiled in a packaged build resolves the field name and not the
	 * label, which is the honest degradation -- the label is an editor artefact.
	 */
	FProperty* FindTimelinePropertyOn(const UStruct* InScope, const FString& InName)
	{
		if (FProperty* Direct = InScope->FindPropertyByName(FName(*InName)))
		{
			return Direct;
		}
#if WITH_EDITORONLY_DATA
		for (TFieldIterator<FProperty> It(InScope); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Interp) && It->GetMetaData(TEXT("DisplayName")) == InName)
			{
				return *It;
			}
		}
#endif
		return nullptr;
	}

	/**
	 * Which object on this node owns the property, and which property it is.
	 *
	 * The same candidate order every bare property name uses -- widget, its visual, then behaviours --
	 * so `Color` on a Text node means the visual's Color in a timeline exactly as it does in an
	 * assignment. Nothing here reports; the caller has the line and turns a false into DUI5016.
	 */
	bool ResolveTimelineTarget(UDreamWidget* InWidget, const FString& InPropertyPath, FResolvedTimelineTarget& OutTarget)
	{
		TArray<FString> Segments;
		InPropertyPath.ParseIntoArray(Segments, TEXT("."), /*InCullEmpty*/true);
		if (Segments.Num() == 0)
		{
			return false;
		}

		TArray<UObject*> Candidates;
		Candidates.Add(InWidget);
		if (UDreamVisual* Visual = InWidget->GetVisual())
		{
			Candidates.Add(Visual);
		}
		for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			if (IsValid(Behaviour))
			{
				Candidates.Add(Behaviour);
			}
		}

		for (UObject* Candidate : Candidates)
		{
			FProperty* Head = FindTimelinePropertyOn(Candidate->GetClass(), Segments[0]);
			if (Head == nullptr)
			{
				continue;
			}
			// The RESOLVED spelling is accumulated as the walk goes, not copied from the author's
			// text, because the two can differ: `Width` resolves to the field `AnimatableWidth`, and
			// the path is what FTrackInstancePropertyBindings walks at run time. Handing it the label
			// would build a track that resolves nothing and drives nothing, silently.
			TArray<FString> ResolvedSegments;
			ResolvedSegments.Add(Head->GetName());

			FProperty* Leaf = Head;
			for (int32 Index = 1; Index < Segments.Num() && Leaf != nullptr; ++Index)
			{
				const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf);
				// Field names only inside a struct: a label is something the animation editor puts on
				// a track ROW, and a row is the head of the path, never a field within one.
				Leaf = AsStruct != nullptr ? AsStruct->Struct->FindPropertyByName(FName(*Segments[Index])) : nullptr;
				if (Leaf != nullptr)
				{
					ResolvedSegments.Add(Leaf->GetName());
				}
			}
			if (Leaf == nullptr)
			{
				return false;
			}
			OutTarget.Object = Candidate;
			OutTarget.HeadProperty = Head;
			OutTarget.LeafProperty = Leaf;
			OutTarget.PropertyPath = FString::Join(ResolvedSegments, TEXT("."));
			return true;
		}
		return false;
	}

	/** How many float/double channels this property's track carries, or 0 when it has no track. */
	int32 GetTimelineChannelCount(const FProperty* InProperty, bool& bOutIsColor, bool& bOutIsFloatChannel)
	{
		bOutIsColor = false;
		bOutIsFloatChannel = false;
		if (CastField<FFloatProperty>(InProperty) != nullptr)
		{
			bOutIsFloatChannel = true;
			return 1;
		}
		if (CastField<FDoubleProperty>(InProperty) != nullptr)
		{
			return 1;
		}
		const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty);
		if (AsStruct == nullptr)
		{
			return 0;
		}
		// FLinearColor and FColor, and deliberately not FSlateColor: a slate colour is a value OR a
		// style-table lookup, and a track that drove the value half would silently turn a themed
		// colour into a literal one. Nothing in the library declares an Interp FSlateColor anyway.
		if (AsStruct->Struct == TBaseStructure<FLinearColor>::Get()
			|| AsStruct->Struct == TBaseStructure<FColor>::Get())
		{
			bOutIsColor = true;
			bOutIsFloatChannel = true;
			return 4;
		}
		if (AsStruct->Struct == TBaseStructure<FVector2D>::Get())
		{
			return 2;
		}
		if (AsStruct->Struct == TBaseStructure<FVector>::Get())
		{
			return 3;
		}
		if (AsStruct->Struct == TBaseStructure<FVector4>::Get())
		{
			return 4;
		}
		return 0;
	}

	/**
	 * The authored literal as up to four channel values, through the SAME parser every other value
	 * uses -- so `(1, 1)`, `#FFC800` and `0.5` mean here exactly what they mean on a property line.
	 *
	 * Parsed into a scratch copy of the destination property rather than interpreted by shape, which
	 * is what keeps colour quantisation, tuple arity and the short-form table in one place.
	 */
	bool ReadTimelineChannels(const FProperty* InLeaf, const FDreamUIValue& InValue, int32 InChannelCount,
		bool bInIsColor, double OutChannels[4])
	{
		void* Scratch = FMemory::Malloc(InLeaf->GetSize(), InLeaf->GetMinAlignment());
		InLeaf->InitializeValue(Scratch);
		bool bParsed = false;
		if (DreamUIValueFormat::HasShortForm(InLeaf))
		{
			bParsed = DreamUIValueFormat::Parse(InLeaf, InValue, Scratch);
		}
		else if (const FNumericProperty* AsNumeric = CastField<FNumericProperty>(InLeaf))
		{
			double Number = 0.0;
			bParsed = InValue.Kind == EDreamUIValueKind::Number && LexTryParseString(Number, *InValue.Raw);
			if (bParsed)
			{
				AsNumeric->SetFloatingPointPropertyValue(Scratch, Number);
			}
		}

		if (bParsed)
		{
			if (bInIsColor)
			{
				// Through FLinearColor whatever the destination is: the colour track's four channels
				// are linear floats, and FColor's bytes are sRGB. ParseColorHex/PrintColorHex own
				// that conversion for the whole pipeline; reproducing it here is how the two drift.
				FLinearColor Linear = FLinearColor::White;
				const FStructProperty* AsStruct = CastField<FStructProperty>(InLeaf);
				if (AsStruct->Struct == TBaseStructure<FLinearColor>::Get())
				{
					Linear = *static_cast<const FLinearColor*>(Scratch);
				}
				else
				{
					// FLinearColor(FColor) is the sRGB-decoding constructor -- the same pair
					// ParseColorHex and PrintColorHex use, because the track's channels are linear.
					Linear = FLinearColor(*static_cast<const FColor*>(Scratch));
				}
				OutChannels[0] = Linear.R;
				OutChannels[1] = Linear.G;
				OutChannels[2] = Linear.B;
				OutChannels[3] = Linear.A;
			}
			else if (const FNumericProperty* AsNumeric = CastField<FNumericProperty>(InLeaf))
			{
				OutChannels[0] = AsNumeric->GetFloatingPointPropertyValue(Scratch);
			}
			else if (InChannelCount == 2)
			{
				const FVector2D& Value = *static_cast<const FVector2D*>(Scratch);
				OutChannels[0] = Value.X;
				OutChannels[1] = Value.Y;
			}
			else if (InChannelCount == 3)
			{
				const FVector& Value = *static_cast<const FVector*>(Scratch);
				OutChannels[0] = Value.X;
				OutChannels[1] = Value.Y;
				OutChannels[2] = Value.Z;
			}
			else if (InChannelCount == 4)
			{
				const FVector4& Value = *static_cast<const FVector4*>(Scratch);
				OutChannels[0] = Value.X;
				OutChannels[1] = Value.Y;
				OutChannels[2] = Value.Z;
				OutChannels[3] = Value.W;
			}
		}

		InLeaf->DestroyValue(Scratch);
		FMemory::Free(Scratch);
		return bParsed;
	}

	/** The ease of that name, or false. The word list is EDreamTweenEase's, read through reflection. */
	bool FindEaseType(const FString& InName, EDreamTweenEase& OutEase)
	{
		const UEnum* EaseEnum = StaticEnum<EDreamTweenEase>();
		const int64 Value = EaseEnum != nullptr ? EaseEnum->GetValueByNameString(InName) : INDEX_NONE;
		if (Value == INDEX_NONE || static_cast<EDreamTweenEase>(Value) == EDreamTweenEase::CurveFloat)
		{
			// CurveFloat is excluded on purpose rather than missing: it names a curve ASSET, and a
			// timeline key has nowhere to put one. An author who needs a hand-drawn curve marks the
			// timeline `external` and draws it in Sequencer, which is the layer that exists for it.
			return false;
		}
		OutEase = static_cast<EDreamTweenEase>(Value);
		return true;
	}

	/** One channel's worth of keys, in frames, for one segment of one track. */
	struct FTimelineSample
	{
		FFrameNumber Frame;
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
	};

	/**
	 * Every key a track line produces, eases resolved into sampled points.
	 *
	 * Returns false only when an ease name is not one; a track with a single key is fine and produces
	 * one constant key, which is how an author pins a value for the whole animation.
	 */
	bool SampleTimelineTrack(const FDreamUITimelineTrack& InTrack, const TArray<TArray<double>>& InKeyChannels,
		int32 InChannelCount, TArray<FTimelineSample>& OutSamples, FString& OutBadEaseName)
	{
		const double SecondsPerSample = 1.0 / static_cast<double>(TimelineDisplayRate);
		for (int32 KeyIndex = 0; KeyIndex < InTrack.Keys.Num(); ++KeyIndex)
		{
			const FDreamUITimelineKey& Key = InTrack.Keys[KeyIndex];
			FTimelineSample Start;
			Start.Frame = TimelineTimeToFrame(Key.Time);
			for (int32 Channel = 0; Channel < InChannelCount; ++Channel)
			{
				Start.Channels[Channel] = InKeyChannels[KeyIndex][Channel];
			}
			OutSamples.Add(Start);

			if (KeyIndex + 1 >= InTrack.Keys.Num())
			{
				break;
			}
			const FDreamUITimelineKey& NextKey = InTrack.Keys[KeyIndex + 1];
			const double Span = NextKey.Time - Key.Time;
			if (Span <= 0.0 || Key.EaseName.IsEmpty() || Key.EaseName.Equals(TEXT("Linear")))
			{
				// Linear needs no samples between: two keys and a straight line is exactly the curve.
				continue;
			}

			EDreamTweenEase Ease = EDreamTweenEase::Linear;
			if (!FindEaseType(Key.EaseName, Ease))
			{
				OutBadEaseName = Key.EaseName;
				return false;
			}
			const FDreamTweenFunction Curve = UDreamTweener::GetEaseFunction(Ease);
			if (!Curve.IsBound())
			{
				OutBadEaseName = Key.EaseName;
				return false;
			}

			const int32 SampleCount = FMath::Clamp(FMath::CeilToInt(Span / SecondsPerSample) - 1, 0, 600);
			for (int32 Sample = 1; Sample <= SampleCount; ++Sample)
			{
				const double Alpha = static_cast<double>(Sample) / static_cast<double>(SampleCount + 1);
				// c = 1, b = 0, d = 1: the curve library's own normalised 0..1 shape, so `ease
				// OutBounce` here and a DreamTween OutBounce are the same motion by construction.
				const double Eased = static_cast<double>(Curve.Execute(1.0f, 0.0f, static_cast<float>(Alpha), 1.0f));
				FTimelineSample Between;
				Between.Frame = TimelineTimeToFrame(Key.Time + Span * Alpha);
				for (int32 Channel = 0; Channel < InChannelCount; ++Channel)
				{
					const double From = InKeyChannels[KeyIndex][Channel];
					const double To = InKeyChannels[KeyIndex + 1][Channel];
					Between.Channels[Channel] = From + (To - From) * Eased;
				}
				OutSamples.Add(Between);
			}
		}
		return true;
	}

	/** Write one channel's samples into a float or double MovieScene channel. */
	void FillTimelineFloatChannel(FMovieSceneFloatChannel& OutChannel, const TArray<FTimelineSample>& InSamples, int32 InChannel)
	{
		for (const FTimelineSample& Sample : InSamples)
		{
			OutChannel.AddLinearKey(Sample.Frame, static_cast<float>(Sample.Channels[InChannel]));
		}
	}

	void FillTimelineDoubleChannel(FMovieSceneDoubleChannel& OutChannel, const TArray<FTimelineSample>& InSamples, int32 InChannel)
	{
		for (const FTimelineSample& Sample : InSamples)
		{
			OutChannel.AddLinearKey(Sample.Frame, Sample.Channels[InChannel]);
		}
	}

	/** One property track line, built onto InAnimation. Reports and returns false on refusal. */
	bool BuildTimelineTrack(const FDreamUITimeline& InTimeline, const FDreamUITimelineTrack& InTrack,
		UDreamWidgetAnimation* InAnimation, UDreamWidget* InHost, FBuildContext& InContext)
	{
		UDreamWidget* Target = FindWidgetByNodePath(InHost, InTrack.NodePath);
		if (!IsValid(Target))
		{
			AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::TimelineTargetNotFound, InTrack.Location,
				FString::Printf(TEXT("timeline '%s' animates '%s', and this file declares no node on that path"),
					*InTimeline.Name, *InTrack.NodePath));
			return false;
		}

		FResolvedTimelineTarget Resolved;
		if (!ResolveTimelineTarget(Target, InTrack.PropertyName, Resolved))
		{
			AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::TimelinePropertyNotAnimatable, InTrack.Location,
				FString::Printf(TEXT("'%s' has no property '%s' on itself, its visual or any of its behaviours -- a track names what the animation editor shows, which is a property's own name or the label on its row"),
					*Target->GetDisplayName(), *InTrack.PropertyName));
			return false;
		}
		if (!Resolved.HeadProperty->HasAnyPropertyFlags(CPF_Interp))
		{
			// Sequencer's own rule, and the reason it is the rule here: a property track drives its
			// target through the Interp machinery, so a property nobody marked Interp is one the
			// animation editor does not offer either. Refusing with the reason beats compiling a
			// track that exists and never writes.
			AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::TimelinePropertyNotAnimatable, InTrack.Location,
				FString::Printf(TEXT("'%s' is not marked Interp, so no animation track can drive it -- the animation editor does not offer it either"),
					*InTrack.PropertyName));
			return false;
		}

		bool bIsColor = false;
		bool bIsFloatChannel = false;
		const int32 ChannelCount = GetTimelineChannelCount(Resolved.LeafProperty, bIsColor, bIsFloatChannel);
		if (ChannelCount == 0)
		{
			AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::TimelinePropertyNotAnimatable, InTrack.Location,
				FString::Printf(TEXT("'%s' is a %s, and a timeline drives numbers, 2-, 3- and 4-component vectors and colours -- anything else belongs in an 'external' timeline"),
					*InTrack.PropertyName, *Resolved.LeafProperty->GetCPPType()));
			return false;
		}

		TArray<TArray<double>> KeyChannels;
		KeyChannels.Reserve(InTrack.Keys.Num());
		for (const FDreamUITimelineKey& Key : InTrack.Keys)
		{
			double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
			if (!ReadTimelineChannels(Resolved.LeafProperty, Key.Value, ChannelCount, bIsColor, Channels))
			{
				AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::ValueTypeMismatch, Key.Location,
					FString::Printf(TEXT("'%s' cannot be read as a value for '%s'"), *Key.Value.Raw, *InTrack.PropertyName));
				return false;
			}
			KeyChannels.Add(TArray<double>({ Channels[0], Channels[1], Channels[2], Channels[3] }));
		}

		TArray<FTimelineSample> Samples;
		FString BadEase;
		if (!SampleTimelineTrack(InTrack, KeyChannels, ChannelCount, Samples, BadEase))
		{
			AddErrorIn(*InContext.Diagnostics, InTimeline.SourceName, EDreamUIDiagnosticCode::UnknownEaseName, InTrack.Location,
				FString::Printf(TEXT("'%s' is not a curve name -- the set is EDreamTweenEase's, minus CurveFloat"), *BadEase));
			return false;
		}
		if (Samples.Num() == 0)
		{
			return true;
		}

		UMovieScene* MovieScene = InAnimation->GetMovieScene();
		// One possessable per bound OBJECT, reused across track lines: two tracks on one widget are
		// two rows under one binding in Sequencer, which is what an author expects to see. A property
		// that lives on the widget's VISUAL or on a behaviour is a different object and therefore a
		// different binding -- the name says which, because otherwise two rows read "Icon" and only
		// their contents tell them apart.
		const FString BindingName = Resolved.Object == Target
			? Target->GetDisplayName()
			: FString::Printf(TEXT("%s.%s"), *Target->GetDisplayName(), *Resolved.Object->GetClass()->GetName());
		FGuid Binding;
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
			// A possessable's class is editor-only data, so a game build goes by the name alone -- which
			// already says which object it is, since a visual's or a behaviour's binding carries its class.
			if (Possessable.GetName() == BindingName
#if WITH_EDITORONLY_DATA
				&& Possessable.GetPossessedObjectClass() == Resolved.Object->GetClass()
#endif
				)
			{
				Binding = Possessable.GetGuid();
				break;
			}
		}
		if (!Binding.IsValid())
		{
			Binding = MovieScene->AddPossessable(BindingName, Resolved.Object->GetClass());
			// Against the HOST, which is what the runtime resolves from -- see BindPossessableObject.
			InAnimation->BindPossessableObject(Binding, *Resolved.Object, InHost);
		}

		const TRange<FFrameNumber> SectionRange(FFrameNumber(0), TimelineTimeToFrame(InTimeline.Duration) + 1);
		if (bIsColor)
		{
			UMovieSceneColorTrack* Track = MovieScene->AddTrack<UMovieSceneColorTrack>(Binding);
			Track->SetPropertyNameAndPath(FName(*Resolved.LeafProperty->GetName()), Resolved.PropertyPath);
			UMovieSceneColorSection* Section = CastChecked<UMovieSceneColorSection>(Track->CreateNewSection());
			Section->SetRange(SectionRange);
			FillTimelineFloatChannel(Section->GetRedChannel(), Samples, 0);
			FillTimelineFloatChannel(Section->GetGreenChannel(), Samples, 1);
			FillTimelineFloatChannel(Section->GetBlueChannel(), Samples, 2);
			FillTimelineFloatChannel(Section->GetAlphaChannel(), Samples, 3);
			Track->AddSection(*Section);
			return true;
		}
		if (ChannelCount == 1 && bIsFloatChannel)
		{
			UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(Binding);
			Track->SetPropertyNameAndPath(FName(*Resolved.LeafProperty->GetName()), Resolved.PropertyPath);
			UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
			Section->SetRange(SectionRange);
			TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			if (Channels.Num() > 0)
			{
				FillTimelineFloatChannel(*Channels[0], Samples, 0);
			}
			Track->AddSection(*Section);
			return true;
		}
		if (ChannelCount == 1)
		{
			UMovieSceneDoubleTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleTrack>(Binding);
			Track->SetPropertyNameAndPath(FName(*Resolved.LeafProperty->GetName()), Resolved.PropertyPath);
			UMovieSceneDoubleSection* Section = CastChecked<UMovieSceneDoubleSection>(Track->CreateNewSection());
			Section->SetRange(SectionRange);
			TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			if (Channels.Num() > 0)
			{
				FillTimelineDoubleChannel(*Channels[0], Samples, 0);
			}
			Track->AddSection(*Section);
			return true;
		}

		UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(Binding);
		Track->SetPropertyNameAndPath(FName(*Resolved.LeafProperty->GetName()), Resolved.PropertyPath);
		Track->SetNumChannelsUsed(ChannelCount);
		UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
		Section->SetChannelsUsed(ChannelCount);
		Section->SetRange(SectionRange);
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		for (int32 Index = 0; Index < ChannelCount && Index < Channels.Num(); ++Index)
		{
			FillTimelineDoubleChannel(*Channels[Index], Samples, Index);
		}
		Track->AddSection(*Section);
		return true;
	}

	/** The block's `@time -> Name` lines as one unbound event row. */
	void BuildTimelineEventTrack(const FDreamUITimeline& InTimeline, const FDreamUITimelineTrack& InTrack,
		UDreamWidgetAnimation* InAnimation)
	{
		UMovieScene* MovieScene = InAnimation->GetMovieScene();
		UDreamUIAnimEventTrack* Track = MovieScene->AddTrack<UDreamUIAnimEventTrack>();
		UDreamUIAnimEventSection* Section = CastChecked<UDreamUIAnimEventSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), TimelineTimeToFrame(InTimeline.Duration) + 1));
		for (const FDreamUITimelineKey& Key : InTrack.Keys)
		{
			Section->EventChannel.GetData().AddKey(TimelineTimeToFrame(Key.Time), Key.EventName);
		}
		Track->AddSection(*Section);
	}

	/**
	 * Every `timeline` block in the file, onto the root's animation component.
	 *
	 * The root, because that is where the animations already live in practice and what the compiler's
	 * carry, the class-variable pass and the runtime's playback entry all address. `external` blocks
	 * build nothing at all -- their whole content is the statement that they exist, which the
	 * compiler reads off the AST when it decides what to carry.
	 */
	void BuildTimelines(FBuildContext& InContext)
	{
		if (InContext.Ast == nullptr || InContext.Ast->Timelines.Num() == 0)
		{
			return;
		}
		UDreamWidget* Host = InContext.Tree != nullptr ? InContext.Tree->RootWidget.Get() : nullptr;
		if (!IsValid(Host))
		{
			return;
		}

		UDreamWidgetAnimationComponent* Animator = nullptr;
		for (const FDreamUITimeline& Timeline : InContext.Ast->Timelines)
		{
			if (Timeline.bExternal)
			{
				continue;
			}
			if (Animator == nullptr)
			{
				Animator = Host->GetComponent<UDreamWidgetAnimationComponent>();
				if (Animator == nullptr)
				{
					Animator = Host->AddComponent<UDreamWidgetAnimationComponent>();
				}
			}
			if (Animator == nullptr)
			{
				return;
			}

			UDreamWidgetAnimation* Animation = Animator->AddNewAnimation();
			if (!IsValid(Animation))
			{
				continue;
			}
			Animation->SetDisplayNameString(Timeline.Name);
			Animation->SetLanguageOwned(true);

			// The longest key wins when the block declares no duration, so a timeline is never
			// shorter than the motion written into it -- an author who wrote keys past `duration`
			// meant the keys.
			double Duration = Timeline.Duration;
			for (const FDreamUITimelineTrack& Track : Timeline.Tracks)
			{
				for (const FDreamUITimelineKey& Key : Track.Keys)
				{
					Duration = FMath::Max(Duration, Key.Time);
				}
			}

			UMovieScene* MovieScene = Animation->GetMovieScene();
			MovieScene->SetTickResolutionDirectly(FFrameRate(TimelineTickResolution, 1));
			MovieScene->SetDisplayRate(FFrameRate(TimelineDisplayRate, 1));
			MovieScene->SetPlaybackRange(FFrameNumber(0), TimelineTimeToFrame(Duration).Value + 1);

			FDreamUITimeline Resolved = Timeline;
			Resolved.Duration = Duration;
			for (const FDreamUITimelineTrack& Track : Timeline.Tracks)
			{
				if (Track.bIsEvent)
				{
					BuildTimelineEventTrack(Resolved, Track, Animation);
				}
				else
				{
					BuildTimelineTrack(Resolved, Track, Animation, Host, InContext);
				}
			}
		}
	}
}

UDreamWidgetTree* FDreamUITextBuilder::Build(const FDreamUIAst& InAst, UObject* InOuter,
	FDreamUIDiagnosticBag& OutDiagnostics, TArray<FDreamWidgetPropertyBinding>& OutBindings,
	TArray<FDreamWidgetEventBinding>* OutEventBindings, TArray<FDreamWidgetEachBinding>* OutEachBindings)
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
	Context.EventBindings = OutEventBindings;
	Context.EachBindings = OutEachBindings;
	// The namespace a translator sees. ClassPath makes it stable across a rename of the .dui file,
	// which the source name would not; the source name is the fallback for a file that has not been
	// given a class yet, which is every file in an editor preview before it is first compiled.
	Context.LocalizationNamespace = InAst.ClassPath.IsEmpty() ? OutDiagnostics.SourceName : InAst.ClassPath;
	Context.Tree = NewObject<UDreamWidgetTree>(InOuter != nullptr ? InOuter : (UObject*)GetTransientPackage());

	// Before the walk, so an alias that can never be written as a type is said once, at its own line, rather than
	// left to look like it worked: every node spelling it quietly builds the built-in instead.
	CheckAliasesAgainstBuiltIns(Context);

	Context.Tree->RootWidget = BuildNode(InAst.Root, nullptr, Context);
	if (!IsValid(Context.Tree->RootWidget))
	{
		return nullptr;
	}
	// TrySetParent already set every back-pointer on the way down. This is the belt to that braces:
	// the tree's invariant is that Parent is derivable from Children, and a builder that leaves it
	// almost-true hands the designer a tree whose GetParent() is null in one place nobody looks.
	Context.Tree->RebuildParentLinks();

	// Last, because it is the one pass that needs the finished tree: a property may name a node
	// declared below the line that points at it, and the walk above only ever knows what it has
	// already passed. After the parent links rather than before, so what it resolves against is a
	// tree with no half-set invariant left in it.
	ResolveNodeReferences(Context);

	// Last of all, because a track line's path is resolved against the FINISHED tree: a timeline may
	// animate a node declared below the block that names it, exactly as a node reference may.
	BuildTimelines(Context);

	return OutDiagnostics.NumErrors() > ErrorsBefore ? nullptr : Context.Tree;
}
