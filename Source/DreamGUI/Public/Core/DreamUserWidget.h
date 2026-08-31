// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamFieldNotification.h"
#include "INotifyFieldValueChanged.h"
#include "DreamUserWidget.generated.h"

class UDreamWidgetTree;
class UDreamUserWidget;

/** Blueprint-facing counterpart of CreateDreamWidget's before-alive hook. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIWidgetCreatedCallback, UDreamUserWidget*, CreatedWidget);

/**
 * A widget hierarchy that is a CLASS rather than an asset instance -- the counterpart to UMG's
 * UUserWidget, and the thing a DreamUI prefab is becoming.
 *
 * It is itself a UDreamWidget, exactly as UUserWidget is a UWidget. That is what makes nesting fall
 * out for free: one of these placed inside another hierarchy is just a widget in that hierarchy,
 * while its own contents come from its own class.
 *
 * Its contents live behind WidgetTree rather than directly in Children, and that indirection is not
 * decoration. Without it "the class of one widget" and "the class of a hierarchy" would be the same
 * thing, and a UDreamWidget subclass (they are Blueprintable) could not be told apart from a
 * hierarchy template. UMG buys the same separation with UWidgetTree, and it is not optional here
 * either.
 *
 * WidgetTree is Transient: a class template must never carry an expanded subtree, or a nested user
 * widget would be baked into its parent's template and stop tracking its own class. It is built at
 * Initialize, from the class, every time.
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable, DisplayName = "DreamUI User Widget")
class DREAMGUI_API UDreamUserWidget : public UDreamWidget, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	/**
	 * No native fields yet: everything notifiable on a user widget today is authored in the
	 * Blueprint, where the kismet compiler records it -- but only for classes that implement
	 * INotifyFieldValueChanged (KismetCompiler gates FieldNotifies on exactly that), which is why
	 * this interface lives on the base class rather than being opt-in per Blueprint.
	 */
	struct FFieldNotificationClassDescriptor : public ::UE::FieldNotification::IClassDescriptor
	{
		DREAMGUI_API virtual void ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const override;
	};

	//~ Begin INotifyFieldValueChanged Interface
	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate) override;
	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject) override;
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	virtual void BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId) override;
	//~ End INotifyFieldValueChanged Interface

	UFUNCTION(BlueprintCallable, Category = "FieldNotify", meta = (DisplayName = "Add Field Value Changed Delegate", ScriptName = "AddFieldValueChangedDelegate"))
	void K2_AddFieldValueChangedDelegate(FFieldNotificationId FieldId, FFieldValueChangedDynamicDelegate Delegate);

	UFUNCTION(BlueprintCallable, Category = "FieldNotify", meta = (DisplayName = "Remove Field Value Changed Delegate", ScriptName = "RemoveFieldValueChangedDelegate"))
	void K2_RemoveFieldValueChangedDelegate(FFieldNotificationId FieldId, FFieldValueChangedDynamicDelegate Delegate);
	/**
	 * This instance's own hierarchy, instanced from the class template. Transient and
	 * DuplicateTransient: it is regenerated from the class, never persisted and never copied.
	 */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UDreamWidgetTree> WidgetTree = nullptr;

	/**
	 * What the HOST authored for the slots this widget's class declares, keyed by slot name.
	 *
	 * The values are widgets of the host's tree, not of this class's -- that is the whole point.
	 * Nothing about them is a difference recorded against another asset: they are the host's own
	 * widgets, authored in the host's hierarchy, that happen to be placed through a hole the class
	 * opened. Instanced, so object instancing carries them across with the rest of the host's tree,
	 * and Initialize hangs them under the matching UDreamNamedSlot afterwards.
	 *
	 * Empty on the class template of a class that declares slots -- a slot with nothing in it is the
	 * normal state, and the class has no business filling its own holes.
	 */
	UPROPERTY(Instanced)
	TMap<FName, TObjectPtr<UDreamWidget>> NamedSlotContent;

	/**
	 * Instance the class's template into this widget, resolve the by-name widget bindings, and attach
	 * the resulting root beneath this widget.
	 *
	 * Idempotent, and a no-op on a class template (a CDO has no instance to build). Called for you by
	 * CreateDreamWidget; call it directly only when you constructed the object yourself.
	 */
	void Initialize();

	/**
	 * Build the contents from InArchetype rather than from this widget's own class.
	 *
	 * This is why InitializeWidgetStatic takes an archetype at all, and it is the designer's entry
	 * point. A designer preview has to show the hierarchy AS IT IS BEING AUTHORED, which is not what
	 * the class holds until the next compile -- so the preview is an instance of the class (for its
	 * logic, its bindings and its identity) whose contents are instanced from the Blueprint's
	 * authoring tree instead. UMG's preview does exactly this, for exactly this reason; see
	 * UUserWidget::DuplicateAndInitializeFromWidgetTree.
	 *
	 * Like Initialize, it runs once and does nothing on a class template.
	 */
	void InitializeFromArchetype(UDreamWidgetTree* InArchetype);

	/**
	 * The tree exists and every by-name widget binding points into it; nothing has read this widget's
	 * own data yet.
	 *
	 * This is the place to fill what the bindings and the `each` lists are about to read. There was no
	 * such place before, and the first frame is composed inside Initialize -- so a list source
	 * populated from a Blueprint Begin Play, or on the first tick, is populated after the list has
	 * already asked and been told there is nothing.
	 *
	 * Runs in the designer preview too, which instances the same way.
	 */
	virtual void NativeOnInitialized();

	/** The Blueprint half of NativeOnInitialized, called by it. Same contract, same moment. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Initialized"))
	void OnInitialized();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	UDreamWidgetTree* GetWidgetTree() const { return WidgetTree; }

	/** The root of this widget's own contents -- the tree's root, not this widget. Null before Initialize. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	UDreamWidget* GetContentRoot() const;

	/** The widget carrying the UDreamNamedSlot of that name inside this instance, or null. */
	UDreamWidget* FindSlotWidget(FName InSlotName) const;

	/** What the host put in InSlotName, or null. Reads the binding, not the attachment. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	UDreamWidget* GetContentForNamedSlot(FName InSlotName) const;

	/**
	 * Bind InContent to InSlotName. InContent must belong to the same tree as this widget -- a slot
	 * is filled by the host that placed this instance, never by a third asset.
	 *
	 * Returns false, loudly, when the slot is not declared or the content is not the caller's to
	 * give. Editor-facing: the designer's drop into a slot row is this call.
	 */
	bool SetContentForNamedSlot(FName InSlotName, UDreamWidget* InContent);

	/** Every slot name InTree declares, in tree order. Static because the compiler asks before any instance exists. */
	static void CollectDeclaredSlotNames(const UDreamWidgetTree* InTree, TArray<FName>& OutNames);

	/**
	 * Run ALL of this widget's property bindings once: call each bound function, hand the result to
	 * the widget's setter. Runs at Initialize (so the first frame shows bound values), and is safe
	 * to call by hand.
	 *
	 * Going through the setter is what makes the change take -- see FDreamWidgetPropertyBinding.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|UserWidget")
	void EvaluatePropertyBindings();

	/**
	 * Run only the POLLED bindings -- the ones whose source carries no FieldNotify entry and can
	 * therefore change without telling anyone. This is what the manager calls every frame; the
	 * subscribed bindings re-evaluate from their broadcast instead and cost nothing in between.
	 */
	void EvaluatePolledPropertyBindings();

	/** Whether this widget resolved any bindings at all. */
	bool HasPropertyBindings() const { return ResolvedBindings.Num() > 0; }

	/** Whether any binding still needs the per-frame poll; the manager only ticks the ones that do. */
	bool HasPolledPropertyBindings() const { return PolledBindingCount > 0; }

	/**
	 * Re-read every `each` source and refresh its list. A source that is a FieldNotify variable
	 * calls this for you when it broadcasts; a function source has nothing to broadcast, so code
	 * that changed what it returns calls this by hand.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|UserWidget")
	void RefreshEachBindings();

private:
	/** A binding with its lookups already done. Resolved once, at Initialize. */
	struct FResolvedBinding
	{
		/** The object the setter is called on -- the widget, its visual, or one of its behaviours. */
		TWeakObjectPtr<UObject> Target;
		UFunction* SourceFunction = nullptr;
		UFunction* Setter = nullptr;
		/**
		 * The source function's FieldNotify id on this class, when it has one. A valid id means the
		 * binding is subscription-driven; an invalid one means it stays on the per-frame poll.
		 */
		UE::FieldNotification::FFieldId SourceFieldId;
	};
	TArray<FResolvedBinding> ResolvedBindings;

	/** How many of ResolvedBindings carry no FieldNotify id and must be polled. */
	int32 PolledBindingCount = 0;

	/** Resolve the class's bindings against this instance's widgets. Silent: the compiler reported. */
	void ResolvePropertyBindings();
	/** Adds each compiled `Event -> Handler` route as a delegate on its live target. */
	void BindEventBindings();
	/** Wires each `each` block: template and data source onto the host's list view, one adapter per block. */
	void ResolveEachBindings();
	/** A FieldNotify array source broadcast: refresh the adapters reading that field. */
	void HandleEachSourceChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId);

	/** One per `each` block, kept alive here; the view holds them only as its data source interface. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UDreamUIEachAdapter>> EachAdapters;
	/** One binding, source through setter. Shared by the poll, the initial push and the broadcasts. */
	void EvaluateBinding(const FResolvedBinding& InBinding);
	/** Re-evaluates every binding whose source field just broadcast. */
	void HandleSourceFieldValueChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId);

	FDreamFieldNotificationDelegates NotificationDelegates;

	uint8 bInitialized : 1 = false;
};

