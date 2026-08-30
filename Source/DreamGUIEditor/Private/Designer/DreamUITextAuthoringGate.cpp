// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamUITextAuthoringGate.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintEditor.h"
#include "DreamWidgetPropertyBindingExtension.h"

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUIReflectionPolicy.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamUITextAuthoringGate"

namespace DreamUITextAuthoring
{
	namespace Local
	{
		/**
		 * The widget the panel's object hangs off: itself, or the widget that owns it.
		 *
		 * Every object the details panel can show under a designer is one of five things -- the widget,
		 * its visual, its panel slot, its layout container or self, or one of its behaviours -- and all
		 * but the first answer GetWidget(). Anything else is not part of a hierarchy and is not this
		 * namespace's business.
		 */
		const UDreamWidget* FindOwningWidget(const UObject* InOwner)
		{
			if (const UDreamWidget* AsWidget = Cast<UDreamWidget>(InOwner))
			{
				return AsWidget;
			}
			// Both bases, because they are siblings rather than one chain: UDreamVisual, UDreamPanelSlot
			// and the layouts derive from UDreamWidgetSubObjectBehaviour, while the things in
			// GetAllComponents() derive from UDreamUIBehaviour, and neither is the other.
			if (const UDreamWidgetSubObjectBehaviour* AsSubObject = Cast<UDreamWidgetSubObjectBehaviour>(InOwner))
			{
				return AsSubObject->GetWidget();
			}
			if (const UDreamUIBehaviour* AsBehaviour = Cast<UDreamUIBehaviour>(InOwner))
			{
				return AsBehaviour->GetWidget();
			}
			return nullptr;
		}

		/**
		 * The properties of a UDreamWidget the designer may still write under a `.dui`.
		 *
		 * ROOTS, not leaves: a chain is matched at its outermost link, so naming AnchorData here covers
		 * AnchorData.SizeDelta and AnchorData.AnchorMin.X without listing them, and the language writes
		 * exactly those dotted paths.
		 *
		 * An ALLOWLIST rather than a list of what is refused, and the direction is the whole point. A
		 * blocklist fails open: a property added to UDreamWidget next month would be editable, be
		 * mirrored onto the template, look right, and be gone at the next compile -- which is the exact
		 * failure this gate exists to remove. An allowlist fails closed, and the cost of missing one is
		 * a row that is greyed until somebody adds a name here.
		 *
		 * NO TABLE ANY MORE. The list this function used to keep was the panel-side twin of the
		 * write-back's path lists, and the pair had the failure both halves of a copied rule always
		 * have: rotation was edited, mirrored, and dropped, because one list knew it and the other
		 * did not. Both now ask DreamUIReflection -- the same (Edit-or-setter, not hidden, writable)
		 * policy, one body -- so a row is grey exactly when the sweep would not write it. Widening
		 * the set is done at the property: tag or untag it, and both halves move together.
		 *
		 * The QUAT is a deliberate oddity worth naming: it is DuiHidden (the euler is its authored
		 * face), which greys a hypothetical plain quat row -- but the transform section is a custom
		 * row gated by category, and its edits arrive as quat chains the MIRROR accepts. That is the
		 * one place the panel writes a property the file does not spell, and it is correct: the
		 * mirror syncs the euler, and the euler is what the sweep prints.
		 */
		bool IsWritableWidgetPropertyRoot(const FName InRootName)
		{
			const FProperty* Root = UDreamWidget::StaticClass()->FindPropertyByName(InRootName);
			return DreamUIReflection::IsSweepRoot(Root);
		}

		/**
		 * The name the writable set is matched on: the outermost link of the details panel's chain.
		 *
		 * ParentProperties[0] is the immediate parent and the last entry is the property declared on the
		 * object, so Last() is the one a .dui line would name. An empty chain means the leaf IS that
		 * property.
		 */
		FName GetChainRootName(const FProperty* InLeafProperty, TConstArrayView<const FProperty*> InParentProperties)
		{
			for (int32 Index = InParentProperties.Num() - 1; Index >= 0; Index--)
			{
				if (InParentProperties[Index] != nullptr)
				{
					return InParentProperties[Index]->GetFName();
				}
			}
			return InLeafProperty != nullptr ? InLeafProperty->GetFName() : NAME_None;
		}

