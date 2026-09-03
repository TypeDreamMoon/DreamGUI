// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamFieldNotification.h"
#include "INotifyFieldValueChanged.h"
// The event bridge declared at the bottom of this header is a behaviour and SPEAKS the pointer
// interfaces, so these are base classes here, not forward-declarable references.
#include "Core/DreamUIBehaviour.h"
#include "Event/DreamPointerEventData.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
// FDreamUIAnimationHandle is passed and returned by value below, so it is a definition here rather
// than a forward declaration.
#include "Animation/DreamWidgetAnimationComponent.h"
#include "DreamUserWidget.generated.h"

class UDreamWidgetTree;
class UDreamUserWidget;
class UDreamUserWidgetEventBridge;

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
	 * The children this widget had BEFORE it made any of its own -- what a host nested on it.
	 *
	 * Taken at the top of Initialize, ahead of both roads that produce contents (an archetype
	 * instanced above, a native control's tree realized in NativeOnInitialized), which is what makes
	 * it a clean "not mine" with no control having to name its own root.
	 *
	 * Live only for the length of Initialize, and emptied once the default slot has taken its share
	 * so nothing reads a stale list a frame later. A class that needs the list for something other
	 * than a slot -- the tab view makes one TAB per nested page -- reads it from a hook that runs
	 * inside that window.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidget>> HostSuppliedChildren;

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

	// ---------------------------------------------------------------------------- animation

	/**
	 * Plays one of this widget's animations. The entry the compiler's generated animation
	 * variables feed: drag the variable in, drop it on Animation, leave Target as self.
	 *
	 * The animations live on UDreamWidgetAnimationComponents somewhere in this hierarchy rather
	 * than on the widget itself, which is an implementation detail no graph should have to walk --
	 * UMG puts PlayAnimation on UUserWidget for the same reason. Each overload here finds the
	 * component that actually owns the thing it was handed and forwards to it.
	 *
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimation(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	/** By display name, for callers that genuinely start from text. Prefer the animation variable. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationByName(
		const FString& Name,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	/**
	 * Plays an animation and stops it at EndAtTime rather than at its end.
	 * @param EndAtTime Absolute seconds into the animation where playback ends. Zero or less means the animation's own end.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationTimeRange(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		float EndAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	/**
	 * Plays an animation forward relative to its current state: an instance already running or
	 * paused turns around from where it is, otherwise a new one starts from the beginning. The
	 * "panel slides out on click, slides back on the next click" idiom, exactly as in UMG.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationForward(UMovieSceneSequence* Animation, float PlaybackSpeed = 1.0f, bool bRestoreState = false);

	/** The reverse half of PlayAnimationForward: turns a live instance around, or starts one from the end. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationReverse(UMovieSceneSequence* Animation, float PlaybackSpeed = 1.0f, bool bRestoreState = false);

	// The same operations deferred to the end of this frame's sequence evaluation: safe from inside
	// an animation's own Started / Finished / event callbacks.

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	void QueuePlayAnimation(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	void QueuePlayAnimationTimeRange(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		float EndAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void QueueStopAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void QueueStopAllAnimations();

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void QueuePauseAnimation(FDreamUIAnimationHandle Handle);

	/** @return the time the instance was at when paused, in seconds; feed it back to PlayAnimation's StartAtTime to resume from there. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	float PauseAnimation(FDreamUIAnimationHandle Handle);

	/** Continues a paused instance in the direction it was going. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void ResumeAnimation(FDreamUIAnimationHandle Handle);

	/** Ends the instance where it is. Its Finished delegates fire, the same as a natural end. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void StopAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void ReverseAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	bool IsAnimationPlaying(FDreamUIAnimationHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	bool IsAnimationPaused(FDreamUIAnimationHandle Handle) const;

	/** True while the instance runs towards its end; false when reversed. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	bool IsAnimationPlayingForward(FDreamUIAnimationHandle Handle) const;

	/** Seconds into the animation; zero for a handle that is no longer live. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	float GetAnimationCurrentTime(FDreamUIAnimationHandle Handle) const;

	/** Jumps the instance to a time in seconds without changing whether it is playing. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void SetAnimationCurrentTime(FDreamUIAnimationHandle Handle, float InTime);

	/** Changes how many times a live instance plays in total. Zero loops indefinitely. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void SetNumLoopsToPlay(FDreamUIAnimationHandle Handle, int32 NumLoopsToPlay);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void SetPlaybackSpeed(FDreamUIAnimationHandle Handle, float PlaybackSpeed = 1.0f);

	/** The newest live (playing or paused) instance of an animation, or an invalid handle. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	FDreamUIAnimationHandle FindAnimationInstance(UMovieSceneSequence* Animation) const;

	/** True if any instance of the animation is currently playing. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	bool HasPlayingAnimation(UMovieSceneSequence* Animation) const;

	/** Stops every live instance of the animation. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void StopAnimationsOf(UMovieSceneSequence* Animation);

	/** Pauses every live instance of the animation. @return the paused time of the newest one, in seconds. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	float PauseAnimationsOf(UMovieSceneSequence* Animation);

	/** True if any animation on any component of this widget's own contents is playing. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	bool IsAnyAnimationPlaying() const;

	/** Every animation on every component of this widget's own contents, nested instances excluded. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void StopAllAnimations();

	/** Applies any evaluation still queued for this widget's animations before returning. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void FlushAnimations();

	/** Called when an instance of the animation starts. Unbind with the same delegate. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void BindToAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void UnbindFromAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void UnbindAllFromAnimationStarted(UMovieSceneSequence* Animation);

	/** Called when an instance of the animation ends, naturally or by Stop. Unbind with the same delegate. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void BindToAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void UnbindFromAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void UnbindAllFromAnimationFinished(UMovieSceneSequence* Animation);

	/** The general form of the two above. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation")
	void BindToAnimationEvent(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate, EDreamUIAnimationEvent AnimationEvent);

	/** A DreamUI Event track key crossed while one of this widget's animations plays. */
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Animation")
	FDreamUIAnimEventDelegate OnAnimationEvent;

	/** The component-side entry points; each raises the matching overridable event below. */
	void NotifyAnimationStarted(UMovieSceneSequence* Animation);
	void NotifyAnimationFinished(UMovieSceneSequence* Animation);
	void NotifyAnimationEvent(FName EventName);

	/**
	 * The component holding InAnimation, or null. Exposed because a caller that wants the parts of
	 * the component API this widget does not mirror should not have to guess which widget carries it.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Animation")
	UDreamWidgetAnimationComponent* FindAnimationComponentFor(UMovieSceneSequence* InAnimation) const;

	/** The animation components on this widget's own contents, nested instances excluded. */
	void CollectAnimationComponents(TArray<UDreamWidgetAnimationComponent*>& OutComponents) const;

protected:
	/** An instance of one of this widget's animations started. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void OnAnimationStarted(UMovieSceneSequence* Animation);
	virtual void OnAnimationStarted_Implementation(UMovieSceneSequence* Animation) {}

	/** An instance of one of this widget's animations ended, naturally or by Stop. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "DreamGUI|Animation")
	void OnAnimationFinished(UMovieSceneSequence* Animation);
	virtual void OnAnimationFinished_Implementation(UMovieSceneSequence* Animation) {}

	/** FindAnimationComponentFor, logging on a miss in the caller's name. */
	UDreamWidgetAnimationComponent* RequireAnimationComponentFor(UMovieSceneSequence* InAnimation, const TCHAR* Caller) const;

public:

	/**
	 * The widget carrying the UDreamNamedSlot of that name inside this instance, or null.
	 *
	 * Two roads, because there are two kinds of contents. A class built from an archetype has a
	 * UDreamWidgetTree to walk. A native control has none -- nothing instanced a template to make
	 * its contents; it built them under itself in NativeOnInitialized -- so the walk is its own
	 * children, stopping at any nested instance, whose slots belong to that instance.
	 */
	UDreamWidget* FindSlotWidget(FName InSlotName) const;

	/**
	 * The holes this CLASS opens, for a class whose contents are code rather than an archetype.
	 *
	 * An archetype-built class declares its slots by putting a UDreamNamedSlot in its tree, and
	 * every consumer -- the compiler, the designer's hierarchy rows, the text builder -- reads them
	 * off that tree. A native control has no tree to read: it has none on the CDO, and none at all
	 * until it builds one. So the class itself has to be able to answer, before any instance exists.
	 *
	 * The names returned here are a promise: each must be the display name of a widget the control
	 * puts a UDreamNamedSlot on. This is the declaration and the built tree is the implementation;
	 * FindSlotWidget is what matches them, by name.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	virtual TArray<FName> GetNativeSlotNames() const;

	/**
	 * The slot that content arriving WITHOUT a slot name goes to -- .dui nesting, and a designer
	 * drop onto the control itself. None means unslotted content stays where it landed.
	 *
	 * Nesting is the one thing .dui does natively, so for the common case -- one hole, the obvious
	 * one -- it should not have to be spelled. `Native.Button OK { Image Icon {} Text {} }` puts
	 * both children inside the button because the button's default slot is its content area.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	virtual FName GetDefaultSlotName() const;

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
	 * Every slot name InClass offers, from BOTH roads: the UDreamNamedSlots in its archetype, and
	 * a native class's own GetNativeSlotNames.
	 *
	 * The overload every consumer should reach for. Asking the archetype alone was correct while
	 * only archetype-built classes could have slots at all, and is now the answer to a narrower
	 * question -- one that silently reports "no slots" for every native control.
	 */
	static void CollectDeclaredSlotNames(const UClass* InClass, TArray<FName>& OutNames);

	/**
	 * Hang what the host bound into the slots of that name inside this instance.
	 *
	 * Called for you at the end of Initialize, after NativeOnInitialized -- which is the earliest
	 * moment BOTH kinds of contents exist, and still before registration, so nothing lays out a
	 * half-filled shell. It used to live inside InitializeWidgetStatic, where it ran before a
	 * native control had built anything for the content to go into.
	 */
	void AttachNamedSlotContent();

	/**
	 * This widget's holes have just been filled -- the named bindings and whatever nesting handed to
	 * the default slot. Called at the end of Initialize, after NativeOnInitialized.
	 *
	 * A class cannot answer "is my content slot filled?" from NativeOnInitialized, because the
	 * content arrives after it. Anything that has to react to what a host supplied -- a stock label
	 * standing down for a supplied one, a header sizing itself around what is in it -- belongs here
	 * rather than in a first pass that is structurally too early to be right.
	 */
	virtual void NativeOnSlotContentAttached() {}

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

#pragma region BlueprintSurface
	/*
	 * THE BLUEPRINT-OVERRIDABLE SURFACE, and how it is wired.
	 *
	 * Every event below follows the NativeOnInitialized precedent exactly: a Native* virtual that a
	 * C++ subclass overrides (calling Super so the Blueprint event still fires), calling a
	 * BlueprintImplementableEvent of the same moment. What differs per group is which REAL seam feeds
	 * the Native* twin, because this framework routes everything through a widget's BEHAVIOURS:
	 * UDreamEventSystem::ExecuteDreamUIInterface iterates GetAllComponents() and never asks the widget
	 * object itself, the navigation search (UDreamPointerInputModule::Navigate) does the same, and the
	 * only per-frame update paths the manager owns are behaviour lists. A user widget therefore cannot
	 * simply implement the IDream* interfaces -- nothing would ever call them.
	 *
	 * The wiring is one hidden behaviour, UDreamUserWidgetEventBridge (bottom of this file), added at
	 * Initialize in GAME worlds only. It rides the ordinary behaviour lifecycle -- Awake/OnEnable/
	 * Tick/OnDisable/OnDestroy -- and implements the pointer, drag and navigation interfaces, each
	 * callback a one-line forward to the Native* twin here. Focus needs no bridge: UDreamWidget's own
	 * NotifyFocusReceived/NotifyFocusLost broadcasts feed two handlers this class binds on itself.
	 *
	 * Routing neutrality: adding the bridge must not change what any EXISTING screen receives. It does
	 * not, for reasons a reader can check in the dispatch code: who receives a pointer event is decided
	 * by the raycast and by ProcessPointerEvent's press/drag state machine, never by who implements an
	 * interface; the bubble walk only stops when a component RETURNS FALSE, and every bridge callback
	 * returns bAllowEventBubbleUp, default true; select/deselect routing (the one path that does search
	 * by interface -- GetEventHandle) is untouched because the bridge deliberately does not implement
	 * IDreamPointerSelectDeselectInterface; and the navigation search skips any component whose
	 * CanNavigateHere is false, which the bridge answers until bCanNavigateHere is opted in. The
	 * designer never sees the bridge at all -- edit-world widgets are skipped -- so authored component
	 * lists, previews and asset diffs are unchanged.
	 */
public:
	/**
	 * The widget has begun play: its hierarchy is registered, the manager has begun, and it is on
	 * screen. Rides the bridge's Awake, which UDreamWidget::BeginPlay drives -- so it fires once per
	 * begin-play regardless of the active flag, before the first OnEnable. Overriders call Super.
	 */
	virtual void NativeOnConstruct();

	/** The Blueprint half of NativeOnConstruct, called by it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Construct"))
	void OnConstruct();

	/**
	 * The widget is going away: EndPlay (DestroyWidget, or the world tearing down). Runs after the
	 * final NativeOnDisable, mirroring behaviour OnDestroy. Overriders call Super.
	 */
	virtual void NativeOnDestruct();

	/** The Blueprint half of NativeOnDestruct, called by it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Destruct"))
	void OnDestruct();

	/**
	 * WidgetActiveInHierarchy became true (or was true at begin play). The bridge's OnEnable, with
	 * the same contract as every behaviour's: after Construct, before the first tick.
	 */
	virtual void NativeOnEnable();

	/** The Blueprint half of NativeOnEnable, called by it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Enable"))
	void OnEnable();

	/** WidgetActiveInHierarchy became false, or the widget is being destroyed (before OnDestruct). */
	virtual void NativeOnDisable();

	/** The Blueprint half of NativeOnDisable, called by it. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Disable"))
	void OnDisable();

	/**
	 * Once per frame while bWantsTick is on and the widget is active. Costs nothing when off: the
	 * bridge only joins the manager's tick list when asked. The Blueprint event is additionally
	 * skipped when the Blueprint never implemented it, so a C++ override pays no ProcessEvent.
	 */
	virtual void NativeOnTick(float DeltaTime);

	/** The Blueprint half of NativeOnTick, called by it while ticking is opted in. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget", meta = (DisplayName = "On Tick"))
	void OnTick(float DeltaTime);

	/** True between NativeOnConstruct and NativeOnDestruct -- the widget is live on screen. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool IsConstructed() const { return bConstructed; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool GetWantsTick() const { return bWantsTick; }

	/** Opt this widget in or out of per-frame OnTick. Safe at any time, including from OnInitialized. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|UserWidget")
	void SetWantsTick(bool Value);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool GetAllowEventBubbleUp() const { return bAllowEventBubbleUp; }

	/** Whether pointer/drag events keep bubbling to ancestors after this widget has seen them. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|UserWidget")
	void SetAllowEventBubbleUp(bool Value) { bAllowEventBubbleUp = Value; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool GetCanNavigateHere() const { return bCanNavigateHere; }

	/** Opt this widget in as a gamepad/keyboard navigation stop. Off, navigation ignores it entirely. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|UserWidget")
	void SetCanNavigateHere(bool Value) { bCanNavigateHere = Value; }

	/*
	 * Pointer events. Delivered when this widget is the pointer's target, or when a descendant let the
	 * event bubble up -- the same delivery every behaviour gets. A control that consumes its events (a
	 * Button's UISelectable returns bubble-up false) stops them before they get here, exactly as it
	 * stops them reaching any other ancestor. Each Native* returns whether the event may continue
	 * bubbling PAST this widget; the default implementation fires the Blueprint event and returns
	 * bAllowEventBubbleUp. Overriders call Super and forward its return.
	 */
	virtual bool NativeOnPointerEnter(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Pointer", meta = (DisplayName = "On Pointer Enter"))
	void OnPointerEnter(UDreamPointerEventData* EventData);

	virtual bool NativeOnPointerExit(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Pointer", meta = (DisplayName = "On Pointer Exit"))
	void OnPointerExit(UDreamPointerEventData* EventData);

	virtual bool NativeOnPointerDown(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Pointer", meta = (DisplayName = "On Pointer Down"))
	void OnPointerDown(UDreamPointerEventData* EventData);

	virtual bool NativeOnPointerUp(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Pointer", meta = (DisplayName = "On Pointer Up"))
	void OnPointerUp(UDreamPointerEventData* EventData);

	virtual bool NativeOnPointerClick(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Pointer", meta = (DisplayName = "On Pointer Click"))
	void OnPointerClick(UDreamPointerEventData* EventData);

	/*
	 * Drag events. BeginDrag/Drag/EndDrag arrive on (or bubble up from) the widget being dragged;
	 * Drop arrives on the widget under the pointer when something ELSE is dropped onto it. Same
	 * bubble contract as the pointer events.
	 */
	virtual bool NativeOnBeginDrag(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Drag", meta = (DisplayName = "On Begin Drag"))
	void OnBeginDrag(UDreamPointerEventData* EventData);

	virtual bool NativeOnDrag(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Drag", meta = (DisplayName = "On Drag"))
	void OnDrag(UDreamPointerEventData* EventData);

	virtual bool NativeOnEndDrag(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Drag", meta = (DisplayName = "On End Drag"))
	void OnEndDrag(UDreamPointerEventData* EventData);

	virtual bool NativeOnDrop(UDreamPointerEventData* EventData);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Drag", meta = (DisplayName = "On Drop"))
	void OnDrop(UDreamPointerEventData* EventData);

	/*
	 * Focus. UDreamWidget already broadcasts OnFocusReceived/OnFocusLost delegates from
	 * NotifyFocusReceived/NotifyFocusLost; this class listens to its own broadcasts and forwards.
	 * The Blueprint events carry Receive* C++ names because the delegate PROPERTIES of those names
	 * already exist on UDreamWidget; their Blueprint faces read "On Focus Received"/"On Focus Lost".
	 */
	virtual void NativeOnFocusReceived(int32 UserIndex, int32 PointerId);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Focus", meta = (DisplayName = "On Focus Received"))
	void ReceiveFocusReceived(int32 UserIndex, int32 PointerId);

	virtual void NativeOnFocusLost(int32 UserIndex, int32 PointerId);
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Focus", meta = (DisplayName = "On Focus Lost"))
	void ReceiveFocusLost(int32 UserIndex, int32 PointerId);

	/**
	 * A directional navigation move while this widget holds the navigation highlight. Only reachable
	 * after SetCanNavigateHere(true) -- an opted-out widget is invisible to the navigation search.
	 * Fill OutNextWidget to send navigation somewhere else (the target needs a navigation-capable
	 * behaviour, a UISelectable being the ordinary one); leave it null to keep the highlight here.
	 * The default implementation asks the Blueprint event.
	 */
	virtual void NativeOnNavigate(EDreamUINavigationDirection Direction, UDreamWidget*& OutNextWidget);

	/** The Blueprint half of NativeOnNavigate: return the widget to move to, or nothing to stay. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI|UserWidget|Focus", meta = (DisplayName = "On Navigate"))
	UDreamWidget* OnNavigate(EDreamUINavigationDirection Direction);
#pragma endregion

private:
	/**
	 * Move content that arrived with no slot name into the default slot.
	 *
	 * Reads HostSuppliedChildren, which is the children this widget had BEFORE it made any of its
	 * own. That ordering is the whole trick: .dui nesting and a designer drop attach to the control
	 * while it is still empty, so "was already here" and "is not mine" are the same set, and no
	 * control has to name its own root to be told apart from its guests.
	 *
	 * Anything a named binding already claimed has been re-parented into its slot by then and is no
	 * longer a child of this widget, so it is skipped without a second rule.
	 */
	void AdoptUnslottedChildren();

	/**
	 * Add the event bridge to this instance, once, in game worlds only. Edit worlds (the designer's
	 * preview above all) are skipped so authored component lists, panels and diffs never see it; a
	 * duplicated instance already carries a copy through the Instanced Components array, which the
	 * idempotence check honours instead of stacking a second one.
	 */
	void EnsureEventBridge();

	/** Bound to this widget's own OnFocusReceived broadcast; forwards to NativeOnFocusReceived. */
	UFUNCTION()
	void HandleFocusReceivedBroadcast(int32 UserIndex, int32 PointerId);

	/** Bound to this widget's own OnFocusLost broadcast; forwards to NativeOnFocusLost. */
	UFUNCTION()
	void HandleFocusLostBroadcast(int32 UserIndex, int32 PointerId);

	/**
	 * Per-frame OnTick, off by default: the cost of a tick is a manager list entry, a virtual call and
	 * a Blueprint ProcessEvent every frame, which no widget should pay for a surface it never uses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI|UserWidget", Getter = "GetWantsTick", Setter = "SetWantsTick", meta = (AllowPrivateAccess = true, DisplayName = "Wants Tick"))
	bool bWantsTick = false;

	/**
	 * The bubble policy every pointer/drag Native* returns by default. True, unlike UISelectable's
	 * false: a user widget is a container, and a container that silently swallowed every click inside
	 * it would break each screen it was placed on. Set false to consume events at this boundary.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI|UserWidget", Getter = "GetAllowEventBubbleUp", Setter = "SetAllowEventBubbleUp", meta = (AllowPrivateAccess = true, DisplayName = "Allow Event Bubble Up"))
	bool bAllowEventBubbleUp = true;

	/**
	 * Whether this widget itself is a navigation stop. Off by default so the navigation search walks
	 * past it exactly as it always has -- a user widget full of UISelectables must not steal their
	 * moves. Opting in makes OnNavigate reachable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI|UserWidget", Getter = "GetCanNavigateHere", Setter = "SetCanNavigateHere", meta = (AllowPrivateAccess = true, DisplayName = "Can Navigate Here"))
	bool bCanNavigateHere = false;

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
	/** Set by NativeOnConstruct, cleared by NativeOnDestruct. See IsConstructed. */
	uint8 bConstructed : 1 = false;
	/**
	 * The Blueprint actually implemented OnTick, as opposed to merely being able to. Same check
	 * UDreamUIBehaviour makes for ReceiveTick, for the same reason: a per-frame ProcessEvent for an
	 * event nobody wrote is pure cost. Cached at Initialize.
	 */
	uint8 bHasBlueprintOnTick : 1 = false;
};

/**
 * The hidden behaviour that connects a UDreamUserWidget to the seams this framework actually routes
 * through. Everything about events and lifecycle here is component-shaped: the event system's bubble
 * walk (UDreamEventSystem::ExecuteDreamUIInterface), the navigation search and the manager's
 * start/tick lists all speak to a widget's BEHAVIOURS and never to the widget object itself -- so the
 * user widget's Blueprint surface is fed by this one bridge rather than by interfaces on the widget,
 * which nothing would ever call.
 *
 * Runtime-only and invisible to authors: UDreamUserWidget::EnsureEventBridge adds it in game worlds
 * at Initialize, transient, and the designer's edit worlds never get one. Every callback is a
 * one-line forward to the owning widget's Native* twin; every pointer callback returns the widget's
 * bAllowEventBubbleUp so a widget with no Blueprint bodies is routing-identical to one without a
 * bridge. It implements neither IDreamPointerSelectDeselectInterface (GetEventHandle searches by that
 * one, and finding the bridge would move focus/deselect decisions onto every user widget) nor the
 * scroll interface; navigation is present but answers CanNavigateHere false until the widget opts in.
 *
 * The ForwardCount fields are introspection for tests and diagnostics -- they count forwards that
 * COMPLETED, so a count is proof the widget-side Native* ran.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown, DisplayName = "User Widget Event Bridge")
class DREAMGUI_API UDreamUserWidgetEventBridge : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerClickInterface
	, public IDreamPointerDragInterface
	, public IDreamPointerDragDropInterface
	, public IDreamNavigationInterface
{
	GENERATED_BODY()
public:
	UDreamUserWidgetEventBridge();

	/** The owning user widget: this bridge is outered to it, as every behaviour is to its widget. */
	UDreamUserWidget* GetUserWidget() const;

	/**
	 * Align tick participation with the widget's bWantsTick, through whichever door matches the
	 * current lifecycle state: registration add/remove once enabled, a bare flag before that.
	 */
	void SyncTickEnabled(bool bValue);

	/** True while this bridge would forward a tick if the manager delivered one. */
	bool IsTickForwardingEnabled() const { return bCanExecuteTick; }

	/** Completed forwards into the widget's Native* seams. Introspection; never reset. */
	int32 ConstructForwardCount = 0;
	int32 DestructForwardCount = 0;
	int32 EnableForwardCount = 0;
	int32 DisableForwardCount = 0;
	int32 TickForwardCount = 0;

protected:
	//~ Behaviour lifecycle -> widget lifecycle
	virtual void Awake() override;
	virtual void OnEnable() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnDisable() override;
	virtual void OnDestroy() override;

	//~ IDreamPointerEnterExitInterface
	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override;
	//~ IDreamPointerDownUpInterface
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override;
	//~ IDreamPointerClickInterface
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData) override;
	//~ IDreamPointerDragInterface
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;
	//~ IDreamPointerDragDropInterface
	virtual bool OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData) override;
	//~ IDreamNavigationInterface
	virtual bool CanNavigateHere_Implementation() const override;
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result) override;
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