/**
 * Register a freshly built hierarchy, and begin play on it when the world's UI manager already has.
 *
 * Building a hierarchy is not enough on its own: an unregistered widget is inert -- no layout, no
 * rendering, no behaviour lifecycle -- so a caller that only instanced a template would produce a
 * hierarchy that is structurally perfect and completely dead. Structure tests do not notice.
 *
 * CreateDreamWidget calls this for you. It is exposed for the one other caller that builds a
 * hierarchy by hand rather than from a class: the designer, whose preview comes from an authoring
 * tree. Bringing it to life through a second, parallel copy of these rules is how the two drift.
 */
DREAMGUI_API void RegisterDreamWidgetHierarchy(UDreamWidget* InRoot);

/**
 * Whether an editor should show what is inside this widget, or stop and treat it as one thing.
 *
 * A widget blueprint instance is a node in its host's hierarchy and expands its own contents when it
 * is initialized, so a live hierarchy is one flat run of widgets with no seam in it: the designer's
 * tree showed a music player as forty rows, most of them the innards of Buttons and Sliders that are
 * separate assets. Worse than long -- they are not editable here. Editing one has to mean editing the
 * class, which is what opening that asset is for; UMG draws exactly this line.
 *
 * The line: the OUTERMOST widget blueprint instance on a path is the one being edited, and everything
 * nested inside it is somebody else's asset. Asked of the widget rather than of the editor, so that
 * the hierarchy panel, viewport picking and the preview-to-template pairing cannot disagree about
 * where an instance ends -- three answers to this question is how "the panel folded it but you could
 * still click into it" happens.
 *
 * Not a visibility rule and not persisted: it changes nothing about what a hierarchy IS. The one
 * sanctioned hole in it is UDreamNamedSlot, and CollectDreamEditorChildren is where it is opened.
 */