		/**
		 * Whether the `.dui` can address this object at all, which is a question about the OBJECT and
		 * not about any property on it.
		 *
		 * The language has exactly three notations and they name exactly these things: a bare
		 * `Name = Value` reaches the node or its visual, `@slot Name = Value` reaches its panel slot,
		 * and a `+ Class { }` block reaches a behaviour, a layout container or a layout self -- all
		 * three of which BuildComponents creates from the same `+` line and the patcher addresses by
		 * the same component index. Derived from the grammar rather than listed by hand, because a
		 * hand-kept list is how the gate and the language start disagreeing.
		 */
		bool IsObjectAddressableInText(const UObject* InOwner, const UDreamWidget* InWidget)
		{
			if (!IsValid(InWidget) || InOwner == nullptr)
			{
				return false;
			}
			if (InOwner == InWidget->GetVisual()
				|| InOwner == InWidget->GetPanelSlot()
				|| InOwner == InWidget->GetLayoutContainer()
				|| InOwner == InWidget->GetLayoutSelf())
			{
				return true;
			}
			// By identity in the widget's own component list, not by class: a behaviour reached some
			// other way -- one that belongs to a different widget, or one the panel is showing because
			// a property points at it -- has no `+` line on THIS node for a value to be written into.
			if (const UDreamUIBehaviour* AsBehaviour = Cast<UDreamUIBehaviour>(InOwner))
			{
				return InWidget->GetAllComponents().Contains(AsBehaviour);
			}
			return false;
		}

		/**
		 * The class default object the criterion is read off, or null when there is none to read.
		 *
		 * The generated class first and the parent second, mirroring the compiler exactly. See the
		 * header: the parent answers the two moments the generated class has no CDO, and a gate that
		 * disagreed with the compiler about which one wins would let an edit through that the compile
		 * then throws away.
		 */
		const UDreamTextUserWidget* FindTextDefaults(const UDreamWidgetBlueprint* InBlueprint)
		{
			if (!IsValid(InBlueprint))
			{
				return nullptr;
			}
			// bCreateIfNeeded false, on both. This is reached from a Slate attribute that is evaluated
			// every tick for every visible row; building a class default object as a side effect of
			// drawing a panel is not a cost, it is a defect.
			if (InBlueprint->GeneratedClass != nullptr)
			{
				if (const UDreamTextUserWidget* Generated =
					Cast<UDreamTextUserWidget>(InBlueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false)))
				{
					return Generated;
				}
			}
			if (InBlueprint->ParentClass != nullptr)
			{
				return Cast<UDreamTextUserWidget>(InBlueprint->ParentClass->GetDefaultObject(/*bCreateIfNeeded*/false));
			}
			return nullptr;
		}

		/**
		 * One notification per gesture, not one per widget.
		 *
		 * Deleting a multi-selection calls the refusal once per widget, and five toasts stacked up for
		 * one Delete keypress is worse than the silence it replaces -- the author dismisses them
		 * without reading, which is the same outcome with more clicking. Refusals arriving close
		 * together fold into the first one with a count.
		 *
		 * Folded on TIME rather than on the message, because the messages differ: each names its own
		 * widget. What the author needs to know is the one thing all of them say.
		 */
		void NotifyRefusal(const FText& InMessage, const UDreamWidgetBlueprint* InBlueprint)
		{
			// Notifications are Slate. A commandlet, a cook or an automation run without a UI reaches
			// here through the same primitives and must not try to draw one.
			if (!FSlateApplication::IsInitialized())
			{
				return;
			}

			static TWeakPtr<SNotificationItem> WeakItem;
			static double LastTime = 0.0;
			static int32 FoldedCount = 0;
			static FText FirstMessage;

			constexpr double FoldWindowSeconds = 2.0;
			const double Now = FPlatformTime::Seconds();
			const TSharedPtr<SNotificationItem> Existing = WeakItem.Pin();

			if (Existing.IsValid() && (Now - LastTime) < FoldWindowSeconds)
			{
				LastTime = Now;
				++FoldedCount;
				Existing->SetText(FText::Format(
					LOCTEXT("StructuralRefusalFolded", "{0} ({1} more refused)"),
					FirstMessage, FText::AsNumber(FoldedCount)));
				return;
			}

			LastTime = Now;
			FoldedCount = 0;
			FirstMessage = InMessage;

			FNotificationInfo Info(InMessage);
			Info.ExpireDuration = 8.0f;
			Info.bFireAndForget = true;

			// The file, because the message just told them structure lives in it. A refusal that names
			// a file and gives no way to reach it is a message that ends in homework.
			const FString Resolved = UDreamTextUserWidget::ResolveDuiFilePath(GetAuthoredSourcePath(InBlueprint));
			if (!Resolved.IsEmpty() && FPaths::FileExists(Resolved))
			{
				Info.Hyperlink = FSimpleDelegate::CreateLambda([Resolved]
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*Resolved, nullptr, ELaunchVerb::Open);
				});
				Info.HyperlinkText = LOCTEXT("StructuralRefusalOpen", "Open the file");
			}

			const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
			if (Item.IsValid())
			{
				Item->SetCompletionState(SNotificationItem::CS_Fail);
			}
			WeakItem = Item;
		}

		/**
		 * "Is there a path here", without building the trimmed copy TrimStartAndEnd would return.
		 *
		 * The same rule as the compiler's trim -- a path of nothing but spaces is a path nobody typed
		 * on purpose -- asked in a way that allocates nothing, because the question is asked once per
		 * property row per tick and the answer is thrown away every time.
		 */
		bool HasNonWhitespace(const FString& InText)
		{
			for (const TCHAR Character : InText)
			{
				if (!FChar::IsWhitespace(Character))
				{
					return true;
				}
			}
			return false;
		}

		/** The installed spelling judge. Empty until whoever owns the write-back hands one over. */
		FLiteralSpellingProbe& GetLiteralSpellingProbe()
		{
			static FLiteralSpellingProbe Probe;
			return Probe;
		}

		/**
		 * Where the live bytes of this property are, walking the details panel's chain from the owner.
		 *
		 * Needed because the probe judges a VALUE and not only a type -- an enum holding a number that
		 * is not one of its entries has no identifier to write, and only the bytes know that.
		 *
		 * Returns null rather than guessing when a link in the chain is not a struct. ContainerPtrToValuePtr
		 * is only meaningful through structs; through an array or a map the same arithmetic produces a
		 * plausible pointer into the wrong memory, and handing that to a printer is a crash with no
		 * stack anyone can read. A null here means "do not ask the probe", which fails open.
		 */
		const void* ResolveValuePtr(const UObject* InOwner, const FProperty* InLeafProperty,
			TConstArrayView<const FProperty*> InParentProperties)
		{
			if (InOwner == nullptr || InLeafProperty == nullptr)
			{
				return nullptr;
			}
			// The outermost link has to be declared on this object's class, and this is not paranoia
			// about the details panel -- it is about every other caller. ContainerPtrToValuePtr is
			// unchecked pointer arithmetic: hand it a property from an unrelated class and it returns
			// a plausible pointer into the middle of something else, which a printer then reads. A
			// wrong answer from the gate costs one greyed row; a wild read costs a crash with no stack.
			const FProperty* Root = InParentProperties.Num() > 0 ? InParentProperties.Last() : InLeafProperty;
			const UClass* DeclaringClass = Root != nullptr ? Cast<UClass>(Root->GetOwnerStruct()) : nullptr;
			if (DeclaringClass == nullptr || !InOwner->GetClass()->IsChildOf(DeclaringClass))
			{
				return nullptr;
			}
			// Outermost first: ParentProperties[0] is the immediate parent, so the walk runs backwards.
			const void* Container = static_cast<const void*>(InOwner);
			for (int32 Index = InParentProperties.Num() - 1; Index >= 0; Index--)
			{
				const FStructProperty* AsStruct = CastField<FStructProperty>(InParentProperties[Index]);
				if (AsStruct == nullptr)
				{
					return nullptr;
				}
				Container = AsStruct->ContainerPtrToValuePtr<void>(Container);
			}
			return InLeafProperty->ContainerPtrToValuePtr<void>(Container);
		}

		/** Categories whose custom rows stay live on a text-authored widget. See IsCustomRowReadOnly. */
		bool IsWritableWidgetCategory(const FName InCategoryName)
		{
			static const FName WritableCategories[] =
			{
				// The anchor block and the transform fields -- the same values the viewport handles write.
				FName(TEXT("DreamLayout")),
				// The panel slot's own parameters, which `@slot` lines carry.
				FName(TEXT("DreamSlot")),
				// The design canvas. Not part of the hierarchy at all: it is designer state on the asset,
				// so the text has nothing to say about it and greying it out would be a bug of our own.
				FName(TEXT("DreamCanvasSize")),
			};
			for (const FName Writable : WritableCategories)
			{
				if (Writable == InCategoryName)
				{
					return true;
				}
			}
			return false;
		}
	}

	FString GetAuthoredSourcePath(const UDreamWidgetBlueprint* InBlueprint)
	{
		const UDreamTextUserWidget* Defaults = Local::FindTextDefaults(InBlueprint);
		if (Defaults == nullptr)
		{
			return FString();
		}
		// Trimmed, matching the compiler: a path of nothing but spaces is a path nobody typed on
		// purpose, and treating it as authored would lock a designer over a stray keystroke.
		return Defaults->SourceFile.FilePath.TrimStartAndEnd();
	}

	FString GetAuthoredSourceFileName(const UDreamWidgetBlueprint* InBlueprint)
	{
		const FString Path = GetAuthoredSourcePath(InBlueprint);
		return Path.IsEmpty() ? FString() : FPaths::GetCleanFilename(Path);
	}

	bool IsTextAuthored(const UDreamWidgetBlueprint* InBlueprint)
	{
		// Not `!GetAuthoredSourcePath(...).IsEmpty()`, which is the obvious spelling: this is the hot
		// one -- every property row asks it every tick -- and that spelling allocates a trimmed copy of
		// the path to throw away each time.
		const UDreamTextUserWidget* Defaults = Local::FindTextDefaults(InBlueprint);
		return Defaults != nullptr && Local::HasNonWhitespace(Defaults->SourceFile.FilePath);
	}

	bool CanAuthorFromText(const UDreamWidgetBlueprint* InBlueprint)
	{
		return Local::FindTextDefaults(InBlueprint) != nullptr;
	}

	bool SetAuthoredSourcePath(UDreamWidgetBlueprint* InBlueprint, const FString& InPath)
	{
		if (!IsValid(InBlueprint))
		{
			return false;
		}
		// bCreateIfNeeded TRUE here, unlike every read in this file: a read happens once per row per
		// tick and must not build a CDO as a side effect of drawing, but a write happens when someone
		// clicks, and a Blueprint whose generated class has no CDO yet is exactly the case that needs
		// one made rather than skipped.
		UDreamTextUserWidget* Defaults = InBlueprint->GeneratedClass != nullptr
			? Cast<UDreamTextUserWidget>(InBlueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/true))
			: nullptr;
		if (Defaults == nullptr)
		{
			// The parent's CDO is what the READ falls back to, and it must not be what the write
			// falls back to: writing there would set the source file of every Blueprint deriving from
			// that parent, this one included, and the property would look like it took.
			return false;
		}

		const FString Portable = DreamUIPaths::MakePortablePath(InPath.TrimStartAndEnd());
		if (Defaults->SourceFile.FilePath.Equals(Portable, ESearchCase::CaseSensitive))
		{
			// Already there. Returning true rather than false: the caller asked for a state, and the
			// state is what it asked for. Recompiling anyway would make picking the same file twice a
			// way to rebuild the tree, which is a coincidence rather than a feature.
			return true;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetAuthoredSource", "Set DreamUI Source File"));
		Defaults->SetFlags(RF_Transactional);
		Defaults->Modify();
		InBlueprint->Modify();
		Defaults->SourceFile.FilePath = Portable;

		// Structurally, not merely modified: the hierarchy this class declares is about to be a
		// different one, so the skeleton has to regenerate along with it -- a member variable per
		// widget, and the old file's widgets gone.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(InBlueprint);
		// SkipGarbageCollection, matching every other compile in this plugin. Nothing here needs a
		// collection, and running one turns an unrelated leak somewhere else in the process into an
		// error attributed to whoever happened to call this.
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		return true;
	}

	UDreamWidgetBlueprint* FindOwningBlueprint(const UDreamWidget* InWidget)
	{
		if (!IsValid(InWidget))
		{
			return nullptr;
		}
		// The template half: outered to the tree, which is outered to the asset.
		if (UDreamWidgetBlueprint* Outered = InWidget->GetTypedOuter<UDreamWidgetBlueprint>())
		{
			return Outered;
		}
		// The preview half: no asset anywhere above it, so it is found by the world it lives in. This
		// is the branch that matters -- the details panel and every viewport gesture hold previews.
		if (const FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(InWidget))
		{
			return Designer->GetWidgetBlueprint();
		}
		return nullptr;
	}

	FText DescribeStructuralRefusal(const UDreamWidgetBlueprint* InBlueprint, const FString& InOperation)
	{
		return FText::Format(
			LOCTEXT("StructuralRefusal",
				"Refusing to {0} in '{1}': its hierarchy is authored in '{2}'. Structure lives in the text -- edit the file to change it; the designer edits values."),
			FText::FromString(InOperation),
			FText::FromString(GetNameSafe(InBlueprint)),
			FText::FromString(GetAuthoredSourceFileName(InBlueprint)));
	}

	bool RefuseStructuralEdit(const UDreamWidgetBlueprint* InBlueprint,
		const TCHAR* InFunction, const int32 InLine, const FString& InOperation)
	{
		if (!IsTextAuthored(InBlueprint))
		{
			return false;
		}
		const FText Message = DescribeStructuralRefusal(InBlueprint, InOperation);
		// The file name, never just "this asset comes from text". The author's next action is to open a
		// file, and a message that does not name one is a message they have to go and research.
		//
		// One category for all six call sites even though the family they interrupt logs to two, and
		// one sentence built here rather than at each site: six refusals written six times is six
		// chances for one of them to be worded so it cannot be told from an ordinary failure.
		UE_LOG(DreamGUIEditor, Error, TEXT("[%s].%d %s"), InFunction, InLine, *Message.ToString());
		// And where the author is looking, not only in the Output Log. This class comment has said
		// since it was written that a refusal has to be visible BEFORE the edit -- and it was, for the
		// two call sites that had somewhere to put an FText. The five primitives had nowhere, so
		// dragging from the palette, pressing Delete, or renaming in the hierarchy did nothing at all
		// and said nothing at all. That is the failure this whole gate exists to prevent, reproduced
		// by the gate.
		Local::NotifyRefusal(Message, InBlueprint);
		return true;
	}

	bool IsPropertyWrittenAsABinding(const UObject* InOwner, const FProperty* InProperty)
	{
		if (!IsValid(InOwner) || InProperty == nullptr)
		{
			return false;
		}
		const UDreamWidget* Widget = Local::FindOwningWidget(InOwner);
		UDreamWidgetBlueprint* Blueprint = FindOwningBlueprint(Widget);
		if (!IsValid(Blueprint))
		{
			return false;
		}
		// const_cast because ResolveBindingSite takes a mutable pointer and reads it; it resolves which
		// widget and which of its objects this is, and writes nothing.
		const DreamWidgetPropertyBindingExtension::FBindingSite Site =
			DreamWidgetPropertyBindingExtension::ResolveBindingSite(const_cast<UObject*>(InOwner));
		if (!Site.IsValid())
		{
			// A panel slot or a layout container: EDreamWidgetBindingTarget cannot name one, so no
			// binding can exist against it and there is nothing here to protect.
			return false;
		}
		return DreamWidgetPropertyBindingExtension::FindBinding(Blueprint, Site, InProperty->GetFName()) != nullptr;
	}

	bool IsExpandedFromLoop(const UDreamWidget* InWidget)
	{
		// False for every widget in every tree, today, and the header says why at length: the builder
		// refuses a loop body outright (DUI5007), so nothing in a tree ever came from one. Kept as a
		// call rather than deleted because the gate's call sites are the part that is expensive to
		// rediscover, and the answer changes in exactly one place when expansion lands.
		(void)InWidget;
		return false;
	}

	EPropertyEditVerdict GetPropertyEditVerdict(const UObject* InOwner, const FProperty* InLeafProperty,
		TConstArrayView<const FProperty*> InParentProperties)
	{
		if (!IsValid(InOwner) || InLeafProperty == nullptr)
		{
			return EPropertyEditVerdict::NotTextAuthored;
		}
		const UDreamWidget* Widget = Local::FindOwningWidget(InOwner);
		if (!IsValid(Widget) || !IsTextAuthored(FindOwningBlueprint(Widget)))
		{
			// Fails OPEN, and deliberately so -- see the header. Everything not part of a text-authored
			// hierarchy keeps behaving exactly as it did before this file existed.
			return EPropertyEditVerdict::NotTextAuthored;
		}

		// Bindings are tested BEFORE the writable set, because a bound property is usually inside it: a
		// `Text <- GetTitle()` on a text visual is a property this gate would otherwise wave through,
		// and one drag would then replace authored behaviour with the value it happened to be showing.
		// The patcher refuses that write anyway (DUI7001), which is the wrong place to find out.
		if (IsPropertyWrittenAsABinding(InOwner, InLeafProperty))
		{
			return EPropertyEditVerdict::WrittenAsABinding;
		}
		if (IsExpandedFromLoop(Widget))
		{
			return EPropertyEditVerdict::ExpandedFromALoop;
		}

		const bool bInWritableSet = InOwner == Widget
			? Local::IsWritableWidgetPropertyRoot(Local::GetChainRootName(InLeafProperty, InParentProperties))
			// Every other object the panel can be showing is one the language addresses as a whole --
			// the visual, the panel slot, the layouts, a behaviour -- so every property on it has a
			// line it can be written into and none of them needs an allowlist of its own.
			: Local::IsObjectAddressableInText(InOwner, Widget);
		if (!bInWritableSet)
		{
			return EPropertyEditVerdict::OutsideTheWritableSet;
		}

		// Last, because it is the narrowest question: this property WOULD be written, if only its
		// value could be spelled. The font on a text visual is the case -- FontSize is a number the
		// language writes and the Font next to it is an object reference it does not, and without this
		// they look identical in the panel.
		const FLiteralSpellingProbe& Probe = Local::GetLiteralSpellingProbe();
		if (Probe)
		{
			if (const void* ValuePtr = Local::ResolveValuePtr(InOwner, InLeafProperty, InParentProperties))
			{
				if (!Probe(InLeafProperty, ValuePtr))
				{
					return EPropertyEditVerdict::HasNoTextSpelling;
				}
			}
		}
		return EPropertyEditVerdict::Writable;
	}

	void SetLiteralSpellingProbe(FLiteralSpellingProbe InProbe)
	{
		Local::GetLiteralSpellingProbe() = MoveTemp(InProbe);
	}

	bool HasLiteralSpellingProbe()
	{
		return static_cast<bool>(Local::GetLiteralSpellingProbe());
	}

	bool IsPropertyReadOnly(const UObject* InOwner, const FProperty* InLeafProperty,
		TConstArrayView<const FProperty*> InParentProperties)
	{
		const EPropertyEditVerdict Verdict = GetPropertyEditVerdict(InOwner, InLeafProperty, InParentProperties);
		return Verdict != EPropertyEditVerdict::Writable && Verdict != EPropertyEditVerdict::NotTextAuthored;
	}

	FText DescribePropertyVerdict(const EPropertyEditVerdict InVerdict, const FString& InSourceFileName)
	{
		const FText FileName = FText::FromString(InSourceFileName);
		switch (InVerdict)
		{
		case EPropertyEditVerdict::WrittenAsABinding:
			return FText::Format(LOCTEXT("ReadOnly_Binding",
				"'{0}' drives this from a function. Editing it here would replace the binding with the value it is currently showing."), FileName);
		case EPropertyEditVerdict::ExpandedFromALoop:
			return FText::Format(LOCTEXT("ReadOnly_Loop",
				"One line of '{0}' produces every copy of this widget, so there is no per-copy value to write."), FileName);
		case EPropertyEditVerdict::OutsideTheWritableSet:
			return FText::Format(LOCTEXT("ReadOnly_NotWritable",
				"This value is written in '{0}'. The designer writes layout, slot and style values back to the file; the rest would be lost at the next compile."), FileName);
		case EPropertyEditVerdict::HasNoTextSpelling:
			return FText::Format(LOCTEXT("ReadOnly_NoSpelling",
				"'{0}' has no way to write this value down -- asset references and unsupported types are left alone rather than guessed at -- so a change here would be lost at the next compile."), FileName);
		default:
			return FText::GetEmpty();
		}
	}

	bool IsCustomRowReadOnly(const UObject* InOwner, const FName InRowName, const FName InCategoryName)
	{
		(void)InRowName;
		if (!IsValid(InOwner))
		{
			return false;
		}
		const UDreamWidget* Widget = Local::FindOwningWidget(InOwner);
		if (!IsValid(Widget) || !IsTextAuthored(FindOwningBlueprint(Widget)))
		{
			return false;
		}
		if (InOwner != Widget)
		{
			// A visual, a slot, a layout or a behaviour: addressable in full, so its rows are live.
			return !Local::IsObjectAddressableInText(InOwner, Widget);
		}
		// A custom row on the widget itself arrives with nothing but two names -- there is no property
		// node behind it to ask, which is why this is the category and not the property that decides.
		// Refusing by default is what makes the Visual / Panel / Self Layout placeholder rows -- which
		// are class pickers that CREATE those sub-objects, i.e. structure -- come out disabled.
		return !Local::IsWritableWidgetCategory(InCategoryName);
	}

	bool CanAuthorBindingsOn(const UObject* InOwner)
	{
		if (!IsValid(InOwner))
		{
			return true;
		}
		const UDreamWidget* Widget = Local::FindOwningWidget(InOwner);
		return !IsTextAuthored(FindOwningBlueprint(Widget));
	}
}

#undef LOCTEXT_NAMESPACE