DREAMGUI_API bool DreamWidget_ShouldEditorExpandContents(const UDreamWidget* InWidget);

/**
 * The children an editor shows under InWidget, which is not always the children it has.
 *
 * For anything but a nested widget blueprint instance this is simply Children. For a nested
 * instance it is the SLOTS its class declares -- one row each, named by the slot -- and nothing
 * else, because everything else in there belongs to another asset. What the host put in a slot then
 * hangs under that row and expands like any other widget, which it is: it lives in the host's tree.
 *
 * One function rather than a predicate each caller interprets: the hierarchy panel, viewport picking
 * and the walk that records per-widget designer state all have to agree about where an instance ends
 * and a hole begins, and three implementations of that is how "the panel folded it but you could
 * still click into it" happens.
 */
DREAMGUI_API void CollectDreamEditorChildren(UDreamWidget* InWidget, TArray<UDreamWidget*>& OutChildren);

/**
 * InRoot and its descendants as an editor sees them: everything down to, and including, each nested
 * widget blueprint instance, and nothing inside one.
 *
 * The counterpart to UDreamWidget::CollectChildrenWidgets, which walks the whole live hierarchy and
 * is the right answer for anything about what EXISTS -- bounds, reachability, registration. This is
 * the right answer for anything about what the author is editing, and the designer's per-widget
 * state is the reason it has to exist: hidden, locked and collapsed are recorded by object name, and
 * every asset's names start at DreamWidget_0, so walking into a nested instance files its widgets
 * under names the host also uses. Hiding a Button's inner Text hid an unrelated host widget.
 */
DREAMGUI_API void CollectDreamWidgetsToNestedBoundary(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets, bool bIncludeRoot = true);

/**
 * Copy a live widget subtree and bring the copy to life under InParent.
 *
 * InTemplate is used as the ARCHETYPE of a single NewObject: FObjectInstancingGraph follows the
 * Instanced properties (Children, the visual, the behaviours, the panel slot) and carries the whole
 * subtree across in one go. This is the same mechanism the compiler instantiates a class's hierarchy
 * with -- NewObject does not care whether its archetype is a template object or a live one.
 *
 * The copy is NOT attached in the source's sibling order and gets a generated object name; callers
 * that care about either say so afterwards. Display names are not made unique here either: what
 * "unique" means depends on where the copy landed, which only the caller knows.
 */
DREAMGUI_API UDreamWidget* DuplicateDreamWidgetHierarchy(UObject* InOuter, UDreamWidget* InTemplate, UDreamWidget* InParent);

/**
 * Create and initialize a user widget of InClass.
 *
 * A widget needs a tree to belong to, so a fresh UDreamWidgetTree is minted and outered to the world,
 * with the new widget as its root -- the same ownership a prefab load produces. Pass InParent to put
 * the widget into an existing hierarchy instead, in which case it joins that hierarchy's tree.
 *
 * InCallbackBeforeAlive runs after the hierarchy exists and is parented, but before it is registered
 * and before any behaviour's Awake. It is the counterpart of the prefab loader's CallbackBeforeAwake
 * and exists for the same reason: a caller that has to reshape what was built -- swapping the root
 * canvas, for one -- must do it before anything observes the old shape and caches it.
 *
 * Returns null if InClass is not a UDreamUserWidget, or if the world is invalid.
 */
DREAMGUI_API UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent = nullptr,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive = nullptr);

template<typename WidgetT>
WidgetT* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass = WidgetT::StaticClass(), UDreamWidget* InParent = nullptr,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive = nullptr)
{
	static_assert(TPointerIsConvertibleFromTo<WidgetT, const UDreamUserWidget>::Value,
		"'WidgetT' template parameter to CreateDreamWidget must be derived from UDreamUserWidget");
	return Cast<WidgetT>(CreateDreamWidget(InWorld, InClass, InParent, InCallbackBeforeAlive));
}
